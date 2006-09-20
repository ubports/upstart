## Process this file with automake to produce Makefile.in

eventdir = $(sysconfdir)/event.d
event_DATA = \
	logd

CLEANFILES = $(event_DATA)
EXTRA_DIST = logd.in

do_subst = sed -e 's,[@]sbindir[@],$(sbindir),g'


logd: logd.in
	$(do_subst) < $< > $@
