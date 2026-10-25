#include <assert.h>
#include <unistd.h>

static int test_access(const char *path, int mode);
#define access test_access
#define main buckle_main
#include "../main.c"
#undef main
#undef access

static int loads;
static int sources;
static int plays;
static int deleted_buffers;
static ALuint bound[512];

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

ALenum AL_APIENTRY alGetError(void) { return AL_NO_ERROR; }

void AL_APIENTRY alGenSources(ALsizei n, ALuint *out)
{
	assert(n == 1);
	*out = ++sources;
}

void AL_APIENTRY alSource3f(ALuint source, ALenum param,
		ALfloat x, ALfloat y, ALfloat z) {}
void AL_APIENTRY alSourcef(ALuint source, ALenum param, ALfloat value) {}

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

	audio_forget_sources();
	assert(deleted_buffers == 3);
	for(int i = 0; i < 512; i++) {
		assert(src[i] == 0 && buf[i] == 0);
	}

	audio_load_buffers();
	assert(loads == 6 && sources == 4);
	assert(play(1, 1) == 0 && bound[src[0x101]] == buf[0x101]);
	audio_forget_sources();
	assert(deleted_buffers == 6);
	return 0;
}
