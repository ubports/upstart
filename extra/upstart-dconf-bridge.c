/* upstart-dconf-bridge
 *
 * Copyright © 2012-2013 Canonical Ltd.
 * Author: Stéphane Graber <stgraber@ubuntu.com>.
 * Author: Thomas Bechtold <thomasbechtold@jpberlin.de>.
 * Author: James Hunt <james.hunt@ubuntu.com>.
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

#include <dconf.h>
#include <gio/gio.h>

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

/**
 * DCONF_EVENT:
 *
 * Name of event this program emits.
 **/
#define DCONF_EVENT "dconf-changed"

/* Prototypes for static functions */
static void dconf_changed (DConfClient *client, const gchar *prefix,
			   const gchar * const *changes, const gchar *tag,
			   GDBusProxy *upstart);

/**
 * daemonise:
 *
 * Set to TRUE if we should become a daemon, rather than just running
 * in the foreground.
 **/
static int daemonise = FALSE;

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
	char             **args;
	DConfClient       *client;
	GMainLoop         *mainloop;
	GDBusProxy        *upstart_proxy;
	GDBusConnection   *connection;
	GError            *error = NULL;
	char              *user_session_addr = NULL;
	nih_local char   **user_session_path = NULL;
	char              *path_element = NULL;
	char              *pidfile_path = NULL;
	char              *pidfile = NULL;

	client = dconf_client_new ();
	mainloop = g_main_loop_new (NULL, FALSE);

	/* Use NIH to parse the arguments */
	nih_main_init (argv[0]);

	nih_option_set_synopsis (_("Bridge dconf events into upstart"));
	nih_option_set_help (
		_("By default, upstart-dconf-bridge does not detach from the "
		  "console and remains in the foreground.  Use the --daemon "
		  "option to have it detach."));

	args = nih_option_parser (NULL, argc, argv, options, FALSE);
	if (! args)
		exit (1);

	user_session_addr = getenv ("UPSTART_SESSION");
	if (! user_session_addr) {
		nih_fatal (_("UPSTART_SESSION isn't set in environment"));
		exit (1);
	}

	/* Connect to the Upstart session */
	connection = g_dbus_connection_new_for_address_sync (user_session_addr,
			G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
			NULL, /* GDBusAuthObserver*/
			NULL, /* GCancellable */
			&error);

	if (! connection) {
		g_error ("D-BUS Upstart session init error: %s",
			 (error && error->message) ? error->message : "Unknown error");
		g_clear_error (&error);
		exit (1);
	}

	/* Get an Upstart proxy object */
	upstart_proxy = g_dbus_proxy_new_sync  (connection,
					        (G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START
						| G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES),
					        NULL, /* GDBusInterfaceInfo */
					        NULL, /* name */
					        "/com/ubuntu/Upstart",
					        "com.ubuntu.Upstart0_6",
					        NULL, /* GCancellable */
					        &error);

	if (! upstart_proxy) {
		g_error ("D-BUS Upstart proxy error: %s",
			 (error && error->message) ? error->message : "Unknown error");
		g_clear_error (&error);
		exit (1);
	}

	if (daemonise) {
		/* Deal with the pidfile location when becoming a daemon.
		 * We need to be able to run one bridge per upstart daemon.
		 * Store the PID file in XDG_RUNTIME_DIR or HOME and include the pid of
		 * the Upstart instance (last part of the DBus path) in the filename.
		 */

		/* Extract PID from UPSTART_SESSION */
		user_session_path = nih_str_split (NULL, user_session_addr, "/", TRUE);

		for (int i = 0; user_session_path && user_session_path[i]; i++)
			path_element = user_session_path[i];

		if (! path_element) {
			nih_fatal (_("Invalid value for UPSTART_SESSION"));
			exit (1);
		}

		pidfile_path = getenv ("XDG_RUNTIME_DIR");
		if (!pidfile_path)
			pidfile_path = getenv ("HOME");

		if (pidfile_path) {
			NIH_MUST (nih_strcat_sprintf (&pidfile, NULL, "%s/upstart-dconf-bridge.%s.pid",
					                        pidfile_path, path_element));
			nih_main_set_pidfile (pidfile);
		}

		if (nih_main_daemonise () < 0) {
			NihError *err;

			err = nih_error_get ();
			nih_fatal ("%s: %s", _("Unable to become daemon"),
				   err->message);
			nih_free (err);

			exit (1);
		}
	}

	/* Listen for any dconf change */
	g_signal_connect (client, "changed", (GCallback) dconf_changed,
					  upstart_proxy);
	dconf_client_watch_sync (client, "/");

	/* Start the glib mainloop */
	g_main_loop_run (mainloop);

	g_object_unref (client);
	g_object_unref (upstart_proxy);
	g_object_unref (connection);
	g_main_loop_unref (mainloop);

	exit (0);
}

/**
 * dconf_changed:
 *
 * Emit an event 
 *
 **/
static void
dconf_changed (DConfClient         *client,
	       const gchar         *prefix,
	       const gchar * const *changes,
	       const gchar         *tag,
	       GDBusProxy          *upstart)
{
	GVariant         *value;
	gchar            *value_str = NULL;
	gchar            *path = NULL;
	gchar            *env_key = NULL;
	gchar            *env_value = NULL;
	GVariant         *event;
	GVariantBuilder   builder;
	int               i = 0;

	/* Iterate through the various changes */
	while (changes[i] != NULL) {
		if (changes[i] && *changes[i]) {
			/* It is unclear from the documentation which if
			 * either of prefix or each individual change element contains
			 * a slash as separator, but clearly to
			 * reconstruct the full path we need to produce one from
			 * somewhere.
			 */
			if (g_str_has_suffix (prefix, "/") || g_str_has_prefix (changes[i], "/"))
				path = g_strconcat (prefix, changes[i], NULL);
			else
				path = g_strconcat (prefix, "/", changes[i], NULL);
		} else {
			path = g_strconcat (prefix, NULL);
		}

		value = dconf_client_read (client, path);
		value_str = g_variant_print (value, FALSE);

		env_key = g_strconcat ("KEY=", path, NULL);
		env_value = g_strconcat ("VALUE=", value_str, NULL);

		/* Build event environment as GVariant */
		g_variant_builder_init (&builder, G_VARIANT_TYPE_TUPLE);

		g_variant_builder_add (&builder, "s", DCONF_EVENT);

		g_variant_builder_open (&builder, G_VARIANT_TYPE_ARRAY);
		g_variant_builder_add (&builder, "s", env_key);
		g_variant_builder_add (&builder, "s", env_value);
		g_variant_builder_close (&builder);

		g_variant_builder_add (&builder, "b", FALSE);
		event = g_variant_builder_end (&builder);

		/* Send the event */
		g_dbus_proxy_call (upstart,
				   "EmitEvent",
				   event,
				   G_DBUS_CALL_FLAGS_NONE,
				   -1,
				   NULL,
				   NULL, /* GAsyncReadyCallback
				            we don't care about the answer */
				   NULL);

		g_variant_builder_clear (&builder);
		g_variant_unref (value);
		g_free (path);
		g_free (value_str);
		g_free (env_key);
		g_free (env_value);

		i += 1;
	}
}
