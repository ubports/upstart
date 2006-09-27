## Process this file with automake to produce Makefile.in

DEFAULT_INCLUDES = -I$(top_builddir) -I$(top_srcdir) -I$(srcdir)

localedir = $(datadir)/locale
DEFS += -DLOCALEDIR="\"$(localedir)\""
INCLUDES = -I$(top_srcdir)/intl


dist_man_MANS = \
	man/initctl.8 \
	man/start.8


sbin_PROGRAMS = \
	initctl \
	start


initctl_SOURCES = initctl.c
initctl_LDFLAGS = -static
initctl_LDADD = ../nih/libnih.la ../upstart/libupstart.la

start_SOURCES = start.c
start_LDFLAGS = -static
start_LDADD = ../nih/libnih.la ../upstart/libupstart.la


install-data-hook:
	ln -sf start.8 $(DESTDIR)$(man8dir)/stop.8
	ln -sf start.8 $(DESTDIR)$(man8dir)/status.8

install-exec-hook:
	ln -sf start $(DESTDIR)$(sbindir)/stop
	ln -sf start $(DESTDIR)$(sbindir)/status


TESTS = 

check_PROGRAMS = $(TESTS)


clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
