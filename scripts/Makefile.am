## Process this file with automake to produce Makefile.in

SUBDIRS = data

install_scripts = \
	initctl2dot.py \
	init-checkconf.sh \
	upstart-monitor.py

EXTRA_DIST = \
	$(install_scripts) \
	pyupstart.py \
	tests/test_pyupstart_session_init.py \
	tests/test_pyupstart_system_init.py

dist_man_MANS = \
  man/initctl2dot.8 \
  man/init-checkconf.8 \
  man/upstart-monitor.8

install-exec-hook: $(install_scripts)
	[ ! -d "$(DESTDIR)/$(bindir)" ] && mkdir -p "$(DESTDIR)/$(bindir)"; \
	for file in $(install_scripts); do \
		from=`echo "$$file" | sed '$(transform)'`; \
		to=`basename $$from | cut -d. -f1`; \
		echo "  cp '$(srcdir)/$$from' '$(DESTDIR)$(bindir)/$$to'"; \
		cp "$(srcdir)/$$from" "$(DESTDIR)$(bindir)/$$to"; \
	done

uninstall-hook:
	for file in $(install_scripts); do \
		inst=`basename $$file | cut -d. -f1`; \
		echo "  rm -f '$(DESTDIR)$(bindir)/$$inst'"; \
		rm -f "$(DESTDIR)$(bindir)/$$inst"; \
	done
