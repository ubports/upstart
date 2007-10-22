## Process this file with automake to produce Makefile.in

AM_CPPFLAGS = \
	-DLOCALEDIR="\"$(localedir)\"" \
	-DCFG_DIR="\"$(sysconfdir)/event.d\"" \
	-DTELINIT="\"$(sbindir)/telinit\"" \
	-I$(top_builddir) -I$(top_srcdir) -I$(srcdir) \
	-I$(top_srcdir)/intl


dist_man_MANS = \
	man/init.8


sbin_PROGRAMS = init

init_SOURCES = \
	main.c \
	errors.h paths.h \
	process.c process.h \
	job.c job.h \
	event.c event.h \
	control.c control.h \
	notify.c notify.h \
	cfgfile.c cfgfile.h

init_LDFLAGS = -static
init_LDADD = \
	../upstart/libupstart.la \
	../nih/libnih.la \
	$(LTLIBINTL)


EXTRA_DIST = init.supp


TESTS = \
	test_process \
	test_job \
	test_event \
	test_control \
	test_notify \
	test_cfgfile

check_PROGRAMS = $(TESTS)

test_job_SOURCES = tests/test_job.c
test_job_LDFLAGS = -static
test_job_LDADD = \
	process.o job.o event.o control.o notify.o \
	../upstart/libupstart.la ../nih/libnih.la

test_process_SOURCES = tests/test_process.c
test_process_LDFLAGS = -static
test_process_LDADD = \
	process.o job.o event.o control.o notify.o \
	../upstart/libupstart.la ../nih/libnih.la

test_event_SOURCES = tests/test_event.c
test_event_LDFLAGS = -static
test_event_LDADD = \
	process.o job.o event.o control.o notify.o \
	../upstart/libupstart.la ../nih/libnih.la

test_control_SOURCES = tests/test_control.c
test_control_LDFLAGS = -static
test_control_LDADD = \
	process.o job.o event.o control.o notify.o \
	../upstart/libupstart.la ../nih/libnih.la

test_notify_SOURCES = tests/test_notify.c
test_notify_LDFLAGS = -static
test_notify_LDADD = \
	process.o job.o event.o control.o notify.o \
	../upstart/libupstart.la ../nih/libnih.la

test_cfgfile_SOURCES = tests/test_cfgfile.c
test_cfgfile_LDFLAGS = -static
test_cfgfile_LDADD = \
	process.o job.o event.o control.o notify.o cfgfile.o \
	../upstart/libupstart.la ../nih/libnih.la


clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
