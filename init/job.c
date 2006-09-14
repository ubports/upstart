/* upstart
 *
 * job.c - handling of tasks and services
 *
 * Copyright © 2006 Canonical Ltd.
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

#include <upstart/job.h>

#include "event.h"
#include "process.h"
#include "job.h"
#include "control.h"
#include "paths.h"


/* Prototypes for static functions */
static void job_run_process   (Job *job, char * const  argv[]);
static void job_kill_timer    (Job *job, NihTimer *timer);
static int  job_catch_runaway (Job *job);


/**
 * jobs:
 *
 * This list holds the list of known jobs, each entry is of the Job
 * structure.  No particular order is maintained.
 **/
static NihList *jobs = NULL;

/**
 * idle_event:
 *
 * Event to be triggered once when the system is idle with no jobs changing
 * state.
 **/
static char *idle_event = NULL;


/**
 * job_init:
 *
 * Initialise the list of jobs.
 **/
static inline void
job_init (void)
{
	if (! jobs)
		NIH_MUST (jobs = nih_list_new ());
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
 * The job can be removed using #nih_list_free.
 *
 * Returns: newly allocated job structure or %NULL if insufficient memory.
 **/
Job *
job_new (void       *parent,
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

	job->goal = JOB_STOP;
	job->state = JOB_WAITING;

	nih_list_init (&job->start_events);
	nih_list_init (&job->stop_events);
	nih_list_init (&job->depends);

	job->process_state = PROCESS_NONE;
	job->pid = 0;
	job->kill_timeout = JOB_DEFAULT_KILL_TIMEOUT;
	job->kill_timer = NULL;

	job->spawns_instance = 0;
	job->is_instance = 0;

	job->respawn = 0;
	job->respawn_limit = JOB_DEFAULT_RESPAWN_LIMIT;
	job->respawn_interval = JOB_DEFAULT_RESPAWN_INTERVAL;
	job->respawn_count = 0;
	job->respawn_time = 0;
	job->normalexit = NULL;
	job->normalexit_len = 0;

	job->daemon = 0;
	job->pidfile = NULL;
	job->binary = NULL;
	job->pid_timeout = JOB_DEFAULT_PID_TIMEOUT;
	job->pid_timer = NULL;

	job->command = NULL;
	job->script = NULL;
	job->start_script = NULL;
	job->stop_script = NULL;
	job->respawn_script = NULL;

	job->console = CONSOLE_LOGGED;
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
 * Returns: job found or %NULL if not known.
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
 * Returns: job found or %NULL if not known.
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
	nih_assert (job->process_state == PROCESS_NONE);

	while (job->state != state) {
		JobState  old_state;
		char     *event = NULL;
		int       job_event = FALSE;

		nih_info (_("%s state changed from %s to %s"), job->name,
			  job_state_name (job->state), job_state_name (state));
		old_state = job->state;
		job->state = state;

		/* Check for invalid state changes; if ok, run the
		 * appropriate script or command, or change the state
		 * or goal.
		 */
		switch (job->state) {
		case JOB_WAITING:
			nih_assert (old_state == JOB_STOPPING);
			nih_assert (job->goal == JOB_STOP);

			/* FIXME
			 * instances need to be cleaned up */

			NIH_MUST (event = nih_sprintf (job, "%s/stopped",
						       job->name));

			break;
		case JOB_STARTING:
			nih_assert ((old_state == JOB_WAITING)
				    || (old_state == JOB_STOPPING));

			if (job->start_script) {
				job_run_script (job, job->start_script);
			} else {
				state = job_next_state (job);
			}

			NIH_MUST (event = nih_sprintf (job, "%s/start",
						       job->name));

			break;
		case JOB_RUNNING:
			nih_assert ((old_state == JOB_STARTING)
				    || (old_state == JOB_RESPAWNING));

			/* Must have either a script, or a command,
			 * but not both or neither.
			 */
			nih_assert (   ((job->script == NULL)
				        && (job->command != NULL))
				    || ((job->script != NULL)
					&& (job->command == NULL)));

			/* Catch run-away respawns */
			if (job_catch_runaway (job)) {
				nih_warn (_("%s respawning too fast, stopped"),
					  job->name);

				job->goal = JOB_STOP;
				state = job_next_state (job);
				event = NULL;
				break;
			}

			if (job->script) {
				job_run_script (job, job->script);
			} else if (job->command) {
				job_run_command (job, job->command);
			}

			/* Release our dependencies.  This will cause some
			 * amount of stack recursion while dependencies get
			 * started but it cannot affect this job (it only
			 * touches those in START/WAITING and we're in
			 * START/RUNNING)
			 */
			if (job->process_state == PROCESS_ACTIVE) {
				job_release_depends (job);

				/* Also send the job event if we're a service
				 * that is now running
				 */
				if (job->respawn)
					job_event = TRUE;
			}

			NIH_MUST (event = nih_sprintf (job, "%s/started",
						       job->name));

			break;
		case JOB_STOPPING:
			nih_assert ((old_state == JOB_STARTING)
				    || (old_state == JOB_RUNNING)
				    || (old_state == JOB_RESPAWNING));

			if (job->stop_script) {
				job_run_script (job, job->stop_script);
			} else {
				state = job_next_state (job);
			}

			NIH_MUST (event = nih_sprintf (job, "%s/stop",
						       job->name));

			/* Send the job event if we're a task that's just
			 * finished
			 */
			if (! job->respawn)
				job_event = TRUE;

			break;
		case JOB_RESPAWNING:
			nih_assert (old_state == JOB_RUNNING);

			if (job->respawn_script) {
				job_run_script (job, job->respawn_script);
			} else {
				state = job_next_state (job);
			}

			NIH_MUST (event = nih_sprintf (job, "%s/respawn",
						       job->name));

			break;
		}

		/* Notify subscribed processes and queue the event */
		control_handle_job (job);
		if (event) {
			event_queue (event);
			nih_free (event);
		}

		if (job_event)
			event_queue (job->name);
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
			return JOB_STOPPING;
		case JOB_START:
			return JOB_RUNNING;
		}
	case JOB_RUNNING:
		switch (job->goal) {
		case JOB_STOP:
			return JOB_STOPPING;
		case JOB_START:
			return JOB_RESPAWNING;
		}
	case JOB_STOPPING:
		switch (job->goal) {
		case JOB_STOP:
			return JOB_WAITING;
		case JOB_START:
			return JOB_STARTING;
		}
	case JOB_RESPAWNING:
		switch (job->goal) {
		case JOB_STOP:
			return JOB_STOPPING;
		case JOB_START:
			return JOB_RUNNING;
		}
	default:
		return job->state;
	}
}


/**
 * job_run_command:
 * @job: job to run process for,
 * @command: command and arguments to be run.
 *
 * This function splits @command into whitespace separated program name
 * and arguments and calls #job_run_process with the result.
 *
 * As a bonus, if @command contains any special shell characters such
 * as variables, redirection, or even just quotes; it arranges for the
 * command to instead be run by the shell so we don't need any complex
 * argument parsing of our own.
 *
 * No error is returned from this function because it will block until
 * the #process_spawn calls succeeds, that can only fail for temporary
 * reasons (such as a lack of process ids) which would cause problems
 * carrying on anyway.
 **/
void
job_run_command (Job        *job,
		 const char *command)
{
	char **argv;

	nih_assert (job != NULL);
	nih_assert (command != NULL);

	/* Use the shell for non-simple commands */
	if (strpbrk (command, "~`!$^&*()=|\\{}[];\"'<>?")) {
		char *cmd;

		NIH_MUST (argv = nih_alloc (NULL, sizeof (char *) * 4));
		NIH_MUST (cmd = nih_sprintf (argv, "exec %s", command));
		argv[0] = SHELL;
		argv[1] = "-c";
		argv[2] = cmd;
		argv[3] = NULL;
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
 * to read commands from one of the special /dev/fd/NN devices and #NihIo
 * used to feed the script into that device.  A pointer to the #NihIo object
 * is not kept or stored because it will automatically clean itself up should
 * the script go away as the other end of the pipe will be closed.
 *
 * In either case the shell is run with the -e option so that commands will
 * fail if their exit status is not checked.
 *
 * No error is returned from this function because it will block until
 * the #process_spawn calls succeeds, that can only fail for temporary
 * reasons (such as a lack of process ids) which would cause problems
 * carrying on anyway.
 **/
void
job_run_script (Job        *job,
		const char *script)
{
	char *argv[5];

	nih_assert (job != NULL);
	nih_assert (script != NULL);

	/* Normally we just pass the script to the shell using the -c
	 * option, however there's a limit to the length of a command line
	 * (about 4KB) and that just looks bad in ps as well.
	 *
	 * So as an alternative we use the magic /dev/fd/NN devices and
	 * give the shell a script to run by piping it down.  Of course,
	 * the pipe buffer may not be big enough either, so we use NihIo
	 * to do it all asynchronously in the background.
	 */
	if (strlen (script) > 1024) {
		NihIo *io;
		char  *cmd;
		int    fds[2];

		/* Close the writing end when the child is exec'd */
		NIH_MUST (pipe (fds) == 0);
		nih_io_set_cloexec (fds[1]);

		/* FIXME actually always want it to be /dev/fd/3 and
		 * dup2() in the child to make it that way ... no way
		 * of passing that yet
		 */

		NIH_MUST (cmd = nih_sprintf (NULL, "/dev/fd/%d", fds[0]));

		argv[0] = SHELL;
		argv[1] = "-e";
		argv[2] = cmd;
		argv[3] = NULL;

		job_run_process (job, argv);

		/* Clean up and close the reading end (we don't need it) */
		nih_free (argv[2]);
		close (fds[0]);

		/* Put the entire script into an NihIo send buffer and
		 * then mark it for closure so that the shell gets EOF
		 * and the structure gets cleaned up automatically.
		 */
		NIH_MUST (io = nih_io_reopen (job, fds[1], NULL, NULL,
					      NULL, NULL));
		NIH_MUST (nih_io_write (io, script, strlen (script)) == 0);
		nih_io_shutdown (io);
	} else {
		/* Pass the script using -c */
		argv[0] = SHELL;
		argv[1] = "-e";
		argv[2] = "-c";
		argv[3] = (char *)script;
		argv[4] = NULL;

		job_run_process (job, argv);
	}
}

/**
 * job_run_process:
 * @job: job to run process for,
 * @argv: %NULL-terminated list of arguments for the process.
 *
 * This function spawns a new process for @job storing the pid and new
 * process state back in that object.  This can only be called when there
 * is not already a process, and the state is one that permits a process
 * (ie. everything except %JOB_WAITING).
 *
 * The caller should have already prepared the arguments, the list is
 * passed directly to #process_spawn.
 *
 * No error is returned from this function because it will block until
 * the #process_spawn calls succeeds, that can only fail for temporary
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
	nih_assert (job->state != JOB_WAITING);
	nih_assert (job->process_state == PROCESS_NONE);

	/* Run the process, repeat until fork() works */
	while ((pid = process_spawn (job, argv)) < 0) {
		NihError *err;

		err = nih_error_get ();
		if (! error)
			nih_warn ("%s: %s", _("Failed to spawn process"),
				  err->message);
		nih_free (err);
	}

	/* Update the job details */
	job->pid = pid;
	if (job->daemon && (job->state == JOB_RUNNING)) {
		/* FIXME should probably set timer or something?
		 *
		 * need to cope with daemons not being, after all */
		nih_info (_("Spawned %s process (%d)"), job->name, job->pid);
		job->process_state = PROCESS_SPAWNED;
	} else {
		nih_info (_("Active %s process (%d)"), job->name, job->pid);
		job->process_state = PROCESS_ACTIVE;
	}
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
 * The only state that this may be called in is %JOB_RUNNING with an
 * active process; all other states are transient, and are expected to
 * change within a relatively short space of time anyway.  For those it
 * is sufficient to simply change the goal and have the appropriate
 * state selected once the running script terminates.
 **/
void
job_kill_process (Job *job)
{
	nih_assert (job != NULL);

	nih_assert (job->state == JOB_RUNNING);
	nih_assert (job->process_state == PROCESS_ACTIVE);

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
		job->process_state = PROCESS_NONE;

		job_change_state (job, job_next_state (job));
		return;
	}

	job->process_state = PROCESS_KILLED;
	control_handle_job (job);

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

	nih_assert (job->state == JOB_RUNNING);
	nih_assert (job->process_state == PROCESS_KILLED);

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
	job->process_state = PROCESS_NONE;
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
 * This callback should be registered with #nih_child_add_watch so that
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
		nih_warn (_("%s process (%d) killed by signal %d"),
			  job->name, pid, status);
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
		/* Check whether we should respawn the process
		 *
		 * If a list of "normal" exit codes is provided, this is
		 * the list of exit codes that _prevent_ a respawn
		 */
		if ((job->respawn) && (job->goal == JOB_START)) {
			size_t i;

			for (i = 0; i < job->normalexit_len; i++)
				if ((! killed) &&
				    (job->normalexit[i] == status))
					break;

			if (i == job->normalexit_len) {
				nih_warn (_("%s process ended, respawning"),
					  job->name);
				break;
			}
		}

		job->goal = JOB_STOP;
		break;
	default:
		/* If a script is killed or exits with a status other than
		 * zero, it's considered a failure and prevents the process
		 * from starting.
		 */
		if (killed || status)
			job->goal = JOB_STOP;

		break;
	}

	job_change_state (job, job_next_state (job));
}


/**
 * job_start:
 * @job: job to be started.
 *
 * Changes the goal of @job from %JOB_STOP to %JOB_START and begins the
 * process of actually starting the job by changing the state if
 * necessary.
 *
 * The caller can infer the success of this function by checking the job
 * state after the call, e.g. if it remains in waiting then there are
 * dependencies.
 *
 * If @job is already active in some way (e.g. currently stopping), this
 * ensures that the job will be cleanly restarted when possible.
 *
 * This function has no effect if the goal is already %JOB_START.
 **/
void
job_start (Job *job)
{
	int held = FALSE;

	nih_assert (job != NULL);

	job_init ();

	if (job->goal == JOB_START)
		return;

	/* FIXME
	 * instance jobs need to be duplicated */

	nih_info (_("%s will be started"), job->name);
	job->goal = JOB_START;

	/* The only state change we need to induce is one away from the
	 * waiting state; anything else will be handled as the processes
	 * naturally terminate -- now that the goal is reversed, we'll
	 * go the other way.
	 */
	if (job->state != JOB_WAITING) {
		control_handle_job (job);
		return;
	}

	/* Iterate our dependencies */
	NIH_LIST_FOREACH (&job->depends, iter) {
		JobName *dep = (JobName *)iter;
		Job     *dep_job;

		/* Ignore dependencies on ourselves ... some people */
		if (! strcmp (dep->name, job->name))
			continue;

		/* First check the dependency is actually known; if not we
		 * still hold the job but we warn that there's a potential
		 * bogon in there.
		 */
		dep_job = job_find_by_name (dep->name);
		if (! dep_job) {
			nih_warn (_("%s waiting for unknown dependency: %s"),
				  job->name, dep->name);
			held = TRUE;
			continue;
		}

		/* If the dependency is running with an active process,
		 * we don't need to hold for it.
		 */
		if ((dep_job->goal == JOB_START)
		    && (dep_job->state == JOB_RUNNING)
		    && (dep_job->process_state == PROCESS_ACTIVE))
			continue;

		/* Hold for it */
		held = TRUE;
		nih_info (_("%s waiting for dependency: %s"),
			  job->name, dep_job->name);

		/* If the job is sitting at STOP, try poking it with a
		 * dependency event to see whether that does anything
		 * useful.
		 *
		 * NOTE: if successful and the dependency goes straight
		 * to RUNNING, this will mean that we'll be released
		 * _by_the_time_we_return_.
		 *
		 * MAKE NO FURTHER ASSUMPTIONS ABOUT THE JOB STATE
		 * and certainly DO NOT call job_next_state!
		 *
		 * Fortunately we're safe from that while held is TRUE.
		 */
		if (dep_job->goal == JOB_STOP) {
			Event *event;

			NIH_MUST (event = event_new (NULL, "dependency"));
			job_start_event (dep_job, event);
			nih_free (event);
		}
	}

	if (held) {
		/* Notify subscribers if we're still waiting as we've
		 * made a goal change but not a state change.
		 */
		if (job->state == JOB_WAITING)
			control_handle_job (job);
	} else {
		job_change_state (job, job_next_state (job));
	}
}

/**
 * job_stop:
 * @job: job to be stopped.
 *
 *
 * Changes the goal of @job from %JOB_START to %JOB_STOP and begins the
 * process of actually stopping the job by killing the active running
 * process if necessary.
 *
 * The caller can infer the success of this function by checking the job
 * state after the call.
 *
 * If @job is in the process of starting, this ensures that the job will
 * be cleanly stopped when possible.
 *
 * This function has no effect if the goal is already %JOB_STOP.
 **/
void
job_stop (Job *job)
{
	nih_assert (job != NULL);

	job_init ();

	if (job->goal == JOB_STOP)
		return;

	nih_info (_("%s will be stopped"), job->name);
	job->goal = JOB_STOP;

	/* The only state change we need to induce is one away from an
	 * active running process; anything else will be handled as the
	 * processes naturally terminate -- now that the goal is reversed,
	 * we'll go the other way.
	 */
	if ((job->state != JOB_RUNNING)
	    || (job->process_state != PROCESS_ACTIVE)) {
		control_handle_job (job);
		return;
	}

	job_kill_process (job);
}


/**
 * job_catch_runaway
 * @job: job respawning.
 *
 * This function ensures that a job doesn't enter a respawn loop by
 * limiting the number of respawns in a particular time limit.
 *
 * Returns: %TRUE if the job is respawning too fast, %FALSE if not.
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
 * job_release_depends:
 * @job: job now running.
 *
 * Release any jobs which depend on @job now that it is running with an
 * active process.
 *
 * The only jobs that will be affected are those that have a goal of
 * JOB_START but a state of JOB_WAITING and which list @job in their
 * depends list.  #job_change_state is called on these jobs.
 *
 * It is not possible for @job to be changed by a call to this function.
 **/
void
job_release_depends (Job *job)
{
	nih_assert (job != NULL);
	nih_assert (job->goal == JOB_START);
	nih_assert (job->state == JOB_RUNNING);
	nih_assert (job->process_state == PROCESS_ACTIVE);

	/* This is rather too expensive, but at least avoids hard to maintain
	 * linking tables.  Hopefully we'll never have thousands of jobs.
	 */
	NIH_LIST_FOREACH (jobs, job_iter) {
		Job *dep_job = (Job *)job_iter;

		/* Only release those jobs waiting to be released */
		if ((dep_job->goal != JOB_START)
		    || (dep_job->state != JOB_WAITING))
			continue;

		/* Check whether it's waiting for us */
		NIH_LIST_FOREACH (&dep_job->depends, dep_iter) {
			JobName *dep = (JobName *)dep_iter;

			if (strcmp (dep->name, job->name))
				continue;

			job_change_state (dep_job, job_next_state (dep_job));
		}
	}
}


/**
 * job_start_event:
 * @job: job to be started,
 * @event: event to handle.
 *
 * Iterates the list of events that can cause @job to be started, and if
 * @event is present, calls #job_start to change the goal.
 **/
void
job_start_event (Job   *job,
		 Event *event)
{
	nih_assert (job != NULL);
	nih_assert (event != NULL);

	NIH_LIST_FOREACH (&job->start_events, iter) {
		Event *start_event = (Event *)iter;

		if (event_match (event, start_event))
			job_start (job);
	}
}

/**
 * job_stop_event:
 * @job: job to be stopped,
 * @event: event to handle.
 *
 * Iterates the list of events that can cause @job to be stopped, and if
 * @event is present, calls #job_stop to change the goal.
 **/
void
job_stop_event (Job   *job,
		Event *event)
{
	nih_assert (job != NULL);
	nih_assert (event != NULL);

	NIH_LIST_FOREACH (&job->stop_events, iter) {
		Event *stop_event = (Event *)iter;

		if (event_match (event, stop_event))
			job_stop (job);
	}
}

/**
 * job_handle_event:
 * @event: event to be handled.
 *
 * This function is called whenever an event occurs.  It iterates the list
 * of jobs and stops or starts any necessary.
 **/
void
job_handle_event (Event *event)
{
	nih_assert (event != NULL);

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
		job_stop_event (job, event);
		job_start_event (job, event);
	}
}


/**
 * job_detect_idle:
 * @data: unused,
 * @func: loop function.
 *
 * This function is called each time through the main loop to detect whether
 * the system is stalled (nothing is running) or idle (nothing is changing
 * state).
 *
 * For the former it will generate the stalled event so that the system may
 * take action (e.g. opening a shell), and for the latter it will generate
 * the idle event set (usually none).
 **/
void
job_detect_idle (void)
{
	int stalled = TRUE, idle = TRUE, can_stall = FALSE;

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


		if (job->goal == JOB_STOP) {
			if (job->state != JOB_WAITING)
				stalled = idle = FALSE;
		} else {
			stalled = FALSE;

			if ((job->state != JOB_RUNNING)
			    || (job->process_state != PROCESS_ACTIVE))
				idle = FALSE;
		}
	}

	if (idle && idle_event) {
		nih_info (_("System is idle, generating %s event"),
			  idle_event);

		event_queue (idle_event);
		nih_free (idle_event);
		idle_event = NULL;

		nih_main_loop_interrupt ();
	} else if (stalled && can_stall) {
		nih_info (_("System has stalled, generating %s event"),
			  STALLED_EVENT);
		event_queue (STALLED_EVENT);

		nih_main_loop_interrupt ();
	}
}

/**
 * job_set_idle_event:
 * @name: event name to trigger when idle.
 *
 * This function is used to indicate that an event should be triggered
 * when the system is idle, which occurs when all jobs are either stopped
 * and waiting or starting and running.
 *
 * This event is only triggered once.
 **/
void
job_set_idle_event (const char *name)
{
	nih_assert (name != NULL);

	NIH_MUST (idle_event = nih_strdup (NULL, name));
}


/**
 * job_read_state:
 * @job: job to update,
 * @buf: serialised state.
 *
 * Parse the serialised state and update the job's details if we recognise
 * the line.  We need to always retain knowledge of this so we can always
 * be re-exec'd by an earlier version of init.  That's why this is so
 * trivial.
 *
 * @job may be %NULL if @buf begins "Job "
 **/
Job *
job_read_state (Job  *job,
		char *buf)
{
	char *ptr;

	nih_assert (buf != NULL);

	/* Every line must have a space, which splits the key and value */
	ptr = strchr (buf, ' ');
	if (ptr) {
		*(ptr++) = '\0';
	} else {
		return job;
	}

	/* Handle the case where we don't have a job yet first */
	if (! job) {
		if (strcmp (buf, "Job"))
			return job;

		/* Value is the name of the job to update */
		return job_find_by_name (ptr);
	}

	/* Otherwise handle the attributes */
	if (! strcmp (buf, ".goal")) {
		JobGoal value;

		value = job_goal_from_name (ptr);
		if (value != -1)
			job->goal = value;

	} else if (! strcmp (buf, ".state")) {
		JobState value;

		value = job_state_from_name (ptr);
		if (value != -1)
			job->state = value;

	} else if (! strcmp (buf, ".process_state")) {
		ProcessState value;

		value = process_state_from_name (ptr);
		if (value != -1)
			job->process_state = value;

	} else if (! strcmp (buf, ".pid")) {
		long value;

		value = strtol (ptr, &ptr, 10);
		if ((! *ptr) && (value > 1) && (value <= INT_MAX))
			job->pid = value;

	} else if (! strcmp (buf, ".kill_timer_due")) {
		time_t value;

		value = strtol (ptr, &ptr, 10);
		if ((! *ptr) && (value > 1) && (value <= INT_MAX))
			NIH_MUST (job->kill_timer = nih_timer_add_timeout (
					  job, value - time (NULL),
					  (NihTimerCb)job_kill_timer, job));

	} else if (! strcmp (buf, ".respawn_count")) {
		long value;

		value = strtol (ptr, &ptr, 10);
		if (! *ptr)
			job->respawn_count = value;

	} else if (! strcmp (buf, ".respawn_time")) {
		time_t value;

		value = strtol (ptr, &ptr, 10);
		if (! *ptr)
			job->respawn_time = value;
	}

	return job;
}

/**
 * job_write_state:
 * @state: file to write to.
 *
 * This is the companion function to %job_read_state, it writes to @state
 * lines for each job known about.
 **/
void
job_write_state (FILE *state)
{
	nih_assert (state != NULL);

	NIH_LIST_FOREACH (jobs, iter) {
		Job *job = (Job *)iter;

		fprintf (state, "Job %s\n", job->name);
		fprintf (state, ".goal %s\n", job_goal_name (job->goal));
		fprintf (state, ".state %s\n", job_state_name (job->state));
		fprintf (state, ".process_state %s\n",
			 process_state_name (job->process_state));
		fprintf (state, ".pid %d\n", job->pid);
		if (job->kill_timer)
			fprintf (state, ".kill_timer_due %ld\n",
				 job->kill_timer->due);
		if (job->pid_timer)
			fprintf (state, ".pid_timer_due %ld\n",
				 job->pid_timer->due);
		fprintf (state, ".respawn_count %d\n", job->respawn_count);
		fprintf (state, ".respawn_time %ld\n", job->respawn_time);
	}
}
