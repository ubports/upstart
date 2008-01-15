/* upstart
 *
 * process.c - job process handling
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
#include <sys/ioctl.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/signal.h>
#include <nih/io.h>
#include <nih/main.h>
#include <nih/logging.h>
#include <nih/error.h>

#include "job.h"
#include "process.h"
#include "paths.h"


/* Prototypes for static functions */
static int process_setup_environment (Job *job);


/**
 * process_spawn:
 * @job: job context for process to be spawned in,
 * @argv: NULL-terminated list of arguments for the process.
 *
 * This function spawns a new process using the @job details to set up the
 * environment for it; the process is always a session and process group
 * leader as we never want anything in our own group.
 *
 * The process to be executed is given in the @argv array which is passed
 * directly to execvp(), so should be in the same NULL-terminated form with
 * the first argument containing the path or filename of the binary.  The
 * PATH environment in @job will be searched.
 *
 * This function only spawns the process, it is up to the caller to ensure
 * that the information is saved into the job and the process is watched, etc.
 *
 * Note that the only error raised within this function is a failure of the
 * fork() syscall as the environment is set up within the child process.
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
	int       i, fds[2];

	nih_assert (job != NULL);

	/* Create a pipe to communicate with the child process until it
	 * execs so we know whether that was successful or an error occurred.
	 */
	if (pipe (fds) < 0)
		nih_return_system_error (-1);

	/* Block all signals while we fork to avoid the child process running
	 * our own signal handlers before we've reset them all back to the
	 * default.
	 */
	sigfillset (&child_set);
	sigprocmask (SIG_BLOCK, &child_set, &orig_set);

	/* Fork the child process, handling success and failure by resetting
	 * the signal mask and returning the new process id or a raised error.
	 */
	pid = fork ();
	if (pid > 0) {
		sigprocmask (SIG_SETMASK, &orig_set, NULL);
		close (fds[1]);

		/* FIXME read the error from the child */
		close (fds[0]);

		return pid;
	} else if (pid < 0) {
		nih_error_raise_system ();

		sigprocmask (SIG_SETMASK, &orig_set, NULL);
		return -1;
	}


	/* We're now in the child process.
	 *
	 * The reset of this function sets the child up and ends by executing
	 * the new binary.  Failures are handled by terminating the child.
	 */

	/* Close the reading end of the pipe with our parent and mark the
	 * writing end to be closed-on-exec so the parent knows we got that
	 * far because read() returned zero.
	 */
	close (fds[0]);
	nih_io_set_cloexec (fds[1]);

	/* Become the leader of a new session and process group, shedding
	 * any controlling tty (which we shouldn't have had anyway).
	 */
	setsid ();

	/* Set the standard file descriptors to an output of our chosing;
	 * any other open descriptor must be intended for the child, or have
	 * the FD_CLOEXEC flag so it's automatically closed when we exec()
	 * later.
	 */
	if (process_setup_console (job->config->console, FALSE) < 0)
		goto error;

	/* Set the process environment, taking environment variables from
	 * the job definition, the events that started/stopped it and also
	 * include the standard ones that tell you which job you are.
	 */
	if (process_setup_environment (job) < 0)
		goto error;


	/* Set the file mode creation mask; this is one of the few operations
	 * that can never fail.
	 */
	umask (job->config->umask);

	/* Adjust the process priority ("nice level").
	 */
	if (setpriority (PRIO_PROCESS, 0, job->config->nice) < 0)
		goto error;

	/* Set resource limits for the process, skipping over any that
	 * aren't set in the job configuration such that they inherit from
	 * ourselves (and we inherit from kernel defaults).
	 */
	for (i = 0; i < RLIMIT_NLIMITS; i++) {
		if (! job->config->limits[i])
			continue;

		if (setrlimit (i, job->config->limits[i]) < 0)
			goto error;
	}

	/* Change the root directory, confining path resolution within it;
	 * we do this before the working directory call so that is always
	 * relative to the new root.
	 */
	if (job->config->chroot) {
		if (chroot (job->config->chroot) < 0)
			goto error;
	}

	/* Change the working directory of the process, either to the one
	 * configured in the job, or to the root directory of the filesystem
	 * (or at least relative to the chroot).
	 */
	if (chdir (job->config->chdir ? job->config->chdir : "/") < 0)
		nih_return_system_error (-1);


	/* Reset all the signal handlers back to their default handling so
	 * the child isn't unexpectedly ignoring any, and so we won't
	 * surprisingly handle them before we've exec()d the new process.
	 */
	nih_signal_reset ();
	sigprocmask (SIG_SETMASK, &orig_set, NULL);

	/* Set up a process trace if we need to trace forks */
	if (job->trace_state == TRACE_NEW) {
		if (ptrace (PTRACE_TRACEME, 0, NULL, 0) < 0) {
			nih_error_raise_system();
			goto error;
		}
	}

	/* Execute the process, if we escape from here it failed */
	if (execvp (argv[0], argv) < 0)
		nih_error_raise_system ();

error:
	err = nih_error_get ();

	nih_error (_("Unable to execute \"%s\" for %s (#%u): %s"),
		   argv[0], job->config->name, job->id, err->message);
	nih_free (err);

	exit (255);
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
	char  *path, *term, *jobid;

	nih_assert (job != NULL);

	/* Inherit PATH and TERM from our parent's environment, everything
	 * else is often just overspill from initramfs.
	 */
	NIH_MUST (path = nih_strdup (NULL, getenv ("PATH")));
	NIH_MUST (term = nih_strdup (NULL, getenv ("TERM")));

	if (clearenv () < 0)
		nih_return_system_error (-1);

	if (path) {
		if (setenv ("PATH", path, TRUE) < 0)
			nih_return_system_error (-1);
		nih_free (path);
	}

	if (term) {
		if (setenv ("TERM", term, TRUE) < 0)
			nih_return_system_error (-1);
		nih_free (term);
	}

	NIH_MUST (jobid = nih_sprintf (NULL, "%u", job->id));
	if (setenv ("UPSTART_JOB_ID", jobid, TRUE) < 0)
		nih_return_system_error (-1);
	nih_free (jobid);

	if (setenv ("UPSTART_JOB", job->config->name, TRUE) < 0)
		nih_return_system_error (-1);

	/* Put all environment from the job's start events */
	if (job->start_on) {
		NIH_TREE_FOREACH (&job->start_on->node, iter) {
			EventOperator *oper = (EventOperator *)iter;

			if ((oper->type == EVENT_MATCH)
			    && oper->value
			    && oper->event)
				for (env = oper->event->env;
				     env && *env; env++)
					if (putenv (*env) < 0)
						nih_return_system_error (-1);
		}
	}

	for (env = job->config->env; env && *env; env++)
		if (putenv (*env) < 0)
			nih_return_system_error (-1);

	return 0;
}


/**
 * process_setup_console:
 * @type: console type,
 * @reset: reset console to sane defaults.
 *
 * Set up the standard input, output and error file descriptors for the
 * current process based on the console @type given.  If @reset is TRUE then
 * the console device will be reset to sane defaults.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
process_setup_console (ConsoleType type,
		       int         reset)
{
	int fd = -1, i;

	/* Close the standard file descriptors since we're about to re-open
	 * them; it may be that some of these aren't already open, we get
	 * called in some very strange ways.
	 */
	for (i = 0; i < 3; i++)
		close (i);

	/* Open the new first file descriptor, which should always become
	 * file zero.
	 */
	switch (type) {
	case CONSOLE_OUTPUT:
	case CONSOLE_OWNER:
		/* Ordinary console input and output */
		fd = open (CONSOLE, O_RDWR | O_NOCTTY);
		if (fd < 0)
			nih_return_system_error (-1);

		if (type == CONSOLE_OWNER)
			ioctl (fd, TIOCSCTTY, 1);
		break;
	case CONSOLE_NONE:
		/* No console really means /dev/null */
		fd = open (DEV_NULL, O_RDWR | O_NOCTTY);
		if (fd < 0)
			nih_return_system_error (-1);
		break;
	}

	/* Reset to sane defaults, cribbed from sysvinit, initng, etc. */
	if (reset) {
		struct termios tty;

		tcgetattr (0, &tty);

		tty.c_cflag &= (CBAUD | CBAUDEX | CSIZE | CSTOPB
				| PARENB | PARODD);
		tty.c_cflag |= (HUPCL | CLOCAL | CREAD);

		/* Set up usual keys */
		tty.c_cc[VINTR]  = 3;   /* ^C */
		tty.c_cc[VQUIT]  = 28;  /* ^\ */
		tty.c_cc[VERASE] = 127;
		tty.c_cc[VKILL]  = 24;  /* ^X */
		tty.c_cc[VEOF]   = 4;   /* ^D */
		tty.c_cc[VTIME]  = 0;
		tty.c_cc[VMIN]   = 1;
		tty.c_cc[VSTART] = 17;  /* ^Q */
		tty.c_cc[VSTOP]  = 19;  /* ^S */
		tty.c_cc[VSUSP]  = 26;  /* ^Z */

		/* Pre and post processing */
		tty.c_iflag = (IGNPAR | ICRNL | IXON | IXANY);
		tty.c_oflag = (OPOST | ONLCR);
		tty.c_lflag = (ISIG | ICANON | ECHO | ECHOCTL
			       | ECHOPRT | ECHOKE);

		/* Set the terminal line and flush it */
		tcsetattr (0, TCSANOW, &tty);
		tcflush (0, TCIOFLUSH);
	}

	/* Copy to standard output and standard error */
	while (dup (fd) < 2)
		;

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

	if (kill (-pid, signal) < 0)
		nih_return_system_error (-1);

	return 0;
}
