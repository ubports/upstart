/* upstart-dconf-bridge
 *
 * Copyright © 2012 Canonical Ltd.
 * Author: Stéphane Graber <stgraber@ubuntu.com>.
 * Author: Thomas Bechtold <thomasbechtold@jpberlin.de>.
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

/* Prototypes for static functions */
static void dconf_changed   (DConfClient *client, const gchar *prefix,
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
	char **             args;
	DConfClient *       client;
	GMainLoop *         mainloop;
	GDBusProxy *        upstart_proxy;
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

	/* get a upstart proxy object on session bus */
	upstart_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
						       G_DBUS_PROXY_FLAGS_NONE,
						       NULL, /* GDBusInterfaceInfo */
						       "com.ubuntu.Upstart",
						       "/com/ubuntu/Upstart",
						       "com.ubuntu.Upstart0_6",
						       NULL, /* GCancellable */
						       &error);

	if (upstart_proxy == NULL) {
		g_error ("D-BUS Upstart proxy error: %s",
			 (error && error->message) ? error->message : "Unknown error");
		g_clear_error (&error);
		return EXIT_FAILURE;
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
	g_signal_connect (client, "changed", (GCallback) dconf_changed, upstart_proxy);
	dconf_client_watch_sync (client, "/");

	/* Start the glib mainloop */
	g_main_loop_run (mainloop);

	g_object_unref (client);
	g_object_unref (upstart_proxy);
	g_main_loop_unref (mainloop);

	return EXIT_SUCCESS;
}

static
void dconf_changed (DConfClient *client, const gchar *prefix,
		    const gchar * const *changes, const gchar *tag,
		    GDBusProxy *upstart)
{
	GVariant *          value;
	gchar *             value_str = NULL;
	gchar *             path = NULL;
	gchar *             env_key = NULL;
	gchar *             env_value = NULL;
	int                 i = 0;
	GVariantBuilder     builder;
	GVariant *          event;

	/* Iterate through the various changes */
	while (changes[i] != NULL) {
		/* Get the current values */
		path = g_strconcat (prefix, path, NULL);
		value = dconf_client_read (client, path);
		value_str = g_variant_print (value, FALSE);
		env_key = g_strconcat ("KEY=", prefix, path, NULL);
		env_value = g_strconcat ("VALUE=", value_str, NULL);

                /* FIXME: Debug */
		g_debug ("'%s' => '%s'", env_key, env_value);

		/* Build event environment as GVariant */
		g_variant_builder_init (&builder, G_VARIANT_TYPE_TUPLE);
		g_variant_builder_add (&builder, "s", "dconf-changed");
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
				   NULL, /* GAsyncReadyCallback - we don't care about the answer */
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
