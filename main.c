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
#include <pthread.h>
#include <dirent.h>

#ifdef __APPLE__
#include <OpenAL/al.h>
#include <sndfile.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <sndfile.h>
#endif

/* ALC_SOFT_HRTF lives in alext.h, which the OpenAL framework on macOS does
 * not have.  Without it there is nothing to ask for and --hrtf says so. */

#ifdef ALC_HRTF_SOFT
#define HAVE_HRTF 1
#endif

#include "buckle.h"

#define DEFAULT_MUTE_KEYCODE 0x46 /* Scroll Lock */
#define FALLBACK_KEYCODE 0x31 /* N, the stand-in for an uncovered key */

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
static int parse_hrtf(const char *arg);
static const char *clip_set_path(const char *name);
static const char *clip_set_path_quiet(const char *name);
static void list_clips(void);
static int compare_names(const void *a, const void *b);
static void audio_forget_clips(void);
struct clip;
static int load_variants(struct clip *c, const char *dir, int code, int press);
static int fallback_variants(struct clip *c, int code, int press);
static ALCenum device_specifier(void);
static const char *default_device_name(void);
static int audio_open(const char *name);
static void audio_close(void);
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
	7.5,
};

/* One centre per row of keyloc, or find_key_loc() reads off the end of this
 * one.  The arrow cluster, the last row, had no entry of its own. */

_Static_assert(sizeof midloc / sizeof *midloc == sizeof keyloc / sizeof *keyloc,
		"keyloc and midloc must have the same number of rows");

static int opt_verbose = 0;
static int opt_stereo_width = 50;
static int opt_gain = 100;
static int opt_fallback_sound = 0;
static int opt_mute_keycode = DEFAULT_MUTE_KEYCODE;
static int opt_no_repeat = 0;
static int opt_mouse = MOUSE_ALL;
static int opt_no_tray = 0;
static int opt_hrtf = HRTF_AUTO;
static int opt_repeat_delay = 0;
static int opt_repeat_rate = 0;
static const char *opt_device = NULL;
static const char *opt_path_audio = PATH_AUDIO;
static int muted = 0;

/*
 * The samples, the sources playing them and the device they go to.  These used
 * to be locals of play() and of main(), which was fine while the program was
 * one thread deep in the key event loop.  The tray, when there is one, runs
 * the GTK loop on the main thread and changes the gain, the mute and even the
 * output device from there, so the lot moves out here behind one lock.  Every
 * al* call in this file is made with it held.
 */

static pthread_mutex_t audio_lock = PTHREAD_MUTEX_INITIALIZER;
static ALCdevice *device = NULL;
static ALCcontext *context = NULL;

/*
 * The voices.  A source is what OpenAL plays a buffer through, and there is a
 * fixed budget of them -- 256 in OpenAL Soft -- which this program used to
 * run out of, exiting with "source generation" a few hundred keys into a
 * session: it generated a source for every recording it loaded and held it
 * for as long as the clip stayed cached, which with several recordings to a
 * key is three or four voices standing idle for every key ever pressed.
 *
 * A click lasts about a tenth of a second, so what is wanted is not a voice
 * per recording but a voice per click still sounding.  These are made once,
 * with the device, and taken in turn.
 */

#define SOURCE_COUNT 32

static ALuint sources[SOURCE_COUNT];
static int source_next;


/*
 * One key and press state, with every recording the set offers for it.  A set
 * may hold more than one: vain's have two to four of each, and picking among
 * them at random is what keeps a held or repeated key from sounding like a
 * loop of the same click.
 */

#define CLIP_VARIANTS 8

struct clip {
	ALuint buf[CLIP_VARIANTS];
	int n;		/* recordings found */
	int tried;	/* looked for already, so a miss is not looked for again */
};

static struct clip clips[512];


/* Options with no short form of their own start past the character codes. */
enum {
	OPT_REPEAT_DELAY = 256,
	OPT_REPEAT_RATE,
	OPT_NO_TRAY,
	OPT_HRTF,
	OPT_CLIPS,
	OPT_LIST_CLIPS,
	OPT_PREFERENCES,
};

static const char short_opts[] = "d:fg:hlm:Mp:rs:c::vV";

static const struct option long_opts[] = {
	{ "clips",          required_argument, NULL, OPT_CLIPS },
	{ "device",         required_argument, NULL, 'd' },
	{ "fallback-sound", no_argument,       NULL, 'f' },
	{ "gain",           required_argument, NULL, 'g' },
	{ "help",           no_argument,       NULL, 'h' },
	{ "list-clips",     no_argument,       NULL, OPT_LIST_CLIPS },
	{ "list-devices",   no_argument,       NULL, 'l' },
	{ "preferences",    no_argument,       NULL, OPT_PREFERENCES },
	{ "mute-keycode",   required_argument, NULL, 'm' },
	{ "mute",           no_argument,       NULL, 'M' },
	{ "audio-path",     required_argument, NULL, 'p' },
	{ "no-repeat",      no_argument,       NULL, 'r' },
	{ "repeat-delay",   required_argument, NULL, OPT_REPEAT_DELAY },
	{ "repeat-rate",    required_argument, NULL, OPT_REPEAT_RATE },
	{ "stereo-width",   required_argument, NULL, 's' },
	{ "hrtf",           optional_argument, NULL, OPT_HRTF },
	{ "no-tray",        no_argument,       NULL, OPT_NO_TRAY },
	{ "no-click",       optional_argument, NULL, 'c' },
	{ "verbose",        no_argument,       NULL, 'v' },
	{ "version",        no_argument,       NULL, 'V' },
        { 0, 0, 0, 0 }
};



#ifdef HAVE_TRAY
static void *scan_thread(void *arg)
{
	(void)arg;
	scan(opt_verbose);
	return NULL;
}
#endif


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
			case OPT_PREFERENCES:
#ifdef HAVE_TRAY
				/* Nothing of this process but the asking: the
				 * window belongs to the program that is
				 * already running and holding the sound. */

				return tray_show_running() == 0 ? 0 : 1;
#else
				fprintf(stderr, "%s was built without the "
						"tray and its window\n", argv[0]);
				return 1;
#endif
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
			case OPT_CLIPS:
				opt_path_audio = clip_set_path(optarg);
				break;
			case OPT_LIST_CLIPS:
				list_clips();
				return 0;
			case OPT_HRTF:
				opt_hrtf = parse_hrtf(optarg);
				break;
			case OPT_NO_TRAY:
				opt_no_tray = 1;
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

	/* For choosing among a key's recordings; nothing here needs a
	 * sequence anyone could not guess. */

	srand((unsigned)time(NULL));

	/* Create openal context */

	if (!opt_device) {
		opt_device = default_device_name();
	}

	printd("Opening OpenAL audio device \"%s\"", opt_device);

	pthread_mutex_lock(&audio_lock);
	if(audio_open(opt_device) < 0) {
		pthread_mutex_unlock(&audio_lock);
		fprintf(stderr, "unable to open audio device \"%s\"\n", opt_device);
		return EXIT_FAILURE;
	}
	pthread_mutex_unlock(&audio_lock);

	/* Path to data files can also be specified by environment, this is
	 * used by the snap package */

	const char *env_path = getenv("BUCKLESPRING_WAV_DIR");
	if (env_path) {
		opt_path_audio = env_path;
	}

	printd("HRTF: asked for %s, %s",
			opt_hrtf == HRTF_ON ? "on" : opt_hrtf == HRTF_OFF ? "off" : "auto",
			!buckle_hrtf_supported() ? "not built in" :
			buckle_hrtf_active() < 0 ? "not offered by this device" :
			buckle_hrtf_active() ? "in use" : "not in use");

	printd("Using wav dir: \"%s\"\n", opt_path_audio);

#ifdef HAVE_TRAY
	/*
	 * GTK wants the main thread for its loop, and the key event loop never
	 * returns either way, so with a tray the two swap places.  Without one
	 * compiled in, or with no display to put it on, nothing changes.
	 */

	if(!opt_no_tray && tray_init()) {
		pthread_t scanner;

		if(pthread_create(&scanner, NULL, scan_thread, NULL) == 0) {
			tray_run();
		} else {
			fprintf(stderr, "Cannot start the key event thread\n");
			rv = EXIT_FAILURE;
		}
	} else
#endif
	{
		scan(opt_verbose);
	}

	pthread_mutex_lock(&audio_lock);
	audio_forget_clips();
	audio_close();
	pthread_mutex_unlock(&audio_lock);

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
		"      --clips=NAME          play the clip set NAME; --list-clips says\n"
		"                            which are installed\n"
		"  -d, --device=DEVICE       use OpenAL audio device DEVICE\n"
		"  -f, --fallback-sound      use a fallback sound for unknown keys\n"
		"  -g, --gain=GAIN           set playback gain [0..100]\n"
		"      --hrtf[=WHEN]         filter for headphones, so that keys sound\n"
		"                            placed around you rather than panned: on,\n"
		"                            off, or auto to leave it to OpenAL\n"
		"  -m, --mute-keycode=CODE   use CODE as mute key (default 0x46 for scroll lock)\n"
		"  -M, --mute                start the program muted\n"
		"  -c, --no-click[=LIST]     don't play a sound on mouse click; LIST\n"
		"                            narrows it to some of left, middle, right,\n"
		"                            side, extra, wheel, hwheel, all, none\n"
		"  -h, --help                show help\n"
		"      --list-clips          list the clip sets that are installed\n"
		"  -l, --list-devices        list available OpenAL audio devices\n"
		"      --preferences         open the preferences window of the\n"
		"                            running bucklespring\n"
		"  -p, --audio-path=PATH     load clips from the directory PATH\n"
		"  -r, --no-repeat           stay silent while a held key auto repeats\n"
		"      --repeat-delay=MS     wait MS before the first repeat, overriding\n"
		"                            what the compositor or the kernel says\n"
		"      --repeat-rate=HZ      make HZ repeats per second, likewise\n"
		"  -s, --stereo-width=WIDTH  set stereo width [0..100]\n"
		"      --no-tray             do not put an icon in the system tray\n"
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


/*
 * A clip set is a directory of wav files under PATH_CLIPS, one subdirectory
 * per set.  Which one is in use is simply which directory play() loads from,
 * so switching is a matter of pointing opt_path_audio somewhere else and
 * dropping the samples already loaded.
 */

char **buckle_clip_sets(void)
{
	DIR *d = opendir(PATH_CLIPS);
	struct dirent *e;
	char **out = NULL;
	size_t n = 0;

	if(d == NULL) {
		return NULL;
	}

	while((e = readdir(d)) != NULL) {
		char **grown;

		if(e->d_name[0] == '.') {
			continue;
		}
		if(e->d_type != DT_DIR && e->d_type != DT_UNKNOWN) {
			continue;
		}

		grown = realloc(out, (n + 2) * sizeof *out);
		if(grown == NULL) {
			break;
		}
		out = grown;
		out[n] = strdup(e->d_name);
		if(out[n] == NULL) {
			break;
		}
		n++;
		out[n] = NULL;
	}

	closedir(d);

	/* readdir order is whatever the filesystem feels like; a menu and a
	 * listing both want it settled. */

	if(out != NULL) {
		qsort(out, n, sizeof *out, compare_names);
	}

	return out;
}


void buckle_clip_sets_free(char **list)
{
	buckle_audio_devices_free(list);
}


/* The set in use, or NULL when -p pointed somewhere outside PATH_CLIPS. */

const char *buckle_clip_set(void)
{
	static const char prefix[] = PATH_CLIPS "/";

	if(opt_path_audio != NULL &&
	   strncmp(opt_path_audio, prefix, sizeof prefix - 1) == 0) {
		return opt_path_audio + sizeof prefix - 1;
	}

	return NULL;
}


int buckle_set_clip_set(const char *name)
{
	const char *path = clip_set_path_quiet(name);

	if(path == NULL) {
		return -1;
	}

	pthread_mutex_lock(&audio_lock);
	audio_forget_clips();
	opt_path_audio = path;
	pthread_mutex_unlock(&audio_lock);

	printd("Clip set is now \"%s\"", name);

	return 0;
}


/*
 * ALC_DEVICE_SPECIFIER is not the list of sound cards: with OpenAL Soft it is
 * one entry, "OpenAL Soft", standing for whatever that library decides to
 * use.  The real outputs are behind ALC_ENUMERATE_ALL_EXT, so ask for those
 * where it is present and keep the old token only as a fallback.
 */

static ALCenum device_specifier(void)
{
	return alcIsExtensionPresent(NULL, "ALC_ENUMERATE_ALL_EXT")
		? ALC_ALL_DEVICES_SPECIFIER
		: ALC_DEVICE_SPECIFIER;
}


static const char *default_device_name(void)
{
	return alcGetString(NULL,
		alcIsExtensionPresent(NULL, "ALC_ENUMERATE_ALL_EXT")
			? ALC_DEFAULT_ALL_DEVICES_SPECIFIER
			: ALC_DEFAULT_DEVICE_SPECIFIER);
}


/*
 * The ALC device list is one run of NUL terminated names closed by an empty
 * one.  Unpack it into something a caller can walk without knowing that.
 */

char **buckle_audio_devices(void)
{
	const ALCchar *s = alcGetString(NULL, device_specifier());
	const ALCchar *p;
	size_t n = 0, i = 0;
	char **out;

	for(p = s; p != NULL && *p != '\0'; p += strlen(p) + 1) {
		n++;
	}

	out = calloc(n + 1, sizeof *out);
	if(out == NULL) {
		return NULL;
	}

	for(p = s; p != NULL && *p != '\0'; p += strlen(p) + 1) {
		out[i] = strdup(p);
		if(out[i] == NULL) {
			break;
		}
		i++;
	}

	return out;
}


void buckle_audio_devices_free(char **list)
{
	size_t i;

	if(list == NULL) {
		return;
	}
	for(i = 0; list[i] != NULL; i++) {
		free(list[i]);
	}
	free(list);
}


/*
 * Read one clip into an AL buffer, 0 if it cannot be had.
 *
 * Brought to mono whatever it arrives as: OpenAL does not position a
 * multi-channel buffer, it plays it as it stands, so a stereo clip set would
 * quietly lose the left-to-right placement of the keys, which is most of the
 * point of this program.  AL_SOFT_source_spatialize exists to ask for it
 * anyway, but mixing down is both simpler and what the samples want.
 *
 * libsndfile reads rather more than wav, so a clip set may be ogg, opus or
 * flac instead, at a fraction of the size; load_variants() picks among them.
 */

static ALuint load_clip(const char *path)
{
	SF_INFO info;
	SNDFILE *sf;
	short *frames;
	size_t n;
	sf_count_t got;
	ALuint buffer = 0;
	ALenum error;

	memset(&info, 0, sizeof info);

	sf = sf_open(path, SFM_READ, &info);
	if(sf == NULL) {
		return 0;
	}

	if(info.frames < 1 || info.channels < 1 || info.samplerate < 1) {
		sf_close(sf);
		return 0;
	}

	n = (size_t)info.frames * (size_t)info.channels;
	frames = malloc(n * sizeof *frames);
	if(frames == NULL) {
		sf_close(sf);
		return 0;
	}

	got = sf_readf_short(sf, frames, info.frames);
	sf_close(sf);

	if(got < 1) {
		free(frames);
		return 0;
	}

	if(info.channels > 1) {
		sf_count_t i;
		int c;

		for(i = 0; i < got; i++) {
			long sum = 0;

			for(c = 0; c < info.channels; c++) {
				sum += frames[i * info.channels + c];
			}
			frames[i] = (short)(sum / info.channels);
		}
	}

	alGetError();
	alGenBuffers(1, &buffer);
	alBufferData(buffer, AL_FORMAT_MONO16, frames,
			(ALsizei)(got * sizeof *frames), info.samplerate);
	free(frames);

	error = alGetError();
	if(error != AL_NO_ERROR) {
		if(buffer != 0) {
			alDeleteBuffers(1, &buffer);
		}
		return 0;
	}

	return buffer;
}


/*
 * A clip set names its files by scancode and press state; the extension is
 * whichever libsndfile format it was stored in.  Lossless first, so that a
 * set holding a key in both forms is heard at its best, and because the set
 * shipped here is flac.
 */

static const char *const clip_extensions[] = { "flac", "wav", "ogg", "opus" };

/*
 * Every recording a set offers for one key.  Two namings are understood:
 * XX-Y.ext for a set with a single clip per key, and XX-Y-Z.ext for the Zth
 * of several, which is how the sets with more than one are laid out.  The
 * numbered ones are taken in order and stop at the first gap.
 */

static int load_variants(struct clip *c, const char *dir, int code, int press)
{
	char path[512];
	size_t e;
	int z;

	c->n = 0;

	if(dir == NULL) {
		return 0;
	}

	for(e = 0; e < sizeof clip_extensions / sizeof *clip_extensions; e++) {
		snprintf(path, sizeof path, "%s/%02x-%d.%s", dir, code, press,
				clip_extensions[e]);

		c->buf[0] = load_clip(path);
		if(c->buf[0] != 0) {
			printd("Loaded \"%s\"", path);
			c->n = 1;
			return c->n;
		}
	}

	for(z = 0; z < CLIP_VARIANTS; z++) {
		ALuint buffer = 0;

		for(e = 0; e < sizeof clip_extensions / sizeof *clip_extensions &&
				buffer == 0; e++) {
			snprintf(path, sizeof path, "%s/%02x-%d-%d.%s", dir,
					code, press, z, clip_extensions[e]);
			buffer = load_clip(path);
			if(buffer != 0) {
				printd("Loaded \"%s\"", path);
			}
		}

		if(buffer == 0) {
			break;
		}

		c->buf[c->n++] = buffer;
	}

	return c->n;
}


/*
 * What to play when the chosen set has nothing for this key.  A set need not
 * cover the whole keyboard -- several of the ones people have recorded cover
 * only the letters -- so --fallback-sound works down a list, nearest first:
 *
 *   the set's own generic click, keeping whatever the set sounds like, which
 *   is the reason for choosing it;
 *   then this key from the set shipped with the program, which is the right
 *   key even if it is the wrong keyboard;
 *   then that set's generic click, which exists for certain.
 *
 * Without --fallback-sound a key the set does not cover stays silent.
 */

static int fallback_variants(struct clip *c, int code, int press)
{
	if(load_variants(c, opt_path_audio, FALLBACK_KEYCODE, press) > 0) {
		return c->n;
	}

	/* Nothing further to try when the set in use is the shipped one. */

	if(opt_path_audio != NULL && strcmp(opt_path_audio, PATH_AUDIO) == 0) {
		return 0;
	}

	if(load_variants(c, PATH_AUDIO, code, press) > 0) {
		return c->n;
	}

	return load_variants(c, PATH_AUDIO, FALLBACK_KEYCODE, press);
}


/* All three expect audio_lock to be held. */

static void audio_forget_clips(void)
{
	size_t i;
	int v;

	/* A buffer bound to a voice cannot be deleted, and the voices outlive
	 * the clips, so they are stopped and emptied first. */

	for(v = 0; v < SOURCE_COUNT; v++) {
		if(sources[v] != 0) {
			alSourceStop(sources[v]);
			alSourcei(sources[v], AL_BUFFER, 0);
		}
	}

	for(i = 0; i < sizeof clips / sizeof *clips; i++) {
		for(v = 0; v < clips[i].n; v++) {
			if(clips[i].buf[v] != 0) {
				alDeleteBuffers(1, &clips[i].buf[v]);
			}
		}

		memset(&clips[i], 0, sizeof clips[i]);
	}
}


/* The attribute list for a context or a device reset, or NULL when there is
 * nothing to say and OpenAL's own setting should stand. */

static ALCint *hrtf_attrs(int setting, ALCint *buf)
{
#ifdef HAVE_HRTF
	if(setting == HRTF_AUTO) {
		return NULL;
	}

	buf[0] = ALC_HRTF_SOFT;
	buf[1] = setting == HRTF_ON ? ALC_TRUE : ALC_FALSE;
	buf[2] = 0;

	return buf;
#else
	(void)setting;
	(void)buf;
	return NULL;
#endif
}


static int audio_open(const char *name)
{
	/* Looking down negative Z with Y up, which is where OpenAL puts a
	 * listener it has not been told about.  See the note in play() on why
	 * the sources moved with it. */

	ALfloat ori[] = { 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f };
	ALCint attrs[3];

	device = alcOpenDevice(name);
	if(device == NULL) {
		return -1;
	}

	context = alcCreateContext(device, hrtf_attrs(opt_hrtf, attrs));
	if(context == NULL || !alcMakeContextCurrent(context)) {
		if(context != NULL) {
			alcDestroyContext(context);
		}
		alcCloseDevice(device);
		device = NULL;
		context = NULL;
		return -1;
	}

	alListener3f(AL_POSITION, 0, 0, 0);
	alListener3f(AL_VELOCITY, 0, 0, 0);
	alListenerfv(AL_ORIENTATION, ori);

	/* The voices belong to the context, so they are made here and go
	 * again with it.  Not getting them is this device being no use, and
	 * is reported as the device not opening. */

	alGetError();
	alGenSources(SOURCE_COUNT, sources);
	if(alGetError() != AL_NO_ERROR) {
		memset(sources, 0, sizeof sources);
		audio_close();
		return -1;
	}

	source_next = 0;

	return 0;
}


static void audio_close(void)
{
	if(context != NULL) {
		alDeleteSources(SOURCE_COUNT, sources);
		memset(sources, 0, sizeof sources);
	}

	alcMakeContextCurrent(NULL);
	if(context != NULL) {
		alcDestroyContext(context);
		context = NULL;
	}
	if(device != NULL) {
		alcCloseDevice(device);
		device = NULL;
	}
}


static void list_devices(void)
{
	char **list = buckle_audio_devices();
	size_t i;

	printf("Available audio devices:");
	for(i = 0; list != NULL && list[i] != NULL; i++) {
		printf(" \"%s\"", list[i]);
	}
	printf("\n");
	buckle_audio_devices_free(list);
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


static int compare_names(const void *a, const void *b)
{
	return strcmp(*(const char **)a, *(const char **)b);
}


/* PATH_CLIPS/name, kept for the life of the process. */

static const char *clip_set_path_quiet(const char *name)
{
	char *path;
	size_t n;

	if(name == NULL || *name == '\0' || strchr(name, '/') != NULL) {
		return NULL;
	}

	/* snprintf rather than asprintf, which would want _GNU_SOURCE. */

	n = strlen(PATH_CLIPS) + 1 + strlen(name) + 1;
	path = malloc(n);
	if(path == NULL) {
		return NULL;
	}
	snprintf(path, n, "%s/%s", PATH_CLIPS, name);

	return path;
}


static const char *clip_set_path(const char *name)
{
	const char *path = clip_set_path_quiet(name);
	char **list;
	size_t i;

	if(path != NULL) {
		return path;
	}

	fprintf(stderr, "Not a clip set name: \"%s\"\n", name);
	fprintf(stderr, "Installed sets are:");
	list = buckle_clip_sets();
	for(i = 0; list != NULL && list[i] != NULL; i++) {
		fprintf(stderr, " %s", list[i]);
	}
	fprintf(stderr, "\n");
	buckle_clip_sets_free(list);
	exit(1);
}


static void list_clips(void)
{
	char **list = buckle_clip_sets();
	size_t i;

	printf("Installed clip sets:");
	for(i = 0; list != NULL && list[i] != NULL; i++) {
		printf(" %s", list[i]);
	}
	printf("\n");
	buckle_clip_sets_free(list);
}


/* The argument of --hrtf.  Bare --hrtf means on, as asking for it at all is
 * unlikely to mean "whatever you were going to do anyway". */

static int parse_hrtf(const char *arg)
{
	if(arg == NULL || strcmp(arg, "on") == 0 || strcmp(arg, "yes") == 0) {
		return HRTF_ON;
	}
	if(strcmp(arg, "off") == 0 || strcmp(arg, "no") == 0) {
		return HRTF_OFF;
	}
	if(strcmp(arg, "auto") == 0) {
		return HRTF_AUTO;
	}

	fprintf(stderr, "Unknown --hrtf setting \"%s\"; expected on, off or auto\n",
			arg);
	exit(1);
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


/*
 * What the tray can ask for and change.  Each takes the audio lock, so these
 * are safe to call from the GTK thread while the key event loop is playing.
 */

int buckle_muted(void)
{
	int m;

	pthread_mutex_lock(&audio_lock);
	m = muted;
	pthread_mutex_unlock(&audio_lock);

	return m;
}


void buckle_set_muted(int on)
{
	pthread_mutex_lock(&audio_lock);
	muted = on ? 1 : 0;
	pthread_mutex_unlock(&audio_lock);
}


int buckle_gain(void)
{
	int g;

	pthread_mutex_lock(&audio_lock);
	g = opt_gain;
	pthread_mutex_unlock(&audio_lock);

	return g;
}


/* The gain is set on a source when it is first made, so the sources that
 * already exist have to be told again. */

void buckle_set_gain(int gain)
{
	int i;

	if(gain < 0) { gain = 0; }
	if(gain > 100) { gain = 100; }

	pthread_mutex_lock(&audio_lock);
	opt_gain = gain;

	/* play() gives a voice its gain along with its buffer, so this is
	 * only for the clicks still sounding. */

	for(i = 0; i < SOURCE_COUNT; i++) {
		if(sources[i] != 0) {
			alSourcef(sources[i], AL_GAIN, opt_gain / 100.0);
		}
	}
	pthread_mutex_unlock(&audio_lock);
}


/*
 * The name of the device that is open, which is not always the name it was
 * opened by: asking for the default gets a real output back.  Copied into a
 * buffer because the string belongs to the device and would dangle the moment
 * one is closed; only the tray calls this, and only from the GTK thread.
 */

/*
 * HRTF, the filter pair that makes a positioned source sound placed around
 * the listener rather than panned between the ears.  It is worth having on
 * headphones and wrong on speakers, so the default is to leave the choice to
 * OpenAL, which turns it on when it believes it is talking to headphones.
 */

/* Whether there is an --hrtf to speak of at all, which is a question about
 * how this was built rather than about the device in front of us. */

int buckle_hrtf_supported(void)
{
#ifdef HAVE_HRTF
	return 1;
#else
	return 0;
#endif
}


int buckle_hrtf_setting(void)
{
	return opt_hrtf;
}


/* What OpenAL is actually doing, which need not be what was asked for: -1
 * when there is no telling. */

int buckle_hrtf_active(void)
{
#ifdef HAVE_HRTF
	ALCint state = -1;

	pthread_mutex_lock(&audio_lock);
	if(device != NULL && alcIsExtensionPresent(device, "ALC_SOFT_HRTF")) {
		alcGetIntegerv(device, ALC_HRTF_SOFT, 1, &state);
	}
	pthread_mutex_unlock(&audio_lock);

	return state;
#else
	return -1;
#endif
}


/*
 * Switching it over while running is a device reset rather than a reopen, so
 * the sources and the samples already loaded into them survive it.
 */

int buckle_set_hrtf(int setting)
{
#ifdef HAVE_HRTF
	ALCint attrs[3];
	ALCint *attrp;
	LPALCRESETDEVICESOFT reset;
	int ok = 0;

	pthread_mutex_lock(&audio_lock);

	reset = (LPALCRESETDEVICESOFT)
			alcGetProcAddress(device, "alcResetDeviceSOFT");

	if(device != NULL && reset != NULL) {

		/* A reset wants to be told either way; leaving the attribute
		 * out here would keep whatever the last reset asked for. */

		attrs[0] = ALC_HRTF_SOFT;
		attrs[1] = setting == HRTF_ON ? ALC_TRUE
			 : setting == HRTF_OFF ? ALC_FALSE
			 : ALC_DONT_CARE_SOFT;
		attrs[2] = 0;
		attrp = attrs;

		ok = reset(device, attrp);
		if(ok) {
			opt_hrtf = setting;
		}
	}

	pthread_mutex_unlock(&audio_lock);

	return ok ? 0 : -1;
#else
	(void)setting;
	return -1;
#endif
}


/*
 * Whether the device we are playing to is still there: 1 yes, 0 gone, -1 if
 * this OpenAL will not say.  A bluetooth headset going out of range is the
 * usual way for it to turn 0.
 */

int buckle_audio_connected(void)
{
#ifdef ALC_EXT_disconnect
	ALCint state = -1;

	pthread_mutex_lock(&audio_lock);
	if(device != NULL && alcIsExtensionPresent(device, "ALC_EXT_disconnect")) {
		alcGetIntegerv(device, ALC_CONNECTED, 1, &state);
	}
	pthread_mutex_unlock(&audio_lock);

	return state;
#else
	return -1;
#endif
}


/*
 * Move to whatever the default device is now, for when the one in use has
 * gone.  Without this a headset disconnecting leaves the program running and
 * silent until it is restarted.  Returns 1 if it moved.
 */

int buckle_audio_recover(void)
{
	const char *name = default_device_name();

	pthread_mutex_lock(&audio_lock);

	audio_forget_clips();
	audio_close();

	if(audio_open(name) == 0) {
		opt_device = name;
		pthread_mutex_unlock(&audio_lock);
		printd("Audio device went away; moved to \"%s\"", name);
		return 1;
	}

	pthread_mutex_unlock(&audio_lock);
	fprintf(stderr, "The audio device went away and \"%s\" could not be "
			"opened in its place\n", name != NULL ? name : "(default)");

	return 0;
}


const char *buckle_audio_device(void)
{
	static char name[256];
	const ALCchar *s = NULL;

	pthread_mutex_lock(&audio_lock);
	if(device != NULL) {
		s = alcGetString(device, device_specifier());
	}
	if(s == NULL) {
		s = opt_device;
	}
	snprintf(name, sizeof name, "%s", s != NULL ? s : "");
	pthread_mutex_unlock(&audio_lock);

	return name;
}


/*
 * Move to another output device.  The buffers and the voices belong to the
 * old context, so they go with it and play() loads the clips again on
 * demand.  On failure the old device is reopened, as having no device at
 * all would leave the program running and silent with no way back.
 */

int buckle_set_audio_device(const char *name)
{
	const char *old = opt_device;
	int rv = 0;

	pthread_mutex_lock(&audio_lock);

	audio_forget_clips();
	audio_close();

	if(audio_open(name) == 0) {
		opt_device = name;
	} else if(audio_open(old) == 0) {
		rv = -1;
	} else {
		fprintf(stderr, "Lost the audio device \"%s\" and could not get "
				"\"%s\" back\n", name, old);
		rv = -1;
	}

	pthread_mutex_unlock(&audio_lock);

	return rv;
}


int buckle_mute_keycode(void)
{
	return opt_mute_keycode;
}


void buckle_quit(void)
{
	pthread_mutex_lock(&audio_lock);
	audio_forget_clips();
	audio_close();
	pthread_mutex_unlock(&audio_lock);

	exit(EXIT_SUCCESS);
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

	for(row=0; row < (int)(sizeof keyloc / sizeof *keyloc); row++) {
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

	pthread_mutex_lock(&audio_lock);

	/*
	 * No device means a switch or a recovery could not get one, and every
	 * al* call below would fail with no context current.  TEST_ERROR ends
	 * the process on those, so silence is the right answer here.
	 */

	if(device == NULL) {
		pthread_mutex_unlock(&audio_lock);
		return -1;
	}

	/* Check for mute sequence: ScrollLock down+up+down */

	if (press) {
		handle_mute_key(code == opt_mute_keycode);
	}

	struct clip *clip = &clips[code + press * 256];

	if(!clip->tried) {

		clip->tried = 1;

		if(load_variants(clip, opt_path_audio, code, press) == 0) {
			if(opt_fallback_sound) {
				fallback_variants(clip, code, press);
			} else {
				fprintf(stderr, "No clip for %02x-%d in \"%s\"; "
						"-f borrows one\n", code, press,
						opt_path_audio);
			}
		}

		if(clip->n > 1) {
			printd("%02x-%d has %d recordings to choose from",
					code, press, clip->n);
		}

	}

	if(clip->n < 1) {
		pthread_mutex_unlock(&audio_lock);
		return -1;
	}

	/*
	 * One of the recordings, at random where there is a choice, through
	 * the next voice in turn.  A voice has to be stopped before its
	 * buffer can be changed, and this one may still be finishing an
	 * earlier key; taking them in turn makes that a key pressed
	 * SOURCE_COUNT clicks ago, which at a tenth of a second a click
	 * would take three hundred keys a second to catch up with.
	 *
	 * The keyboard sits in front of the listener, to the right for a
	 * positive find_key_loc().  This used to be written with the listener
	 * facing backwards, down positive Z, and with the x and z of every
	 * source negated to match; the two cancelled, so it panned correctly
	 * and nothing sounded wrong.  Against a backwards listener, though,
	 * every key was behind the head, which only plain panning can be
	 * indifferent to.
	 *
	 * A stereo width of zero means no placement at all, which is where a
	 * source sits before anything is said about it and so where a voice
	 * used for an earlier key has to be put back.
	 */

	if (!muted) {
		ALuint src = sources[source_next];
		double x = find_key_loc(code);

		source_next = (source_next + 1) % SOURCE_COUNT;

		alSourceStop(src);
		alSourcei(src, AL_BUFFER,
				clip->buf[clip->n > 1 ? rand() % clip->n : 0]);
		TEST_ERROR("buffer binding");

		if (opt_stereo_width > 0) {
			alSource3f(src, AL_POSITION, x, 0,
				-(100 - opt_stereo_width) / 100.0);
		} else {
			alSource3f(src, AL_POSITION, 0, 0, 0);
		}
		alSourcef(src, AL_GAIN, opt_gain / 100.0);

		alSourcePlay(src);
	}
	TEST_ERROR("source playing");

	pthread_mutex_unlock(&audio_lock);

	return 0;
}



/*
 * End
 */
