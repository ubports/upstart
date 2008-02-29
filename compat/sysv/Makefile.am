## Process this file with automake to produce Makefile.in

AM_CPPFLAGS = \
	-DLOCALEDIR="\"$(localedir)\"" \
	-DSBINDIR="\"$(sbindir)\"" \
	-I$(top_builddir) -I$(top_srcdir) -I$(srcdir) \
	-I$(top_srcdir)/intl


manpages = \
	man/reboot.8 \
	man/shutdown.8 \
	man/runlevel.8 \
	man/telinit.8

EXTRA_DIST = $(manpages)


if COMPAT_SYSV
man_MANS = $(manpages)

sbin_PROGRAMS = \
	reboot \
	runlevel \
	shutdown \
	telinit
endif

reboot_SOURCES = reboot.c
reboot_LDFLAGS = -static
reboot_LDADD = ../../upstart/libupstart.la ../../nih/libnih.la $(LTLIBINTL)

runlevel_SOURCES = runlevel.c
runlevel_LDFLAGS = -static
runlevel_LDADD = ../../nih/libnih.la $(LTLIBINTL)

shutdown_SOURCES = shutdown.c
shutdown_LDFLAGS = -static
shutdown_LDADD = ../../upstart/libupstart.la ../../nih/libnih.la $(LTLIBINTL)

telinit_SOURCES = telinit.c
telinit_LDFLAGS = -static
telinit_LDADD = ../../upstart/libupstart.la ../../nih/libnih.la $(LTLIBINTL)


if COMPAT_SYSV
install-data-hook:
	src=`echo reboot | sed '$(transform)'`.8; \
	for symlink in halt poweroff; do \
	  inst=`echo $$symlink | sed '$(transform)'`.8; \
          echo " ln -sf '$$src' '$(DESTDIR)$(man8dir)/$$inst'"; \
	  ln -sf "$$src" "$(DESTDIR)$(man8dir)/$$inst"; \
	done

install-exec-hook:
	src=`echo reboot | sed '$(transform);s/$$/$(EXEEXT)/'`; \
	for symlink in halt poweroff; do \
	  inst=`echo $$symlink | sed '$(transform);s/$$/$(EXEEXT)/'`; \
          echo " ln -sf '$$src' '$(DESTDIR)$(sbindir)/$$inst'"; \
	  ln -sf "$$src" "$(DESTDIR)$(sbindir)/$$inst"; \
	done
endif


TESTS = 

check_PROGRAMS = $(TESTS)


clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
