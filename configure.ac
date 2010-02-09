# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([upstart], [0.6.6], [upstart-devel@lists.ubuntu.com])
NIH_COPYRIGHT([[Copyright © 2010 Canonical Ltd.]])
AC_CONFIG_SRCDIR([init/main.c])
AC_CONFIG_MACRO_DIR([m4])

AC_GNU_SOURCE

AM_INIT_AUTOMAKE([1.10 gnu nostdinc check-news color-tests silent-rules])
AM_SILENT_RULES([yes])
AM_MAINTAINER_MODE([enable])

LT_PREREQ(2.2.4)
LT_INIT

AM_GNU_GETTEXT_VERSION([0.17])
AM_GNU_GETTEXT()

# Checks for programs.
AC_PROG_CC
PKG_PROG_PKG_CONFIG([0.22])

AC_PATH_PROG([NIH_DBUS_TOOL], [nih-dbus-tool])

# Checks for libraries.
AC_ARG_WITH(local-libnih,
	AS_HELP_STRING([[[--with-local-libnih[=DIR]]]],
		       [Use libnih from source tree DIR]),
[AS_IF([test "x$with_local_libnih" != "xno"],
       [AS_IF([! test -f "$withval/nih/alloc.c"],
       	      [AC_MSG_ERROR([$withval doesn't look like a libnih source tree])])

	nih_dir="`cd $withval && pwd`"

        NIH_CFLAGS="-I\"\\\"$nih_dir\\\"\""
        NIH_LIBS="\"$nih_dir/nih/libnih.la\""
        NIH_DBUS_CFLAGS="-I\"\\\"$nih_dir\\\"\""
	NIH_DBUS_LIBS="\"$nih_dir/nih-dbus/libnih-dbus.la\""
	NIH_DBUS_TOOL="\"$nih_dir/nih-dbus-tool/nih-dbus-tool\""])
])

PKG_CHECK_MODULES([NIH], [libnih >= 1.0.0])
PKG_CHECK_MODULES([NIH_DBUS], [libnih-dbus >= 1.0.0])
PKG_CHECK_MODULES([DBUS], [dbus-1 >= 1.2.16])

# Checks for header files.
AC_CHECK_HEADERS([valgrind/valgrind.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_PROG_CC_C99
AM_PROG_CC_C_O
NIH_C_THREAD

# Checks for library functions.

# Other checks
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

NIH_COMPILER_WARNINGS
NIH_COMPILER_OPTIMISATIONS
NIH_COMPILER_COVERAGE

NIH_LINKER_OPTIMISATIONS


AC_CONFIG_FILES([ Makefile intl/Makefile
		  dbus/Makefile init/Makefile util/Makefile conf/Makefile
		  doc/Makefile contrib/Makefile po/Makefile.in ])
AC_CONFIG_HEADERS([config.h])
AC_OUTPUT
