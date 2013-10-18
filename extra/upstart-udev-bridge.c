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
#include <ctype.h>

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

static char *make_safe_string    (const void *parent, const char *original);

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
 * no_strip_udev_data:
 *
 * If TRUE, do not modify any udev message data (old behaviour).
 * If FALSE, use make_safe_string () to cleanse udev strings.
 **/
static int no_strip_udev_data = FALSE;

/**
 * options:
 *
 * Command-line options accepted by this program.
 **/
static NihOption options[] = {
	{ 0, "daemon", N_("Detach and run in the background"),
	  NULL, NULL, &daemonise, NULL },
	{ 0, "no-strip", N_("Do not strip non-printable bytes from udev message data"),
	  NULL, NULL, &no_strip_udev_data, NULL },

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
	nih_local char *        subsystem = NULL;
	nih_local char *        action = NULL;
	nih_local char *        kernel = NULL;
	nih_local char *        devpath = NULL;
	nih_local char *        devname = NULL;
	nih_local char *        name = NULL;
	nih_local char **       env = NULL;
	const char *            value = NULL;
	size_t                  env_len = 0;
	DBusPendingCall *       pending_call;
	char                 *(*copy_string)(const void *, const char *) = NULL;


	udev_device = udev_monitor_receive_device (udev_monitor);
	if (! udev_device)
		return;

	copy_string = no_strip_udev_data ? nih_strdup : make_safe_string;

	value = udev_device_get_subsystem (udev_device);
	subsystem = value ? copy_string (NULL, value) : NULL;

	value = udev_device_get_action (udev_device);
	action = value ? copy_string (NULL, value) : NULL;

	value = udev_device_get_sysname (udev_device);
	kernel = value ? copy_string (NULL, value) : NULL;

	value = udev_device_get_devpath (udev_device);
	devpath = value ? copy_string (NULL, value) : NULL;

	value = udev_device_get_devnode (udev_device);
	devname = value ? copy_string (NULL, value) : NULL;

	/* Protect against the "impossible" */
	if (! action)
		goto out;

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
		nih_local char *udev_name = NULL;
		nih_local char *udev_value = NULL;
		nih_local char *var = NULL;

		udev_name = copy_string (NULL, udev_list_entry_get_name (list_entry));

		if (! strcmp (udev_name, "DEVPATH"))
			continue;
		if (! strcmp (udev_name, "DEVNAME"))
			continue;
		if (! strcmp (udev_name, "SUBSYSTEM"))
			continue;
		if (! strcmp (udev_name, "ACTION"))
			continue;

		udev_value = copy_string (NULL, udev_list_entry_get_value (list_entry));

		var = NIH_MUST (nih_sprintf (NULL, "%s=%s", udev_name, udev_value));
		NIH_MUST (nih_str_array_addp (&env, NULL, &env_len, var));
	}

	nih_debug ("%s %s", name, devname ? devname : "");

	pending_call = upstart_emit_event (upstart,
			name, env, FALSE,
			NULL, emit_event_error, NULL,
			NIH_DBUS_TIMEOUT_NEVER);

	if (! pending_call) {
		NihError *err;
		int saved = errno;

		err = nih_error_get ();
		nih_warn ("%s", err->message);

		if (saved != ENOMEM && subsystem)
			nih_warn ("Likely that udev '%s' event contains binary garbage", subsystem);

		nih_free (err);
	}

	dbus_pending_call_unref (pending_call);

out:
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

/**
 * make_safe_string:
 * @parent: parent,
 * @original: original string.
 *
 * Strip non-printable and non-blank bytes from specified string.
 *
 * Notes:
 *
 * Sadly, this is necessary as some hardware (such as battery devices)
 * exposes non-printable bytes in their descriptive registers to the
 * kernel. Since neither the kernel nor udev specify any encoding for
 * udev messages, these (probably bogus) bytes get passed up to userland
 * to deal with. This is sub-optimal since it implies that _every_
 * application that processes udev messages must perform its own
 * sanitizing on the messages. Let's just hope they all deal with the
 * problem in the same way...
 *
 * Note that *iff* the kernel/udev did specify an encoding model, this
 * problem could go away since one of the lower layers could then
 * detect the out-of-bound data and deal with it at source. All instances
 * of this issue seen so far seem to indicate the binary control data
 * being presented by the hardware is in fact bogus ("corruption") and
 * looks like some block of memory has not been initialized correctly.
 *
 * The approach taken here is to simulate the approach already adopted
 * by 'upower' (up_device_supply_make_safe_string()), with the exception
 * that we also allow blank characters (such as tabs).
 *
 * Returns a copy of @original stripped of all non-printable and
 * non-blank characters, or NULL if insufficient memory.
 **/
char *
make_safe_string (const void *parent, const char *original)
{
	size_t   len;
	size_t   i, j;
	char    *cleaned;

	nih_assert (original);

	len = strlen (original);

	cleaned = nih_alloc (parent, len + 1);

	if (! cleaned)
		return NULL;

	for (i=0, j=0; i < len; ) {
		/* Skip over bogus bytes */
		if (! (isprint (original[i]) || isblank (original[i]))) {
			i++;
			continue;
		}

		/* Copy what remains */
		cleaned[j] = original[i];
		i++; j++;
	}

	/* Terminate */
	cleaned[j] = '\0';

	if (i != j)
		nih_debug ("removed unexpected bytes from udev message data");

	/* Note that strictly we should realloc the string if
	 * bogus bytes were found (since it will now be shorter).
	 * However, since all the strings are short (and short-lived) we
	 * do not do this to avoid the associated overhead.
	 */
	return cleaned;
}
