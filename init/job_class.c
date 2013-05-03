/* upstart
 *
 * job_class.c - job class definition handling
 *
 * Copyright  2011 Canonical Ltd.
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
#include <sys/time.h>
#include <sys/resource.h>

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

extern json_object *json_classes;

/* Prototypes for static functions */
static void  job_class_add (JobClass *class);
static int   job_class_remove (JobClass *class, const Session *session);

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
 * job_environ:
 *
 * Array of environment variables that will be set in the jobs
 * environment.
 **/
static char **job_environ = NULL;

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
 * job_class_environ_init:
 *
 * Initialise the job_environ array.
 **/
void
job_class_environment_init (void)
{
	char * const default_environ[] = { JOB_DEFAULT_ENVIRONMENT, NULL };

	if (! job_environ) {
		job_environ = NIH_MUST (nih_str_array_new (NULL));
		NIH_MUST (environ_append (&job_environ, NULL, 0, TRUE, default_environ));
	}
}

/**
 * job_class_environment_reset:
 *
 * Reset the environment back to defaults.
 *
 * Note: not applied to running job instances.
 **/
void
job_class_environment_reset (void)
{
	if (job_environ)
		nih_free (job_environ);

	job_environ = NULL;

	job_class_environment_init ();
}

/**
 * job_class_environment_set:
 *
 * @var: environment variable to set in form 'name[=value]',
 * @replace: TRUE if @name should be overwritten if already set, else
 *  FALSE.
 *
 * Set specified variable in job environment.
 *
 * Returns: 0 on success, -1 on error.
 **/
int
job_class_environment_set (const char *var, int replace)
{
	nih_assert (var);
	nih_assert (job_environ);

	if (! environ_add (&job_environ, NULL, NULL, replace, var))
		return -1;

	/* Update all running jobs */
	NIH_HASH_FOREACH (job_classes, iter) {
		JobClass *class = (JobClass *)iter;

		NIH_HASH_FOREACH (class->instances, job_iter) {
			Job *job = (Job *)job_iter;

			if (! environ_add (&job->env, job, NULL, replace, var))
				return -1;
		}
	}

	return 0;
}

/**
 * job_class_environment_unset:
 *
 * @var: name of environment variable to unset.
 *
 * Remove specified variable from job environment array.
 *
 * Returns: 0 on success, -1 on error.
 **/
int
job_class_environment_unset (const char *name)
{
	nih_assert (name);
	nih_assert (job_environ);

	if (! environ_remove (&job_environ, NULL, NULL, name))
		return -1;

	/* Update all running jobs */
	NIH_HASH_FOREACH (job_classes, iter) {
		JobClass *class = (JobClass *)iter;

		NIH_HASH_FOREACH (class->instances, job_iter) {
			Job *job = (Job *)job_iter;

			if ( ! environ_remove (&job->env, job, NULL, name))
				return -1;
		}
	}

	return 0;
}

/**
 * job_class_environment_get_all:
 *
 * @parent: parent for new environment array.
 *
 * Obtain a copy of the entire environment a job will be provided with.
 *
 * Returns: Newly-allocated copy of the job environment array,
 * or NULL on error.
 **/
char **
job_class_environment_get_all (const void *parent)
{
	nih_assert (job_environ);

	return nih_str_array_copy (parent, NULL, job_environ);
}

/**
 * job_class_environment_get:
 *
 * @name: name of variable to query.
 *
 * Determine value of variable @name in job environment.
 *
 * XXX: The returned value must not be freed.
 *
 * Returns: pointer to static storage value of @name, or NULL if @name
 * does not exist in job environment.
 **/
const char *
job_class_environment_get (const char *name)
{
	nih_assert (name);
	nih_assert (job_environ);

	return environ_get (job_environ, name);
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

	if (class->session && class->session->chroot) {
		class->path = nih_dbus_path (class, DBUS_PATH_UPSTART, "jobs",
					     session->chroot,
					     class->name, NULL);

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
	class->nice = JOB_NICE_INVALID;
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
 * job_class_get_registered:
 *
 * @name: name of JobClass to search for,
 * @session: Session of @class.
 *
 * Determine the currently registered JobClass with name @name for
 * session @session.
 *
 * Returns: JobClass or NULL if no JobClass with name @name and
 * session @session is registered.
 **/
JobClass *
job_class_get_registered (const char *name, Session *session)
{
	JobClass *registered;

	nih_assert (name);

	job_class_init ();

	registered = (JobClass *)nih_hash_search (job_classes, name, NULL);

	/* If we found an entry, ensure we only consider the appropriate session */
	while (registered && registered->session != session)
		registered = (JobClass *)nih_hash_search (job_classes, name, &registered->entry);

	return registered;
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
	JobClass           *registered = NULL;
	JobClass           *best = NULL;

	nih_assert (class != NULL);

	job_class_init ();

	best = conf_select_job (class->name, class->session);
	nih_assert (best != NULL);
	nih_assert (best->session == class->session);

	registered = job_class_get_registered (class->name, class->session);

	if (registered != best) {
		if (registered) {
			job_class_event_block (NULL, registered, best);

			if (! job_class_remove (registered, class->session)) {
				/* Couldn't deregister, so undo */
				if (best->start_on)
					event_operator_reset (best->start_on);
				return FALSE;
			}
		}

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
	JobClass           *registered = NULL;
	JobClass           *best = NULL;

	nih_assert (class != NULL);

	job_class_init ();

	best = conf_select_job (class->name, class->session);

	registered = job_class_get_registered (class->name, class->session);

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
 * job_class_event_block:
 *
 * @parent: parent object for list,
 * @old: original JobClass currently registered in job_classes,
 * @new: new "best" JobClass that is not yet present in job_classes.
 *
 * Compare @old and @new start on EventOperator trees looking for
 * matching events that occur in both (_and_ which implicitly still exist
 * in the global events list). Events that satisfy these criteria will have
 * their reference count elevated to allow @new to replace @old in job_classes
 * without the destruction of @old freeing the events in question.
 *
 * Note that the reference count never needs to be decremented back
 * again since this function effectively passes "ownership" of the event
 * block from @old to @new, since @old will be replaced by @new but @new
 * should replicate the EventOperator state of @old.
 **/
void
job_class_event_block (void *parent, JobClass *old, JobClass *new)
{
	EventOperator  *old_root;
	EventOperator  *new_root;

	if (! old || ! new)
		return;

	old_root = old->start_on;
	new_root = new->start_on;

	/* If either @old or @new are NULL, or have no start_on
	 * condition, there is no need to modify any events.
	 */
	if (! old_root || ! new_root)
		return;

	/* The old JobClass has associated instances meaning it 
	 * will not be possible for job_class_remove() to replace it, so
	 * we don't need to manipulate any event reference counts.
	 */
	NIH_HASH_FOREACH (old->instances, iter)
		return;

	NIH_TREE_FOREACH_POST (&old_root->node, iter) {
		EventOperator  *old_oper = (EventOperator *)iter;
		Event          *event;

		if (old_oper->type != EVENT_MATCH)
			continue;

		/* Ignore nodes that are not blocking events */
		if (! old_oper->event)
			continue;

		/* Since the JobClass is blocking an event,
		 * that event must be valid.
		 */
		event = old_oper->event;

		NIH_TREE_FOREACH_POST (&new_root->node, niter) {
			EventOperator *new_oper = (EventOperator *)niter;

			if (new_oper->type != EVENT_MATCH)
				continue;

			/* ignore the return - we just want to ensure
			 * that any events in @new that match those in
			 * @old have identical nodes.
			 */
			(void)event_operator_handle (new_oper, event, NULL);
		}
	}
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
	char  **env;

	nih_assert (class != NULL);
	nih_assert (job_environ);

	env = nih_str_array_new (parent);
	if (! env)
		return NULL;
	if (len)
		*len = 0;

	/* Copy the set of environment variables, usually these just
	 * pick up the values from init's own environment.
	 */
	if (! environ_append (&env, parent, len, TRUE, job_environ))
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
 * com.ubuntu.Upstart.Error.JobFailed D-Bus error will be returned when the
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
 * job_class_get:
 *
 * @name: name of job class,
 * @session: session of job class.
 *
 * Obtain JobClass with name @name and session @session.
 *
 * Returns: JobClass, or NULL if no matching job class found.
 **/
JobClass *
job_class_get (const char *name, Session *session)
{
	JobClass  *class = NULL;
	NihList   *prev = NULL;

	nih_assert (name);

	job_class_init ();

	do {
		class = (JobClass *)nih_hash_search (job_classes, name, prev);
		if (! class)
			return NULL;
		if (class && class->session == session)
			return class;

		nih_assert (class);

		prev = (NihList *)class;
	} while (TRUE);

	nih_assert_not_reached ();
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
 * Convert @class into a JSON representation for serialisation.
 * Caller must free returned value using json_object_put().
 *
 * Returns: JSON-serialised JobClass object, or NULL on error.
 **/
json_object *
job_class_serialise (const JobClass *class)
{
	json_object      *json;
	json_object      *json_export;
	json_object      *json_emits;
	json_object      *json_processes;
	json_object      *json_normalexit;
	json_object      *json_limits;
	json_object      *json_jobs;
	json_object      *json_start_on;
	json_object      *json_stop_on;
	int               session_index;

	nih_assert (class);
	nih_assert (job_classes);

	json = json_object_new_object ();
	if (! json)
		return NULL;
	
	session_index = session_get_index (class->session);
	if (session_index < 0)
		goto error;

	if (! state_set_json_int_var (json, "session", session_index))
		goto error;

	if (! state_set_json_string_var_from_obj (json, class, name))
		goto error;

	if (! state_set_json_string_var_from_obj (json, class, path))
		goto error;

	if (! state_set_json_string_var_from_obj (json, class, instance))
		goto error;

	json_jobs = job_serialise_all (class->instances);

	if (! json_jobs)
		goto error;

	json_object_object_add (json, "jobs", json_jobs);

	if (! state_set_json_string_var_from_obj (json, class, description))
		goto error;

	if (! state_set_json_string_var_from_obj (json, class, author))
		goto error;

	if (! state_set_json_string_var_from_obj (json, class, version))
		goto error;

	if (! state_set_json_str_array_from_obj (json, class, env))
		goto error;

	json_export = class->export
		? state_serialise_str_array (class->export)
		: json_object_new_array ();

	if (! json_export)
		goto error;
	json_object_object_add (json, "export", json_export);

	if (class->start_on) {
		json_start_on = event_operator_serialise_all (class->start_on);
		if (! json_start_on)
			goto error;

		json_object_object_add (json, "start_on", json_start_on);
	}

	if (class->stop_on) {
		json_stop_on = event_operator_serialise_all (class->stop_on);
		if (! json_stop_on)
			goto error;

		json_object_object_add (json, "stop_on", json_stop_on);
	}

	json_emits = class->emits
		? state_serialise_str_array (class->emits)
		: json_object_new_array ();

	if (! json_emits)
		goto error;
	json_object_object_add (json, "emits", json_emits);

	json_processes = process_serialise_all (
			(const Process const * const * const)class->process);
	if (! json_processes)
		goto error;
	json_object_object_add (json, "process", json_processes);

	if (! state_set_json_enum_var (json,
				job_class_expect_type_enum_to_str,
				"expect", class->expect))
		goto error;

	if (! state_set_json_int_var_from_obj (json, class, task))
		goto error;

	if (! state_set_json_int_var_from_obj (json, class, kill_timeout))
		goto error;

	if (! state_set_json_int_var_from_obj (json, class, kill_signal))
		goto error;

	if (! state_set_json_int_var_from_obj (json, class, respawn))
		goto error;

	if (! state_set_json_int_var_from_obj (json, class, respawn_limit))
		goto error;

	if (! state_set_json_int_var_from_obj (json, class, respawn_interval))
		goto error;

	json_normalexit = state_serialise_int_array (int, class->normalexit,
					     class->normalexit_len);
	if (! json_normalexit)
		goto error;

	json_object_object_add (json, "normalexit", json_normalexit);

	if (! state_set_json_enum_var (json,
				job_class_console_type_enum_to_str,
				"console", class->console))
		goto error;

	if (! state_set_json_int_var_from_obj (json, class, umask))
		goto error;

	if (! state_set_json_int_var_from_obj (json, class, nice))
		goto error;

	if (! state_set_json_int_var_from_obj (json, class, oom_score_adj))
		goto error;

	json_limits = state_rlimit_serialise_all (class->limits);
	if (! json_limits)
		goto error;
	json_object_object_add (json, "limits", json_limits);

	if (! state_set_json_string_var_from_obj (json, class, chroot))
		goto error;

	if (! state_set_json_string_var_from_obj (json, class, chdir))
		goto error;

	if (! state_set_json_string_var_from_obj (json, class, setuid))
		goto error;

	if (! state_set_json_string_var_from_obj (json, class, setgid))
		goto error;

	if (! state_set_json_int_var_from_obj (json, class, deleted))
		goto error;

	if (! state_set_json_int_var_from_obj (json, class, debug))
		goto error;

	if (! state_set_json_string_var_from_obj (json, class, usage))
		goto error;

	return json;

error:
	json_object_put (json);
	return NULL;
}

/**
 * job_class_serialise_all:
 *
 * Convert existing JobClass objects in job classes hash to JSON
 * representation.
 *
 * NOTE: despite its name, this function does not _necessarily_
 * serialise all JobClasses - there may be "best" (ie newer) JobClasses
 * associated with ConfFiles that have not yet replaced the existing
 * entries in the job classes hash if the JobClass has running instances.
 *
 * However, this is academic since although such data is not serialised,
 * after the re-exec conf_reload() is called to recreate these "best"
 * JobClasses. This also has the nice side-effect of ensuring that
 * should jobs get created in the window when Upstart is statefully
 * re-exec'ing, it will always see the newest versions of on-disk files
 * (which is what the user expects).
 *
 * Returns: JSON object containing array of JobClass objects,
 * or NULL on error.
 **/
json_object *
job_class_serialise_all (void)
{
	json_object *json;

	job_class_init ();

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
 * Create JobClass from provided JSON and add to the
 * job classes table.
 *
 * Returns: JobClass object, or NULL on error.
 **/
JobClass *
job_class_deserialise (json_object *json)
{
	json_object    *json_normalexit;
	JobClass       *class = NULL;
	ConfFile       *file = NULL;
	Session        *session;
	int             session_index = -1;
	int             ret;
	nih_local char *name = NULL;
	nih_local char *path = NULL;

	nih_assert (json);
	nih_assert (job_classes);

	if (! state_check_json_type (json, object))
		goto error;

	if (! state_get_json_int_var (json, "session", session_index))
		goto error;

	if (session_index < 0)
		goto error;

	session = session_from_index (session_index);

	if (! state_get_json_string_var_strict (json, "name", NULL, name))
		goto error;

	/* Lookup the ConfFile associated with this class */
	file = conf_file_find (name, session);
	if (! file)
		goto error;

	/* Create the class and associate it with the ConfFile */
	class = file->job = job_class_new (NULL, name, session);
	if (! class)
		goto error;

	/* job_class_new() sets path */
	if (! state_get_json_string_var_strict (json, "path", NULL, path))
		goto error;

	nih_assert (! strcmp (class->path, path));

	/* Discard default instance as we're about to be handed a fresh
	 * string from the JSON.
	 */
	nih_free (class->instance);

	if (! state_get_json_string_var_to_obj (json, class, instance))
		goto error;

	if (! state_get_json_string_var_to_obj (json, class, description))
		goto error;

	if (! state_get_json_string_var_to_obj (json, class, author))
		goto error;

	if (! state_get_json_string_var_to_obj (json, class, version))
		goto error;

	if (! state_get_json_env_array_to_obj (json, class, env))
		goto error;

	if (! state_get_json_env_array_to_obj (json, class, export))
		goto error;

	/* start and stop conditions are optional */
	if (json_object_object_get (json, "start_on")) {

		if (state_check_json_type (json, array)) {
			json_object *json_start_on;

			if (! state_get_json_var_full (json, "start_on", array, json_start_on))
				goto error;

			class->start_on = event_operator_deserialise_all (class, json_start_on);
			if (! class->start_on)
				goto error;
		} else {
			nih_local char *start_on = NULL;

			/* old format (string) */

			if (! state_get_json_string_var_strict (json, "start_on", NULL, start_on))
				goto error;

			if (*start_on) {
				class->start_on = parse_on_simple (class, "start", start_on);
				if (! class->start_on) {
					NihError *err;

					err = nih_error_get ();

					nih_error ("%s %s: %s",
							_("BUG"),
							_("'start on' parse error"),
							err->message);

					nih_free (err);

					goto error;
				}
			}
		}
	}

	if (json_object_object_get (json, "stop_on")) {

		if (state_check_json_type (json, array)) {
			json_object *json_stop_on;

			if (! state_get_json_var_full (json, "stop_on", array, json_stop_on))
				goto error;

			class->stop_on = event_operator_deserialise_all (class, json_stop_on);
			if (! class->stop_on)
				goto error;
		} else {
			nih_local char *stop_on = NULL;

			/* old format (string) */

			if (! state_get_json_string_var_strict (json, "stop_on", NULL, stop_on))
				goto error;

			if (*stop_on) {
				class->stop_on = parse_on_simple (class, "stop", stop_on);
				if (! class->stop_on) {
					NihError *err;

					err = nih_error_get ();

					nih_error ("%s %s: %s",
							_("BUG"),
							_("'stop on' parse error"),
							err->message);

					nih_free (err);

					goto error;
				}
			}
		}
	}

	if (! state_get_json_str_array_to_obj (json, class, emits))
		goto error;

	if (! state_get_json_enum_var (json,
				job_class_expect_type_str_to_enum,
				"expect", class->expect))
		goto error;

	if (! state_get_json_int_var_to_obj (json, class, task))
		goto error;

	if (! state_get_json_int_var_to_obj (json, class, kill_timeout))
		goto error;

	if (! state_get_json_int_var_to_obj (json, class, kill_signal))
		goto error;

	if (! state_get_json_int_var_to_obj (json, class, respawn))
		goto error;

	if (! state_get_json_int_var_to_obj (json, class, respawn_limit))
		goto error;

	if (! state_get_json_int_var_to_obj (json, class, respawn_interval))
		goto error;

	if (! state_get_json_enum_var (json,
				job_class_console_type_str_to_enum,
				"console", class->console))
		goto error;

	if (! state_get_json_int_var_to_obj (json, class, umask))
		goto error;

	if (! state_get_json_int_var_to_obj (json, class, nice))
		goto error;

	if (! state_get_json_int_var_to_obj (json, class, oom_score_adj))
		goto error;

	if (! state_get_json_string_var_to_obj (json, class, chroot))
		goto error;

	if (! state_get_json_string_var_to_obj (json, class, chdir))
		goto error;

	if (! state_get_json_string_var_to_obj (json, class, setuid))
		goto error;

	if (! state_get_json_string_var_to_obj (json, class, setgid))
		goto error;

	if (! state_get_json_int_var_to_obj (json, class, deleted))
		goto error;

	if (! state_get_json_int_var_to_obj (json, class, debug))
		goto error;

	if (! state_get_json_string_var_to_obj (json, class, usage))
		goto error;

	json_normalexit = json_object_object_get (json, "normalexit");
	if (! json_normalexit)
		goto error;

	ret = state_deserialise_int_array (class, json_normalexit,
			int, &class->normalexit, &class->normalexit_len);
	if (ret < 0)
		goto error;

	if (state_rlimit_deserialise_all (json, class, &class->limits) < 0)
		goto error;

	if (process_deserialise_all (json, class->process, class->process) < 0)
		goto error;

	/* Add the class to the job_classes hash */
	job_class_consider (class);

	/* Any jobs must be added after the class is registered
	 * (since you cannot add a job to a partially-created
	 * class).
	 */
	if (job_deserialise_all (class, json) < 0)
		goto error;

	return class;

error:
	if (class)
		nih_free (class);

	return NULL;
}

/**
 * job_class_deserialise_all:
 *
 * @json: root of JSON-serialised state.
 *
 * Convert JSON representation of JobClasses back into JobClass objects.
 *
 * Returns: 0 on success, -1 on error.
 **/
int
job_class_deserialise_all (json_object *json)
{
	JobClass     *class = NULL;

	nih_assert (json);

	job_class_init ();

	json_classes = json_object_object_get (json, "job_classes");

	if (! json_classes)
			goto error;

	if (! state_check_json_type (json_classes, array))
		goto error;

	for (int i = 0; i < json_object_array_length (json_classes); i++) {
		json_object         *json_class;

		json_class = json_object_array_get_idx (json_classes, i);
		if (! json_class)
			goto error;

		if (! state_check_json_type (json_class, object))
			goto error;

		class = job_class_deserialise (json_class);
		if (! class)
			goto error;
	}

	return 0;

error:
	if (class)
		nih_free (class);

	return -1;
}


/**
 * job_class_expect_type_enum_to_str:
 *
 * @expect: ExpectType.
 *
 * Convert ExpectType to a string representation.
 *
 * Returns: string representation of @expect, or NULL if not known.
 **/
const char *
job_class_expect_type_enum_to_str (ExpectType expect)
{
	state_enum_to_str (EXPECT_NONE, expect);
	state_enum_to_str (EXPECT_STOP, expect);
	state_enum_to_str (EXPECT_DAEMON, expect);
	state_enum_to_str (EXPECT_FORK, expect);

	return NULL;
}

/**
 * job_class_expect_type_str_to_enum:
 *
 * @expect: string ExpectType value.
 *
 * Convert @expect back into an enum value.
 *
 * Returns: ExpectType representing @expect, or -1 if not known.
 **/
ExpectType
job_class_expect_type_str_to_enum (const char *expect)
{
	nih_assert (expect);

	state_str_to_enum (EXPECT_NONE, expect);
	state_str_to_enum (EXPECT_STOP, expect);
	state_str_to_enum (EXPECT_DAEMON, expect);
	state_str_to_enum (EXPECT_FORK, expect);

	return -1;
}

/**
 * job_class_console_type_enum_to_str:
 *
 * @console: ConsoleType.
 *
 * Convert ConsoleType to a string representation.
 *
 * Returns: string representation of @console, or NULL if not known.
 **/
const char *
job_class_console_type_enum_to_str (ConsoleType console)
{
	state_enum_to_str (CONSOLE_NONE, console);
	state_enum_to_str (CONSOLE_OUTPUT, console);
	state_enum_to_str (CONSOLE_OWNER, console);
	state_enum_to_str (CONSOLE_LOG, console);

	return NULL;
}

/**
 * job_class_console_type_str_to_enum:
 *
 * @console: string ConsoleType value.
 *
 * Convert @console back into enum value.
 *
 * Returns: ExpectType representing @console, or -1 if not known.
 **/
ConsoleType
job_class_console_type_str_to_enum (const char *console)
{
	if (! console)
		goto error;

	state_str_to_enum (CONSOLE_NONE, console);
	state_str_to_enum (CONSOLE_OUTPUT, console);
	state_str_to_enum (CONSOLE_OWNER, console);
	state_str_to_enum (CONSOLE_LOG, console);

error:
	return -1;
}

/**
 * job_class_prepare_reexec:
 *
 * Prepare for a re-exec by clearing the CLOEXEC bit on all log object
 * file descriptors associated with their parent jobs.
 **/
void
job_class_prepare_reexec (void)
{
	job_class_init ();

	NIH_HASH_FOREACH (job_classes, iter) {
		JobClass *class = (JobClass *)iter;

		NIH_HASH_FOREACH (class->instances, job_iter) {
			Job *job = (Job *)job_iter;

			nih_assert (job->log);

			for (int process = 0; process < PROCESS_LAST; process++) {
				int  fd;
				Log *log;

				log = job->log[process];

				/* No associated job process or logger has detected
				 * remote end of pty has closed.
				 */
				if (! log || ! log->io)
					continue;

				nih_assert (log->io->watch);

				fd = log->io->watch->fd;
				if (fd < 0)
					continue;

				if (state_toggle_cloexec (fd, FALSE) < 0)
					goto error;

				fd = log->fd;
				if (fd < 0)
					continue;

				if (state_toggle_cloexec (fd, FALSE) < 0)
					goto error;
			}
		}
	}

	return;

error:
	nih_warn (_("unable to clear CLOEXEC bit on log fd"));
}

/**
 * job_class_find:
 *
 * @session: session,
 * @name: name of JobClass.
 *
 * Lookup a JobClass by session and name.
 *
 * Returns: JobClass associated with @session, or NULL if not found.
 */
JobClass *
job_class_find (const Session *session,
		const char *name)
{
	JobClass *class = NULL;

	nih_assert (name);
	nih_assert (job_classes);

	do {
		class = (JobClass *)nih_hash_search (job_classes,
				name, class ? &class->entry : NULL);
	} while (class && class->session != session);

	return class;
}

/**
 * job_class_max_kill_timeout:
 *
 * Determine maximum kill timeout for all running jobs.
 *
 * Returns: Maximum kill timeout (seconds).
 **/
time_t
job_class_max_kill_timeout (void)
{
	time_t kill_timeout = JOB_DEFAULT_KILL_TIMEOUT;

	job_class_init ();

	NIH_HASH_FOREACH (job_classes, iter) {
		JobClass *class = (JobClass *)iter;

		NIH_HASH_FOREACH (class->instances, job_iter) {
			Job *job = (Job *)job_iter;

			if (job->class->kill_timeout > kill_timeout) {
				kill_timeout = job->class->kill_timeout;
				break;
			}
		}
	}

	return kill_timeout;
}

/**
 * job_class_get_index:
 * @class: JobClass to search for.
 *
 * Returns: index of @class in the job classes hash,
 * or -1 if not found.
 **/
ssize_t
job_class_get_index (const JobClass *class)
{
	ssize_t i = 0;

	nih_assert (class);

	NIH_HASH_FOREACH (job_classes, iter) {
		JobClass *c = (JobClass *)iter;

		if (! strcmp (c->name, class->name)
				&& c->session == class->session)
			return i;
		i++;
	}

	return -1;
}
