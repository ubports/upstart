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
#include <fnmatch.h>
#include <pwd.h>
#include <dirent.h>
#include <ctype.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/main.h>
#include <nih/option.h>
#include <nih/command.h>
#include <nih/logging.h>
#include <nih/error.h>
#include <nih/hash.h>
#include <nih/tree.h>
#include <nih/file.h>

#include <nih-dbus/dbus_error.h>
#include <nih-dbus/dbus_proxy.h>
#include <nih-dbus/errors.h>
#include <nih-dbus/dbus_connection.h>

#include "dbus/upstart.h"

#include "com.ubuntu.Upstart.h"
#include "com.ubuntu.Upstart.Job.h"
#include "com.ubuntu.Upstart.Instance.h"

#include "init/events.h"
#include "init/xdg.h"
#include "initctl.h"


/* Prototypes for local functions */
NihDBusProxy *upstart_open (const void *parent)
	__attribute__ ((warn_unused_result, malloc));
char *        job_status   (const void *parent,
			    NihDBusProxy *job_class, NihDBusProxy *job)
	__attribute__ ((warn_unused_result, malloc));
char *        job_usage    (const void *parent,
			    NihDBusProxy *job_class)
	__attribute__ ((warn_unused_result, malloc));

/* Prototypes for static functions */
static void   start_reply_handler (char **job_path, NihDBusMessage *message,
				   const char *instance);
static void   reply_handler       (int *ret, NihDBusMessage *message);
static void   error_handler       (void *data, NihDBusMessage *message);

static void   job_class_condition_handler (void *data,
		NihDBusMessage *message,
		char ** const *value);

static void   job_class_condition_err_handler (void *data,
		NihDBusMessage *message);

static void   job_class_parse_events (const ConditionHandlerData *data,
		char ** const *variant_array);

static void   job_class_show_emits (const void *parent,
		NihDBusProxy *job_class_proxy, const char *job_class_name);

static void   job_class_show_conditions (NihDBusProxy *job_class_proxy,
		const char *job_class_name);

static void   eval_expr_tree (const char *expr, NihList **stack);

static int    check_condition (const char *job_class,
		const char *condition, NihList *condition_list,
		int *job_class_displayed)
	__attribute__ ((warn_unused_result));

static int    tree_filter (void *data, NihTree *node);

static void   display_check_errors (const char *job_class,
		const char *condition, NihTree *node);

static int    allow_job (const char *job);
static int    allow_event (const char *event);

#ifndef TEST

static int    dbus_bus_type_setter  (NihOption *option, const char *arg);
static int    ignored_events_setter (NihOption *option, const char *arg);

#endif

/* Prototypes for option and command functions */
int start_action                  (NihCommand *command, char * const *args);
int stop_action                   (NihCommand *command, char * const *args);
int restart_action                (NihCommand *command, char * const *args);
int reload_action                 (NihCommand *command, char * const *args);
int status_action                 (NihCommand *command, char * const *args);
int list_action                   (NihCommand *command, char * const *args);
int emit_action                   (NihCommand *command, char * const *args);
int reload_configuration_action   (NihCommand *command, char * const *args);
int version_action                (NihCommand *command, char * const *args);
int log_priority_action           (NihCommand *command, char * const *args);
int show_config_action            (NihCommand *command, char * const *args);
int check_config_action           (NihCommand *command, char * const *args);
int usage_action                  (NihCommand *command, char * const *args);
int notify_disk_writeable_action  (NihCommand *command, char * const *args);
int list_sessions_action          (NihCommand *command, char * const *args);

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
 * user_mode:
 *
 * If TRUE, talk to Upstart over the private socket defined in UPSTART_SESSION
 * if UPSTART_SESSION isn't defined, then fallback to the session bus.
 **/
int user_mode = FALSE;

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
 * enumerate_events:
 *
 * If TRUE, list out all events/jobs that a particular job *may require* to
 * be run: essentially any event/job mentioned in a job configuration files
 * "start on" / "stop on" condition. Used for showing dependencies
 * between jobs and events.
 **/
int enumerate_events = FALSE;

/**
 * check_config_mode:
 *
 * If TRUE, parse all job configuration files looking for unreachable
 * jobs/events.
 **/
int check_config_mode = FALSE;

/**
 * check_config_warn:
 *
 * If TRUE, check-config will generate a warning for *any* unreachable
 * events/jobs.
 **/
int check_config_warn = FALSE;

/**
 * check_config_data:
 *
 * Used to record details of all known jobs and events.
 **/
CheckConfigData check_config_data;

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
 * NihOption setter function to handle specification of events to
 * ignore.
 *
 * Returns 1 on success, else 0.
 **/
int
ignored_events_setter (NihOption *option, const char *arg)
{
	NihError     *err;
	char        **events;
	char        **event;
	NihListEntry *entry;

	nih_assert (option);
	nih_assert (arg);

	if (! check_config_data.ignored_events_hash)
		check_config_data.ignored_events_hash = NIH_MUST (nih_hash_string_new (NULL, 0));

	events = nih_str_split (NULL, arg, ",", TRUE);

	if (!events) {
		goto error;
	}

	for (event = events; event && *event; ++event) {
		entry = NIH_MUST (nih_list_entry_new (check_config_data.ignored_events_hash));
		entry->str = NIH_MUST (nih_strdup (entry, *event));
		nih_hash_add (check_config_data.ignored_events_hash, &entry->entry);
	}

	nih_free (events);

	return 1;

error:
	err = nih_error_get ();
	nih_error ("%s", err->message);
	nih_free (err);

	return 0;
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
	char * user_addr;

	user_addr = getenv ("UPSTART_SESSION");

	if (user_addr && dbus_bus_type < 0) {
		user_mode = TRUE;
	}

	if (! user_mode) {
		if (use_dbus < 0)
			use_dbus = getuid () ? TRUE : FALSE;
		if (use_dbus >= 0 && dbus_bus_type < 0)
			dbus_bus_type = DBUS_BUS_SYSTEM;
	}
	else {
		if (! user_addr) {
			nih_error ("UPSTART_SESSION isn't set in the environment. "
				       "Unable to locate the Upstart instance.");
			return NULL;
		}
		dest_address = user_addr;
		use_dbus = FALSE;
	}


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
 * Returns: newly allocated string or NULL on raised error.
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
 * job_usage:
 * @parent: parent object,
 * @job_class_proxy: D-Bus proxy for job class,
 * @job_class_name: Name of job class.
 *
 * Display usage of job class.
 * 
 * Returns: newly allocated string or NULL on raised error.
 **/
char *
job_usage (const void *parent, NihDBusProxy *job_class_proxy)
{
	char             *usage = NULL;

	nih_assert (job_class_proxy);

	if (job_class_get_usage_sync (parent, job_class_proxy, &usage) < 0) {
		return NULL;
	}

	return usage;
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
 * show_config_action:
 * @command: NihCommand invoked,
 * @args: command-line arguments.
 *
 * This function is called for the "show-config" command.
 *
 * Returns: command exit status.
 **/
int
show_config_action (NihCommand *  command,
	     char * const *args)
{
	nih_local NihDBusProxy  *upstart = NULL;
	nih_local char         **job_class_paths = NULL;
	const char              *upstart_job_class = NULL;
	NihError                *err;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	upstart = upstart_open (NULL);
	if (! upstart)
		return 1;

	if (args[0]) {
		/* Single job specified */
		upstart_job_class = args[0];
		job_class_paths = NIH_MUST (nih_alloc (NULL, 2*sizeof (char *)));
		job_class_paths[1] = NULL;

		if (upstart_get_job_by_name_sync (NULL, upstart, upstart_job_class,
					job_class_paths) < 0)
			goto error;
	} else {
		/* Obtain a list of jobs */
		if (upstart_get_all_jobs_sync (NULL, upstart, &job_class_paths) < 0)
			goto error;
	}

	for (char **job_class_path = job_class_paths;
	     job_class_path && *job_class_path; job_class_path++) {
		nih_local NihDBusProxy *job_class      = NULL;
		nih_local char         *job_class_name = NULL;

		job_class = nih_dbus_proxy_new (NULL, upstart->connection,
						upstart->name, *job_class_path,
						NULL, NULL);
		if (! job_class)
			goto error;

		job_class->auto_start = FALSE;

		if (job_class_get_name_sync (NULL, job_class, &job_class_name) < 0)
			goto error;

		if (! check_config_mode)
			nih_message ("%s", job_class_name);

		job_class_show_emits (NULL, job_class, job_class_name);
		job_class_show_conditions (job_class, job_class_name);

		/* Add any jobs *without* "start on"/"stop on" conditions
		 * to ensure we have a complete list of jobs for check-config to work with.
		 */
		if (check_config_mode) {
			JobCondition *entry;

			entry = (JobCondition *)nih_hash_lookup (check_config_data.job_class_hash,
					job_class_name);
			if (!entry) {
				MAKE_JOB_CONDITION (check_config_data.job_class_hash,
						entry, job_class_name);
				nih_hash_add (check_config_data.job_class_hash, &entry->list);
			}
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
 * usage_action:
 * @command: NihCommand invoked,
 * @args: command-line arguments.
 *
 * This function is called for the "usage" command.
 *
 * Returns: command exit usage.
 **/
int
usage_action (NihCommand *  command,
	       char * const *args)
{
	nih_local NihDBusProxy *upstart = NULL;
	const char *            upstart_job = NULL;
	const char *            upstart_instance = NULL;
	nih_local char *        job_class_name = NULL;
	nih_local char *        job_class_path = NULL;
	nih_local NihDBusProxy *job_class = NULL;
	nih_local char *        job_path = NULL;
	nih_local NihDBusProxy *job = NULL;
	nih_local char *        usage = NULL;
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

	if (job_class_get_name_sync (NULL, job_class, &job_class_name) < 0)
		goto error;

	usage = job_usage (NULL, job_class);
	if (! usage)
		goto error;

	nih_message ("%s: %s", _("Usage"), usage);

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


/**
 * check_config_action:
 * @command: NihCommand invoked,
 * @args: command-line arguments.
 *
 * This function is called for the "check-config" command.
 *
 * Returns: command exit status.
 **/
int
check_config_action (NihCommand *command,
		char * const *args)
{
	int              ret;
	char            *no_args[1] = { NULL };
	char            *job_class  = NULL;

	check_config_data.job_class_hash = NIH_MUST (nih_hash_string_new (NULL, 0));
	check_config_data.event_hash     = NIH_MUST (nih_hash_string_new (NULL, 0));

	if (! check_config_data.ignored_events_hash)
		check_config_data.ignored_events_hash =
			NIH_MUST (nih_hash_string_new (NULL, 0));

	/* Tell other functions we are running in a special mode */
	check_config_mode = TRUE;

	/* Obtain emits, start on and stop on data.
	 *
	 * Note: we pass null args since we always want details of all jobs.
	 */
	ret = show_config_action (command, no_args);

	if (ret)
		return 0;

	if (args[0]) {
		NihList *entry;
		job_class = args[0];

		entry = nih_hash_lookup (check_config_data.job_class_hash, job_class);

		if (! entry) {
			nih_error ("%s: %s", _("Invalid job class"), job_class);
			return 1;
		}
	}

	NIH_HASH_FOREACH (check_config_data.job_class_hash, iter) {
		JobCondition *j = (JobCondition *)iter;
		int job_class_displayed = FALSE;

		if (job_class && strcmp (job_class, j->job_class) != 0) {
			/* user specified a job on the command-line,
			 * so only show that one.
			 */
			continue;
		}

		ret += check_condition (j->job_class, "start on", j->start_on,
				&job_class_displayed);

		ret += check_condition (j->job_class, "stop on" , j->stop_on,
				&job_class_displayed);
	}

	nih_free (check_config_data.job_class_hash);
	nih_free (check_config_data.event_hash);
	if (check_config_data.ignored_events_hash)
		nih_free (check_config_data.ignored_events_hash);

	return ret ? 1 : 0;
}

/**
 * notify_disk_writeable_action:
 * @command: NihCommand invoked,
 * @args: command-line arguments.
 *
 * This function is called for the "notify-disk-writeable" command.
 *
 * Returns: command exit status.
 **/
int
notify_disk_writeable_action (NihCommand *command,
		char * const *args)
{
	nih_local NihDBusProxy *upstart = NULL;
	NihError *              err;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	upstart = upstart_open (NULL);
	if (! upstart)
		return 1;

	if (upstart_notify_disk_writeable_sync (NULL, upstart) < 0)
		goto error;

	return 0;

error:
	err = nih_error_get ();
	nih_error ("%s", err->message);
	nih_free (err);

	return 1;
}


/**
 * list_sessions_action:
 * @command: NihCommand invoked,
 * @args: command-line arguments.
 *
 * This function is called for the "list-sessions" command.
 *
 * Unlike other commands, this does not attempt to connect to Upstart.
 *
 * Returns: command exit status.
 **/
int
list_sessions_action (NihCommand *command, char * const *args)
{
	nih_local const char *session_dir = NULL;
	DIR                  *dir;
	struct dirent        *ent;

	nih_assert (command);
	nih_assert (args);
	
	session_dir = get_session_dir ();

	if (! session_dir) {
		nih_error (_("Unable to query session directory"));
		return 1;
	}

	dir = opendir (session_dir);
	if (! dir)
		goto error;

	while ((ent = readdir (dir))) {
		nih_local char  *contents = NULL;
		size_t           len;
		nih_local char  *path = NULL;
		pid_t            pid;
		nih_local char  *name = NULL;
		char            *session;
		char            *p;
		char            *ext;
		char            *file;
		int              all_digits = TRUE;

		file = ent->d_name;

		if (! strcmp (file, ".") || ! strcmp (file, ".."))
			continue;

		ext = p = strchr (file, '.');

		/* No extension */
		if (! ext)
			continue;

		/* Invalid extension */
		if (strcmp (ext, ".session"))
			continue;

		NIH_MUST (nih_strncat (&name, NULL, file, (p - file)));

		for (p = name; p && *p; p++) {
			if (! isdigit (*p)) {
				all_digits = FALSE;
				break;
			}
		}

		/* Invalid name */
		if (! all_digits)
			continue;

		pid = (pid_t) atol (name);

		NIH_MUST (nih_strcat_sprintf (&path, NULL, "%s/%s", session_dir, file));

		if (kill (pid, 0)) {
			nih_info ("%s: %s", _("Ignoring stale session file"), path);
			continue;
		}

		contents = nih_file_read (NULL, path, &len);

		if (! contents)
			continue;

		if (contents[len-1] == '\n')
			contents[len-1] = '\0';

		p = strstr (contents, "UPSTART_SESSION" "=");
		if (p != contents)
			continue;

		session = p + strlen ("UPSTART_SESSION") + 1;

		if (! session || ! *session)
			continue;

		nih_message ("%d %s", (int)pid, session);
	}

	closedir (dir);

	return 0;

error:
	nih_error ("unable to determine sessions");
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

/**
 * job_class_parse_events:
 * @condition_data: type of condition we are parsing (used as an indicator to
 * user) and name of job,
 * @variant_array: pointer to array of variants.
 *
 * The array of variants encodes the event operator tree in 
 * Reverse Polish Notation (RPN).
 *
 * Each variant is itself an array. There are two types:
 *
 * - An "Operator" (array length == 1).
 *
 *   Operators are: "/AND" and "/OR".
 *
 * - An "Event" (array length >= 1).
 *
 *   Each Event comprises a name (array element zero), followed by zero or
 *   more "Event Matches". If the first Event Match is of the form "JOB=name",
 *   or is a single token "name" (crucially not containining "="), then
 *   'name' refers to the job which emitted the event.
 **/
void
job_class_parse_events (const ConditionHandlerData *data, char ** const *variant_array)
{
	char          ** const *variant;
	char                  **arg;
	char                   *token;
	nih_local NihList      *rpn_stack = NULL;
	char                   *name = NULL;
	const char             *stanza_name;
	const char             *job_class_name;

	nih_assert (data);

	stanza_name    = ((ConditionHandlerData *)data)->condition_name;
	job_class_name = ((ConditionHandlerData *)data)->job_class_name;

	if (! variant_array || ! *variant_array || ! **variant_array)
		return;

	STACK_CREATE (rpn_stack);
	STACK_SHOW (rpn_stack);

	for (variant = variant_array; variant && *variant && **variant; variant++, name = NULL) {

		/* token is either the first token beyond the stanza name (ie the event name),
		 * or an operator.
		 */
		token = **variant;

		if (IS_OPERATOR (token)) {
			/* Used to hold result of combining top two stack elements. */
			nih_local char *new_token = NULL;

			nih_local NihList *first  = NULL;
			nih_local NihList *second = NULL;

			if (enumerate_events) {
				/* We only care about operands in this mode. */
				continue;
			}

			if (check_config_mode) {
				/* Save token verbatim. */
				new_token = NIH_MUST (nih_strdup (NULL, token));
				STACK_PUSH_NEW_ELEM (rpn_stack, new_token);
				continue;
			}

			first  = NIH_MUST (nih_list_new (NULL));
			second = NIH_MUST (nih_list_new (NULL));

			/* Found an operator, so pop 2 values off stack,
			 * combine them and push back onto stack.
			 */
			STACK_POP (rpn_stack, first);
			STACK_POP (rpn_stack, second);

			new_token = NIH_MUST (nih_strdup (NULL, ""));
			new_token = NIH_MUST (nih_strcat_sprintf (&new_token,
					NULL,
					"(%s %s %s)",
					((NihListEntry *)second)->str,
					IS_OP_AND (token) ? "and" : "or",
					((NihListEntry *)first)->str));

			STACK_PUSH_NEW_ELEM (rpn_stack, new_token);
		} else {
			/* Save operand token (event or job), add
			 * arguments (job names and env vars) and push
			 * onto stack. If we are enumerating_events,
			 * this records the environment only.
			 */
			nih_local char *element = NULL;
			int i;

			element = NIH_MUST (nih_strdup (NULL,
						enumerate_events ? "" : token));

			/* Handle arguments (job names and env vars). */
			arg = (*variant)+1;

			for (i=0; arg[i] && *arg[i]; i++) {
				if ((enumerate_events || check_config_mode) && IS_JOB_EVENT (token)) {
					if (!name) {
						GET_JOB_NAME (name, i, arg[i]);
						if (name)
							continue;
					}
				}

				if (! check_config_mode) {
					element = NIH_MUST (nih_strcat (&element, NULL, " "));
					element = NIH_MUST (nih_strcat (&element, NULL, arg[i]));
				}
			}

			if (enumerate_events) {
				nih_message ("  %s %s (job:%s%s, env:%s)",
						stanza_name,
						token,
						name ? " " : "",
						name ? name : "",
						element);
			} else {
				if (check_config_mode) {
					element = NIH_MUST (nih_sprintf (NULL, "%s%s%s",
								token,
								name ? " " : "",
								name ? name : ""));
				}
				STACK_PUSH_NEW_ELEM (rpn_stack, element);
			}

		}
	}

	if (enumerate_events)
		return;

	if (check_config_mode) {
		int add = 0;
		JobCondition *entry;

		/* Create job class entry if necessary */
		entry = (JobCondition *)nih_hash_lookup (check_config_data.job_class_hash, job_class_name);

		if (!entry) {
			add = 1;
			MAKE_JOB_CONDITION (check_config_data.job_class_hash, entry, job_class_name);
		}

		/* Unstitch the conditions from the stack and stash them
		 * in the appropriate list for the job in question.
		 */
		NIH_LIST_FOREACH_SAFE (rpn_stack, iter) {
			NihListEntry *node = (NihListEntry *)iter; 
			NihList *l = !strcmp (stanza_name, "start on")
				? entry->start_on
				: entry->stop_on;
			nih_ref (node, l);
			nih_unref (node, rpn_stack);
			nih_list_add_after (l, &node->entry);
		}

		if (add)
			nih_hash_add (check_config_data.job_class_hash, &entry->list);

		return;
	}

	/* Handle case where a single event was specified (there
	 * was no operator to pop the entry off the stack).
	 */
	if (! STACK_EMPTY (rpn_stack)) {
		if (! enumerate_events) {
			/* Our job is done: show the user what we found. */
			nih_message ("  %s %s", stanza_name,
					STACK_PEEK (rpn_stack));
		}
	}
}

/**
 * job_class_show_conditions:
 * @job_class_proxy: D-Bus proxy for job class.
 * @job_class_name: Name of config whose conditions we wish to display.
 *
 * Register D-Bus call-backs to display job classes start on and stop on
 * conditions.
 **/
void
job_class_show_conditions (NihDBusProxy *job_class_proxy, const char *job_class_name)
{
	DBusPendingCall  *pending_call;
	NihError         *err;
	ConditionHandlerData start_data, stop_data;

	nih_assert (job_class_proxy);
	nih_assert (job_class_name);
	
	start_data.condition_name = "start on";
	start_data.job_class_name = job_class_name;

	stop_data.condition_name  = "stop on";
	stop_data.job_class_name  = job_class_name;

	pending_call = job_class_get_start_on (job_class_proxy,
			job_class_condition_handler,
			job_class_condition_err_handler, 
			&start_data,
			NIH_DBUS_TIMEOUT_NEVER);

	if (!pending_call)
		goto error;

	/* wait for completion */
	dbus_pending_call_block (pending_call);
	dbus_pending_call_unref (pending_call);

	pending_call = job_class_get_stop_on (job_class_proxy,
			job_class_condition_handler,
			job_class_condition_err_handler, 
			&stop_data,
			NIH_DBUS_TIMEOUT_NEVER);

	if (!pending_call)
		goto error;

	/* wait for completion */
	dbus_pending_call_block (pending_call);
	dbus_pending_call_unref (pending_call);

	return;

error:
	err = nih_error_get ();
	nih_error ("%s", err->message);
	nih_free (err);
}

/**
 * job_class_show_emits:
 * @parent: parent object,
 * @job_class_proxy: D-Bus proxy for job class,
 * @job_class_name: Name of job class that emits an event.
 *
 * Display events job class emits to user.
 **/
void
job_class_show_emits (const void *parent, NihDBusProxy *job_class_proxy, const char *job_class_name)
{
	NihError         *err;
	nih_local char **job_emits = NULL;

	nih_assert (job_class_proxy);

	if (job_class_get_emits_sync (parent, job_class_proxy, &job_emits) < 0) {
		goto error;
	}

	if (job_emits && *job_emits) {
		char **p = job_emits;
		while (*p) {
			if (check_config_mode) {
				/* Record event for later */
				NihListEntry *node = NIH_MUST (nih_list_entry_new (check_config_data.event_hash));
				node->str = NIH_MUST (nih_strdup (node, *p));
				nih_hash_add_unique (check_config_data.event_hash, &node->entry);
			}
			else {
				nih_message ("  emits %s", *p);
			}
			p++;
		}
	}

	return;

error:
	err = nih_error_get ();
	nih_error ("%s", err->message);
	nih_free (err);
}

/**
 * job_class_condition_handler:
 * @data: data passed via job_class_get_start_on() or job_class_get_stop_on(),
 * @message: D-Bus message received,
 * @value: array of variants generated by D-Bus call we are registering
 * with. 
 *
 * Handler for D-Bus message encoding a job classes "start on" or
 * "stop on" condtions.
 **/
void
job_class_condition_handler (void *data, NihDBusMessage *message, char ** const *value)
{
	job_class_parse_events ((const ConditionHandlerData *)data, value);
}

/**
 * job_class_condition_err_handler:
 *
 * @data data passed via job_class_get_start_on() or job_class_get_stop_on(),
 * @message: D-Bus message received.
 *
 * Error handler for D-Bus message encoding a job classes "start on" or
 * "stop on" conditions.
 **/
void
job_class_condition_err_handler (void *data, NihDBusMessage *message)
{
	/* no remedial action possible */
}


/**
 * eval_expr_tree:
 *
 * @expr: expression to consider (see ExprNode),
 * @stack: stack used to hold evaluated expressions.
 *
 * Evaluate @expr, in the context of @stack, creating ExprNode
 * nodes as necessary.
 *
 * See ExprNode for details of @token.
 **/
void
eval_expr_tree (const char *expr, NihList **stack)
{
	NihList *s;

	nih_assert (stack);

	s = *stack;

	if (IS_OPERATOR (expr)) {
		ExprNode *node, *first, *second;
		NihListEntry *tmp = NULL;
		NihListEntry *le;

		/* make node for operator */
		MAKE_EXPR_NODE (NULL, node, expr);

		/* pop */
		nih_assert (! NIH_LIST_EMPTY (s));
		tmp = (NihListEntry *)nih_list_remove (s->next);

		/* re-parent */
		nih_ref (tmp, node);
		nih_unref (tmp, s);

		first = (ExprNode *)tmp->data;
		nih_assert (first->value != -1);

		/* attach to operator node */
		nih_tree_add (&node->node, &first->node, NIH_TREE_LEFT);


		/* pop */
		nih_assert (! NIH_LIST_EMPTY (s));
		tmp = (NihListEntry *)nih_list_remove (s->next);

		/* re-parent */
		nih_ref (tmp, node);
		nih_unref (tmp, s);

		second = (ExprNode *)tmp->data;
		nih_assert (second->value != -1);

		/* attach to operator node */
		nih_tree_add (&node->node, &second->node, NIH_TREE_RIGHT);


		/* Determine truth value for
		 * this node based on children
		 * and type of operator.
		 */
		if (IS_OP_AND (expr) || check_config_warn)
			node->value = first->value && second->value;
		else
			node->value = first->value || second->value;

		/* Create list entry and hook
		 * node onto it.
		 */
		le = NIH_MUST (nih_new (s, NihListEntry));
		nih_list_init (&le->entry);
		le->data = node;

		/* push operator node */
		nih_list_add_after (s, &le->entry);

	} else {
		char *event = NULL;
		char *job   = NULL;
		NihListEntry *le;
		ExprNode *en;
		int errors = 0;

		le = NIH_MUST (nih_new (s, NihListEntry));
		nih_list_init (&le->entry);

		MAKE_EXPR_NODE (le, en, expr);
		le->data = en;

		event = en->expr;

		/* Determine the type of operand node we
		 * have.
		 */
		job = strchr (en->expr, ' ');

		/* found a job */
		if (job) {
			job++;
			*(job-1) = '\0';

			if (! allow_job (job)) {
				errors++;

				/* remember the error for later */
				en->job_in_error = job;
			}
		}

		/* handle event */
		if (! allow_event (event)) {
			errors++;
			/* remember the error for later */
			en->event_in_error = en->expr;
		}

		/* Determine if this this node is in error (0 means yes) */
		en->value = errors ? 0 : 1;

		nih_list_add_after (s, &le->entry);
	}
}


/**
 * check_condition:
 *
 * @job_class: name of job class,
 * @condition: name of condition,
 * @condition_list: conditions to check.
 * @job_class_displayed: Will be set to TRUE when an error node is found
 * and the job class has been displayed to the user.
 *
 * Evaluate all expression tree nodes in @condition_list looking for
 * errors.
 *
 * Returns error count.
 **/
int
check_condition (const char *job_class, const char *condition, NihList *condition_list,
		int *job_class_displayed)
{
	nih_local NihList *stack  = NULL;
	NihTree           *root   = NULL;
	int                errors = 0;

	nih_assert (job_class);
	nih_assert (condition);

	if (! condition_list || NIH_LIST_EMPTY (condition_list))
		return 0;

	STACK_CREATE (stack);
	STACK_SHOW (stack);

	NIH_LIST_FOREACH (condition_list, iter) {
		NihListEntry *e = (NihListEntry *)iter; 

		eval_expr_tree (e->str, &stack);
	}

	root = &(((ExprNode *)((NihListEntry *)stack->next)->data)->node);

	if (! root)
		/* no conditions found */
		return 0;

	/* Look through the expression tree for nodes in error and
	 * display them.
	 */
	NIH_TREE_FOREACH_PRE_FULL (root, iter, tree_filter, root) {
		ExprNode *e = (ExprNode *)iter;

		if ( e->value != 1 ) {
			errors++;
			if ( *job_class_displayed == FALSE) {
				nih_message ("%s", job_class);
				*job_class_displayed = TRUE;
			}

			display_check_errors (job_class, condition, iter);
			break;
		}
	}
	return errors;
}

/**
 * display_check_errors:
 *
 * @job_class: name of job class,
 * @condition: name of condition,
 * @node: tree node that is in error.
 *
 * Display error details from expression tree nodes
 * that are in error.
 *
 * Note that this function should only be passed operand nodes or the
 * root node.
 **/
void
display_check_errors (const char *job_class, const char *condition, NihTree *node)
{
	nih_assert (job_class);
	nih_assert (node);

	NIH_TREE_FOREACH_POST (node, iter) {
		ExprNode   *expr  = (ExprNode *)iter;
		const char *event = expr->event_in_error;
		const char *job   = expr->job_in_error;

		if (event)
			nih_message ("  %s: %s %s", condition,
				_("unknown event"), event);

		if (job)
			nih_message ("  %s: %s %s", condition,
				_("unknown job"), job);
	}
}


/**
 * tree_filter:
 *
 * @data: node representing root of tree,
 * @node: node to consider.
 *
 * Node filter for NIH_TREE_FOREACH_PRE_FULL that ensures only 
 * operator nodes and the root node are to be considered.
 *
 * Returns FALSE if node should be considered, else TRUE.
 *
 **/
int
tree_filter (void *data, NihTree *node)
{
	NihTree  *root = (NihTree *)data;
	ExprNode *e    = (ExprNode *)node;

	nih_assert (root);
	nih_assert (e);

	if (IS_OPERATOR (e->expr) || node == root)
		return FALSE;

	/* ignore */
	return TRUE;
}


/**
 * allow_job:
 *
 * @job: name of job to check.
 *
 * Returns TRUE if @job is recognized or can be ignored,
 * else FALSE.
 **/
int
allow_job (const char *job)
{
	NihList *found;

	nih_assert (job);

	found = nih_hash_lookup (check_config_data.job_class_hash, job);

	/* The second part of this test ensures we ignore the (unusual)
	 * situation whereby the condition references a
	 * variable (for example an instance).
	 */
	if (!found && job[0] != '$')
		return FALSE;

	return TRUE;
}


/**
 * allow_event:
 *
 * @event: name of event to check.
 *
 * Returns TRUE if @event is recognized or can be ignored,
 * else FALSE.
 **/
int
allow_event (const char *event)
{
	nih_assert (event);

	NIH_HASH_FOREACH (check_config_data.event_hash, iter) {
		NihListEntry *entry = (NihListEntry *)iter;

		/* handles expansion of any globs */
		if (fnmatch (entry->str, event, 0) == 0)
			goto out;
	}

	if (IS_INIT_EVENT (event) ||
		nih_hash_lookup (check_config_data.ignored_events_hash, event))
		goto out;

	return FALSE;

out:
	return TRUE;
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
	{ 0, "user", N_("run in user mode (as used for user sessions)"),
		NULL, NULL, &user_mode, NULL },

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
 * show_config_options:
 *
 * Command-line options accepted for the show-config command.
 **/
NihOption show_config_options[] = {
	{ 'e', "enumerate",
		N_("enumerate list of events and jobs causing job "
		   "created from job config to start/stop"),
	  NULL, NULL, &enumerate_events, NULL },

	NIH_OPTION_LAST
};

/**
 * check_config_options:
 *
 * Command-line options accepted for the check-config command.
 **/
NihOption check_config_options[] = {
	{ 'i', "ignore-events", N_("ignore specified list of events (comma-separated)"),
	  NULL, "EVENT_LIST", NULL, ignored_events_setter },
	{ 'w', "warn", N_("Generate warning for any unreachable events/jobs"),
	  NULL, NULL, &check_config_warn, NULL },
	NIH_OPTION_LAST
};

/**
 * usage_options:
 *
 * Command-line options accepted for the usage command.
 **/
NihOption usage_options[] = {
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

	{ "show-config", N_("[CONF]"),
	  N_("Show emits, start on and stop on details for job configurations."),
	  N_("If CONF specified, show configuration details for single job "
	     "configuration, else show details for all jobs configurations.\n"),
	  NULL, show_config_options, show_config_action },

	{ "check-config", N_("[CONF]"),
	  N_("Check for unreachable jobs/event conditions."),
	  N_("List all jobs and events which cannot be satisfied by "
	     "currently available job configuration files"),
	  NULL, check_config_options, check_config_action },

	{ "usage",  N_("JOB"),
	  N_("Show job usage message if available."),
	  N_("JOB is the name of the job which usage is to be shown.\n" ),
	  NULL, usage_options, usage_action },

	{ "notify-disk-writeable", NULL,
	  N_("Inform Upstart that disk is now writeable."),
	  N_("Run to ensure output from jobs ending before "
			  "disk is writeable are flushed to disk"),
	  NULL, NULL, notify_disk_writeable_action },

	{ "list-sessions", NULL,
	  N_("List all sessions."),
	  N_("Displays list of running Session Init sessions"),
	  NULL, NULL, list_sessions_action },

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
