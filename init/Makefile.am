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
	paths.h \
	events.h \
	system.c system.h \
	environ.c environ.h \
	process.c process.h \
	job_class.c job_class.h \
	job_process.c job_process.h \
	job.c job.h \
	event.c event.h \
	event_operator.c event_operator.h \
	blocked.c blocked.h \
	parse_job.c parse_job.h \
	parse_conf.c parse_conf.h \
	conf.c conf.h \
	control.c control.h \
	com.ubuntu.Upstart.c com.ubuntu.Upstart.h \
	com.ubuntu.Upstart.Job.c com.ubuntu.Upstart.Job.h \
	com.ubuntu.Upstart.Instance.c com.ubuntu.Upstart.Instance.h \
	errors.h

init_LDADD = \
	../nih/libnih.la \
	../nih/libnih-dbus.la \
	$(LTLIBINTL) \
	$(DBUS_LIBS)


BUILT_SOURCES = \
	com.ubuntu.Upstart.c com.ubuntu.Upstart.h \
	com.ubuntu.Upstart.Job.c com.ubuntu.Upstart.Job.h \
	com.ubuntu.Upstart.Instance.c com.ubuntu.Upstart.Instance.h

com.ubuntu.Upstart.c com.ubuntu.Upstart.h: \
		../dbus/com.ubuntu.Upstart.xml
	$(top_builddir)/nih/nih-dbus-tool \
		--mode=object --prefix=control --output=$@ $<

com.ubuntu.Upstart.Job.c com.ubuntu.Upstart.Job.h: \
		../dbus/com.ubuntu.Upstart.Job.xml
	$(top_builddir)/nih/nih-dbus-tool \
		--mode=object --prefix=job_class --output=$@ $<

com.ubuntu.Upstart.Instance.c com.ubuntu.Upstart.Instance.h: \
		../dbus/com.ubuntu.Upstart.Instance.xml
	$(top_builddir)/nih/nih-dbus-tool \
		--mode=object --prefix=job --output=$@ $<

CLEANFILES = $(BUILT_SOURCES)


EXTRA_DIST = init.supp


TESTS = \
	test_system \
	test_environ \
	test_process \
	test_job_class \
	test_job_process \
	test_job \
	test_event \
	test_event_operator \
	test_blocked \
	test_parse_job \
	test_parse_conf \
	test_conf \
	test_control

check_PROGRAMS = $(TESTS)

test_system_SOURCES = tests/test_system.c
test_system_LDFLAGS = -static
test_system_LDADD = \
	system.o \
	../nih/libnih.la

test_environ_SOURCES = tests/test_environ.c
test_environ_LDFLAGS = -static
test_environ_LDADD = \
	environ.o \
	../nih/libnih.la

test_process_SOURCES = tests/test_process.c
test_process_LDFLAGS = -static
test_process_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o conf.o control.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	../nih/libnih.la \
	../nih/libnih-dbus.la \
	$(DBUS_LIBS)

test_job_class_SOURCES = tests/test_job_class.c
test_job_class_LDFLAGS = -static
test_job_class_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o conf.o control.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	../nih/libnih.la \
	../nih/libnih-dbus.la \
	$(DBUS_LIBS)

test_job_process_SOURCES = tests/test_job_process.c
test_job_process_LDFLAGS = -static
test_job_process_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o conf.o control.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	../nih/libnih.la \
	../nih/libnih-dbus.la \
	$(DBUS_LIBS)

test_job_SOURCES = tests/test_job.c
test_job_LDFLAGS = -static
test_job_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o conf.o control.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	../nih/libnih.la \
	../nih/libnih-dbus.la \
	$(DBUS_LIBS)

test_event_SOURCES = tests/test_event.c
test_event_LDFLAGS = -static
test_event_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o conf.o control.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	../nih/libnih.la \
	../nih/libnih-dbus.la \
	$(DBUS_LIBS)

test_event_operator_SOURCES = tests/test_event_operator.c
test_event_operator_LDFLAGS = -static
test_event_operator_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o conf.o control.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	../nih/libnih.la \
	../nih/libnih-dbus.la \
	$(DBUS_LIBS)

test_blocked_SOURCES = tests/test_blocked.c
test_blocked_LDFLAGS = -static
test_blocked_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o conf.o control.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	../nih/libnih.la \
	../nih/libnih-dbus.la \
	$(DBUS_LIBS)

test_parse_job_SOURCES = tests/test_parse_job.c
test_parse_job_LDFLAGS = -static
test_parse_job_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o conf.o control.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	../nih/libnih.la \
	../nih/libnih-dbus.la \
	$(DBUS_LIBS)

test_parse_conf_SOURCES = tests/test_parse_conf.c
test_parse_conf_LDFLAGS = -static
test_parse_conf_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o conf.o control.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	../nih/libnih.la \
	../nih/libnih-dbus.la \
	$(DBUS_LIBS)

test_conf_SOURCES = tests/test_conf.c
test_conf_LDFLAGS = -static
test_conf_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o conf.o control.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	../nih/libnih.la \
	../nih/libnih-dbus.la \
	$(DBUS_LIBS)

test_control_SOURCES = tests/test_control.c
test_control_LDFLAGS = -static
test_control_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o conf.o control.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
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
