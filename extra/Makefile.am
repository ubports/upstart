## Process this file with automake to produce Makefile.in

AM_CFLAGS = \
	$(NIH_CFLAGS) \
	$(NIH_DBUS_CFLAGS) \
	$(DBUS_CFLAGS) \
	$(DCONF_CFLAGS) \
	$(GIO_CFLAGS) \
	$(UDEV_CFLAGS)

AM_CPPFLAGS = \
	-DLOCALEDIR="\"$(localedir)\"" \
	-DSBINDIR="\"$(sbindir)\"" \
	-I$(top_builddir) -I$(top_srcdir) -iquote$(builddir) -iquote$(srcdir) \
	-I$(top_srcdir)/intl


initdir = $(sysconfdir)/init


sbin_PROGRAMS = \
	upstart-socket-bridge \
	upstart-event-bridge \
	upstart-file-bridge

dist_init_DATA = \
	conf/upstart-socket-bridge.conf \
	conf/upstart-event-bridge.conf \
	conf/upstart-file-bridge.conf

dist_man_MANS = \
	man/upstart-socket-bridge.8 \
	man/upstart-event-bridge.8 \
	man/upstart-file-bridge.8 \
	man/socket-event.7 \
	man/file-event.7

upstart_socket_bridge_SOURCES = \
	upstart-socket-bridge.c
nodist_upstart_socket_bridge_SOURCES = \
	$(com_ubuntu_Upstart_OUTPUTS) \
	$(com_ubuntu_Upstart_Job_OUTPUTS)
upstart_socket_bridge_LDADD = \
	$(LTLIBINTL) \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS)

upstart_event_bridge_SOURCES = \
	upstart-event-bridge.c
nodist_upstart_event_bridge_SOURCES = \
	$(com_ubuntu_Upstart_OUTPUTS) \
	$(com_ubuntu_Upstart_Job_OUTPUTS)
upstart_event_bridge_LDADD = \
	$(LTLIBINTL) \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS)

upstart_file_bridge_SOURCES = \
	upstart-file-bridge.c
nodist_upstart_file_bridge_SOURCES = \
	$(com_ubuntu_Upstart_OUTPUTS) \
	$(com_ubuntu_Upstart_Job_OUTPUTS)
upstart_file_bridge_LDADD = \
	$(LTLIBINTL) \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS)

if HAVE_DCONF
sbin_PROGRAMS += \
	upstart-dconf-bridge

upstart_dconf_bridge_SOURCES = \
	upstart-dconf-bridge.c
nodist_upstart_dconf_bridge_SOURCES = \
	$(com_ubuntu_Upstart_OUTPUTS)
upstart_dconf_bridge_LDADD = \
	$(LTLIBINTL) \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS) \
	$(GIO_LIBS) \
	$(DCONF_LIBS)
endif

if HAVE_UDEV
dist_init_DATA += \
	conf/upstart-udev-bridge.conf

dist_man_MANS += \
	man/upstart-udev-bridge.8

sbin_PROGRAMS += \
	upstart-udev-bridge

upstart_udev_bridge_SOURCES = \
	upstart-udev-bridge.c
nodist_upstart_udev_bridge_SOURCES = \
	$(com_ubuntu_Upstart_OUTPUTS)
upstart_udev_bridge_LDADD = \
	$(LTLIBINTL) \
	$(NIH_LIBS) \
	$(NIH_DBUS_LIBS) \
	$(DBUS_LIBS) \
	$(UDEV_LIBS)

install-data-hook:
	src=`echo upstart-udev-bridge| sed '$(transform)'`.8; \
	for symlink in \
		net-device-added \
		net-device-removed \
		graphics-device-added \
		drm-device-added; do \
	  inst=`echo $$symlink | sed '$(transform)'`.7; \
          echo " ln -sf '$(man8dir)/$$src' '$(DESTDIR)$(man7dir)/$$inst'"; \
	  ln -sf "$(man8dir)/$$src" "$(DESTDIR)$(man7dir)/$$inst"; \
	done

else
EXTRA_DIST = \
	man/upstart-udev-bridge.8
endif


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


# These have to be built sources because we can't compile object files
# without the header file existing first
BUILT_SOURCES = \
	$(com_ubuntu_Upstart_OUTPUTS) \
	$(com_ubuntu_Upstart_Job_OUTPUTS)

CLEANFILES = \
	$(com_ubuntu_Upstart_OUTPUTS) \
	$(com_ubuntu_Upstart_Job_OUTPUTS)


clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
