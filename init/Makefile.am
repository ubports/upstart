## Process this file with automake to produce Makefile.in

localedir = $(datadir)/locale
DEFS += -DLOCALEDIR="\"$(localedir)\""
INCLUDES = -I$(top_srcdir)/intl


sbin_PROGRAMS = init

init_SOURCES = \
	main.c \
	job.c job.h \
	process.c process.h

init_LDFLAGS = -static
init_LDADD = \
	../nih/libnih.la


TESTS = \
	test_job \
	test_process

check_PROGRAMS = $(TESTS)

test_job_SOURCES = tests/test_job.c
test_job_LDFLAGS = -static
test_job_LDADD = ../nih/libnih.la job.o

test_process_SOURCES = tests/test_process.c
test_process_LDFLAGS = -static
test_process_LDADD = ../nih/libnih.la job.o process.o

clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
