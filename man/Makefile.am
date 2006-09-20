## Process this file with automake to produce Makefile.in

dist_man_MANS = \
	init.8 \
	logd.8 \
	initctl.8 \
	reboot.8 \
	shutdown.8 \
	start.8 \
	runlevel.8 \
	telinit.8


install-data-hook:
	ln -sf reboot.8 $(DESTDIR)$(man8dir)/halt.8
	ln -sf reboot.8 $(DESTDIR)$(man8dir)/poweroff.8

	ln -sf start.8 $(DESTDIR)$(man8dir)/stop.8
	ln -sf start.8 $(DESTDIR)$(man8dir)/status.8
