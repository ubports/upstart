## Process this file with automake to produce Makefile.in

INITCTL_BINARY = $(abs_top_builddir)/util/initctl
UPSTART_BINARY = $(abs_top_builddir)/init/init

AM_CFLAGS = \
	$(NIH_CFLAGS) \
	$(NIH_DBUS_CFLAGS) \
	$(DBUS_CFLAGS)

AM_CPPFLAGS = \
	-DLOCALEDIR="\"$(localedir)\"" \
	-DSBINDIR="\"$(sbindir)\"" \
	-DUPSTART_BINARY="\"$(UPSTART_BINARY)\"" \
	-DINITCTL_BINARY="\"$(INITCTL_BINARY)\"" \
	-I$(top_builddir) -I$(top_srcdir) -iquote$(builddir) -iquote$(srcdir) \
	-I$(top_srcdir)/intl

check_LIBRARIES = libtest_util_common.a
libtest_util_common_a_SOURCES = \
	test_util_common.c \
	test_util_common.h

if ENABLE_TAP_OUTPUT
LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) $(top_srcdir)/tap-driver.sh
else
LOG_DRIVER = $(SHELL) $(top_srcdir)/test-driver
endif

TESTS = test_util_check_env
check_PROGRAMS = $(TESTS)

.PHONY: tests
tests: $(check_PROGRAMS)

test_util_check_env_SOURCES = tests/test_util_check_env.c
test_util_check_env_LDADD = \
	libtest_util_common.a \
	$(NIH_LIBS)
