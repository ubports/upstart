## Process this file with automake to produce Makefile.in

initconfdir = $(sysconfdir)/init

AM_CFLAGS = \
	$(DBUS_CFLAGS)

AM_CPPFLAGS = \
	-DLOCALEDIR="\"$(localedir)\"" \
	-DCONFDIR="\"$(initconfdir)\"" \
	-DSBINDIR="\"$(sbindir)\"" \
	-I$(top_builddir) -I$(top_srcdir) -I$(srcdir) \
	-I$(top_srcdir)/intl


dist_man_MANS = \
	man/init.8


sbin_PROGRAMS = init

init_SOURCES = \
	main.c \
	errors.h paths.h \
	environ.c environ.h \
	process.c process.h \
	job_class.c job_class.h \
	job.c job.h \
	event.c event.h \
	parse_job.c parse_job.h \
	parse_conf.c parse_conf.h \
	conf.c conf.h \
	control.c control.h

init_LDADD = \
	../nih/libnih.la \
	../nih/libnih-dbus.la \
	$(LTLIBINTL) \
	$(DBUS_LIBS)


EXTRA_DIST = init.supp


TESTS = \
	test_environ \
	test_process \
	test_job_class \
	test_job \
	test_event \
	test_parse_job \
	test_parse_conf \
	test_conf \
	test_control

check_PROGRAMS = $(TESTS)

test_environ_SOURCES = tests/test_environ.c
test_environ_LDFLAGS = -static
test_environ_LDADD = \
	environ.o \
	../nih/libnih.la

test_process_SOURCES = tests/test_process.c
test_process_LDFLAGS = -static
test_process_LDADD = \
	environ.o process.o job_class.o job.o event.o \
	parse_job.o parse_conf.o conf.o control.o \
	../nih/libnih.la \
	../nih/libnih-dbus.la \
	$(DBUS_LIBS)

test_job_class_SOURCES = tests/test_job_class.c
test_job_class_LDFLAGS = -static
test_job_class_LDADD = \
	environ.o process.o job_class.o job.o event.o \
	parse_job.o parse_conf.o conf.o control.o \
	../nih/libnih.la \
	../nih/libnih-dbus.la \
	$(DBUS_LIBS)

test_job_SOURCES = tests/test_job.c
test_job_LDFLAGS = -static
test_job_LDADD = \
	environ.o process.o job_class.o job.o event.o \
	parse_job.o parse_conf.o conf.o control.o \
	../nih/libnih.la \
	../nih/libnih-dbus.la \
	$(DBUS_LIBS)

test_event_SOURCES = tests/test_event.c
test_event_LDFLAGS = -static
test_event_LDADD = \
	environ.o process.o job_class.o job.o event.o \
	parse_job.o parse_conf.o conf.o control.o \
	../nih/libnih.la \
	../nih/libnih-dbus.la \
	$(DBUS_LIBS)

test_parse_job_SOURCES = tests/test_parse_job.c
test_parse_job_LDFLAGS = -static
test_parse_job_LDADD = \
	environ.o process.o job_class.o job.o event.o \
	parse_job.o parse_conf.o conf.o control.o \
	../nih/libnih.la \
	../nih/libnih-dbus.la \
	$(DBUS_LIBS)

test_parse_conf_SOURCES = tests/test_parse_conf.c
test_parse_conf_LDFLAGS = -static
test_parse_conf_LDADD = \
	environ.o process.o job_class.o job.o event.o \
	parse_job.o parse_conf.o conf.o control.o \
	../nih/libnih.la \
	../nih/libnih-dbus.la \
	$(DBUS_LIBS)

test_conf_SOURCES = tests/test_conf.c
test_conf_LDFLAGS = -static
test_conf_LDADD = \
	environ.o process.o job_class.o job.o event.o \
	parse_job.o parse_conf.o conf.o control.o \
	../nih/libnih.la \
	../nih/libnih-dbus.la \
	$(DBUS_LIBS)

test_control_SOURCES = tests/test_control.c
test_control_LDFLAGS = -static
test_control_LDADD = \
	environ.o process.o job_class.o job.o event.o \
	parse_job.o parse_conf.o conf.o control.o \
	../nih/libnih.la \
	../nih/libnih-dbus.la \
	$(DBUS_LIBS)


install-data-local:
	$(MKDIR_P) $(DESTDIR)$(initconfdir)
	$(MKDIR_P) $(DESTDIR)$(initconfdir)/conf.d
	$(MKDIR_P) $(DESTDIR)$(initconfdir)/jobs.d

clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
