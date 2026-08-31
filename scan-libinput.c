#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/poll.h>
#include <string.h>
#include <errno.h>

#include <libinput.h>
#include <linux/input-event-codes.h>

#include "buckle.h"


static int open_restricted(const char *path, int flags, void *user_data)
{
	int fd = open(path, flags);

	if(fd < 0) {
		fprintf(stderr, "Failed to open %s (%s)\n", path, strerror(errno));
	}

	return fd < 0 ? -errno : fd;
}


static void close_restricted(int fd, void *user_data)
{
	close(fd);
}


static const struct libinput_interface interface = {
	.open_restricted = open_restricted,
	.close_restricted = close_restricted,
};


static void handle_key(struct libinput_event *ev)
{
	struct libinput_event_keyboard *k = libinput_event_get_keyboard_event(ev);
	enum libinput_key_state state = libinput_event_keyboard_get_key_state(k);
	uint32_t key = libinput_event_keyboard_get_key(k);

	play(key, state == LIBINPUT_KEY_STATE_PRESSED);
}

static void handle_button(struct libinput_event *ev)
{
	struct libinput_event_pointer *p = libinput_event_get_pointer_event(ev);
	enum libinput_button_state state = libinput_event_pointer_get_button_state(p);
	uint32_t button = libinput_event_pointer_get_button(p);

	if(button == BTN_LEFT || button == BTN_RIGHT)
		play(0xff, state == LIBINPUT_BUTTON_STATE_PRESSED);
}

/*
 * A wheel detent is 120 units, the v120 convention libinput took from Windows.
 * A high resolution wheel reports fractions of that, so what is left over is
 * carried to the next event rather than rounded away.  Unlike a button there
 * is no press and release to follow, so a detent makes a single click.
 *
 * Only a real wheel is handled, not LIBINPUT_EVENT_POINTER_SCROLL_FINGER or
 * _CONTINUOUS: two fingers on a touchpad scroll smoothly rather than in
 * detents, and there is nothing there to click once per.
 */

#define WHEEL_DETENT 120

static void handle_scroll_axis(struct libinput_event_pointer *p,
		enum libinput_pointer_axis axis, int *acc)
{
	double v120;

	if(!libinput_event_pointer_has_axis(p, axis)) {
		return;
	}

	v120 = libinput_event_pointer_get_scroll_value_v120(p, axis);
	*acc += (int)(v120 < 0 ? -v120 : v120);

	while(*acc >= WHEEL_DETENT) {
		play(0xff, 1);
		*acc -= WHEEL_DETENT;
	}
}


static void handle_scroll(struct libinput_event *ev)
{
	static int acc_vert = 0;
	static int acc_horz = 0;

	struct libinput_event_pointer *p = libinput_event_get_pointer_event(ev);

	handle_scroll_axis(p, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, &acc_vert);
	handle_scroll_axis(p, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL, &acc_horz);
}


static void handle_events(struct libinput *li)
{
	struct libinput_event *ev;
		
	libinput_dispatch(li);

	while((ev = libinput_get_event(li))) {

		switch(libinput_event_get_type(ev)) {
			case LIBINPUT_EVENT_KEYBOARD_KEY:
				handle_key(ev);
				break;
			case LIBINPUT_EVENT_POINTER_BUTTON:
				handle_button(ev);
				break;
			case LIBINPUT_EVENT_POINTER_SCROLL_WHEEL:
				handle_scroll(ev);
				break;
			default:
				break;
		}

		libinput_event_destroy(ev);
		libinput_dispatch(li);
	}
}


static void log_handler(struct libinput *li, enum libinput_log_priority priority,
		const char *format, va_list args)
{
	vprintf(format, args);
}


int scan(int verbose)
{
	struct udev *udev;
	struct libinput *li;

	udev = udev_new();
        if (!udev) {
                fprintf(stderr, "Failed to initialize udev\n");
                return -1;
        }

	li = libinput_udev_create_context(&interface, NULL, udev);
	if(!li) {
		fprintf(stderr, "Failed to initialize context\n");
		return -1;
	}

	if(verbose) {
		libinput_log_set_handler(li, log_handler);
		libinput_log_set_priority(li, LIBINPUT_LOG_PRIORITY_DEBUG);
	}

	if (libinput_udev_assign_seat(li, "seat0")) {
		fprintf(stderr, "Failed to set seat\n");
		return -1;
	}

	libinput_dispatch(li);

        struct pollfd fds;
        fds.fd = libinput_get_fd(li);
        fds.events = POLLIN;
        fds.revents = 0;

	while(poll(&fds, 1, -1) > -1) {
		handle_events(li);
	}

	return 0;
}


void open_console(void)
{
}

