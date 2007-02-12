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
	initctl.c initctl.h \
	jobs.c jobs.h \
	events.c events.h

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
	test_jobs \
	test_events

check_PROGRAMS = $(TESTS)

test_jobs_SOURCES = tests/test_jobs.c
test_jobs_LDFLAGS = -static
test_jobs_LDADD = ../nih/libnih.la ../upstart/libupstart.la jobs.o

test_events_SOURCES = tests/test_events.c
test_events_LDFLAGS = -static
test_events_LDADD = ../nih/libnih.la ../upstart/libupstart.la events.o


clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
