## Process this file with automake to produce Makefile.in

AM_CPPFLAGS = \
	-DLOCALEDIR="\"$(localedir)\"" \
	-I$(top_builddir) -I$(top_srcdir) -I$(srcdir) \
	-I$(top_srcdir)/intl


jobsdir = $(sysconfdir)/init/jobs.d
jobs_DATA = \
	jobs.d/logd

CLEANFILES = $(jobs_DATA)
EXTRA_DIST = jobs.d/logd.in

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


jobs.d/logd: jobs.d/logd.in
	$(mkdir_p) jobs.d
	$(do_subst) < $< > $@


clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
