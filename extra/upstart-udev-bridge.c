/* upstart
 *
 * Copyright © 2009 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <libudev.h>

#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/io.h>
#include <nih/option.h>
#include <nih/main.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <nih-dbus/dbus_connection.h>
#include <nih-dbus/dbus_proxy.h>

#include "dbus/upstart.h"
#include "com.ubuntu.Upstart.h"


/* Prototypes for static functions */
static void udev_monitor_watcher (struct udev_monitor *udev_monitor,
				  NihIoWatch *watch, NihIoEvents events);
static void upstart_disconnected (DBusConnection *connection);
static void emit_event_error     (void *data, NihDBusMessage *message);


/**
 * daemonise:
 *
 * Set to TRUE if we should become a daemon, rather than just running
 * in the foreground.
 **/
static int daemonise = FALSE;

/**
 * upstart:
 *
 * Proxy to Upstart daemon.
 **/
static NihDBusProxy *upstart = NULL;


/**
 * options:
 *
 * Command-line options accepted by this program.
 **/
static NihOption options[] = {
	{ 0, "daemon", N_("Detach and run in the background"),
	  NULL, NULL, &daemonise, NULL },

	NIH_OPTION_LAST
};


int
main (int   argc,
      char *argv[])
{
	char **              args;
	DBusConnection *     connection;
	struct udev *        udev;
	struct udev_monitor *udev_monitor;
	int                  ret;

	nih_main_init (argv[0]);

	nih_option_set_synopsis (_("Bridge udev events into upstart"));
	nih_option_set_help (
		_("By default, upstart-udev-bridge does not detach from the "
		  "console and remains in the foreground.  Use the --daemon "
		  "option to have it detach."));

	args = nih_option_parser (NULL, argc, argv, options, FALSE);
	if (! args)
		exit (1);

	/* Initialise the connection to Upstart */
	connection = NIH_SHOULD (nih_dbus_connect (DBUS_ADDRESS_UPSTART, upstart_disconnected));
	if (! connection) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not connect to Upstart"),
			   err->message);
		nih_free (err);

		exit (1);
	}

	upstart = NIH_SHOULD (nih_dbus_proxy_new (NULL, connection,
						  NULL, DBUS_PATH_UPSTART,
						  NULL, NULL));
	if (! upstart) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not create Upstart proxy"),
			   err->message);
		nih_free (err);

		exit (1);
	}

	/* Initialise the connection to udev */
	nih_assert (udev = udev_new ());
	nih_assert (udev_monitor = udev_monitor_new_from_netlink (udev, "udev"));
	nih_assert (udev_monitor_enable_receiving (udev_monitor) == 0);
	udev_monitor_set_receive_buffer_size(udev_monitor, 128*1024*1024);

	NIH_MUST (nih_io_add_watch (NULL, udev_monitor_get_fd (udev_monitor),
				    NIH_IO_READ,
				    (NihIoWatcher)udev_monitor_watcher,
				    udev_monitor));

	/* Become daemon */
	if (daemonise) {
		if (nih_main_daemonise () < 0) {
			NihError *err;

			err = nih_error_get ();
			nih_fatal ("%s: %s", _("Unable to become daemon"),
				   err->message);
			nih_free (err);

			exit (1);
		}

		/* Send all logging output to syslog */
		openlog (program_name, LOG_PID, LOG_DAEMON);
		nih_log_set_logger (nih_logger_syslog);
	}

	/* Handle TERM and INT signals gracefully */
	nih_signal_set_handler (SIGTERM, nih_signal_handler);
	NIH_MUST (nih_signal_add_handler (NULL, SIGTERM, nih_main_term_signal, NULL));

	if (! daemonise) {
		nih_signal_set_handler (SIGINT, nih_signal_handler);
		NIH_MUST (nih_signal_add_handler (NULL, SIGINT, nih_main_term_signal, NULL));
	}

	ret = nih_main_loop ();

	return ret;
}


static void
udev_monitor_watcher (struct udev_monitor *udev_monitor,
		      NihIoWatch *         watch,
		      NihIoEvents          events)
{
	struct udev_device *    udev_device;
	const char *            subsystem;
	const char *            action;
	const char *            kernel;
	const char *            devpath;
	const char *            devname;
	nih_local char *        name = NULL;
	nih_local char **       env = NULL;
	size_t                  env_len = 0;
	DBusPendingCall *       pending_call;

	udev_device = udev_monitor_receive_device (udev_monitor);
	if (! udev_device)
		return;

	subsystem = udev_device_get_subsystem (udev_device);
	action = udev_device_get_action (udev_device);
	kernel = udev_device_get_sysname (udev_device);
	devpath = udev_device_get_devpath (udev_device);
	devname = udev_device_get_devnode (udev_device);

	if (! strcmp (action, "add")) {
		name = NIH_MUST (nih_sprintf (NULL, "%s-device-added",
					      subsystem));
	} else if (! strcmp (action, "change")) {
		name = NIH_MUST (nih_sprintf (NULL, "%s-device-changed",
					      subsystem));
	} else if (! strcmp (action, "remove")) {
		name = NIH_MUST (nih_sprintf (NULL, "%s-device-removed",
					      subsystem));
	} else {
		name = NIH_MUST (nih_sprintf (NULL, "%s-device-%s",
					      subsystem, action));
	}

	env = NIH_MUST (nih_str_array_new (NULL));

	if (kernel) {
		nih_local char *var = NULL;

		var = NIH_MUST (nih_sprintf (NULL, "KERNEL=%s", kernel));
		NIH_MUST (nih_str_array_addp (&env, NULL, &env_len, var));
	}

	if (devpath) {
		nih_local char *var = NULL;

		var = NIH_MUST (nih_sprintf (NULL, "DEVPATH=%s", devpath));
		NIH_MUST (nih_str_array_addp (&env, NULL, &env_len, var));
	}

	if (devname) {
		nih_local char *var = NULL;

		var = NIH_MUST (nih_sprintf (NULL, "DEVNAME=%s", devname));
		NIH_MUST (nih_str_array_addp (&env, NULL, &env_len, var));
	}

	if (subsystem) {
		nih_local char *var = NULL;

		var = NIH_MUST (nih_sprintf (NULL, "SUBSYSTEM=%s", subsystem));
		NIH_MUST (nih_str_array_addp (&env, NULL, &env_len, var));
	}

	if (action) {
		nih_local char *var = NULL;

		var = NIH_MUST (nih_sprintf (NULL, "ACTION=%s", action));
		NIH_MUST (nih_str_array_addp (&env, NULL, &env_len, var));
	}

	for (struct udev_list_entry *list_entry = udev_device_get_properties_list_entry (udev_device);
	     list_entry != NULL;
	     list_entry = udev_list_entry_get_next (list_entry)) {
		const char *    key;
		nih_local char *var = NULL;

		key = udev_list_entry_get_name (list_entry);
		if (! strcmp (key, "DEVPATH"))
			continue;
		if (! strcmp (key, "DEVNAME"))
			continue;
		if (! strcmp (key, "SUBSYSTEM"))
			continue;
		if (! strcmp (key, "ACTION"))
			continue;

		var = NIH_MUST (nih_sprintf (NULL, "%s=%s", key,
					     udev_list_entry_get_value (list_entry)));
		NIH_MUST (nih_str_array_addp (&env, NULL, &env_len, var));
	}

	nih_debug ("%s %s", name, devname);

	pending_call = NIH_SHOULD (upstart_emit_event (upstart,
						       name, env, FALSE,
						       NULL, emit_event_error, NULL,
						       NIH_DBUS_TIMEOUT_NEVER));
	if (! pending_call) {
		NihError *err;

		err = nih_error_get ();
		nih_warn ("%s", err->message);
		nih_free (err);
	}

	dbus_pending_call_unref (pending_call);

	udev_device_unref (udev_device);
}


static void
upstart_disconnected (DBusConnection *connection)
{
	nih_fatal (_("Disconnected from Upstart"));
	nih_main_loop_exit (1);
}

static void
emit_event_error (void *          data,
		  NihDBusMessage *message)
{
	NihError *err;

	err = nih_error_get ();
	nih_warn ("%s", err->message);
	nih_free (err);
}
