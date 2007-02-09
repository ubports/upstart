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


/* Prototypes for static functions */
static void job_change_cause  (Job *job, EventEmission *emission);
static void job_emit_event    (Job *job);
static void job_run_process   (Job *job, char * const argv[]);
static int  job_catch_runaway (Job *job);
static void job_kill_timer    (Job *job, NihTimer *timer);


/**
 * jobs:
 *
 * This list holds the list of known jobs, each entry is of the Job
 * structure.  No particular order is maintained.
 **/
static NihList *jobs = NULL;


/**
 * job_init:
 *
 * Initialise the list of jobs.
 **/
static inline void
job_init (void)
{
	if (! jobs)
		NIH_MUST (jobs = nih_list_new (NULL));
}

/**
 * job_list:
 *
 * Return the list of jobs.
 **/
NihList *
job_list (void)
{
	job_init ();

	return jobs;
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

	job->name = nih_strdup (job, name);
	if (! job->name) {
		nih_free (job);
		return NULL;
	}

	job->description = NULL;
	job->author = NULL;
	job->version = NULL;

	job->delete = FALSE;

	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->pid = 0;

	job->cause = NULL;
	job->blocker = NULL;

	job->failed = FALSE;
	job->failed_state = JOB_WAITING;
	job->exit_status = 0;

	nih_list_init (&job->start_events);
	nih_list_init (&job->stop_events);
	nih_list_init (&job->emits);

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

	job->command = NULL;
	job->script = NULL;
	job->start_script = NULL;
	job->stop_script = NULL;

	job->console = CONSOLE_NONE;
	job->env = NULL;

	job->umask = JOB_DEFAULT_UMASK;
	job->nice = 0;

	for (i = 0; i < RLIMIT_NLIMITS; i++)
		job->limits[i] = NULL;

	job->chroot = NULL;
	job->chdir = NULL;

	nih_list_add (jobs, &job->entry);

	return job;
}


/**
 * job_find_by_name:
 * @name: name of job.
 *
 * Finds the job with the given @name in the list of known jobs.
 *
 * Returns: job found or NULL if not known.
 **/
Job *
job_find_by_name (const char *name)
{
	Job *job;

	nih_assert (name != NULL);

	job_init ();

	NIH_LIST_FOREACH (jobs, iter) {
		job = (Job *)iter;

		if (! strcmp (job->name, name))
			return job;
	}

	return NULL;
}

/**
 * job_find_by_pid:
 * @pid: process id of job.
 *
 * Finds the job with a process of the given @pid in the list of known jobs.
 *
 * Returns: job found or NULL if not known.
 **/
Job *
job_find_by_pid (pid_t pid)
{
	Job *job;

	nih_assert (pid > 0);

	job_init ();

	NIH_LIST_FOREACH (jobs, iter) {
		job = (Job *)iter;

		if (job->pid == pid)
			return job;
	}

	return NULL;
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
 **/
void
job_change_goal (Job           *job,
		 JobGoal        goal,
		 EventEmission *emission)
{
	nih_assert (job != NULL);

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
	 *
	 * FIXME also we need to kill running post-start or pre-stop scripts.
	 */
	switch (job->goal) {
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
 * It does NOT perform any actions to leave the current state, so this
 * function may only be called when there is no active process.
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
	nih_assert (job->pid == 0);

	while (job->state != state) {
		JobState old_state;

		nih_info (_("%s state changed from %s to %s"), job->name,
			  job_state_name (job->state), job_state_name (state));

		old_state = job->state;
		job->state = state;
		notify_job (job);

		/* Perform whatever action is necessary to enter the new
		 * state, such as executing a process or emitting an event.
		 */
		switch (job->state) {
		case JOB_WAITING:
			nih_assert (job->goal == JOB_STOP);
			nih_assert ((old_state == JOB_POST_STOP)
				    || (old_state == JOB_STARTING));

			job_emit_event (job);

			job_change_cause (job, NULL);

			break;
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
					job->failed_state = job->state;
					job->exit_status = 0;
				}

				break;
			}

			/* Clear any old failed information */
			job->failed = FALSE;
			job->failed_state = JOB_WAITING;
			job->exit_status = 0;

			job_emit_event (job);

			break;
		case JOB_PRE_START:
			nih_assert (job->goal == JOB_START);
			nih_assert (old_state == JOB_STARTING);

			if (job->start_script) {
				job_run_script (job, job->start_script);
			} else {
				state = job_next_state (job);
			}

			break;
		case JOB_SPAWNED:
			nih_assert (job->goal == JOB_START);
			nih_assert (old_state == JOB_PRE_START);

			if (job->script) {
				job_run_script (job, job->script);
			} else if (job->command) {
				job_run_command (job, job->command);
			}

			state = job_next_state (job);

			break;
		case JOB_POST_START:
			nih_assert (job->goal == JOB_START);
			nih_assert (old_state == JOB_SPAWNED);

			state = job_next_state (job);

			break;
		case JOB_RUNNING:
			nih_assert (job->goal == JOB_START);
			nih_assert ((old_state == JOB_POST_START)
				    || (old_state == JOB_PRE_STOP));

			job_emit_event (job);

			/* Clear the cause if we're a service since our goal
			 * is to be running, not to get back to waiting again.
			 */
			if (job->service)
				job_change_cause (job, NULL);

			break;
		case JOB_PRE_STOP:
			nih_assert (job->goal == JOB_STOP);
			nih_assert (old_state == JOB_RUNNING);

			state = job_next_state (job);

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

			if (job->pid > 0) {
				job_kill_process (job);
			} else {
				state = job_next_state (job);
			}

			break;
		case JOB_POST_STOP:
			nih_assert (old_state == JOB_KILLED);

			if (job->stop_script) {
				job_run_script (job, job->stop_script);
			} else {
				state = job_next_state (job);
			}

			break;
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
			return JOB_WAITING;
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
			return JOB_PRE_STOP;
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
 * in the blocker member of @job.
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

	if (stop && job->failed) {
		char *exit;

		len = 0;
		NIH_MUST (args = nih_str_array_new (NULL));
		NIH_MUST (nih_str_array_add (&args, NULL, &len, "failed"));
		NIH_MUST (nih_str_array_add (
				  &args, NULL, &len,
				  job_state_name (job->failed_state)));

		if (job->exit_status & 0x80) {
			const char *sig;

			sig = nih_signal_to_name (job->exit_status & 0x7f);
			if (sig) {
				NIH_MUST (exit = nih_sprintf (
						  NULL, "EXIT_SIGNAL=%s",
						  sig));
			} else {
				NIH_MUST (exit = nih_sprintf (
						  NULL, "EXIT_SIGNAL=%d",
						  job->exit_status & 0x7f));
			}
		} else {
			NIH_MUST (exit = nih_sprintf (NULL, "EXIT_STATUS=%d",
						      job->exit_status));
		}

		len = 0;
		NIH_MUST (env = nih_str_array_new (NULL));
		NIH_MUST (nih_str_array_addp (&env, NULL, &len, exit));

	} else if (stop) {
		len = 0;
		NIH_MUST (args = nih_str_array_new (NULL));
		NIH_MUST (nih_str_array_add (&args, NULL, &len, "ok"));
	}

	emission = event_emit (name, args, env);

	if (block)
		job->blocker = emission;
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
 * job_run_command:
 * @job: job to run process for,
 * @command: command and arguments to be run.
 *
 * This function splits @command into whitespace separated program name
 * and arguments and calls job_run_process() with the result.
 *
 * As a bonus, if @command contains any special shell characters such
 * as variables, redirection, or even just quotes; it arranges for the
 * command to instead be run by the shell so we don't need any complex
 * argument parsing of our own.
 *
 * No error is returned from this function because it will block until
 * the process_spawn() calls succeeds, that can only fail for temporary
 * reasons (such as a lack of process ids) which would cause problems
 * carrying on anyway.
 **/
void
job_run_command (Job        *job,
		 const char *command)
{
	char   **argv;
	size_t   argc;

	nih_assert (job != NULL);
	nih_assert (command != NULL);

	/* Use the shell for non-simple commands */
	if (strpbrk (command, "~`!$^&*()=|\\{}[];\"'<>?")) {
		char *cmd;

		NIH_MUST (cmd = nih_sprintf (NULL, "exec %s", command));

		argc = 0;
		NIH_MUST (argv = nih_str_array_new (NULL));

		NIH_MUST (nih_str_array_add (&argv, NULL, &argc, SHELL));
		NIH_MUST (nih_str_array_add (&argv, NULL, &argc, "-c"));
		NIH_MUST (nih_str_array_addp (&argv, NULL, &argc, cmd));
	} else {
		NIH_MUST (argv = nih_str_split (NULL, command, " ", TRUE));
	}

	job_run_process (job, argv);

	nih_free (argv);
}

/**
 * job_run_script:
 * @job: job to run process for,
 * @script: shell script to be run.
 *
 * This function takes the shell script code stored verbatim in @script
 * and arranges for it to be run by the system shell.
 *
 * If @script is reasonably small (less than 1KB) it is passed to the
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
job_run_script (Job        *job,
		const char *script)
{
	struct stat   statbuf;
	char        **argv;
	size_t        argc;

	nih_assert (job != NULL);
	nih_assert (script != NULL);

	argc = 0;
	NIH_MUST (argv = nih_str_array_new (NULL));

	/* Normally we just pass the script to the shell using the -c
	 * option, however there's a limit to the length of a command line
	 * (about 4KB) and that just looks bad in ps as well.
	 *
	 * So as an alternative we use the magic /dev/fd/NN devices and
	 * give the shell a script to run by piping it down.  Of course,
	 * the pipe buffer may not be big enough either, so we use NihIo
	 * to do it all asynchronously in the background.
	 */
	if ((strlen (script) > 1024)
	    && (stat (DEV_FD, &statbuf) == 0) && (S_ISDIR (statbuf.st_mode))) {
		NihIo *io;
		char  *cmd, **arg;
		int    fds[2];

		/* Close the writing end when the child is exec'd */
		NIH_MUST (pipe (fds) == 0);
		nih_io_set_cloexec (fds[1]);

		/* FIXME actually always want it to be /dev/fd/3 and
		 * dup2() in the child to make it that way ... no way
		 * of passing that yet
		 */

		NIH_MUST (cmd = nih_sprintf (NULL, "%s/%d", DEV_FD, fds[0]));

		NIH_MUST (nih_str_array_add (&argv, NULL, &argc, SHELL));
		NIH_MUST (nih_str_array_add (&argv, NULL, &argc, "-e"));
		NIH_MUST (nih_str_array_addp (&argv, NULL, &argc, cmd));

		if (job->cause)
			for (arg = job->cause->event.args; arg && *arg; arg++)
				NIH_MUST (nih_str_array_add (&argv, NULL,
							     &argc, *arg));

		job_run_process (job, argv);

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
	} else {
		char **arg;

		/* Pass the script using -c */
		NIH_MUST (nih_str_array_add (&argv, NULL, &argc, SHELL));
		NIH_MUST (nih_str_array_add (&argv, NULL, &argc, "-e"));
		NIH_MUST (nih_str_array_add (&argv, NULL, &argc, "-c"));
		NIH_MUST (nih_str_array_add (&argv, NULL, &argc, script));

		if (job->cause) {
			NIH_MUST (nih_str_array_add (&argv, NULL,
						     &argc, SHELL));

			for (arg = job->cause->event.args; arg && *arg; arg++)
				NIH_MUST (nih_str_array_add (&argv, NULL,
							     &argc, *arg));
		}

		job_run_process (job, argv);
	}

	nih_free (argv);
}

/**
 * job_run_process:
 * @job: job to run process for,
 * @argv: NULL-terminated list of arguments for the process.
 *
 * This function spawns a new process for @job storing the pid and new
 * process state back in that object.  This can only be called when there
 * is not already a process for the job.
 *
 * The caller should have already prepared the arguments, the list is
 * passed directly to process_spawn().
 *
 * No error is returned from this function because it will block until
 * the process_spawn() calls succeeds, that can only fail for temporary
 * reasons (such as a lack of process ids) which would cause problems
 * carrying on anyway.
 **/
static void
job_run_process (Job          *job,
		 char * const  argv[])
{
	pid_t pid;
	int   error = FALSE;

	nih_assert (job != NULL);
	nih_assert (job->pid == 0);

	/* Run the process, repeat until fork() works */
	while ((pid = process_spawn (job, argv)) < 0) {
		NihError *err;

		err = nih_error_get ();
		if (! error)
			nih_warn ("%s: %s", _("Failed to spawn process"),
				  err->message);
		nih_free (err);

		error = TRUE;
	}

	nih_info (_("Active %s process (%d)"), job->name, pid);

	job->pid = pid;
}


/**
 * job_kill_process:
 * @job: job to kill active process of.
 *
 * This function forces a @job to leave its current state by killing
 * its active process, thus forcing the state to be changed once the
 * process has terminated.
 *
 * The state change is not immediate unless the kill syscall fails.
 *
 * The only state that this may be called in is JOB_KILLED when there
 * is a process; all other states are transient, and are expected to
 * change within a relatively short space of time anyway.  For those it
 * is sufficient to simply change the goal and have the appropriate
 * state selected once the running script terminates.
 **/
void
job_kill_process (Job *job)
{
	nih_assert (job != NULL);
	nih_assert (job->state == JOB_KILLED);
	nih_assert (job->pid > 0);

	nih_info (_("Sending TERM signal to %s process (%d)"),
		  job->name, job->pid);

	if (process_kill (job, job->pid, FALSE) < 0) {
		NihError *err;

		err = nih_error_get ();
		if (err->number != ESRCH)
			nih_warn (_("Failed to send TERM signal to %s process (%d): %s"),
				  job->name, job->pid, err->message);
		nih_free (err);

		/* Carry on regardless; probably went away of its own
		 * accord while we were dawdling
		 */
		job->pid = 0;
		job_change_state (job, job_next_state (job));
		return;
	}

	NIH_MUST (job->kill_timer = nih_timer_add_timeout (
			  job, job->kill_timeout,
			  (NihTimerCb)job_kill_timer, job));
}

/**
 * job_kill_timer:
 * @job: job to kill active process of,
 * @timer: timer that caused us to be called.
 *
 * This callback is called if the process failed to terminate within
 * a particular time of being sent the TERM signal.  The process is killed
 * more forcibly by sending the KILL signal and is assumed to have died
 * whatever happens.
 **/
static void
job_kill_timer (Job      *job,
		NihTimer *timer)
{
	nih_assert (job != NULL);
	nih_assert (job->state == JOB_KILLED);
	nih_assert (job->pid > 0);

	nih_info (_("Sending KILL signal to %s process (%d)"),
		   job->name, job->pid);

	if (process_kill (job, job->pid, TRUE) < 0) {
		NihError *err;

		err = nih_error_get ();
		if (err->number != ESRCH)
			nih_warn (_("Failed to send KILL signal to %s process (%d): %s"),
				  job->name, job->pid, err->message);
		nih_free (err);
	}

	/* No point waiting around, if it's ignoring the KILL signal
	 * then it's wedged in the kernel somewhere; either that or it died
	 * while we were faffing
	 */

	job->pid = 0;
	job->kill_timer = NULL;

	job_change_state (job, job_next_state (job));
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
	Job *job;
	int  failed = FALSE, stop = FALSE;

	nih_assert (data == NULL);
	nih_assert (pid > 0);

	/* Find the job that died; if it's not one of ours, just let it
	 * be reaped normally
	 */
	job = job_find_by_pid (pid);
	if (! job)
		return;

	/* Report the death */
	if (killed) {
		const char *sig;

		sig = nih_signal_to_name (status);
		if (sig) {
			nih_warn (_("%s process (%d) killed by %s signal"),
				  job->name, pid, sig);
		} else {
			nih_warn (_("%s process (%d) killed by signal %d"),
				  job->name, pid, status);
		}

		/* Mark it as a signal */
		status |= 0x80;
	} else if (status) {
		nih_warn (_("%s process (%d) terminated with status %d"),
			  job->name, pid, status);
	} else {
		nih_info (_("%s process (%d) exited normally"),
			  job->name, pid);
	}

	/* FIXME we may be in SPAWNED here, in which case we don't want
	 * to do all this!
	 */

	job->pid = 0;
	job->process_state = PROCESS_NONE;

	/* Cancel any timer trying to kill the job */
	if (job->kill_timer) {
		nih_free (job->kill_timer);
		job->kill_timer = NULL;
	}


	switch (job->state) {
	case JOB_RUNNING:
		/* Check the list of normal exit codes; if the exit status
		 * or signal appears in that list, then we don't consider
		 * the job to have failed.
		 *
		 * An exit status of zero is only implied to be normal if
		 * the job isn't marked to be respawned.
		 */
		if (killed || status || job->respawn) {
			size_t i;

			failed = TRUE;
			for (i = 0; i < job->normalexit_len; i++) {
				if (job->normalexit[i] == status) {
					failed = FALSE;
					break;
				}
			}
		}

		/* We may be able to respawn the failed process, in which
		 * case we don't need to bother anything else; respawning
		 * is simply a matter of not touching anything.
		 */
		if (failed && job->respawn && (job->goal == JOB_START)) {
			nih_warn (_("%s process ended, respawning"),
				  job->name);

			failed = FALSE;
			break;
		}

		/* Otherwise whether it's failed or not, it's going away */
		stop = TRUE;
		break;
	default:
		/* If a script is killed or exits with a status other than
		 * zero, it's always considered a failure.  It also always
		 * results in the job being sent back to stop.
		 */
		if (killed || status) {
			failed = TRUE;
			stop = TRUE;
		}

		break;
	}

	/* Mark the job as failed; this information shows up as arguments
	 * and environment to the stop and stopped events generated for the
	 * job.
	 *
	 * In addition, mark the cause event failed as well; this is
	 * reported to the emitted of the event, and also causes a failed
	 * event to be generated.
	 *
	 * Never overwrite an existing failure record, and ignore failure
	 * of the running job if the goal is stop.
	 */
	if (failed && (! job->failed)
	    && ((job->goal != JOB_STOP) || (job->state != JOB_RUNNING))) {
		job->failed = TRUE;
		job->failed_state = job->state;
		job->exit_status = status;

		if (job->cause)
			job->cause->failed = TRUE;
	}

	/* Change the goal to stop; since we're in a non-rest state, this
	 * has no side-effects to the state.
	 */
	if (stop)
		job_change_goal (job, JOB_STOP, job->cause);

	/* We've reached a gateway point, switch to the next state. */
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

	NIH_LIST_FOREACH_SAFE (jobs, iter) {
		Job *job = (Job *)iter;

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

			if (event_match (&emission->event, start_event))
				job_change_goal (job, JOB_START, emission);
		}
	}
}


/**
 * job_detect_stalled:
 * @data: unused,
 * @func: loop function.
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

	NIH_LIST_FOREACH (jobs, iter) {
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
