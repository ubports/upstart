/* upstart-dconf-bridge
 *
 * Copyright © 2012 Canonical Ltd.
 * Author: Stéphane Graber <stgraber@ubuntu.com>.
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
#include <dbus/dbus-glib.h>

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

/* Prototypes for static functions */
static void dconf_changed   (DConfClient *client, const gchar *prefix,
							    const gchar * const *changes, const gchar *tag,
							    DBusGProxy *upstart);

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
	char **             args;
	DConfClient *       client;
	GMainLoop *         mainloop;
	DBusGConnection *   dbus;
	DBusGProxy *        upstart;
	GError *            error = NULL;

	/* Initialise the various gobjects */
	g_type_init ();
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

	dbus = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (error != NULL) {
		g_error ("D-BUS Connection error: %s", error->message);
		g_error_free (error);
	}

	if (!dbus) {
		g_error ("D-BUS connection cannot be created");
		return 1;
	}

	upstart = dbus_g_proxy_new_for_name (dbus,
										    "com.ubuntu.Upstart",
										    "/com/ubuntu/Upstart",
										    "com.ubuntu.Upstart0_6");

	if (!upstart) {
		g_error ("Cannot connect to upstart");
		return 1;
	}

	/* Become daemon FIXME: Tries to create a pid file, fails when non-root */
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

	/* Listen for any dconf change */
	g_signal_connect (client, "changed", (GCallback) dconf_changed, upstart);
	dconf_client_watch_sync (client, "/");

	/* Start the glib mainloop */
	g_main_loop_run (mainloop);

	g_object_unref (client);
	g_main_loop_unref (mainloop);

	return 0;
}

static
void dconf_changed (DConfClient *client, const gchar *prefix,
					    const gchar * const *changes, const gchar *tag,
					    DBusGProxy *upstart)
{
	GVariant *          value;
	gchar *             value_str = NULL;
	gchar *             path = NULL;
	gchar *             env_key = NULL;
	gchar *             env_value = NULL;
	char **             env;
	GError *            error = NULL;
	int                 i = 0;

	/* Iterate through the various changes */
	while (changes[i] != NULL) {
		/* Get the current values */
		path = g_strconcat (prefix, path, NULL);
		value = dconf_client_read (client, path);
		value_str = g_variant_print (value, FALSE);

		/* FIXME: Debug */
		g_print("%s => %s\n", path, value_str);

		env_key = g_strconcat ("KEY=", path, NULL);
		env_value = g_strconcat ("VALUE=", value_str, NULL);

		/* Build event environment */
		env = g_new (char *, 2);
		env[0] = env_key;
		env[1] = env_value;
		env[2] = NULL;

		/* Send the event */
		dbus_g_proxy_call (upstart, "EmitEvent", &error,
						    G_TYPE_STRING, "dconf-changed",
						    G_TYPE_STRV, env,
						    G_TYPE_BOOLEAN, FALSE,
						    G_TYPE_INVALID,
						    G_TYPE_INVALID);

		g_free (path);
		g_variant_unref (value);
		g_free (value_str);
		g_free (env_key);
		g_free (env_value);
		g_free (env);

		if (error) {
			/* Ignore DBUS errors.
			   Those events will just be lost until upstart reappears.
			 */
			g_error_free (error);
		}

		i += 1;
	}

	return;
}
