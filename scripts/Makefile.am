## Process this file with automake to produce Makefile.in

EXTRA_DIST = \
  initctl2dot.py \
  init-checkconf.sh \
  upstart-monitor.py

dist_man_MANS = \
  man/initctl2dot.8 \
  man/init-checkconf.8

install-exec-hook: $(EXTRA_DIST)
	[ ! -d "$(DESTDIR)/$(bindir)" ] && mkdir -p "$(DESTDIR)/$(bindir)"; \
	for file in $(EXTRA_DIST); do \
		from=`echo "$$file" | sed '$(transform)'`; \
		to=`basename $$from | cut -d. -f1`; \
		echo "  cp '$(srcdir)/$$from' '$(DESTDIR)$(bindir)/$$to'"; \
		cp "$(srcdir)/$$from" "$(DESTDIR)$(bindir)/$$to"; \
	done

uninstall-hook:
	for file in $(EXTRA_DIST); do \
		inst=`basename $$file | cut -d. -f1`; \
		echo "  rm -f '$(DESTDIR)$(bindir)/$$inst'"; \
		rm -f "$(DESTDIR)$(bindir)/$$inst"; \
	done
