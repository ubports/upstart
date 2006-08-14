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


#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/io.h>
#include <nih/logging.h>
#include <nih/error.h>
#include <nih/errors.h>

#include "process.h"
#include "job.h"


/* Prototypes for static functions */
static void job_run_process (Job *job, char * const  argv[]);


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
		NIH_MUST (jobs = nih_list_new ());
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

	job->process_state = PROCESS_NONE;
	job->pid = 0;
	job->kill_timeout = JOB_DEFAULT_KILL_TIMEOUT;
	job->kill_timer = NULL;

	job->spawns_instance = 0;
	job->is_instance = 0;

	job->respawn = 0;
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
		return job->state;
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
 * job_state_name:
 * @state: state to convert.
 *
 * Converts an enumerated job state into the string used for the event
 * and for logging purposes.
 *
 * Returns: static string or %NULL if state not known.
 **/
const char *
job_state_name (JobState state)
{
	switch (state) {
	case JOB_WAITING:
		return "waiting";
	case JOB_STARTING:
		return "starting";
	case JOB_RUNNING:
		return "running";
	case JOB_STOPPING:
		return "stopping";
	case JOB_RESPAWNING:
		return "respawning";
	default:
		return NULL;
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
		argv = nih_alloc (NULL, sizeof (char *) * 4);
		argv[0] = SHELL;
		argv[1] = "-c";
		argv[2] = nih_sprintf (argv, "exec %s", command);
		argv[3] = NULL;
	} else {
		argv = nih_str_split (NULL, command, " ", TRUE);
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
		int    fds[2];

		/* Close the writing end when the child is exec'd */
		NIH_MUST (pipe (fds) == 0);
		nih_io_set_cloexec (fds[1]);

		argv[0] = SHELL;
		argv[1] = "-e";
		argv[2] = nih_sprintf (NULL, "/dev/fd/%d", fds[0]);
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
			nih_error ("%s: %s", _("Failed to spawn process"),
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
