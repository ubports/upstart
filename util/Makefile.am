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
	initctl.c

initctl_LDFLAGS = -static
initctl_LDADD = \
	../upstart/libupstart.la \
	../nih/libnih.la \
	$(LTLIBINTL)


install-data-hook:
	src=`echo initctl | sed '$(transform)'`.8; \
	for symlink in start stop status; do \
	  inst=`echo $$symlink | sed '$(transform)'`.8; \
          echo " ln -sf '$$src' '$(DESTDIR)$(man8dir)/$$inst'"; \
	  ln -sf "$$src" "$(DESTDIR)$(man8dir)/$$inst"; \
	done

install-exec-hook:
	src=`echo initctl | sed '$(transform);s/$$/$(EXEEXT)/'`; \
	for symlink in start stop status; do \
	  inst=`echo $$symlink | sed '$(transform);s/$$/$(EXEEXT)/'`; \
          echo " ln -sf '$$src' '$(DESTDIR)$(sbindir)/$$inst'"; \
	  ln -sf "$$src" "$(DESTDIR)$(sbindir)/$$inst"; \
	done


TESTS = \
	test_initctl

check_PROGRAMS = $(TESTS)

test_initctl_SOURCES = tests/test_initctl.c initctl.c
test_initctl_CFLAGS = -DTEST
test_initctl_LDFLAGS = -static
test_initctl_LDADD = ../upstart/libupstart.la ../nih/libnih.la


clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
