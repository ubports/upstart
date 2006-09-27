## Process this file with automake to produce Makefile.in

DEFAULT_INCLUDES = -I$(top_builddir) -I$(top_srcdir) -I$(srcdir)

localedir = $(datadir)/locale
DEFS += -DLOCALEDIR="\"$(localedir)\"" -DSHUTDOWN="\"$(sbindir)/shutdown\""
INCLUDES = -I$(top_srcdir)/intl


sbin_PROGRAMS = \
	reboot \
	runlevel \
	shutdown \
	telinit

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


install-exec-hook:
	ln -sf reboot $(DESTDIR)$(sbindir)/halt
	ln -sf reboot $(DESTDIR)$(sbindir)/poweroff


TESTS = 

check_PROGRAMS = $(TESTS)


clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
