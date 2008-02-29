## Process this file with automake to produce Makefile.in

initconfdir = $(sysconfdir)/init

AM_CPPFLAGS = \
	-DLOCALEDIR="\"$(localedir)\"" \
	-DCONFDIR="\"$(initconfdir)\"" \
	-DLEGACY_CONFDIR="\"$(sysconfdir)/event.d\"" \
	-DSBINDIR="\"$(sbindir)\"" \
	-I$(top_builddir) -I$(top_srcdir) -I$(srcdir) \
	-I$(top_srcdir)/intl


dist_man_MANS = \
	man/init.8


sbin_PROGRAMS = init

init_SOURCES = \
	main.c \
	errors.h paths.h \
	enum.c enum.h \
	process.c process.h \
	job.c job.h \
	event.c event.h \
	parse_job.c parse_job.h \
	parse_conf.c parse_conf.h \
	conf.c conf.h

init_LDFLAGS = -static
init_LDADD = \
	../nih/libnih.la \
	$(LTLIBINTL)


EXTRA_DIST = init.supp


TESTS = \
	test_enum \
	test_process \
	test_job \
	test_event \
	test_parse_job \
	test_parse_conf \
	test_conf

check_PROGRAMS = $(TESTS)

test_enum_SOURCES = tests/test_enum.c
test_enum_LDFLAGS = -static
test_enum_LDADD = \
	enum.o \
	../nih/libnih.la

test_process_SOURCES = tests/test_process.c
test_process_LDFLAGS = -static
test_process_LDADD = \
	enum.o process.o job.o event.o parse_job.o parse_conf.o conf.o \
	../nih/libnih.la

test_job_SOURCES = tests/test_job.c
test_job_LDFLAGS = -static
test_job_LDADD = \
	enum.o process.o job.o event.o parse_job.o parse_conf.o conf.o \
	../nih/libnih.la

test_event_SOURCES = tests/test_event.c
test_event_LDFLAGS = -static
test_event_LDADD = \
	enum.o process.o job.o event.o parse_job.o parse_conf.o conf.o \
	../nih/libnih.la

test_parse_job_SOURCES = tests/test_parse_job.c
test_parse_job_LDFLAGS = -static
test_parse_job_LDADD = \
	enum.o process.o job.o event.o parse_job.o parse_conf.o conf.o \
	../nih/libnih.la

test_parse_conf_SOURCES = tests/test_parse_conf.c
test_parse_conf_LDFLAGS = -static
test_parse_conf_LDADD = \
	enum.o process.o job.o event.o parse_job.o parse_conf.o conf.o \
	../nih/libnih.la

test_conf_SOURCES = tests/test_conf.c
test_conf_LDFLAGS = -static
test_conf_LDADD = \
	enum.o process.o job.o event.o parse_job.o parse_conf.o conf.o \
	../nih/libnih.la


install-data-local:
	$(MKDIR_P) $(DESTDIR)$(initconfdir)
	$(MKDIR_P) $(DESTDIR)$(initconfdir)/conf.d
	$(MKDIR_P) $(DESTDIR)$(initconfdir)/jobs.d
	$(MKDIR_P) $(DESTDIR)$(sysconfdir)/event.d

clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
