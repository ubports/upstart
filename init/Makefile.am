## Process this file with automake to produce Makefile.in

localedir = $(datadir)/locale
DEFS += -DLOCALEDIR="\"$(localedir)\""
INCLUDES = -I$(top_srcdir)/intl


sbin_PROGRAMS = init

init_SOURCES = \
	main.c \
	process.c process.h \
	job.c job.h

init_LDFLAGS = -static
init_LDADD = \
	../nih/libnih.la


TESTS = \
	test_process \
	test_job

check_PROGRAMS = $(TESTS)

test_job_SOURCES = tests/test_job.c
test_job_LDFLAGS = -static
test_job_LDADD = ../nih/libnih.la process.o job.o

test_process_SOURCES = tests/test_process.c
test_process_LDFLAGS = -static
test_process_LDADD = ../nih/libnih.la process.o job.o


clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
