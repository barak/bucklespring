ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = buckle

buckle_SOURCES = main.c buckle.h

if BACKEND_X11
buckle_SOURCES += scan-x11.c
endif
if BACKEND_LIBINPUT
buckle_SOURCES += scan-libinput.c wl-repeat.c
endif
if BACKEND_MAC
buckle_SOURCES += scan-mac.c
endif
if BACKEND_WINDOWS
buckle_SOURCES += scan-windows.c
endif

buckle_CPPFLAGS = -DPATH_AUDIO='"$(wavdir)"'
buckle_CFLAGS = $(AUDIO_CFLAGS) $(BACKEND_CFLAGS)
buckle_LDADD = $(AUDIO_LIBS) $(BACKEND_LIBS)

AM_CFLAGS = -Wall

# The sample recorder, built only when asked for: make rec
EXTRA_PROGRAMS = rec
rec_SOURCES = rec.c

desktopdir = $(datadir)/applications
dist_desktop_DATA = buckle.desktop

EXTRA_DIST = README.md LICENSE img/buckle.gif img/icon.png img/icon128.ico \
	img/model-m.jpg mac/lib/pkgconfig/openal.pc

wavdir = $(pkgdatadir)/wav
dist_wav_DATA = wav/01-0.wav wav/01-1.wav wav/02-0.wav wav/02-1.wav	\
	wav/03-0.wav wav/03-1.wav wav/04-0.wav wav/04-1.wav		\
	wav/05-0.wav wav/05-1.wav wav/06-0.wav wav/06-1.wav		\
	wav/07-0.wav wav/07-1.wav wav/08-0.wav wav/08-1.wav		\
	wav/09-0.wav wav/09-1.wav wav/0a-0.wav wav/0a-1.wav		\
	wav/0b-0.wav wav/0b-1.wav wav/0c-0.wav wav/0c-1.wav		\
	wav/0d-0.wav wav/0d-1.wav wav/0e-0.wav wav/0e-1.wav		\
	wav/0f-0.wav wav/0f-1.wav wav/10-0.wav wav/10-1.wav		\
	wav/11-0.wav wav/11-1.wav wav/12-0.wav wav/12-1.wav		\
	wav/13-0.wav wav/13-1.wav wav/14-0.wav wav/14-1.wav		\
	wav/15-0.wav wav/15-1.wav wav/16-0.wav wav/16-1.wav		\
	wav/17-0.wav wav/17-1.wav wav/18-0.wav wav/18-1.wav		\
	wav/19-0.wav wav/19-1.wav wav/1a-0.wav wav/1a-1.wav		\
	wav/1b-0.wav wav/1b-1.wav wav/1c-0.wav wav/1c-1.wav		\
	wav/1d-0.wav wav/1d-1.wav wav/1e-0.wav wav/1e-1.wav		\
	wav/1f-0.wav wav/1f-1.wav wav/20-0.wav wav/20-1.wav		\
	wav/21-0.wav wav/21-1.wav wav/22-0.wav wav/22-1.wav		\
	wav/23-0.wav wav/23-1.wav wav/24-0.wav wav/24-1.wav		\
	wav/25-0.wav wav/25-1.wav wav/26-0.wav wav/26-1.wav		\
	wav/27-0.wav wav/27-1.wav wav/28-0.wav wav/28-1.wav		\
	wav/29-0.wav wav/29-1.wav wav/2a-0.wav wav/2a-1.wav		\
	wav/2b-0.wav wav/2b-1.wav wav/2c-0.wav wav/2c-1.wav		\
	wav/2d-0.wav wav/2d-1.wav wav/2e-0.wav wav/2e-1.wav		\
	wav/2f-0.wav wav/2f-1.wav wav/30-0.wav wav/30-1.wav		\
	wav/31-0.wav wav/31-1.wav wav/32-0.wav wav/32-1.wav		\
	wav/33-0.wav wav/33-1.wav wav/34-0.wav wav/34-1.wav		\
	wav/35-0.wav wav/35-1.wav wav/36-0.wav wav/36-1.wav		\
	wav/37-0.wav wav/37-1.wav wav/38-0.wav wav/38-1.wav		\
	wav/39-0.wav wav/39-1.wav wav/3a-0.wav wav/3a-1.wav		\
	wav/3b-0.wav wav/3b-1.wav wav/3c-0.wav wav/3c-1.wav		\
	wav/3d-0.wav wav/3d-1.wav wav/3e-0.wav wav/3e-1.wav		\
	wav/3f-0.wav wav/3f-1.wav wav/40-0.wav wav/40-1.wav		\
	wav/41-0.wav wav/41-1.wav wav/42-0.wav wav/42-1.wav		\
	wav/43-0.wav wav/43-1.wav wav/44-0.wav wav/44-1.wav		\
	wav/45-0.wav wav/45-1.wav wav/46-0.wav wav/46-1.wav		\
	wav/47-0.wav wav/47-1.wav wav/48-0.wav wav/48-1.wav		\
	wav/49-0.wav wav/49-1.wav wav/4a-0.wav wav/4a-1.wav		\
	wav/4b-0.wav wav/4b-1.wav wav/4c-0.wav wav/4c-1.wav		\
	wav/4d-0.wav wav/4d-1.wav wav/4e-0.wav wav/4e-1.wav		\
	wav/4f-0.wav wav/4f-1.wav wav/50-0.wav wav/50-1.wav		\
	wav/51-0.wav wav/51-1.wav wav/52-0.wav wav/52-1.wav		\
	wav/53-0.wav wav/53-1.wav wav/56-0.wav wav/56-1.wav		\
	wav/57-0.wav wav/57-1.wav wav/58-0.wav wav/58-1.wav		\
	wav/5b-0.wav wav/5b-1.wav wav/60-0.wav wav/60-1.wav		\
	wav/61-0.wav wav/61-1.wav wav/62-0.wav wav/62-1.wav		\
	wav/63-0.wav wav/63-1.wav wav/64-0.wav wav/64-1.wav		\
	wav/66-0.wav wav/66-1.wav wav/67-0.wav wav/67-1.wav		\
	wav/68-0.wav wav/68-1.wav wav/69-0.wav wav/69-1.wav		\
	wav/6a-0.wav wav/6a-1.wav wav/6b-0.wav wav/6b-1.wav		\
	wav/6c-0.wav wav/6c-1.wav wav/6d-0.wav wav/6d-1.wav		\
	wav/6e-0.wav wav/6e-1.wav wav/6f-0.wav wav/6f-1.wav		\
	wav/77-0.wav wav/7d-0.wav wav/7d-1.wav wav/ff-0.wav		\
	wav/ff-1.wav
