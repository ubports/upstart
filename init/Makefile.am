## Process this file with automake to produce Makefile.in

initconfdir = $(sysconfdir)/init

AM_MAKEFLAGS = TERM=xterm

AM_CFLAGS = \
	$(NIH_CFLAGS) \
	$(NIH_DBUS_CFLAGS) \
	$(DBUS_CFLAGS) \
	$(JSON_CFLAGS) \
	$(CGMANAGER_CFLAGS)
AM_LIBADD = -lcgmanager $(NIH_LIBS) $(NIH_DBUS_LIBS)

AM_CPPFLAGS = \
	-DLOCALEDIR="\"$(localedir)\"" \
	-DCONFFILE="\"$(sysconfdir)/init.conf\"" \
	-DCONFDIR="\"$(initconfdir)\"" \
	-DSBINDIR="\"$(sbindir)\"" \
	-I$(top_builddir) -I$(top_srcdir) -iquote$(builddir) -iquote$(srcdir) \
	-I$(top_srcdir)/intl \
	-I$(top_srcdir)/test -iquote$(top_srcdir)/test

dist_man_MANS = \
	man/init.8 \
	man/init.5 \
	man/startup.7 \
	man/starting.7 \
	man/started.7 \
	man/stopping.7 \
	man/stopped.7 \
	man/control-alt-delete.7 \
	man/keyboard-request.7 \
	man/power-status-changed.7 \
	man/upstart.7 \
	man/inittab.5 \
	man/session-end.7


sbin_PROGRAMS = init

init_SOURCES = \
	main.c \
	paths.h \
	events.h \
	system.c system.h \
	environ.c environ.h \
	process.c process.h \
	session.c session.h \
	state.c state.h \
	job_class.c job_class.h \
	job_process.c job_process.h \
	job.c job.h \
	log.c log.h \
	event.c event.h \
	event_operator.c event_operator.h \
	blocked.c blocked.h \
	parse_job.c parse_job.h \
	parse_conf.c parse_conf.h \
	conf.c conf.h \
	control.c control.h \
	xdg.c xdg.h \
	quiesce.c quiesce.h \
	errors.h \
	apparmor.c apparmor.h
nodist_init_SOURCES = \
	$(com_ubuntu_Upstart_OUTPUTS) \
	$(com_ubuntu_Upstart_Job_OUTPUTS) \
	$(com_ubuntu_Upstart_Instance_OUTPUTS)

init_LDADD = \
	$(LTLIBINTL) \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS) \
	$(JSON_LIBS) \
	$(CGMANAGER_LIBS) \
	-lrt

if ENABLE_CGROUPS
init_SOURCES +=	cgroup.c cgroup.h
endif

com_ubuntu_Upstart_OUTPUTS = \
	com.ubuntu.Upstart.c \
	com.ubuntu.Upstart.h

com_ubuntu_Upstart_XML = \
	../dbus/com.ubuntu.Upstart.xml

$(com_ubuntu_Upstart_OUTPUTS): $(com_ubuntu_Upstart_XML)
	$(AM_V_GEN)$(NIH_DBUS_TOOL) \
		--package=$(PACKAGE) \
		--mode=object --prefix=control \
		--default-interface=com.ubuntu.Upstart0_6 \
		--output=$@ $<


com_ubuntu_Upstart_Job_OUTPUTS = \
	com.ubuntu.Upstart.Job.c \
	com.ubuntu.Upstart.Job.h

com_ubuntu_Upstart_Job_XML = \
	../dbus/com.ubuntu.Upstart.Job.xml

$(com_ubuntu_Upstart_Job_OUTPUTS): $(com_ubuntu_Upstart_Job_XML)
	$(AM_V_GEN)$(NIH_DBUS_TOOL) \
		--package=$(PACKAGE) \
		--mode=object --prefix=job_class \
		--default-interface=com.ubuntu.Upstart0_6.Job \
		--output=$@ $<


com_ubuntu_Upstart_Instance_OUTPUTS = \
	com.ubuntu.Upstart.Instance.c \
	com.ubuntu.Upstart.Instance.h

com_ubuntu_Upstart_Instance_XML = \
	../dbus/com.ubuntu.Upstart.Instance.xml

$(com_ubuntu_Upstart_Instance_OUTPUTS): $(com_ubuntu_Upstart_Instance_XML)
	$(AM_V_GEN)$(NIH_DBUS_TOOL) \
		--package=$(PACKAGE) \
		--mode=object --prefix=job \
		--default-interface=com.ubuntu.Upstart0_6.Instance \
		--output=$@ $<

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

TEST_DATA_DIR = $(srcdir)/tests/data

AM_CPPFLAGS += -DTEST_DATA_DIR="\"$(TEST_DATA_DIR)\""

TEST_DATA_FILES = \
	$(TEST_DATA_DIR)/upstart-1.6.json \
	$(TEST_DATA_DIR)/upstart-1.8.json \
	$(TEST_DATA_DIR)/upstart-pre-security.json \
	$(TEST_DATA_DIR)/upstart-1.8+full_serialisation-apparmor.json \
	$(TEST_DATA_DIR)/upstart-1.8+apparmor.json \
	$(TEST_DATA_DIR)/upstart-1.8+full_serialisation+apparmor.json \
	$(TEST_DATA_DIR)/upstart-session.json \
	$(TEST_DATA_DIR)/upstart-session2.json \
	$(TEST_DATA_DIR)/upstart-session-infinity.json \
	$(TEST_DATA_DIR)/upstart-1.9.json \
	$(TEST_DATA_DIR)/upstart-1.11.json

upstart_test_programs = \
	test_system \
	test_environ \
	test_process \
	test_job_class \
	test_job_process \
	test_job \
	test_log \
	test_state \
	test_event \
	test_event_operator \
	test_blocked \
	test_parse_job \
	test_parse_conf \
	test_conf_static \
	test_xdg \
	test_control \
	test_main

if ENABLE_CGROUPS
upstart_test_programs += test_cgroup
endif

if ENABLE_TAP_OUTPUT
LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) $(top_srcdir)/tap-driver.sh
else
LOG_DRIVER = $(SHELL) $(top_srcdir)/test-driver
endif

# Ensure a clean test environment (that will not touch the running users
# one). Tests that need the variables listed below are expected to set
# them to appropriate temporary values as needed.
AM_TESTS_ENVIRONMENT= \
	unset -v \
		XDG_RUNTIME_DIR \
		DBUS_SESSION_BUS_ADDRESS \
		UPSTART_SESSION \
		UPSTART_INSTANCE \
		UPSTART_EVENTS \
		UPSTART_JOB;

TESTS = \
	$(upstart_test_programs) \
	test_conf_preload.sh$(EXEEXT)

EXTRA_DIST = init.supp $(TEST_DATA_FILES) tests/test_conf_preload.sh.in

test_conf_preload.sh$(EXEEXT): tests/test_conf_preload.sh.in Makefile test_conf
	sed -e 's|[@]abs_builddir[@]|$(abs_builddir)|g' \
	    -e 's|[@]inotify_preload_library[@]|$(check_LTLIBRARIES)|g' \
	    $< > $@
	chmod +x $@

check_PROGRAMS = $(upstart_test_programs) test_conf

check_SCRIPTS = test_conf_preload.sh$(EXEEXT)
CLEANFILES += $(check_SCRIPTS)

tests: $(BUILT_SOURCES) $(check_PROGRAMS) $(check_LTLIBRARIES) $(top_builddir)/util/initctl $(top_builddir)/test/libtest_util_common.a

test_system_SOURCES = tests/test_system.c
test_system_LDADD = \
	system.o \
	$(NIH_LIBS)

test_environ_SOURCES = tests/test_environ.c
test_environ_LDADD = \
	environ.o \
	$(NIH_LIBS)

test_process_SOURCES = tests/test_process.c
test_process_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o conf.o control.o quiesce.o \
	session.o log.o state.o xdg.o apparmor.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	$(top_builddir)/test/libtest_util_common.a \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS) \
	$(JSON_LIBS) \
	-lrt
if ENABLE_CGROUPS
test_process_LDADD += cgroup.o $(CGMANAGER_LIBS)
endif

test_job_class_SOURCES = tests/test_job_class.c
test_job_class_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o conf.o control.o quiesce.o \
	session.o log.o state.o xdg.o apparmor.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS) \
	$(JSON_LIBS) \
	-lrt
if ENABLE_CGROUPS
test_job_class_LDADD += cgroup.o $(CGMANAGER_LIBS)
endif

test_job_process_SOURCES = tests/test_job_process.c
test_job_process_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o conf.o control.o quiesce.o \
	session.o log.o state.o xdg.o apparmor.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	$(top_builddir)/test/libtest_util_common.a \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS) \
	$(JSON_LIBS) \
	-lrt -lutil
if ENABLE_CGROUPS
test_job_process_LDADD += cgroup.o $(CGMANAGER_LIBS)
endif

test_job_SOURCES = tests/test_job.c
test_job_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o conf.o control.o quiesce.o \
	session.o log.o state.o xdg.o apparmor.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	$(top_builddir)/test/libtest_util_common.a \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS) \
	$(JSON_LIBS) \
	-lrt
if ENABLE_CGROUPS
test_job_LDADD += cgroup.o $(CGMANAGER_LIBS)
endif

test_log_SOURCES = tests/test_log.c
test_log_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o conf.o control.o quiesce.o \
	session.o log.o state.o xdg.o apparmor.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	$(top_builddir)/test/libtest_util_common.a \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS) \
	$(JSON_LIBS) \
	-lrt -lutil
if ENABLE_CGROUPS
test_log_LDADD += cgroup.o $(CGMANAGER_LIBS)
endif

test_state_SOURCES = tests/test_state.c tests/test_util.c tests/test_util.h
test_state_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o conf.o control.o quiesce.o \
	session.o log.o state.o xdg.o apparmor.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	$(top_builddir)/test/libtest_util_common.a \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS) \
	$(JSON_LIBS) \
	-lrt -lutil
if ENABLE_CGROUPS
test_state_LDADD += cgroup.o $(CGMANAGER_LIBS)
endif

test_event_SOURCES = tests/test_event.c
test_event_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o conf.o control.o quiesce.o \
	session.o log.o state.o xdg.o apparmor.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS) \
	$(JSON_LIBS) \
	-lrt
if ENABLE_CGROUPS
test_event_LDADD += cgroup.o $(CGMANAGER_LIBS)
endif

test_event_operator_SOURCES = tests/test_event_operator.c tests/test_util.c tests/test_util.h
test_event_operator_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o conf.o control.o quiesce.o \
	session.o log.o state.o xdg.o apparmor.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	$(top_builddir)/test/libtest_util_common.a \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS) \
	$(JSON_LIBS) \
	-lrt
if ENABLE_CGROUPS
test_event_operator_LDADD += cgroup.o $(CGMANAGER_LIBS)
endif

test_blocked_SOURCES = tests/test_blocked.c
test_blocked_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o conf.o control.o quiesce.o \
	session.o log.o state.o xdg.o apparmor.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS) \
	$(JSON_LIBS) \
	-lrt
if ENABLE_CGROUPS
test_blocked_LDADD += cgroup.o $(CGMANAGER_LIBS)
endif

test_parse_job_SOURCES = tests/test_parse_job.c
test_parse_job_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o conf.o control.o quiesce.o \
	session.o log.o state.o xdg.o apparmor.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	$(top_builddir)/test/libtest_util_common.a \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS) \
	$(JSON_LIBS) \
	-lrt
if ENABLE_CGROUPS
test_parse_job_LDADD += cgroup.o $(CGMANAGER_LIBS)
endif

test_parse_conf_SOURCES = tests/test_parse_conf.c
test_parse_conf_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o conf.o control.o quiesce.o \
	session.o log.o state.o xdg.o apparmor.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS) \
	$(JSON_LIBS) \
	-lrt
if ENABLE_CGROUPS
test_parse_conf_LDADD += cgroup.o $(CGMANAGER_LIBS)
endif

check_LTLIBRARIES = tests/libwrap_inotify.la
tests_libwrap_inotify_la_SOURCES = tests/wrap_inotify.c
tests_libwrap_inotify_la_LIBADD = -lrt -ldl $(AM_LIBADD)
tests_libwrap_inotify_la_LDFLAGS = avoid-version -module -shared -export-dynamic -rpath /nowhere -ldl

test_conf_SOURCES = tests/test_conf.c $(check_LTLIBRARIES)
test_conf_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o conf.o control.o quiesce.o \
	session.o log.o state.o xdg.o apparmor.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	$(top_builddir)/test/libtest_util_common.a \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS) \
	$(JSON_LIBS) \
	-lrt
if ENABLE_CGROUPS
test_conf_LDADD += cgroup.o $(CGMANAGER_LIBS)
endif

test_conf_static_SOURCES = tests/test_conf_static.c
test_conf_static_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o control.o quiesce.o \
	session.o log.o state.o xdg.o apparmor.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS) \
	$(JSON_LIBS) \
	-lrt
if ENABLE_CGROUPS
test_conf_static_LDADD += cgroup.o $(CGMANAGER_LIBS)
endif

test_xdg_SOURCES = tests/test_xdg.c
test_xdg_LDADD = \
	xdg.o \
	environ.o \
	$(NIH_LIBS) \
	$(top_builddir)/test/libtest_util_common.a \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS) \
	-lcgmanager \
	-lrt

test_cgroup_SOURCES = tests/test_cgroup.c
test_cgroup_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o conf.o control.o quiesce.o \
	session.o log.o state.o xdg.o apparmor.o cgroup.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	$(top_builddir)/test/libtest_util_common.a \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS) \
	$(JSON_LIBS) \
	$(CGMANAGER_LIBS) \
	-lrt

test_control_SOURCES = tests/test_control.c
test_control_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o conf.o control.o quiesce.o \
	session.o log.o state.o xdg.o apparmor.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS) \
	$(JSON_LIBS) \
	-lrt
if ENABLE_CGROUPS
test_control_LDADD += cgroup.o $(CGMANAGER_LIBS)
endif

test_main_SOURCES = tests/test_main.c
test_main_LDADD = \
	system.o environ.o process.o \
	job_class.o job_process.o job.o event.o event_operator.o blocked.o \
	parse_job.o parse_conf.o conf.o control.o quiesce.o \
	session.o log.o state.o xdg.o apparmor.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	$(top_builddir)/test/libtest_util_common.a \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS) \
	$(JSON_LIBS) \
	-lrt
if ENABLE_CGROUPS
test_main_LDADD += cgroup.o $(CGMANAGER_LIBS)
endif

install-data-local:
	$(MKDIR_P) $(DESTDIR)$(initconfdir)

clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov

FORCE:

$(top_builddir)/util/initctl $(top_builddir)/test/libtest_util_common.a: FORCE
	(cd $(top_builddir) && $(MAKE) $(AM_MAKEFLAGS) )
	(cd $(top_builddir)/test && $(MAKE) $(AM_MAKEFLAGS) libtest_util_common.a)
