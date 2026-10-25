#include <assert.h>
#include <time.h>
#include <unistd.h>

static int test_access(const char *path, int mode);
static time_t test_time(time_t *out);
#define access test_access
#define time test_time
#define main buckle_main
#include "../main.c"
#undef main
#undef access
#undef time

static int loads;
static int sources;
static int plays;
static int deleted_buffers;
static int error_checks;
static int listener_gain_changes;
static int source_gain_changes;
static ALfloat listener_gain;
static ALuint bound[512];

static time_t test_time(time_t *out)
{
	assert(out == NULL);
	return 100;
}

static int test_access(const char *path, int mode)
{
	assert(mode == F_OK);
	return strcmp(path, "test-wav/01-1.wav") == 0 ||
	       strcmp(path, "test-wav/31-0.wav") == 0 ||
	       strcmp(path, "test-wav/31-1.wav") == 0 ? 0 : -1;
}

ALuint ALURE_APIENTRY alureCreateBufferFromFile(const ALchar *path)
{
	assert(test_access(path, F_OK) == 0);
	return ++loads;
}

ALenum AL_APIENTRY alGetError(void)
{
	error_checks++;
	return AL_NO_ERROR;
}

void AL_APIENTRY alGenSources(ALsizei n, ALuint *out)
{
	assert(n == 1);
	*out = ++sources;
}

void AL_APIENTRY alSource3f(ALuint source, ALenum param,
		ALfloat x, ALfloat y, ALfloat z) {}

void AL_APIENTRY alSourcef(ALuint source, ALenum param, ALfloat value)
{
	assert(param == AL_GAIN);
	source_gain_changes++;
}

void AL_APIENTRY alListenerf(ALenum param, ALfloat value)
{
	assert(param == AL_GAIN);
	listener_gain = value;
	listener_gain_changes++;
}

void AL_APIENTRY alSourcei(ALuint source, ALenum param, ALint value)
{
	assert(param == AL_BUFFER);
	bound[source] = value;
}

void AL_APIENTRY alSourcePlay(ALuint source)
{
	assert(bound[source] != 0);
	plays++;
}

void AL_APIENTRY alDeleteSources(ALsizei n, const ALuint *ids)
{
	assert(n == 1);
	bound[*ids] = 0;
}

void AL_APIENTRY alDeleteBuffers(ALsizei n, const ALuint *ids)
{
	assert(n == 1);
	for(int i = 1; i <= sources; i++) {
		assert(bound[i] != *ids);
	}
	deleted_buffers++;
}

int scan(int verbose) { return 0; }
void open_console(void) {}
int tray_init(void) { return 0; }
void tray_run(void) {}

int main(void)
{
	opt_path_audio = "test-wav";
	audio_load_buffers();
	assert(loads == 3 && sources == 0);
	assert(buf[0x101] != 0 && buf[0x31] != 0 && buf[0x131] != 0);

	buckle_set_muted(1);
	assert(play(1, 1) == 0 && play(1, 0) == 0 && play(2, 1) == 0);
	assert(sources == 0 && plays == 0 && loads == 3 && error_checks == 0);
	assert(src[0x101] == 0 && src[1] == 0 && src[0x102] == 0);
	buckle_set_muted(0);

	assert(play(1, 1) == 0);
	assert(play(1, 1) == 0);
	assert(loads == 3 && sources == 1 && plays == 2);
	assert(bound[src[0x101]] == buf[0x101]);
	assert(play(2, 1) == -1 && src[0x102] == SRC_INVALID);

	opt_fallback_sound = 1;
	assert(play(3, 0) == 0 && bound[src[3]] == buf[0x31]);
	assert(play(3, 1) == 0 && bound[src[0x103]] == buf[0x131]);
	assert(play(0x31, 1) == 0);
	assert(loads == 3 && sources == 4 && plays == 5);

	context = (ALCcontext *)&loads;
	buckle_set_gain(50);
	assert(listener_gain == 0.5f && listener_gain_changes == 1);
	assert(opt_gain == 50 && source_gain_changes == 0);
	buckle_set_gain(150);
	assert(listener_gain == 1.0f && opt_gain == 100);
	buckle_set_gain(-1);
	assert(listener_gain == 0.0f && opt_gain == 0);
	context = NULL;
	buckle_set_gain(100);
	assert(listener_gain_changes == 3 && opt_gain == 100);

	audio_forget_sources();
	assert(deleted_buffers == 3);
	for(int i = 0; i < 512; i++) {
		assert(src[i] == 0 && buf[i] == 0);
	}

	audio_load_buffers();
	assert(loads == 6 && sources == 4);
	assert(play(1, 1) == 0 && bound[src[0x101]] == buf[0x101]);

	opt_mute_keycode = 0x31;
	buckle_set_muted(1);
	int quiet_sources = sources;
	int quiet_plays = plays;
	assert(play(0x31, 1) == 0 && play(0x31, 0) == 0);
	assert(muted && sources == quiet_sources && plays == quiet_plays);
	assert(play(0x31, 1) == 0 && !muted);
	assert(sources == quiet_sources + 1 && plays == quiet_plays + 1);

	audio_forget_sources();
	assert(deleted_buffers == 6);
	return 0;
}
