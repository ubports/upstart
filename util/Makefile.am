## Process this file with automake to produce Makefile.in

DEFAULT_INCLUDES = -I$(top_builddir) -I$(top_srcdir) -I$(srcdir)

localedir = $(datadir)/locale
DEFS += -DLOCALEDIR="\"$(localedir)\""
INCLUDES = -I$(top_srcdir)/intl


sbin_PROGRAMS = \
	initctl \
	shutdown

initctl_SOURCES = \
	initctl.c

initctl_LDFLAGS = -static
initctl_LDADD = \
	../nih/libnih.la \
	../upstart/libupstart.la


shutdown_SOURCES = \
	shutdown.c

shutdown_LDFLAGS = -static
shutdown_LDADD = \
	../nih/libnih.la \
	../upstart/libupstart.la


TESTS = 

check_PROGRAMS = $(TESTS)


clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
