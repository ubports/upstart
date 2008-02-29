# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([upstart], [0.5.0], [upstart-devel@lists.ubuntu.com])
AC_COPYRIGHT([[Copyright © 2008 Canonical Ltd.]])
AC_CONFIG_SRCDIR([init/main.c])
AC_CONFIG_MACRO_DIR([m4])

AC_GNU_SOURCE

AM_INIT_AUTOMAKE([1.10 gnu nostdinc check-news dist-bzip2])

AM_GNU_GETTEXT_VERSION([0.16.1])
AM_GNU_GETTEXT()

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
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

AC_MSG_CHECKING([whether to include sbindir in PATH])
eval upstart_sbindir=\"$sbindir\"
case $upstart_sbindir in
    /sbin|//sbin|/usr/sbin|/usr/local/sbin)
    	AC_MSG_RESULT([no])
	;;
    *)
    	AC_MSG_RESULT([yes])
	AC_DEFINE_UNQUOTED([EXTRA_PATH], ["$upstart_sbindir"],
			   [Directory to append to path.])
	;;
esac


AC_CONFIG_FILES([ Makefile m4/Makefile po/Makefile.in intl/Makefile
		  nih/Makefile init/Makefile doc/Makefile ])
# util/Makefile compat/sysv/Makefile logd/Makefile
AC_CONFIG_HEADERS([config.h])
AC_OUTPUT
