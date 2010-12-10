/* upstart
 *
 * Copyright © 2010 Canonical Ltd.
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


#include <stdlib.h>
#include <syslog.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/hash.h>
#include <nih/string.h>
#include <nih/option.h>
#include <nih/main.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <nih-dbus/dbus_connection.h>
#include <nih-dbus/dbus_proxy.h>

#include "dbus/upstart.h"
#include "com.ubuntu.Upstart.h"
#include "com.ubuntu.Upstart.Job.h"


/* Structure we use for tracking jobs */
typedef struct job {
	NihList entry;
	char *path;
} Job;


/* Prototypes for static functions */
static void upstart_job_added    (void *data, NihDBusMessage *message,
				  const char *job);
static void upstart_job_removed  (void *data, NihDBusMessage *message,
				  const char *job);
static void upstart_disconnected (DBusConnection *connection);
//static void emit_event_error     (void *data, NihDBusMessage *message);


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
 * jobs:
 *
 * Jobs that we're monitoring.
 **/
static NihHash *jobs = NULL;


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
	char **         args;
	DBusConnection *connection;
	char **         job_class_paths;
	int             ret;

	nih_main_init (argv[0]);

	nih_option_set_synopsis (_("Bridge socket events into upstart"));
	nih_option_set_help (
		_("By default, upstart-socket-bridge does not detach from the "
		  "console and remains in the foreground.  Use the --daemon "
		  "option to have it detach."));

	args = nih_option_parser (NULL, argc, argv, options, FALSE);
	if (! args)
		exit (1);

	jobs = NIH_MUST (nih_hash_string_new (NULL, 0));

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

	/* Connect signals to be notified when jobs come and go */
	if (! nih_dbus_proxy_connect (upstart, &upstart_com_ubuntu_Upstart0_6, "JobAdded",
				      (NihDBusSignalHandler)upstart_job_added, NULL)) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not create JobAdded signal connection"),
			   err->message);
		nih_free (err);

		exit (1);
	}

	if (! nih_dbus_proxy_connect (upstart, &upstart_com_ubuntu_Upstart0_6, "JobRemoved",
				      (NihDBusSignalHandler)upstart_job_removed, NULL)) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not create JobRemoved signal connection"),
			   err->message);
		nih_free (err);

		exit (1);
	}

	/* Request a list of all current jobs */
	if (upstart_get_all_jobs_sync (NULL, upstart, &job_class_paths) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not obtain job list"),
			   err->message);
		nih_free (err);

		exit (1);
	}

	for (char **job_class_path = job_class_paths;
	     job_class_path && *job_class_path; job_class_path++)
		upstart_job_added (NULL, NULL, *job_class_path);

	nih_free (job_class_paths);

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
upstart_job_added (void *          data,
		   NihDBusMessage *message,
		   const char *    job_class_path)
{
	nih_local NihDBusProxy *job_class = NULL;
	nih_local char ***start_on = NULL;
	nih_local char ***stop_on = NULL;
	Job *job;

	nih_assert (job_class_path != NULL);

	nih_debug ("Job got added %s", job_class_path);

	/* Obtain a proxy to the job */
	job_class = nih_dbus_proxy_new (NULL, upstart->connection,
					upstart->name, job_class_path,
					NULL, NULL);
	if (! job_class) {
		NihError *err;

		err = nih_error_get ();
		nih_error ("Could not create proxy for job %s: %s",
			   job_class_path, err->message);
		nih_free (err);

		return;
	}

	job_class->auto_start = FALSE;

	/* Obtain the start_on and stop_on properties of the job */
	if (job_class_get_start_on_sync (NULL, job_class, &start_on) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_error ("Could not obtain job start condition %s: %s",
			   job_class_path, err->message);
		nih_free (err);

		return;
	}

	if (job_class_get_start_on_sync (NULL, job_class, &stop_on) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_error ("Could not obtain job stop condition %s: %s",
			   job_class_path, err->message);
		nih_free (err);

		return;
	}

	/* Free any existing record for the job (should never happen,
	 * but worth being safe).
	 */
	job = (Job *)nih_hash_lookup (jobs, job_class_path);
	if (job)
		nih_free (job);

	/* Create new record for the job */
	job = NIH_MUST (nih_new (NULL, Job));
	job->path = NIH_MUST (nih_strdup (job, job_class_path));

	nih_list_init (&job->entry);
	nih_alloc_set_destructor (job, nih_list_destroy);
	nih_hash_add (jobs, &job->entry);
}

static void
upstart_job_removed (void *          data,
		     NihDBusMessage *message,
		     const char *    job_path)
{
	Job *job;

	nih_assert (job_path != NULL);

	job = (Job *)nih_hash_lookup (jobs, job_path);
	if (job) {
		nih_debug ("Job went away %s", job_path);
		nih_free (job);
	}
}


static void
upstart_disconnected (DBusConnection *connection)
{
	nih_fatal (_("Disconnected from Upstart"));
	nih_main_loop_exit (1);
}

/*
static void
emit_event_error (void *          data,
		  NihDBusMessage *message)
{
	NihError *err;

	err = nih_error_get ();
	nih_warn ("%s", err->message);
	nih_free (err);
}
*/
