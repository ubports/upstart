## Process this file with automake to produce Makefile.in

AM_CPPFLAGS = \
	-DLOCALEDIR="\"$(localedir)\"" \
	-DSBINDIR="\"$(sbindir)\"" \
	-I$(top_builddir) -I$(top_srcdir) -iquote$(srcdir) \
	-I$(top_srcdir)/intl


dist_man_MANS = \
	man/initctl.8 \
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

initctl_LDFLAGS = -static
initctl_LDADD = \
	../upstart/libupstart.la \
	../nih/libnih.la \
	$(LTLIBINTL)


reboot_SOURCES = \
	reboot.c

reboot_LDFLAGS = -static
reboot_LDADD = \
	../../upstart/libupstart.la \
	../../nih/libnih.la \
	$(LTLIBINTL)


runlevel_SOURCES = \
	runlevel.c

runlevel_LDFLAGS = -static
runlevel_LDADD = \
	../../upstart/libupstart.la \
	../../nih/libnih.la \
	$(LTLIBINTL)


shutdown_SOURCES = \
	shutdown.c

shutdown_LDFLAGS = -static
shutdown_LDADD = \
	../../upstart/libupstart.la \
	../../nih/libnih.la \
	$(LTLIBINTL)


telinit_SOURCES = \
	telinit.c

telinit_LDFLAGS = -static
telinit_LDADD = \
	../../upstart/libupstart.la \
	../../nih/libnih.la \
	$(LTLIBINTL)


runlevel_SOURCES = runlevel.c
runlevel_LDFLAGS = -static
runlevel_LDADD = ../../nih/libnih.la $(LTLIBINTL)

shutdown_SOURCES = shutdown.c
shutdown_LDFLAGS = -static
shutdown_LDADD = ../../upstart/libupstart.la ../../nih/libnih.la $(LTLIBINTL)

telinit_SOURCES = telinit.c
telinit_LDFLAGS = -static
telinit_LDADD = ../../upstart/libupstart.la ../../nih/libnih.la $(LTLIBINTL)


install-data-hook:
	src=`echo initctl | sed '$(transform)'`.8; \
	for symlink in start stop status; do \
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
	for symlink in start stop status; do \
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
	test_initctl

check_PROGRAMS = $(TESTS)

test_initctl_SOURCES = tests/test_initctl.c initctl.c
test_initctl_CFLAGS = -DTEST
test_initctl_LDFLAGS = -static
test_initctl_LDADD = ../upstart/libupstart.la ../nih/libnih.la


clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
