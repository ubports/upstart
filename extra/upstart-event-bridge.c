/* upstart
 *
 * Copyright © 2012 Canonical Ltd.
 * Author: Stéphane Graber <stgraber@ubuntu.com>
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


/* Prototypes for static functions */
static void upstart_disconnected (DBusConnection *connection);
static void upstart_forward_event    (void *data, NihDBusMessage *message,
				  const char *path);
static void upstart_forward_restarted    (void *data, NihDBusMessage *message,
				  const char *path);
static void emit_event_error     (void *data, NihDBusMessage *message);

/**
 * daemonise:
 *
 * Set to TRUE if we should become a daemon, rather than just running
 * in the foreground.
 **/
static int daemonise = FALSE;

/**
 * system_upstart:
 *
 * Proxy to system Upstart daemon.
 **/
static NihDBusProxy *system_upstart = NULL;

/**
 * session_upstart:
 *
 * Proxy to session Upstart daemon.
 **/
static NihDBusProxy *session_upstart = NULL;

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
	DBusConnection *     system_connection;
	DBusConnection *     session_connection;
	int                  ret;


	nih_main_init (argv[0]);

	nih_option_set_synopsis (_("Bridge system upstart events into session upstart"));
	nih_option_set_help (
		_("By default, upstart-event-bridge does not detach from the "
		  "console and remains in the foreground.  Use the --daemon "
		  "option to have it detach."));

	args = nih_option_parser (NULL, argc, argv, options, FALSE);
	if (! args)
		exit (1);

	/* Initialise the connection to system Upstart */
	system_connection = NIH_SHOULD (nih_dbus_bus (DBUS_BUS_SYSTEM, upstart_disconnected));

	if (! system_connection) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not connect to system Upstart"),
			   err->message);
		nih_free (err);

		exit (1);
	}

	system_upstart = NIH_SHOULD (nih_dbus_proxy_new (NULL, system_connection,
						  DBUS_SERVICE_UPSTART, DBUS_PATH_UPSTART,
						  NULL, NULL));
	if (! system_upstart) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not create Upstart proxy"),
			   err->message);
		nih_free (err);

		exit (1);
	}

	if (! nih_dbus_proxy_connect (system_upstart, &upstart_com_ubuntu_Upstart0_6, "EventEmitted",
				      (NihDBusSignalHandler)upstart_forward_event, NULL)) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not create EventEmitted signal connection"),
			   err->message);
		nih_free (err);

		exit (1);
	}

	if (! nih_dbus_proxy_connect (system_upstart, &upstart_com_ubuntu_Upstart0_6, "Restarted",
				      (NihDBusSignalHandler)upstart_forward_restarted, NULL)) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not create Restarted signal connection"),
			   err->message);
		nih_free (err);

		exit (1);
	}

	/* Initialise the connection to session Upstart */
	session_connection = nih_dbus_bus (DBUS_BUS_SESSION, upstart_disconnected);

	if (! session_connection) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not connect to session Upstart"),
			   err->message);
		nih_free (err);

		exit (1);
	}

	session_upstart = NIH_SHOULD (nih_dbus_proxy_new (NULL, session_connection,
						  DBUS_SERVICE_UPSTART, DBUS_PATH_UPSTART,
						  NULL, NULL));
	if (! session_upstart) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not create Upstart proxy"),
			   err->message);
		nih_free (err);

		exit (1);
	}

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
upstart_disconnected (DBusConnection *connection)
{
	nih_fatal (_("Disconnected from Upstart"));
	nih_main_loop_exit (1);
}

static void
upstart_forward_event (void *          data,
		     NihDBusMessage *message,
		     const char *    path)
{
	char *              event_name = NULL;
	nih_local char *    new_event_name = NULL;
	char **             event_env = NULL;
	int                 event_env_count = 0;
	DBusError           error;
	DBusPendingCall *   pending_call;

	dbus_error_init (&error);

	/* Extract information from the original event */
	if (!dbus_message_get_args (message->message, &error,
	        DBUS_TYPE_STRING, &event_name,
	        DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &event_env, &event_env_count,
	        DBUS_TYPE_INVALID)) {
		nih_error("DBUS error: %s", error.message);
		dbus_error_free(&error);
		return;
	}

	nih_assert (event_name != NULL);

	/* Build the new event name */
	NIH_MUST (nih_strcat_sprintf (&new_event_name, NULL, ":sys:%s", event_name));

	/* Re-transmit the event */
	pending_call = upstart_emit_event (session_upstart,
			new_event_name, event_env, FALSE,
			NULL, emit_event_error, NULL,
			NIH_DBUS_TIMEOUT_NEVER);

	if (! pending_call) {
		NihError *err;
		err = nih_error_get ();
		nih_warn ("%s", err->message);
		nih_free (err);
	}

	dbus_pending_call_unref (pending_call);
	dbus_free_string_array (event_env);
}

static void
upstart_forward_restarted (void *          data,
		     NihDBusMessage *message,
		     const char *    path)
{
	DBusPendingCall *   pending_call;

	/* Re-transmit the event */
	pending_call = upstart_emit_event (session_upstart,
			":sys:restarted", NULL, FALSE,
			NULL, emit_event_error, NULL,
			NIH_DBUS_TIMEOUT_NEVER);

	if (! pending_call) {
		NihError *err;
		err = nih_error_get ();
		nih_warn ("%s", err->message);
		nih_free (err);
	}

	dbus_pending_call_unref (pending_call);
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
