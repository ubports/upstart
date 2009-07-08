## Process this file with automake to produce Makefile.in

AM_CFLAGS = \
	$(DBUS_CFLAGS)

AM_CPPFLAGS = \
	-DLOCALEDIR="\"$(localedir)\"" \
	-DSBINDIR="\"$(sbindir)\"" \
	-I$(top_builddir) -I$(top_srcdir) -iquote$(builddir) -iquote$(srcdir) \
	-I$(top_srcdir)/intl


dist_man_MANS = \
	man/initctl.8 \
	man/initctl.7 \
	man/reboot.8 \
	man/shutdown.8 \
	man/runlevel.8 \
	man/telinit.8


sbin_PROGRAMS = \
	initctl \
	reboot \
	runlevel \
	shutdown \
	telinit

initctl_SOURCES = \
	initctl.c
nodist_initctl_SOURCES = \
	$(com_ubuntu_Upstart_OUTPUTS) \
	$(com_ubuntu_Upstart_Job_OUTPUTS) \
	$(com_ubuntu_Upstart_Instance_OUTPUTS)
initctl_LDFLAGS = -static
initctl_LDADD = \
	../nih/libnih.la \
	../nih-dbus/libnih-dbus.la \
	$(LTLIBINTL) \
	$(DBUS_LIBS)

reboot_SOURCES = \
	reboot.c \
	utmp.c utmp.h
reboot_LDFLAGS = -static
reboot_LDADD = \
	../nih/libnih.la \
	$(LTLIBINTL)

runlevel_SOURCES = \
	runlevel.c \
	utmp.c utmp.h
runlevel_LDFLAGS = -static
runlevel_LDADD = \
	../nih/libnih.la \
	$(LTLIBINTL)

shutdown_SOURCES = \
	shutdown.c \
	utmp.c utmp.h \
	sysv.c sysv.h
nodist_shutdown_SOURCES = \
	$(com_ubuntu_Upstart_OUTPUTS)
shutdown_LDFLAGS = -static
shutdown_LDADD = \
	../nih/libnih.la \
	../nih-dbus/libnih-dbus.la \
	$(LTLIBINTL) \
	$(DBUS_LIBS)

telinit_SOURCES = \
	telinit.c \
	utmp.c utmp.h \
	sysv.c sysv.h
nodist_telinit_SOURCES = \
	$(com_ubuntu_Upstart_OUTPUTS)
telinit_LDFLAGS = -static
telinit_LDADD = \
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
		--mode=proxy --prefix=upstart --default-interface=com.ubuntu.Upstart0_6 \
		--output=$@ $<


com_ubuntu_Upstart_Job_OUTPUTS = \
	com.ubuntu.Upstart.Job.c \
	com.ubuntu.Upstart.Job.h

com_ubuntu_Upstart_Job_XML = \
	../dbus/com.ubuntu.Upstart.Job.xml

$(com_ubuntu_Upstart_Job_OUTPUTS): $(com_ubuntu_Upstart_Job_XML)  $(top_builddir)/nih-dbus-tool/nih-dbus-tool
	$(top_builddir)/nih-dbus-tool/nih-dbus-tool \
		--mode=proxy --prefix=job_class --default-interface=com.ubuntu.Upstart0_6.Job \
		--output=$@ $<


com_ubuntu_Upstart_Instance_OUTPUTS = \
	com.ubuntu.Upstart.Instance.c \
	com.ubuntu.Upstart.Instance.h

com_ubuntu_Upstart_Instance_XML = \
	../dbus/com.ubuntu.Upstart.Instance.xml

$(com_ubuntu_Upstart_Instance_OUTPUTS): $(com_ubuntu_Upstart_Instance_XML) $(top_builddir)/nih-dbus-tool/nih-dbus-tool
	$(top_builddir)/nih-dbus-tool/nih-dbus-tool \
		--mode=proxy --prefix=job --default-interface=com.ubuntu.Upstart0_6.Instance \
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


install-data-hook:
	src=`echo initctl | sed '$(transform)'`.8; \
	for symlink in start stop restart status; do \
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
	for symlink in start stop restart status; do \
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


TESTS = \
	test_initctl \
	test_utmp \
	test_sysv

check_PROGRAMS = $(TESTS)

test_initctl_SOURCES = tests/test_initctl.c initctl.c
test_initctl_CFLAGS = $(AM_CFLAGS) -DTEST
test_initctl_LDFLAGS = -static
test_initctl_LDADD = \
	com.ubuntu.Upstart.o \
	com.ubuntu.Upstart.Job.o com.ubuntu.Upstart.Instance.o \
	../nih/libnih.la \
	../nih-dbus/libnih-dbus.la \
	$(DBUS_LIBS)

test_utmp_SOURCES = tests/test_utmp.c
test_utmp_LDFLAGS = -static
test_utmp_LDADD = \
	utmp.o \
	../nih/libnih.la

test_sysv_SOURCES = tests/test_sysv.c
nodist_test_sysv_SOURCES = \
	$(com_ubuntu_Upstart_OUTPUTS)
test_sysv_LDFLAGS = -static
test_sysv_LDADD = \
	sysv.o utmp.o \
	../nih/libnih.la \
	../nih-dbus/libnih-dbus.la \
	$(LTLIBINTL) \
	$(DBUS_LIBS)


.PHONY: tests
tests: $(BUILT_SOURCES) $(check_PROGRAMS)

clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
