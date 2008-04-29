/* upstart
 *
 * job_process.c - job process handling
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
#include <sys/ptrace.h>
#include <sys/resource.h>

#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/signal.h>
#include <nih/io.h>
#include <nih/logging.h>
#include <nih/error.h>

#include "paths.h"
#include "system.h"
#include "job_process.h"
#include "job_class.h"
#include "errors.h"


/**
 * JobProcessWireError:
 *
 * This structure is used to pass an error from the child process back to the
 * parent.  It contains the same basic particulars as a JobProcessError but
 * without the message.
 **/
typedef struct job_process_wire_error {
	JobProcessErrorType type;
	int                 arg;
	int                 errnum;
} JobProcessWireError;


/* Prototypes for static functions */
static void job_process_error_abort (int fd, JobProcessErrorType type, int arg)
	__attribute__ ((noreturn));
static int  job_process_error_read  (int fd)
	__attribute__ ((warn_unused_result));


/**
 * job_process_spawn:
 * @class: job class of process to be spawned,
 * @argv: NULL-terminated list of arguments for the process,
 * @env: NULL-terminated list of environment variables for the process,
 * @trace: whether to trace this process.
 *
 * This function spawns a new process using the @class details to set up the
 * environment for it; the process is always a session and process group
 * leader as we never want anything in our own group.
 *
 * The process to be executed is given in the @argv array which is passed
 * directly to execvp(), so should be in the same NULL-terminated form with
 * the first argument containing the path or filename of the binary.  The
 * PATH environment in @config will be searched.
 *
 * If @trace is TRUE, the process will be traced with ptrace and this will
 * cause the process to be stopped when the exec() call is made.  You must
 * wait for this and then may use it to set options before continuing the
 * process.
 *
 * This function only spawns the process, it is up to the caller to ensure
 * that the information is saved into the job and that the process is watched,
 * etc.
 *
 * Spawning a process may fail for temporary reasons, usually due to a failure
 * of the fork() syscall or communication with the child; or more permanent
 * reasons such as a failure to setup the child environment.  These latter
 * are always represented by a JOB_PROCESS_ERROR error.
 *
 * Returns: process id of new process on success, -1 on raised error
 **/
pid_t
job_process_spawn (JobClass     *class,
		   char * const  argv[],
		   char * const *env,
		   int           trace)
{
	sigset_t  child_set, orig_set;
	pid_t     pid;
	int       i, fds[2];
	char      filename[PATH_MAX];
	FILE     *fd;

	nih_assert (class != NULL);

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

		/* If the job is not a session leader, it will fork twice,
		 * so we need to read the new child pid.
		 */
		if (! class->leader) {
			ssize_t len;

			len = read (fds[0], &pid, sizeof (pid));
			nih_assert (len == sizeof (pid));
		}

		/* Read error from the pipe, return if one is raised */
		if (job_process_error_read (fds[0]) < 0) {
			close (fds[0]);
			return -1;
		}

		close (fds[0]);
		return pid;
	} else if (pid < 0) {
		nih_error_raise_system ();

		sigprocmask (SIG_SETMASK, &orig_set, NULL);
		close (fds[0]);
		close (fds[1]);
		return -1;
	}


	/* We're now in the child process.
	 *
	 * The rest of this function sets the child up and ends by executing
	 * the new binary.  Failures are handled by terminating the child
	 * and writing an error back to the parent.
	 */

	/* Close the reading end of the pipe with our parent and mark the
	 * writing end to be closed-on-exec so the parent knows we got that
	 * far because read() returned zero.
	 */
	close (fds[0]);
	nih_io_set_cloexec (fds[1]);

	/* Become the leader of a new session and process group, shedding
	 * any controlling tty (which we shouldn't have had anyway).  If
	 * the process is not supposed to be a session leader, we need to
	 * fork again and write back our new pid.
	 */
	setsid ();
	if (! class->leader) {
		pid = fork ();
		if (pid > 0) {
			exit (0);
		} else if (pid < 0) {
			nih_error_raise_system ();

			/* Write -1 as the pid to the parent followed by
			 * the error.
			 */
			write (fds[1], &pid, sizeof (pid));
			job_process_error_abort (fds[1],
						 JOB_PROCESS_ERROR_FORK, 0);
		}

		/* Write the new child pid to the parent */
		pid = getpid ();
		write (fds[1], &pid, sizeof (pid));
	}

	/* Set the standard file descriptors to an output of our chosing;
	 * any other open descriptor must be intended for the child, or have
	 * the FD_CLOEXEC flag so it's automatically closed when we exec()
	 * later.
	 */
	if (system_setup_console (class->console, FALSE) < 0)
		job_process_error_abort (fds[1], JOB_PROCESS_ERROR_CONSOLE, 0);

	/* Set resource limits for the process, skipping over any that
	 * aren't set in the job configuration such that they inherit from
	 * ourselves (and we inherit from kernel defaults).
	 */
	for (i = 0; i < RLIMIT_NLIMITS; i++) {
		if (! class->limits[i])
			continue;

		if (setrlimit (i, class->limits[i]) < 0) {
			nih_error_raise_system ();
			job_process_error_abort (fds[1],
						 JOB_PROCESS_ERROR_RLIMIT, i);
		}
	}

	/* Set the process environment from the function paramters. */
	environ = (char **)env;

	/* Set the file mode creation mask; this is one of the few operations
	 * that can never fail.
	 */
	umask (class->umask);

	/* Adjust the process priority ("nice level").
	 */
	if (setpriority (PRIO_PROCESS, 0, class->nice) < 0) {
		nih_error_raise_system ();
		job_process_error_abort (fds[1],
					 JOB_PROCESS_ERROR_PRIORITY, 0);
	}

	/* Adjust the process OOM killer priority.
	 */
	snprintf (filename, sizeof (filename), "/proc/%d/oom_adj", getpid ());

	fd = fopen (filename, "w");
	if (! fd) {
		nih_error_raise_system ();
		job_process_error_abort (fds[1], JOB_PROCESS_ERROR_OOM_ADJ, 0);
	}

	fprintf (fd, "%d\n", class->oom_adj);

	if (fclose (fd)) {
		nih_error_raise_system ();
		job_process_error_abort (fds[1], JOB_PROCESS_ERROR_OOM_ADJ, 0);
	}

	/* Change the root directory, confining path resolution within it;
	 * we do this before the working directory call so that is always
	 * relative to the new root.
	 */
	if (class->chroot) {
		if (chroot (class->chroot) < 0) {
			nih_error_raise_system ();
			job_process_error_abort (fds[1],
						 JOB_PROCESS_ERROR_CHROOT, 0);
		}
	}

	/* Change the working directory of the process, either to the one
	 * configured in the job, or to the root directory of the filesystem
	 * (or at least relative to the chroot).
	 */
	if (chdir (class->chdir ? class->chdir : "/") < 0) {
		nih_error_raise_system ();
		job_process_error_abort (fds[1], JOB_PROCESS_ERROR_CHDIR, 0);
	}


	/* Reset all the signal handlers back to their default handling so
	 * the child isn't unexpectedly ignoring any, and so we won't
	 * surprisingly handle them before we've exec()d the new process.
	 */
	nih_signal_reset ();
	sigprocmask (SIG_SETMASK, &orig_set, NULL);

	/* Set up a process trace if we need to trace forks */
	if (trace) {
		if (ptrace (PTRACE_TRACEME, 0, NULL, 0) < 0) {
			nih_error_raise_system();
			job_process_error_abort (fds[1],
						 JOB_PROCESS_ERROR_PTRACE, 0);
		}
	}

	/* Execute the process, if we escape from here it failed */
	if (execvp (argv[0], argv) < 0) {
		nih_error_raise_system ();
		job_process_error_abort (fds[1], JOB_PROCESS_ERROR_EXEC, 0);
	}

	nih_assert_not_reached ();
}

/**
 * job_process_error_abort:
 * @fd: writing end of pipe,
 * @type: step that failed,
 * @arg: argument to @type.
 *
 * Abort the child process, first writing the error details in @type, @arg
 * and the currently raised NihError to the writing end of the pipe specified
 * by @fd.
 *
 * This function calls the abort() system call, so never returns.
 **/
static void
job_process_error_abort (int                 fd,
			 JobProcessErrorType type,
			 int                 arg)
{
	NihError            *err;
	JobProcessWireError  wire_err;

	/* Get the currently raised system error */
	err = nih_error_get ();

	/* Fill in the structure we send over the pipe */
	wire_err.type = type;
	wire_err.arg = arg;
	wire_err.errnum = err->number;

	/* Write structure to the pipe; in theory this should never fail, but
	 * if it does, we abort anyway.
	 */
	write (fd, &wire_err, sizeof (wire_err));

	abort ();
}

/**
 * job_process_error_read:
 * @fd: reading end of pipe.
 *
 * Read from the reading end of the pipe specified by @fd, if we receive
 * data then the child raised a process error which we reconstruct and raise
 * again; otherwise no problem was found and no action is taken.
 *
 * The reconstructed error will be of JOB_PROCESS_ERROR type, the human-
 * readable message is generated according to the type of process error
 * and argument passed along with it.
 *
 * Returns: zero if no error was found, or negative value on raised error.
 **/
static int
job_process_error_read (int fd)
{
	JobProcessWireError  wire_err;
	ssize_t              len;
	JobProcessError     *err;
	const char          *res;

	/* Read the error from the pipe; a zero read indicates that the
	 * exec succeeded so we return success, otherwise if we don't receive
	 * a JobProcessWireError structure, we return a temporary error so we
	 * try again.
	 */
	len = read (fd, &wire_err, sizeof (wire_err));
	if (len == 0) {
		return 0;
	} else if (len < 0) {
		nih_return_system_error (-1);
	} else if (len != sizeof (wire_err)) {
		errno = EILSEQ;
		nih_return_system_error (-1);
	}

	/* Construct a JobProcessError to be raised containing information
	 * from the wire, augmented with human-readable information we
	 * generate here.
	 */
	NIH_MUST (err = nih_new (NULL, JobProcessError));

	err->type = wire_err.type;
	err->arg = wire_err.arg;
	err->errnum = wire_err.errnum;

	err->error.number = JOB_PROCESS_ERROR;

	switch (err->type) {
	case JOB_PROCESS_ERROR_FORK:
		NIH_MUST (err->error.message = nih_sprintf (
				  err, _("unable to fork: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_CONSOLE:
		NIH_MUST (err->error.message = nih_sprintf (
				  err, _("unable to open console: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_RLIMIT:
		switch (err->arg) {
		case RLIMIT_CPU:
			res = "cpu";
			break;
		case RLIMIT_FSIZE:
			res = "fsize";
			break;
		case RLIMIT_DATA:
			res = "data";
			break;
		case RLIMIT_STACK:
			res = "stack";
			break;
		case RLIMIT_CORE:
			res = "core";
			break;
		case RLIMIT_RSS:
			res = "rss";
			break;
		case RLIMIT_NPROC:
			res = "nproc";
			break;
		case RLIMIT_NOFILE:
			res = "nofile";
			break;
		case RLIMIT_MEMLOCK:
			res = "memlock";
			break;
		case RLIMIT_AS:
			res = "as";
			break;
		case RLIMIT_LOCKS:
			res = "locks";
			break;
		case RLIMIT_SIGPENDING:
			res = "sigpending";
			break;
		case RLIMIT_MSGQUEUE:
			res = "msgqueue";
			break;
		case RLIMIT_NICE:
			res = "nice";
			break;
		case RLIMIT_RTPRIO:
			res = "rtprio";
			break;
		default:
			nih_assert_not_reached ();
		}

		NIH_MUST (err->error.message = nih_sprintf (
				  err, _("unable to set \"%s\" resource limit: %s"),
				  res, strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_PRIORITY:
		NIH_MUST (err->error.message = nih_sprintf (
				  err, _("unable to set priority: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_OOM_ADJ:
		NIH_MUST (err->error.message = nih_sprintf (
				  err, _("unable to set oom adjustment: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_CHROOT:
		NIH_MUST (err->error.message = nih_sprintf (
				  err, _("unable to change root directory: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_CHDIR:
		NIH_MUST (err->error.message = nih_sprintf (
				  err, _("unable to change working directory: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_PTRACE:
		NIH_MUST (err->error.message = nih_sprintf (
				  err, _("unable to set trace: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_EXEC:
		NIH_MUST (err->error.message = nih_sprintf (
				  err, _("unable to execute: %s"),
				  strerror (err->errnum)));
		break;
	default:
		nih_assert_not_reached ();
	}

	nih_error_raise_again (&err->error);
	return -1;
}
