/* upstart
 *
 * job.c - handling of tasks and services
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


#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/resource.h>

#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/hash.h>
#include <nih/timer.h>
#include <nih/io.h>
#include <nih/logging.h>
#include <nih/error.h>
#include <nih/errors.h>

#include <linux/ptrace.h>

#include "enum.h"
#include "environ.h"
#include "event.h"
#include "process.h"
#include "job.h"
#include "conf.h"
#include "paths.h"
#include "errors.h"


/**
 * SHELL_CHARS:
 *
 * This is the list of characters that, if encountered in a process, cause
 * it to always be run with a shell.
 **/
#define SHELL_CHARS "~`!$^&*()=|\\{}[];\"'<>?"


/* Prototypes for static functions */
static void        job_failed                  (Job *job, ProcessType process,
						int status);
static void        job_unblock                 (Job *job, int failed);
static Event *     job_emit_event              (Job *job)
	__attribute__ ((malloc));
static void        job_kill_timer              (ProcessType process,
						NihTimer *timer);
static void        job_process_terminated      (Job *job, ProcessType process,
						int status);
static int         job_catch_runaway           (Job *job);
static void        job_process_stopped         (Job *job, ProcessType process);
static void        job_process_trace_new       (Job *job, ProcessType process);
static void        job_process_trace_new_child (Job *job, ProcessType process);
static void        job_process_trace_signal    (Job *job, ProcessType process,
						int signum);
static void        job_process_trace_fork      (Job *job, ProcessType process);
static void        job_process_trace_exec      (Job *job, ProcessType process);


/**
 * jobs:
 *
 * This hash table holds the list of known jobs indexed by their name.
 * Each entry is a JobConfig structure; multiple entries with the same name
 * are not permitted.
 **/
NihHash *jobs = NULL;


/**
 * job_init:
 *
 * Initialise the jobs hash table.
 **/
void
job_init (void)
{
	if (! jobs)
		NIH_MUST (jobs = nih_hash_string_new (NULL, 0));
}


/**
 * job_process_new:
 * @parent: parent of new job process.
 *
 * Allocates and returns a new empty JobProcess structure.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.
 *
 * Returns: newly allocated JobProcess structure or NULL if insufficient
 * memory.
 **/
JobProcess *
job_process_new (const void *parent)
{
	JobProcess *process;

	process = nih_new (parent, JobProcess);
	if (! process)
		return NULL;

	process->script = FALSE;
	process->command = NULL;

	return process;
}


/**
 * job_config_new:
 * @parent: parent of new job,
 * @name: name of new job,
 *
 * Allocates and returns a new JobConfig structure with the @name given.
 * It is up to the caller to register it in the hash table and ensure that
 * @name is unique; usually this is done through configuration sources.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.
 *
 * Returns: newly allocated JobConfig structure or NULL if insufficient memory.
 **/
JobConfig *
job_config_new (const void *parent,
		const char *name)
{
	JobConfig *config;
	int        i;

	nih_assert (name != NULL);
	nih_assert (strlen (name) > 0);

	job_init ();

	config = nih_new (parent, JobConfig);
	if (! config)
		return NULL;

	nih_list_init (&config->entry);

	nih_alloc_set_destructor (config, (NihDestructor)nih_list_destroy);

	config->name = nih_strdup (config, name);
	if (! config->name) {
		nih_free (config);
		return NULL;
	}

	config->description = NULL;
	config->author = NULL;
	config->version = NULL;

	config->start_on = NULL;
	config->stop_on = NULL;

	nih_list_init (&config->emits);

	config->process = nih_alloc (config,
				     sizeof (JobProcess *) * PROCESS_LAST);
	if (! config->process) {
		nih_free (config);
		return NULL;
	}

	for (i = 0; i < PROCESS_LAST; i++)
		config->process[i] = NULL;

	config->expect = JOB_EXPECT_NONE;

	config->kill_timeout = JOB_DEFAULT_KILL_TIMEOUT;

	config->task = FALSE;

	config->instance = FALSE;
	config->instance_name = NULL;

	config->respawn = FALSE;
	config->respawn_limit = JOB_DEFAULT_RESPAWN_LIMIT;
	config->respawn_interval = JOB_DEFAULT_RESPAWN_INTERVAL;

	config->normalexit = NULL;
	config->normalexit_len = 0;

	config->leader = FALSE;
	config->console = CONSOLE_NONE;
	config->env = NULL;

	config->umask = JOB_DEFAULT_UMASK;
	config->nice = 0;

	for (i = 0; i < RLIMIT_NLIMITS; i++)
		config->limits[i] = NULL;

	config->chroot = NULL;
	config->chdir = NULL;

	nih_list_init (&config->instances);
	config->deleted = FALSE;

	return config;
}

/**
 * job_config_replace:
 * @config: job config to replace.
 *
 * This function checks whether @config can be replaced (does not have
 * any instances) and if it can, replaces @config in the jobs hash table
 * with the highest priority job with the same name from known configuration
 * sources; this might be the same job.
 *
 * Returns: replacement job, which may be @config or NULL if there was
 * no replacement.
 **/
JobConfig *
job_config_replace (JobConfig *config)
{
	nih_assert (config != NULL);

	if (! NIH_LIST_EMPTY (&config->instances))
		return config;

	nih_list_remove (&config->entry);

	NIH_LIST_FOREACH (conf_sources, iter) {
		ConfSource *source = (ConfSource *)iter;

		if (source->type != CONF_JOB_DIR)
			continue;

		NIH_HASH_FOREACH (source->files, file_iter) {
			ConfFile *file = (ConfFile *)file_iter;

			if (! file->job)
				continue;

			if (! strcmp (file->job->name, config->name)) {
				nih_hash_add (jobs, &file->job->entry);
				return file->job;
			}
		}
	}

	return NULL;
}

/**
 * job_config_environment:
 * @parent: parent for new table,
 * @config: configuration of job,
 * @len: pointer to variable to store table length.
 *
 * Constructs an environment table containing the standard environment
 * variables and defined in the job's @config.
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
job_config_environment (const void *parent,
			JobConfig  *config,
			size_t     *len)
{
	const char  *builtin[] = { JOB_DEFAULT_ENVIRONMENT, NULL };
	char       **e;
	char       **env;

	nih_assert (config != NULL);

	env = nih_str_array_new (parent);
	if (! env)
		return NULL;
	if (len)
		*len = 0;

	/* Copy the builtin set of environment variables, usually these just
	 * pick up the values from init's own environment.
	 */
	for (e = (char **)builtin; e && *e; e++)
		if (! environ_add (&env, parent, len, *e))
			goto error;

	/* Copy the set of environment variables from the job configuration,
	 * these often have values but also often don't and we want them to
	 * override the builtins.
	 */
	for (e = config->env; e && *e; e++)
		if (! environ_add (&env, parent, len, *e))
			goto error;

	return env;

error:
	nih_free (env);
	return NULL;
}


/**
 * job_new:
 * @config: configuration for new job,
 * @name: name for new instance.
 *
 * Allocates and returns a new Job structure for the @config given,
 * appending it to the list of instances for @config.  The returned job
 * will also be an nih_alloc() child of @config.
 *
 * @name is optional and may be NULL, but if given it will be reparented
 * to be an nih_alloc() child of the returned job and should not be modified
 * after the call.
 *
 * Returns: newly allocated job structure or NULL if insufficient memory.
 **/
Job *
job_new (JobConfig *config,
	 char      *name)
{
	Job *job;
	int  i;

	nih_assert (config != NULL);

	job = nih_new (config, Job);
	if (! job)
		return NULL;

	nih_list_init (&job->entry);

	nih_alloc_set_destructor (job, (NihDestructor)nih_list_destroy);

	job->config = config;
	job->name = name;

	job->stop_on = NULL;
	if (config->stop_on) {
		job->stop_on = event_operator_copy (job, config->stop_on);
		if (! job->stop_on)
			goto error;
	}

	job->goal = JOB_STOP;
	job->state = JOB_WAITING;

	job->pid = nih_alloc (job, sizeof (pid_t) * PROCESS_LAST);
	if (! job->pid)
		goto error;

	for (i = 0; i < PROCESS_LAST; i++)
		job->pid[i] = 0;

	job->env = NULL;
	job->start_env = NULL;
	job->stop_env = NULL;

	job->blocked = NULL;
	job->blocking = NULL;

	job->failed = FALSE;
	job->failed_process = -1;
	job->exit_status = 0;

	job->kill_timer = NULL;

	job->respawn_time = 0;
	job->respawn_count = 0;

	job->trace_forks = 0;
	job->trace_state = TRACE_NONE;

	nih_list_add (&config->instances, &job->entry);

	if (job->name)
		nih_alloc_reparent (job->name, job);

	return job;

error:
	nih_free (job);
	return NULL;
}

/**
 * job_name:
 * @job: job to return name of.
 *
 * Returns a string used in messages that contains the job name; this
 * always begins with the name from the configuration, and then if set,
 * has the name of the instance appended in brackets.
 *
 * Returns: internal copy of the string.
 **/
static const char *
job_name (Job *job)
{
	static char *name = NULL;

	nih_assert (job != NULL);

	if (name)
		nih_free (name);

	if (job->name) {
		NIH_MUST (name = nih_sprintf (NULL, "%s (%s)",
					      job->config->name, job->name));
	} else {
		NIH_MUST (name = nih_strdup (NULL, job->config->name));
	}

	return name;
}


/**
 * job_find_by_pid:
 * @pid: process id to find,
 * @process: pointer to place process which is running @pid.
 *
 * Finds the job with a process of the given @pid in the jobs hash table.
 * If @process is not NULL, the @process variable is set to point at the
 * process entry in the table which has @pid.
 *
 * Returns: job found or NULL if not known.
 **/
Job *
job_find_by_pid (pid_t        pid,
		 ProcessType *process)
{
	nih_assert (pid > 0);

	job_init ();

	NIH_HASH_FOREACH (jobs, iter) {
		JobConfig *config = (JobConfig *)iter;

		NIH_LIST_FOREACH (&config->instances, job_iter) {
			Job *job = (Job *)job_iter;
			int  i;

			for (i = 0; i < PROCESS_LAST; i++) {
				if (job->pid[i] == pid) {
					if (process)
						*process = i;

					return job;
				}
			}
		}
	}

	return NULL;
}


/**
 * job_instance:
 * @config: job configuration to look in,
 * @name: name of instance to find.
 *
 * This function is used to find a particular instance of @config.
 *
 * For single-instance jobs, this will always be that instance if active
 * or NULL if not, so a new one will be created.
 *
 * For unlimited-instance jobs, this will always return NULL since a new
 * instance should always be created.
 *
 * For limited-instance jobs, @name must not be NULL and will be looked up
 * in the list of active instances.
 *
 * Returns: existing instance or NULL if a new one should be created.
 **/
Job *
job_instance (JobConfig  *config,
	      const char *name)
{
	nih_assert (config != NULL);

	/* There aren't any instances in the list, always return NULL */
	if (NIH_LIST_EMPTY (&config->instances))
		return NULL;

	/* Not an instance job, always return the first instance */
	if (! config->instance)
		return (Job *)config->instances.next;

	/* Unlimited instance job, always return NULL since they'll want to
	 * spawn a new one.
	 */
	if (! config->instance_name)
		return NULL;

	nih_assert (name != NULL);

	/* Lookup an instance with the name given */
	NIH_LIST_FOREACH (&config->instances, iter) {
		Job *job = (Job *)iter;

		nih_assert (job->name != NULL);

		if (! strcmp (job->name, name))
			return job;
	}

	return NULL;
}


/**
 * job_change_goal:
 * @job: job to change goal of,
 * @goal: goal to change to.
 *
 * This function changes the current goal of a @job to the new @goal given,
 * performing any necessary state changes or actions (such as killing
 * the running process) to correctly enter the new goal.
 *
 * WARNING: On return from this function, @job may no longer be valid
 * since it will be freed once it becomes fully stopped; it may only be
 * called without unexpected side-effects if you are not in the WAITING
 * or RUNNING state and changing the goal to START or STOP respectively.
 **/
void
job_change_goal (Job     *job,
		 JobGoal  goal)
{
	nih_assert (job != NULL);

	if (job->goal == goal)
		return;

	nih_info (_("%s goal changed from %s to %s"),
		  job_name (job), job_goal_name (job->goal),
		  job_goal_name (goal));

	job->goal = goal;


	/* Normally whatever process or event is associated with the state
	 * will finish naturally, so all we need do is change the goal and
	 * we'll change direction through the state machine at that point.
	 *
	 * The exceptions are the natural rest sates of waiting and a
	 * running process; these need induction to get them moving.
	 */
	switch (goal) {
	case JOB_START:
		if (job->state == JOB_WAITING)
			job_change_state (job, job_next_state (job));

		break;
	case JOB_STOP:
		if (job->state == JOB_RUNNING)
			job_change_state (job, job_next_state (job));

		break;
	}
}


/**
 * job_change_state:
 * @job: job to change state of,
 * @state: state to change to.
 *
 * This function changes the current state of a @job to the new @state
 * given, performing any actions to correctly enter the new state (such
 * as spawning scripts or processes).
 *
 * The associated event is also queued by this function.
 *
 * Some state transitions are not be permitted and will result in an
 * assertion failure.  Also some state transitions may result in further
 * transitions, so the state when this function returns may not be the
 * state requested.
 *
 * WARNING: On return from this function, @job may no longer be valid
 * since it will be freed once it becomes fully stopped.
 **/
void
job_change_state (Job      *job,
		  JobState  state)
{
	nih_assert (job != NULL);

	while (job->state != state) {
		JobState old_state;

		nih_assert (job->blocked == NULL);

		nih_info (_("%s state changed from %s to %s"),
			  job_name (job), job_state_name (job->state),
			  job_state_name (state));

		old_state = job->state;
		job->state = state;

		/* Perform whatever action is necessary to enter the new
		 * state, such as executing a process or emitting an event.
		 */
		switch (job->state) {
		case JOB_STARTING:
			nih_assert (job->goal == JOB_START);
			nih_assert ((old_state == JOB_WAITING)
				    || (old_state == JOB_POST_STOP));

			/* Throw away our old environment and use the newly
			 * set environment from now on; unless that's NULL
			 * in which case we just keep our old environment.
			 */
			if (job->start_env) {
				if (job->env)
					nih_free (job->env);

				job->env = job->start_env;
				job->start_env = NULL;
			}

			/* Throw away the stop environment */
			if (job->stop_env) {
				nih_free (job->stop_env);
				job->stop_env = NULL;
			}

			/* Clear any old failed information */
			job->failed = FALSE;
			job->failed_process = -1;
			job->exit_status = 0;

			job->blocked = job_emit_event (job);

			break;
		case JOB_PRE_START:
			nih_assert (job->goal == JOB_START);
			nih_assert (old_state == JOB_STARTING);

			if (job->config->process[PROCESS_PRE_START]) {
				if (job_run_process (job, PROCESS_PRE_START) < 0) {
					job_failed (job, PROCESS_PRE_START, -1);
					job_change_goal (job, JOB_STOP);
					state = job_next_state (job);
				}
			} else {
				state = job_next_state (job);
			}

			break;
		case JOB_SPAWNED:
			nih_assert (job->goal == JOB_START);
			nih_assert (old_state == JOB_PRE_START);

			if (job->config->process[PROCESS_MAIN]) {
				if (job_run_process (job, PROCESS_MAIN) < 0) {
					job_failed (job, PROCESS_MAIN, -1);
					job_change_goal (job, JOB_STOP);
					state = job_next_state (job);
				} else if (job->config->expect == JOB_EXPECT_NONE)
					state = job_next_state (job);
			} else {
				state = job_next_state (job);
			}

			break;
		case JOB_POST_START:
			nih_assert (job->goal == JOB_START);
			nih_assert (old_state == JOB_SPAWNED);

			if (job->config->process[PROCESS_POST_START]) {
				if (job_run_process (job, PROCESS_POST_START) < 0)
					state = job_next_state (job);
			} else {
				state = job_next_state (job);
			}

			break;
		case JOB_RUNNING:
			nih_assert (job->goal == JOB_START);
			nih_assert ((old_state == JOB_POST_START)
				    || (old_state == JOB_PRE_STOP));

			if (old_state == JOB_PRE_STOP) {
				/* Throw away the stop environment */
				if (job->stop_env) {
					nih_free (job->stop_env);
					job->stop_env = NULL;
				}

				/* Cancel the stop attempt */
				job_unblock (job, FALSE);
			} else {
				job_emit_event (job);

				/* If we're not a task, our goal is to be
				 * running.
				 */
				if (! job->config->task)
					job_unblock (job, FALSE);
			}

			break;
		case JOB_PRE_STOP:
			nih_assert (job->goal == JOB_STOP);
			nih_assert (old_state == JOB_RUNNING);

			if (job->config->process[PROCESS_PRE_STOP]) {
				if (job_run_process (job, PROCESS_PRE_STOP) < 0)
					state = job_next_state (job);
			} else {
				state = job_next_state (job);
			}

			break;
		case JOB_STOPPING:
			nih_assert ((old_state == JOB_PRE_START)
				    || (old_state == JOB_SPAWNED)
				    || (old_state == JOB_POST_START)
				    || (old_state == JOB_RUNNING)
				    || (old_state == JOB_PRE_STOP));

			job->blocked = job_emit_event (job);

			break;
		case JOB_KILLED:
			nih_assert (old_state == JOB_STOPPING);

			if (job->config->process[PROCESS_MAIN]
			    && (job->pid[PROCESS_MAIN] > 0)) {
				job_kill_process (job, PROCESS_MAIN);
			} else {
				state = job_next_state (job);
			}

			break;
		case JOB_POST_STOP:
			nih_assert (old_state == JOB_KILLED);

			if (job->config->process[PROCESS_POST_STOP]) {
				if (job_run_process (job, PROCESS_POST_STOP) < 0) {
					job_failed (job, PROCESS_POST_STOP, -1);
					job_change_goal (job, JOB_STOP);
					state = job_next_state (job);
				}
			} else {
				state = job_next_state (job);
			}

			break;
		case JOB_WAITING:
			nih_assert (job->goal == JOB_STOP);
			nih_assert ((old_state == JOB_POST_STOP)
				    || (old_state == JOB_STARTING));

			job_emit_event (job);

			job_unblock (job, FALSE);

			/* Remove the job from the list of instances and
			 * then allow a better configuration to replace us
			 * in the hash table if we have no other instances
			 * and there is one.
			 */
			nih_list_remove (&job->entry);
			job_config_replace (job->config);

			/* If the config is due to be deleted, free it
			 * taking the job with it; otherwise free the
			 * job.
			 */
			if (job->config->deleted) {
				nih_free (job->config);
			} else {
				nih_free (job);
			}

			return;
		}
	}
}

/**
 * job_next_state:
 * @job: job undergoing state change.
 *
 * The next state a job needs to change into is not always obvious as it
 * depends both on the current state and the ultimate goal of the job, ie.
 * whether we're moving towards stop or start.
 *
 * This function contains the logic to decide the next state the job should
 * be in based on the current state and goal.
 *
 * It is up to the caller to ensure the goal is set appropriately before
 * calling this function, for example setting it to JOB_STOP if something
 * failed.  It is also up to the caller to actually set the new state as
 * this simply returns the suggested one.
 *
 * Returns: suggested state to change to.
 **/
JobState
job_next_state (Job *job)
{
	nih_assert (job != NULL);

	switch (job->state) {
	case JOB_WAITING:
		switch (job->goal) {
		case JOB_STOP:
			nih_assert_not_reached ();
		case JOB_START:
			return JOB_STARTING;
		}
	case JOB_STARTING:
		switch (job->goal) {
		case JOB_STOP:
			return JOB_STOPPING;
		case JOB_START:
			return JOB_PRE_START;
		}
	case JOB_PRE_START:
		switch (job->goal) {
		case JOB_STOP:
			return JOB_STOPPING;
		case JOB_START:
			return JOB_SPAWNED;
		}
	case JOB_SPAWNED:
		switch (job->goal) {
		case JOB_STOP:
			return JOB_STOPPING;
		case JOB_START:
			return JOB_POST_START;
		}
	case JOB_POST_START:
		switch (job->goal) {
		case JOB_STOP:
			return JOB_STOPPING;
		case JOB_START:
			return JOB_RUNNING;
		}
	case JOB_RUNNING:
		switch (job->goal) {
		case JOB_STOP:
			if (job->config->process[PROCESS_MAIN]
			    && (job->pid[PROCESS_MAIN] > 0)) {
				return JOB_PRE_STOP;
			} else {
				return JOB_STOPPING;
			}
		case JOB_START:
			return JOB_STOPPING;
		}
	case JOB_PRE_STOP:
		switch (job->goal) {
		case JOB_STOP:
			return JOB_STOPPING;
		case JOB_START:
			return JOB_RUNNING;
		}
	case JOB_STOPPING:
		switch (job->goal) {
		case JOB_STOP:
			return JOB_KILLED;
		case JOB_START:
			return JOB_KILLED;
		}
	case JOB_KILLED:
		switch (job->goal) {
		case JOB_STOP:
			return JOB_POST_STOP;
		case JOB_START:
			return JOB_POST_STOP;
		}
	case JOB_POST_STOP:
		switch (job->goal) {
		case JOB_STOP:
			return JOB_WAITING;
		case JOB_START:
			return JOB_STARTING;
		}
	default:
		nih_assert_not_reached ();
	}
}


/**
 * job_failed:
 * @job: job that has failed,
 * @process: process that failed,
 * @status: status of @process at failure.
 *
 * Mark @job as having failed, unless it already has been marked so, storing
 * @process and @status so that they may show up as arguments and environment
 * to the stop and stopped events generated for the job.
 *
 * Additionally this marks the start and stop events as failed as well; this
 * is reported to the emitted of the event, and also causes a failed event
 * to be generated.
 *
 * @process may be -1 to indicate a failure to respawn, and @exit_status
 * may be -1 to indicate a spawn failure.
 **/
static void
job_failed (Job         *job,
	    ProcessType  process,
	    int          status)
{
	nih_assert (job != NULL);

	if (job->failed)
		return;

	job->failed = TRUE;
	job->failed_process = process;
	job->exit_status = status;

	job_unblock (job, TRUE);
}

/**
 * job_unblock:
 * @job: job that is blocking,
 * @failed: mark events as failed.
 *
 * This function unblocks any events blocking on @job; it is called when the
 * job reaches a rest state (waiting for all, running for services), when a
 * new command is received or when the job fails.
 *
 * If @failed is TRUE then the events that are blocking will be marked as
 * failed.
 **/
static void
job_unblock (Job *job,
	     int  failed)
{
	nih_assert (job != NULL);

	if (job->blocking) {
		NIH_LIST_FOREACH (job->blocking, iter) {
			NihListEntry *entry = (NihListEntry *)iter;
			Event        *event = (Event *)entry->data;

			nih_assert (event != NULL);

			if (failed)
				event->failed = TRUE;

			event_unblock (event);
		}

		nih_free (job->blocking);
		job->blocking = NULL;
	}
}

/**
 * job_emit_event:
 * @job: job generating the event.
 *
 * Called from a state change because it believes an event should be
 * emitted.  Constructs the event with the right arguments and environment
 * and adds it to the pending queue.
 *
 * The stopping and stopped events have an extra argument that is "ok" if
 * the job terminated successfully, or "failed" if it terminated with an
 * error.  If failed, a further argument indicates which process it was
 * that caused the failure and either an EXIT_STATUS or EXIT_SIGNAL
 * environment variable detailing it.
 *
 * Returns: new Event in the queue.
 **/
static Event *
job_emit_event (Job *job)
{
	Event       *event;
	const char  *name;
	int          stop = FALSE;
	char       **env = NULL;
	size_t       len;

	nih_assert (job != NULL);

	switch (job->state) {
	case JOB_STARTING:
		name = JOB_STARTING_EVENT;
		break;
	case JOB_RUNNING:
		name = JOB_STARTED_EVENT;
		break;
	case JOB_STOPPING:
		name = JOB_STOPPING_EVENT;
		stop = TRUE;
		break;
	case JOB_WAITING:
		name = JOB_STOPPED_EVENT;
		stop = TRUE;
		break;
	default:
		nih_assert_not_reached ();
	}

	len = 0;
	NIH_MUST (env = nih_str_array_new (NULL));

	/* Add the job name */
	NIH_MUST (environ_set (&env, NULL, &len, "JOB=%s", job->config->name));

	/* Don't include additional arguments unless this is a stop event. */
	if (! stop)
		goto emit;

	/* Add only a simple "ok" argument if the job didn't fail. */
	if (! job->failed) {
		NIH_MUST (environ_add (&env, NULL, &len, "RESULT=ok"));
		goto emit;
	}

	/* All failure events get a "failed" argument. */
	NIH_MUST (environ_add (&env, NULL, &len, "RESULT=failed"));

	/* Check for respawn failure, which has a special "respawn" argument
	 * and no environment.
	 */
	if (job->failed_process == -1) {
		NIH_MUST (environ_add (&env, NULL, &len, "PROCESS=respawn"));
		goto emit;
	}

	/* All other failure events get the process name as an argument. */
	NIH_MUST (environ_set (&env, NULL, &len, "PROCESS=%s",
			       process_name (job->failed_process)));

	/* Check for spawn failure, which receives no environment */
	if (job->exit_status == -1)
		goto emit;

	/* If the job was terminated by a signal, that will be stored in the
	 * higher byte and we set EXIT_SIGNAL instead of EXIT_STATUS.
	 */
	if (job->exit_status & ~0xff) {
		const char *sig;

		sig = nih_signal_to_name (job->exit_status >> 8);
		if (sig) {
			NIH_MUST (environ_set (&env, NULL, &len,
					       "EXIT_SIGNAL=%s", sig));
		} else {
			NIH_MUST (environ_set (&env, NULL, &len,
					       "EXIT_SIGNAL=%d",
					       job->exit_status >> 8));
		}
	} else {
		NIH_MUST (environ_set (&env, NULL, &len,
				       "EXIT_STATUS=%d", job->exit_status));
	}

emit:
	event = event_new (NULL, name, env);

	return event;
}


/**
 * job_run_process:
 * @job: job context for process to be run in,
 * @process: job process to run.
 *
 * This function looks up @process in the job's process table and uses
 * the information there to spawn a new process for the @job, storing the
 * pid in that table entry.
 *
 * The process is normally executed using the system shell, unless the
 * script member of @process is FALSE and there are no typical shell
 * characters within the command member, in which case it is executed
 * directly using exec after splitting on whitespace.
 *
 * When exectued with the shell, if the command (which may be an entire
 * script) is reasonably small (less than 1KB) it is passed to the
 * shell using the POSIX-specified -c option.  Otherwise the shell is told
 * to read commands from one of the special /dev/fd/NN devices and NihIo
 * used to feed the script into that device.  A pointer to the NihIo object
 * is not kept or stored because it will automatically clean itself up should
 * the script go away as the other end of the pipe will be closed.
 *
 * In either case the shell is run with the -e option so that commands will
 * fail if their exit status is not checked.
 *
 * This function will block until the process_spawn() call succeeds or
 * a non-temporary error occurs (such as file not found).  It is up to the
 * called to decide whether non-temporary errors are a reason to change the
 * job state or not.
 *
 * Returns: zero on success, negative value on non-temporary error.
 **/
int
job_run_process (Job         *job,
		 ProcessType  process)
{
	JobProcess  *proc;
	char       **argv, **env, **e, *script = NULL;
	size_t       argc, envc;
	int          error = FALSE, fds[2], trace = FALSE;

	nih_assert (job != NULL);

	proc = job->config->process[process];
	nih_assert (proc != NULL);
	nih_assert (proc->command != NULL);

	/* We run the process using a shell if it says it wants to be run
	 * as such, or if it contains any shell-like characters; since that's
	 * the best way to deal with things like variables.
	 */
	if ((proc->script) || strpbrk (proc->command, SHELL_CHARS)) {
		struct stat statbuf;

		argc = 0;
		NIH_MUST (argv = nih_str_array_new (NULL));

		NIH_MUST (nih_str_array_add (&argv, NULL, &argc, SHELL));
		NIH_MUST (nih_str_array_add (&argv, NULL, &argc, "-e"));

		/* If the process wasn't originally marked to be run through
		 * a shell, prepend exec to the script so that the shell
		 * gets out of the way after parsing.
		 */
		if (proc->script) {
			NIH_MUST (script = nih_strdup (NULL, proc->command));
		} else {
			NIH_MUST (script = nih_sprintf (NULL, "exec %s",
							proc->command));
		}

		/* If the script is very large, we consider piping it using
		 * /dev/fd/NNN; we can only do that if /dev/fd exists,
		 * of course.
		 */
		if ((strlen (script) > 1024)
		    && (stat (DEV_FD, &statbuf) == 0)
		    && (S_ISDIR (statbuf.st_mode)))
		{
			char *cmd;

			/* Close the writing end when the child is exec'd */
			NIH_MUST (pipe (fds) == 0);
			nih_io_set_cloexec (fds[1]);

			/* FIXME actually always want it to be /dev/fd/3 and
			 * dup2() in the child to make it that way ... no way
			 * of passing that yet
			 */
			NIH_MUST (cmd = nih_sprintf (argv, "%s/%d",
						     DEV_FD, fds[0]));
			NIH_MUST (nih_str_array_addp (&argv, NULL,
						      &argc, cmd));
		} else {
			NIH_MUST (nih_str_array_add (&argv, NULL,
						     &argc, "-c"));
			NIH_MUST (nih_str_array_addp (&argv, NULL,
						      &argc, script));

			/* Next argument is argv[0]; just pass the shell */
			NIH_MUST (nih_str_array_add (&argv, NULL,
						     &argc, SHELL));

			script = NULL;
		}
	} else {
		/* Split the command on whitespace to produce a list of
		 * arguments that we can exec directly.
		 */
		NIH_MUST (argv = nih_str_split (NULL, proc->command,
						" \t\r\n", TRUE));
	}

	/* We provide the standard job environment to all of its processes,
	 * except for pre-stop which also has the stop event environment,
	 * adding special variables that indicate which job it was -- mostly
	 * so that initctl can have clever behaviour when called within them.
	 */
	envc = 0;
	if (job->env) {
		NIH_MUST (env = nih_str_array_copy (NULL, &envc, job->env));
	} else {
		NIH_MUST (env = nih_str_array_new (NULL));
	}

	if ((process == PROCESS_PRE_STOP) && job->stop_env)
		for (e = job->stop_env; *e; e++)
			NIH_MUST (environ_set (&env, NULL, &envc, *e));

	NIH_MUST (environ_set (&env, NULL, &envc,
			       "UPSTART_JOB=%s", job->config->name));
	if (job->name)
		NIH_MUST (environ_set (&env, NULL, &envc,
				       "UPSTART_INSTANCE=%s", job->name));

	/* If we're about to spawn the main job and we expect it to become
	 * a daemon or fork before we can move out of spawned, we need to
	 * set a trace on it.
	 */
	if ((process == PROCESS_MAIN)
	    && ((job->config->expect == JOB_EXPECT_DAEMON)
		|| (job->config->expect == JOB_EXPECT_FORK)))
		trace = TRUE;

	/* Spawn the process, repeat until fork() works */
	while ((job->pid[process] = process_spawn (job->config, argv,
						   env, trace)) < 0) {
		NihError *err;

		err = nih_error_get ();
		if (err->number == PROCESS_ERROR) {
			/* Non-temporary error condition, we're not going
			 * to be able to spawn this process.  Clean up after
			 * ourselves before returning.
			 */
			nih_free (argv);
			nih_free (env);
			if (script) {
				close (fds[0]);
				close (fds[1]);
				nih_free (script);
			}

			job->pid[process] = 0;

			/* Return non-temporary error condition */
			nih_warn (_("Failed to spawn %s %s process: %s"),
				  job_name (job), process_name (process),
				  err->message);
			nih_free (err);
			return -1;
		} else if (! error)
			nih_warn ("%s: %s", _("Temporary process spawn error"),
				  err->message);
		nih_free (err);

		error = TRUE;
	}

	nih_free (argv);
	nih_free (env);

	nih_info (_("%s %s process (%d)"),
		  job_name (job), process_name (process), job->pid[process]);

	job->trace_forks = 0;
	job->trace_state = trace ? TRACE_NEW : TRACE_NONE;

	/* Feed the script to the child process */
	if (script) {
		NihIo *io;

		/* Clean up and close the reading end (we don't need it) */
		close (fds[0]);

		/* Put the entire script into an NihIo send buffer and
		 * then mark it for closure so that the shell gets EOF
		 * and the structure gets cleaned up automatically.
		 */
		while (! (io = nih_io_reopen (job, fds[1], NIH_IO_STREAM,
					      NULL, NULL, NULL, NULL))) {
			NihError *err;

			err = nih_error_get ();
			if (err->number != ENOMEM)
				nih_assert_not_reached ();
			nih_free (err);
		}

		NIH_ZERO (nih_io_write (io, script, strlen (script)));
		nih_io_shutdown (io);

		nih_free (script);
	}

	return 0;
}

/**
 * job_kill_process:
 * @job: job to kill process of,
 * @process: process to be killed.
 *
 * This function forces a @job to leave its current state by sending
 * @process the TERM signal, and maybe later the KILL signal.  The actual
 * state changes are performed by job_child_reaper when the process
 * has actually terminated.
 **/
void
job_kill_process (Job         *job,
		  ProcessType  process)
{
	nih_assert (job != NULL);
	nih_assert (job->pid[process] > 0);

	nih_info (_("Sending TERM signal to %s %s process (%d)"),
		  job_name (job), process_name (process), job->pid[process]);

	if (process_kill (job->config, job->pid[process], FALSE) < 0) {
		NihError *err;

		err = nih_error_get ();
		if (err->number != ESRCH)
			nih_warn (_("Failed to send TERM signal to %s %s process (%d): %s"),
				  job_name (job), process_name (process),
				  job->pid[process], err->message);
		nih_free (err);

		return;
	}

	NIH_MUST (job->kill_timer = nih_timer_add_timeout (
			  job, job->config->kill_timeout,
			  (NihTimerCb)job_kill_timer, (void *)process));
}

/**
 * job_kill_timer:
 * @process: process to be killed,
 * @timer: timer that caused us to be called.
 *
 * This callback is called if the process failed to terminate within
 * a particular time of being sent the TERM signal.  The process is killed
 * more forcibly by sending the KILL signal.
 **/
static void
job_kill_timer (ProcessType  process,
		NihTimer    *timer)
{
	Job *job;

	nih_assert (timer != NULL);
	job = nih_alloc_parent (timer);

	nih_assert (job->pid[process] > 0);

	job->kill_timer = NULL;

	nih_info (_("Sending KILL signal to %s %s process (%d)"),
		  job_name (job), process_name (process), job->pid[process]);

	if (process_kill (job->config, job->pid[process], TRUE) < 0) {
		NihError *err;

		err = nih_error_get ();
		if (err->number != ESRCH)
			nih_warn (_("Failed to send KILL signal to %s %s process (%d): %s"),
				  job_name (job), process_name (process),
				  job->pid[process], err->message);
		nih_free (err);
	}
}


/**
 * job_child_handler:
 * @data: unused,
 * @pid: process that changed,
 * @event: event that occurred on the child,
 * @status: exit status, signal raised or ptrace event.
 *
 * This callback should be registered with nih_child_add_watch() so that
 * when processes associated with jobs die, stop, receive signals or other
 * ptrace events, the appropriate action is taken.
 *
 * Normally this is registered so it is called for all processes, and is
 * safe to do as it only acts if the process is linked to a job.
 **/
void
job_child_handler (void           *data,
		   pid_t           pid,
		   NihChildEvents  event,
		   int             status)
{
	Job         *job;
	ProcessType  process;
	const char  *sig;

	nih_assert (pid > 0);

	/* Find the job that an event ocurred for, and identify which of the
	 * job's process it was.  If we don't know about it, then we simply
	 * ignore the event.
	 */
	job = job_find_by_pid (pid, &process);
	if (! job)
		return;

	switch (event) {
	case NIH_CHILD_EXITED:
		/* Child exited; check status to see whether it exited
		 * normally (zero) or with a non-zero status.
		 */
		if (status) {
			nih_warn (_("%s %s process (%d) "
				    "terminated with status %d"),
				  job_name (job), process_name (process),
				  pid, status);
		} else {
			nih_info (_("%s %s process (%d) exited normally"),
				  job_name (job), process_name (process), pid);
		}

		job_process_terminated (job, process, status);
		break;
	case NIH_CHILD_KILLED:
	case NIH_CHILD_DUMPED:
		/* Child was killed by a signal, and maybe dumped core.  We
		 * store the signal value in the higher byte of status (it's
		 * safe to do that) to distinguish it from a normal exit
		 * status.
		 */
		sig = nih_signal_to_name (status);
		if (sig) {
			nih_warn (_("%s %s process (%d) killed by %s signal"),
				  job_name (job), process_name (process),
				  pid, sig);
		} else {
			nih_warn (_("%s %s process (%d) killed by signal %d"),
				  job_name (job), process_name (process),
				  pid, status);
		}

		status <<= 8;
		job_process_terminated (job, process, status);
		break;
	case NIH_CHILD_STOPPED:
		/* Child was stopped by a signal, make sure it was SIGSTOP
		 * and not a tty-related signal.
		 */
		sig = nih_signal_to_name (status);
		if (sig) {
			nih_warn (_("%s %s process (%d) stopped by %s signal"),
				  job_name (job), process_name (process),
				  pid, sig);
		} else {
			nih_warn (_("%s %s process (%d) stopped by signal %d"),
				  job_name (job), process_name (process),
				  pid, status);
		}

		if (status == SIGSTOP)
			job_process_stopped (job, process);

		break;
	case NIH_CHILD_TRAPPED:
		/* Child received a signal while we were tracing it.  This
		 * can be a signal raised inside the kernel as a side-effect
		 * of the trace because the child called fork() or exec();
		 * we only know that from our own state tracking.
		 */
		if ((job->trace_state == TRACE_NEW)
		    && (status == SIGTRAP)) {
			job_process_trace_new (job, process);
		} else if ((job->trace_state == TRACE_NEW_CHILD)
			   && (status == SIGSTOP)) {
			job_process_trace_new_child (job, process);
		} else {
			job_process_trace_signal (job, process, status);
		}
		break;
	case NIH_CHILD_PTRACE:
		/* Child called an important syscall that can modify the
		 * state of the process trace we hold.
		 */
		switch (status) {
		case PTRACE_EVENT_FORK:
			job_process_trace_fork (job, process);
			break;
		case PTRACE_EVENT_EXEC:
			job_process_trace_exec (job, process);
			break;
		default:
			nih_assert_not_reached ();
		}
		break;
	default:
		nih_assert_not_reached ();
	}

}


/**
 * job_process_terminated:
 * @job: job that changed,
 * @process: specific process,
 * @status: exit status or signal in higher byte.
 *
 * This function is called whenever a @process attached to @job terminates,
 * @status should contain the exit status in the lower byte or signal in
 * the higher byte.
 *
 * The job structure is updated and the next appropriate state for the job
 * is chosen, which may involve changing the goal to stop first.
 **/
static void
job_process_terminated (Job         *job,
			ProcessType  process,
			int          status)
{
	int failed = FALSE, stop = FALSE, state = TRUE;

	nih_assert (job != NULL);

	switch (process) {
	case PROCESS_MAIN:
		nih_assert ((job->state == JOB_RUNNING)
			    || (job->state == JOB_SPAWNED)
			    || (job->state == JOB_KILLED)
			    || (job->state == JOB_STOPPING)
			    || (job->state == JOB_POST_START)
			    || (job->state == JOB_PRE_STOP));

		/* We don't assume that because the primary process was
		 * killed or exited with a non-zero status, it failed.
		 * Instead we check the normalexit list to see whether
		 * the exit signal or status is in that list, and only
		 * if not, do we consider it failed.
		 *
		 * For services that can be respawned, a zero exit status is
		 * also a failure unless listed.
		 *
		 * If the job is already to be stopped, we never consider
		 * it to be failed since we probably caused the termination.
		 */
		if ((job->goal != JOB_STOP)
		    && (status || (job->config->respawn
				   && (! job->config->task))))
		{
			failed = TRUE;
			for (size_t i = 0; i < job->config->normalexit_len; i++) {
				if (job->config->normalexit[i] == status) {
					failed = FALSE;
					break;
				}
			}

			/* We might be able to respawn the failed job;
			 * that's a simple matter of doing nothing.  Check
			 * the job isn't running away first though.
			 */
			if (failed && job->config->respawn) {
				if (job_catch_runaway (job)) {
					nih_warn (_("%s respawning too fast, stopped"),
						  job_name (job));

					failed = FALSE;
					job_failed (job, -1, 0);
				} else {
					nih_warn (_("%s %s process ended, respawning"),
						  job_name (job),
						  process_name (process));
					failed = FALSE;
					break;
				}
			}
		}

		/* We don't change the state if we're in post-start and there's
		 * a post-start process running, or if we're in pre-stop and
		 * there's a pre-stop process running; we wait for those to
		 * finish instead.
		 */
		if ((job->state == JOB_POST_START)
		    && job->config->process[PROCESS_POST_START]
		    && (job->pid[PROCESS_POST_START] > 0)) {
			state = FALSE;
		} else if ((job->state == JOB_PRE_STOP)
		    && job->config->process[PROCESS_PRE_STOP]
		    && (job->pid[PROCESS_PRE_STOP] > 0)) {
			state = FALSE;
		}

		/* Otherwise whether it's failed or not, we should
		 * stop the job now.
		 */
		stop = TRUE;
		break;
	case PROCESS_PRE_START:
		nih_assert (job->state == JOB_PRE_START);

		/* If the pre-start script is killed or exits with a status
		 * other than zero, it's always considered a failure since
		 * we don't know what state the job might be in.
		 */
		if (status) {
			failed = TRUE;
			stop = TRUE;
		}
		break;
	case PROCESS_POST_START:
		nih_assert (job->state == JOB_POST_START);

		/* We always want to change the state when the post-start
		 * script terminates; if the main process is running, we'll
		 * stay in that state, otherwise we'll skip through.
		 *
		 * Failure is ignored since there's not much we can do
		 * about it at this point.
		 */
		break;
	case PROCESS_PRE_STOP:
		nih_assert (job->state == JOB_PRE_STOP);

		/* We always want to change the state when the pre-stop
		 * script terminates, we either want to go back into running
		 * or head towards killing the main process.
		 *
		 * Failure is ignored since there's not much we can do
		 * about it at this point.
		 */
		break;
	case PROCESS_POST_STOP:
		nih_assert (job->state == JOB_POST_STOP);

		/* If the post-stop script is killed or exits with a status
		 * other than zero, it's always considered a failure since
		 * we don't know what state the job might be in.
		 */
		if (status) {
			failed = TRUE;
			stop = TRUE;
		}
		break;
	default:
		nih_assert_not_reached ();
	}


	/* Cancel any timer trying to kill the job, since it's just
	 * died.  We could do this inside the main process block above, but
	 * leaving it here for now means we can use the timer for any
	 * additional process later.
	 */
	if (job->kill_timer) {
		nih_free (job->kill_timer);
		job->kill_timer = NULL;
	}

	/* Clear the process pid field */
	job->pid[process] = 0;


	/* Mark the job as failed */
	if (failed)
		job_failed (job, process, status);

	/* Change the goal to stop; normally this doesn't have any
	 * side-effects, except when we're in the RUNNING state when it'll
	 * change the state as well.  We obviously don't want to change the
	 * state twice.
	 */
	if (stop) {
		if (job->state == JOB_RUNNING)
			state = FALSE;

		job_change_goal (job, JOB_STOP);
	}

	if (state)
		job_change_state (job, job_next_state (job));
}

/**
 * job_catch_runaway
 * @job: job being started.
 *
 * This function is called when changing the state of a job to starting,
 * before emitting the event.  It ensures that a job doesn't end up in
 * a restart loop by limiting the number of restarts in a particular
 * time limit.
 *
 * Returns: TRUE if the job is respawning too fast, FALSE if not.
 */
static int
job_catch_runaway (Job *job)
{
	nih_assert (job != NULL);

	if (job->config->respawn_limit && job->config->respawn_interval) {
		time_t interval;

		/* Time since last respawn ... this goes very large if we
		 * haven't done one, which is fine
		 */
		interval = time (NULL) - job->respawn_time;
		if (interval < job->config->respawn_interval) {
			job->respawn_count++;
			if (job->respawn_count > job->config->respawn_limit)
				return TRUE;
		} else {
			job->respawn_time = time (NULL);
			job->respawn_count = 1;
		}
	}

	return FALSE;
}

/**
 * job_process_stopped:
 * @job: job that changed,
 * @process: specific process.
 *
 * This function is called whenever a @process attached to @job is stopped
 * by the SIGSTOP signal (and not by a tty-related signal).
 *
 * Some jobs use this signal to signify that they have completed starting
 * up and are now running; thus we move them out of the spawned state.
 **/
static void
job_process_stopped (Job         *job,
		     ProcessType  process)
{
	nih_assert (job != NULL);

	/* Any process can stop on a signal, but we only care about the
	 * main process when the state is still spawned.
	 */
	if ((process != PROCESS_MAIN) || (job->state != JOB_SPAWNED))
		return;

	/* Send SIGCONT back and change the state to the next one, if this
	 * job behaves that way.
	 */
	if (job->config->expect == JOB_EXPECT_STOP) {
		kill (job->pid[process], SIGCONT);
		job_change_state (job, job_next_state (job));
	}
}


/**
 * job_process_trace_new:
 * @job: job that changed,
 * @process: specific process.
 *
 * This function is called when the traced @process attached to @job calls
 * its first exec(), still within the Upstart code before passing control
 * to the new executable.
 *
 * It sets the options for the trace so that forks and execs are reported.
 **/
static void
job_process_trace_new (Job         *job,
		       ProcessType  process)
{
	nih_assert (job != NULL);
	nih_assert ((job->trace_state == TRACE_NEW)
		    || (job->trace_state == TRACE_NEW_CHILD));

	/* Any process can get us to trace them, but we only care about the
	 * main process when the state is still spawned.
	 */
	if ((process != PROCESS_MAIN) || (job->state != JOB_SPAWNED))
		return;

	/* Set options so that we are notified when the process forks, and
	 * get a different kind of notification when it execs to a plain
	 * SIGTRAP.
	 */
	if (ptrace (PTRACE_SETOPTIONS, job->pid[process], NULL,
		    PTRACE_O_TRACEFORK | PTRACE_O_TRACEEXEC) < 0) {
		nih_warn (_("Failed to set ptrace options for "
			    "%s %s process (%d): %s"),
			  job_name (job), process_name (process),
			  job->pid[process], strerror (errno));
		return;
	}

	job->trace_state = TRACE_NORMAL;

	/* Allow the process to continue without delivering the
	 * kernel-generated signal that was for our eyes not theirs.
	 */
	if (ptrace (PTRACE_CONT, job->pid[process], NULL, 0) < 0)
		nih_warn (_("Failed to continue traced %s %s process (%d): %s"),
			  job_name (job), process_name (process),
			  job->pid[process], strerror (errno));
}

/**
 * job_process_trace_new_child:
 * @job: job that changed,
 * @process: specific process.
 *
 * This function is called whenever a traced @process attached to @job stops
 * after the fork() so that we can set the options before continuing it.
 *
 * Check to see whether we've reached the number of forks we expected, if
 * so detach the process and move towards the running state; otherwise we
 * set our important ptrace options, update the trace state so that
 * further SIGSTOP or SIGTRAP signals are treated as just that and allow
 * the process to continue.
 **/
static void
job_process_trace_new_child (Job         *job,
			     ProcessType  process)
{
	nih_assert (job != NULL);
	nih_assert (job->trace_state == TRACE_NEW_CHILD);

	/* Any process can get us to trace them, but we only care about the
	 * main process when the state is still spawned.
	 */
	if ((process != PROCESS_MAIN) || (job->state != JOB_SPAWNED))
		return;

	/* We need to fork at least twice unless we're expecting a
	 * single fork when we only need to fork once; once that limit
	 * has been reached, end the trace.
	 */
	job->trace_forks++;
	if ((job->trace_forks > 1) || (job->config->expect == JOB_EXPECT_FORK))
	{
		if (ptrace (PTRACE_DETACH, job->pid[process], NULL, 0) < 0)
			nih_warn (_("Failed to detach traced "
				    "%s %s process (%d): %s"),
				  job_name (job), process_name (process),
				  job->pid[process], strerror (errno));

		job->trace_state = TRACE_NONE;
		job_change_state (job, job_next_state (job));
		return;
	}

	job_process_trace_new (job, process);
}

/**
 * job_process_trace_signal:
 * @job: job that changed,
 * @process: specific process.
 *
 * This function is called whenever a traced @process attached to @job has
 * a signal sent to it.
 *
 * We don't care about these, they're a side effect of ptrace that we can't
 * turn off, so we just deliver them untampered with.
 **/
static void
job_process_trace_signal (Job         *job,
			  ProcessType  process,
			  int          signum)
{
	nih_assert (job != NULL);

	/* Any process can get us to trace them, but we only care about the
	 * main process when the state is still spawned.
	 */
	if ((process != PROCESS_MAIN) || (job->state != JOB_SPAWNED)
	    || (job->trace_state != TRACE_NORMAL))
		return;

	/* Deliver the signal */
	if (ptrace (PTRACE_CONT, job->pid[process], NULL, signum) < 0)
		nih_warn (_("Failed to deliver signal to traced "
			    "%s %s process (%d): %s"),
			  job_name (job), process_name (process),
			  job->pid[process], strerror (errno));
}

/**
 * job_process_trace_fork:
 * @job: job that changed,
 * @process: specific process.
 *
 * This function is called whenever a traced @process attached to @job calls
 * the fork() system call.
 *
 * We obtain the new child process id from the message and update the
 * structure so that we follow that instead, detaching from the process that
 * called fork.
 **/
static void
job_process_trace_fork (Job         *job,
			ProcessType  process)
{
	unsigned long data;

	nih_assert (job != NULL);

	/* Any process can get us to trace them, but we only care about the
	 * main process when the state is still spawned.
	 */
	if ((process != PROCESS_MAIN) || (job->state != JOB_SPAWNED)
	    || (job->trace_state != TRACE_NORMAL))
		return;

	/* Obtain the child process id from the ptrace event. */
	if (ptrace (PTRACE_GETEVENTMSG, job->pid[process], NULL, &data) < 0) {
		nih_warn (_("Failed to obtain child process id "
			    "for %s %s process (%d): %s"),
			  job_name (job), process_name (process),
			  job->pid[process], strerror (errno));
		return;
	}

	nih_info (_("%s %s process (%d) became new process (%d)"),
		  job_name (job), process_name (process),
		  job->pid[process], (pid_t)data);

	/* We no longer care about this process, it's the child that we're
	 * interested in from now on, so detach it and allow it to go about
	 * its business unhindered.
	 */
	if (ptrace (PTRACE_DETACH, job->pid[process], NULL, 0) < 0)
		nih_warn (_("Failed to detach traced %s %s process (%d): %s"),
			  job_name (job), process_name (process),
			  job->pid[process], strerror (errno));

	/* Update the process we're supervising which is about to get SIGSTOP
	 * so set the trace options to capture it.
	 */
	job->pid[process] = (pid_t)data;
	job->trace_state = TRACE_NEW_CHILD;
}

/**
 * job_process_trace_exec:
 * @job: job that changed,
 * @process: specific process.
 *
 * This function is called whenever a traced @process attached to @job calls
 * the exec() system call after we've set options on it to distinguish them
 * from ordinary SIGTRAPs.
 *
 * We assume that if the job calls exec that it's finished forking so we can
 * drop the trace entirely; we have no interest in tracing the new child.
 **/
static void
job_process_trace_exec (Job         *job,
			ProcessType  process)
{
	nih_assert (job != NULL);

	/* Any process can get us to trace them, but we only care about the
	 * main process when the state is still spawned and we're tracing it.
	 */
	if ((process != PROCESS_MAIN) || (job->state != JOB_SPAWNED)
	    || (job->trace_state != TRACE_NORMAL))
		return;

	nih_info (_("%s %s process (%d) executable changed"),
		  job_name (job), process_name (process), job->pid[process]);

	if (ptrace (PTRACE_DETACH, job->pid[process], NULL, 0) < 0)
		nih_warn (_("Failed to detach traced "
			    "%s %s process (%d): %s"),
			  job_name (job), process_name (process),
			  job->pid[process], strerror (errno));

	job->trace_state = TRACE_NONE;
	job_change_state (job, job_next_state (job));
}


/**
 * job_handle_event:
 * @event: event to be handled.
 *
 * This function is called whenever an event reaches the handling state.
 * It iterates the list of jobs and stops or starts any necessary.
 **/
void
job_handle_event (Event *event)
{
	nih_assert (event != NULL);

	job_init ();

	NIH_HASH_FOREACH_SAFE (jobs, iter) {
		JobConfig *config = (JobConfig *)iter;

		/* We stop first so that if an event is listed both as a
		 * stop and start event, it causes an active running process
		 * to be killed, the stop script then the start script to be
		 * run.  In any other state, it has no special effect.
		 *
		 * (The other way around would be just strange, it'd cause
		 * a process's start and stop scripts to be run without the
		 * actual process).
		 */
		NIH_LIST_FOREACH_SAFE (&config->instances, job_iter) {
			Job *job = (Job *)job_iter;

			if (job->stop_on
			    && event_operator_handle (job->stop_on, event,
						      job->env)
			    && job->stop_on->value) {
				char    **env = NULL;
				size_t    len = 0;
				NihList  *list;

				/* Collect environment that stopped the job
				 * for the pre-stop script; it can make a
				 * more informed decision whether the stop is
				 * valid.  We don't add config environment
				 * since this is appended to the existing job
				 * environment.
				 */
				NIH_MUST (list = nih_list_new (NULL));
				event_operator_collect (job->stop_on,
							&env, NULL, &len,
							"UPSTART_STOP_EVENTS",
							list);

				if (job->goal != JOB_STOP) {
					if (job->stop_env)
						nih_free (job->stop_env);

					nih_alloc_reparent (env, job);
					job->stop_env = env;

					job_unblock (job, FALSE);

					nih_alloc_reparent (list, job);
					job->blocking = list;

					job_change_goal (job, JOB_STOP);
				} else {
					NIH_LIST_FOREACH (list, iter) {
						NihListEntry *entry = (NihListEntry *)iter;
						Event        *event = (Event *)entry->data;

						nih_assert (event != NULL);

						event_unblock (event);
					}

					nih_free (list);
					nih_free (env);
				}

				event_operator_reset (job->stop_on);
			}

		}

		/* Now we match the start events for the configuration to
		 * see whether we need a new instance.
		 */
		if (config->start_on
		    && event_operator_handle (config->start_on, event, NULL)
		    && config->start_on->value) {
			char    **env, *name = NULL;
			size_t    len;
			NihList  *list;
			Job      *job;

			/* Construct the environment for the new instance
			 * from the configuration and the start events
			 * and collect the list of events we have to block.
			 */
			NIH_MUST (list = nih_list_new (NULL));
			NIH_MUST (env = job_config_environment (
					  NULL, config, &len));
			event_operator_collect (config->start_on,
						&env, NULL, &len,
						"UPSTART_EVENTS", list);

			/* Expand the instance name against the environment */
			if (config->instance_name) {
				NIH_SHOULD (name = environ_expand (
						    NULL, config->instance_name,
						    env));
				if (! name) {
					NihError *err;

					err = nih_error_get ();
					nih_warn (_("Failed to obtain %s instance: %s"),
						  config->name, err->message);
					nih_free (err);

					goto error;
				}
			}

			/* Locate the current instance or create a new one */
			job = job_instance (config, name);
			if (! job) {
				NIH_MUST (job = job_new (config, name));
			} else {
				if (name)
					nih_free (name);
			}

			/* Start the job with the environment we want */
			if (job->goal != JOB_START) {
				if (job->start_env)
					nih_free (job->start_env);

				nih_alloc_reparent (env, job);
				job->start_env = env;

				job_unblock (job, FALSE);

				nih_alloc_reparent (list, job);
				job->blocking = list;

				job_change_goal (job, JOB_START);
			} else {
			error:
				NIH_LIST_FOREACH (list, iter) {
					NihListEntry *entry = (NihListEntry *)iter;
					Event        *event = (Event *)entry->data;

					nih_assert (event != NULL);

					event_unblock (event);
				}

				nih_free (list);
				nih_free (env);
			}

			event_operator_reset (config->start_on);
		}
	}
}

/**
 * job_handle_event_finished:
 * @event: event that has finished.
 *
 * This function is called whenever an event finishes.  It iterates the
 * list of jobs checking for any blocked by that event, unblocking them
 * and sending them to the next state.
 **/
void
job_handle_event_finished (Event *event)
{
	nih_assert (event != NULL);

	job_init ();

	NIH_HASH_FOREACH_SAFE (jobs, iter) {
		JobConfig *config = (JobConfig *)iter;

		NIH_LIST_FOREACH_SAFE (&config->instances, job_iter) {
			Job *job = (Job *)job_iter;

			if (job->blocked != event)
				continue;

			job->blocked = NULL;

			job_change_state (job, job_next_state (job));
		}
	}
}
