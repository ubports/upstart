/* upstart
 *
 * job_class.c - job class definition handling
 *
 * Copyright © 2008 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <string.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/hash.h>
#include <nih/logging.h>

#include <nih/dbus.h>

#include "environ.h"
#include "process.h"
#include "job_class.h"
#include "job.h"
#include "event_operator.h"
#include "conf.h"
#include "control.h"


/**
 * JOB_DEFAULT_KILL_TIMEOUT:
 *
 * The default length of time to wait after sending a process the TERM
 * signal before sending the KILL signal if it hasn't terminated.
 **/
#define JOB_DEFAULT_KILL_TIMEOUT 5

/**
 * JOB_DEFAULT_RESPAWN_LIMIT:
 *
 * The default number of times in JOB_DEFAULT_RESPAWN_INTERVAL seconds that
 * we permit a process to respawn before stoping it
 **/
#define JOB_DEFAULT_RESPAWN_LIMIT 10

/**
 * JOB_DEFAULT_RESPAWN_INTERVAL:
 *
 * The default number of seconds before resetting the respawn timer.
 **/
#define JOB_DEFAULT_RESPAWN_INTERVAL 5

/**
 * JOB_DEFAULT_UMASK:
 *
 * The default file creation mark for processes.
 **/
#define JOB_DEFAULT_UMASK 022

/**
 * JOB_DEFAULT_ENVIRONMENT:
 *
 * Environment variables to always copy from our own environment, these
 * can be overriden in the job definition or by events since they have the
 * lowest priority.
 **/
#define JOB_DEFAULT_ENVIRONMENT \
	"PATH",			\
	"TERM"


/* Prototypes for static functions */
static JobClass *job_class_select (const char *name);
static int       job_class_remove (JobClass *class);


/**
 * job_classes:
 *
 * This hash table holds the list of known job classes indexed by their name.
 * Each entry is a JobClass structure; multiple entries with the same name
 * are not permitted.
 **/
NihHash *job_classes = NULL;


/**
 * job_class_interfaces:
 *
 * Interfaces exported by job class objects.
 **/
const static NihDBusInterface *job_class_interfaces[] = {
	NULL
};


/**
 * job_class_init:
 *
 * Initialise the job classes hash table.
 **/
void
job_class_init (void)
{
	if (! job_classes)
		NIH_MUST (job_classes = nih_hash_string_new (NULL, 0));
}


/**
 * job_class_new:
 * @parent: parent of new job class,
 * @name: name of new job class.
 *
 * Allocates and returns a new JobClass structure with the @name given.
 * It will not be automatically added to the job classes table, it is up
 * to the caller to ensure this is done using job_class_register() once
 * the class has been set up.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.
 *
 * Returns: newly allocated JobClass structure or NULL if insufficient memory.
 **/
JobClass *
job_class_new (const void *parent,
	       const char *name)
{
	JobClass *class;
	int       i;

	nih_assert (name != NULL);
	nih_assert (strlen (name) > 0);

	class = nih_new (parent, JobClass);
	if (! class)
		return NULL;

	nih_list_init (&class->entry);

	nih_alloc_set_destructor (class, (NihDestructor)nih_list_destroy);

	class->name = nih_strdup (class, name);
	if (! class->name)
		goto error;

	class->path = nih_dbus_path (class, CONTROL_ROOT, "jobs",
				     class->name, NULL);
	if (! class->path)
		goto error;

	class->instance = NULL;
	nih_list_init (&class->instances);

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

	class->respawn = FALSE;
	class->respawn_limit = JOB_DEFAULT_RESPAWN_LIMIT;
	class->respawn_interval = JOB_DEFAULT_RESPAWN_INTERVAL;

	class->normalexit = NULL;
	class->normalexit_len = 0;

	class->leader = FALSE;
	class->console = CONSOLE_NONE;

	class->umask = JOB_DEFAULT_UMASK;
	class->nice = 0;
	class->oom_adj = 0;

	for (i = 0; i < RLIMIT_NLIMITS; i++)
		class->limits[i] = NULL;

	class->chroot = NULL;
	class->chdir = NULL;

	class->deleted = FALSE;

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
	JobClass *registered;

	nih_assert (class != NULL);

	job_class_init ();

	registered = (JobClass *)nih_hash_lookup (job_classes, class->name);
	if (registered)
		if (! job_class_remove (registered))
			return FALSE;

	registered = job_class_select (class->name);
	nih_assert (registered != NULL);

	return (registered == class ? TRUE : FALSE);
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
	JobClass *registered;

	nih_assert (class != NULL);

	job_class_init ();

	registered = (JobClass *)nih_hash_lookup (job_classes, class->name);
	if (registered == class) {
		if (! job_class_remove (class))
			return FALSE;

		registered = job_class_select (class->name);

		return (registered == class ? FALSE : TRUE);
	}

	return TRUE;
}

/**
 * job_class_select:
 * @name: name of class to select.
 *
 * Selects the best available job class named @name, adds it to the hash
 * table and registers it with all current D-Bus connections.
 *
 * Returns: class selected, which may be NULL if none was available.
 **/
static JobClass *
job_class_select (const char *name)
{
	JobClass *class;

	nih_assert (name != NULL);

	control_init ();

	class = conf_select_job (name);
	if (! class)
		return NULL;

	nih_hash_add (job_classes, &class->entry);

	NIH_LIST_FOREACH (control_conns, iter) {
		NihListEntry   *entry = (NihListEntry *)iter;
		DBusConnection *conn = (DBusConnection *)entry->data;

		job_class_register (class, conn);
	}

	return class;
}

/**
 * job_class_register:
 * @class: class to register,
 * @conn: connection to register for.
 *
 * Register the job @class with the D-Bus connection @conn, using the
 * path set when the class was created.  Since multiple classes with the
 * same name may exist, this should only ever be called with the current
 * class of that name, and job_class_unregister() should be used before
 * registering a new one with the same name.
 **/
void
job_class_register (JobClass       *class,
		    DBusConnection *conn)
{
	nih_assert (class != NULL);
	nih_assert (conn != NULL);

	NIH_MUST (nih_dbus_object_new (class, conn, class->path,
				       job_class_interfaces, class));

	nih_debug ("Registered job %s", class->path);

	NIH_LIST_FOREACH (&class->instances, iter) {
		Job *job = (Job *)iter;

		job_register (job, conn);
	}
}

/**
 * job_class_remove:
 * @class: class to remove.
 *
 * Removes @class from the hash table and unregisters it from all current
 * D-Bus connections.
 *
 * Returns: TRUE if class could be unregistered, FALSE if there are
 * active instances that prevent unregistration.
 **/
static int
job_class_remove (JobClass *class)
{
	nih_assert (class != NULL);

	control_init ();

	if (! NIH_LIST_EMPTY (&class->instances))
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
	nih_assert (NIH_LIST_EMPTY (&class->instances));

	NIH_MUST (dbus_connection_unregister_object_path (conn, class->path));

	nih_debug ("Unregistered job %s", class->path);
}


/**
 * job_class_environment:
 * @parent: parent for new table,
 * @class: job class,
 * @len: pointer to variable to store table length.
 *
 * Constructs an environment table containing the standard environment
 * variables and defined in the job's @class.
 *
 * This table is suitable for storing in @job's env member so that it is
 * used for all processes spawned by the job.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.
 *
 * If @len is not NULL it will be updated to contain the new array length.
 *
 * Returns: new environment table or NULL if insufficient memory.
 **/
char **
job_class_environment (const void *parent,
		       JobClass   *class,
		       size_t     *len)
{
	const char  *builtin[] = { JOB_DEFAULT_ENVIRONMENT, NULL };
	char       **e;
	char       **env;

	nih_assert (class != NULL);

	env = nih_str_array_new (parent);
	if (! env)
		return NULL;
	if (len)
		*len = 0;

	/* Copy the builtin set of environment variables, usually these just
	 * pick up the values from init's own environment.
	 */
	for (e = (char **)builtin; e && *e; e++)
		if (! environ_add (&env, parent, len, TRUE, *e))
			goto error;

	/* Copy the set of environment variables from the job configuration,
	 * these often have values but also often don't and we want them to
	 * override the builtins.
	 */
	for (e = class->env; e && *e; e++)
		if (! environ_add (&env, parent, len, TRUE, *e))
			goto error;

	return env;

error:
	nih_free (env);
	return NULL;
}
