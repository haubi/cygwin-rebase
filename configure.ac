# -*- autoconf -*- vim: filetype=config
# configure.ac for rebase

AC_PREREQ([2.64])
AC_INIT([rebase], [3.0.2], [cygwin@cygwin.com])
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

AC_CHECK_DECLS([cygwin_conv_path], [],[
  case "$host" in
  *cygwin* ) AC_MSG_ERROR([At least cygwin-1.7 is required]) ;;
  esac],dnl
  [[#include <sys/cygwin.h>]])

LIBOBJS=
case "$host" in
  *msys* ) AC_LIBOBJ([getopt_long]) ;;
esac
AC_SUBST([LIBOBJS])

AC_CONFIG_FILES([Makefile
                 imagehelper/Makefile])

AC_OUTPUT

