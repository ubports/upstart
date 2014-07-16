# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([upstart], [1.13.1], [upstart-devel@lists.ubuntu.com])
NIH_COPYRIGHT([[Copyright © 2006-2014 Canonical Ltd., 2011 Scott James Remnant]])
AC_CONFIG_SRCDIR([init/main.c])
AC_CONFIG_MACRO_DIR([m4])

AC_GNU_SOURCE

AM_INIT_AUTOMAKE([1.10 gnu nostdinc check-news color-tests silent-rules])
AM_SILENT_RULES([yes])
AM_MAINTAINER_MODE([enable])

LT_PREREQ(2.2.4)
LT_INIT

AM_GNU_GETTEXT_VERSION([0.17])
AM_GNU_GETTEXT([external])

# Checks for programs.
AC_PROG_CC
AC_PROG_CC_C99
AM_PROG_CC_C_O
PKG_PROG_PKG_CONFIG([0.22])

AC_PATH_PROG([NIH_DBUS_TOOL], [nih-dbus-tool])

# Checks for libraries.
NIH_WITH_LOCAL_LIBNIH

PKG_CHECK_MODULES([NIH], [libnih >= 1.0.2])
PKG_CHECK_MODULES([NIH_DBUS], [libnih-dbus >= 1.0.0])
PKG_CHECK_MODULES([DBUS], [dbus-1 >= 1.2.16])
PKG_CHECK_MODULES([GIO], [gio-2.0 >= 2.36], [have_gio=yes], [have_gio=no])
PKG_CHECK_MODULES([UDEV], [libudev >= 146], [have_udev=yes], [have_udev=no])
PKG_CHECK_MODULES([DCONF], [dconf >= 0.14], [have_dconf=yes], [have_dconf=no])

AC_CHECK_HEADER([sys/epoll.h], [have_epoll=yes], [have_epoll=no])

AC_ARG_ENABLE([udev-bridge],
	AS_HELP_STRING([--disable-udev-bridge],
		[Disable building of upstart-udev-bridge even if required dependencies available]),
		[udev_bridge=no], [udev_bridge=yes])

AM_CONDITIONAL([ENABLE_UDEV_BRIDGE], [test "$have_udev" = yes && test "$udev_bridge" = yes])

AC_ARG_ENABLE([socket-bridge],
	AS_HELP_STRING([--disable-socket-bridge],
		[Disable building of upstart-socket-bridge even if required dependencies available]),
		[socket_bridge=no], [socket_bridge=yes])

AM_CONDITIONAL([ENABLE_SOCKET_BRIDGE], [test "$have_epoll" = yes && test "$socket_bridge" = yes])

AC_ARG_ENABLE([local-bridge],
	AS_HELP_STRING([--disable-local-bridge],
		[Disable building of upstart-local-bridge even if required dependencies available]),
		[local_bridge=no], [local_bridge=yes])

AM_CONDITIONAL([ENABLE_LOCAL_BRIDGE], [test "$local_bridge" = yes])


AC_ARG_ENABLE([dconf-bridge],
	AS_HELP_STRING([--disable-dconf-bridge],
		[Disable building of upstart-dconf-bridge even if required dependencies available]),
		[dconf_bridge=no], [dconf_bridge=yes])

AM_CONDITIONAL([ENABLE_DCONF_BRIDGE], [test "$have_dconf" = yes &&
                                test "$have_gio" = yes &&
				test "$dconf_bridge" = yes])

#AC_CHECK_DECL([TEST_PLAN],[
#have_tap=yes
#AC_REQUIRE_AUX_FILE([tap-driver.sh])
#AC_DEFINE([NIH_TAP_OUTPUT], [1], [Do not abort on test failure])
#],[have_tap=no],[[#include <nih/test.h>]])
have_tap=no
AM_CONDITIONAL([ENABLE_TAP_OUTPUT], [test "$have_tap" = "yes"])

AC_ARG_ENABLE([cgroups],
	AS_HELP_STRING([--disable-cgroups],
		[Disable support for Linux cgroups]),
		[enable_cgroups=no], [enable_cgroups=yes])
PKG_CHECK_MODULES([CGMANAGER], [libcgmanager], [have_cgmanager=yes], [have_cgmanager=no])
AM_CONDITIONAL([ENABLE_CGROUPS], [test "$enable_cgroups" = yes && test "$have_cgmanager" = yes])

if test "$enable_cgroups" = yes && test "$have_cgmanager" = yes; then
	AC_DEFINE([ENABLE_CGROUPS], [1], [Build with Cgroup CGManager support])
fi

# Reasons for requiring this library version:
#
# 1) RFC 4627, the JSON "memo" (it is *NOT* a standard!) helpfully fails
#    to specify the maximum size of an integer type. And yet, JSON is
#    supposedly a subset of ECMA-262, which specifies a Number type to
#    be 64-bits.
#
#    The loose JSON "memo" might expalain why older versions of JSON-C encode
#    a JSON Number in sizoef(int) bytes which is only 32-bits on 32-bit
#    systems. This is not acceptable for Upstart which needs to be able
#    to encode 'size_t' and 'unsigned long' types which can be larger
#    than 'int'.
#
# 2) New json_tokener_parse_verbose() function required to detect invalid
#    JSON (!)
#
PKG_CHECK_MODULES([JSON], [json-c], [],
	[PKG_CHECK_MODULES([JSON], [json >= 0.10])])

AC_ARG_ENABLE([abi-check],
	AS_HELP_STRING([--disable-abi-check],
		[Disable libupstart ABI check performed if abi-compliance-checker available]),
		[abi_check=no], [abi_check=yes])

if test x$abi_check = xyes; then
	AC_CHECK_PROGS([ABI_COMPLIANCE_CHECKER],[abi-compliance-checker])
	AC_SUBST([ABI_COMPLIANCE_CHECKER])
fi

AM_CONDITIONAL([HAVE_ABI_CHECKER], [test ! -z "$ABI_COMPLIANCE_CHECKER" && test -e "$srcdir"/lib/abi/"$host_cpu"-"$host_os"/*.abi])

# Checks for header files.
AC_CHECK_HEADERS([valgrind/valgrind.h, sys/prctl.h])

# Checks for typedefs, structures, and compiler characteristics.
NIH_C_THREAD

AC_CHECK_SIZEOF(int)
AC_CHECK_SIZEOF(uid_t)
AC_CHECK_SIZEOF(pid_t)
AC_CHECK_SIZEOF(time_t)
AC_CHECK_SIZEOF(mode_t)
AC_CHECK_SIZEOF(size_t)
AC_CHECK_SIZEOF(ssize_t)

# Unlikely to hit this limit for a while, but it pays to be safe.
for type in int uid_t pid_t time_t mode_t size_t ssize_t
do
	eval value="\$ac_cv_sizeof_${type}"
	test "$value" -gt 8 && AC_MSG_ERROR([type $type is $value bytes, larger than JSON-C can represent])
done

# Checks for library functions.

# Other checks
AC_MSG_CHECKING([whether to include sbindir in PATH])
eval upstart_sbindir=\"$sbindir\"
case $upstart_sbindir in
    /sbin|NONE/sbin|//sbin|/usr/sbin|/usr/local/sbin)
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


AC_CONFIG_FILES([ Makefile
		  dbus/Makefile init/Makefile util/Makefile conf/Makefile
		  extra/Makefile doc/Makefile contrib/Makefile po/Makefile.in
		  scripts/Makefile scripts/data/Makefile
		  test/Makefile lib/Makefile lib/libupstart.pc.in ])
AC_CONFIG_HEADERS([config.h])
AC_OUTPUT
