## Process this file with automake to produce Makefile.in

AM_CPPFLAGS = \
	-DLOCALEDIR="\"$(localedir)\"" \
	-I$(top_builddir) -I$(top_srcdir) -I$(srcdir) \
	-I$(top_srcdir)/intl


eventdir = $(sysconfdir)/event.d
event_DATA = \
	event.d/logd

CLEANFILES = $(event_DATA)
EXTRA_DIST = event.d/logd.in

do_subst = sed -e '\,[@]sbindir[@],{s,[@]sbindir[@],$(sbindir),g;s,//*,/,g}'


dist_man_MANS = \
	man/logd.8


sbin_PROGRAMS = logd

logd_SOURCES = \
	main.c

logd_LDFLAGS = -static
logd_LDADD = \
	../nih/libnih.la \
	$(LTLIBINTL)


TESTS =

check_PROGRAMS = $(TESTS)


event.d/logd: event.d/logd.in
	$(mkdir_p) event.d
	$(do_subst) < $< > $@


clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
