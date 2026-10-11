/*
 * Two ways in without a terminal: an icon in the system tray, and a
 * preferences window.
 *
 * The menu on the icon holds the two things worth doing in one click --
 * pause and resume, and quit -- and an entry that opens the window.
 * Everything else is in the window.  The split is not only tidiness: a
 * StatusNotifierItem menu travels over dbusmenu, which carries item
 * properties rather than widgets, so a volume there could only ever be a
 * ladder of radio items, a device list has to be rebuilt as a submenu
 * whenever ALC's answer changes, and the panel decides when the menu closes.
 * A window has a slider, a combo box and none of that machinery.
 *
 * The icon is a StatusNotifierItem, put up through libayatana-appindicator.
 * That is what KDE shows natively and what the AppIndicator extension shows
 * under GNOME Shell; where nothing is listening the item simply never
 * appears, which is why tray_init() settling for a display is as far as the
 * checking can usefully go.
 *
 * Everything here runs on the main thread, which owns the GTK loop, and
 * reaches the rest of the program only through the buckle_* calls in
 * buckle.h, each of which takes the audio lock for itself.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <signal.h>
#include <stdarg.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gio/gio.h>
#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif
#include <libayatana-appindicator/app-indicator.h>

#include "buckle.h"

#define VOLUME_STEP 10
#define DEFAULT_MUTE_KEYCODE 0x46 /* Scroll Lock */

static GtkWidget *item_mute;

/*
 * The controls window: built the first time it is asked for, and from then on
 * kept and hidden rather than destroyed, so that it comes back where the user
 * left it.  window_show() says why it exists at all.
 */

static GtkWidget *window;
static GtkWidget *win_mute;
static GtkWidget *win_volume;
static GtkWidget *win_devices;
static GtkWidget *win_hrtf;
static GtkWidget *win_hrtf_state;

static void window_sync(void);
static void window_show(void);
static void window_refill_devices(void);

static int syncing = 0;


/*
 * Two presses of the mute key inside two seconds toggle the sound, so the
 * same sequence does both halves of pause and resume.  Name the key only
 * when it is the one the label can be sure of.
 */

static const char *mute_sequence(void)
{
	return buckle_mute_keycode() == DEFAULT_MUTE_KEYCODE
		? "ScrollLock \303\227 2"
		: "mute key \303\227 2";
}


/*
 * What a setting that will not take says for itself.  These come from the
 * window's own controls, so the window is the parent while it is up; a modal
 * dialog with no parent is all that can be done without it.
 */

static void warn_dialog(const char *fmt, ...) G_GNUC_PRINTF(1, 2);

static void warn_dialog(const char *fmt, ...)
{
	GtkWindow *parent = NULL;
	GtkWidget *dialog;
	va_list ap;
	char text[512];

	if(window != NULL && gtk_widget_get_visible(window)) {
		parent = GTK_WINDOW(window);
	}

	va_start(ap, fmt);
	g_vsnprintf(text, sizeof text, fmt, ap);
	va_end(ap);

	dialog = gtk_message_dialog_new(parent, 0, GTK_MESSAGE_WARNING,
			GTK_BUTTONS_CLOSE, "%s", text);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}


static void sync_menu(void)
{
	char label[128];

	g_snprintf(label, sizeof label, "%s  (%s)",
			buckle_muted() ? "Resume" : "Pause", mute_sequence());
	gtk_menu_item_set_label(GTK_MENU_ITEM(item_mute), label);

	window_sync();
}


/* The mute key works whether or not anyone is looking at the menu, so the
 * label is worth re-reading rather than only writing when it is clicked. */

static gboolean on_tick(gpointer data)
{
	(void)data;
	sync_menu();
	return G_SOURCE_CONTINUE;
}


static void on_mute(GtkMenuItem *item, gpointer data)
{
	(void)item;
	(void)data;
	buckle_set_muted(!buckle_muted());
	sync_menu();
}


/* A wheel over the icon, which is the nearest thing to a slider that a tray
 * icon has, and does not involve opening the menu at all. */

static void on_scroll(AppIndicator *indicator, gint delta,
		GdkScrollDirection direction, gpointer data)
{
	int step;

	(void)indicator;
	(void)data;

	if(delta < 1) {
		delta = 1;
	}

	switch(direction) {
		case GDK_SCROLL_UP:
		case GDK_SCROLL_RIGHT:
			step = VOLUME_STEP;
			break;
		case GDK_SCROLL_DOWN:
		case GDK_SCROLL_LEFT:
			step = -VOLUME_STEP;
			break;
		default:
			return;
	}

	buckle_set_gain(buckle_gain() + step * delta);
	sync_menu();
}


static void on_about(GtkWidget *item, gpointer data)
{
	(void)item;
	(void)data;

	char comments[512];

	g_snprintf(comments, sizeof comments,
		"The sound of an IBM Model M buckling spring keyboard, played "
		"back for every key you press.\n\n"
		"Two presses of %s pause and resume it; the same is on this menu. "
		"The mouse buttons and the scroll wheel click too, and a key held "
		"down clicks for every repeat.\n\n"
		"Run with --help for the rest.",
		buckle_mute_keycode() == DEFAULT_MUTE_KEYCODE
			? "Scroll Lock" : "the mute key");

	gtk_show_about_dialog(NULL,
		"program-name", "Bucklespring",
		"version", PACKAGE_VERSION,
		"comments", comments,
		"copyright", "Copyright \302\251 2016-2025 Ico Doornekamp",
		"license-type", GTK_LICENSE_GPL_2_0,
		"website", PACKAGE_URL,
		"logo-icon-name", "input-keyboard",
		NULL);
}


static void on_preferences(GtkWidget *item, gpointer data)
{
	(void)item;
	(void)data;
	window_show();
}


static void on_quit(GtkWidget *item, gpointer data)
{
	(void)item;
	(void)data;
	buckle_quit();
}


static GtkWidget *menu_item(GtkWidget *menu, const char *label, GCallback cb,
		gpointer data)
{
	GtkWidget *item = gtk_menu_item_new_with_label(label);

	if(cb != NULL) {
		g_signal_connect(item, "activate", cb, data);
	}
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	return item;
}


static void menu_separator(GtkWidget *menu)
{
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
}


/*
 * The controls, which share one guard: setting a widget to what the program
 * already thinks must not read back as the user asking for it.
 */

static void on_win_mute(GtkButton *button, gpointer data)
{
	(void)button;
	(void)data;
	buckle_set_muted(!buckle_muted());
	sync_menu();
}


static void on_win_volume(GtkRange *range, gpointer data)
{
	(void)data;

	if(syncing) {
		return;
	}
	buckle_set_gain((int)gtk_range_get_value(range));
	sync_menu();
}


static void on_win_device(GtkComboBox *combo, gpointer data)
{
	char *name;

	(void)data;

	if(syncing) {
		return;
	}

	name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
	if(name != NULL && g_strcmp0(name, buckle_audio_device()) != 0
			&& buckle_set_audio_device(name) < 0) {
		warn_dialog("Could not switch to the audio device \"%s\".", name);
	}
	g_free(name);
}


/* The three entries go in in the order of the HRTF_* values, so that the row
 * chosen is the setting asked for. */

_Static_assert(HRTF_AUTO == 0 && HRTF_ON == 1 && HRTF_OFF == 2,
		"the HRTF combo box is filled in the order of the settings");

static void on_win_hrtf(GtkComboBox *combo, gpointer data)
{
	int setting = gtk_combo_box_get_active(combo);

	(void)data;

	if(syncing || setting < 0) {
		return;
	}
	if(buckle_set_hrtf(setting) < 0) {
		warn_dialog("This OpenAL cannot change its HRTF setting.");
	}
	sync_menu();
}


static void on_win_close(GtkButton *button, gpointer data)
{
	(void)button;
	(void)data;
	gtk_widget_hide(window);
}


/*
 * A list that has changed under us, as the audio devices do.  Rebuilt rather
 * than merged: it is a handful of entries and the combo box has no notion of
 * a diff.
 */

static void window_fill_combo(GtkWidget *combo, char **list,
		const char *current, const char *empty)
{
	int saved = syncing;
	size_t i;

	syncing = 1;
	gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(combo));

	for(i = 0; list != NULL && list[i] != NULL; i++) {
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), list[i]);
		if(g_strcmp0(list[i], current) == 0) {
			gtk_combo_box_set_active(GTK_COMBO_BOX(combo), (int)i);
		}
	}

	/* Nothing to choose between is said rather than left blank, and there
	 * is then nothing to choose. */

	if(i == 0) {
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), empty);
		gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
	}
	gtk_widget_set_sensitive(combo, i > 0);

	syncing = saved;
}


static void window_refill_devices(void)
{
	char **list;

	if(window == NULL) {
		return;
	}

	list = buckle_audio_devices();
	window_fill_combo(win_devices, list, buckle_audio_device(),
			"(none found)");
	buckle_audio_devices_free(list);
}


static void window_sync(void)
{
	int saved = syncing;
	char label[128];

	if(window == NULL) {
		return;
	}

	syncing = 1;

	g_snprintf(label, sizeof label, "%s  (%s)",
			buckle_muted() ? "Resume" : "Pause", mute_sequence());
	gtk_button_set_label(GTK_BUTTON(win_mute), label);

	gtk_range_set_value(GTK_RANGE(win_volume), buckle_gain());

	if(win_hrtf != NULL) {
		int active = buckle_hrtf_active();

		gtk_combo_box_set_active(GTK_COMBO_BOX(win_hrtf),
				buckle_hrtf_setting());
		gtk_label_set_text(GTK_LABEL(win_hrtf_state),
				active < 0 ? "unavailable" :
				active ? "in use" : "not in use");
	}

	syncing = saved;
}


/* A label on the left and the control on the right, which is every row of
 * the grid. */

static void window_row(GtkWidget *grid, int row, const char *text,
		GtkWidget *control)
{
	GtkWidget *label = gtk_label_new(text);

	gtk_widget_set_halign(label, GTK_ALIGN_START);
	gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), control, 1, row, 1, 1);
}


static GtkWidget *window_button(GtkWidget *box, const char *label, GCallback cb)
{
	GtkWidget *button = gtk_button_new_with_label(label);

	g_signal_connect(button, "clicked", cb, NULL);
	gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);

	return button;
}


static void window_build(void)
{
	GtkWidget *box;
	GtkWidget *grid;
	GtkWidget *buttons;
	int row = 0;

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), "Bucklespring Preferences");
	gtk_window_set_icon_name(GTK_WINDOW(window), "input-keyboard");
	gtk_window_set_resizable(GTK_WINDOW(window), FALSE);

	/* Closing the window is not quitting: the clicks go on, and the
	 * window can be asked for again. */

	g_signal_connect(window, "delete-event",
			G_CALLBACK(gtk_widget_hide_on_delete), NULL);

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_set_border_width(GTK_CONTAINER(box), 12);
	gtk_container_add(GTK_CONTAINER(window), box);

	win_mute = gtk_button_new_with_label("Pause");
	g_signal_connect(win_mute, "clicked", G_CALLBACK(on_win_mute), NULL);
	gtk_box_pack_start(GTK_BOX(box), win_mute, FALSE, FALSE, 0);

	grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
	gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);

	/* The one control here that the menu cannot have: dbusmenu carries
	 * labels and check marks, and a window can carry a slider. */

	win_volume = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
			0, 100, VOLUME_STEP / 2);
	gtk_scale_set_digits(GTK_SCALE(win_volume), 0);
	gtk_scale_set_value_pos(GTK_SCALE(win_volume), GTK_POS_RIGHT);
	gtk_widget_set_size_request(win_volume, 240, -1);
	gtk_widget_set_hexpand(win_volume, TRUE);
	g_signal_connect(win_volume, "value-changed",
			G_CALLBACK(on_win_volume), NULL);
	window_row(grid, row++, "Volume", win_volume);

	win_devices = gtk_combo_box_text_new();
	g_signal_connect(win_devices, "changed", G_CALLBACK(on_win_device), NULL);
	window_row(grid, row++, "Audio device", win_devices);

	/* Shown whenever there is an --hrtf built in, with what OpenAL
	 * settled on beside it, and greyed where the device will not do it. */

	if(buckle_hrtf_supported()) {
		GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

		win_hrtf = gtk_combo_box_text_new();
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(win_hrtf),
				"Automatic");
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(win_hrtf), "On");
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(win_hrtf), "Off");
		g_signal_connect(win_hrtf, "changed",
				G_CALLBACK(on_win_hrtf), NULL);
		gtk_box_pack_start(GTK_BOX(hbox), win_hrtf, FALSE, FALSE, 0);

		win_hrtf_state = gtk_label_new(NULL);
		gtk_widget_set_sensitive(win_hrtf_state, FALSE);
		gtk_box_pack_start(GTK_BOX(hbox), win_hrtf_state, FALSE, FALSE, 0);

		if(buckle_hrtf_active() < 0) {
			gtk_widget_set_sensitive(win_hrtf, FALSE);
		}

		window_row(grid, row++, "Headphone 3D", hbox);
	}

	gtk_box_pack_start(GTK_BOX(box),
			gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
			FALSE, FALSE, 0);

	buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign(buttons, GTK_ALIGN_END);
	gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);

	window_button(buttons, "About", G_CALLBACK(on_about));
	window_button(buttons, "Quit", G_CALLBACK(on_quit));
	window_button(buttons, "Close", G_CALLBACK(on_win_close));
}


/*
 * The window, on request.
 *
 * A tray icon exists only where something hosts it.  KDE Plasma does, and
 * GNOME Shell does once the AppIndicator extension is installed, but a plain
 * GNOME Shell or a bare compositor has nowhere to put one, and then the entry
 * on the menu is out of reach along with the icon it hangs from.
 *
 * It is not put up by itself: an unasked-for window at login would be worse
 * than no icon.  It is asked for, by "buckle --preferences" through the
 * session bus below, by the action on the desktop file, which runs that, or
 * by SIGUSR1, for a keyboard shortcut bound to a bare
 * "pkill -USR1 buckle-libinput".
 */

static void window_show(void)
{
	if(window == NULL) {
		window_build();
	}

	/* Nothing asks ALC while the window is down, so the list is read
	 * again every time it goes up. */

	window_refill_devices();

	window_sync();
	gtk_widget_show_all(window);
	gtk_window_present(GTK_WINDOW(window));

	printd("Controls window shown");
}


/*
 * Being asked.  A name on the session bus is what lets a second invocation
 * reach the running one: a desktop file action or a desktop's keyboard
 * shortcut can run a command, but cannot reach into a process that is already
 * there, and the window has to be the running program's own or it would have
 * nothing to show.
 */

#define BUCKLE_BUS_NAME "nl.zevv.Bucklespring"
#define BUCKLE_BUS_PATH "/nl/zevv/Bucklespring"

static const char bus_introspection[] =
	"<node>"
	"  <interface name='" BUCKLE_BUS_NAME "'>"
	"    <method name='Show'/>"
	"  </interface>"
	"</node>";

static void on_bus_call(GDBusConnection *conn, const char *sender,
		const char *path, const char *iface, const char *method,
		GVariant *params, GDBusMethodInvocation *invocation,
		gpointer data)
{
	(void)conn;
	(void)sender;
	(void)path;
	(void)iface;
	(void)params;
	(void)data;

	if(g_strcmp0(method, "Show") == 0) {
		window_show();
		g_dbus_method_invocation_return_value(invocation, NULL);
		return;
	}

	g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
			G_DBUS_ERROR_UNKNOWN_METHOD, "No method \"%s\"", method);
}


static void on_bus_acquired(GDBusConnection *conn, const char *name,
		gpointer data)
{
	static const GDBusInterfaceVTable vtable = {
		on_bus_call, NULL, NULL, { NULL }
	};
	GDBusNodeInfo *info;
	GError *err = NULL;

	(void)name;
	(void)data;

	info = g_dbus_node_info_new_for_xml(bus_introspection, &err);
	if(info == NULL) {
		printd("Cannot parse the bus interface: %s", err->message);
		g_error_free(err);
		return;
	}

	if(g_dbus_connection_register_object(conn, BUCKLE_BUS_PATH,
			info->interfaces[0], &vtable, NULL, NULL, &err) == 0) {
		printd("Cannot answer on the session bus: %s", err->message);
		g_error_free(err);
	}

	g_dbus_node_info_unref(info);
}


/* Two of these running at once is not an error: the first one keeps the name
 * and --preferences reaches that one. */

static void on_name_lost(GDBusConnection *conn, const char *name, gpointer data)
{
	(void)conn;
	(void)data;

	printd("Another bucklespring already answers to %s, so --preferences will "
			"reach that one", name);
}


#ifdef G_OS_UNIX
static gboolean on_sigusr1(gpointer data)
{
	(void)data;
	window_show();
	return G_SOURCE_CONTINUE;
}
#endif


/*
 * The other half of --preferences, in the process that was started to ask.
 * It talks to the bus and not to the window, so it needs no display of its
 * own and does not care which flavour is running.
 */

int tray_show_running(void)
{
	GDBusConnection *conn;
	GVariant *reply;
	GError *err = NULL;

	conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
	if(conn == NULL) {
		g_printerr("No session bus to ask: %s\n", err->message);
		g_error_free(err);
		return -1;
	}

	/* NO_AUTO_START: there is no service file to start, and the point of
	 * the call is to reach the program that is already running. */

	reply = g_dbus_connection_call_sync(conn, BUCKLE_BUS_NAME,
			BUCKLE_BUS_PATH, BUCKLE_BUS_NAME, "Show", NULL, NULL,
			G_DBUS_CALL_FLAGS_NO_AUTO_START, 5000, NULL, &err);
	if(reply == NULL) {
		g_printerr("No running bucklespring answered: %s\n",
				err->message);
		g_printerr("Start one first, or give it a display to put the "
				"window on.\n");
		g_error_free(err);
		g_object_unref(conn);
		return -1;
	}

	g_variant_unref(reply);
	g_object_unref(conn);

	return 0;
}


/*
 * An AppIndicator that does not fall back to a GtkStatusIcon.
 *
 * With no StatusNotifierItem host on the session bus -- a GNOME Shell without
 * the AppIndicator extension, say -- libayatana-appindicator puts up a
 * GtkStatusIcon instead.  That is an XEmbed icon in a panel's notification
 * area, which GTK3 builds for the X11 backend only: on Wayland the widget
 * behind it is never created, and the library's attempt to dress the icon
 * walks into gtk_widget_get_scale_factor(NULL), which leaves a Gtk-CRITICAL
 * in the journal and nothing on the screen.  Where the fallback cannot work
 * it is declined, which is the same outcome without the complaint.
 */

#define BUCKLE_TYPE_INDICATOR (buckle_indicator_get_type())

typedef struct { AppIndicator parent; } BuckleIndicator;
typedef struct { AppIndicatorClass parent_class; } BuckleIndicatorClass;

G_DEFINE_TYPE(BuckleIndicator, buckle_indicator, APP_INDICATOR_TYPE)

static GtkStatusIcon *buckle_indicator_fallback(AppIndicator *self)
{
#ifdef GDK_WINDOWING_X11
	GdkDisplay *display = gdk_display_get_default();

	if(display != NULL && GDK_IS_X11_DISPLAY(display)) {
		return APP_INDICATOR_CLASS(buckle_indicator_parent_class)
				->fallback(self);
	}
#else
	(void)self;
#endif

	printd("No StatusNotifierItem host on the bus and no X11 "
			"notification area to fall back to, so no tray icon; "
			"\"--preferences\" opens a window with the rest "
			"of the settings");

	return NULL;
}


static void buckle_indicator_class_init(BuckleIndicatorClass *klass)
{
	APP_INDICATOR_CLASS(klass)->fallback = buckle_indicator_fallback;
}


static void buckle_indicator_init(BuckleIndicator *self)
{
	(void)self;
}


int tray_init(void)
{
	AppIndicator *indicator;
	GtkWidget *menu;

	/* Not an error: a console session has no display to put an icon on,
	 * and the program is perfectly usable without one. */

	if(!gtk_init_check(NULL, NULL)) {
		return 0;
	}

	menu = gtk_menu_new();

	item_mute = menu_item(menu, "Pause", G_CALLBACK(on_mute), NULL);

	menu_separator(menu);

	menu_item(menu, "Preferences", G_CALLBACK(on_preferences), NULL);
	menu_item(menu, "Quit Bucklespring " PACKAGE_VERSION,
			G_CALLBACK(on_quit), NULL);

	gtk_widget_show_all(menu);

	/*
	 * app_indicator_new() is deprecated in favour of the GLib-only
	 * libayatana-appindicator-glib, and warns to that effect on every
	 * start.  The advice cannot be taken yet: the new library carries its
	 * menu over org.gtk.Menus, where the panels this icon has to appear in
	 * -- the Plasma tray, the GNOME Shell AppIndicator extension -- read
	 * it over com.canonical.dbusmenu, which is what this one speaks.
	 *
	 * All the deprecated call does is g_object_new() with these three
	 * properties, and the subclass above has to be constructed that way
	 * in any case.  "Hardware" is the category's name on the wire, from
	 * the StatusNotifierItem specification.
	 */

	indicator = g_object_new(BUCKLE_TYPE_INDICATOR,
			"id", PACKAGE_NAME,
			"category", "Hardware",
			"icon-name", "input-keyboard",
			NULL);
	app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
	app_indicator_set_title(indicator, "Bucklespring");
	app_indicator_set_menu(indicator, GTK_MENU(menu));
	g_signal_connect(indicator, APP_INDICATOR_SIGNAL_SCROLL_EVENT,
			G_CALLBACK(on_scroll), NULL);

	/*
	 * What answers --preferences, and what a keyboard shortcut can poke
	 * without any dbus tooling about it.  Neither needs the icon to have
	 * worked, which is the point of them.
	 */

	g_bus_own_name(G_BUS_TYPE_SESSION, BUCKLE_BUS_NAME,
			G_BUS_NAME_OWNER_FLAGS_NONE, on_bus_acquired, NULL,
			on_name_lost, NULL, NULL);

#ifdef G_OS_UNIX
	g_unix_signal_add(SIGUSR1, on_sigusr1, NULL);
#endif

	sync_menu();
	g_timeout_add(500, on_tick, NULL);

	return 1;
}


void tray_run(void)
{
	gtk_main();
}
