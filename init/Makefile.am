## Process this file with automake to produce Makefile.in

initconfdir = $(sysconfdir)/init

AM_CFLAGS = \
	$(DBUS_CFLAGS)

AM_CPPFLAGS = \
	-DLOCALEDIR="\"$(localedir)\"" \
	-DCONFFILE="\"$(sysconfdir)/init.conf\"" \
	-DCONFDIR="\"$(initconfdir)\"" \
	-DSBINDIR="\"$(sbindir)\"" \
	-I$(top_builddir) -I$(top_srcdir) -iquote$(builddir) -iquote$(srcdir) \
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
	errors.h
nodist_init_SOURCES = \
	$(com_ubuntu_Upstart_OUTPUTS) \
	$(com_ubuntu_Upstart_Job_OUTPUTS) \
	$(com_ubuntu_Upstart_Instance_OUTPUTS)

init_LDADD = \
	../nih/libnih.la \
	../nih-dbus/libnih-dbus.la \
	$(LTLIBINTL) \
	$(DBUS_LIBS)


com_ubuntu_Upstart_OUTPUTS = \
	com.ubuntu.Upstart.c \
	com.ubuntu.Upstart.h

com_ubuntu_Upstart_XML = \
	../dbus/com.ubuntu.Upstart.xml

$(com_ubuntu_Upstart_OUTPUTS): $(com_ubuntu_Upstart_XML) $(top_builddir)/nih-dbus-tool/nih-dbus-tool
	$(top_builddir)/nih-dbus-tool/nih-dbus-tool \
		--mode=object --prefix=control --output=$@ $<


com_ubuntu_Upstart_Job_OUTPUTS = \
	com.ubuntu.Upstart.Job.c \
	com.ubuntu.Upstart.Job.h

com_ubuntu_Upstart_Job_XML = \
	../dbus/com.ubuntu.Upstart.Job.xml

$(com_ubuntu_Upstart_Job_OUTPUTS): $(com_ubuntu_Upstart_Job_XML)  $(top_builddir)/nih-dbus-tool/nih-dbus-tool
	$(top_builddir)/nih-dbus-tool/nih-dbus-tool \
		--mode=object --prefix=job_class --output=$@ $<


com_ubuntu_Upstart_Instance_OUTPUTS = \
	com.ubuntu.Upstart.Instance.c \
	com.ubuntu.Upstart.Instance.h

com_ubuntu_Upstart_Instance_XML = \
	../dbus/com.ubuntu.Upstart.Instance.xml

$(com_ubuntu_Upstart_Instance_OUTPUTS): $(com_ubuntu_Upstart_Instance_XML) $(top_builddir)/nih-dbus-tool/nih-dbus-tool
	$(top_builddir)/nih-dbus-tool/nih-dbus-tool \
		--mode=object --prefix=job --output=$@ $<


# These have to be built sources because we can't compile object files
# without the header file existing first
BUILT_SOURCES = \
	$(com_ubuntu_Upstart_OUTPUTS) \
	$(com_ubuntu_Upstart_Job_OUTPUTS) \
	$(com_ubuntu_Upstart_Instance_OUTPUTS)

CLEANFILES = \
	$(com_ubuntu_Upstart_OUTPUTS) \
	$(com_ubuntu_Upstart_Job_OUTPUTS) \
	$(com_ubuntu_Upstart_Instance_OUTPUTS)

EXTRA_DIST = \
	$(com_ubuntu_Upstart_XML) \
	$(com_ubuntu_Upstart_Job_XML) \
	$(com_ubuntu_Upstart_Instance_XML)


EXTRA_DIST += init.supp


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

tests: $(BUILT_SOURCES) $(check_PROGRAMS)

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
	../nih-dbus/libnih-dbus.la \
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
	../nih-dbus/libnih-dbus.la \
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
	../nih-dbus/libnih-dbus.la \
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
	../nih-dbus/libnih-dbus.la \
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
	../nih-dbus/libnih-dbus.la \
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
	../nih-dbus/libnih-dbus.la \
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
	../nih-dbus/libnih-dbus.la \
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
	../nih-dbus/libnih-dbus.la \
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
	../nih-dbus/libnih-dbus.la \
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
	../nih-dbus/libnih-dbus.la \
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
	../nih-dbus/libnih-dbus.la \
	$(DBUS_LIBS)


install-data-local:
	$(MKDIR_P) $(DESTDIR)$(initconfdir)

clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
