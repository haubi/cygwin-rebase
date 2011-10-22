# -*- autoconf -*- vim: filetype=config
# configure.ac for rebase

AC_PREREQ([2.64])
AC_INIT([rebase], [4.0.0], [cygwin@cygwin.com])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_SRCDIR([peflags.c])
AC_PREFIX_DEFAULT([/usr])
AC_CANONICAL_HOST

AC_ARG_WITH([dash], AS_HELP_STRING([use dash instead of ash]),
            [], [with_dash=no])
ASH=ash
AS_IF([test "x$with_dash" != xno], [ASH=dash])
AC_SUBST([ASH])

AC_PROG_INSTALL
AC_PROG_MKDIR_P
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_SED
AC_PROG_EGREP
AC_PROG_FGREP

AC_PROG_CC
AC_PROG_CXX
AC_CHECK_TOOL(AR, ar, ar)

AC_CHECK_DECLS([cygwin_conv_path], [],[
  case "$host" in
  *cygwin* ) AC_MSG_ERROR([At least cygwin-1.7 is required]) ;;
  esac],dnl
  [[#include <sys/cygwin.h>]])

LIBOBJS=
case "$host" in
  *msys* ) AC_LIBOBJ([getopt_long])
           AC_LIBOBJ([strtoll]) ;;
esac
AC_SUBST([LIBOBJS])

case "$host" in
 *mingw* | *msys* )	DEFAULT_OFFSET_VALUE=0x10000 ;;
 *)			DEFAULT_OFFSET_VALUE=0 ;;
esac
AC_SUBST(DEFAULT_OFFSET_VALUE)

case "$host" in
 *msys*   )	EXTRA_CFLAG_OVERRIDES=
		EXTRA_LDFLAG_OVERRIDES="-static-libgcc"
		EXTRA_CXX_LDFLAG_OVERRIDES=
   ;;
 *cygwin* )	EXTRA_CFLAG_OVERRIDES=
		EXTRA_LDFLAG_OVERRIDES="-static-libgcc"
		EXTRA_CXX_LDFLAG_OVERRIDES="-static-libstdc++"
   ;;
 *mingw*  )	EXTRA_CFLAG_OVERRIDES="-D__USE_MINGW_ANSI_STDIO"
		EXTRA_LDFLAG_OVERRIDES="-static-libgcc"
		EXTRA_CXX_LDFLAG_OVERRIDES="-static-libstdc++"
   ;;
esac
AC_SUBST(EXTRA_CFLAG_OVERRIDES)
AC_SUBST(EXTRA_LDFLAG_OVERRIDES)
AC_SUBST(EXTRA_CXX_LDFLAG_OVERRIDES)

AC_CONFIG_FILES([Makefile
                 imagehelper/Makefile])

AC_OUTPUT

