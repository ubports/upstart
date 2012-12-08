## Process this file with automake to produce Makefile.in

AM_CFLAGS = \
	$(NIH_CFLAGS) \
	$(NIH_DBUS_CFLAGS) \
	$(DBUS_CFLAGS)

AM_CPPFLAGS = \
	-DLOCALEDIR="\"$(localedir)\"" \
	-DSBINDIR="\"$(sbindir)\"" \
	-I$(top_builddir) -I$(top_srcdir) -iquote$(builddir) -iquote$(srcdir) \
	-I$(top_srcdir)/intl


dist_man_MANS = \
	man/initctl.8 \
	man/reboot.8 \
	man/shutdown.8 \
	man/runlevel.8 \
	man/telinit.8 \
	man/runlevel.7


sbin_PROGRAMS = \
	initctl \
	reboot \
	runlevel \
	shutdown \
	telinit

initctl_SOURCES = \
	initctl.c initctl.h
nodist_initctl_SOURCES = \
	$(com_ubuntu_Upstart_OUTPUTS) \
	$(com_ubuntu_Upstart_Job_OUTPUTS) \
	$(com_ubuntu_Upstart_Instance_OUTPUTS)
initctl_LDADD = \
	$(LTLIBINTL) \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS)

reboot_SOURCES = \
	reboot.c \
	utmp.c utmp.h
reboot_LDADD = \
	$(LTLIBINTL) \
	$(NIH_LIBS)

runlevel_SOURCES = \
	runlevel.c \
	utmp.c utmp.h
runlevel_LDADD = \
	$(LTLIBINTL) \
	$(NIH_LIBS)

shutdown_SOURCES = \
	shutdown.c \
	utmp.c utmp.h \
	sysv.c sysv.h
nodist_shutdown_SOURCES = \
	$(com_ubuntu_Upstart_OUTPUTS)
shutdown_LDADD = \
	$(LTLIBINTL) \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS)

telinit_SOURCES = \
	telinit.c \
	utmp.c utmp.h \
	sysv.c sysv.h
nodist_telinit_SOURCES = \
	$(com_ubuntu_Upstart_OUTPUTS) \
	$(com_ubuntu_Upstart_Instance_OUTPUTS)
telinit_LDADD = \
	$(LTLIBINTL) \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS)


com_ubuntu_Upstart_OUTPUTS = \
	com.ubuntu.Upstart.c \
	com.ubuntu.Upstart.h

com_ubuntu_Upstart_XML = \
	../dbus/com.ubuntu.Upstart.xml

$(com_ubuntu_Upstart_OUTPUTS): $(com_ubuntu_Upstart_XML)
	$(AM_V_GEN)$(NIH_DBUS_TOOL) \
		--package=$(PACKAGE) \
		--mode=proxy --prefix=upstart \
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
		--mode=proxy --prefix=job_class \
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
		--mode=proxy --prefix=job \
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


EXTRA_DIST = util.supp


install-data-hook:
	src=`echo initctl | sed '$(transform)'`.8; \
	for symlink in start stop restart reload status; do \
	  inst=`echo $$symlink | sed '$(transform)'`.8; \
          echo " ln -sf '$$src' '$(DESTDIR)$(man8dir)/$$inst'"; \
	  ln -sf "$$src" "$(DESTDIR)$(man8dir)/$$inst"; \
	done

	src=`echo reboot | sed '$(transform)'`.8; \
	for symlink in halt poweroff; do \
	  inst=`echo $$symlink | sed '$(transform)'`.8; \
          echo " ln -sf '$$src' '$(DESTDIR)$(man8dir)/$$inst'"; \
	  ln -sf "$$src" "$(DESTDIR)$(man8dir)/$$inst"; \
	done

install-exec-hook:
	src=`echo initctl | sed '$(transform);s/$$/$(EXEEXT)/'`; \
	for symlink in start stop restart reload status; do \
	  inst=`echo $$symlink | sed '$(transform);s/$$/$(EXEEXT)/'`; \
          echo " ln -sf '$$src' '$(DESTDIR)$(sbindir)/$$inst'"; \
	  ln -sf "$$src" "$(DESTDIR)$(sbindir)/$$inst"; \
	done

	src=`echo reboot | sed '$(transform);s/$$/$(EXEEXT)/'`; \
	for symlink in halt poweroff; do \
	  inst=`echo $$symlink | sed '$(transform);s/$$/$(EXEEXT)/'`; \
          echo " ln -sf '$$src' '$(DESTDIR)$(sbindir)/$$inst'"; \
	  ln -sf "$$src" "$(DESTDIR)$(sbindir)/$$inst"; \
	done

uninstall-hook:
	for symlink in start stop restart reload status halt poweroff; do \
	  file=`echo $$symlink | sed '$(transform)'`.8; \
          echo " rm -f '$(DESTDIR)$(man8dir)/$$file'"; \
	  rm -f "$(DESTDIR)$(man8dir)/$$file"; \
	  file=`echo $$symlink | sed '$(transform);s/$$/$(EXEEXT)/'`; \
          echo " rm -f '$(DESTDIR)$(sbindir)/$$file'"; \
	  rm -f "$(DESTDIR)$(sbindir)/$$file"; \
	done


TESTS = \
	test_initctl \
	test_utmp \
	test_sysv \
	test_telinit

EXTRA_DIST = tests/test_user_sessions.sh

check_PROGRAMS = $(TESTS)

test_initctl_SOURCES = tests/test_initctl.c initctl.c
test_initctl_CFLAGS = $(AM_CFLAGS) -DTEST
test_initctl_LDADD = \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS)

test_utmp_SOURCES = tests/test_utmp.c
test_utmp_LDADD = \
	utmp.o \
	$(NIH_LIBS)

test_sysv_SOURCES = tests/test_sysv.c
nodist_test_sysv_SOURCES = \
	$(com_ubuntu_Upstart_OUTPUTS)
test_sysv_LDADD = \
	sysv.o utmp.o \
	$(LTLIBINTL) \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS)

test_telinit_SOURCES = tests/test_telinit.c telinit.c
test_telinit_CFLAGS = $(AM_CFLAGS) -DTEST
test_telinit_LDADD = \
	sysv.o utmp.o \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS)


.PHONY: tests
tests: $(BUILT_SOURCES) $(check_PROGRAMS)

clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
