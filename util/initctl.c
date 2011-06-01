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


#include <dbus/dbus.h>

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/main.h>
#include <nih/option.h>
#include <nih/command.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <nih-dbus/dbus_error.h>
#include <nih-dbus/dbus_proxy.h>
#include <nih-dbus/errors.h>

#include "dbus/upstart.h"

#include "com.ubuntu.Upstart.h"
#include "com.ubuntu.Upstart.Job.h"
#include "com.ubuntu.Upstart.Instance.h"


/* Prototypes for local functions */
NihDBusProxy *upstart_open (const void *parent)
	__attribute__ ((warn_unused_result, malloc));
char *        job_status   (const void *parent,
			    NihDBusProxy *job_class, NihDBusProxy *job)
	__attribute__ ((warn_unused_result, malloc));

/* Prototypes for static functions */
static void   start_reply_handler (char **job_path, NihDBusMessage *message,
				   const char *instance);
static void   reply_handler       (int *ret, NihDBusMessage *message);
static void   error_handler       (void *data, NihDBusMessage *message);

#ifndef TEST

static int    dbus_bus_type_setter  (NihOption *option, const char *arg);

#endif

/* Prototypes for option and command functions */
int start_action                (NihCommand *command, char * const *args);
int stop_action                 (NihCommand *command, char * const *args);
int restart_action              (NihCommand *command, char * const *args);
int reload_action               (NihCommand *command, char * const *args);
int status_action               (NihCommand *command, char * const *args);
int list_action                 (NihCommand *command, char * const *args);
int emit_action                 (NihCommand *command, char * const *args);
int reload_configuration_action (NihCommand *command, char * const *args);
int version_action              (NihCommand *command, char * const *args);
int log_priority_action         (NihCommand *command, char * const *args);


/**
 * use_dbus:
 *
 * If  1, connect using a D-Bus bus.
 * If  0, connect using private connection.
 * If -1, determine appropriate connection based on UID.
 */
int use_dbus = -1;

/**
 * dbus_bus_type:
 *
 * D-Bus bus to connect to (DBUS_BUS_SYSTEM or DBUS_BUS_SESSION), or -1
 * to have an appropriate bus selected.
 */
int dbus_bus_type = -1;

/**
 * dest_name:
 *
 * Name on the D-Bus system bus that the message should be sent to when
 * system is TRUE.
 **/
char *dest_name = NULL;

/**
 * dest_address:
 *
 * Address for private D-Bus connection.
 **/
const char *dest_address = DBUS_ADDRESS_UPSTART;

/**
 * no_wait:
 *
 * Whether to wait for a job or event to be finished before existing or not.
 **/
int no_wait = FALSE;

/**
 * NihOption setter function to handle selection of appropriate D-Bus
 * bus.
 *
 * Always returns 1 denoting success.
 **/
int
dbus_bus_type_setter (NihOption *option, const char *arg)
{
	nih_assert (option);

	if (! strcmp (option->long_option, "system")) {
		use_dbus      = TRUE;
		dbus_bus_type = DBUS_BUS_SYSTEM;
	}
	else if (! strcmp (option->long_option, "session")) {
		use_dbus      = TRUE;
		dbus_bus_type = DBUS_BUS_SESSION;
	}

	return 1;
}

/**
 * upstart_open:
 * @parent: parent object for new proxy.
 *
 * Opens a connection to the init daemon and returns a proxy to the manager
 * object.  If @dest_name is not NULL, a connection is instead opened to
 * the system bus and the proxy linked to the well-known name given.
 *
 * Error messages are output to standard error.
 *
 * If @parent is not NULL, it should be a pointer to another object which
 * will be used as a parent for the returned proxy.  When all parents
 * of the returned proxy are freed, the returned proxy will also be
 * freed.
 *
 * Returns: newly allocated D-Bus proxy or NULL on error.
 **/
NihDBusProxy *
upstart_open (const void *parent)
{
	DBusError       dbus_error;
	DBusConnection *connection;
	NihDBusProxy *  upstart;

	if (use_dbus < 0)
		use_dbus = getuid () ? TRUE : FALSE;
	if (use_dbus >= 0 && dbus_bus_type < 0)
		dbus_bus_type = DBUS_BUS_SYSTEM;

	dbus_error_init (&dbus_error);
	if (use_dbus) {
		if (! dest_name)
			dest_name = DBUS_SERVICE_UPSTART;

		connection = dbus_bus_get (dbus_bus_type, &dbus_error);
		if (! connection) {
			nih_error ("%s: %s",
				dbus_bus_type == DBUS_BUS_SYSTEM
				? _("Unable to connect to system bus")
				: _("Unable to connect to session bus"),
				   dbus_error.message);
			dbus_error_free (&dbus_error);
			return NULL;
		}

		dbus_connection_set_exit_on_disconnect (connection, FALSE);
	} else {
		if (dest_name) {
			fprintf (stderr, _("%s: --dest given without --system\n"),
				 program_name);
			nih_main_suggest_help ();
			return NULL;
		}

		connection = dbus_connection_open (dest_address, &dbus_error);
		if (! connection) {
			nih_error ("%s: %s", _("Unable to connect to Upstart"),
				   dbus_error.message);
			dbus_error_free (&dbus_error);
			return NULL;
		}
	}
	dbus_error_free (&dbus_error);

	upstart = nih_dbus_proxy_new (parent, connection,
				      dest_name,
				      DBUS_PATH_UPSTART,
				      NULL, NULL);
	if (! upstart) {
		NihError *err;

		err = nih_error_get ();
		nih_error ("%s", err->message);
		nih_free (err);

		dbus_connection_unref (connection);
		return NULL;
	}

	upstart->auto_start = FALSE;

	/* Drop initial reference now the proxy holds one */
	dbus_connection_unref (connection);

	return upstart;
}


/**
 * job_status:
 * @parent: parent object for new string,
 * @job_class: proxy for remote job class object,
 * @job: proxy for remote instance object.
 *
 * Queries the job object @job and contructs a string defining the status
 * of that instance, containing the name of the @job_class and @job,
 * the goal, state and any running processes.
 *
 * @job may be NULL in which case a non-running job is assumed, the
 * function also catches the remote object no longer existing.
 *
 * If @parent is not NULL, it should be a pointer to another object which
 * will be used as a parent for the returned string.  When all parents
 * of the returned string are freed, the returned string will also be
 * freed.
 *
 * Returns: newly allocated string or NULL on raised error..
 **/
char *
job_status (const void *  parent,
	    NihDBusProxy *job_class,
	    NihDBusProxy *job)
{
	nih_local char *         job_class_name = NULL;
	nih_local JobProperties *props = NULL;
	char *                   str = NULL;

	nih_assert (job_class != NULL);

	/* Get the job name */
	if (job_class_get_name_sync (NULL, job_class, &job_class_name) < 0)
		return NULL;

	/* Get the instance properties, catching the instance going away
	 * between the time we got it and this call.
	 */
	if (job) {
		if (job_get_all_sync (NULL, job, &props) < 0) {
			NihDBusError *dbus_err;

			dbus_err = (NihDBusError *)nih_error_get ();
			if ((dbus_err->number != NIH_DBUS_ERROR)
			    || strcmp (dbus_err->name, DBUS_ERROR_UNKNOWN_METHOD)) {
				return NULL;
			} else {
				nih_free (dbus_err);
			}
		}
	}

	if (props && *props->name) {
		str = nih_sprintf (parent, "%s (%s)",
				   job_class_name, props->name);
		if (! str)
			nih_return_no_memory_error (NULL);
	} else {
		str = nih_strdup (parent, job_class_name);
		if (! str)
			nih_return_no_memory_error (NULL);
	}

	if (props) {
		if (! nih_strcat_sprintf (&str, parent, " %s/%s",
					  props->goal, props->state)) {
			nih_error_raise_no_memory ();
			nih_free (str);
			return NULL;
		}

		/* The first process returned is always the main process,
		 * which is the process we always want to display alongside
		 * the state if there is one.  Prefix if it's not one of
		 * the standard processes.
		 */
		if (props->processes[0]) {
			if (strcmp (props->processes[0]->item0, "main")
			    && strcmp (props->processes[0]->item0, "pre-start")
			    && strcmp (props->processes[0]->item0, "post-stop")) {
				if (! nih_strcat_sprintf (&str, parent, ", (%s) process %d",
							  props->processes[0]->item0,
							  props->processes[0]->item1)) {
					nih_error_raise_no_memory ();
					nih_free (str);
					return NULL;
				}
			} else {
				if (! nih_strcat_sprintf (&str, parent, ", process %d",
							  props->processes[0]->item1)) {
					nih_error_raise_no_memory ();
					nih_free (str);
					return NULL;
				}
			}

			/* Append a line for each additional process */
			for (JobProcessesElement **p = &props->processes[1];
			     p && *p; p++) {
				if (! nih_strcat_sprintf (&str, parent, "\n\t%s process %d",
							  (*p)->item0,
							  (*p)->item1)) {
					nih_error_raise_no_memory ();
					nih_free (str);
					return NULL;
				}
			}
		}
	} else {
		if (! nih_strcat (&str, parent, " stop/waiting")) {
			nih_error_raise_no_memory ();
			nih_free (str);
			return NULL;
		}
	}

	return str;
}


/**
 * start_action:
 * @command: NihCommand invoked,
 * @args: command-line arguments.
 *
 * This function is called for the "start" command.
 *
 * Returns: command exit status.
 **/
int
start_action (NihCommand *  command,
	      char * const *args)
{
	nih_local NihDBusProxy *upstart = NULL;
	const char *            upstart_job = NULL;
	const char *            upstart_instance = NULL;
	nih_local char *        job_class_path = NULL;
	nih_local NihDBusProxy *job_class = NULL;
	nih_local char *        job_path = NULL;
	nih_local NihDBusProxy *job = NULL;
	DBusPendingCall *       pending_call;
	int                     ret = 1;
	NihError *              err;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	if (args[0]) {
		upstart_job = args[0];
	} else {
		upstart_job = getenv ("UPSTART_JOB");
		upstart_instance = getenv ("UPSTART_INSTANCE");

		if (! (upstart_job && upstart_instance)) {
			fprintf (stderr, _("%s: missing job name\n"), program_name);
			nih_main_suggest_help ();
			return 1;
		}
	}

	upstart = upstart_open (NULL);
	if (! upstart)
		return 1;

	/* Obtain a proxy to the job */
	if (upstart_get_job_by_name_sync (NULL, upstart, upstart_job,
					  &job_class_path) < 0)
		goto error;

	job_class = nih_dbus_proxy_new (NULL, upstart->connection,
					upstart->name, job_class_path,
					NULL, NULL);
	if (! job_class)
		goto error;

	job_class->auto_start = FALSE;

	/* When called from a job handler, we directly change the goal
	 * so we need the instance.  These calls are always made without
	 * waiting, since otherwise we'd block the job we're called from.
	 */
	if (upstart_instance) {
		if (job_class_get_instance_by_name_sync (NULL, job_class,
							 upstart_instance,
							 &job_path) < 0)
			goto error;

		job = nih_dbus_proxy_new (NULL, upstart->connection,
					  upstart->name, job_path,
					  NULL, NULL);
		if (! job)
			goto error;

		job->auto_start = FALSE;

		pending_call = job_start (job, FALSE,
					  (JobStartReply)reply_handler,
					  error_handler, &ret,
					  NIH_DBUS_TIMEOUT_NEVER);
		if (! pending_call)
			goto error;
	} else {
		/* job_path is nih_local, so whatever gets filled in will
		 * be automatically freed.
		 */
		pending_call = job_class_start (job_class, &args[1], (! no_wait),
						(JobClassStartReply)start_reply_handler,
						error_handler, &job_path,
						NIH_DBUS_TIMEOUT_NEVER);
		if (! pending_call)
			goto error;
	}

	dbus_pending_call_block (pending_call);
	dbus_pending_call_unref (pending_call);

	/* Make sure we got a valid job path, and in the case of the instance
	 * path, no error message.  Then display the current job status.
	 */
	if (job_path && ((job == NULL) || (ret == 0))) {
		nih_local char *status = NULL;

		if (! job) {
			job = NIH_SHOULD (nih_dbus_proxy_new (
						  NULL, upstart->connection,
						  upstart->name, job_path,
						  NULL, NULL));
			if (! job)
				goto error;

			job->auto_start = FALSE;
		}

		status = NIH_SHOULD (job_status (NULL, job_class, job));
		if (! status)
			goto error;

		nih_message ("%s", status);

		ret = 0;
	} else {
		ret = 1;
	}

	return ret;

error:
	err = nih_error_get ();
	nih_error ("%s", err->message);
	nih_free (err);

	return 1;
}


/**
 * stop_action:
 * @command: NihCommand invoked,
 * @args: command-line arguments.
 *
 * This function is called for the "stop" command.
 *
 * Returns: command exit status.
 **/
int
stop_action (NihCommand *  command,
	     char * const *args)
{
	nih_local NihDBusProxy *upstart = NULL;
	const char *            upstart_job = NULL;
	const char *            upstart_instance = NULL;
	nih_local char *        job_class_path = NULL;
	nih_local NihDBusProxy *job_class = NULL;
	nih_local char *        job_path = NULL;
	nih_local NihDBusProxy *job = NULL;
	DBusPendingCall *       pending_call;
	int                     ret = 1;
	NihError *              err;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	if (args[0]) {
		upstart_job = args[0];
	} else {
		upstart_job = getenv ("UPSTART_JOB");
		upstart_instance = getenv ("UPSTART_INSTANCE");

		if (! (upstart_job && upstart_instance)) {
			fprintf (stderr, _("%s: missing job name\n"), program_name);
			nih_main_suggest_help ();
			return 1;
		}
	}

	upstart = upstart_open (NULL);
	if (! upstart)
		return 1;

	/* Obtain a proxy to the job */
	if (upstart_get_job_by_name_sync (NULL, upstart, upstart_job,
					  &job_class_path) < 0)
		goto error;

	job_class = nih_dbus_proxy_new (NULL, upstart->connection,
					upstart->name, job_class_path,
					NULL, NULL);
	if (! job_class)
		goto error;

	job_class->auto_start = FALSE;

	/* When called from a job handler, we directly change the goal
	 * so need the instance.  These calls are always made without
	 * waiting, since otherwise we'd block the job we're called from.
	 */
	if (upstart_instance) {
		if (job_class_get_instance_by_name_sync (NULL, job_class,
							 upstart_instance,
							 &job_path) < 0)
			goto error;

		job = nih_dbus_proxy_new (NULL, upstart->connection,
					  upstart->name, job_path,
					  NULL, NULL);
		if (! job)
			goto error;

		job->auto_start = FALSE;

		pending_call = job_stop (job, FALSE,
					 (JobStopReply)reply_handler,
					 error_handler, &ret,
					 NIH_DBUS_TIMEOUT_NEVER);
		if (! pending_call)
			goto error;
	} else {
		/* Lookup the instance that we'll end up stopping */
		if (job_class_get_instance_sync (NULL, job_class, &args[1],
						 &job_path) < 0)
			goto error;

		job = nih_dbus_proxy_new (NULL, upstart->connection,
					  upstart->name, job_path,
					  NULL, NULL);
		if (! job)
			goto error;

		job->auto_start = FALSE;

		pending_call = job_class_stop (job_class, &args[1], (! no_wait),
					       (JobClassStopReply)reply_handler,
					       error_handler, &ret,
					       NIH_DBUS_TIMEOUT_NEVER);
		if (! pending_call)
			goto error;
	}

	dbus_pending_call_block (pending_call);
	dbus_pending_call_unref (pending_call);

	/* Display the current job status */
	if (ret == 0) {
		nih_local char *status = NULL;

		status = NIH_SHOULD (job_status (NULL, job_class, job));
		if (! status)
			goto error;

		nih_message ("%s", status);
	}

	return ret;

error:
	err = nih_error_get ();
	nih_error ("%s", err->message);
	nih_free (err);

	return 1;
}


/**
 * restart_action:
 * @command: NihCommand invoked,
 * @args: command-line arguments.
 *
 * This function is called for the "restart" command.
 *
 * Returns: command exit status.
 **/
int
restart_action (NihCommand *  command,
		char * const *args)
{
	nih_local NihDBusProxy *upstart = NULL;
	const char *            upstart_job = NULL;
	const char *            upstart_instance = NULL;
	nih_local char *        job_class_path = NULL;
	nih_local NihDBusProxy *job_class = NULL;
	nih_local char *        job_path = NULL;
	nih_local NihDBusProxy *job = NULL;
	DBusPendingCall *       pending_call;
	int                     ret = 1;
	NihError *              err;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	if (args[0]) {
		upstart_job = args[0];
	} else {
		upstart_job = getenv ("UPSTART_JOB");
		upstart_instance = getenv ("UPSTART_INSTANCE");

		if (! (upstart_job && upstart_instance)) {
			fprintf (stderr, _("%s: missing job name\n"), program_name);
			nih_main_suggest_help ();
			return 1;
		}
	}

	upstart = upstart_open (NULL);
	if (! upstart)
		return 1;

	/* Obtain a proxy to the job */
	if (upstart_get_job_by_name_sync (NULL, upstart, upstart_job,
					  &job_class_path) < 0)
		goto error;

	job_class = nih_dbus_proxy_new (NULL, upstart->connection,
					upstart->name, job_class_path,
					NULL, NULL);
	if (! job_class)
		goto error;

	job_class->auto_start = FALSE;

	/* When called from a job handler, we directly toggle the goal
	 * so we need the instance.  These calls are always made without
	 * waiting, since otherwise we'd block the job we're called from.
	 */
	if (upstart_instance) {
		if (job_class_get_instance_by_name_sync (NULL, job_class,
							 upstart_instance,
							 &job_path) < 0)
			goto error;

		job = nih_dbus_proxy_new (NULL, upstart->connection,
					  upstart->name, job_path,
					  NULL, NULL);
		if (! job)
			goto error;

		job->auto_start = FALSE;

		pending_call = job_restart (job, FALSE,
					    (JobRestartReply)reply_handler,
					    error_handler, &ret,
					    NIH_DBUS_TIMEOUT_NEVER);
		if (! pending_call)
			goto error;
	} else {
		/* job_path is nih_local, so whatever gets filled in will
		 * be automatically freed.
		 */
		pending_call = job_class_restart (job_class, &args[1], (! no_wait),
						  (JobClassRestartReply)start_reply_handler,
						  error_handler, &job_path,
						  NIH_DBUS_TIMEOUT_NEVER);
		if (! pending_call)
			goto error;
	}

	dbus_pending_call_block (pending_call);
	dbus_pending_call_unref (pending_call);

	/* Make sure we got a valid job path, and in the case of the instance
	 * path, no error message.  Then display the current job status.
	 */
	if (job_path && ((job == NULL) || (ret == 0))) {
		nih_local char *status = NULL;

		if (! job) {
			job = NIH_SHOULD (nih_dbus_proxy_new (
						  NULL, upstart->connection,
						  upstart->name, job_path,
						  NULL, NULL));
			if (! job)
				goto error;

			job->auto_start = FALSE;
		}

		status = NIH_SHOULD (job_status (NULL, job_class, job));
		if (! status)
			goto error;

		nih_message ("%s", status);

		ret = 0;
	} else {
		ret = 1;
	}

	return ret;

error:
	err = nih_error_get ();
	nih_error ("%s", err->message);
	nih_free (err);

	return 1;
}


/**
 * reload_action:
 * @command: NihCommand invoked,
 * @args: command-line arguments.
 *
 * This function is called for the "reload" command.
 *
 * Returns: command exit status.
 **/
int
reload_action (NihCommand *  command,
	       char * const *args)
{
	nih_local NihDBusProxy *upstart = NULL;
	const char *            upstart_job = NULL;
	const char *            upstart_instance = NULL;
	nih_local char *        job_class_path = NULL;
	nih_local NihDBusProxy *job_class = NULL;
	nih_local char *        job_path = NULL;
	nih_local NihDBusProxy *job = NULL;
	nih_local JobProcessesElement **processes = NULL;
	NihError *              err;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	if (args[0]) {
		upstart_job = args[0];
	} else {
		upstart_job = getenv ("UPSTART_JOB");
		upstart_instance = getenv ("UPSTART_INSTANCE");

		if (! (upstart_job && upstart_instance)) {
			fprintf (stderr, _("%s: missing job name\n"), program_name);
			nih_main_suggest_help ();
			return 1;
		}
	}

	upstart = upstart_open (NULL);
	if (! upstart)
		return 1;

	/* Obtain a proxy to the job */
	if (upstart_get_job_by_name_sync (NULL, upstart, upstart_job,
					  &job_class_path) < 0)
		goto error;

	job_class = nih_dbus_proxy_new (NULL, upstart->connection,
					upstart->name, job_class_path,
					NULL, NULL);
	if (! job_class)
		goto error;

	job_class->auto_start = FALSE;

	/* Obtain a proxy to the specific instance.  Catch the case where
	 * we were just given a job name, and there was no single instance
	 * running.
	 */
	if (upstart_instance) {
		if (job_class_get_instance_by_name_sync (NULL, job_class,
							 upstart_instance,
							 &job_path) < 0)
			goto error;
	} else {
		if (job_class_get_instance_sync (NULL, job_class,
						 &args[1], &job_path) < 0)
			goto error;
	}

	job = nih_dbus_proxy_new (NULL, upstart->connection,
				  upstart->name, job_path,
				  NULL, NULL);
	if (! job)
		goto error;

	job->auto_start = FALSE;

	/* Get the process list */
	if (job_get_processes_sync (NULL, job, &processes) < 0)
		goto error;

	if ((! processes[0]) || strcmp (processes[0]->item0, "main")) {
		nih_error (_("Not running"));
		return 1;
	}

	if (kill (processes[0]->item1, SIGHUP) < 0) {
		nih_error_raise_system ();
		goto error;
	}

	return 0;

error:
	err = nih_error_get ();
	nih_error ("%s", err->message);
	nih_free (err);

	return 1;
}


/**
 * status_action:
 * @command: NihCommand invoked,
 * @args: command-line arguments.
 *
 * This function is called for the "status" command.
 *
 * Returns: command exit status.
 **/
int
status_action (NihCommand *  command,
	       char * const *args)
{
	nih_local NihDBusProxy *upstart = NULL;
	const char *            upstart_job = NULL;
	const char *            upstart_instance = NULL;
	nih_local char *        job_class_path = NULL;
	nih_local NihDBusProxy *job_class = NULL;
	nih_local char *        job_path = NULL;
	nih_local NihDBusProxy *job = NULL;
	nih_local char *        status = NULL;
	NihError *              err;
	NihDBusError *          dbus_err;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	if (args[0]) {
		upstart_job = args[0];
	} else {
		upstart_job = getenv ("UPSTART_JOB");
		upstart_instance = getenv ("UPSTART_INSTANCE");

		if (! (upstart_job && upstart_instance)) {
			fprintf (stderr, _("%s: missing job name\n"), program_name);
			nih_main_suggest_help ();
			return 1;
		}
	}

	upstart = upstart_open (NULL);
	if (! upstart)
		return 1;

	/* Obtain a proxy to the job */
	if (upstart_get_job_by_name_sync (NULL, upstart, upstart_job,
					  &job_class_path) < 0)
		goto error;

	job_class = nih_dbus_proxy_new (NULL, upstart->connection,
					upstart->name, job_class_path,
					NULL, NULL);
	if (! job_class)
		goto error;

	job_class->auto_start = FALSE;

	/* Obtain a proxy to the specific instance.  Catch the case where
	 * we were just given a job name, and there was no single instance
	 * running.
	 */
	if (upstart_instance) {
		if (job_class_get_instance_by_name_sync (NULL, job_class,
							 upstart_instance,
							 &job_path) < 0)
			goto error;
	} else {
		if (job_class_get_instance_sync (NULL, job_class,
						 &args[1], &job_path) < 0) {
			dbus_err = (NihDBusError *)nih_error_get ();
			if (args[1]
			    || (dbus_err->number != NIH_DBUS_ERROR)
			    || strcmp (dbus_err->name, DBUS_INTERFACE_UPSTART ".Error.UnknownInstance"))
				goto error;

			nih_free (dbus_err);
		}
	}

	if (job_path) {
		job = nih_dbus_proxy_new (NULL, upstart->connection,
					  upstart->name, job_path,
					  NULL, NULL);
		if (! job)
			goto error;

		job->auto_start = FALSE;
	} else {
		job = NULL;
	}

	status = job_status (NULL, job_class, job);
	if (! status)
		goto error;

	nih_message ("%s", status);

	return 0;

error:
	err = nih_error_get ();
	nih_error ("%s", err->message);
	nih_free (err);

	return 1;
}


/**
 * list_action:
 * @command: NihCommand invoked,
 * @args: command-line arguments.
 *
 * This function is called for the "list" command.
 *
 * Returns: command exit status.
 **/
int
list_action (NihCommand *  command,
	     char * const *args)
{
	nih_local NihDBusProxy *upstart = NULL;
	nih_local char **       job_class_paths = NULL;
	NihError *              err;
	NihDBusError *          dbus_err;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	upstart = upstart_open (NULL);
	if (! upstart)
		return 1;

	/* Obtain a list of jobs */
	if (upstart_get_all_jobs_sync (NULL, upstart, &job_class_paths) < 0)
		goto error;

	for (char **job_class_path = job_class_paths;
	     job_class_path && *job_class_path; job_class_path++) {
		nih_local NihDBusProxy *job_class = NULL;
		nih_local char **       job_paths = NULL;

		job_class = nih_dbus_proxy_new (NULL, upstart->connection,
						upstart->name, *job_class_path,
						NULL, NULL);
		if (! job_class)
			goto error;

		job_class->auto_start = FALSE;

		/* Obtain a list of instances, catch an error from the
		 * command and assume the job just went away.
		 */
		if (job_class_get_all_instances_sync (NULL, job_class,
						      &job_paths) < 0) {
			dbus_err = (NihDBusError *)nih_error_get ();
			if ((dbus_err->number != NIH_DBUS_ERROR)
			    || strcmp (dbus_err->name, DBUS_ERROR_UNKNOWN_METHOD))
				goto error;

			nih_free (dbus_err);
			continue;
		}

		/* Handle no running instances by assuming that the
		 * job is just stopped, again catch an error and assume
		 * the job went away.
		 */
		if (! *job_paths) {
			nih_local char *status = NULL;

			status = job_status (NULL, job_class, NULL);
			if (! status) {
				dbus_err = (NihDBusError *)nih_error_get ();
				if ((dbus_err->number != NIH_DBUS_ERROR)
				    || strcmp (dbus_err->name, DBUS_ERROR_UNKNOWN_METHOD))
					goto error;

				nih_free (dbus_err);
				continue;
			}

			nih_message ("%s", status);
		}

		/* Otherwise iterate the instances and output the status
		 * of each.
		 */
		for (char **job_path = job_paths; job_path && *job_path;
		     job_path++) {
			nih_local NihDBusProxy *job = NULL;
			nih_local char *        status = NULL;

			job = nih_dbus_proxy_new (NULL, upstart->connection,
						  upstart->name, *job_path,
						  NULL, NULL);
			if (! job)
				goto error;

			job->auto_start = FALSE;

			status = job_status (NULL, job_class, job);
			if (! status) {
				dbus_err = (NihDBusError *)nih_error_get ();
				if ((dbus_err->number != NIH_DBUS_ERROR)
				    || strcmp (dbus_err->name, DBUS_ERROR_UNKNOWN_METHOD))
					goto error;

				nih_free (dbus_err);
				continue;
			}

			nih_message ("%s", status);
		}
	}

	return 0;

error:
	err = nih_error_get ();
	nih_error ("%s", err->message);
	nih_free (err);

	return 1;
}


/**
 * emit_action:
 * @command: NihCommand invoked,
 * @args: command-line arguments.
 *
 * This function is called for the "emit" command.
 *
 * Returns: command exit status.
 **/
int
emit_action (NihCommand *  command,
	     char * const *args)
{
	nih_local NihDBusProxy *upstart = NULL;
	DBusPendingCall *       pending_call;
	int                     ret = 1;
	NihError *              err;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	if (! args[0]) {
		fprintf (stderr, _("%s: missing event name\n"), program_name);
		nih_main_suggest_help ();
		return 1;
	}

	upstart = upstart_open (NULL);
	if (! upstart)
		return 1;

	pending_call = upstart_emit_event (upstart, args[0], &args[1], (! no_wait),
					   (UpstartEmitEventReply)reply_handler,
					   error_handler, &ret,
					   NIH_DBUS_TIMEOUT_NEVER);
	if (! pending_call)
		goto error;

	dbus_pending_call_block (pending_call);
	dbus_pending_call_unref (pending_call);

	return ret;

error:
	err = nih_error_get ();
	nih_error ("%s", err->message);
	nih_free (err);

	return 1;
}


/**
 * reload_configuration_action:
 * @command: NihCommand invoked,
 * @args: command-line arguments.
 *
 * This function is called for the "reload-configuration" command.
 *
 * Returns: command exit status.
 **/
int
reload_configuration_action (NihCommand *  command,
			     char * const *args)
{
	nih_local NihDBusProxy *upstart = NULL;
	NihError *              err;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	upstart = upstart_open (NULL);
	if (! upstart)
		return 1;

	if (upstart_reload_configuration_sync (NULL, upstart) < 0)
		goto error;

	return 0;

error:
	err = nih_error_get ();
	nih_error ("%s", err->message);
	nih_free (err);

	return 1;
}


/**
 * version_action:
 * @command: NihCommand invoked,
 * @args: command-line arguments.
 *
 * This function is called for the "version" command.
 *
 * Returns: command exit status.
 **/
int
version_action (NihCommand *  command,
		char * const *args)
{
	nih_local NihDBusProxy *upstart = NULL;
	nih_local char *        version = NULL;
	NihError *              err;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	upstart = upstart_open (NULL);
	if (! upstart)
		return 1;

	if (upstart_get_version_sync (NULL, upstart, &version) < 0)
		goto error;

	nih_message ("%s", version);

	return 0;

error:
	err = nih_error_get ();
	nih_error ("%s", err->message);
	nih_free (err);

	return 1;
}


/**
 * log_priority_action:
 * @command: NihCommand invoked,
 * @args: command-line arguments.
 *
 * This function is called for the "log-priority" command.
 *
 * Returns: command exit status.
 **/
int
log_priority_action (NihCommand *  command,
		     char * const *args)
{
	nih_local NihDBusProxy *upstart = NULL;
	NihError *              err;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	upstart = upstart_open (NULL);
	if (! upstart)
		return 1;

	if (args[0]) {
		if (upstart_set_log_priority_sync (NULL, upstart, args[0]) < 0)
			goto error;
	} else {
		nih_local char *log_priority = NULL;

		if (upstart_get_log_priority_sync (NULL, upstart,
						   &log_priority) < 0)
			goto error;

		nih_message ("%s", log_priority);
	}

	return 0;

error:
	err = nih_error_get ();
	nih_error ("%s", err->message);
	nih_free (err);

	return 1;
}


static void
start_reply_handler (char **         job_path,
		     NihDBusMessage *message,
		     const char *    instance)
{
	nih_assert (message != NULL);
	nih_assert (instance != NULL);

	/* Return the job path */
	*job_path = NIH_MUST (nih_strdup (NULL, instance));
}

static void
reply_handler (int *           ret,
	       NihDBusMessage *message)
{
	nih_assert (message != NULL);

	*ret = 0;
}

static void
error_handler (void *          data,
	       NihDBusMessage *message)
{
	NihError *err;

	nih_assert (message != NULL);

	err = nih_error_get ();
	nih_error ("%s", err->message);
	nih_free (err);
}


#ifndef TEST
/**
 * options:
 *
 * Command-line options accepted for all arguments.
 **/
static NihOption options[] = {
	{ 0, "session", N_("use D-Bus session bus to connect to init daemon (for testing)"),
	  NULL, NULL, NULL, dbus_bus_type_setter },
	{ 0, "system", N_("use D-Bus system bus to connect to init daemon"),
	  NULL, NULL, NULL, dbus_bus_type_setter },
	{ 0, "dest", N_("destination well-known name on D-Bus bus"),
	  NULL, "NAME", &dest_name, NULL },

	NIH_OPTION_LAST
};


/**
 * start_options:
 *
 * Command-line options accepted for the start command.
 **/
NihOption start_options[] = {
	{ 'n', "no-wait", N_("do not wait for job to start before exiting"),
	  NULL, NULL, &no_wait, NULL },

	NIH_OPTION_LAST
};

/**
 * stop_options:
 *
 * Command-line options accepted for the stop command.
 **/
NihOption stop_options[] = {
	{ 'n', "no-wait", N_("do not wait for job to stop before exiting"),
	  NULL, NULL, &no_wait, NULL },

	NIH_OPTION_LAST
};

/**
 * restart_options:
 *
 * Command-line options accepted for the restart command.
 **/
NihOption restart_options[] = {
	{ 'n', "no-wait", N_("do not wait for job to restart before exiting"),
	  NULL, NULL, &no_wait, NULL },

	NIH_OPTION_LAST
};

/**
 * reload_options:
 *
 * Command-line options accepted for the reload command.
 **/
NihOption reload_options[] = {
	NIH_OPTION_LAST
};

/**
 * status_options:
 *
 * Command-line options accepted for the status command.
 **/
NihOption status_options[] = {
	NIH_OPTION_LAST
};

/**
 * list_options:
 *
 * Command-line options accepted for the list command.
 **/
NihOption list_options[] = {
	NIH_OPTION_LAST
};

/**
 * emit_options:
 *
 * Command-line options accepted for the emit command.
 **/
NihOption emit_options[] = {
	{ 'n', "no-wait", N_("do not wait for event to finish before exiting"),
	  NULL, NULL, &no_wait, NULL },

	NIH_OPTION_LAST
};

/**
 * reload_configuration_options:
 *
 * Command-line options accepted for the reload-configuration command.
 **/
NihOption reload_configuration_options[] = {
	NIH_OPTION_LAST
};

/**
 * version_options:
 *
 * Command-line options accepted for the version command.
 **/
NihOption version_options[] = {
	NIH_OPTION_LAST
};

/**
 * log_priority_options:
 *
 * Command-line options accepted for the log-priority command.
 **/
NihOption log_priority_options[] = {
	NIH_OPTION_LAST
};


/**
 * job_group:
 *
 * Group of commands related to jobs.
 **/
static NihCommandGroup job_commands = { N_("Job") };

/**
 * event_group:
 *
 * Group of commands related to events.
 **/
static NihCommandGroup event_commands = { N_("Event") };

/**
 * commands:
 *
 * Commands accepts as the first non-option argument, or program name.
 **/
static NihCommand commands[] = {
	{ "start", N_("JOB [KEY=VALUE]..."),
	  N_("Start job."),
	  N_("JOB is the name of the job that is to be started, this may "
	     "be followed by zero or more environment variables to be "
	     "defined in the new job.\n"
	     "\n"
	     "The environment may also serve to distinguish between job "
	     "instances, and thus decide whether a new instance will be "
	     "started or an error returned if an existing instance is "
	     "already running."),
	  &job_commands, start_options, start_action },

	{ "stop", N_("JOB [KEY=VALUE]..."),
	  N_("Stop job."),
	  N_("JOB is the name of the job that is to be stopped, this may "
	     "be followed by zero or more environment variables to be "
	     "passed to the job's pre-stop and post-stop processes.\n"
	     "\n"
	     "The environment also serves to distinguish between job "
	     "instances, and thus decide which of multiple instances will "
	     "be stopped."),
	  &job_commands, stop_options, stop_action },

	{ "restart", N_("JOB [KEY=VALUE]..."),
	  N_("Restart job."),
	  N_("JOB is the name of the job that is to be restarted, this may "
	     "be followed by zero or more environment variables to be "
	     "defined in the job after restarting.\n"
	     "\n"
	     "The environment also serves to distinguish between job "
	     "instances, and thus decide which of multiple instances will "
	     "be restarted."),
	  &job_commands, restart_options, restart_action },

	{ "reload", N_("JOB [KEY=VALUE]..."),
	  N_("Send HUP signal to job."),
	  N_("JOB is the name of the job that is to be sent the signal, "
	     "this may be followed by zero or more environment variables "
	     "to distinguish between job instances.\n"),
	  &job_commands, reload_options, reload_action },

	{ "status", N_("JOB [KEY=VALUE]..."),
	  N_("Query status of job."),
	  N_("JOB is the name of the job that is to be queried, this may "
	     "be followed by zero or more environment variables to "
	     "distguish between job instances.\n" ),
	  &job_commands, status_options, status_action },

	{ "list", NULL,
	  N_("List known jobs."),
	  N_("The known jobs and their current status will be output."),
	  &job_commands, list_options, list_action },

	{ "emit", N_("EVENT [KEY=VALUE]..."),
	  N_("Emit an event."),
	  N_("EVENT is the name of an event the init daemon should emit, "
	     "this may be followed by zero or more environment variables "
	     "to be included in the event.\n"),
	  &event_commands, emit_options, emit_action },

	{ "reload-configuration", NULL,
	  N_("Reload the configuration of the init daemon."),
	  NULL,
	  NULL, reload_configuration_options, reload_configuration_action },
	{ "version", NULL,
	  N_("Request the version of the init daemon."),
	  NULL,
	  NULL, version_options, version_action },
	{ "log-priority", N_("[PRIORITY]"),
	  N_("Change the minimum priority of log messages from the init "
	     "daemon"),
	  N_("PRIORITY may be one of:\n"
	     "  `debug' (messages useful for debugging upstart are logged, "
	     "equivalent to --debug on kernel command-line);\n"
	     "  `info' (messages about job goal and state changes, as well "
	     "as event emissions are logged, equivalent to --verbose on the "
	     "kernel command-line);\n"
	     "  `message' (informational and debugging messages are suppressed, "
	     "the default); "
	     "  `warn' (ordinary messages are suppressed whilst still "
	     "logging warnings and errors);\n"
	     "  `error' (only errors are logged, equivalent to --quiet on "
	     "the kernel command-line) or\n"
	     "  `fatal' (only fatal errors are logged).\n"
	     "\n"
	     "Without arguments, this outputs the current log priority."),
	  NULL, log_priority_options, log_priority_action },

	NIH_COMMAND_LAST
};


int
main (int   argc,
      char *argv[])
{
	int ret;

	nih_main_init (argv[0]);

	ret = nih_command_parser (NULL, argc, argv, options, commands);
	if (ret < 0)
		exit (1);

	dbus_shutdown ();

	return ret;
}
#endif
