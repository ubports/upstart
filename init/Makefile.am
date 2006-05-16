## Process this file with automake to produce Makefile.in

localedir = $(datadir)/locale
DEFS = -DLOCALEDIR="\"$(localedir)\""
INCLUDES = -I$(top_srcdir)/intl -I$(top_srcdir)


sbin_PROGRAMS = init

init_SOURCES = \
	main.c

init_LDFLAGS = -static
init_LDADD = \
	../nih/libnih.la


TESTS =

check_PROGRAMS = $(TESTS)


clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
