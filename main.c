/*
 * Autoconf writes the package name and version into config.h; the Makefile
 * shipped with the source passes VERSION on the command line instead.  Take
 * whichever is there, under the names autoconf uses, so that moving the build
 * to autoconf and automake needs nothing here beyond AC_CONFIG_HEADERS: the
 * fallbacks simply stop being reached.
 *
 * This has to come before the system headers, as config.h is where autoconf
 * puts the feature test macros that decide what they declare.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef PACKAGE_NAME
#define PACKAGE_NAME "bucklespring"
#endif

#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION VERSION
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <limits.h>
#include <stdbool.h>
#include <getopt.h>
#include <time.h>

#ifdef __APPLE__
#include <OpenAL/al.h>
#include <OpenAL/alure.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alure.h>
#endif

#include "buckle.h"

#define SRC_INVALID INT_MAX
#define DEFAULT_MUTE_KEYCODE 0x46 /* Scroll Lock */

#define TEST_ERROR(_msg)		\
	error = alGetError();		\
	if (error != AL_NO_ERROR) {	\
		fprintf(stderr, _msg "\n");	\
		exit(1);		\
	}


static void usage(char *exe, int status);
static void version(char *exe);
static void list_devices(void);
static int parse_mouse(const char *arg);
static double find_key_loc(int code);



/* 
 * Horizontal position on keyboard for each key as they are located on my model-M
 */

static int keyloc[][32] = {
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6e, 0x66, 0x68, 0x1c, 0x45, 0x62, 0x37, 0x4a, -1 },
	{ 0x01, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x57, 0x58, 0x6f, 0x6b, 0x6d, 0x47, 0x48, 0x49, 0x4e, -1 },
	{ 0x29, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x4b, 0x4c, 0x4d, -1 },
	{ 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x2b, 0x4f, 0x50, 0x51, 0x60, -1 },
	{ 0x3a, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x1c, 0x52, 0x53, -1 },
	{ 0x2a, 0x56, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, -1 },
	{ 0x1d, 0x7d, 0x5b, 0x38, 0x39, 0x64, 0x61, 0x67, -1 },
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x69, 0x6c, 0x6a, -1 },
};

/* 
 * Horizontal position on keyboard of the pragmatic center of the row, since keys come in different sizes and shapes
 */
static double midloc[] = {
	7.5,
	7.5,
	7.5,
	6.5,
	6.5,
	6.5,
	4.5,
};

static int opt_verbose = 0;
static int opt_stereo_width = 50;
static int opt_gain = 100;
static int opt_fallback_sound = 0;
static int opt_mute_keycode = DEFAULT_MUTE_KEYCODE;
static int opt_no_repeat = 0;
static int opt_mouse = MOUSE_ALL;
static int opt_repeat_delay = 0;
static int opt_repeat_rate = 0;
static const char *opt_device = NULL;
static const char *opt_path_audio = PATH_AUDIO;
static int muted = 0;


/* Options with no short form of their own start past the character codes. */
enum {
	OPT_REPEAT_DELAY = 256,
	OPT_REPEAT_RATE,
};

static const char short_opts[] = "d:fg:hlm:Mp:rs:c::vV";

static const struct option long_opts[] = {
	{ "device",         required_argument, NULL, 'd' },
	{ "fallback-sound", no_argument,       NULL, 'f' },
	{ "gain",           required_argument, NULL, 'g' },
	{ "help",           no_argument,       NULL, 'h' },
	{ "list-devices",   no_argument,       NULL, 'l' },
	{ "mute-keycode",   required_argument, NULL, 'm' },
	{ "mute",           no_argument,       NULL, 'M' },
	{ "audio-path",     required_argument, NULL, 'p' },
	{ "no-repeat",      no_argument,       NULL, 'r' },
	{ "repeat-delay",   required_argument, NULL, OPT_REPEAT_DELAY },
	{ "repeat-rate",    required_argument, NULL, OPT_REPEAT_RATE },
	{ "stereo-width",   required_argument, NULL, 's' },
	{ "no-click",       optional_argument, NULL, 'c' },
	{ "verbose",        no_argument,       NULL, 'v' },
	{ "version",        no_argument,       NULL, 'V' },
        { 0, 0, 0, 0 }
};



int main(int argc, char **argv)
{
	int c;
	int rv = EXIT_SUCCESS;
	int idx;

	while( (c = getopt_long(argc, argv, 
			       short_opts, long_opts, &idx)) != -1) {
		switch(c) {
			case 'd':
				opt_device = optarg;
				break;
			case 'f':
				opt_fallback_sound = 1;
				break;
			case 'g':
				opt_gain = atoi(optarg);
				break;
			case 'h':
				usage(argv[0], 0);
				return 0;
			case 'l':
				list_devices();
				return 0;
			case 'm':
				opt_mute_keycode = strtol(optarg, NULL, 0);
				break;
			case 'M':
				muted = !muted;
				break;
			case 'p':
				opt_path_audio = optarg;
				break;
			case 'r':
				opt_no_repeat = 1;
				break;
			case OPT_REPEAT_DELAY:
				opt_repeat_delay = atoi(optarg);
				break;
			case OPT_REPEAT_RATE:
				opt_repeat_rate = atoi(optarg);
				break;
			case 's':
				opt_stereo_width = atoi(optarg);
				break;
			case 'c':
				opt_mouse &= ~(optarg ? parse_mouse(optarg)
				                      : MOUSE_ALL);
				break;
			case 'v':
				opt_verbose++;
				break;
			case 'V':
				version(argv[0]);
				exit(0);
				break;
			default:
				usage(argv[0], 1);
				return 1;
				break;
		}
	}

	if(opt_verbose) {
		open_console();
	}

	/* Create openal context */

	ALCdevice *device = NULL;
	ALCcontext *context = NULL;
	ALfloat listenerOri[] = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f };
	ALCenum error;

	if (!opt_device) {
		opt_device = alcGetString(NULL, ALC_DEFAULT_DEVICE_SPECIFIER);
	}

	printd("Opening OpenAL audio device \"%s\"", opt_device);

	device = alcOpenDevice(opt_device);
	if (!device) {
		fprintf(stderr, "unable to open default device\n");
		rv = EXIT_FAILURE;
		goto out;
	}

	context = alcCreateContext(device, NULL);
	if (!alcMakeContextCurrent(context)) {
		fprintf(stderr, "failed to make default context\n");
		return -1;
	}
	TEST_ERROR("make default context");

	alListener3f(AL_POSITION, 0, 0, 0);
	alListener3f(AL_VELOCITY, 0, 0, 0);
	alListenerfv(AL_ORIENTATION, listenerOri);

	/* Path to data files can also be specified by environment, this is
	 * used by the snap package */

	const char *env_path = getenv("BUCKLESPRING_WAV_DIR");
	if (env_path) {
		opt_path_audio = env_path;
	}

	printd("Using wav dir: \"%s\"\n", opt_path_audio);

	scan(opt_verbose);

out:
	device = alcGetContextsDevice(context);
	alcMakeContextCurrent(NULL);
	if(context) alcDestroyContext(context);
	if(device) alcCloseDevice(device);

	return rv;
}


/*
 * Asked for with --help this goes to stdout and is the answer to a question;
 * printed because the command line would not parse it goes to stderr and is a
 * complaint, which is the distinction the GNU coding standards draw and what
 * lets --help be piped into a pager without the shell losing the lot.
 */

static void usage(char *exe, int status)
{
	FILE *out = status == 0 ? stdout : stderr;

	fprintf(out,
		"bucklespring version " VERSION "\n"
		"Usage: %s [options]\n"
		"\n"
		"Options:\n"
		"\n"
		"  -d, --device=DEVICE       use OpenAL audio device DEVICE\n"
		"  -f, --fallback-sound      use a fallback sound for unknown keys\n"
		"  -g, --gain=GAIN           set playback gain [0..100]\n"
		"  -m, --mute-keycode=CODE   use CODE as mute key (default 0x46 for scroll lock)\n"
		"  -M, --mute                start the program muted\n"
		"  -c, --no-click[=LIST]     don't play a sound on mouse click; LIST\n"
		"                            narrows it to some of left, middle, right,\n"
		"                            side, extra, wheel, hwheel, all, none\n"
		"  -h, --help                show help\n"
		"  -l, --list-devices        list available OpenAL audio devices\n"
		"  -p, --audio-path=PATH     load .wav files from directory PATH\n"
		"  -r, --no-repeat           stay silent while a held key auto repeats\n"
		"      --repeat-delay=MS     wait MS before the first repeat, overriding\n"
		"                            what the compositor or the kernel says\n"
		"      --repeat-rate=HZ      make HZ repeats per second, likewise\n"
		"  -s, --stereo-width=WIDTH  set stereo width [0..100]\n"
		"  -v, --verbose             increase verbosity / debugging\n"
		"  -V, --version             show version and exit\n",
		exe
       );
}

/*
 * --version in the shape the GNU coding standards ask for: the program, the
 * package it belongs to and the version on the first line, then the copyright,
 * the licence and the author.  On stdout, where the standards put it, even
 * though the usage text goes to stderr.
 *
 * help2man reads the lot: the first line gives the version stamped in the page
 * footer, and it turns the rest into the COPYRIGHT and AUTHOR sections, so
 * neither can drift away from the program.
 */

static void version(char *exe)
{
	const char *name = strrchr(exe, '/');

	printf("%s (%s) %s\n", name ? name + 1 : exe,
			PACKAGE_NAME, PACKAGE_VERSION);
	printf("Copyright (C) 2016-2025 Ico Doornekamp\n");
	printf("License GPLv2+: GNU GPL version 2 or later"
			" <https://gnu.org/licenses/gpl.html>.\n");
	printf("This is free software: you are free to"
			" change and redistribute it.\n");
	printf("There is NO WARRANTY, to the extent permitted by law.\n");
	printf("\n");
	printf("Written by Ico Doornekamp.\n");
}


static void list_devices(void)
{
	const ALCchar *devices = alcGetString(NULL, ALC_DEVICE_SPECIFIER);
	const ALCchar *device = devices, *next = devices + 1;
	size_t len = 0;

	printf("Available audio devices:");
	while (device && *device != '\0' && next && *next != '\0') {
		fprintf(stdout, " \"%s\"", device);
		len = strlen(device);
		device += (len + 1);
		next += (len + 2);
	}
	printf("\n");
}


/*
 * Should a key that the user holds down make a sound each time it auto
 * repeats?  The backends ask, as each has to recognise a repeat its own way.
 */

/*
 * The events --no-click can name.  "all" and "none" are the two ends of the
 * list rather than special cases in the parser, "all" being what a bare
 * --no-click means and "none" the option not being given at all.
 */

static const struct {
	const char *name;
	int bits;
} mouse_events[] = {
	{ "left",   MOUSE_LEFT   },
	{ "middle", MOUSE_MIDDLE },
	{ "right",  MOUSE_RIGHT  },
	{ "side",   MOUSE_SIDE   },
	{ "extra",  MOUSE_EXTRA  },
	{ "wheel",  MOUSE_WHEEL  },
	{ "hwheel", MOUSE_HWHEEL },
	{ "all",    MOUSE_ALL    },
	{ "none",   0            },
};

#define N_MOUSE_EVENTS (sizeof(mouse_events) / sizeof(mouse_events[0]))


/* The argument of --no-click: the events to fall silent on. */

static int parse_mouse(const char *arg)
{
	char *spec, *tok, *save = NULL;
	int bits = 0;
	size_t i;

	spec = strdup(arg);
	if(spec == NULL) {
		fprintf(stderr, "Out of memory\n");
		exit(1);
	}

	for(tok = strtok_r(spec, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {

		for(i = 0; i < N_MOUSE_EVENTS; i++) {
			if(strcmp(tok, mouse_events[i].name) == 0) {
				break;
			}
		}

		if(i == N_MOUSE_EVENTS) {
			fprintf(stderr, "Unknown mouse event \"%s\"\n", tok);
			fprintf(stderr, "Expected a comma separated list of:");
			for(i = 0; i < N_MOUSE_EVENTS; i++) {
				fprintf(stderr, " %s", mouse_events[i].name);
			}
			fprintf(stderr, "\n");
			exit(1);
		}

		bits |= mouse_events[i].bits;
	}

	free(spec);

	return bits;
}


/*
 * Is this one of the mouse events the user left enabled?  Asked by both
 * backends, which name the same events by different numbers.  Doing it here
 * rather than in play() is what lets --no-click take a list: by the time a
 * sound reaches play() every mouse event looks alike.
 */

int mouse_enabled(int which)
{
	return (opt_mouse & which) != 0;
}


int repeat_enabled(void)
{
	return !opt_no_repeat;
}


/*
 * The backend works out how fast the keyboard repeats and then offers the
 * answer here, so that anyone who disagrees with it can say so on the command
 * line.  Values left at zero are the ones the user did not care about.
 */

void repeat_override(int *delay_ms, int *period_ms)
{
	if(opt_repeat_delay > 0) {
		*delay_ms = opt_repeat_delay;
	}
	if(opt_repeat_rate > 0) {
		*period_ms = 1000 / opt_repeat_rate;
	}
}


void printd(const char *fmt, ...)
{
	if(opt_verbose) {
		
		char buf[256];
		va_list va;

		va_start(va, fmt);
		vsnprintf(buf, sizeof(buf), fmt, va);
		va_end(va);

		fprintf(stderr, "%s\n", buf);
	}
}


/*
 * Find horizontal position of the given key on the keyboard. returns -1.0 for
 * left to 1.0 for right 
 */

static double find_key_loc(int code)
{
	int row;
	int col, keycol = 0;

	for(row=0; row<8; row++) {
		for(col=0; col<32; col++) {
			if(keyloc[row][col] == code) keycol = col+1;
			if(keyloc[row][col] == -1) break;
		}
		if(keycol) {
			return ((double) keycol-midloc[row])/(col-midloc[row]);
		}
	}
	return 0;
}


/*
 * To silence play temporarily, press mute key (default ScrollLock) within 2
 * seconds, same to unmute
 */


static void handle_mute_key(int mute_key)
{
	static time_t t_prev;
	static int count = 0;

	if(mute_key) {
		time_t t_now = time(NULL);
		if(t_now - t_prev < 2) {
			count ++;
			if(count == 2) {
				muted = !muted;
				printd("Mute %s", muted ? "enabled" : "disabled");
				count = 0;
			}
		} else {
			count = 1;
		}
		t_prev = t_now;
	} else {
		count = 0;
	}
}


/*
 * Play audio file for given keycode. Wav files are loaded on demand
 */

int play(int code, int press)
{
	ALCenum error;

	printd("scancode %d/0x%x", code, code);

	/* Check for mute sequence: ScrollLock down+up+down */

	if (press) {
		handle_mute_key(code == opt_mute_keycode);
	}

	static ALuint buf[512] = { 0 };
	static ALuint src[512] = { 0 };

	int idx = code + press * 256;

	if(src[idx] == 0) {

		char fname[256];
		snprintf(fname, sizeof(fname), "%s/%02x-%d.wav", opt_path_audio, code, press);

		printd("Loading audio file \"%s\"", fname);

		buf[idx] = alureCreateBufferFromFile(fname);
		if(buf[idx] == 0) {

			if(opt_fallback_sound) {
				snprintf(fname, sizeof(fname), "%s/%02x-%d.wav", opt_path_audio, 0x31, press);
				buf[idx] = alureCreateBufferFromFile(fname);
			} else {
				fprintf(stderr, "Error opening audio file \"%s\": %s\n", fname, alureGetErrorString());
			}

			if(buf[idx] == 0) {
				src[idx] = SRC_INVALID;
				return -1;
			}
		}
	
		alGenSources((ALuint)1, &src[idx]);
		TEST_ERROR("source generation");

		double x = find_key_loc(code);
		if (opt_stereo_width > 0) {
			alSource3f(src[idx], AL_POSITION, -x, 0, (100 - opt_stereo_width) / 100.0);
		}
		alSourcef(src[idx], AL_GAIN, opt_gain / 100.0);

		alSourcei(src[idx], AL_BUFFER, buf[idx]);
		TEST_ERROR("buffer binding");
	}


	if(src[idx] != 0 && src[idx] != SRC_INVALID) {
		if (!muted)
			alSourcePlay(src[idx]);
		TEST_ERROR("source playing");
	}

	return 0;
}



/*
 * End
 */
