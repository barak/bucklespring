#include <assert.h>

#include "../scan-libinput.c"

static int session_active = 1;
static int session_checks;
static int monitor_pending;
static int poll_error;
static int queued;
static int played;
static int destroyed;
static int suspended;
static int compositor_period_ms = REPEAT_PERIOD_MS;
static char event;
static char keyboard_event;
static enum libinput_event_type event_type = LIBINPUT_EVENT_KEYBOARD_KEY;

int sd_session_is_active(const char *session)
{
	assert(strcmp(session, "test-session") == 0);
	session_checks++;
	return session_active;
}

int poll(struct pollfd *fds, nfds_t count, int timeout)
{
	assert(count == 1 && timeout == 0 && fds[0].fd == 42);
	if(poll_error) {
		errno = EIO;
		return -1;
	}
	fds[0].revents = monitor_pending ? POLLIN : 0;
	return monitor_pending;
}

int libinput_dispatch(struct libinput *li) { return 0; }

struct libinput_event *libinput_get_event(struct libinput *li)
{
	if(!queued) return NULL;
	queued--;
	return (struct libinput_event *)&event;
}

enum libinput_event_type libinput_event_get_type(struct libinput_event *ev)
{
	return event_type;
}

struct libinput_event_keyboard *libinput_event_get_keyboard_event(struct libinput_event *ev)
{
	return (struct libinput_event_keyboard *)&keyboard_event;
}

enum libinput_key_state libinput_event_keyboard_get_key_state(struct libinput_event_keyboard *ev)
{
	return LIBINPUT_KEY_STATE_PRESSED;
}

uint32_t libinput_event_keyboard_get_key(struct libinput_event_keyboard *ev)
{
	return KEY_A;
}

void libinput_event_destroy(struct libinput_event *ev) { destroyed++; }
void libinput_suspend(struct libinput *li) { suspended++; queued++; }
int play(int code, int press) { played++; return 0; }
int repeat_enabled(void) { return 1; }
int mouse_enabled(int which) { return 1; }
void repeat_override(int *delay, int *period) {}
void printd(const char *fmt, ...) {}
int wl_repeat_open(void) { return -1; }
int wl_repeat_dispatch(void) { return 0; }
int wl_repeat_get(int *delay, int *period)
{
	*period = compositor_period_ms;
	return 1;
}

int main(void)
{
	int fd = open_restricted("/dev/null", O_RDONLY, "test-session");
	assert(fd >= 0);
	close_restricted(fd, "test-session");

	queued = 1;
	assert(handle_events(NULL, "test-session", 42) == 0);
	assert(played == 1 && destroyed == 1 && repeat_key == KEY_A);

	session_active = 0;
	assert(open_restricted("/dev/null", O_RDONLY, "test-session") == -EACCES);
	const enum libinput_event_type types[] = {
		LIBINPUT_EVENT_KEYBOARD_KEY,
		LIBINPUT_EVENT_POINTER_BUTTON,
		LIBINPUT_EVENT_POINTER_SCROLL_WHEEL,
	};
	for(unsigned int i = 0; i < sizeof types / sizeof types[0]; i++) {
		event_type = types[i];
		queued = 1;
		assert(handle_events(NULL, "test-session", 42) == 0);
		assert(played == 1 && repeat_key == -1);
	}

	repeat_key = KEY_A;
	repeat_after(0);
	assert(repeat_dispatch("test-session", 42) == 0);
	assert(played == 1 && repeat_key == -1);

	session_active = -ENXIO;
	assert(open_restricted("/dev/null", O_RDONLY, "test-session") == -EACCES);
	repeat_key = KEY_A;
	repeat_after(0);
	assert(repeat_dispatch("test-session", 42) == -ENXIO);
	assert(played == 1 && repeat_key == -1);

	session_active = 1;
	monitor_pending = 1;
	event_type = LIBINPUT_EVENT_KEYBOARD_KEY;
	queued = 1;
	assert(handle_events(NULL, "test-session", 42) == 0);
	assert(played == 1 && repeat_key == -1);
	repeat_key = KEY_A;
	repeat_after(0);
	assert(repeat_dispatch("test-session", 42) == 0);
	assert(played == 1 && repeat_key == -1);

	monitor_pending = 0;
	queued = 1;
	assert(handle_events(NULL, "test-session", 42) == 0);
	assert(played == 2 && repeat_key == KEY_A);

	int before = destroyed;
	queued = 2;
	suspend_input(NULL);
	assert(suspended == 1 && queued == 0 && destroyed == before + 3);
	assert(played == 2 && repeat_key == -1);

	poll_error = 1;
	queued = 1;
	assert(handle_events(NULL, "test-session", 42) == -EIO);
	assert(played == 2);

	event_type = LIBINPUT_EVENT_POINTER_MOTION;
	queued = 2;
	before = destroyed;
	int checks = session_checks;
	assert(handle_events(NULL, "test-session", 42) == 0);
	assert(session_checks == checks && destroyed == before + 2);
	assert(played == 2);

	poll_error = 0;
	repeat_key = KEY_A;
	repeat_after(0);
	compositor_period_ms = 0;
	repeat_refresh();
	assert(repeat_key == -1 && repeat_timeout() == -1);
	assert(repeat_dispatch("test-session", 42) == 0 && played == 2);

	compositor_period_ms = REPEAT_PERIOD_MS;
	repeat_refresh();
	assert(repeat_key == -1);
	event_type = LIBINPUT_EVENT_KEYBOARD_KEY;
	queued = 1;
	assert(handle_events(NULL, "test-session", 42) == 0);
	assert(played == 3 && repeat_key == KEY_A);
	return 0;
}
