## Process this file with automake to produce Makefile.in

DEFAULT_INCLUDES = -I$(top_builddir) -I$(top_srcdir) -I$(srcdir)

localedir = $(datadir)/locale
DEFS += -DLOCALEDIR="\"$(localedir)\""
INCLUDES = -I$(top_srcdir)/intl


lib_LTLIBRARIES = libupstart.la

libupstart_la_SOURCES = \
	job.c \
	control.c


libupstart_la_LDFLAGS = \
	-version-info 0:0:0
if HAVE_VERSION_SCRIPT_ARG
libupstart_la_LDFLAGS += @VERSION_SCRIPT_ARG@=$(srcdir)/libupstart.ver
endif

libupstart_la_LIBADD = \
	../nih/libnih.la


EXTRA_DIST = libupstart.ver


include_HEADERS = \
	libupstart.h

upstartincludedir = $(includedir)/upstart
upstartinclude_HEADERS = \
	job.h \
	control.h \
	errors.h


TESTS = \
	test_job \
	test_control

check_PROGRAMS = $(TESTS)

test_job_SOURCES = tests/test_job.c
test_job_LDFLAGS = -static
test_job_LDADD = libupstart.la

test_control_SOURCES = tests/test_control.c
test_control_LDFLAGS = -static
test_control_LDADD = libupstart.la


clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
