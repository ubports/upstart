# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT([upstart], [0.3.0], [new@bugs.launchpad.net])
AC_COPYRIGHT([[Copyright © 2006 Canonical Ltd.]])
AC_CONFIG_SRCDIR([init/main.c])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([1.9 gnu check-news dist-bzip2])

AM_GNU_GETTEXT_VERSION(0.14.1)
AM_GNU_GETTEXT()

# Checks for programs.
AC_PROG_CC
AC_PROG_LIBTOOL

# Checks for libraries.
NIH_INIT

# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

# Other checks
AC_ARG_ENABLE(compat,
	AS_HELP_STRING([--enable-compat],
		       [Enable compatibility tools]),
[case "$enable_compat" in
    yes|sysv) compat=sysv ;;
    no)       compat=none ;;
    *)        AC_MSG_ERROR([bad value $enable_compat for --enable-compat]) ;;
esac], [compat_sysv=none])dnl
AM_CONDITIONAL(COMPAT_SYSV, test "x$compat" = "xsysv")dnl


AC_CONFIG_FILES([ Makefile m4/Makefile po/Makefile.in intl/Makefile
		  nih/Makefile upstart/Makefile init/Makefile initctl/Makefile
		  compat/sysv/Makefile logd/Makefile
		  event.d/Makefile doc/Makefile ])
AC_CONFIG_HEADERS([config.h])
AC_OUTPUT
