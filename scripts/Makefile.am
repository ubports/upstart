## Process this file with automake to produce Makefile.in

EXTRA_DIST = \
  initctl2dot.py \
  init-checkconf.sh

dist_man_MANS = \
  man/init-checkconf.8 \
  man/initctl2dot.8

install-exec-local: $(EXTRA_DIST)
	[ ! -d "$(DESTDIR)/$(bindir)" ] && mkdir -p "$(DESTDIR)/$(bindir)"; \
	for file in $(EXTRA_DIST); do \
		cp "$$file" "$(DESTDIR)$(bindir)/`basename $$file|cut -d. -f1`"; \
	done

