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
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/signal.h>
#include <nih/main.h>
#include <nih/logging.h>
#include <nih/error.h>

#include "job.h"
#include "process.h"
#include "paths.h"


/* Prototypes for static functions */
static int process_setup_limits      (Job *job);
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
	int       i;

	nih_assert (job != NULL);

	/* Block signals before we fork to avoid child running our signal
	 * handlers before it's reset them.
	 */
	sigfillset (&child_set);
	sigprocmask (SIG_BLOCK, &child_set, &orig_set);

	/* Fork the child process, and return either the id or failure
	 * back to the caller.
	 */
	pid = fork ();
	if (pid > 0) {
		sigprocmask (SIG_SETMASK, &orig_set, NULL);

		nih_debug ("Spawned process %d for %s (#%u)",
			   pid, job->config->name, job->id);
		return pid;
	} else if (pid < 0) {
		nih_error_raise_system ();

		sigprocmask (SIG_SETMASK, &orig_set, NULL);
		return -1;
	}


	/* We're now in the child process.
	 *
	 * First we put all the signal handlers back to their default
	 * handling; this means the child won't be unexpectedly ignoring
	 * them and means we won't surprisingly handle them before we've
	 * exec()d the new process.
	 */
	nih_signal_reset ();
	sigprocmask (SIG_SETMASK, &orig_set, NULL);

	/* Next we close the standard file descriptors so we don't
	 * inherit them directly from init but get to pick them ourselves.
	 *
	 * Any other open descriptor must have had the FD_CLOEXEC flag set,
	 * so will get automatically closed when we exec() later.
	 */
	for (i = 0; i < 3; i++)
		close (i);

	/* Become the leader of a new session and process group */
	setsid ();

	/* The job defines what the process's standard input, output and
	 * error file descriptors should look like; set those up.
	 */
	if (process_setup_console (job, job->config->console) < 0)
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

	nih_error (_("Unable to execute \"%s\" for %s (#%u): %s"),
		   argv[0], job->config->name, job->id, err->message);
	nih_free (err);

	exit (255);
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

	umask (job->config->umask);

	if (setpriority (PRIO_PROCESS, 0, job->config->nice) < 0)
		nih_return_system_error (-1);

	for (i = 0; i < RLIMIT_NLIMITS; i++) {
		if (! job->config->limits[i])
			continue;

		if (setrlimit (i, job->config->limits[i]) < 0)
			nih_return_system_error (-1);
	}

	if (job->config->chroot) {
		if (chroot (job->config->chroot) < 0)
			nih_return_system_error (-1);
		if (chdir ("/") < 0)
			nih_return_system_error (-1);
	}

	if (job->config->chdir) {
		if (chdir (job->config->chdir) < 0)
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
 * process_setup_console:
 * @job: job details,
 * @type: console type.
 *
 * Set up the standard input, output and error file descriptors for the
 * current process based on the console @type given.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
process_setup_console (Job         *job,
		       ConsoleType  type)
{
	int fd = -1;

	switch (type) {
	case CONSOLE_LOGGED:
		/* Input from /dev/null, output to the logging process */
		fd = open (DEV_NULL, O_RDWR | O_NOCTTY);
		if (fd >= 0) {
			int                 sock;
			struct sockaddr_un  addr;
			size_t              addrlen, namelen;
			const char         *name;

			/* Open a unix stream socket */
			sock = socket (PF_UNIX, SOCK_STREAM, 0);
			if (sock < 0) {
				nih_warn (_("Unable to open logd socket: %s"),
					  strerror (errno));
				break;
			}

			/* Use the abstract namespace */
			addr.sun_family = AF_UNIX;
			addr.sun_path[0] = '\0';

			/* Specifically /com/ubuntu/upstart/logd */
			addrlen = offsetof (struct sockaddr_un, sun_path) + 1;
			addrlen += snprintf (addr.sun_path + 1,
					     sizeof (addr.sun_path) - 1,
					     "/com/ubuntu/upstart/logd");

			/* Connect to the logging daemon (note: this blocks,
			 * but that's ok, we're in the child)
			 */
			if (connect (sock, (struct sockaddr *)&addr,
				     addrlen) < 0) {
				if (errno != ECONNREFUSED)
					nih_warn (_("Unable to connect to "
						    "logd: %s"),
						  strerror (errno));

				close (sock);
				break;
			}

			/* First send the length of the job name */
			name = job ? job->config->name : program_name;
			namelen = strlen (name);
			if (write (sock, &namelen, sizeof (namelen)) < 0) {
				nih_warn (_("Unable to send name to logd: %s"),
					  strerror (errno));
				close (sock);
				break;
			}

			/* Then send the job name */
			if (write (sock, name, namelen) < 0) {
				nih_warn (_("Unable to send name to logd: %s"),
					  strerror (errno));
				close (sock);
				break;
			}

			/* Socket is ready to be used for output */
			fd = sock;
		}

		break;

	case CONSOLE_OUTPUT:
		/* Ordinary console input and output */
		fd = open (CONSOLE, O_RDWR | O_NOCTTY);
		break;

	case CONSOLE_OWNER:
		/* As CONSOLE_OUTPUT but with ^C, etc. sent */
		fd = open (CONSOLE, O_RDWR | O_NOCTTY);
		if (fd >= 0)
			ioctl (fd, TIOCSCTTY, 1);

		break;

	case CONSOLE_NONE:
		/* No console really means /dev/null */
		fd = open (DEV_NULL, O_RDWR | O_NOCTTY);
		break;
	}

	/* Failed to open a console?  Use /dev/null */
	if (fd < 0) {
		nih_warn (_("Failed to open console: %s"), strerror (errno));

		if ((fd = open (DEV_NULL, O_RDWR | O_NOCTTY)) < 0)
			nih_return_system_error (-1);
	}

	/* Copy to standard output and standard error */
	while (dup (fd) < 2)
		;

	return 0;
}
