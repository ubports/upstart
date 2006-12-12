## Process this file with automake to produce Makefile.in

AM_CPPFLAGS = \
	-DLOCALEDIR="\"$(localedir)\"" \
	-DSHUTDOWN="\"$(sbindir)/shutdown\"" \
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
reboot_LDADD = ../../nih/libnih.la ../../upstart/libupstart.la

runlevel_SOURCES = runlevel.c
runlevel_LDFLAGS = -static
runlevel_LDADD = ../../nih/libnih.la

shutdown_SOURCES = shutdown.c
shutdown_LDFLAGS = -static
shutdown_LDADD = ../../nih/libnih.la ../../upstart/libupstart.la

telinit_SOURCES = telinit.c
telinit_LDFLAGS = -static
telinit_LDADD = ../../nih/libnih.la ../../upstart/libupstart.la


install-data-hook:
	ln -sf reboot.8 $(DESTDIR)$(man8dir)/halt.8
	ln -sf reboot.8 $(DESTDIR)$(man8dir)/poweroff.8

install-exec-hook:
	ln -sf reboot $(DESTDIR)$(sbindir)/halt
	ln -sf reboot $(DESTDIR)$(sbindir)/poweroff


TESTS = 

check_PROGRAMS = $(TESTS)


clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
