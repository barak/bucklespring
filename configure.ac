AC_INIT([bucklespring],[1.5.2~dev],[https://github.com/barak/bucklespring/issues],
        [bucklespring],[https://github.com/barak/bucklespring])

AC_CONFIG_SRCDIR([main.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([foreign -Wall])

AC_CANONICAL_HOST
AC_PROG_CC
PKG_PROG_PKG_CONFIG

# Which source of key events to build against.  There is one binary, and it
# reads events exactly one way, so this is a choice at configure time rather
# than something selectable at run time.
AC_ARG_WITH([backend],
  [AS_HELP_STRING([--with-backend=x11|libinput|mac|windows],
     [where to read key events from @<:@default: x11, or mac or windows by host@:>@])],
  [], [with_backend=auto])

AS_IF([test "x$with_backend" = xauto], [
  AS_CASE([$host_os],
    [mingw*|cygwin*|msys*], [with_backend=windows],
    [darwin*],              [with_backend=mac],
                            [with_backend=x11])
])

PKG_CHECK_MODULES([AUDIO], [openal alure])

AS_CASE([$with_backend],
  [x11],      [PKG_CHECK_MODULES([BACKEND], [x11 xtst])],
  [libinput], [PKG_CHECK_MODULES([BACKEND], [libinput libudev wayland-client])],
  [mac],      [BACKEND_LIBS="-framework ApplicationServices -framework OpenAL"],
  [windows],  [BACKEND_LIBS="-lALURE32 -lOpenAL32"],
  [AC_MSG_ERROR([unknown backend "$with_backend"])])

AC_SUBST([BACKEND_CFLAGS])
AC_SUBST([BACKEND_LIBS])

AM_CONDITIONAL([BACKEND_X11],      [test "x$with_backend" = xx11])
AM_CONDITIONAL([BACKEND_LIBINPUT], [test "x$with_backend" = xlibinput])
AM_CONDITIONAL([BACKEND_MAC],      [test "x$with_backend" = xmac])
AM_CONDITIONAL([BACKEND_WINDOWS],  [test "x$with_backend" = xwindows])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

AC_MSG_NOTICE([key event backend: $with_backend])
