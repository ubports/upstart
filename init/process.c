/* upstart
 *
 * process.c - job process handling
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
#include "errors.h"


/**
 * ProcessWireError:
 *
 * This structure is used to pass an error from the child process back to the
 * parent.  It contains the same basic particulars as a ProcessError but
 * without the message.
 **/
typedef struct process_wire_error {
	ProcessErrorType type;
	int              arg;
	int              errnum;
} ProcessWireError;


/* Prototypes for static functions */
static void process_error_abort       (int fd, ProcessErrorType type, int arg)
	__attribute__ ((noreturn));
static int  process_error_read        (int fd)
	__attribute__ ((warn_unused_result));


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
 * Spawning a process may fail for temporary reasons, usually due to a failure
 * of the fork() syscall or communication with the child; or more permanent
 * reasons such as a failure to setup the child environment.  These latter
 * are always represented by a PROCESS_ERROR error.
 *
 * Returns: process id of new process on success, -1 on raised error
 **/
pid_t
process_spawn (Job          *job,
	       char * const  argv[])
{
	sigset_t child_set, orig_set;
	pid_t    pid;
	int      i, fds[2];

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

		/* Read error from the pipe, return if one is raised */
		if (process_error_read (fds[0]) < 0) {
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
		process_error_abort (fds[1], PROCESS_ERROR_CONSOLE, 0);

	/* Set resource limits for the process, skipping over any that
	 * aren't set in the job configuration such that they inherit from
	 * ourselves (and we inherit from kernel defaults).
	 */
	for (i = 0; i < RLIMIT_NLIMITS; i++) {
		if (! job->config->limits[i])
			continue;

		if (setrlimit (i, job->config->limits[i]) < 0) {
			nih_error_raise_system ();
			process_error_abort (fds[1], PROCESS_ERROR_RLIMIT, i);
		}
	}

	/* Set the process environment, taking environment variables from
	 * the job definition, the events that started/stopped it and also
	 * include the standard ones that tell you which job you are.
	 */
	NIH_MUST (environ = process_environment (job));

	/* Set the file mode creation mask; this is one of the few operations
	 * that can never fail.
	 */
	umask (job->config->umask);

	/* Adjust the process priority ("nice level").
	 */
	if (setpriority (PRIO_PROCESS, 0, job->config->nice) < 0) {
		nih_error_raise_system ();
		process_error_abort (fds[1], PROCESS_ERROR_PRIORITY, 0);
	}

	/* Change the root directory, confining path resolution within it;
	 * we do this before the working directory call so that is always
	 * relative to the new root.
	 */
	if (job->config->chroot) {
		if (chroot (job->config->chroot) < 0) {
			nih_error_raise_system ();
			process_error_abort (fds[1], PROCESS_ERROR_CHROOT, 0);
		}
	}

	/* Change the working directory of the process, either to the one
	 * configured in the job, or to the root directory of the filesystem
	 * (or at least relative to the chroot).
	 */
	if (chdir (job->config->chdir ? job->config->chdir : "/") < 0) {
		nih_error_raise_system ();
		process_error_abort (fds[1], PROCESS_ERROR_CHDIR, 0);
	}


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
			process_error_abort (fds[1], PROCESS_ERROR_PTRACE, 0);
		}
	}

	/* Execute the process, if we escape from here it failed */
	if (execvp (argv[0], argv) < 0) {
		nih_error_raise_system ();
		process_error_abort (fds[1], PROCESS_ERROR_EXEC, 0);
	}

	nih_assert_not_reached ();
}

/**
 * process_error_abort:
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
process_error_abort (int              fd,
		     ProcessErrorType type,
		     int              arg)
{
	NihError         *err;
	ProcessWireError  wire_err;

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
 * process_error_read:
 * @fd: reading end of pipe.
 *
 * Read from the reading end of the pipe specified by @fd, if we receive
 * data then the child raised a process error which we reconstruct and raise
 * again; otherwise no problem was found and no action is taken.
 *
 * The reconstructed error will be of PROCESS_ERROR type, the human-readable
 * message is generated according to the type of process error and argument
 * passed along with it.
 *
 * Returns: zero if no error was found, or negative value on raised error.
 **/
static int
process_error_read (int fd)
{
	ProcessWireError  wire_err;
	ssize_t           len;
	ProcessError     *err;
	const char       *res;

	/* Read the error from the pipe; a zero read indicates that the
	 * exec succeeded so we return success, otherwise if we don't receive
	 * a ProcessWireError structure, we return a temporary error so we
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

	/* Construct a ProcessError to be raised containing information
	 * from the wire, augmented with human-readable information we
	 * generate here.
	 */
	NIH_MUST (err = nih_new (NULL, ProcessError));

	err->type = wire_err.type;
	err->arg = wire_err.arg;
	err->errnum = wire_err.errnum;

	err->error.number = PROCESS_ERROR;

	switch (err->type) {
	case PROCESS_ERROR_CONSOLE:
		NIH_MUST (err->error.message = nih_sprintf (
				  err, _("unable to open console: %s"),
				  strerror (err->errnum)));
		break;
	case PROCESS_ERROR_RLIMIT:
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
	case PROCESS_ERROR_ENVIRON:
		NIH_MUST (err->error.message = nih_sprintf (
				  err, _("unable to set up environment: %s"),
				  strerror (err->errnum)));
		break;
	case PROCESS_ERROR_PRIORITY:
		NIH_MUST (err->error.message = nih_sprintf (
				  err, _("unable to set priority: %s"),
				  strerror (err->errnum)));
		break;
	case PROCESS_ERROR_CHROOT:
		NIH_MUST (err->error.message = nih_sprintf (
				  err, _("unable to change root directory: %s"),
				  strerror (err->errnum)));
		break;
	case PROCESS_ERROR_CHDIR:
		NIH_MUST (err->error.message = nih_sprintf (
				  err, _("unable to change working directory: %s"),
				  strerror (err->errnum)));
		break;
	case PROCESS_ERROR_PTRACE:
		NIH_MUST (err->error.message = nih_sprintf (
				  err, _("unable to set trace: %s"),
				  strerror (err->errnum)));
		break;
	case PROCESS_ERROR_EXEC:
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


/**
 * process_environment:
 * @job: job to construct environment for.
 *
 * Constructs an environment table containing information about @job
 * and the event(s) that started it, including their own environment
 * variables.
 *
 * This table is suitable for storing in @job's env member so that it is
 * used for all processes spawned by the job.
 *
 * The returned table is an nih_alloc() child of @job.
 *
 * Returns: new environment table or NULL on insufficient memory.
 **/
char **
process_environment (Job *job)
{
	const char  *builtin[] = { PROCESS_ENVIRONMENT, NULL };
	char       **e;
	char       **env, *value;
	size_t       len;

	nih_assert (job != NULL);

	len = 0;
	env = nih_str_array_new (job);
	if (! env)
		return NULL;

	/* Copy the builtin set of environment variables, usually these just
	 * pick up the values from init's own environment.
	 */
	for (e = (char **)builtin; e && *e; e++)
		if (! process_environment_add (&env, job, &len, *e))
			goto error;

	/* Copy the set of environment variables from the job configuration,
	 * these often have values but also often don't and we want them to
	 * override the builtins.
	 */
	for (e = job->config->env; e && *e; e++)
		if (! process_environment_add (&env, job, &len, *e))
			goto error;

	/* Copy the environment variables from the event(s) that started the
	 * job, these always have values and override those builtin and in
	 * the job configuration (so those provide defaults).  Keep a record
	 * of which events we copy in, and then provide those in the special
	 * UPSTART_EVENTS variable.
	 */
	if (job->start_on) {
		char   *list, *new_list;
		size_t  listsz;
		int     found = FALSE;

		list = nih_strdup (NULL, "UPSTART_EVENTS");
		if (! list)
			goto error;

		listsz = strlen (list);

		NIH_TREE_FOREACH (&job->start_on->node, iter) {
			EventOperator *oper = (EventOperator *)iter;

			if ((oper->type != EVENT_MATCH) || (! oper->value)
			    || (oper->event == NULL))
				continue;

			/* Append the name of the event to the list that we
			 * build to store in the UPSTART_EVENTS variable.
			 */
			new_list = nih_realloc (
				list, NULL,
				listsz + strlen (oper->event->name) + 2);
			if (! new_list) {
				nih_free (list);
				goto error;
			}

			list = new_list;
			list[listsz++] = (found ? ' ' : '=');
			list[listsz] = '\0';

			strcat (list, oper->event->name);
			listsz += strlen (oper->event->name);

			/* Add the environment from the event as well */
			for (e = oper->event->env; e && *e; e++) {
				if (! process_environment_add (&env, job,
							       &len, *e)) {
					nih_free (list);
					goto error;
				}
			}

			found = TRUE;
		}

		/* Append the UPSTART_EVENTS variable to the environment */
		if (found) {
			if (! process_environment_add (&env, job, &len, list)) {
				nih_free (list);
				goto error;
			}
		}

		nih_free (list);
	}

	/* Place the job name in the UPSTART_JOB environment variable; this
	 * is useful for scripts that want to know who they're being run by
	 * and cannot be overriden.
	 */
	value = nih_sprintf (NULL, "UPSTART_JOB=%s", job->config->name);
	if (! value)
		goto error;
	if (! process_environment_add (&env, job, &len, value)) {
		nih_free (value);
		goto error;
	}
	nih_free (value);

	/* Place the job id in the UPSTART_JOB_ID environment variable; this
	 * is used by initctl when no other arguments are given, so it changes
	 * the actual job being run -- again cannot be overriden.
	 */
	value = nih_sprintf (NULL, "UPSTART_JOB_ID=%u", job->id);
	if (! value)
		goto error;
	if (! process_environment_add (&env, job, &len, value)) {
		nih_free (value);
		goto error;
	}
	nih_free (value);

	return env;

error:
	nih_free (env);
	return NULL;
}

/**
 * process_environment_add:
 * @env: pointer to environment table,
 * @parent: parent of @env,
 * @len: length of @env,
 * @str: string to add.
 *
 * Add the new environment variable @str to the table @env (which has @len
 * elements, excluding the final NULL element), either replacing an existing
 * entry or appended to the end.  Both the array and the new string within it
 * are allocated using nih_alloc(), @parent must be that of @env.
 *
 * @str may be in KEY=VALUE format, in which case the given key will be
 * replaced with that value or appended to the table; or it may simply
 * be in KEY format, which which case the value is taken from init's own
 * environment.
 *
 * @len will be updated to contain the new array length and @env will
 * be updated to point to the new array pointer; use the return value
 * simply to check for success.
 *
 * Returns: new array pointer or NULL if insufficient memory.
 **/
char **
process_environment_add (char       ***env,
			 const void   *parent,
			 size_t       *len,
			 const char   *str)
{
	size_t   key;
	char   **e, *new_str;

	nih_assert (env != NULL);
	nih_assert (len != NULL);
	nih_assert (str != NULL);

	/* Calculate the length of the key in the string, if we reach the
	 * end of the string, then we lookup the value in the environment
	 * and use that as the new value otherwise use the string given.
	 */
	key = strcspn (str, "=");
	if (str[key] == '=') {
		new_str = nih_strdup (*env, str);
		if (! new_str)
			return NULL;
	} else {
		const char *value;

		value = getenv (str);
		if (value) {
			new_str = nih_sprintf (*env, "%s=%s", str, value);
			if (! new_str)
				return NULL;
		} else {
			new_str = NULL;
		}
	}

	/* Check the environment table for an existing entry for the key,
	 * if we find one we overwrite it instead of extending the table.
	 */
	for (e = *env; *e; e++) {
		if ((strncmp (*e, str, key) == 0) && ((*e)[key] == '=')) {
			nih_free (*e);

			if (new_str) {
				*e = new_str;
			} else {
				memmove (e, e + 1,
					 (char *)(*env + *len) - (char *)e);
				(*len)--;
			}

			return *env;
		}
	}

	/* No existing entry exists so extend the table instead.
	 */
	if (new_str) {
		if (! nih_str_array_addp (env, parent, len, new_str)) {
			nih_free (new_str);
			return NULL;
		}
	}

	return *env;
}
