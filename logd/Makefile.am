## Process this file with automake to produce Makefile.in

localedir = $(datadir)/locale
DEFS += -DLOCALEDIR="\"$(localedir)\""
INCLUDES = -I$(top_srcdir)/intl


sbin_PROGRAMS = logd

logd_SOURCES = \
	main.c

logd_LDFLAGS = -static
logd_LDADD = \
	../nih/libnih.la


TESTS =

check_PROGRAMS = $(TESTS)



clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
