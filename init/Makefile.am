## Process this file with automake to produce Makefile.in

localedir = $(datadir)/locale
DEFS += -DLOCALEDIR="\"$(localedir)\""
INCLUDES = -I$(top_srcdir)/intl


sbin_PROGRAMS = init

init_SOURCES = \
	main.c \
	process.c process.h \
	job.c job.h \
	event.c event.h \
	control.c control.h

init_LDFLAGS = -static
init_LDADD = \
	../nih/libnih.la \
	../upstart/libupstart.la


TESTS = \
	test_process \
	test_job \
	test_event \
	test_control

check_PROGRAMS = $(TESTS)

test_job_SOURCES = tests/test_job.c
test_job_LDFLAGS = -static
test_job_LDADD = \
	../nih/libnih.la ../upstart/libupstart.la \
	../nih/libnih.la process.o job.o event.o control.o

test_process_SOURCES = tests/test_process.c
test_process_LDFLAGS = -static
test_process_LDADD = \
	../nih/libnih.la ../upstart/libupstart.la \
	process.o job.o event.o control.o

test_event_SOURCES = tests/test_event.c
test_event_LDFLAGS = -static
test_event_LDADD = \
	../nih/libnih.la ../upstart/libupstart.la \
	process.o job.o event.o control.o

test_control_SOURCES = tests/test_control.c
test_control_LDFLAGS = -static
test_control_LDADD = \
	../nih/libnih.la ../upstart/libupstart.la \
	process.o job.o event.o control.o


clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
