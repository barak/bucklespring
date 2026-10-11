AM_CFLAGS = -Wall

# Every flavour looks for its samples in the same place.  These have to be
# built here rather than in configure.ac: pkgdatadir is written in terms of
# ${prefix}, which only make expands.
#
# PATH_CLIPS is the directory the clip sets sit in, one subdirectory each, and
# PATH_AUDIO is the one to use when nothing says otherwise.
buckle_cppflags = -DPATH_CLIPS='"$(clipsdir)"' \
	-DPATH_AUDIO='"$(clipsdir)/$(DEFAULT_CLIPS)"'

bin_PROGRAMS =
dist_man1_MANS =
dist_man8_MANS =
CLEANFILES =

# One program per source of key events.  main.c is shared, so per target flags
# are what keeps them apart; automake gives each its own object files.
# The tray is the same for every flavour, so it joins each of them rather than
# being a flavour of its own.
if HAVE_TRAY
tray_cflags = $(TRAY_CFLAGS) $(PTHREAD_CFLAGS)
tray_libs = $(TRAY_LIBS)
else
tray_cflags =
tray_libs =
endif

buckle_x11_SOURCES = main.c buckle.h scan-x11.c
buckle_x11_CPPFLAGS = $(buckle_cppflags)
buckle_x11_CFLAGS = $(AM_CFLAGS) $(AUDIO_CFLAGS) $(X11_CFLAGS) $(tray_cflags)
buckle_x11_LDADD = $(AUDIO_LIBS) $(X11_LIBS) $(tray_libs)

buckle_libinput_SOURCES = main.c buckle.h scan-libinput.c wl-repeat.c
buckle_libinput_CPPFLAGS = $(buckle_cppflags)
buckle_libinput_CFLAGS = $(AM_CFLAGS) $(AUDIO_CFLAGS) $(LIBINPUT_CFLAGS) $(tray_cflags)
buckle_libinput_LDADD = $(AUDIO_LIBS) $(LIBINPUT_LIBS) $(tray_libs)

buckle_mac_SOURCES = main.c buckle.h scan-mac.c
buckle_mac_CPPFLAGS = $(buckle_cppflags)
buckle_mac_CFLAGS = $(AM_CFLAGS) $(AUDIO_CFLAGS) $(tray_cflags)
buckle_mac_LDADD = $(AUDIO_LIBS) $(MAC_LIBS) $(tray_libs)

buckle_windows_SOURCES = main.c buckle.h scan-windows.c
buckle_windows_CPPFLAGS = $(buckle_cppflags)
buckle_windows_CFLAGS = $(AM_CFLAGS) $(AUDIO_CFLAGS) $(tray_cflags)
buckle_windows_LDFLAGS = $(WINDOWS_LDFLAGS)
buckle_windows_LDADD = $(AUDIO_LIBS) $(tray_libs)

if HAVE_TRAY
buckle_x11_SOURCES += tray.c
buckle_libinput_SOURCES += tray.c
buckle_mac_SOURCES += tray.c
buckle_windows_SOURCES += tray.c
endif

desktopdir = $(datadir)/applications
dist_desktop_DATA =

if BACKEND_X11
bin_PROGRAMS += buckle-x11
dist_man1_MANS += man/buckle-x11.1
dist_desktop_DATA += buckle-x11.desktop
endif

if BACKEND_LIBINPUT
bin_PROGRAMS += buckle-libinput
dist_man1_MANS += man/buckle-libinput.1
dist_desktop_DATA += buckle-libinput.desktop
endif

if BACKEND_MAC
bin_PROGRAMS += buckle-mac
endif

if BACKEND_WINDOWS
bin_PROGRAMS += buckle-windows
endif

# The systemd user units, and the manual pages that document them, follow the
# programs they start.  The units are generated so that ExecStart names the
# bindir this was configured for; the [@] keeps config.status from eating the
# token on its way through Makefile.in.
subst = $(SED) \
	-e 's|@bindir[@]|$(bindir)|g' \
	-e 's|@sysconfdir[@]|$(sysconfdir)|g'

if INSTALL_SYSTEMD
systemduserunit_DATA =
if BACKEND_X11
systemduserunit_DATA += systemd/buckle-x11.service
dist_man8_MANS += man/buckle-x11.service.8
endif
if BACKEND_LIBINPUT
systemduserunit_DATA += systemd/buckle-libinput.service
dist_man8_MANS += man/buckle-libinput.service.8
endif
CLEANFILES += $(systemduserunit_DATA)
endif

systemd/buckle-x11.service: $(srcdir)/systemd/buckle-x11.service.in Makefile
	$(AM_V_GEN)$(MKDIR_P) systemd && $(subst) < $< > $@

systemd/buckle-libinput.service: $(srcdir)/systemd/buckle-libinput.service.in Makefile
	$(AM_V_GEN)$(MKDIR_P) systemd && $(subst) < $< > $@

# The uaccess rule is only of use to the flavour that reads the raw devices.
# It does nothing until the administrator creates the flag file; read the
# comments in it before arming it.
if INSTALL_UDEV
if BACKEND_LIBINPUT
udevrules_DATA = udev/70-bucklespring-libinput.rules
CLEANFILES += $(udevrules_DATA)
endif
endif

udev/70-bucklespring-libinput.rules: $(srcdir)/udev/70-bucklespring-libinput.rules.in Makefile
	$(AM_V_GEN)$(MKDIR_P) udev && $(subst) < $< > $@

# The manual pages for the executables are generated from their own --help and
# --version output, so an option added to the program turns up in its page
# without anyone writing it down twice.  help2man has to run the program, which
# a cross build cannot do, so the pages are kept in the tree and refreshed on
# demand.  A page that changes here is telling you to commit it.
H2M = $(HELP2MAN) --no-info \
	--source="Bucklespring keyboard sound" --manual="User Commands"

.PHONY: update-man
update-man: $(bin_PROGRAMS)
	@test "x$(HELP2MAN)" != xno || \
	  { echo "update-man needs help2man; none was found by configure" >&2; \
	    exit 1; }
if BACKEND_X11
	$(H2M) --include=$(srcdir)/man/buckle-x11.1.h2m \
		--output=$(srcdir)/man/buckle-x11.1 ./buckle-x11$(EXEEXT)
endif
if BACKEND_LIBINPUT
	$(H2M) --include=$(srcdir)/man/buckle-libinput.1.h2m \
		--output=$(srcdir)/man/buckle-libinput.1 ./buckle-libinput$(EXEEXT)
endif

# The sample recorder, built only when asked for: make rec
EXTRA_PROGRAMS = rec
rec_SOURCES = rec.c


EXTRA_DIST = README.md LICENSE \
	img/buckle.gif img/icon.png img/icon128.ico img/model-m.jpg \
	mac/lib/pkgconfig/openal.pc \
	man/buckle-x11.1.h2m man/buckle-libinput.1.h2m \
	systemd/buckle-x11.service.in systemd/buckle-libinput.service.in \
	udev/70-bucklespring-libinput.rules.in

# The clip sets.  Each is a subdirectory of clipsdir holding one wav per
# scancode and press state; DEFAULT_CLIPS names the one used when no
# --clips says otherwise.
DEFAULT_CLIPS = bucklespring
clipsdir = $(pkgdatadir)/clips

bucklespringclipsdir = $(clipsdir)/bucklespring
dist_bucklespringclips_DATA = \
	clips/bucklespring/01-0.flac \
	clips/bucklespring/01-1.flac \
	clips/bucklespring/02-0.flac \
	clips/bucklespring/02-1.flac \
	clips/bucklespring/03-0.flac \
	clips/bucklespring/03-1.flac \
	clips/bucklespring/04-0.flac \
	clips/bucklespring/04-1.flac \
	clips/bucklespring/05-0.flac \
	clips/bucklespring/05-1.flac \
	clips/bucklespring/06-0.flac \
	clips/bucklespring/06-1.flac \
	clips/bucklespring/07-0.flac \
	clips/bucklespring/07-1.flac \
	clips/bucklespring/08-0.flac \
	clips/bucklespring/08-1.flac \
	clips/bucklespring/09-0.flac \
	clips/bucklespring/09-1.flac \
	clips/bucklespring/0a-0.flac \
	clips/bucklespring/0a-1.flac \
	clips/bucklespring/0b-0.flac \
	clips/bucklespring/0b-1.flac \
	clips/bucklespring/0c-0.flac \
	clips/bucklespring/0c-1.flac \
	clips/bucklespring/0d-0.flac \
	clips/bucklespring/0d-1.flac \
	clips/bucklespring/0e-0.flac \
	clips/bucklespring/0e-1.flac \
	clips/bucklespring/0f-0.flac \
	clips/bucklespring/0f-1.flac \
	clips/bucklespring/10-0.flac \
	clips/bucklespring/10-1.flac \
	clips/bucklespring/11-0.flac \
	clips/bucklespring/11-1.flac \
	clips/bucklespring/12-0.flac \
	clips/bucklespring/12-1.flac \
	clips/bucklespring/13-0.flac \
	clips/bucklespring/13-1.flac \
	clips/bucklespring/14-0.flac \
	clips/bucklespring/14-1.flac \
	clips/bucklespring/15-0.flac \
	clips/bucklespring/15-1.flac \
	clips/bucklespring/16-0.flac \
	clips/bucklespring/16-1.flac \
	clips/bucklespring/17-0.flac \
	clips/bucklespring/17-1.flac \
	clips/bucklespring/18-0.flac \
	clips/bucklespring/18-1.flac \
	clips/bucklespring/19-0.flac \
	clips/bucklespring/19-1.flac \
	clips/bucklespring/1a-0.flac \
	clips/bucklespring/1a-1.flac \
	clips/bucklespring/1b-0.flac \
	clips/bucklespring/1b-1.flac \
	clips/bucklespring/1c-0.flac \
	clips/bucklespring/1c-1.flac \
	clips/bucklespring/1d-0.flac \
	clips/bucklespring/1d-1.flac \
	clips/bucklespring/1e-0.flac \
	clips/bucklespring/1e-1.flac \
	clips/bucklespring/1f-0.flac \
	clips/bucklespring/1f-1.flac \
	clips/bucklespring/20-0.flac \
	clips/bucklespring/20-1.flac \
	clips/bucklespring/21-0.flac \
	clips/bucklespring/21-1.flac \
	clips/bucklespring/22-0.flac \
	clips/bucklespring/22-1.flac \
	clips/bucklespring/23-0.flac \
	clips/bucklespring/23-1.flac \
	clips/bucklespring/24-0.flac \
	clips/bucklespring/24-1.flac \
	clips/bucklespring/25-0.flac \
	clips/bucklespring/25-1.flac \
	clips/bucklespring/26-0.flac \
	clips/bucklespring/26-1.flac \
	clips/bucklespring/27-0.flac \
	clips/bucklespring/27-1.flac \
	clips/bucklespring/28-0.flac \
	clips/bucklespring/28-1.flac \
	clips/bucklespring/29-0.flac \
	clips/bucklespring/29-1.flac \
	clips/bucklespring/2a-0.flac \
	clips/bucklespring/2a-1.flac \
	clips/bucklespring/2b-0.flac \
	clips/bucklespring/2b-1.flac \
	clips/bucklespring/2c-0.flac \
	clips/bucklespring/2c-1.flac \
	clips/bucklespring/2d-0.flac \
	clips/bucklespring/2d-1.flac \
	clips/bucklespring/2e-0.flac \
	clips/bucklespring/2e-1.flac \
	clips/bucklespring/2f-0.flac \
	clips/bucklespring/2f-1.flac \
	clips/bucklespring/30-0.flac \
	clips/bucklespring/30-1.flac \
	clips/bucklespring/31-0.flac \
	clips/bucklespring/31-1.flac \
	clips/bucklespring/32-0.flac \
	clips/bucklespring/32-1.flac \
	clips/bucklespring/33-0.flac \
	clips/bucklespring/33-1.flac \
	clips/bucklespring/34-0.flac \
	clips/bucklespring/34-1.flac \
	clips/bucklespring/35-0.flac \
	clips/bucklespring/35-1.flac \
	clips/bucklespring/36-0.flac \
	clips/bucklespring/36-1.flac \
	clips/bucklespring/37-0.flac \
	clips/bucklespring/37-1.flac \
	clips/bucklespring/38-0.flac \
	clips/bucklespring/38-1.flac \
	clips/bucklespring/39-0.flac \
	clips/bucklespring/39-1.flac \
	clips/bucklespring/3a-0.flac \
	clips/bucklespring/3a-1.flac \
	clips/bucklespring/3b-0.flac \
	clips/bucklespring/3b-1.flac \
	clips/bucklespring/3c-0.flac \
	clips/bucklespring/3c-1.flac \
	clips/bucklespring/3d-0.flac \
	clips/bucklespring/3d-1.flac \
	clips/bucklespring/3e-0.flac \
	clips/bucklespring/3e-1.flac \
	clips/bucklespring/3f-0.flac \
	clips/bucklespring/3f-1.flac \
	clips/bucklespring/40-0.flac \
	clips/bucklespring/40-1.flac \
	clips/bucklespring/41-0.flac \
	clips/bucklespring/41-1.flac \
	clips/bucklespring/42-0.flac \
	clips/bucklespring/42-1.flac \
	clips/bucklespring/43-0.flac \
	clips/bucklespring/43-1.flac \
	clips/bucklespring/44-0.flac \
	clips/bucklespring/44-1.flac \
	clips/bucklespring/45-0.flac \
	clips/bucklespring/45-1.flac \
	clips/bucklespring/46-0.flac \
	clips/bucklespring/46-1.flac \
	clips/bucklespring/47-0.flac \
	clips/bucklespring/47-1.flac \
	clips/bucklespring/48-0.flac \
	clips/bucklespring/48-1.flac \
	clips/bucklespring/49-0.flac \
	clips/bucklespring/49-1.flac \
	clips/bucklespring/4a-0.flac \
	clips/bucklespring/4a-1.flac \
	clips/bucklespring/4b-0.flac \
	clips/bucklespring/4b-1.flac \
	clips/bucklespring/4c-0.flac \
	clips/bucklespring/4c-1.flac \
	clips/bucklespring/4d-0.flac \
	clips/bucklespring/4d-1.flac \
	clips/bucklespring/4e-0.flac \
	clips/bucklespring/4e-1.flac \
	clips/bucklespring/4f-0.flac \
	clips/bucklespring/4f-1.flac \
	clips/bucklespring/50-0.flac \
	clips/bucklespring/50-1.flac \
	clips/bucklespring/51-0.flac \
	clips/bucklespring/51-1.flac \
	clips/bucklespring/52-0.flac \
	clips/bucklespring/52-1.flac \
	clips/bucklespring/53-0.flac \
	clips/bucklespring/53-1.flac \
	clips/bucklespring/56-0.flac \
	clips/bucklespring/56-1.flac \
	clips/bucklespring/57-0.flac \
	clips/bucklespring/57-1.flac \
	clips/bucklespring/58-0.flac \
	clips/bucklespring/58-1.flac \
	clips/bucklespring/5b-0.flac \
	clips/bucklespring/5b-1.flac \
	clips/bucklespring/60-0.flac \
	clips/bucklespring/60-1.flac \
	clips/bucklespring/61-0.flac \
	clips/bucklespring/61-1.flac \
	clips/bucklespring/62-0.flac \
	clips/bucklespring/62-1.flac \
	clips/bucklespring/63-0.flac \
	clips/bucklespring/63-1.flac \
	clips/bucklespring/64-0.flac \
	clips/bucklespring/64-1.flac \
	clips/bucklespring/66-0.flac \
	clips/bucklespring/66-1.flac \
	clips/bucklespring/67-0.flac \
	clips/bucklespring/67-1.flac \
	clips/bucklespring/68-0.flac \
	clips/bucklespring/68-1.flac \
	clips/bucklespring/69-0.flac \
	clips/bucklespring/69-1.flac \
	clips/bucklespring/6a-0.flac \
	clips/bucklespring/6a-1.flac \
	clips/bucklespring/6b-0.flac \
	clips/bucklespring/6b-1.flac \
	clips/bucklespring/6c-0.flac \
	clips/bucklespring/6c-1.flac \
	clips/bucklespring/6d-0.flac \
	clips/bucklespring/6d-1.flac \
	clips/bucklespring/6e-0.flac \
	clips/bucklespring/6e-1.flac \
	clips/bucklespring/6f-0.flac \
	clips/bucklespring/6f-1.flac \
	clips/bucklespring/77-0.flac \
	clips/bucklespring/7d-0.flac \
	clips/bucklespring/7d-1.flac \
	clips/bucklespring/ff-0.flac \
	clips/bucklespring/ff-1.flac
