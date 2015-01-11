/* upstart
 *
 * Copyright © 2012-2013 Canonical Ltd.
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
#include "org.freedesktop.systemd1.h"
#include "org.freedesktop.systemd1.job.h"

/* Defines for constants */
#define SYSTEMD_EVENT "systemd"
#define DBUS_PATH_SYSTEMD "/org/freedesktop/systemd1"
#define DBUS_SERVICE_SYSTEMD "org.freedesktop.systemd1"

/* Structure we use for tracking systemd jobs */
typedef struct systemd_job {
	NihList entry;
	char *path;
	char *job_type;
} SystemdJob;

/* Prototypes for static functions */
static void dbus_disconnected (DBusConnection *connection);
static void upstart_forward_event    (void *data, NihDBusMessage *message,
				  const char *path);
static void emit_event (const char *unit, const char *job_type);
static void emit_event_error     (void *data, NihDBusMessage *message);


static void systemd_job_new (void *data, NihDBusMessage *message, uint32_t id, const char *job, const char *unit);

static void systemd_job_remove (void *data, NihDBusMessage *message, uint32_t id, const char *job, const char *unit, const char *result);

static SystemdJob *job_new (const char *path, const uint32_t id, const char *unit, const char *job_type)
	__attribute__ ((warn_unused_result));

static int *job_destroy (SystemdJob *job)
	__attribute__ ((warn_unused_result));

/**
 * daemonise:
 *
 * Set to TRUE if we should become a daemon, rather than just running
 * in the foreground.
 **/
static int daemonise = FALSE;

/**
 * systemd_jobs:
 *
 * Hash of systemd jobs that we're monitoring.
 **/
static NihHash *systemd_jobs = NULL;

/**
 * systemd:
 *
 * Proxy to systemd daemon.
 **/
static NihDBusProxy *systemd = NULL;

/**
 * user_upstart:
 *
 * Proxy to user Upstart daemon instance.
 **/
static NihDBusProxy *user_upstart = NULL;

/**
 * systemd_connection:
 *
 * System DBus connection.
 **/
static DBusConnection *system_connection = NULL;

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
	DBusConnection *     user_connection;
	int                  ret;
	char *               pidfile_path = NULL;
	char *               pidfile = NULL;
	char *               user_session_addr = NULL;
	nih_local char **    user_session_path = NULL;
	char *               path_element = NULL;

	nih_main_init (argv[0]);

	nih_option_set_synopsis (_("Bridge system upstart events into the user session upstart"));
	nih_option_set_help (
		_("By default, upstart-event-bridge does not detach from the "
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

	/* Allocate jobs hash table */
	systemd_jobs = NIH_MUST (nih_hash_string_new (NULL, 0));

	/* Initialise the connection to user session Upstart */
	user_connection = NIH_SHOULD (nih_dbus_connect (user_session_addr, dbus_disconnected));

	if (! user_connection) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not connect to the user session Upstart"),
			   err->message);
		nih_free (err);

		exit (1);
	}

	user_upstart = NIH_SHOULD (nih_dbus_proxy_new (NULL, user_connection,
						  NULL, DBUS_PATH_UPSTART,
						  NULL, NULL));
	if (! user_upstart) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not create Upstart proxy"),
			   err->message);
		nih_free (err);

		exit (1);
	}

	/* Initialise the connection to system systemd */
	system_connection = NIH_SHOULD (nih_dbus_bus (DBUS_BUS_SYSTEM, dbus_disconnected));

	if (! system_connection) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not connect to system DBus"),
			   err->message);
		nih_free (err);

		exit (1);
	}

	systemd = NIH_SHOULD (nih_dbus_proxy_new (NULL, system_connection,
						  DBUS_SERVICE_SYSTEMD, DBUS_PATH_SYSTEMD,
						  NULL, NULL));
	if (! systemd) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not create Upstart proxy"),
			   err->message);
		nih_free (err);

		exit (1);
	}

	if (! nih_dbus_proxy_connect (systemd, &systemd_org_freedesktop_systemd1_Manager, "JobNew",
				      (NihDBusSignalHandler)systemd_job_new, NULL)) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not create JobNew signal connection"),
			   err->message);
		nih_free (err);

		exit (1);
	}

	if (! nih_dbus_proxy_connect (systemd, &systemd_org_freedesktop_systemd1_Manager, "JobRemoved",
				      (NihDBusSignalHandler)systemd_job_remove, NULL)) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not create JobRemove signal connection"),
			   err->message);
		nih_free (err);

		exit (1);
	}

	if (systemd_subscribe_sync (NULL, systemd)) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not subscribe as a client"),
			   err->message);
		nih_free (err);

		exit (1);
	}

	/* Become daemon */
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
			NIH_MUST (nih_strcat_sprintf (&pidfile, NULL, "%s/%s.%s.pid",
						      pidfile_path, program_invocation_short_name, path_element));
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

static void
dbus_disconnected (DBusConnection *connection)
{
	nih_fatal (_("Disconnected from DBus"));
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
	pending_call = upstart_emit_event (user_upstart,
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
emit_event (const char *unit,
	    const char *job_type)
{
	DBusPendingCall *pending_call;
	nih_local char **env = NULL;
	nih_local char *var = NULL;
	size_t env_len = 0;
	
	env = NIH_MUST (nih_str_array_new (NULL));
	
	var = NIH_MUST (nih_sprintf (NULL, "UNIT=%s", unit));
	NIH_MUST (nih_str_array_addp (&env, NULL, &env_len, var));
	
	var = NIH_MUST (nih_sprintf (NULL, "JOBTYPE=%s", job_type));
	NIH_MUST (nih_str_array_addp (&env, NULL, &env_len, var));
	
	pending_call = NIH_SHOULD (upstart_emit_event
				   (user_upstart, SYSTEMD_EVENT, env, FALSE,
				    NULL, emit_event_error, NULL,
				    NIH_DBUS_TIMEOUT_NEVER));
	
	if (! pending_call) {
		NihError *err;
		
		err = nih_error_get ();
		nih_warn ("%s", err->message);
		nih_free (err);
	}
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

static void
systemd_job_new (void *data, NihDBusMessage *message, uint32_t id, const char *job, const char *unit)
{
	nih_local NihDBusProxy *job_proxy = NULL;
	nih_local char *job_type = NULL;

	// get job proxy
	job_proxy = NIH_SHOULD (nih_dbus_proxy_new (NULL, system_connection,
						  DBUS_SERVICE_SYSTEMD, job,
						  NULL, NULL));
	if (! job_proxy) {
		NihError *err;

		err = nih_error_get ();
		nih_error ("%s: %s", _("Could not get job_proxy"),
			   err->message);
		nih_free (err);
		goto notype;
	}

	if (systemd_job_get_job_type_sync (NULL, job_proxy, &job_type) < 0) {
		NihError *err;
		err = nih_error_get ();
		nih_error ("%s: %s", _("Could not get JobType"),
			   err->message);
		nih_free (err);
		goto notype;
	}

	// create our job object in the jobs hash
	NIH_SHOULD (job_new (job, id, unit, job_type));
	return;

notype:
	// create our job object in the jobs hash
	NIH_SHOULD (job_new (job, id, unit, "unknown"));
	return;
}

static void
systemd_job_remove (void *data, NihDBusMessage *message, uint32_t id, const char *job, const char *unit, const char *result)
{

	nih_local SystemdJob *systemd_job = NULL;

	// pop from hashtable
	systemd_job = (SystemdJob *)nih_hash_lookup (systemd_jobs, job);

	if (!systemd_job)
		return;

	// if done, emit event
	if (! strcmp("done", result)) {
		emit_event (unit, systemd_job->job_type);
	}
}

/**
 * job_new:
 *
 * @id: Systemd Job ID
 * @unit: Name of the associated unit
 * @job_type: Job type
 *
 * Create a new Job object representing an inflight systemd job.
 *
 * Returns: job, or NULL on insufficient memory.
 **/
static SystemdJob *
job_new (const char *path, const uint32_t id, const char *unit, const char *job_type)
{
	SystemdJob *job = NULL;

	nih_assert (id);
	nih_assert (unit);

	job = nih_new (NULL, SystemdJob);
	if (! job)
		return NULL;

	nih_list_init (&job->entry);

	nih_alloc_set_destructor (job, nih_list_destroy);

	job->path = nih_strdup (job, path);
	if (! job->path)
		goto error;

	job->job_type = nih_strdup (job, job_type);
	if (! job->job_type)
		goto error;

	if (! nih_hash_add_unique (systemd_jobs, &job->entry))
		goto error;

	return job;

error:
	nih_free (job);
	return NULL;
}
