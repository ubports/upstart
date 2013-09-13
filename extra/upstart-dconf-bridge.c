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
#include <nih/hash.h>
#include <nih/io.h>
#include <nih/option.h>
#include <nih/main.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <nih-dbus/dbus_connection.h>
#include <nih-dbus/dbus_proxy.h>

#include "dbus/upstart.h"
#include "com.ubuntu.Upstart.h"
#include "com.ubuntu.Upstart.Job.h"

/**
 * DCONF_EVENT:
 *
 * Name of event this program emits.
 **/
#define DCONF_EVENT "dconf"

/* Prototypes for static functions */
static void dconf_changed (DConfClient *client, const gchar *prefix,
			   const gchar * const *changes, const gchar *tag,
			   GDBusProxy *upstart);

static void handle_upstart_job (GDBusProxy *proxy, gchar *sender_name,
			 gchar *signal_name, GVariant *parameters,
			 gpointer user_data);

static int handle_existing_jobs (GDBusProxy *upstart_proxy)
	__attribute__ ((warn_unused_result));

static int job_needs_event (const char *object_path)
	__attribute__ ((warn_unused_result));

static int jobs_need_event (void)
	__attribute__ ((warn_unused_result));

/**
 * Structure we use for tracking jobs
 *
 * @entry: list header, 
 * @path: D-Bus path of job being tracked.
 **/
typedef struct job {
	NihList entry;
	char *path;
} Job;

/**
 * daemonise:
 *
 * Set to TRUE if we should become a daemon, rather than just running
 * in the foreground.
 **/
static int daemonise = FALSE;

/**
 * always:
 *
 * If TRUE, always emit Upstart events, regardless of whether
 * existing jobs care about DBUS_EVENT.
 */
static int always = FALSE;

/**
 * jobs:
 *
 * Jobs that we're monitoring.
 **/
static NihHash *jobs = NULL;

/**
 * connection:
 *
 * D-Bus connection to Upstart.
 **/
GDBusConnection *connection = NULL;

/**
 * options:
 *
 * Command-line options accepted by this program.
 **/
static NihOption options[] = {
	{ 0, "always", N_("Always emit an event on a dconf change"),
	  NULL, NULL, &always, NULL },
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

	/* Allocate jobs hash table */
	jobs = NIH_MUST (nih_hash_string_new (NULL, 0));

	/* Get an Upstart proxy object */
	upstart_proxy = g_dbus_proxy_new_sync (connection,
					       G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
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

	/* Connect signal to be notified when jobs come and go */
	g_signal_connect (upstart_proxy, "g-signal", (GCallback) handle_upstart_job, NULL);

	if (! handle_existing_jobs (upstart_proxy))
		exit (1);
	
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
			NIH_MUST (nih_strcat_sprintf (&pidfile, NULL,
						"%s/upstart-dconf-bridge.%s.pid",
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

	/* Handle TERM and INT signals gracefully */
	nih_signal_set_handler (SIGTERM, nih_signal_handler);
	NIH_MUST (nih_signal_add_handler (NULL, SIGTERM, nih_main_term_signal, NULL));

	/* Listen for any dconf change */
	g_signal_connect (client, "changed", (GCallback) dconf_changed, upstart_proxy);
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
 * handle_upstart_job:
 *
 * Called when an Upstart D-Bus signal is emitted.
 **/
static void
handle_upstart_job (GDBusProxy *proxy,
		    gchar      *sender_name,
		    gchar      *signal_name,
		    GVariant   *parameters,
		    gpointer    user_data)
{
	GVariantIter   iter;
	GVariant      *child;
	const gchar   *job_class_path;
	Job           *job;
	int            add;

	nih_assert (signal_name);
	nih_assert (parameters);
	nih_assert (jobs);

	if (! strcmp (signal_name, "JobAdded")) {
		add = 1;
	} else if (! strcmp (signal_name, "JobRemoved")) {
		add = 0;
	} else {
		return;
	}

	nih_assert (g_variant_is_of_type (parameters, G_VARIANT_TYPE_TUPLE));

	g_variant_iter_init (&iter, parameters);

	nih_assert (g_variant_iter_n_children (&iter) == 1);

	child = g_variant_iter_next_value (&iter);

	job_class_path = g_variant_get_string (child, NULL);
	nih_assert (g_variant_is_object_path (job_class_path));

	/* Free any existing record for the job if we are adding
	 * (should never happen, but worth being safe).
	 */
	job = (Job *)nih_hash_lookup (jobs, job_class_path);
	if (job)
		nih_free (job);

	/* Job isn't interested in DCONF_EVENT */
	if (add && ! job_needs_event (job_class_path))
		goto out;

	if (add)
		nih_debug ("Job got added %s for event %s", job_class_path, DCONF_EVENT);
	else
		nih_debug ("Job went away %s", job_class_path);

	/* We're removing, so job done */
	if (! add)
		goto out;

	/* Create new record for the job */
	job = NIH_MUST (nih_new (NULL, Job));
	job->path = NIH_MUST (nih_strdup (job, job_class_path));

	nih_list_init (&job->entry);
	nih_alloc_set_destructor (job, nih_list_destroy);
	nih_hash_add (jobs, &job->entry);

out:
	g_variant_unref (child);
}

/**
 * dconf_changed:
 *
 * Emit an Upstart event corresponding to a dconf key change.
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

	/* dconf currently only currently supports the changed signal,
	 * but parameterise to allow for a future API change.
	 */
	const gchar      *event_type = "TYPE=changed";

	if (! jobs_need_event () && ! always)
		return;

	/* Iterate through the various changes */
	while (changes[i] != NULL) {
		path = g_strconcat (prefix, changes[i], NULL);

		value = dconf_client_read (client, path);
		value_str = g_variant_print (value, FALSE);

		env_key = g_strconcat ("KEY=", path, NULL);
		env_value = g_strconcat ("VALUE=", value_str, NULL);

		/* Build event environment as GVariant */
		g_variant_builder_init (&builder, G_VARIANT_TYPE_TUPLE);

		g_variant_builder_add (&builder, "s", DCONF_EVENT);

		g_variant_builder_open (&builder, G_VARIANT_TYPE_ARRAY);
		g_variant_builder_add (&builder, "s", event_type);
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

/**
 * jobs_need_event:
 *
 * Returns: TRUE if any jobs need DCONF_EVENT, else FALSE.
 **/
static int
jobs_need_event (void)
{
	NIH_HASH_FOREACH (jobs, iter) {
		return TRUE;
	}

	return FALSE;
}

/**
 * job_needs_event:
 * @object_path: Full D-Bus object path for job.
 *
 * Returns: TRUE if job specified by @object_path specifies DCONF_EVENT
 * in its 'start on' or 'stop on' stanza, else FALSE.
 **/
static int
job_needs_event (const char *class_path)
{
	GDBusProxy    *job_proxy;
	GError        *error = NULL;
	GVariantIter   iter;
	const gchar   *event_name;
	int            ret = FALSE;

	/* Arrays of arrays of strings (aas) */
	GVariant      *start_on = NULL;
	GVariant      *stop_on = NULL;

	/* Array containing event name and optional environment
	 * variable elements.
	 */
	GVariant      *event_element;

	/* Either an event name or "/AND" or "/OR" */
	GVariant      *event;

	nih_assert (class_path);

	job_proxy = g_dbus_proxy_new_sync (connection,
			G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
			NULL, /* GDBusInterfaceInfo */
			NULL, /* name */
			class_path,
			"com.ubuntu.Upstart0_6.Job",
			NULL, /* GCancellable */
			&error);

	start_on = g_dbus_proxy_get_cached_property (job_proxy, "start_on");
	nih_assert (g_variant_is_of_type (start_on, G_VARIANT_TYPE_ARRAY));

	g_variant_iter_init (&iter, start_on);

	while ((event_element = g_variant_iter_next_value (&iter))) {
		nih_assert (g_variant_is_of_type (event_element, G_VARIANT_TYPE_ARRAY));

		/* First element is always the event name */
		event = g_variant_get_child_value (event_element, 0);
		nih_assert (g_variant_is_of_type (event, G_VARIANT_TYPE_STRING));

		event_name = g_variant_get_string (event, NULL);

		if (! strcmp (event_name, DCONF_EVENT))
			ret = TRUE;

		g_variant_unref (event_element);
		g_variant_unref (event);

		if (ret)
			goto out;
	}

	/* Now handle stop on */
	stop_on = g_dbus_proxy_get_cached_property (job_proxy, "stop_on");
	nih_assert (g_variant_is_of_type (stop_on, G_VARIANT_TYPE_ARRAY));

	g_variant_iter_init (&iter, stop_on);

	while ((event_element = g_variant_iter_next_value (&iter))) {
		nih_assert (g_variant_is_of_type (event_element, G_VARIANT_TYPE_ARRAY));

		/* First element is always the event name */
		event = g_variant_get_child_value (event_element, 0);
		nih_assert (g_variant_is_of_type (event, G_VARIANT_TYPE_STRING));

		event_name = g_variant_get_string (event, NULL);

		if (! strcmp (event_name, DCONF_EVENT))
			ret = TRUE;

		g_variant_unref (event_element);
		g_variant_unref (event);

		if (ret)
			goto out;
	}

out:
	if (start_on)
		g_variant_unref (start_on);

	if (stop_on)
		g_variant_unref (stop_on);

	g_object_unref (job_proxy);

	return ret;
}

/**
 * handle_existing_jobs:
 *
 * @upstart_proxy: Upstart proxy.
 *
 * Add all existing jobs which specify DCONF_EVENT to the list
 * of tracked jobs.
 *
 * Returns: TRUE or FALSE on error.
 **/
static int
handle_existing_jobs (GDBusProxy *upstart_proxy)
{
	GVariant      *result;
	GVariant      *child;
	GVariant      *proxy_job;
	const gchar   *job_class_path;
	GError        *error = NULL;
	GVariantIter   iter;
	Job           *job;

	nih_assert (upstart_proxy);

	result = g_dbus_proxy_call_sync (upstart_proxy,
			"GetAllJobs",
			NULL,
			G_DBUS_CALL_FLAGS_NO_AUTO_START,
			-1,
			NULL,
			&error);

	if (! result) {
		g_error ("D-BUS Upstart proxy error: %s",
				(error && error->message)
				? error->message
				: "Unknown error");
		g_clear_error (&error);
		return FALSE;
	}

	nih_assert (g_variant_is_of_type (result, G_VARIANT_TYPE_TUPLE));
	nih_assert (g_variant_n_children (result) == 1);

	child = g_variant_get_child_value (result, 0);

	nih_assert (g_variant_is_of_type (child, G_VARIANT_TYPE_OBJECT_PATH_ARRAY));

	g_variant_iter_init (&iter, child);

	while ((proxy_job = g_variant_iter_next_value (&iter))) {
		job_class_path = g_variant_get_string (proxy_job, NULL);

		/* Free any existing record for the job if we are adding
		 * (should never happen, but worth being safe).
		 */
		job = (Job *)nih_hash_lookup (jobs, job_class_path);
		if (job)
			nih_free (job);

		if (job_needs_event (job_class_path)) {
			/* Create new record for the job */
			job = NIH_MUST (nih_new (NULL, Job));
			job->path = NIH_MUST (nih_strdup (job, job_class_path));

			nih_list_init (&job->entry);
			nih_alloc_set_destructor (job, nih_list_destroy);
			nih_hash_add (jobs, &job->entry);

			nih_debug ("Job added %s for event %s", job_class_path, DCONF_EVENT);
		}

		g_variant_unref (proxy_job);
	}

	g_variant_unref (child);
	g_variant_unref (result);

	return TRUE;
}
