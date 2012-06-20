/* upstart
 *
 * job_class.c - job class definition handling
 *
 * Copyright © 2011 Canonical Ltd.
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


#include <errno.h>
#include <string.h>
#include <signal.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/hash.h>
#include <nih/tree.h>
#include <nih/logging.h>

#include <nih-dbus/dbus_error.h>
#include <nih-dbus/dbus_message.h>
#include <nih-dbus/dbus_object.h>
#include <nih-dbus/dbus_util.h>

#include "dbus/upstart.h"

#include "environ.h"
#include "process.h"
#include "session.h"
#include "job_class.h"
#include "job.h"
#include "event_operator.h"
#include "blocked.h"
#include "conf.h"
#include "control.h"
#include "parse_job.h"

#include "com.ubuntu.Upstart.h"
#include "com.ubuntu.Upstart.Job.h"

#include <json.h>

/* Prototypes for static functions */
static void  job_class_add    (JobClass *class);
static int   job_class_remove (JobClass *class, const Session *session);

static char *job_class_collapse_env (char **env)
	__attribute__ ((warn_unused_result));

static char *job_class_collapse_condition (EventOperator *condition)
	__attribute__ ((warn_unused_result));

/**
 * default_console:
 *
 * If a job does not specify a value for the 'console' stanza, use this value.
 *
 * Only used if value is >= 0;
 **/
int default_console = -1;

/**
 * job_classes:
 *
 * This hash table holds the list of known job classes indexed by their name.
 * Each entry is a JobClass structure; multiple entries with the same name
 * are not permitted.
 **/
NihHash *job_classes = NULL;


/**
 * job_class_init:
 *
 * Initialise the job classes hash table.
 **/
void
job_class_init (void)
{
	if (! job_classes)
		job_classes = NIH_MUST (nih_hash_string_new (NULL, 0));
}


/**
 * job_class_new:
 *
 * @parent: parent for new job class,
 * @name: name of new job class,
 * @session: session.
 *
 * Allocates and returns a new JobClass structure with the given @name
 * and @session. It will not be automatically added to the job classes
 * table, it is up to the caller to ensure this is done using
 * job_class_register() once the class has been set up.
 *
 * If @parent is not NULL, it should be a pointer to another object which
 * will be used as a parent for the returned job class.  When all parents
 * of the returned job class are freed, the returned job class will also be
 * freed.
 *
 * Returns: newly allocated JobClass structure or NULL if insufficient memory.
 **/
JobClass *
job_class_new (const void *parent,
	       const char *name,
	       Session    *session)
{
	JobClass *class;
	int       i;

	nih_assert (name != NULL);
	nih_assert (strlen (name) > 0);

	class = nih_new (parent, JobClass);
	if (! class)
		return NULL;

	nih_list_init (&class->entry);

	nih_alloc_set_destructor (class, nih_list_destroy);

	class->name = nih_strdup (class, name);
	if (! class->name)
		goto error;

	class->session = session;
	if (class->session
	    && class->session->chroot
	    && class->session->user) {
		nih_local char *uid = NULL;

		uid = nih_sprintf (NULL, "%d", class->session->user);
		if (! uid)
			goto error;

		class->path = nih_dbus_path (class, DBUS_PATH_UPSTART, "jobs",
					     session->chroot, uid,
					     class->name, NULL);

	} else if (class->session
		   && class->session->chroot) {
		class->path = nih_dbus_path (class, DBUS_PATH_UPSTART, "jobs",
					     session->chroot,
					     class->name, NULL);

	} else if (class->session
		   && class->session->user) {
		nih_local char *uid = NULL;

		uid = nih_sprintf (NULL, "%d", class->session->user);
		if (! uid)
			goto error;

		class->path = nih_dbus_path (class, DBUS_PATH_UPSTART, "jobs",
					     uid, class->name, NULL);

	} else {
		class->path = nih_dbus_path (class, DBUS_PATH_UPSTART, "jobs",
					     class->name, NULL);
	}
	if (! class->path)
		goto error;

	class->instance = nih_strdup (class, "");
	if (! class->instance)
		goto error;

	class->instances = nih_hash_string_new (class, 0);
	if (! class->instances)
		goto error;

	class->description = NULL;
	class->author = NULL;
	class->version = NULL;

	class->env = NULL;
	class->export = NULL;

	class->start_on = NULL;
	class->stop_on = NULL;
	class->emits = NULL;

	class->process = nih_alloc (class, sizeof (Process *) * PROCESS_LAST);
	if (! class->process)
		goto error;

	for (i = 0; i < PROCESS_LAST; i++)
		class->process[i] = NULL;

	class->expect = EXPECT_NONE;
	class->task = FALSE;

	class->kill_timeout = JOB_DEFAULT_KILL_TIMEOUT;
	class->kill_signal = SIGTERM;

	class->respawn = FALSE;
	class->respawn_limit = JOB_DEFAULT_RESPAWN_LIMIT;
	class->respawn_interval = JOB_DEFAULT_RESPAWN_INTERVAL;

	class->normalexit = NULL;
	class->normalexit_len = 0;

	class->console = default_console >= 0 ? default_console : CONSOLE_LOG;

	class->umask = JOB_DEFAULT_UMASK;
	class->nice = JOB_DEFAULT_NICE;
	class->oom_score_adj = JOB_DEFAULT_OOM_SCORE_ADJ;

	for (i = 0; i < RLIMIT_NLIMITS; i++)
		class->limits[i] = NULL;

	class->chroot = NULL;
	class->chdir = NULL;

	class->setuid = NULL;
	class->setgid = NULL;

	class->deleted = FALSE;
	class->debug   = FALSE;

	class->usage = NULL;

	return class;

error:
	nih_free (class);
	return NULL;
}


/**
 * job_class_consider:
 * @class: job class to consider.
 *
 * Considers adding @class to the job classes hash table as the best
 * available class, if there is no existing class with the name or the
 * existing class can be replaced.
 *
 * Returns: TRUE if @class is now the registered class, FALSE otherwise.
 **/
int
job_class_consider (JobClass *class)
{
	JobClass *registered = NULL, *best = NULL;

	nih_assert (class != NULL);

	job_class_init ();

	best = conf_select_job (class->name, class->session);
	nih_assert (best != NULL);
	nih_assert (best->session == class->session);

	registered = (JobClass *)nih_hash_search (job_classes, class->name, NULL);

	/* If we found an entry, ensure we only consider the appropriate session */
	while (registered && registered->session != class->session)
	{
		registered = (JobClass *)nih_hash_search (job_classes, class->name, &registered->entry);
	}

	if (registered != best) {
		if (registered)
			if (! job_class_remove (registered, class->session))
				return FALSE;

		job_class_add (best);
	}

	return (class == best ? TRUE : FALSE);
}

/**
 * job_class_reconsider:
 * @class: job class to reconsider.
 *
 * Reconsiders whether @class should be the best available class in the
 * job classes hash table, if it is the existing class and can be
 * replaced by a better then it will be.
 *
 * Note that the best class may be itself unless you have first removed
 * @class from any configuration sources before calling.
 *
 * Returns: FALSE if @class is still the hash table member, TRUE otherwise.
 **/
int
job_class_reconsider (JobClass *class)
{
	JobClass *registered = NULL, *best = NULL;

	nih_assert (class != NULL);

	job_class_init ();

	best = conf_select_job (class->name, class->session);

	registered = (JobClass *)nih_hash_search (job_classes, class->name, NULL);

	/* If we found an entry, ensure we only consider the appropriate session */
	while (registered && registered->session != class->session)
	{
		registered = (JobClass *)nih_hash_search (job_classes, class->name, &registered->entry);
	}

	if (registered == class) {
		if (class != best) {
			if (! job_class_remove (class, class->session))
				return FALSE;

			job_class_add (best);

			return TRUE;
		} else {
			return FALSE;
		}
	}

	return TRUE;
}

/**
 * job_class_add:
 * @class: new class to select.
 *
 * Adds @class to the hash table and registers it with all current D-Bus
 * connections.  @class may be NULL.
 **/
static void
job_class_add (JobClass *class)
{
	control_init ();

	if (! class)
		return;

	nih_hash_add (job_classes, &class->entry);

	NIH_LIST_FOREACH (control_conns, iter) {
		NihListEntry   *entry = (NihListEntry *)iter;
		DBusConnection *conn = (DBusConnection *)entry->data;

		job_class_register (class, conn, TRUE);
	}
}

/**
 * job_class_remove:
 * @class: class to remove,
 * @session: Session of @class.
 *
 * Removes @class from the hash table and unregisters it from all current
 * D-Bus connections.
 *
 * Returns: TRUE if class could be unregistered, FALSE if there are
 * active instances that prevent unregistration, or if @session
 * does not match the session associated with @class.
 **/
static int
job_class_remove (JobClass *class, const Session *session)
{
	nih_assert (class != NULL);

	if (class->session != session)
		return FALSE;

	control_init ();

	/* Return if we have any active instances */
	NIH_HASH_FOREACH (class->instances, iter)
		return FALSE;

	nih_list_remove (&class->entry);

	NIH_LIST_FOREACH (control_conns, iter) {
		NihListEntry   *entry = (NihListEntry *)iter;
		DBusConnection *conn = (DBusConnection *)entry->data;

		job_class_unregister (class, conn);
	}

	return TRUE;
}

/**
 * job_class_register:
 * @class: class to register,
 * @conn: connection to register for
 * @signal: emit the JobAdded signal.
 *
 * Register the job @class with the D-Bus connection @conn, using the
 * path set when the class was created.  Since multiple classes with the
 * same name may exist, this should only ever be called with the current
 * class of that name, and job_class_unregister() should be used before
 * registering a new one with the same name.
 **/
void
job_class_register (JobClass       *class,
		    DBusConnection *conn,
		    int             signal)
{
	nih_assert (class != NULL);
	nih_assert (conn != NULL);

	NIH_MUST (nih_dbus_object_new (class, conn, class->path,
				       job_class_interfaces, class));

	nih_debug ("Registered job %s", class->path);

	if (signal)
		NIH_ZERO (control_emit_job_added (conn, DBUS_PATH_UPSTART,
						  class->path));

	NIH_HASH_FOREACH (class->instances, iter) {
		Job *job = (Job *)iter;

		job_register (job, conn, signal);
	}
}

/**
 * job_class_unregister:
 * @class: class to unregistered,
 * @conn: connection to unregister from.
 *
 * Unregister the job @class from the D-Bus connection @conn, which must
 * have already been registered with job_class_register().
 **/
void
job_class_unregister (JobClass       *class,
		      DBusConnection *conn)
{
	nih_assert (class != NULL);
	nih_assert (conn != NULL);
	NIH_HASH_FOREACH (class->instances, iter)
		nih_assert_not_reached ();

	NIH_MUST (dbus_connection_unregister_object_path (conn, class->path));

	nih_debug ("Unregistered job %s", class->path);

	NIH_ZERO (control_emit_job_removed (conn, DBUS_PATH_UPSTART,
					    class->path));
}


/**
 * job_class_environment:
 * @parent: parent object for new table,
 * @class: job class,
 * @len: pointer to variable to store table length.
 *
 * Constructs an environment table containing the standard environment
 * variables and defined in the job's @class.
 *
 * This table is suitable for storing in @job's env member so that it is
 * used for all processes spawned by the job.
 *
 * If @len is not NULL it will be updated to contain the new array length.
 *
 * If @parent is not NULL, it should be a pointer to another object which
 * will be used as a parent for the returned array.  When all parents
 * of the returned array are freed, the returned array will also be
 * freed.
 *
 * Returns: new environment table or NULL if insufficient memory.
 **/
char **
job_class_environment (const void *parent,
		       JobClass   *class,
		       size_t     *len)
{
	char * const   builtin[] = { JOB_DEFAULT_ENVIRONMENT, NULL };
	char         **env;

	nih_assert (class != NULL);

	env = nih_str_array_new (parent);
	if (! env)
		return NULL;
	if (len)
		*len = 0;

	/* Copy the builtin set of environment variables, usually these just
	 * pick up the values from init's own environment.
	 */
	if (! environ_append (&env, parent, len, TRUE, builtin))
		goto error;

	/* Copy the set of environment variables from the job configuration,
	 * these often have values but also often don't and we want them to
	 * override the builtins.
	 */
	if (! environ_append (&env, parent, len, TRUE, class->env))
		goto error;

	return env;

error:
	nih_free (env);
	return NULL;
}


/**
 * job_class_get_instance:
 * @class: job class to be query,
 * @message: D-Bus connection and message received,
 * @env: NULL-terminated array of environment variables,
 * @instance: pointer for instance name.
 *
 * Implements the GetInstance method of the com.ubuntu.Upstart.Job
 * interface.
 *
 * Called to obtain the path of an instance based on @env, which is used
 * to locate the instance in the same way that Start, Stop and Restart do.
 *
 * If no such instance is found, the com.ubuntu.Upstart.Error.UnknownInstance
 * D-Bus error will be returned immediately.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
job_class_get_instance (JobClass        *class,
			NihDBusMessage  *message,
			char * const    *env,
			char           **instance)
{
	Job             *job;
	nih_local char **instance_env = NULL;
	nih_local char  *name = NULL;
	size_t           len;

	nih_assert (class != NULL);
	nih_assert (message != NULL);
	nih_assert (env != NULL);

	/* Verify that the environment is valid */
	if (! environ_all_valid (env)) {
		nih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,
					     _("Env must be KEY=VALUE pairs"));
		return -1;
	}

	/* Construct the full environment for the instance based on the class
	 * and that provided.
	 */
	instance_env = job_class_environment (NULL, class, &len);
	if (! instance_env)
		nih_return_system_error (-1);

	if (! environ_append (&instance_env, NULL, &len, TRUE, env))
		nih_return_system_error (-1);

	/* Use the environment to expand the instance name and look it up
	 * in the job.
	 */
	name = environ_expand (NULL, class->instance, instance_env);
	if (! name) {
		NihError *error;
		nih_local char *error_message = NULL;

		error = nih_error_get ();
		if (error->number != ENOMEM) {
			error = nih_error_steal ();
			error_message = nih_strdup (NULL, error->message);
			if (! error_message)
				nih_return_system_error (-1);
			if (class->usage) {
				if (! nih_strcat_sprintf (&error_message, NULL,
							"\n%s: %s", _("Usage"), class->usage)) {
					nih_return_system_error (-1);
				}
			}

			nih_dbus_error_raise (DBUS_ERROR_INVALID_ARGS,
					      error_message);
			nih_free (error);
		}

		return -1;
	}

	job = (Job *)nih_hash_lookup (class->instances, name);

	if (! job) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.UnknownInstance",
			_("Unknown instance: %s"), name);
		return -1;
	}

	*instance = nih_strdup (message, job->path);
	if (! *instance)
		nih_return_system_error (-1);

	return 0;
}

/**
 * job_class_get_instance_by_name:
 * @class: class to obtain instance from,
 * @message: D-Bus connection and message received,
 * @name: name of instance to get,
 * @instance: pointer for object path reply.
 *
 * Implements the GetInstanceByName method of the com.ubuntu.Upstart.Job
 * interface.
 *
 * Called to obtain the path to a D-Bus object for the instance named
 * @name of this job which will be stored in @job.  If no instance with
 * that name exists, the com.ubuntu.Upstart.Error.UnknownInstance D-Bus
 * error will be raised.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
job_class_get_instance_by_name (JobClass        *class,
				NihDBusMessage  *message,
				const char      *name,
				char           **instance)
{
	Job *job;

	nih_assert (class != NULL);
	nih_assert (message != NULL);
	nih_assert (name != NULL);
	nih_assert (instance != NULL);

	job = (Job *)nih_hash_lookup (class->instances, name);
	if (! job) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.UnknownInstance",
			_("Unknown instance: %s"), name);
		return -1;
	}

	*instance = nih_strdup (message, job->path);
	if (! *instance)
		nih_return_system_error (-1);

	return 0;
}

/**
 * job_class_get_all_instances:
 * @class: class to obtain instance from,
 * @message: D-Bus connection and message received,
 * @instances: pointer for array of object paths reply.
 *
 * Implements the GetAllInstances method of the com.ubuntu.Upstart.Job
 * interface.
 *
 * Called to obtain the paths of all instances for the given @class, which
 * will be stored in @instances.  If no instances exist, @instances will
 * point to an empty array.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
job_class_get_all_instances (JobClass         *class,
			     NihDBusMessage   *message,
			     char           ***instances)
{
	char   **list;
	size_t   len;

	nih_assert (class != NULL);
	nih_assert (message != NULL);
	nih_assert (instances != NULL);

	len = 0;
	list = nih_str_array_new (message);
	if (! list)
		nih_return_system_error (-1);

	NIH_HASH_FOREACH (class->instances, iter) {
		Job *job = (Job *)iter;

		if (! nih_str_array_add (&list, message, &len, job->path)) {
			nih_error_raise_system ();
			nih_free (list);
			return -1;
		}
	}

	*instances = list;

	return 0;
}


/**
 * job_class_start:
 * @class: job class to be started,
 * @message: D-Bus connection and message received,
 * @env: NULL-terminated array of environment variables,
 * @wait: whether to wait for command to finish before returning.
 *
 * Implements the top half of the Start method of the com.ubuntu.Upstart.Job
 * interface, the bottom half may be found in job_finished().
 *
 * This is the primary method to start new instances of jobs.  The given
 * @env will be used to locate an existing instance, or create a new one
 * if necessary; in either case, the instance will be set to be started
 * (or restarted if it is currently stopping) with @env as its new
 * environment.
 *
 * If the instance goal is already start,
 * the com.ubuntu.Upstart.Error.AlreadyStarted D-Bus error will be returned
 * immediately.  If the instance fails to start, the
 * com.ubuntu.Upstart.Error.JobFailed D-BUs error will be returned when the
 * problem occurs.
 *
 * When @wait is TRUE the method call will not return until the job has
 * finished starting (running for tasks); when @wait is FALSE, the method
 * call returns once the command has been processed and the goal changed.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
job_class_start (JobClass        *class,
		 NihDBusMessage  *message,
		 char * const    *env,
		 int              wait)
{
	Session         *session;
	Blocked         *blocked = NULL;
	Job             *job;
	nih_local char **start_env = NULL;
	nih_local char  *name = NULL;
	size_t           len;

	nih_assert (class != NULL);
	nih_assert (message != NULL);
	nih_assert (env != NULL);

	/* Don't permit out-of-session modification */
	session = session_from_dbus (NULL, message);
	if (session != class->session) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.PermissionDenied",
			_("You do not have permission to modify job: %s"),
			class->name);
		return -1;
	}

	/* Verify that the environment is valid */
	if (! environ_all_valid (env)) {
		nih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,
					     _("Env must be KEY=VALUE pairs"));
		return -1;
	}

	/* Construct the full environment for the instance based on the class
	 * and that provided.
	 */
	start_env = job_class_environment (NULL, class, &len);
	if (! start_env)
		nih_return_system_error (-1);

	if (! environ_append (&start_env, NULL, &len, TRUE, env))
		nih_return_system_error (-1);

	/* Use the environment to expand the instance name and look it up
	 * in the job.
	 */
	name = environ_expand (NULL, class->instance, start_env);
	if (! name) {
		NihError *error;
		nih_local char *error_message = NULL;

		error = nih_error_get ();
		if (error->number != ENOMEM) {
			error = nih_error_steal ();
			error_message = nih_strdup (NULL, error->message);
			if (! error_message)
				nih_return_system_error (-1);
			if (class->usage) {
				if (! nih_strcat_sprintf (&error_message, NULL,
							"\n%s: %s", _("Usage"), class->usage)) {
					nih_return_system_error (-1);
				}
			}
			nih_dbus_error_raise (DBUS_ERROR_INVALID_ARGS,
					      error_message);
			nih_free (error);
		}

		return -1;
	}

	job = (Job *)nih_hash_lookup (class->instances, name);

	/* If no instance exists with the expanded name, create a new
	 * instance.
	 */
	if (! job) {
		job = job_new (class, name);
		if (! job)
			nih_return_system_error (-1);
	}

	if (job->goal == JOB_START) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.AlreadyStarted",
			_("Job is already running: %s"),
			job_name (job));
		return -1;
	}

	if (wait)
		blocked = NIH_MUST (blocked_new (job, BLOCKED_JOB_START_METHOD,
						 message));

	if (job->start_env)
		nih_unref (job->start_env, job);

	job->start_env = start_env;
	nih_ref (job->start_env, job);

	job_finished (job, FALSE);
	if (blocked)
		nih_list_add (&job->blocking, &blocked->entry);

	job_change_goal (job, JOB_START);

	if (! wait)
		NIH_ZERO (job_class_start_reply (message, job->path));

	return 0;
}

/**
 * job_class_stop:
 * @class: job class to be stopped,
 * @message: D-Bus connection and message received,
 * @env: NULL-terminated array of environment variables,
 * @wait: whether to wait for command to finish before returning.
 *
 * Implements the top half of the Stop method of the com.ubuntu.Upstart.Job
 * interface, the bottom half may be found in job_finished().
 *
 * This is the primary method to stop instances of jobs.  The given @env
 * will be used to locate an existing instance which will be set to be
 * stopped with @env as the environment passed to the pre-stop script.
 *
 * If no such instance is found, the com.ubuntu.Upstart.Error.UnknownInstance
 * D-Bus error will be returned immediately.  If the instance goal is already
 * stop, the com.ubuntu.Upstart.Error.AlreadyStopped D-Bus error will be
 * returned immediately.  If the instance fails to stop, the
 * com.ubuntu.Upstart.Error.JobFailed D-Bus error will be returned when the
 * problem occurs.
 *
 * When @wait is TRUE the method call will not return until the job has
 * finished stopping; when @wait is FALSE, the method call returns once
 * the command has been processed and the goal changed.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
job_class_stop (JobClass       *class,
		NihDBusMessage *message,
		char * const   *env,
		int             wait)
{
	Session         *session;
	Blocked         *blocked = NULL;
	Job             *job;
	nih_local char **stop_env = NULL;
	nih_local char  *name = NULL;
	size_t           len;

	nih_assert (class != NULL);
	nih_assert (message != NULL);
	nih_assert (env != NULL);

	/* Don't permit out-of-session modification */
	session = session_from_dbus (NULL, message);
	if (session != class->session) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.PermissionDenied",
			_("You do not have permission to modify job: %s"),
			class->name);
		return -1;
	}

	/* Verify that the environment is valid */
	if (! environ_all_valid (env)) {
		nih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,
					     _("Env must be KEY=VALUE pairs"));
		return -1;
	}

	/* Construct the full environment for the instance based on the class
	 * and that provided; while we don't pass this to the instance itself,
	 * we need this to look up the instance in the first place.
	 */
	stop_env = job_class_environment (NULL, class, &len);
	if (! stop_env)
		nih_return_system_error (-1);

	if (! environ_append (&stop_env, NULL, &len, TRUE, env))
		nih_return_system_error (-1);

	/* Use the environment to expand the instance name and look it up
	 * in the job.
	 */
	name = environ_expand (NULL, class->instance, stop_env);
	if (! name) {
		NihError *error;

		error = nih_error_get ();
		if (error->number != ENOMEM) {
			error = nih_error_steal ();
			nih_dbus_error_raise (DBUS_ERROR_INVALID_ARGS,
					      error->message);
			nih_free (error);
		}

		return -1;
	}

	job = (Job *)nih_hash_lookup (class->instances, name);

	if (! job) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.UnknownInstance",
			_("Unknown instance: %s"), name);
		return -1;
	}


	if (job->goal == JOB_STOP) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.AlreadyStopped",
			_("Job has already been stopped: %s"),
			job_name (job));

		return -1;
	}

	if (wait)
		blocked = NIH_MUST (blocked_new (job, BLOCKED_JOB_STOP_METHOD,
						 message));

	if (job->stop_env)
		nih_unref (job->stop_env, job);

	job->stop_env = (char **)env;
	nih_ref (job->stop_env, job);

	job_finished (job, FALSE);
	if (blocked)
		nih_list_add (&job->blocking, &blocked->entry);

	job_change_goal (job, JOB_STOP);

	if (! wait)
		NIH_ZERO (job_class_stop_reply (message));

	return 0;
}

/**
 * job_restart:
 * @class: job class to be restarted,
 * @message: D-Bus connection and message received,
 * @env: NULL-terminated array of environment variables,
 * @wait: whether to wait for command to finish before returning.
 *
 * Implements the top half of the Restart method of the com.ubuntu.Upstart.Job
 * interface, the bottom half may be found in job_finished().
 *
 * This is the primary method to restart existing instances of jobs; while
 * calling both "Stop" and "Start" may have the same effect, there is no
 * guarantee of atomicity.
 *
 * The given @env will be used to locate the existing instance, which will
 * be stopped and then restarted with @env as its new environment.
 *
 * If no such instance is found, the com.ubuntu.Upstart.Error.UnknownInstance
 * D-Bus error will be returned immediately.  If the instance goal is already
 * stop, the com.ubuntu.Upstart.Error.AlreadyStopped D-Bus error will be
 * returned immediately.  If the instance fails to restart, the
 * com.ubuntu.Upstart.Error.JobFailed D-Bus error will be returned when the
 * problem occurs.
 *
 * When @wait is TRUE the method call will not return until the job has
 * finished starting again (running for tasks); when @wait is FALSE, the
 * method call returns once the command has been processed and the goal
 * changed.

 * Returns: zero on success, negative value on raised error.
 **/
int
job_class_restart (JobClass        *class,
		   NihDBusMessage  *message,
		   char * const    *env,
		   int              wait)
{
	Session         *session;
	Blocked         *blocked = NULL;
	Job             *job;
	nih_local char **restart_env = NULL;
	nih_local char  *name = NULL;
	size_t           len;

	nih_assert (class != NULL);
	nih_assert (message != NULL);
	nih_assert (env != NULL);

	/* Don't permit out-of-session modification */
	session = session_from_dbus (NULL, message);
	if (session != class->session) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.PermissionDenied",
			_("You do not have permission to modify job: %s"),
			class->name);
		return -1;
	}

	/* Verify that the environment is valid */
	if (! environ_all_valid (env)) {
		nih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,
					     _("Env must be KEY=VALUE pairs"));
		return -1;
	}

	/* Construct the full environment for the instance based on the class
	 * and that provided.
	 */
	restart_env = job_class_environment (NULL, class, &len);
	if (! restart_env)
		nih_return_system_error (-1);

	if (! environ_append (&restart_env, NULL, &len, TRUE, env))
		nih_return_system_error (-1);

	/* Use the environment to expand the instance name and look it up
	 * in the job.
	 */
	name = environ_expand (NULL, class->instance, restart_env);
	if (! name) {
		NihError *error;

		error = nih_error_get ();
		if (error->number != ENOMEM) {
			error = nih_error_steal ();
			nih_dbus_error_raise (DBUS_ERROR_INVALID_ARGS,
					      error->message);
			nih_free (error);
		}

		return -1;
	}

	job = (Job *)nih_hash_lookup (class->instances, name);

	if (! job) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.UnknownInstance",
			_("Unknown instance: %s"), name);
		return -1;
	}


	if (job->goal == JOB_STOP) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.AlreadyStopped",
			_("Job has already been stopped: %s"), job->name);

		return -1;
	}

	if (wait)
		blocked = NIH_MUST (blocked_new (job,
						 BLOCKED_JOB_RESTART_METHOD,
						 message));

	if (job->start_env)
		nih_unref (job->start_env, job);

	job->start_env = restart_env;
	nih_ref (job->start_env, job);

	if (job->stop_env)
		nih_unref (job->stop_env, job);
	job->stop_env = NULL;

	job_finished (job, FALSE);
	if (blocked)
		nih_list_add (&job->blocking, &blocked->entry);

	job_change_goal (job, JOB_STOP);
	job_change_goal (job, JOB_START);

	if (! wait)
		NIH_ZERO (job_class_restart_reply (message, job->path));

	return 0;
}


/**
 * job_class_get_name:
 * @class: class to obtain name from,
 * @message: D-Bus connection and message received,
 * @name: pointer for reply string.
 *
 * Implements the get method for the name property of the
 * com.ubuntu.Upstart.Job interface.
 *
 * Called to obtain the name of the given @class, which will be stored in
 * @name.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
job_class_get_name (JobClass        *class,
		    NihDBusMessage  *message,
		    char           **name)
{
	nih_assert (class != NULL);
	nih_assert (message != NULL);
	nih_assert (name != NULL);

	*name = class->name;
	nih_ref (*name, message);

	return 0;
}

/**
 * job_class_get_description:
 * @class: class to obtain name from,
 * @message: D-Bus connection and message received,
 * @description: pointer for reply string.
 *
 * Implements the get method for the description property of the
 * com.ubuntu.Upstart.Job interface.
 *
 * Called to obtain the description of the given @class, which will be stored
 * in @description.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
job_class_get_description (JobClass        *class,
			   NihDBusMessage  *message,
			   char           **description)
{
	nih_assert (class != NULL);
	nih_assert (message != NULL);
	nih_assert (description != NULL);

	if (class->description) {
		*description = class->description;
		nih_ref (*description, message);
	} else {
		*description = nih_strdup (message, "");
		if (! *description)
			nih_return_no_memory_error (-1);
	}

	return 0;
}

/**
 * job_class_get_author:
 * @class: class to obtain name from,
 * @message: D-Bus connection and message received,
 * @author: pointer for reply string.
 *
 * Implements the get method for the author property of the
 * com.ubuntu.Upstart.Job interface.
 *
 * Called to obtain the author of the given @class, which will be stored
 * in @author.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
job_class_get_author (JobClass        *class,
			   NihDBusMessage  *message,
			   char           **author)
{
	nih_assert (class != NULL);
	nih_assert (message != NULL);
	nih_assert (author != NULL);

	if (class->author) {
		*author = class->author;
		nih_ref (*author, message);
	} else {
		*author = nih_strdup (message, "");
		if (! *author)
			nih_return_no_memory_error (-1);
	}

	return 0;
}

/**
 * job_class_get_version:
 * @class: class to obtain name from,
 * @message: D-Bus connection and message received,
 * @version: pointer for reply string.
 *
 * Implements the get method for the version property of the
 * com.ubuntu.Upstart.Job interface.
 *
 * Called to obtain the version of the given @class, which will be stored
 * in @version.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
job_class_get_version (JobClass        *class,
			   NihDBusMessage  *message,
			   char           **version)
{
	nih_assert (class != NULL);
	nih_assert (message != NULL);
	nih_assert (version != NULL);

	if (class->version) {
		*version = class->version;
		nih_ref (*version, message);
	} else {
		*version = nih_strdup (message, "");
		if (! *version)
			nih_return_no_memory_error (-1);
	}

	return 0;
}


/**
 * job_class_get_start_on:
 * @class: class to obtain events from,
 * @message: D-Bus connection and message received,
 * @start_on: pointer for reply array.
 *
 * Implements the get method for the start_on property of the
 * com.ubuntu.Upstart.Job interface.
 *
 * Called to obtain the set of events that will start jobs of the given
 * @class, this is returned as an array of the event tree flattened into
 * reverse polish form.
 *
 * Each array element is an array of strings representing the events,
 * or a single element containing "/OR" or "/AND" to represent the
 * operators.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
job_class_get_start_on (JobClass *      class,
			NihDBusMessage *message,
			char ****       start_on)
{
	size_t len = 0;

	nih_assert (class != NULL);
	nih_assert (message != NULL);
	nih_assert (start_on != NULL);

	*start_on = nih_alloc (message, sizeof (char ***));
	if (! *start_on)
		nih_return_no_memory_error (-1);

	len = 0;
	(*start_on)[len] = NULL;

	if (class->start_on) {
		NIH_TREE_FOREACH_POST (&class->start_on->node, iter) {
			EventOperator *oper = (EventOperator *)iter;

			*start_on = nih_realloc (*start_on, message,
						 sizeof (char ***) * (len + 2));
			if (! *start_on)
				nih_return_no_memory_error (-1);

			(*start_on)[len] = nih_str_array_new (*start_on);
			if (! (*start_on)[len])
				nih_return_no_memory_error (-1);

			switch (oper->type) {
			case EVENT_OR:
				if (! nih_str_array_add (&(*start_on)[len], *start_on,
							 NULL, "/OR"))
					nih_return_no_memory_error (-1);
				break;
			case EVENT_AND:
				if (! nih_str_array_add (&(*start_on)[len], *start_on,
							 NULL, "/AND"))
					nih_return_no_memory_error (-1);
				break;
			case EVENT_MATCH:
				if (! nih_str_array_add (&(*start_on)[len], *start_on,
							 NULL, oper->name))
					nih_return_no_memory_error (-1);
				if (oper->env)
					if (! nih_str_array_append (&(*start_on)[len], *start_on,
								    NULL, oper->env))
						nih_return_no_memory_error (-1);
				break;
			}

			(*start_on)[++len] = NULL;
		}
	}

	return 0;
}

int
job_class_get_stop_on_str (JobClass  *class,
			   void      *parent,
		       	   char   ****stop_on)
{
	size_t len = 0;

	nih_assert (class != NULL);
	nih_assert (parent != NULL);
	nih_assert (stop_on != NULL);

	*stop_on = nih_alloc (parent, sizeof (char ***));
	if (! *stop_on)
		nih_return_no_memory_error (-1);

	len = 0;
	(*stop_on)[len] = NULL;

	if (class->stop_on) {
		NIH_TREE_FOREACH_POST (&class->stop_on->node, iter) {
			EventOperator *oper = (EventOperator *)iter;

			*stop_on = nih_realloc (*stop_on, parent,
						 sizeof (char ***) * (len + 2));
			if (! *stop_on)
				nih_return_no_memory_error (-1);

			(*stop_on)[len] = nih_str_array_new (*stop_on);
			if (! (*stop_on)[len])
				nih_return_no_memory_error (-1);

			switch (oper->type) {
			case EVENT_OR:
				if (! nih_str_array_add (&(*stop_on)[len], *stop_on,
							 NULL, "/OR"))
					nih_return_no_memory_error (-1);
				break;
			case EVENT_AND:
				if (! nih_str_array_add (&(*stop_on)[len], *stop_on,
							 NULL, "/AND"))
					nih_return_no_memory_error (-1);
				break;
			case EVENT_MATCH:
				if (! nih_str_array_add (&(*stop_on)[len], *stop_on,
							 NULL, oper->name))
					nih_return_no_memory_error (-1);
				if (oper->env)
					if (! nih_str_array_append (&(*stop_on)[len], *stop_on,
								    NULL, oper->env))
						nih_return_no_memory_error (-1);
				break;
			}

			(*stop_on)[++len] = NULL;
		}
	}

	return 0;
}

/**
 * job_class_get_stop_on:
 * @class: class to obtain events from,
 * @message: D-Bus connection and message received,
 * @stop_on: pointer for reply array.
 *
 * Implements the get method for the stop_on property of the
 * com.ubuntu.Upstart.Job interface.
 *
 * Called to obtain the set of events that will stop jobs of the given
 * @class, this is returned as an array of the event tree flattened into
 * reverse polish form.
 *
 * Each array element is an array of strings representing the events,
 * or a single element containing "/OR" or "/AND" to represent the
 * operators.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
job_class_get_stop_on (JobClass *      class,
		       NihDBusMessage *message,
		       char ****       stop_on)
{
	int ret;
#if 0
	size_t len = 0;

	nih_assert (class != NULL);
	nih_assert (message != NULL);
	nih_assert (stop_on != NULL);

	*stop_on = nih_alloc (message, sizeof (char ***));
	if (! *stop_on)
		nih_return_no_memory_error (-1);

	len = 0;
	(*stop_on)[len] = NULL;

	if (class->stop_on) {
		NIH_TREE_FOREACH_POST (&class->stop_on->node, iter) {
			EventOperator *oper = (EventOperator *)iter;

			*stop_on = nih_realloc (*stop_on, message,
						 sizeof (char ***) * (len + 2));
			if (! *stop_on)
				nih_return_no_memory_error (-1);

			(*stop_on)[len] = nih_str_array_new (*stop_on);
			if (! (*stop_on)[len])
				nih_return_no_memory_error (-1);

			switch (oper->type) {
			case EVENT_OR:
				if (! nih_str_array_add (&(*stop_on)[len], *stop_on,
							 NULL, "/OR"))
					nih_return_no_memory_error (-1);
				break;
			case EVENT_AND:
				if (! nih_str_array_add (&(*stop_on)[len], *stop_on,
							 NULL, "/AND"))
					nih_return_no_memory_error (-1);
				break;
			case EVENT_MATCH:
				if (! nih_str_array_add (&(*stop_on)[len], *stop_on,
							 NULL, oper->name))
					nih_return_no_memory_error (-1);
				if (oper->env)
					if (! nih_str_array_append (&(*stop_on)[len], *stop_on,
								    NULL, oper->env))
						nih_return_no_memory_error (-1);
				break;
			}

			(*stop_on)[++len] = NULL;
		}
	}

	return 0;
#endif
	ret = job_class_get_stop_on_str (class, message, stop_on);

#if 0
	{
		char ***s;

		for (s=*stop_on; s && *s; s++) {
			nih_message ("stop_on: '%s'", **s);
		}
	}
#endif
	{
		char ***s;
		char  **c;
		char  **list;
		json_object *json;

		list = nih_str_array_new (message);
		if (! list)
			nih_return_system_error (-1);

		for (s=*stop_on; s && *s; ++s) {
			for (c = *s; c && *c; c++) {
				nih_message ("XXX: c='%s'", *c);
				nih_assert (nih_str_array_add (&list, NULL, NULL, *c));

				json = state_serialize_str_array (c);
				if (!json ) {
					nih_message ("XXX: state_serialize_str_array failed");
					nih_return_no_memory_error (-1);
				}

				nih_message ("YYY: json='%s'", json_object_to_json_string (json));
				json_object_put (json);
			}
		}

		for (c = list; c && *c; ++c) {
			nih_message ("ZZZ: list: '%s'", *c);

		}



	}

	return ret;
}

/**
 * job_class_get_emits:
 * @class: class to obtain events from,
 * @message: D-Bus connection and message received,
 * @emits: pointer for reply array.
 *
 * Implements the get method for the emits property of the
 * com.ubuntu.Upstart.Job interface.
 *
 * Called to obtain the list of additional events of the given @class
 * which will be stored as an array in @emits.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
job_class_get_emits (JobClass *      class,
		     NihDBusMessage *message,
		     char ***        emits)
{
	nih_assert (class != NULL);
	nih_assert (message != NULL);
	nih_assert (emits != NULL);

	if (class->emits) {
		*emits = nih_str_array_copy (message, NULL, class->emits);
		if (! *emits)
			nih_return_no_memory_error (-1);
	} else {
		*emits = nih_str_array_new (message);
		if (! *emits)
			nih_return_no_memory_error (-1);
	}

	return 0;
}

/**
 * job_class_console_type:
 * @console: string representing console type.
 *
 * Returns: ConsoleType equivalent of @string, or -1 on invalid @console.
 **/
ConsoleType
job_class_console_type (const char *console)
{
	if (! strcmp (console, "none")) {
		return CONSOLE_NONE;
	} else if (! strcmp (console, "output")) {
		return CONSOLE_OUTPUT;
	} else if (! strcmp (console, "owner")) {
		return CONSOLE_OWNER;
	} else if (! strcmp (console, "log")) {
		return CONSOLE_LOG;
	}

	return (ConsoleType)-1;
}

/**
 * job_class_get_usage:
 * @class: class to obtain usage from,
 * @message: D-Bus connection and message received,
 * @usage: pointer for reply string.
 *
 * Implements the get method for the usage property of the
 * com.ubuntu.Upstart.Job interface.
 *
 * Called to obtain the usage of the given @class
 * which will be stored as an string in @usage.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
job_class_get_usage (JobClass *      class,
		     NihDBusMessage *message,
		     char **        usage)
{
	nih_assert (class != NULL);
	nih_assert (message != NULL);
	nih_assert (usage != NULL);

	if (class->usage) {
		*usage = nih_strdup (message, class->usage);
		}
	else {
		*usage = nih_strdup (message, "");
	}

	if (! *usage) {
		nih_return_no_memory_error (-1);
	}

	return 0;
}


/**
 * job_class_serialise:
 * @class: job class to serialise.
 *
 * Convert @class int a JSON representation for serialisation.
 * Caller must free returned value using json_object_put().
 *
 * Returns: JSON serialised JobClass object, or NULL on error.
 **/
json_object *
job_class_serialise (const JobClass *class)
{
	json_object      *json;
	json_object      *json_session;
	json_object      *json_env;
	json_object      *json_export;
	json_object      *json_emits;
	json_object      *json_normalexit;
	json_object      *json_start_on;
	json_object      *json_stop_on;
	nih_local char   *start_on = NULL;
	nih_local char   *stop_on = NULL;
	int               session_index;


	/* FIXME: */
#if 0
	json_object  *json_instances;

	json_object  *json_process;
	json_object  *json_limits;
#endif

	nih_assert (class);

	job_class_init ();

	json = json_object_new_object ();
	if (! json)
		return NULL;

	session_index = session_get_index (class->session);
	if (session_index < 0)
		goto error;

	if (! state_set_json_var_full (json, "session", session_index, int, json_session))
		goto error;

	if (! state_set_json_string_var (json, class, name))
		goto error;

	if (! state_set_json_string_var (json, class, path))
		goto error;

	if (! state_set_json_string_var (json, class, instance))
		goto error;

	/* FIXME: instances */

	if (! state_set_json_string_var (json, class, description))
		goto error;

	if (! state_set_json_string_var (json, class, author))
		goto error;

	if (! state_set_json_string_var (json, class, version))
		goto error;

	json_env = class->env
		? state_serialize_str_array (class->env)
		: json_object_new_array ();

	if (! json_env)
		goto error;
	json_object_object_add (json, "env", json_env);

	json_export = class->export
		? state_serialize_str_array (class->export)
		: json_object_new_array ();

	if (! json_export)
		goto error;
	json_object_object_add (json, "export", json_export);

	/* set "start/stop on" in the JSON even if no condition specified.
	 */
	/* FIXME: why bother? */
	start_on = class->start_on
		? job_class_collapse_condition (class->start_on)
		: NIH_MUST (nih_strdup (NULL, ""));
	if (! start_on)
		goto error;

	nih_message ("%s:%d: start_on='%s'", __func__, __LINE__, start_on);

	if (! state_set_json_var_full (json, "start_on", start_on, string, json_start_on)) {
		nih_free (start_on);
		goto error;
	}

	stop_on = class->stop_on
		? job_class_collapse_condition (class->stop_on)
		: NIH_MUST (nih_strdup (NULL, ""));
	if (! stop_on)
		goto error;

	nih_message ("XXX: stop_on='%s'", stop_on);

	if (! state_set_json_var_full (json, "stop_on", stop_on, string, json_stop_on)) {
		nih_free (stop_on);
		goto error;
	}

	json_emits = class->emits
		? state_serialize_str_array (class->emits)
		: json_object_new_array ();

	if (! json_emits)
		goto error;
	json_object_object_add (json, "emits", json_emits);

	/* FIXME: process */

	if (! state_set_json_var (json, class, expect, int))
		goto error;

	if (! state_set_json_var (json, class, task, int))
		goto error;

	if (! state_set_json_var (json, class, kill_timeout, int))
		goto error;

	if (! state_set_json_var (json, class, kill_signal, int))
		goto error;

	if (! state_set_json_var (json, class, respawn, int))
		goto error;

	if (! state_set_json_var (json, class, respawn_limit, int))
		goto error;

	if (! state_set_json_var (json, class, respawn_interval, int))
		goto error;

	json_normalexit = class->normalexit_len
		? state_serialize_int_array (class->normalexit,
					     class->normalexit_len)
		: json_object_new_array ();
	if (! json_normalexit)
		goto error;

	json_object_object_add (json, "normalexit", json_normalexit);

	if (! state_set_json_var (json, class, console, int))
		goto error;

	if (! state_set_json_var (json, class, umask, int))
		goto error;

	if (! state_set_json_var (json, class, nice, int))
		goto error;

	if (! state_set_json_var (json, class, oom_score_adj, int))
		goto error;

	/* FIXME: limits */

	if (! state_set_json_string_var (json, class, chroot))
		goto error;

	if (! state_set_json_string_var (json, class, chdir))
		goto error;

	if (! state_set_json_string_var (json, class, setuid))
		goto error;

	if (! state_set_json_string_var (json, class, setgid))
		goto error;

	if (! state_set_json_var (json, class, deleted, int))
		goto error;

	if (! state_set_json_var (json, class, debug, int))
		goto error;

	if (! state_set_json_string_var (json, class, usage))
		goto error;

	return json;

error:
	json_object_put (json);
	return NULL;
}

/**
 * job_class_serialise_all:
 *
 * Convert existing Session objects to JSON representation.
 *
 * Returns: JSON object containing array of JobClasses, or NULL on error.
 **/
json_object *
job_class_serialise_all (void)
{
	json_object *json;

	job_class_init ();

#if 1
	/* FIXME */
	nih_message ("%s:%d:", __func__, __LINE__);
#endif

	json = json_object_new_array ();
	if (! json)
		return NULL;

	NIH_HASH_FOREACH (job_classes, iter) {
		json_object  *json_class;
		JobClass     *class = (JobClass *)iter;

		json_class = job_class_serialise (class);

		if (! json_class)
			goto error;

		json_object_array_add (json, json_class);
	}

	return json;

error:
	json_object_put (json);
	return NULL;
}

/**
 * job_class_deserialise:
 * @json: JSON-serialised JobClass object to deserialise.
 *
 * Convert @json into a partial JobClass object.
 *
 * Note that @class will only be a partial JobClass since not all
 * structure elements are encoded in the JSON.
 *
 * Returns: Process object, or NULL on error.
 **/
#if 0
int
job_class_deserialise (json_object *json, JobClass *class)
{
	json_object    *json_start_on;
	json_object    *json_stop_on;
	json_object    *json_session;
	json_object    *json_name;
	json_object    *json_path;
	json_object    *json_description;
	json_object    *json_author;
	json_object    *json_version;
	int             session_index;
	const char     *name;
	const char     *path;
	const char     *description;
	const char     *author;
	const char     *version;


	nih_assert (json);
	nih_assert (class);

	if (! state_check_type (json, object))
		goto error;

	if (! state_get_json_string_var (json, "name", json_name, name))
			goto error;
	class->name = NIH_MUST (nih_strdup (class, name));

	if (! state_get_json_string_var (json, "path", json_path, path))
			goto error;
	class->path = NIH_MUST (nih_strdup (class, path));

	if (! state_get_json_string_var (json, "description", json_description, description))
			goto error;
	class->description = NIH_MUST (nih_strdup (class, description));

	if (! state_get_json_string_var (json, "author", json_author, author))
			goto error;
	class->author = NIH_MUST (nih_strdup (class, author));

	if (! state_get_json_string_var (json, "version", json_version, version))
			goto error;
	class->version = NIH_MUST (nih_strdup (class, version));

	/* start and stop conditions are optional */
	if (json_object_object_get (json, "start_on")) {
		const char  *start_on = NULL;

		if (! state_get_json_string_var (json, "start_on", json_start_on, start_on))
			goto error;

		nih_message ("%s:%d: json-parsed     start_on='%s'", __func__, __LINE__, start_on);

		if (*start_on) {
			class->start_on = parse_on_simple (class, "start", start_on);
			if (! class->start_on) {
				NihError *err;

				err = nih_error_get ();

				nih_error ("%s: %s",
						_("BUG: parse error"),
						err->message);

				nih_free (err);

				goto error;
			}

#if 1
			nih_message ("%s:%d: parse_on-parsed start_on='%s'",
					__func__, __LINE__,
					job_class_collapse_condition (class->start_on));
#endif


		}
	}

	if (json_object_object_get (json, "stop_on")) {
		const char  *stop_on = NULL;

		if (! state_get_json_string_var (json, "stop_on", json_stop_on, stop_on))
			goto error;

		nih_message ("%s:%d:stop_on='%s'", __func__, __LINE__, stop_on);

		if (*stop_on) {
			class->stop_on = parse_on_simple (class, "stop", stop_on);
			if (! class->stop_on) {
				NihError *err;

				err = nih_error_get ();

				nih_error ("%s: %s",
						_("BUG: parse error"),
						err->message);

				nih_free (err);

				goto error;
			}

#if 1
			nih_message ("%s:%d: stop_on='%s'",
					__func__, __LINE__,
					job_class_collapse_condition (class->stop_on));
#endif

		}

	}

	if (! state_get_json_simple_var (json, "session", int, json_session, session_index))
			goto error;

	/* can't check return value here (as all values are legitimate) */
	class->session = session_from_index (session_index);

	return 0;

error:
	return -1;
}
#endif

/* FIXME: document */
JobClass *
job_class_deserialise (json_object *json)
{
	json_object    *json_start_on;
	json_object    *json_stop_on;
	json_object    *json_session;
	json_object    *json_name;
	json_object    *json_path;
	json_object    *json_instance;
	json_object    *json_description;
	json_object    *json_author;
	json_object    *json_version;
	const char     *name;
	const char     *path;
	const char     *instance;
	const char     *description;
	const char     *author;
	const char     *version;
	JobClass       *partial;
	int             session_index;

	/* FIXME: TODO:
	 *
	 * instances
	 * env
	 * export
	 * emits
	 * process
	 * expect
	 * etc....
	 */

	nih_assert (json);

	if (! state_check_type (json, object))
		goto error;

	partial = nih_new (NULL, JobClass);
	if (! partial)
		return NULL;

	if (! state_get_json_string_var (json, "name", json_name, name))
			goto error;
	partial->name = NIH_MUST (nih_strdup (partial, name));

	if (! state_get_json_string_var (json, "path", json_path, path))
			goto error;
	partial->path = NIH_MUST (nih_strdup (partial, path));

	if (! state_get_json_string_var (json, "instance", json_instance, instance))
			goto error;
	partial->instance = NIH_MUST (nih_strdup (partial, instance));

	if (! state_get_json_string_var (json, "description", json_description, description))
			goto error;
	partial->description = description && *description
		? NIH_MUST (nih_strdup (partial, description))
		: NULL;

	if (! state_get_json_string_var (json, "author", json_author, author))
			goto error;
	partial->author = NIH_MUST (nih_strdup (partial, author));

	if (! state_get_json_string_var (json, "version", json_version, version))
			goto error;
	partial->version = NIH_MUST (nih_strdup (partial, version));

	/* start and stop conditions are optional */
	if (json_object_object_get (json, "start_on")) {
		const char  *start_on = NULL;

		if (! state_get_json_string_var (json, "start_on", json_start_on, start_on))
			goto error;

		nih_message ("%s:%d: json-parsed     start_on='%s'", __func__, __LINE__, start_on);

		if (*start_on) {
			partial->start_on = parse_on_simple (partial, "start", start_on);
			if (! partial->start_on) {
				NihError *err;

				err = nih_error_get ();

				nih_error ("%s: %s",
						_("BUG: parse error"),
						err->message);

				nih_free (err);

				goto error;
			}

#if 1
			nih_message ("%s:%d: parse_on-parsed start_on='%s'",
					__func__, __LINE__,
					job_class_collapse_condition (partial->start_on));
#endif


		}
	}

	if (json_object_object_get (json, "stop_on")) {
		const char  *stop_on = NULL;

		if (! state_get_json_string_var (json, "stop_on", json_stop_on, stop_on))
			goto error;

		nih_message ("%s:%d:stop_on='%s'", __func__, __LINE__, stop_on);

		if (*stop_on) {
			partial->stop_on = parse_on_simple (partial, "stop", stop_on);
			if (! partial->stop_on) {
				NihError *err;

				err = nih_error_get ();

				nih_error ("%s: %s",
						_("BUG: parse error"),
						err->message);

				nih_free (err);

				goto error;
			}

#if 1
			nih_message ("%s:%d: stop_on='%s'",
					__func__, __LINE__,
					job_class_collapse_condition (partial->stop_on));
#endif

		}

	}

	if (! state_get_json_simple_var (json, "session", int, json_session, session_index))
			goto error;

	/* can't check return value here (as all values are legitimate) */
	partial->session = session_from_index (session_index);

	return partial;

error:
	nih_free (partial);
	return NULL;
}

/**
 * job_class_deserialise_all:
 *
 * @json: root of json serialised state.
 *
 * Convert JSON representation of JobClasses back into JobClass objects.
 *
 * Returns: 0 on success, -1 on error.
 **/
int
job_class_deserialise_all (json_object *json)
{
	json_object         *json_classes;
	JobClass            *class;

	nih_assert (json);

#if 1
	/* FIXME */
	nih_message ("%s:%d:", __func__, __LINE__);
#endif

	json_classes = json_object_object_get (json, "job_classes");

	if (! json_classes)
			goto error;

	if (! state_check_type (json_classes, array))
		goto error;

	for (int i = 0; i < json_object_array_length (json_classes); i++) {
		nih_local JobClass *partial = NULL;
		json_object        *json_class;

		/* FIXME */
		nih_message ("XXX: found job class");

		json_class = json_object_array_get_idx (json_classes, i);
		if (! state_check_type (json_class, object))
			goto error;

		partial = job_class_deserialise (json_class);
		if (! partial)
			goto error;

		/* FIXME */
		nih_message ("class[%d]: name='%s'", i, partial->name);

		class = NIH_MUST (job_class_new (NULL, partial->name, partial->session));

		/* job_class_new() sets path */
		nih_assert (! strcmp (class->path, partial->path));

		/* FIXME: TODO
		 *
		 *   instances
		 *   env
		 *   export
		 *   start_on
		 *   stop_on
		 *   emits
		 *   process
		 *   normalexit
		 *   normalexit_len
		 *   rlimits
		 */
		nih_error ("FIXME: need to finish JobClass deserialisation");

		state_partial_copy_string (class, partial, description);
		state_partial_copy_string (class, partial, author);
		state_partial_copy_string (class, partial, version);
		state_partial_copy_string (class, partial, chroot);
		state_partial_copy_string (class, partial, chdir);
		state_partial_copy_string (class, partial, setuid);
		state_partial_copy_string (class, partial, setgid);
		state_partial_copy_string (class, partial, usage);

		state_partial_copy (class, partial, expect);
		state_partial_copy (class, partial, task);
		state_partial_copy (class, partial, kill_timeout);
		state_partial_copy (class, partial, kill_signal);
		state_partial_copy (class, partial, respawn);
		state_partial_copy (class, partial, respawn_limit);
		state_partial_copy (class, partial, respawn_interval);
		state_partial_copy (class, partial, console);
		state_partial_copy (class, partial, umask);
		state_partial_copy (class, partial, nice);
		state_partial_copy (class, partial, oom_score_adj);
		state_partial_copy (class, partial, deleted);
		state_partial_copy (class, partial, debug);

		/* instance must have a value, but only set it if the
		 * partial value differs from the default set by
		 * job_class_new()
		 */
		/*FIXME: GOT TO HERE: crashing! */
		if (partial->instance && *partial->instance) {
			nih_free (class->instance);

			class->instance = NIH_MUST (nih_strdup (class, partial->instance));
		}
	}

	return 0;

error:
	return -1;
}

/**
 * job_class_collapsed_env:
 *
 * @env: string array.
 *
 * Convert @env into a flattened string, quoting values as required.
 *
 * Returns: newly-allocated flattened string representing @env on
 * success, or NULL on error.
 **/
static char *
job_class_collapse_env (char **env)
{
	char   *p;
	char   *flattened;
	char  **elem;

	nih_assert (env);

	if (! env)
		return NULL;

	/* Start with a string we can append to */
	flattened = NIH_MUST (nih_strdup (NULL, ""));

	for (elem = env; elem && *elem; ++elem) {
		p = strchr (*elem, '=');

		/* If an environment variable contains an equals and whitespace
		 * in the value part, quote the value.
		 */
		if (p && strpbrk (p, " \t")) {
			/* append name and equals */
			NIH_MUST (nih_strcat_sprintf (&flattened, NULL, " %.*s",
						(int)((p - *elem) + 1), *elem));
			p++;

			/* add quoted value */
			if (p) {
				NIH_MUST (nih_strcat_sprintf (&flattened, NULL, "\"%s\"",
							p));
			}
		} else {
			/* either a simple 'name' environment variable,
			 * or a name/value pair without space in the
			 * value part.
			 */
			NIH_MUST (nih_strcat_sprintf (&flattened, NULL, " %s", *elem));
		}

	}

	return flattened;
}

/**
 * job_class_collapse_condition:
 *
 * @condition: start on/stop on condition.
 *
 * Collapsed condition will be fully bracketed. Note that as such it may
 * not be lexicographically identical to the original expression that
 * resulted in @condition, but it will be logically identical.
 *
 * Returns: newly-allocated flattened string representing @condition
 * on success, or NULL on error.
 **/
static char *
job_class_collapse_condition (EventOperator *condition)
{
	/* count of number of closing brackets to insert at end
	 * of tree traversal.
	 */
	int right_parens = 0;

	char *str = NULL;

	nih_assert (condition);

	/* Start with a string we can append to */
	str = NIH_MUST (nih_strdup (NULL, ""));

	NIH_TREE_FOREACH (&condition->node, iter) {
		EventOperator *oper = (EventOperator *)iter;

		switch (oper->type) {
		case EVENT_OR: 
		case EVENT_AND:
			{
				right_parens++;
				NIH_MUST (nih_strcat_sprintf (&str, NULL, " %s ",
							oper->type == EVENT_OR ? "or" : "and"));
				break;
			}
		case EVENT_MATCH:
			{
				char *b, *e;
				char *env = NULL;

				if (! oper->node.parent) {
					/* condition comprises a single event */
					b = e = "";
				} else if (oper->node.parent->left == &oper->node) {
					b = "(";
					e = "";
				} else {
					b = "";
					e = ")";
				}

				if (oper->env)
					env = job_class_collapse_env (oper->env);

#if 0
				nih_message ("XXX: job_class_collapse_env returned: '%s'",
						env ? env : "");

				NIH_MUST (nih_strcat_sprintf (&str, NULL, "%s%s%s%s",
							b, oper->name,
							oper->env ? env : "", e));
#endif
				NIH_MUST (nih_strcat_sprintf (&str, NULL, "%s%s",
							b, oper->name));
				if (env)
					NIH_MUST (nih_strcat (&str, NULL, env));

				NIH_MUST (nih_strcat (&str, NULL, e));

				if (env)
					nih_free (env);
			}
			break;

		}

	}
	right_parens--;

	for (int i = 0; i < right_parens; ++i) {
		NIH_MUST (nih_strcat (&str, NULL, ")"));
	}

	return str;
}
