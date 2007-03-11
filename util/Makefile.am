## Process this file with automake to produce Makefile.in

AM_CPPFLAGS = \
	-DLOCALEDIR="\"$(localedir)\"" \
	-I$(top_builddir) -I$(top_srcdir) \
	-I$(top_srcdir)/intl


dist_man_MANS = \
	man/initctl.8


sbin_PROGRAMS = \
	initctl

initctl_SOURCES = \
	initctl.c

initctl_LDFLAGS = -static
initctl_LDADD = \
	../nih/libnih.la \
	../upstart/libupstart.la


install-data-hook:
	ln -sf initctl.8 $(DESTDIR)$(man8dir)/start.8
	ln -sf initctl.8 $(DESTDIR)$(man8dir)/stop.8
	ln -sf initctl.8 $(DESTDIR)$(man8dir)/status.8

install-exec-hook:
	ln -sf initctl $(DESTDIR)$(sbindir)/start
	ln -sf initctl $(DESTDIR)$(sbindir)/stop
	ln -sf initctl $(DESTDIR)$(sbindir)/status


TESTS = \
	test_initctl

check_PROGRAMS = $(TESTS)

test_initctl_SOURCES = tests/test_initctl.c initctl.c
test_initctl_CFLAGS = -DTEST
test_initctl_LDFLAGS = -static
test_initctl_LDADD = ../nih/libnih.la ../upstart/libupstart.la


clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
