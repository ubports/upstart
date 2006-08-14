/* upstart
 *
 * process.c - job process handling
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


#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/resource.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/signal.h>
#include <nih/logging.h>
#include <nih/error.h>

#include "job.h"
#include "process.h"


/* Prototypes for static functions */
static int process_setup_console     (Job *job);
static int process_setup_limits      (Job *job);
static int process_setup_environment (Job *job);


/**
 * process_spawn:
 * @job: job context for process to be spawned in,
 * @argv: %NULL-terminated list of arguments for the process.
 *
 * This function spawns a new process using the @job details to set up the
 * environment for it; the process is always a session and process group
 * leader as we never want anything in our own group.
 *
 * The process to be executed is given in the @argv array which is passed
 * directly to #execvp, so should be in the same %NULL-terminated form with
 * the first argument containing the path or filename of the binary.  The
 * PATH environment for the job will be searched.
 *
 * This function only spawns the process, it is up to the caller to ensure
 * that the information is saved into the job and the process is watched, etc.
 *
 * Note that the only error raised within this function is a failure of the
 * #fork syscall as the environment is set up within the child process.
 *
 * Returns: process id of new process on success, -1 on raised error
 **/
pid_t
process_spawn (Job          *job,
	       char * const  argv[])
{
	NihError *err;
	sigset_t  child_set, orig_set;
	pid_t     pid;
	int       i;

	nih_assert (job != NULL);

	/* Block SIGCHLD while we fork to avoid surprises */
	sigemptyset (&child_set);
	sigaddset (&child_set, SIGCHLD);
	sigprocmask (SIG_BLOCK, &child_set, &orig_set);

	/* Fork the child process, and return either the id or failure
	 * back to the caller.
	 */
	pid = fork ();
	if (pid > 0) {
		sigprocmask (SIG_SETMASK, &orig_set, NULL);

		nih_debug ("Spawned process %d for %s", pid, job->name);
		return pid;
	} else if (pid < 0) {
		sigprocmask (SIG_SETMASK, &orig_set, NULL);

		nih_return_system_error (-1);
	}


	/* We're now in the child process.
	 *
	 * First we close the standard file descriptors so we don't
	 * inherit them directly from init but get to pick them ourselves
	 */
	for (i = 0; i < 3; i++)
		close (i);

	/* Reset the signal mask, and put all signal handlers back to their
	 * default handling so the child isn't unexpectantly ignoring them
	 */
	sigprocmask (SIG_SETMASK, &orig_set, NULL);
	nih_signal_reset ();

	/* Become the leader of a new session and process group */
	setsid ();

	/* The job defines what the process's standard input, output and
	 * error file descriptors should look like; set those up.
	 */
	if (process_setup_console (job) < 0)
		goto error;

	/* The job also gives us a consistent world to run all processes
	 * in, including resource limits and suchlike.  Set that all up.
	 */
	if (process_setup_limits (job) < 0)
		goto error;

	/* And finally set up the environment variables for the process.
	 */
	if (process_setup_environment (job) < 0)
		goto error;

	/* Execute the process, if we escape from here it failed */
	if (execvp (argv[0], argv) < 0)
		nih_error_raise_system ();

error:
	err = nih_error_get ();

	nih_error (_("Unable to execute \"%s\" for %s: %s"),
		   argv[0], job->name, err->message);
	nih_free (err);

	exit (1);
}

/**
 * process_setup_console:
 * @job: job details.
 *
 * Set up the standard input, output and error file descriptors for the
 * current process based on the console member of the @job given.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
process_setup_console (Job *job)
{
	int fd;

	nih_assert (job != NULL);

	switch (job->console) {
	case CONSOLE_OUTPUT:
	case CONSOLE_OWNER:
		/* Open the console itself */
		/* FIXME need to do this properly */
		fd = open (CONSOLE, O_RDWR|O_NOCTTY);
		if (fd >= 0) {
			/* Take ownership of the console */
			if (job->console == CONSOLE_OWNER)
				ioctl (fd, TIOCSCTTY, 1);

			/*
			reset_console ();
			*/

			break;
		}

		/* Open failed, fall through to CONSOLE_NONE handling */
		nih_warn (_("Unable to open console for %s: %s"), job->name,
			  strerror (errno));

	case CONSOLE_NONE:
	case CONSOLE_LOGGED:
		fd = open (DEV_NULL, O_RDWR);
		if (fd < 0)
			nih_return_system_error (-1);

		break;
	}

	/* Copy to standard output and standard error */
	dup (fd);
	dup (fd);

	return 0;
}

/**
 * process_setup_limits:
 * @job: job details.
 *
 * Set up the boundaries for the current process such as the file creation
 * mask, priority, limits, working directory, etc. from the details stored
 * in the @job given.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
process_setup_limits (Job *job)
{
	int i;

	nih_assert (job != NULL);

	umask (job->umask);

	if (setpriority (PRIO_PROCESS, 0, job->nice) < 0)
		nih_return_system_error (-1);

	for (i = 0; i < RLIMIT_NLIMITS; i++) {
		if (! job->limits[i])
			continue;

		if (setrlimit (i, job->limits[i]) < 0)
			nih_return_system_error (-1);
	}

	if (job->chroot) {
		if (chroot (job->chroot) < 0)
			nih_return_system_error (-1);
		if (chdir ("/") < 0)
			nih_return_system_error (-1);
	}

	if (job->chdir) {
		if (chdir (job->chdir) < 0)
			nih_return_system_error (-1);
	}

	return 0;
}

/**
 * process_setup_environment:
 * @job: job details.
 *
 * Set up the environment variables for the current process based on the
 * the details in @job.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
process_setup_environment (Job *job)
{
	char **env;

	nih_assert (job != NULL);

	if (clearenv () < 0)
		nih_return_system_error (-1);

	for (env = job->env; env && *env; env++)
		if (putenv (*env) < 0)
			nih_return_system_error (-1);

	return 0;
}


/**
 * process_kill:
 * @job: job context of process to be killed,
 * @pid: process id of process,
 * @force: force the death.
 *
 * This function only sends the process a TERM signal (KILL if @force is
 * TRUE), it is up to the caller to ensure that the state change is saved
 * into the job and the process is watched; one may also wish to send
 * further signals later.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
process_kill (Job   *job,
	      pid_t  pid,
	      int    force)
{
	int signal;

	nih_assert (job != NULL);
	nih_assert (pid > 0);

	signal = (force ? SIGKILL : SIGTERM);

	if (kill (pid, signal) < 0)
		nih_return_system_error (-1);

	return 0;
}
