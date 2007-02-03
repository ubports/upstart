## Process this file with automake to produce Makefile.in

AM_CPPFLAGS = \
	-DLOCALEDIR="\"$(localedir)\"" \
	-I$(top_builddir) -I$(top_srcdir) \
	-I$(top_srcdir)/intl


lib_LTLIBRARIES = libupstart.la

libupstart_la_SOURCES = \
	enum.c \
	message.c \
	wire.c


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
	enum.h \
	message.h \
	wire.h \
	errors.h


TESTS = \
	test_enum \
	test_message \
	test_wire

check_PROGRAMS = $(TESTS)

test_enum_SOURCES = tests/test_enum.c
test_enum_LDFLAGS = -static
test_enum_LDADD = libupstart.la

test_message_SOURCES = tests/test_message.c
test_message_LDFLAGS = -static
test_message_LDADD = libupstart.la

test_wire_SOURCES = tests/test_wire.c
test_wire_LDFLAGS = -static
test_wire_LDADD = libupstart.la


clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
