/* upstart
 *
 * Copyright © 2013 Canonical Ltd.
 * Author: James Hunt <james.hunt@ubuntu.com>
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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/**
 * DBUS_EVENT:
 *
 * Name of event this program handles.
 **/
#define DBUS_EVENT "dbus"

/* Prototypes for static functions */
static int               dbus_bus_setter      (NihOption *option, const char *arg);
static void              dbus_disconnected    (DBusConnection *connection);
static void              upstart_disconnected (DBusConnection *connection);
static DBusHandlerResult signal_filter        (DBusConnection *connection,
					       DBusMessage *message, void *user_data); 
static void              emit_event_error     (void *data, NihDBusMessage *message);

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
 * user_mode:
 *
 * If TRUE, connect to Session Init rather than PID 1.
 **/
static int user_mode = FALSE;

/**
 * dbus_bus:
 *
 * type of D-Bus bus to connect to.
 **/
DBusBusType dbus_bus = (DBusBusType)-1;

/**
 * options:
 *
 * Command-line options accepted by this program.
 **/
static NihOption options[] = {
	{ 0, "daemon", N_("Detach and run in the background"),
	  NULL, NULL, &daemonise, NULL },
	{ 0, "user", N_("Connect to user session"),
	  NULL, NULL, &user_mode, NULL },
	{ 0, "session", N_("Use D-Bus session bus"),
		NULL, NULL, NULL, dbus_bus_setter },
	{ 0, "system", N_("Use D-Bus system bus"),
		NULL, NULL, NULL, dbus_bus_setter },

	NIH_OPTION_LAST
};


int
main (int   argc,
      char *argv[])
{
	char               **args;
	DBusConnection      *dbus_connection;
	DBusConnection      *connection;
	int                  ret;
	char                *pidfile_path = NULL;
	char                *pidfile = NULL;
	char                *user_session_addr = NULL;
	nih_local char     **user_session_path = NULL;
	char                *path_element = NULL;
	DBusError            error;

	nih_main_init (argv[0]);

	nih_option_set_synopsis (_("Bridge D-Bus signals into upstart"));
	nih_option_set_help (
		_("By default, upstart-event-bridge does not detach from the "
		  "console and remains in the foreground.  Use the --daemon "
		  "option to have it detach."));

	args = nih_option_parser (NULL, argc, argv, options, FALSE);
	if (! args)
		exit (EXIT_FAILURE);

	dbus_error_init (&error);

	/* Default to an appropriate bus */
	if (dbus_bus == (DBusBusType)-1)
		dbus_bus = user_mode ? DBUS_BUS_SESSION : DBUS_BUS_SYSTEM;

	/* Connect to the chosen D-Bus bus */
	dbus_connection = NIH_SHOULD (nih_dbus_bus (dbus_bus, dbus_disconnected));

	if (! dbus_connection) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not connect to D-Bus"),
			   err->message);
		nih_free (err);

		exit (EXIT_FAILURE);
	}

	dbus_bus_add_match (dbus_connection, "type='signal'", &error);

	if (dbus_error_is_set (&error)) {
		nih_fatal ("%s: %s %s", _("Could not add D-Bus signal match"),
			   error.name, error.message);
		dbus_error_free (&error);

		exit (EXIT_FAILURE);
	}

	dbus_connection_add_filter (dbus_connection, signal_filter, NULL, NULL);

	dbus_error_free (&error);

	if (user_mode) {
		user_session_addr = getenv ("UPSTART_SESSION");
		if (! user_session_addr) {
			nih_fatal (_("UPSTART_SESSION isn't set in environment"));
			exit (EXIT_FAILURE);
		}
	}

	connection = NIH_SHOULD (nih_dbus_connect (user_mode
				? user_session_addr
				: DBUS_ADDRESS_UPSTART,
				upstart_disconnected));

	if (! connection) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not connect to Upstart"),
			   err->message);
		nih_free (err);

		exit (EXIT_FAILURE);
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

		exit (EXIT_FAILURE);
	}

	/* Become daemon */
	if (daemonise) {
		/* Deal with the pidfile location when becoming a daemon.
		 * We need to be able to run one bridge per upstart daemon.
		 * Store the PID file in XDG_RUNTIME_DIR or HOME and include the pid of
		 * the Upstart instance (last part of the DBus path) in the filename.
		 */

		if (user_mode) {
			/* Extract PID from UPSTART_SESSION */
			user_session_path = nih_str_split (NULL, user_session_addr, "/", TRUE);
			for (int i = 0; user_session_path[i] != NULL; i++)
				path_element = user_session_path[i];

			if (! path_element) {
				nih_fatal (_("Invalid value for UPSTART_SESSION"));
				exit (EXIT_FAILURE);
			}

			pidfile_path = getenv ("XDG_RUNTIME_DIR");
			if (! pidfile_path)
				pidfile_path = getenv ("HOME");

			if (pidfile_path) {
				NIH_MUST (nih_strcat_sprintf (&pidfile, NULL, "%s/upstart-dbus-bridge.%s.pid",
							pidfile_path, path_element));
				nih_main_set_pidfile (pidfile);
			}
		}

		if (nih_main_daemonise () < 0) {
			NihError *err;

			err = nih_error_get ();
			nih_fatal ("%s: %s", _("Unable to become daemon"),
				   err->message);
			nih_free (err);

			exit (EXIT_FAILURE);
		}
	}

	/* Handle TERM and INT signals gracefully */
	nih_signal_set_handler (SIGTERM, nih_signal_handler);
	NIH_MUST (nih_signal_add_handler (NULL, SIGTERM, nih_main_term_signal, NULL));

	if (! daemonise) {
		nih_signal_set_handler (SIGINT, nih_signal_handler);
		NIH_MUST (nih_signal_add_handler (NULL, SIGINT, nih_main_term_signal, NULL));
	}

	ret = nih_main_loop ();

	/* Destroy any PID file we may have created */
	if (daemonise) {
		nih_main_unlink_pidfile();
	}

	return ret;
}

/**
 * dbus_disconnected:
 *
 * @connection: connection to a D-Bus bus.
 *
 * Handler called when bridge disconnected from D-Bus.
 **/
static void
dbus_disconnected (DBusConnection *connection)
{
	nih_fatal (_("Disconnected from D-Bus"));
	nih_main_loop_exit (EXIT_FAILURE);
}

/**
 * upstart_disconnected:
 *
 * @connection: connection to Upstart.
 *
 * Handler called when bridge disconnected from Upstart.
 **/
static void
upstart_disconnected (DBusConnection *connection)
{
	nih_fatal (_("Disconnected from Upstart"));
	nih_main_loop_exit (EXIT_FAILURE);
}

/**  
 * NihOption setter function to handle selection of D-Bus bus type.
 *
 * Returns: 0 on success, -1 on invalid console type.
 **/
static int
dbus_bus_setter (NihOption *option, const char *arg)
{
	nih_assert (option);
	nih_assert (option->long_option);

	if (! strcmp (option->long_option, "session")) {
		dbus_bus = DBUS_BUS_SESSION;
	} else {
		dbus_bus = DBUS_BUS_SYSTEM;
	}

	return 0;
}

/**
 * signal_filter:
 * @connection: D-Bus connection,
 * @message: D-Bus message,
 * @user_data: unused.
 *
 * Handle D-Bus signal message by emitting an Upstart event
 * containing pertinent details from the original message.
 *
 * Returns: DBUS_HANDLER_RESULT_HANDLED always.
 **/
static DBusHandlerResult
signal_filter (DBusConnection  *connection,
	       DBusMessage     *message,
	       void            *user_data)
{
	DBusPendingCall    *pending_call;
	DBusError           error;
	nih_local char    **env = NULL;
	const char         *sender;
	const char         *destination;
	const char         *interface;
	const char         *signal;
	const char         *path;
	size_t              env_len = 0;

	nih_assert (connection);
	nih_assert (message);

	dbus_error_init (&error);

	sender = dbus_message_get_sender (message);
	signal = dbus_message_get_member (message);
	interface = dbus_message_get_interface (message);
	path = dbus_message_get_path (message);
	destination = dbus_message_get_destination (message);

	/* Don't react to D-Bus signals generated by Upstart
	 * to avoid a possible feedback loop: for example, imagine a job
	 * that emits an event when it detects (via this bridge) that
	 * Upstart has emitted an event by considering Upstarts
	 * "EventEmitted" D-Bus signal interface...
	 */
	if ((sender && ! strcmp (sender, DBUS_SERVICE_UPSTART)) ||
	    (interface && ! strcmp (interface, DBUS_INTERFACE_UPSTART))) {
		nih_debug ("ignoring signal originating from upstart itself");
		goto out;
	}

	env = NIH_MUST (nih_str_array_new (NULL));

	if (signal) {
		nih_local char *var = NULL;
		var = NIH_MUST (nih_sprintf (NULL, "SIGNAL=%s", signal));
		NIH_MUST (nih_str_array_addp (&env, NULL, &env_len, var));
	}

	if (interface) {
		nih_local char *var = NULL;
		var = NIH_MUST (nih_sprintf (NULL, "INTERFACE=%s", interface));
		NIH_MUST (nih_str_array_addp (&env, NULL, &env_len, var));
	}

	if (path) {
		nih_local char *var = NULL;
		var = NIH_MUST (nih_sprintf (NULL, "PATH=%s", path));
		NIH_MUST (nih_str_array_addp (&env, NULL, &env_len, var));
	}

	if (sender) {
		nih_local char *var = NULL;
		var = NIH_MUST (nih_sprintf (NULL, "SENDER=%s", sender));
		NIH_MUST (nih_str_array_addp (&env, NULL, &env_len, var));
	}

	if (destination) {
		nih_local char *var = NULL;
		var = NIH_MUST (nih_sprintf (NULL, "DESTINATION=%s", destination));
		NIH_MUST (nih_str_array_addp (&env, NULL, &env_len, var));
	}

	nih_debug ("Received D-Bus signal: %s "
		   "(sender=%s, destination=%s, interface=%s, path=%s)",
		   signal ? signal : "",
		   sender ? sender : "",
		   destination ? destination : "",
		   interface ? interface : "",
		   path ? path : "");

	pending_call = upstart_emit_event (upstart,
			DBUS_EVENT, env, FALSE,
			NULL, emit_event_error, NULL,
			NIH_DBUS_TIMEOUT_NEVER);

	if (! pending_call) {
		NihError *err;
		err = nih_error_get ();
		nih_warn ("%s", err->message);
		nih_free (err);
	}

	dbus_pending_call_unref (pending_call);

out:
	return DBUS_HANDLER_RESULT_HANDLED;
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
