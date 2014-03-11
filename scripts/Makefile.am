## Process this file with automake to produce Makefile.in

UPSTART_BINARY = $(abs_top_builddir)/init/init
INITCTL_BINARY = $(abs_top_builddir)/util/initctl
FILE_BRIDGE_BINARY = $(abs_top_builddir)/extra/upstart-file-bridge

SUBDIRS = data

install_scripts = \
	initctl2dot.py \
	init-checkconf.sh \
	upstart-monitor.py

noinst_SCRIPTS = \
	pyupstart.py

CLEANFILES = \
	pyupstart.py

EXTRA_DIST = \
	$(install_scripts) \
	pyupstart.py.in \
	tests/__init__.py \
	tests/test_pyupstart_session_init.py \
	tests/test_pyupstart_system_init.py

pyupstart.py: pyupstart.py.in Makefile
	sed -e 's|[@]built_init_binary[@]|$(UPSTART_BINARY)|g' \
	    -e 's|[@]built_initctl_binary[@]|$(INITCTL_BINARY)|g' \
	    -e 's|[@]built_file_bridge_binary[@]|$(FILE_BRIDGE_BINARY)|g' \
	    $< > $@
	chmod +x $@

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
