/* upstart
 *
 * job.c - handling of tasks and services
 *
 * Copyright © 2007 Canonical Ltd.
 * Author: Scott James Remnant <scott@ubuntu.com>.
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

#include <upstart/enum.h>

#include "event.h"
#include "process.h"
#include "job.h"
#include "notify.h"
#include "paths.h"


/**
 * SHELL_CHARS:
 *
 * This is the list of characters that, if encountered in a process, cause
 * it to always be run with a shell.
 **/
#define SHELL_CHARS "~`!$^&*()=|\\{}[];\"'<>?"


/* Prototypes for static functions */
static const char *job_name          (Job *job);
static void        job_change_cause  (Job *job, EventEmission *emission);
static void        job_emit_event    (Job *job);
static int         job_catch_runaway (Job *job);
static void        job_kill_timer    (ProcessType process, NihTimer *timer);


/**
 * job_id
 *
 * This counter is used to assign unique job ids to jobs and is incremented
 * each time we use it.  After a while (4 billion jobs) it'll wrap over, in
 * which case you should set job_id_wrapped and take care to check an id
 * isn't taken.
 **/
static uint32_t job_id = 0;
static int      job_id_wrapped = FALSE;

/**
 * jobs:
 *
 * This hash table holds the list of known jobs indexed by their name.
 * Each entry is a Job structure; multiple entries with the same name may
 * exist since some may be instances of others.
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
		NIH_MUST (jobs = nih_hash_new (NULL, 0,
					       (NihKeyFunction)job_name));
}


/**
 * job_process_new:
 * @parent: parent of new job process.
 *
 * Allocates and returns a new empty JobProcess structure.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
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
	process->pid = 0;

	return process;
}

/**
 * job_process_copy:
 * @parent: parent of new job process,
 * @old_process: job process to copy.
 *
 * Allocates and returns a new JobProcess structure which is a copy of the
 * configuration details of @old_process, but with a clean state.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * Returns: newly allocated job process structure or NULL if insufficient
 * memory.
 **/
JobProcess *
job_process_copy (const void       *parent,
		  const JobProcess *old_process)
{
	JobProcess *process;

	nih_assert (old_process != NULL);

	process = job_process_new (parent);
	if (! process)
		return NULL;

	process->script = old_process->script;

	if (old_process->command) {
		process->command = nih_strdup (process, old_process->command);
		if (! process->command) {
			nih_free (process);
			return NULL;
		}
	}

	return process;
}


/**
 * job_next_id:
 *
 * Returns the current value of the job_id counter, unless that has
 * been wrapped before, in which case it checks whether the value is
 * currently in use before returning it.  If the value is in use, it
 * increments the counter until it finds a value that isn't, or until it
 * has checked the entire value space.
 *
 * This is most efficient while less than 4 billion jobs have been
 * defined, at which point it becomes slightly less efficient.  If there
 * are currently 4 billion known jobs (!!) we lose the ability to generate
 * unique ids, and emit an error -- if we start seeing this in the field,
 * we can always increase the size to a 64-bit number or something.
 *
 * Returns: next usable id.
 **/
static inline uint32_t
job_next_id (void)
{
	uint32_t id;

	/* If we've wrapped the job_id counter, we can't just assume that
	 * the current value isn't taken, we need to make sure that nothing
	 * is using it first.
	 */
	if (job_id_wrapped) {
		uint32_t start_id = job_id;

		while (job_find_by_id (job_id)) {
			job_id++;

			/* Make sure we don't end up in an infinite loop if
			 * we're currently handling 4 billion events.
			 */
			if (job_id == start_id) {
				nih_error (_("Job id %zu is not unique"),
					   job_id);
				break;
			}
		}
	}

	/* Use the current value of the counter, it's unique as we're ever
	 * going to get; increment the counter afterwards so the next time
	 * this runs, we have moved forwards.
	 */
	id = job_id++;

	/* If incrementing the counter gave us zero, we consumed the entire
	 * id space.  This means that in future we can't assume that the ids
	 * are unique, next time we'll have to be more careful.
	 */
	if (! job_id) {
		if (! job_id_wrapped)
			nih_debug ("Wrapped job_id counter");

		job_id_wrapped = TRUE;
	}

	return id;
}

/**
 * job_new:
 * @parent: parent of new job,
 * @name: name of new job,
 *
 * Allocates and returns a new Job structure with the @name given, and
 * appends it to the internal list of registered jobs.  It is up to the
 * caller to ensure that @name is unique amongst the job list.
 *
 * The job can be removed using nih_list_free().
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * Returns: newly allocated job structure or NULL if insufficient memory.
 **/
Job *
job_new (const void *parent,
	 const char *name)
{
	Job *job;
	int  i;

	nih_assert (name != NULL);
	nih_assert (strlen (name) > 0);

	job_init ();

	job = nih_new (parent, Job);
	if (! job)
		return NULL;

	nih_list_init (&job->entry);

	job->id = job_next_id ();
	job->name = nih_strdup (job, name);
	if (! job->name) {
		nih_free (job);
		return NULL;
	}

	job->description = NULL;
	job->author = NULL;
	job->version = NULL;

	job->instance_of = NULL;
	job->delete = FALSE;

	job->goal = JOB_STOP;
	job->state = JOB_WAITING;

	job->cause = NULL;
	job->blocked = NULL;

	job->failed = FALSE;
	job->failed_process = -1;
	job->exit_status = 0;

	nih_list_init (&job->start_events);
	nih_list_init (&job->stop_events);
	nih_list_init (&job->emits);

	job->process = nih_alloc (job, sizeof (JobProcess *)*PROCESS_LAST);
	if (! job->process) {
		nih_free (job);
		return NULL;
	}

	for (i = 0; i < PROCESS_LAST; i++)
		job->process[i] = NULL;

	job->normalexit = NULL;
	job->normalexit_len = 0;

	job->kill_timeout = JOB_DEFAULT_KILL_TIMEOUT;
	job->kill_timer = NULL;

	job->instance = FALSE;
	job->service = FALSE;
	job->respawn = FALSE;
	job->respawn_limit = JOB_DEFAULT_RESPAWN_LIMIT;
	job->respawn_interval = JOB_DEFAULT_RESPAWN_INTERVAL;
	job->respawn_count = 0;
	job->respawn_time = 0;

	job->daemon = FALSE;
	job->pid_file = NULL;
	job->pid_binary = NULL;
	job->pid_timeout = JOB_DEFAULT_PID_TIMEOUT;
	job->pid_timer = NULL;

	job->console = CONSOLE_NONE;
	job->env = NULL;

	job->umask = JOB_DEFAULT_UMASK;
	job->nice = 0;

	for (i = 0; i < RLIMIT_NLIMITS; i++)
		job->limits[i] = NULL;

	job->chroot = NULL;
	job->chdir = NULL;

	nih_hash_add (jobs, &job->entry);

	return job;
}

/**
 * job_copy:
 * @parent: parent of new job,
 * @old_job: job to copy.
 *
 * Allocates and returns a new Job structure which is a copy of the
 * configuration details of @old_job, but with a clean state.
 *
 * The job can be removed using nih_list_free().
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * Returns: newly allocated job structure or NULL if insufficient memory.
 **/
Job *
job_copy (const void *parent,
	  const Job  *old_job)
{
	Job *job;
	int  i;

	nih_assert (old_job != NULL);

	job = job_new (parent, old_job->name);
	if (! job)
		return NULL;

	if (old_job->description) {
		job->description = nih_strdup (job, old_job->description);
		if (! job->description)
			goto error;
	}

	if (old_job->author) {
		job->author = nih_strdup (job, old_job->author);
		if (! job->author)
			goto error;
	}

	if (old_job->version) {
		job->version = nih_strdup (job, old_job->version);
		if (! job->version)
			goto error;
	}

	NIH_LIST_FOREACH (&old_job->start_events, iter) {
		Event *old_event = (Event *)iter;
		Event *event;

		event = event_copy (job, old_event);
		if (! event)
			goto error;

		nih_list_add (&job->start_events, &event->entry);
	}

	NIH_LIST_FOREACH (&old_job->stop_events, iter) {
		Event *old_event = (Event *)iter;
		Event *event;

		event = event_copy (job, old_event);
		if (! event)
			goto error;

		nih_list_add (&job->stop_events, &event->entry);
	}

	NIH_LIST_FOREACH (&old_job->emits, iter) {
		Event *old_event = (Event *)iter;
		Event *event;

		event = event_copy (job, old_event);
		if (! event)
			goto error;

		nih_list_add (&job->emits, &event->entry);
	}

	for (i = 0; i < PROCESS_LAST; i++) {
		if (old_job->process[i]) {
			job->process[i] = job_process_copy (
				job->process, old_job->process[i]);
			if (! job->process[i])
				goto error;
		}
	}

	if (old_job->normalexit && old_job->normalexit_len) {
		job->normalexit = nih_alloc (job, (sizeof (int)
						   * old_job->normalexit_len));
		if (! job->normalexit)
			goto error;

		memcpy (job->normalexit, old_job->normalexit,
			sizeof (int) * old_job->normalexit_len);
		job->normalexit_len = old_job->normalexit_len;
	}

	job->kill_timeout = old_job->kill_timeout;

	job->instance = old_job->instance;
	job->service = old_job->service;
	job->respawn = old_job->respawn;
	job->respawn_limit = old_job->respawn_limit;
	job->respawn_interval = old_job->respawn_interval;

	job->daemon = old_job->daemon;

	if (old_job->pid_file) {
		job->pid_file = nih_strdup (job, old_job->pid_file);
		if (! job->pid_file)
			goto error;
	}

	if (old_job->pid_binary) {
		job->pid_binary = nih_strdup (job, old_job->pid_binary);
		if (! job->pid_binary)
			goto error;
	}

	job->pid_timeout = old_job->pid_timeout;

	job->console = old_job->console;

	if (old_job->env) {
		size_t   len;
		char   **e;

		len = 0;
		job->env = nih_str_array_new (job);
		if (! job->env)
			goto error;

		for (e = old_job->env; e && *e; e++)
			if (! nih_str_array_add (&job->env, job, &len, *e))
				goto error;
	}

	job->umask = old_job->umask;
	job->nice = old_job->nice;

	for (i = 0; i < RLIMIT_NLIMITS; i++) {
		if (old_job->limits[i]) {
			job->limits[i] = nih_new (job, struct rlimit);
			if (! job->limits[i])
				goto error;

			job->limits[i]->rlim_cur \
				= old_job->limits[i]->rlim_cur;
			job->limits[i]->rlim_max \
				= old_job->limits[i]->rlim_max;
		}
	}

	if (old_job->chroot) {
		job->chroot = nih_strdup (job, old_job->chroot);
		if (! job->chroot)
			goto error;
	}

	if (old_job->chdir) {
		job->chdir = nih_strdup (job, old_job->chdir);
		if (! job->chdir)
			goto error;
	}

	return job;

error:
	nih_list_free (&job->entry);
	return NULL;
}

/**
 * job_name:
 * @job: job to be checked.
 *
 * This is the hash key function for the jobs hash table, returning the
 * name of the job.
 *
 * Returns: pointer to the job name.
 **/
static const char *
job_name (Job *job)
{
	nih_assert (job != NULL);

	return job->name;
}


/**
 * job_find_by_name:
 * @name: name of job.
 *
 * Finds the job with the given @name in the jobs hash table.  This will
 * not return instance jobs, instead preferring to return the job that they
 * are actually an instance of, and will not return jobs marked for deletion.
 *
 * Returns: job found or NULL if not known.
 **/
Job *
job_find_by_name (const char *name)
{
	Job *job;

	nih_assert (name != NULL);

	job_init ();

	job = (Job *)nih_hash_lookup (jobs, name);
	while (job) {
		if (job->instance_of && (! job->instance_of->delete)) {
			return job->instance_of;
		} else if (job->delete) {
			job = (Job *)nih_hash_search (jobs, name, &job->entry);
		} else {
			break;
		}
	}

	return job;
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
	Job *job;
	int  i;

	nih_assert (pid > 0);

	job_init ();

	NIH_HASH_FOREACH (jobs, iter) {
		job = (Job *)iter;

		for (i = 0; i < PROCESS_LAST; i++) {
			if (job->process[i] && (job->process[i]->pid == pid)) {
				if (process)
					*process = i;

				return job;
			}
		}
	}

	return NULL;
}

/**
 * job_find_by_id:
 * @id: unique job id to find.
 *
 * Finds the job with the unique id @id in the jobs hash table.
 *
 * Returns: job found or NULL if not known.
 **/
Job *
job_find_by_id (uint32_t id)
{
	Job *job;

	job_init ();

	NIH_HASH_FOREACH (jobs, iter) {
		job = (Job *)iter;

		if (job->id == id)
			return job;
	}

	return NULL;
}


/**
 * job_instance:
 * @job: job to spawn from.
 *
 * This function is used to spawn a new instance of @job, if appropriate;
 * it should be called before attempting to start a job as you cannot
 * start a master of an instance job.
 *
 * Returns: new instance, or @job for non-instance jobs.
 **/
Job *
job_instance (Job *job)
{
	Job *instance;

	nih_assert (job != NULL);
	nih_assert (job->state != JOB_DELETED);

	if ((! job->instance) || (job->instance_of != NULL))
		return job;

	NIH_MUST (instance = job_copy (NULL, job));
	instance->instance_of = job;
	instance->delete = TRUE;

	return instance;
}

/**
 * job_change_goal:
 * @job: job to change goal of,
 * @goal: goal to change to,
 * @emission: event emission causing change.
 *
 * This function changes the current goal of a @job to the new @goal given,
 * performing any necessary state changes or actions (such as killing
 * the running process) to correctly enter the new goal.
 *
 * @emission is stored in the Job's cause member, and may be NULL.
 * Any previous cause is unreferenced and allowed to finish handling
 * if it has no further references.
 *
 * Before starting a job, you should call job_instance() to ensure that you
 * have a job that can be started, as you may not attempt to change the
 * goal of an instance master.  You may also not change the goal of a deleted
 * job.
 **/
void
job_change_goal (Job           *job,
		 JobGoal        goal,
		 EventEmission *emission)
{
	nih_assert (job != NULL);
	nih_assert (job->state != JOB_DELETED);
	nih_assert ((! job->instance) || (job->instance_of != NULL));

	if (job->goal == goal)
		return;

	nih_info (_("%s goal changed from %s to %s"), job->name,
		  job_goal_name (job->goal), job_goal_name (goal));

	job->goal = goal;
	job_change_cause (job, emission);
	notify_job (job);


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
 * job_change_cause:
 * @job: job to change,
 * @emission: emission to set.
 *
 * Updates the reference to the emission that's causing @job to be started
 * or stopped to @emission, which may be NULL or even the same as the current
 * one.
 **/
static void
job_change_cause (Job           *job,
		  EventEmission *emission)
{
	nih_assert (job != NULL);

	if (job->cause == emission)
		return;

	if (job->cause) {
		notify_job_event (job);

		job->cause->jobs--;
		event_emit_finished (job->cause);
	}

	job->cause = emission;
	if (job->cause)
		job->cause->jobs++;
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
 **/
void
job_change_state (Job      *job,
		  JobState  state)
{
	nih_assert (job != NULL);

	while (job->state != state) {
		JobState old_state;

		nih_info (_("%s state changed from %s to %s"), job->name,
			  job_state_name (job->state), job_state_name (state));

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

			/* Catch runaway jobs; make sure we do this before
			 * we emit the starting event, so other jobs don't
			 * think we're going to be started.
			 */
			if (job_catch_runaway (job)) {
				nih_warn (_("%s respawning too fast, stopped"),
					  job->name);

				job_change_goal (job, JOB_STOP, job->cause);
				state = job_next_state (job);

				if (! job->failed) {
					job->failed = TRUE;
					job->failed_process = -1;
					job->exit_status = 0;
				}

				break;
			}

			/* Clear any old failed information */
			job->failed = FALSE;
			job->failed_process = -1;
			job->exit_status = 0;

			job_emit_event (job);

			break;
		case JOB_PRE_START:
			nih_assert (job->goal == JOB_START);
			nih_assert (old_state == JOB_STARTING);

			if (job->process[PROCESS_PRE_START]) {
				job_run_process (job, PROCESS_PRE_START);
			} else {
				state = job_next_state (job);
			}

			break;
		case JOB_SPAWNED:
			nih_assert (job->goal == JOB_START);
			nih_assert (old_state == JOB_PRE_START);

			if (job->process[PROCESS_MAIN])
				job_run_process (job, PROCESS_MAIN);

			if (! job->daemon)
				state = job_next_state (job);

			break;
		case JOB_POST_START:
			nih_assert (job->goal == JOB_START);
			nih_assert (old_state == JOB_SPAWNED);

			if (job->process[PROCESS_POST_START]) {
				job_run_process (job, PROCESS_POST_START);
			} else {
				state = job_next_state (job);
			}

			break;
		case JOB_RUNNING:
			nih_assert (job->goal == JOB_START);
			nih_assert ((old_state == JOB_POST_START)
				    || (old_state == JOB_PRE_STOP));

			job_emit_event (job);

			/* If we're a service, our goal is to be running;
			 * notify subscribed processes that we reached it,
			 * and change the cause.
			 */
			if (job->service) {
				notify_job_finished (job);
				job_change_cause (job, NULL);
			}

			break;
		case JOB_PRE_STOP:
			nih_assert (job->goal == JOB_STOP);
			nih_assert (old_state == JOB_RUNNING);

			if (job->process[PROCESS_PRE_STOP]) {
				job_run_process (job, PROCESS_PRE_STOP);
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

			job_emit_event (job);

			break;
		case JOB_KILLED:
			nih_assert (old_state == JOB_STOPPING);

			if (job->process[PROCESS_MAIN]
			    && (job->process[PROCESS_MAIN]->pid > 0)) {
				job_kill_process (job, PROCESS_MAIN);
			} else {
				state = job_next_state (job);
			}

			break;
		case JOB_POST_STOP:
			nih_assert (old_state == JOB_KILLED);

			if (job->process[PROCESS_POST_STOP]) {
				job_run_process (job, PROCESS_POST_STOP);
			} else {
				state = job_next_state (job);
			}

			break;
		case JOB_WAITING:
			nih_assert (job->goal == JOB_STOP);
			nih_assert ((old_state == JOB_POST_STOP)
				    || (old_state == JOB_STARTING));

			job_emit_event (job);

			notify_job_finished (job);
			job_change_cause (job, NULL);

			if (job->delete)
				state = job_next_state (job);

			break;
		case JOB_DELETED:
			nih_assert (job->goal == JOB_STOP);
			nih_assert (old_state == JOB_WAITING);

			break;
		}

		notify_job (job);
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
			return JOB_DELETED;
		case JOB_START:
			return JOB_STARTING;
		}
	case JOB_STARTING:
		switch (job->goal) {
		case JOB_STOP:
			return JOB_WAITING;
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
			if (job->process[PROCESS_MAIN]
			    && (job->process[PROCESS_MAIN]->pid > 0)) {
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
 * job_emit_event:
 * @job: job generating the event.
 *
 * Called from a state change because it believes an event should be
 * emitted.  Constructs the event with the right arguments and environment,
 * adds it to the pending queue, and if the event should block, stores it
 * in the blocked member of @job.
 *
 * The stopping and stopped events have an extra argument that is "ok" if
 * the job terminated successfully, or "failed" if it terminated with an
 * error.  If failed, a further argument indicates which process it was
 * that caused the failure and either an EXIT_STATUS or EXIT_SIGNAL
 * environment variable detailing it.
 **/
static void
job_emit_event (Job *job)
{
	EventEmission  *emission;
	const char     *name;
	int             stop = FALSE, block = FALSE;
	char          **args = NULL, **env = NULL;
	size_t          len;

	nih_assert (job != NULL);

	switch (job->state) {
	case JOB_STARTING:
		name = JOB_STARTING_EVENT;
		block = TRUE;
		break;
	case JOB_RUNNING:
		name = JOB_STARTED_EVENT;
		break;
	case JOB_STOPPING:
		name = JOB_STOPPING_EVENT;
		stop = TRUE;
		block = TRUE;
		break;
	case JOB_WAITING:
		name = JOB_STOPPED_EVENT;
		stop = TRUE;
		break;
	default:
		nih_assert_not_reached ();
	}

	len = 0;
	NIH_MUST (args = nih_str_array_new (NULL));
	NIH_MUST (nih_str_array_add (&args, NULL, &len, job->name));

	if (stop && job->failed) {
		char *exit;

		NIH_MUST (nih_str_array_add (&args, NULL, &len, "failed"));
		if (job->failed_process == -1) {
			NIH_MUST (nih_str_array_add (
					  &args, NULL, &len, "respawn"));
		} else {
			NIH_MUST (nih_str_array_add (
					  &args, NULL, &len,
					  process_name (job->failed_process)));
		}

		/* If the job is terminated by a signal, that is stored in
		 * the higher byte, and we set EXIT_SIGNAL instead of
		 * EXIT_STATUS.
		 */
		if (job->exit_status & ~0xff) {
			const char *sig;

			sig = nih_signal_to_name (job->exit_status >> 8);
			if (sig) {
				NIH_MUST (exit = nih_sprintf (
						  NULL, "EXIT_SIGNAL=%s",
						  sig));
			} else {
				NIH_MUST (exit = nih_sprintf (
						  NULL, "EXIT_SIGNAL=%d",
						  job->exit_status >> 8));
			}
		} else {
			NIH_MUST (exit = nih_sprintf (NULL, "EXIT_STATUS=%d",
						      job->exit_status));
		}

		len = 0;
		NIH_MUST (env = nih_str_array_new (NULL));
		NIH_MUST (nih_str_array_addp (&env, NULL, &len, exit));

	} else if (stop) {
		NIH_MUST (nih_str_array_add (&args, NULL, &len, "ok"));
	}

	emission = event_emit (name, args, env);

	if (block)
		job->blocked = emission;
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

	if (job->respawn_limit && job->respawn_interval) {
		time_t interval;

		/* Time since last respawn ... this goes very large if we
		 * haven't done one, which is fine
		 */
		interval = time (NULL) - job->respawn_time;
		if (interval < job->respawn_interval) {
			job->respawn_count++;
			if (job->respawn_count > job->respawn_limit)
				return TRUE;
		} else {
			job->respawn_time = time (NULL);
			job->respawn_count = 1;
		}
	}

	return FALSE;
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
 * No error is returned from this function because it will block until
 * the process_spawn() calls succeeds, that can only fail for temporary
 * reasons (such as a lack of process ids) which would cause problems
 * carrying on anyway.
 **/
void
job_run_process (Job         *job,
		 ProcessType  process)
{
	JobProcess  *proc;
	char       **argv, *script = NULL;
	size_t       argc;
	int          error = FALSE, fds[2];

	nih_assert (job != NULL);

	proc = job->process[process];
	nih_assert (proc != NULL);
	nih_assert (proc->command != NULL);
	nih_assert (proc->pid == 0);

	/* We run the process using a shell if it says it wants to be run
	 * as such, or if it contains any shell-like characters; since that's
	 * the best way to deal with things like variables.
	 */
	if ((proc->script) || strpbrk (proc->command, SHELL_CHARS)) {
		struct stat   statbuf;
		char        **arg;

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

		/* Append arguments from the cause event if set. */
		if (job->cause)
			for (arg = job->cause->event.args; arg && *arg; arg++)
				NIH_MUST (nih_str_array_add (&argv, NULL,
							     &argc, *arg));
	} else {
		/* Split the command on whitespace to produce a list of
		 * arguments that we can exec directly.
		 */
		NIH_MUST (argv = nih_str_split (NULL, proc->command,
						" \t\r\n", TRUE));
	}


	/* Spawn the process, repeat until fork() works */
	while ((proc->pid = process_spawn (job, argv)) < 0) {
		NihError *err;

		err = nih_error_get ();
		if (! error)
			nih_warn ("%s: %s", _("Failed to spawn process"),
				  err->message);
		nih_free (err);

		error = TRUE;
	}

	nih_free (argv);

	nih_info (_("Active %s %s process (%d)"),
		  job->name, process_name (process), proc->pid);


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
	JobProcess *proc;

	nih_assert (job != NULL);

	proc = job->process[process];
	nih_assert (proc != NULL);
	nih_assert (proc->pid > 0);

	nih_info (_("Sending TERM signal to %s %s process (%d)"),
		  job->name, process_name (process), proc->pid);

	if (process_kill (job, proc->pid, FALSE) < 0) {
		NihError *err;

		err = nih_error_get ();
		if (err->number != ESRCH)
			nih_warn (_("Failed to send TERM signal to %s %s process (%d): %s"),
				  job->name, process_name (process),
				  proc->pid, err->message);
		nih_free (err);

		return;
	}

	NIH_MUST (job->kill_timer = nih_timer_add_timeout (
			  job, job->kill_timeout,
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
	Job        *job;
	JobProcess *proc;

	nih_assert (timer != NULL);
	job = nih_alloc_parent (timer);

	proc = job->process[process];
	nih_assert (proc != NULL);
	nih_assert (proc->pid > 0);


	job->kill_timer = NULL;

	nih_info (_("Sending KILL signal to %s %s process (%d)"),
		   job->name, process_name (process), proc->pid);

	if (process_kill (job, proc->pid, TRUE) < 0) {
		NihError *err;

		err = nih_error_get ();
		if (err->number != ESRCH)
			nih_warn (_("Failed to send KILL signal to %s %s process (%d): %s"),
				  job->name, process_name (process),
				  proc->pid, err->message);
		nih_free (err);
	}
}


/**
 * job_child_reaper:
 * @data: unused,
 * @pid: process that died,
 * @killed: whether @pid was killed,
 * @status: exit status of @pid or signal that killed it.
 *
 * This callback should be registered with nih_child_add_watch() so that
 * when processes associated with jobs die, the structure is updated and
 * the next appropriate state chosen.
 *
 * Normally this is registered so it is called for all processes, and it
 * safe to do as it only acts if the process is linked to a job.
 **/
void
job_child_reaper (void  *data,
		  pid_t  pid,
		  int    killed,
		  int    status)
{
	Job         *job;
	ProcessType  process;
	int          failed = FALSE, stop = FALSE, state = TRUE;

	nih_assert (data == NULL);
	nih_assert (pid > 0);

	/* Find the job that died; if it's not one of ours, just let it
	 * be reaped normally
	 */
	job = job_find_by_pid (pid, &process);
	if (! job)
		return;

	/* Report the death */
	if (killed) {
		const char *sig;

		sig = nih_signal_to_name (status);
		if (sig) {
			nih_warn (_("%s %s process (%d) killed by %s signal"),
				  job->name, process_name (process), pid, sig);
		} else {
			nih_warn (_("%s %s process (%d) killed by signal %d"),
				  job->name, process_name (process), pid,
				  status);
		}

		/* Store the signal value in the higher byte so we can
		 * distinguish it from a normal exit status.
		 */
		status <<= 8;
	} else if (status) {
		nih_warn (_("%s %s process (%d) terminated with status %d"),
			  job->name, process_name (process), pid, status);
	} else {
		nih_info (_("%s %s process (%d) exited normally"),
			  job->name, process_name (process), pid);
	}

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
		 * For jobs that can be respawned, a zero exit status is
		 * also a failure unless listed.
		 *
		 * If the job is already to be stopped, we never consider
		 * it to be failed since we probably caused the termination.
		 */
		if ((job->goal != JOB_STOP)
		    && (killed || status || job->respawn))
		{
			failed = TRUE;
			for (size_t i = 0; i < job->normalexit_len; i++) {
				if (job->normalexit[i] == status) {
					failed = FALSE;
					break;
				}
			}

			/* We might be able to respawn the failed job;
			 * that's a simple matter of doing nothing.
			 */
			if (failed && job->respawn) {
				nih_warn (_("%s %s process ended, respawning"),
					  job->name, process_name (process));
				failed = FALSE;
				break;
			}
		}

		/* We don't change the state if we're in post-start and there's
		 * a post-start process running, or if we're in pre-stop and
		 * there's a pre-stop process running; we wait for those to
		 * finish instead.
		 */
		if ((job->state == JOB_POST_START)
		    && job->process[PROCESS_POST_START]
		    && (job->process[PROCESS_POST_START]->pid > 0)) {
			state = FALSE;
		} else if ((job->state == JOB_PRE_STOP)
		    && job->process[PROCESS_PRE_STOP]
		    && (job->process[PROCESS_PRE_STOP]->pid > 0)) {
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
		if (killed || status) {
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
		if (killed || status) {
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
	job->process[process]->pid = 0;


	/* Mark the job as failed; this information shows up as arguments
	 * and environment to the stop and stopped events generated for the
	 * job.
	 *
	 * In addition, mark the cause event failed as well; this is
	 * reported to the emitted of the event, and also causes a failed
	 * event to be generated.
	 */
	if (failed && (! job->failed)) {
		job->failed = TRUE;
		job->failed_process = process;
		job->exit_status = status;

		if (job->cause)
			job->cause->failed = TRUE;
	}

	/* Change the goal to stop; normally this doesn't have any
	 * side-effects, except when we're in the RUNNING state when it'll
	 * change the state as well.  We obviously don't want to change the
	 * state twice.
	 */
	if (stop) {
		if (job->state == JOB_RUNNING)
			state = FALSE;

		job_change_goal (job, JOB_STOP, job->cause);
	}

	if (state)
		job_change_state (job, job_next_state (job));
}


/**
 * job_handle_event:
 * @emission: event emission to be handled.
 *
 * This function is called whenever the emission of an event reaches the
 * handling state.  It iterates the list of jobs and stops or starts any
 * necessary.
 **/
void
job_handle_event (EventEmission *emission)
{
	nih_assert (emission != NULL);

	job_init ();

	NIH_HASH_FOREACH_SAFE (jobs, iter) {
		Job *job = (Job *)iter;

		/* Never try and handle events for jobs about to be deleted */
		if (job->state == JOB_DELETED)
			continue;

		/* We stop first so that if an event is listed both as a
		 * stop and start event, it causes an active running process
		 * to be killed, the stop script then the start script to be
		 * run.  In any other state, it has no special effect.
		 *
		 * (The other way around would be just strange, it'd cause
		 * a process's start and stop scripts to be run without the
		 * actual process)
		 */
		NIH_LIST_FOREACH (&job->stop_events, iter) {
			Event *stop_event = (Event *)iter;

			if (event_match (&emission->event, stop_event))
				job_change_goal (job, JOB_STOP, emission);
		}

		NIH_LIST_FOREACH (&job->start_events, iter) {
			Event *start_event = (Event *)iter;

			if (event_match (&emission->event, start_event)) {
				Job *instance;

				instance = job_instance (job);
				job_change_goal (instance, JOB_START,
						 emission);
			}
		}
	}
}

/**
 * job_handle_event_finished:
 * @emission: event emission that has finished.
 *
 * This function is called whenever the emission of an event finishes.  It
 * iterates the list of jobs checking for any blocked by that event,
 * unblocking them and sending them to the next state.
 * necessary.
 **/
void
job_handle_event_finished (EventEmission *emission)
{
	nih_assert (emission != NULL);

	job_init ();

	NIH_HASH_FOREACH_SAFE (jobs, iter) {
		Job *job = (Job *)iter;

		if (job->blocked != emission)
			continue;

		job->blocked = NULL;
		job_change_state (job, job_next_state (job));
	}
}


/**
 * job_detect_stalled:
 *
 * This function is called each time through the main loop to detect whether
 * the system is stalled, a state in which all jobs are dormant.  If we
 * detect this, we generate the stalled event so that the system may take
 * action (e.g. opening a shell).
 **/
void
job_detect_stalled (void)
{
	int stalled = TRUE, can_stall = FALSE;

	if (paused)
		return;

	NIH_HASH_FOREACH (jobs, iter) {
		Job *job = (Job *)iter;

		/* Check the start events to make sure that at least one
		 * job handles the stalled event, otherwise we loop.
		 */
		NIH_LIST_FOREACH (&job->start_events, event_iter) {
			Event *event = (Event *)event_iter;

			if (! strcmp (event->name, STALLED_EVENT))
				can_stall = TRUE;
		}

		if ((job->goal != JOB_STOP) || (job->state != JOB_WAITING))
			stalled = FALSE;
	}

	if (stalled && can_stall) {
		nih_info (_("System has stalled, generating %s event"),
			  STALLED_EVENT);
		event_emit (STALLED_EVENT, NULL, NULL);

		nih_main_loop_interrupt ();
	}
}

/**
 * job_free_deleted:
 * @data: unusued,
 * @func: loop function.
 *
 * This function is called each time through the main loop to free any
 * deleted jobs that are now in the deleted state.  We do this from the
 * main loop because otherwise we'd have to be careful whenever calling
 * job_change_goal() or job_change_state(); and we don't want that.
 **/
void
job_free_deleted (void)
{
	NIH_HASH_FOREACH_SAFE (jobs, iter) {
		Job *job = (Job *)iter;

		if (job->state != JOB_DELETED)
			continue;

		nih_debug ("Deleting %s job", job->name);
		nih_list_free (&job->entry);
	}

	/* Check instance jobs, as they won't ever be in the deleted state;
	 * do it here rather than above as we know that all instances of the
	 * jobs have been cleaned up, and any that remain should save it.
	 */
	NIH_HASH_FOREACH_SAFE (jobs, iter) {
		Job *job = (Job *)iter;
		int  has_instance = FALSE;

		if (! job->delete)
			continue;

		if ((! job->instance) || (job->instance_of != NULL))
			continue;

		/* Check for remaining instances */
		NIH_HASH_FOREACH (jobs, iter) {
			Job *instance = (Job *)iter;

			if (instance->instance_of == job) {
				has_instance = TRUE;
				break;
			}
		}

		if (has_instance)
			continue;

		nih_debug ("Deleting %s job", job->name);
		nih_list_free (&job->entry);
	}
}
