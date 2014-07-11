/* upstart
 *
 * job_process.c - job process handling
 *
 * Copyright © 2011 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */

/* Required for pty handling */
#define _XOPEN_SOURCE   600

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/ioctl.h>

#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utmp.h>
#include <utmpx.h>
#include <pwd.h>
#include <libgen.h>
#include <termios.h>
#include <grp.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/signal.h>
#include <nih/io.h>
#include <nih/logging.h>
#include <nih/error.h>
#include <nih-dbus/dbus_util.h>

#include "paths.h"
#include "system.h"
#include "environ.h"
#include "process.h"
#include "job_process.h"
#include "job_class.h"
#include "job.h"
#include "errors.h"
#include "control.h"
#include "xdg.h"
#include "apparmor.h"

#ifdef ENABLE_CGROUPS
#include "cgroup.h"
#endif /* ENABLE_CGROUPS */

/**
 * SHELL_CHARS:
 *
 * This is the list of characters that, if encountered in a process, cause
 * it to always be run with a shell.
 **/
#define SHELL_CHARS "~`!$^&*()=|\\{}[];\"'<>?"


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

/**
 * log_dir:
 *
 * Full path to directory where job logs are written.
 *
 **/
char *log_dir = NULL;

/**
 * disable_respawn:
 *
 * If TRUE, disallow respawning.
 **/
int disable_respawn = FALSE;

/* Prototypes for static functions */
static void job_process_remap_fd        (int *fd, int reserved_fd, int error_fd);

/**
 * disable_job_logging:
 *
 * If TRUE, do not log any job output.
 *
 **/
int disable_job_logging = 0;

/**
 * no_inherit_env:
 *
 * If TRUE, do not copy the Session Inits environment variables or umask
 * to that provided to jobs.
 **/
int no_inherit_env = FALSE;

/* Prototypes for static functions */
static void job_process_kill_timer      (Job *job, NihTimer *timer);
static void job_process_terminated      (Job *job, ProcessType process,
					 int status, int state_only);
static int  job_process_catch_runaway   (Job *job);
static void job_process_stopped         (Job *job, ProcessType process);
static void job_process_trace_new       (Job *job, ProcessType process);
static void job_process_trace_new_child (Job *job, ProcessType process);
static void job_process_trace_signal    (Job *job, ProcessType process,
					 int signum);
static void job_process_trace_fork      (Job *job, ProcessType process);
static void job_process_trace_exec      (Job *job, ProcessType process);

extern char         *control_server_address;
extern int           user_mode;
extern int           session_end;
extern time_t        quiesce_phase_time;


/**
 * job_process_start:
 *
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
 * When executed with the shell, if the command (which may be an entire
 * script) is reasonably small (less than 1KB) it is passed to the
 * shell using the POSIX-specified -c option.  Otherwise the shell is told
 * to read commands from one of the special /proc/self/fd/NN devices and NihIo
 * used to feed the script into that device.  A pointer to the NihIo object
 * is not kept or stored because it will automatically clean itself up should
 * the script go away as the other end of the pipe will be closed.
 *
 * In either case the shell is run with the -e option so that commands will
 * fail if their exit status is not checked.
 *
 * This function will only block on temporary job_process_spawn_with_fd() error
 * (for example fork() failure): in normal operation it returns as soon as
 * the fork() was successful, registering an NihIo which provides
 * asynchronous handling of the child setup stage. The specified error
 * handler will be called should child setup fail.
 *
 * It is up to the caller to decide whether non-temporary errors are a reason
 * to change the job state or not.
 **/
void
job_process_start (Job                    *job,
		   ProcessType             process)
{
	Process            *proc;
	nih_local char    **argv = NULL;
	nih_local char    **env = NULL;
	nih_local char     *script = NULL;
	char              **e;
	size_t              argc, envc;
	int                 fds[2] = { -1, -1 };
	int                 trace = FALSE, shell = FALSE;
	int                 job_process_fd = -1;
	JobProcessData     *process_data = NULL;

	nih_assert (job);
	nih_assert (process > PROCESS_INVALID);
	nih_assert (process < PROCESS_LAST);

	proc = job->class->process[process];
	nih_assert (proc != NULL);
	nih_assert (proc->command != NULL);

	/* We run the process using a shell if it says it wants to be run
	 * as such, or if it contains any shell-like characters; since that's
	 * the best way to deal with things like variables.
	 */
	if ((proc->script) || strpbrk (proc->command, SHELL_CHARS)) {
		char *nl, *p;

		argc = 0;
		argv = NIH_MUST (nih_str_array_new (NULL));

		NIH_MUST (nih_str_array_add (&argv, NULL, &argc, SHELL));
		NIH_MUST (nih_str_array_add (&argv, NULL, &argc, "-e"));

		/* If the process wasn't originally marked to be run through
		 * a shell, prepend exec to the script so that the shell
		 * gets out of the way after parsing.
		 */
		if (proc->script) {
			script = NIH_MUST (nih_strdup (NULL, proc->command));
		} else {
			script = NIH_MUST (nih_sprintf (NULL, "exec %s",
							proc->command));
		}

		/* Don't pipe single-line scripts into the shell using
		 * /proc/self/fd/NNN, instead just pass them over the
		 * command-line (taking care to strip off the trailing
		 * newlines).
		 */
		p = nl = strchr (script, '\n');
		while (p && (*p == '\n'))
			p++;

		if ((! nl) || (! *p)) {
			/* Strip off the newline(s) */
			if (nl)
				*nl = '\0';

			NIH_MUST (nih_str_array_add (&argv, NULL,
						     &argc, "-c"));
			NIH_MUST (nih_str_array_addp (&argv, NULL,
						      &argc, script));

			/* Next argument is argv[0]; just pass the shell */
			NIH_MUST (nih_str_array_add (&argv, NULL,
						     &argc, SHELL));
		} else {
			nih_local char *cmd = NULL;

			/* Close the writing end when the child is exec'd */
			NIH_ZERO (pipe (fds));
			nih_io_set_cloexec (fds[1]);

			shell = TRUE;

			cmd = NIH_MUST (nih_sprintf (argv, "%s/%d",
						     "/proc/self/fd",
						     JOB_PROCESS_SCRIPT_FD));
			NIH_MUST (nih_str_array_addp (&argv, NULL,
						      &argc, cmd));
		}
	} else {
		/* Split the command on whitespace to produce a list of
		 * arguments that we can exec directly.
		 */
		argv = NIH_MUST (nih_str_split (NULL, proc->command,
						" \t\r\n", TRUE));
	}

	/* We provide the standard job environment to all of its processes,
	 * except for pre-stop which also has the stop event environment,
	 * adding special variables that indicate which job it was -- mostly
	 * so that initctl can have clever behaviour when called within them.
	 */
	envc = 0;
	env = NIH_MUST (nih_str_array_new (NULL));

	if (job->env)
		NIH_MUST (environ_append (&env, NULL, &envc, TRUE, job->env));

	if (job->stop_env
	    && ((process == PROCESS_PRE_STOP)
		|| (process == PROCESS_POST_STOP)))
		for (e = job->stop_env; *e; e++)
			NIH_MUST (environ_set (&env, NULL, &envc, TRUE, *e));

	NIH_MUST (environ_set (&env, NULL, &envc, TRUE,
			       "UPSTART_JOB=%s", job->class->name));
	NIH_MUST (environ_set (&env, NULL, &envc, TRUE,
			       "UPSTART_INSTANCE=%s", job->name));
	if (user_mode)
		NIH_MUST (environ_set (&env, NULL, &envc, TRUE,
			       "UPSTART_SESSION=%s", control_server_address));

	/* If we're about to spawn the main job and we expect it to become
	 * a daemon or fork before we can move out of spawned, we need to
	 * set a trace on it.
	 */
	if ((process == PROCESS_MAIN)
	    && ((job->class->expect == EXPECT_DAEMON)
		|| (job->class->expect == EXPECT_FORK)))
		trace = TRUE;

	/* Spawn the process, repeat until fork() works */
	while ((job->pid[process] = job_process_spawn_with_fd (job, argv, env,
					trace, fds[0], process, &job_process_fd)) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_warn ("%s: %s", _("Temporary process spawn error"),
				err->message);
		nih_free (err);
	}

	nih_info (_("%s %s process (%d)"),
		  job_name (job), process_name (process), job->pid[process]);

	job->trace_forks = 0;
	job->trace_state = trace ? TRACE_NEW : TRACE_NONE;

	if (shell) {
		/* Clean up and close the reading end (we don't need it) */
		close (fds[0]);
	}

	/* At this stage, a child process has been spawned, but may not
	 * yet have been setup appropriately. This operation is handled
	 * asynchronously since some setup operations may block,
	 * and even though they are running in a new child process, they could
	 * still block PID 1 since the latter needs to wait until the
	 * child is fully setup and the appropriate program has been
	 * exec'd before marking the job as running.
	 *
	 * This is now achieved by creating an NihIo and registering
	 * appropriate handlers such that notification of successful
	 * child setup and child error scenarios are handled
	 * automatically.
	 *
	 * job_process_child_reader() handles updating the jobs state.
	 */
	process_data = job->process_data[process] =
		NIH_MUST (job_process_data_new
				(job->process_data, job, process, job_process_fd));

	if (shell) {
		process_data->shell_fd = fds[1];
		process_data->script = script;
		nih_ref (script, process_data);
	}

	/* Set up a handler to monitor the child fd asynchronously */
	job_register_child_handler (job->process_data[process],
			job_process_fd, process_data);
}

/**
 * job_process_spawn_with_fd:
 * @job: job of process to be spawned,
 * @argv: NULL-terminated list of arguments for the process,
 * @env: NULL-terminated list of environment variables for the process,
 * @trace: whether to trace this process,
 * @script_fd: script file descriptor,
 * @process: job process to spawn,
 * @job_process_fd: readable child setup pipe file descriptor.
 *
 * This function spawns a new process using the class details in @job to set up
 * the environment for it; the process is always a session and process group
 * leader as we never want anything in our own group.
 *
 * The process to be executed is given in the @argv array which is passed
 * directly to execvp(), so should be in the same NULL-terminated form with
 * the first argument containing the path or filename of the binary.  The
 * PATH environment in the @job's associated class will be searched.
 *
 * If @trace is TRUE, the process will be traced with ptrace and this will
 * cause the process to be stopped when the exec() call is made.  You must
 * wait for this and then may use it to set options before continuing the
 * process.
 *
 * If @script_fd is not -1, this file descriptor is dup()d to the special fd 9
 * (moving any other out of the way if necessary).
 *
 * This function only spawns the process, it is up to the caller to ensure
 * that the information is saved into the job and that the process is watched,
 * etc. Also, it is up to the caller to monitor @job_process_fd to
 * ensure the child setup was successful; data available to read
 * indicates a failure to setup the child environment (represented by a
 * JOB_PROCESS_ERROR error) whereas if the descriptor is simply
 * closed setup was successful and the caller can then mark the job
 * process as started.
 *
 * Spawning a process may fail for temporary reasons, usually due to a failure
 * of the fork() syscall.
 *
 * Returns: process id of new process on success, -1 on raised error
 **/
pid_t
job_process_spawn_with_fd (Job          *job,
		   char * const  argv[],
		   char * const *env,
		   int           trace,
		   int           script_fd,
		   ProcessType   process,
		   int          *job_process_fd)
{
	sigset_t        child_set, orig_set;
	pid_t           pid;
	int             i, fds[2] = { -1, -1 };
	int             pty_master = -1;
	int             pty_slave = -1;
	char            pts_name[PATH_MAX];
	char            filename[PATH_MAX];
	FILE           *fd;
	nih_local char *log_path = NULL;
	JobClass       *class;
	uid_t           job_setuid = -1;
	gid_t           job_setgid = -1;
	struct passwd   *pwd = NULL;
	struct group    *grp = NULL;

#ifdef ENABLE_CGROUPS
	int              cgroups_needed = FALSE;
#endif /* ENABLE_CGROUPS */

	nih_assert (job != NULL);
	nih_assert (job->class != NULL);
	nih_assert (job->log != NULL);
	nih_assert (process < PROCESS_LAST);

	class = job->class;

	nih_assert (class != NULL);

#ifdef ENABLE_CGROUPS

	cgroups_needed = job_needs_cgroups (job);

	if (cgroups_needed) {
		if (! cgroup_support_enabled ())
			nih_return_error (-1, CGROUP_ERROR, _("cgroup support not available"));

		/* Should never happen */
		if (! cgroup_manager_available ())
			nih_return_error (-1, CGROUP_ERROR, _("cgroup manager not available"));
	}

#endif /* ENABLE_CGROUPS */

	/* Create a pipe to communicate with the child process until it
	 * execs so we know whether that was successful or an error occurred.
	 */
	if (pipe (fds) < 0)
		nih_return_system_error (-1);

	if (class->console == CONSOLE_LOG && disable_job_logging)
			class->console = CONSOLE_NONE;

	if (class->console == CONSOLE_LOG) {
		NihError *err;

		/* Ensure log destroyed for previous matching job process
		 * (occurs when job restarted but previous process has not
		 * yet been reaped).
		 */
		if (job->log[process]) {
			nih_free (job->log[process]);
			job->log[process] = NULL;
		}

		log_path = job_process_log_path (job, 0);

		if (! log_path) {
			/* Consume and re-raise */
			err = nih_error_get ();
			nih_assert (err->number == ENOMEM);
			nih_free (err);
			close (fds[0]);
			close (fds[1]);
			nih_return_no_memory_error (-1);
		}

		pty_master = posix_openpt (O_RDWR | O_NOCTTY);

		if (pty_master < 0) {
			nih_error (_("Failed to create pty - disabling logging for job"));

			/* Ensure that the job can still be started by
			 * disabling logging.
			 */
			class->console = CONSOLE_NONE;

			close (fds[0]);
			close (fds[1]);
			nih_return_system_error (-1);
		}

		/* Stop any process created _before_ the log object below is
		 * freed from inheriting this fd.
		 */
		nih_io_set_cloexec (pty_master);

		/* pty_master will be closed by log_destroy() */
		job->log[process] = log_new (job->log, log_path, pty_master, 0);
		if (! job->log[process]) {
			close (pty_master);
			close (fds[0]);
			close (fds[1]);
			nih_return_system_error (-1);
		}
	}

	/* Block all signals while we fork to avoid the child process running
	 * our own signal handlers before we've reset them all back to the
	 * default.
	 */
	sigfillset (&child_set);
	sigprocmask (SIG_BLOCK, &child_set, &orig_set);

	/* Ensure that any lingering data in stdio buffers is flushed
	 * to avoid the child getting a copy of it.
	 * If not done, CONSOLE_LOG jobs may end up with unexpected data
	 * in their logs if we run with for example '--debug'.
	 */
	fflush (NULL);

	/* Fork the child process, handling success and failure by resetting
	 * the signal mask and returning the new process id or a raised error.
	 */
	pid = fork ();
	if (pid > 0) {
		if (class->debug) {
			nih_info (_("Pausing %s (%d) [pre-exec] for debug"),
			  class->name, pid);
		}

		sigprocmask (SIG_SETMASK, &orig_set, NULL);
		close (fds[1]);

		*job_process_fd = fds[0];

		nih_io_set_cloexec (*job_process_fd);

		return pid;
	} else if (pid < 0) {
		nih_error_raise_system ();

		sigprocmask (SIG_SETMASK, &orig_set, NULL);
		close (fds[0]);
		close (fds[1]);
		if (class->console == CONSOLE_LOG) {
			nih_free (job->log[process]);
			job->log[process] = NULL;
		}
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

	job_process_remap_fd (&fds[1], JOB_PROCESS_SCRIPT_FD, fds[1]);
	nih_io_set_cloexec (fds[1]);

	if (class->console == CONSOLE_LOG) {
		struct sigaction act;
		struct sigaction ignore;

		job_process_remap_fd (&pty_master, JOB_PROCESS_SCRIPT_FD, fds[1]);

		/* Child is the slave, so won't need this */
		nih_io_set_cloexec (pty_master);

		/* Temporarily disable child handler as grantpt(3) disallows one
		 * being in effect when called.
		 */
		ignore.sa_handler = SIG_DFL;
		ignore.sa_flags = 0;
		sigemptyset (&ignore.sa_mask);

		if (sigaction (SIGCHLD, &ignore, &act) < 0) {
			nih_error_raise_system ();
			job_process_error_abort (fds[1], JOB_PROCESS_ERROR_SIGNAL, 0);
		}

		if (grantpt (pty_master) < 0) {
			nih_error_raise_system ();
			job_process_error_abort (fds[1], JOB_PROCESS_ERROR_GRANTPT, 0);
		}

		/* Restore child handler */
		if (sigaction (SIGCHLD, &act, NULL) < 0) {
			nih_error_raise_system ();
			job_process_error_abort (fds[1], JOB_PROCESS_ERROR_SIGNAL, 0);
		}

		if (unlockpt (pty_master) < 0) {
			nih_error_raise_system ();
			job_process_error_abort (fds[1], JOB_PROCESS_ERROR_UNLOCKPT, 0);
		}

		if (ptsname_r (pty_master, pts_name, sizeof(pts_name)) < 0) {
			nih_error_raise_system ();
			job_process_error_abort (fds[1], JOB_PROCESS_ERROR_PTSNAME, 0);
		}

		pty_slave = open (pts_name, O_RDWR | O_NOCTTY);

		if (pty_slave < 0) {
			nih_error_raise_system ();
			job_process_error_abort (fds[1], JOB_PROCESS_ERROR_OPENPT_SLAVE, 0);
		}

		job_process_remap_fd (&pty_slave, JOB_PROCESS_SCRIPT_FD, fds[1]);
	}

	/* Move the script fd to special fd 9; the only gotcha is if that
	 * would be our error descriptor, but that's handled above.
	 */
	if ((script_fd != -1) && (script_fd != JOB_PROCESS_SCRIPT_FD)) {
		int tmp = dup2 (script_fd, JOB_PROCESS_SCRIPT_FD);
		if (tmp < 0) {
			nih_error_raise_system ();
			job_process_error_abort (fds[1], JOB_PROCESS_ERROR_DUP, 0);
		}
		close (script_fd);
		script_fd = tmp;
	}

	/* Become the leader of a new session and process group, shedding
	 * any controlling tty (which we shouldn't have had anyway).
	 */
	setsid ();

	/* Set the process environment from the function parameters. */
	environ = (char **)env;

	/* Set the standard file descriptors to an output of our chosing;
	 * any other open descriptor must be intended for the child, or have
	 * the FD_CLOEXEC flag so it's automatically closed when we exec()
	 * later.
	 */
	if (system_setup_console (class->console, FALSE) < 0) {
		if (class->console == CONSOLE_OUTPUT) {
			NihError *err;

			err = nih_error_get ();
			nih_warn (_("Failed to open system console: %s"),
				  err->message);
			nih_free (err);

			if (system_setup_console (CONSOLE_NONE, FALSE) < 0)
				job_process_error_abort (fds[1], JOB_PROCESS_ERROR_CONSOLE, 0);
		} else
			job_process_error_abort (fds[1], JOB_PROCESS_ERROR_CONSOLE, 0);
	}

	if (class->console == CONSOLE_LOG) {
		/* Redirect stdout and stderr to the logger fd */
		if (dup2 (pty_slave, STDOUT_FILENO) < 0) {
			nih_error_raise_system ();
			job_process_error_abort (fds[1], JOB_PROCESS_ERROR_DUP, 0);
		}

		if (dup2 (pty_slave, STDERR_FILENO) < 0) {
			nih_error_raise_system ();
			job_process_error_abort (fds[1], JOB_PROCESS_ERROR_DUP, 0);
		}

		close (pty_slave);
	}

	/* Switch to the specified AppArmor profile, but only for the main
	   process, so we don't confine the pre- and post- processes.
	 */
	if ((class->apparmor_switch) && (process == PROCESS_MAIN)) {
		nih_local char *profile = NULL;

		/* Use the environment to expand the AppArmor profile name
		 */
		profile = NIH_SHOULD (environ_expand (NULL,
						      class->apparmor_switch,
						      environ));

		if (! profile) {
			job_process_error_abort (fds[1], JOB_PROCESS_ERROR_SECURITY, 0);
		}

		if (apparmor_switch (profile) < 0) {
			nih_error_raise_system ();
			job_process_error_abort (fds[1], JOB_PROCESS_ERROR_SECURITY, 0);
		}
	}

	if (process != PROCESS_SECURITY) {
		/* Set resource limits for the process, skipping over any that
		 * aren't set in the job class such that they inherit from
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

		/* Set the file mode creation mask; this is one of the few operations
		 * that can never fail.
		 */
		umask (class->umask);

		/* Adjust the process priority ("nice level").
		 */
		if (class->nice != JOB_NICE_INVALID &&
		    setpriority (PRIO_PROCESS, 0, class->nice) < 0) {
			nih_error_raise_system ();
			job_process_error_abort (fds[1],
						 JOB_PROCESS_ERROR_PRIORITY, 0);
		}

		/* Adjust the process OOM killer priority.
		 */
		if (class->oom_score_adj != JOB_DEFAULT_OOM_SCORE_ADJ) {
			int oom_value;
			snprintf (filename, sizeof (filename),
				  "/proc/%d/oom_score_adj", getpid ());
			oom_value = class->oom_score_adj;
			fd = fopen (filename, "w");
			if ((! fd) && (errno == ENOENT)) {
				snprintf (filename, sizeof (filename),
					  "/proc/%d/oom_adj", getpid ());
				oom_value = (class->oom_score_adj
					     * ((class->oom_score_adj < 0) ? 17 : 15)) / 1000;
				fd = fopen (filename, "w");
			}
			if (! fd) {
				nih_error_raise_system ();
				job_process_error_abort (fds[1], JOB_PROCESS_ERROR_OOM_ADJ, 0);
			} else {
				fprintf (fd, "%d\n", oom_value);

				if (fclose (fd)) {
					nih_error_raise_system ();
					job_process_error_abort (fds[1], JOB_PROCESS_ERROR_OOM_ADJ, 0);
				}
			}
		}

		/* Handle changing a chroot session job prior to dealing with
		 * the 'chroot' stanza.
		 */
		if (class->session && class->session->chroot) {
			if (chroot (class->session->chroot) < 0) {
				nih_error_raise_system ();
				job_process_error_abort (fds[1], JOB_PROCESS_ERROR_CHROOT, 0);
			}
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
		if (class->chdir || user_mode == FALSE) {
			if (chdir (class->chdir ? class->chdir : "/") < 0) {
				nih_error_raise_system ();
				job_process_error_abort (fds[1], JOB_PROCESS_ERROR_CHDIR, 0);
			}
		}

		/* Change the user and group of the process to the one
		 * configured in the job. We must wait until now to lookup the
		 * UID and GID from the names to accommodate both chroot
		 * session jobs and jobs with a chroot stanza.
		 */
		if (class->setuid) {
			/* Without resetting errno, it's impossible to
			 * distinguish between a non-existent user and and
			 * error during lookup */
			errno = 0;
			pwd = getpwnam (class->setuid);
			if (! pwd) {
				if (errno != 0) {
					nih_error_raise_system ();
					job_process_error_abort (fds[1], JOB_PROCESS_ERROR_GETPWNAM, 0);
				} else {
					nih_error_raise (JOB_PROCESS_INVALID_SETUID,
							 JOB_PROCESS_INVALID_SETUID_STR);
					job_process_error_abort (fds[1], JOB_PROCESS_ERROR_BAD_SETUID, 0);
				}
			}

			job_setuid = pwd->pw_uid;
			/* This will be overridden if setgid is also set: */
			job_setgid = pwd->pw_gid;
		}

		if (class->setgid) {
			errno = 0;
			grp = getgrnam (class->setgid);
			if (! grp) {
				if (errno != 0) {
					nih_error_raise_system ();
					job_process_error_abort (fds[1], JOB_PROCESS_ERROR_GETGRNAM, 0);
				} else {
					nih_error_raise (JOB_PROCESS_INVALID_SETGID,
							 JOB_PROCESS_INVALID_SETGID_STR);
					job_process_error_abort (fds[1], JOB_PROCESS_ERROR_BAD_SETGID, 0);
				}
			}

			job_setgid = grp->gr_gid;
		}

		if (script_fd != -1 &&
		    (job_setuid != (uid_t) -1 || job_setgid != (gid_t) -1) &&
		    fchown (script_fd, job_setuid, job_setgid) < 0) {
			nih_error_raise_system ();
			job_process_error_abort (fds[1], JOB_PROCESS_ERROR_CHOWN, 0);
		}

		/* Make sure we always have the needed pwd and grp structs.
		 * Then pass those to initgroups() to setup the user's group list.
		 * Only do that if we're root as initgroups() won't work when non-root. */
		if (geteuid () == 0) {
			if (! pwd) {
				pwd = getpwuid (geteuid ());
				if (! pwd) {
					nih_error_raise_system ();
					job_process_error_abort (fds[1], JOB_PROCESS_ERROR_GETPWUID, 0);
				}
			}

			if (! grp) {
				grp = getgrgid (getegid ());
				if (! grp) {
					nih_error_raise_system ();
					job_process_error_abort (fds[1], JOB_PROCESS_ERROR_GETGRGID, 0);
				}
			}

			if (pwd && grp) {
				if (initgroups (pwd->pw_name, grp->gr_gid) < 0) {
					nih_error_raise_system ();
					job_process_error_abort (fds[1], JOB_PROCESS_ERROR_INITGROUPS, 0);
				}
			}
		}

#ifdef ENABLE_CGROUPS
		if (cgroups_needed) {
			if (cgroup_manager_connect () < 0)
				job_process_error_abort (fds[1], JOB_PROCESS_ERROR_CGROUP_MGR_CONNECT, 0);

			if (! cgroup_setup (&job->class->cgroups,
						env,
						class->setuid ? job_setuid : geteuid (),
						class->setgid ? job_setgid : getegid ())) {
				job_process_error_abort (fds[1], JOB_PROCESS_ERROR_CGROUP_SETUP, 0);
			}

		}
#endif /* ENABLE_CGROUPS */

		/* Start dropping privileges */
		if (job_setgid != (gid_t) -1 && setgid (job_setgid) < 0) {
			nih_error_raise_system ();
			job_process_error_abort (fds[1], JOB_PROCESS_ERROR_SETGID, 0);
		}

		if (job_setuid != (uid_t)-1 && setuid (job_setuid) < 0) {
			nih_error_raise_system ();
			job_process_error_abort (fds[1], JOB_PROCESS_ERROR_SETUID, 0);
		}
	}

	/* Reset all the signal handlers back to their default handling so
	 * the child isn't unexpectedly ignoring any, and so we won't
	 * surprisingly handle them before we've exec()d the new process.
	 */
	nih_signal_reset ();
	sigprocmask (SIG_SETMASK, &orig_set, NULL);

	/* Notes:
	 *
	 * - we can't use pause() here since there would then be no way to
	 *   resume the process without killing it.
	 *
	 * - we have to close the pipe back to the parent since if we don't,
	 *   the parent hangs until the STOP is cleared. Although this may be
	 *   acceptable for normal operation, this causes the test suite to
	 *   fail. Note that closing the pipe means from this point onwards,
	 *   the parent cannot know the true outcome of the spawn: that
	 *   responsibility lies with the debugger.
	 *
	 * - note that running with the debug stanza enabled will
	 *   unavoidably stop stateful re-exec from working correctly
	 *   since the stopped debug child process will hold copies of
	 *   the parents file descriptors open until it continues to
	 *   call exec below.
	 */
	if (class->debug) {
		/* Since we have not exec'd at this point, we will still
		 * have a copy of the parents fds open. As such, re-exec
		 * will not work.
		 */
		close (fds[1]);
		raise (SIGSTOP);
	}

#ifdef ENABLE_CGROUPS
	/* Move the pid into the appropriate cgroups now that 
	 * the process is running with the correct group and user
	 * ownership.
	 */
	if (cgroups_needed && cgroup_enter_groups (&job->class->cgroups) != TRUE)
		job_process_error_abort (fds[1], JOB_PROCESS_ERROR_CGROUP_ENTER, 0);

#endif /* ENABLE_CGROUPS */

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
 * This function calls the exit() system call, so never returns.
 **/
void
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
	while (write (fd, &wire_err, sizeof (wire_err)) < 0)
		;

	nih_free (err);

	exit (255);
}


/**
 * job_process_error_handler:
 * @buf: data read from child process,
 * @len: length of data read as @wire_err.
 *
 * Read from the reading end of the pipe specified by @fd, if we receive
 * data then the child raised a process error which we reconstruct and raise
 * again; otherwise no problem was found and no action is taken.
 *
 * The reconstructed error will be of JOB_PROCESS_ERROR type, the human-
 * readable message is generated according to the type of process error
 * and argument passed along with it.
 **/
void
job_process_error_handler (const char *buf, size_t len)
{
	JobProcessWireError  *wire_err;
	JobProcessError      *err;
	const char           *res;

	wire_err = (JobProcessWireError *)buf;

	nih_assert (wire_err);

	/* Read the error from the pipe; a zero read indicates that the
	 * exec succeeded so we return success, otherwise if we don't receive
	 * a JobProcessWireError structure, we return a temporary error so we
	 * try again.
	 */
	if (len != sizeof (JobProcessWireError)) {
		errno = EILSEQ;
		nih_return_system_error ();
	}

	/* Construct a JobProcessError to be raised containing information
	 * from the wire, augmented with human-readable information we
	 * generate here.
	 */
	err = NIH_MUST (nih_new (NULL, JobProcessError));

	err->type = wire_err->type;
	err->arg = wire_err->arg;
	err->errnum = wire_err->errnum;

	err->error.number = JOB_PROCESS_ERROR;

	switch (err->type) {
	case JOB_PROCESS_ERROR_DUP:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to move script fd: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_CONSOLE:
		err->error.message = NIH_MUST (nih_sprintf (
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

		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to set \"%s\" resource limit: %s"),
				  res, strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_PRIORITY:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to set priority: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_OOM_ADJ:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to set oom adjustment: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_CHROOT:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to change root directory: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_CHDIR:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to change working directory: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_PTRACE:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to set trace: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_EXEC:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to execute: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_GETPWNAM:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to getpwnam: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_GETGRNAM:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to getgrnam: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_GETPWUID:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to getpwuid: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_GETGRGID:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to getgrgid: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_BAD_SETUID:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to find setuid user")));
		break;
	case JOB_PROCESS_ERROR_BAD_SETGID:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to find setgid group")));
		break;
	case JOB_PROCESS_ERROR_SETUID:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to setuid: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_SETGID:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to setgid: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_CHOWN:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to chown: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_UNLOCKPT:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to unlockpt: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_GRANTPT:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to granpt: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_PTSNAME:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to get ptsname: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_OPENPT_SLAVE:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to open pty slave: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_SIGNAL:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to modify signal handler: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_ALLOC:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to allocate memory: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_INITGROUPS:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to initgroups: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_SECURITY:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to switch security profile: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_CGROUP_MGR_CONNECT:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to connect to CGManager: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_CGROUP_SETUP:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to setup cgroup: %s"),
				  strerror (err->errnum)));
		break;
	case JOB_PROCESS_ERROR_CGROUP_ENTER:
		err->error.message = NIH_MUST (nih_sprintf (
				  err, _("unable to enter cgroup: %s"),
				  strerror (err->errnum)));
		break;

	default:
		nih_assert_not_reached ();
	}

	nih_error_raise_error (&err->error);
}


/**
 * job_process_kill:
 * @job: job to kill process of,
 * @process: process to be killed.
 *
 * This function forces a @job to leave its current state by sending
 * @process the "kill signal" defined signal (TERM by default), and maybe
 * later the KILL signal.  The actual state changes are performed by
 * job_child_reaper when the process has actually terminated.
 **/
void
job_process_kill (Job         *job,
		  ProcessType  process)
{
	nih_assert (job != NULL);
	nih_assert (job->pid[process] > 0);
	nih_assert (job->kill_timer == NULL);
	nih_assert (job->kill_process == PROCESS_INVALID);

	nih_info (_("Sending %s signal to %s %s process (%d)"),
		  nih_signal_to_name (job->class->kill_signal),
		  job_name (job), process_name (process), job->pid[process]);

	if (system_kill (job->pid[process], job->class->kill_signal) < 0) {
		NihError *err;

		err = nih_error_get ();
		if (err->number != ESRCH)
			nih_warn (_("Failed to send %s signal to %s %s process (%d): %s"),
				  nih_signal_to_name (job->class->kill_signal),
				  job_name (job), process_name (process),
				  job->pid[process], err->message);
		nih_free (err);

		return;
	}

	job_process_set_kill_timer (job, process, job->class->kill_timeout);
}

/**
 * job_process_jobs_running:
 *
 * Determine if any jobs are running. Note that simply checking if class
 * instances exist is insufficient: since this call is used for shutdown
 * abstract jobs must not be able to block the shutdown.
 *
 * Returns: TRUE if jobs are still running, else FALSE.
 **/
int
job_process_jobs_running (void)
{
	job_class_init ();

	NIH_HASH_FOREACH (job_classes, iter) {
		JobClass *class = (JobClass *)iter;

		NIH_HASH_FOREACH (class->instances, job_iter) {
			Job *job = (Job *)job_iter;
			int i;

			for (i = 0; i < PROCESS_LAST; i++) {
				if (job->pid[i])
					return TRUE;
			}
		}
	}

	return FALSE;
}


/**
 * job_process_stop_all:
 *
 * Stop all running jobs.
 **/
void
job_process_stop_all (void)
{
	job_class_init ();

	NIH_HASH_FOREACH (job_classes, iter) {
		JobClass *class = (JobClass *)iter;

		/* Note that instances get killed in a random order */
		NIH_HASH_FOREACH (class->instances, job_iter) {
			Job *job = (Job *)job_iter;

			/* Request job instance stops */
			job_change_goal (job, JOB_STOP);
		}
	}
}

/**
 * job_process_set_kill_timer:
 * @job: job to set kill timer for,
 * @process: process to be killed,
 * @timeout: timeout to apply for timer.
 *
 * Set kill timer for specified @job @process with timeout @timeout.
 **/
void
job_process_set_kill_timer (Job          *job,
		  	    ProcessType   process,
			    time_t        timeout)
{
	nih_assert (job);
	nih_assert (timeout);
	nih_assert (job->kill_timer == NULL);

	job->kill_process = process;
	job->kill_timer = NIH_MUST (nih_timer_add_timeout (
			  job, timeout,
			  (NihTimerCb)job_process_kill_timer, job));
}

/**
 * job_process_adj_kill_timer:
 *
 * @job: job whose kill timer is to be modified,
 * @due: new due time to set for job kill timer.
 *
 * Adjust due time for @job's kill timer to @due.
 **/
void
job_process_adj_kill_timer (Job *job, time_t due)
{
	nih_assert (job);
	nih_assert (job->kill_timer);
	nih_assert (due);

	job->kill_timer->due = due;
}

/**
 * job_process_kill_timer:
 * @job: job to kill process of,
 * @timer: timer that caused us to be called.
 *
 * This callback is called if the process failed to terminate within
 * a particular time of being sent the TERM signal.  The process is killed
 * more forcibly by sending the KILL signal.
 **/
static void
job_process_kill_timer (Job      *job,
			NihTimer *timer)
{
	ProcessType process;

	nih_assert (job != NULL);
	nih_assert (timer != NULL);
	nih_assert (job->kill_timer == timer);
	nih_assert (job->kill_process != PROCESS_INVALID);

	process = job->kill_process;
	nih_assert (job->pid[process] > 0);

	job->kill_timer = NULL;
	job->kill_process = PROCESS_INVALID;

	nih_info (_("Sending %s signal to %s %s process (%d)"),
		  "KILL",
		  job_name (job), process_name (process), job->pid[process]);

	if (system_kill (job->pid[process], SIGKILL) < 0) {
		NihError *err;

		err = nih_error_get ();
		if (err->number != ESRCH)
			nih_warn (_("Failed to send %s signal to %s %s process (%d): %s"),
				  "KILL",
				  job_name (job), process_name (process),
				  job->pid[process], err->message);
		nih_free (err);
	}
}


/**
 * job_process_handler:
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
job_process_handler (void           *data,
		     pid_t           pid,
		     NihChildEvents  event,
		     int             status)
{
	Job         *job;
	ProcessType  process;
	NihLogLevel  priority;
	const char  *sig;

	nih_assert (pid > 0);

	/* Find the job that an event ocurred for, and identify which of the
	 * job's process it was.  If we don't know about it, then we simply
	 * ignore the event.
	 */
	job = job_process_find (pid, &process);
	if (! job)
		return;

	/* Check the job's normal exit clauses to see whether this is a failure
	 * worth warning about.
	 */
	priority = NIH_LOG_WARN;
	for (size_t i = 0; i < job->class->normalexit_len; i++) {
		if (job->class->normalexit[i] == status) {
			priority = NIH_LOG_INFO;
			break;
		}
	}

	switch (event) {
	case NIH_CHILD_EXITED:
		/* Child exited; check status to see whether it exited
		 * normally (zero) or with a non-zero status.
		 */
		if (status) {
			nih_log_message (priority, _("%s %s process (%d) "
						     "terminated with status %d"),
					 job_name (job), process_name (process),
					 pid, status);
		} else {
			nih_info (_("%s %s process (%d) exited normally"),
				  job_name (job), process_name (process), pid);
		}

		job_process_terminated (job, process, status, FALSE);
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
			nih_log_message (priority, _("%s %s process (%d) killed by %s signal"),
					 job_name (job), process_name (process),
					 pid, sig);
		} else {
			nih_warn (_("%s %s process (%d) killed by signal %d"),
				  job_name (job), process_name (process),
				  pid, status);
		}

		status <<= 8;
		job_process_terminated (job, process, status, FALSE);
		break;
	case NIH_CHILD_STOPPED:
		/* Child was stopped by a signal, make sure it was SIGSTOP
		 * and not a tty-related signal.
		 */
		sig = nih_signal_to_name (status);
		if (sig) {
			nih_info (_("%s %s process (%d) stopped by %s signal"),
				  job_name (job), process_name (process),
				  pid, sig);
		} else {
			nih_info (_("%s %s process (%d) stopped by signal %d"),
				  job_name (job), process_name (process),
				  pid, status);
		}

		if (status == SIGSTOP)
			job_process_stopped (job, process);

		break;
	case NIH_CHILD_CONTINUED:
		/* Child was continued by a signal.
		 */
		sig = nih_signal_to_name (status);
		if (sig) {
			nih_info (_("%s %s process (%d) continued by %s signal"),
				  job_name (job), process_name (process),
				  pid, sig);
		} else {
			nih_info (_("%s %s process (%d) continued by signal %d"),
				  job_name (job), process_name (process),
				  pid, status);
		}
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
 * @status: exit status or signal in higher byte,
 * @state_only: if TRUE, only update the jobs state (not its goal).
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
			int          status,
			int          state_only)
{
	int failed = FALSE, stop = FALSE, state = TRUE;
	struct utmpx *utmptr;
	struct timeval tv;

	nih_assert (job != NULL);

	if (job->state == JOB_SECURITY_SPAWNING ||
			job->state == JOB_PRE_STARTING ||
			job->state == JOB_SPAWNING ||
			job->state == JOB_POST_STARTING ||
			job->state == JOB_PRE_STOPPING ||
			job->state == JOB_POST_STOPPING) {
		/* A child process has been spawned by
		 * job_process_spawn(), but we have not yet received
		 * confirmation of whether the child setup phase was
		 * successful or not.
		 *
		 * The fact that we are in this function means the child
		 * setup actually failed, however we handle that
		 * by waiting for the handlers registered by
		 * job_register_child_handler() to be called to deal
		 * with this error.
		 */
		nih_assert (job->process_data);
		nih_assert (job->process_data[process]);

		/* Add the child data for the *second* call to this
		 * function when the child pipe has been closed.
		 */
		job->process_data[process]->status = status;

		job->state = job_next_state (job);

		return;
	}

	switch (process) {
	case PROCESS_MAIN:
		nih_assert ((job->state == JOB_RUNNING)
			    || (job->state == JOB_SPAWNED)
			    || (job->state == JOB_KILLED)
			    || (job->state == JOB_STOPPING)
			    || (job->state == JOB_POST_START)
			    || (job->state == JOB_PRE_STOP));

		/* We don't change the state if we're in post-start and there's
		 * a post-start process running, or if we're in pre-stop and
		 * there's a pre-stop process running; we wait for those to
		 * finish instead.
		 */
		if ((job->state == JOB_POST_START)
		    && job->class->process[PROCESS_POST_START]
		    && (job->pid[PROCESS_POST_START] > 0)) {
			state = FALSE;
		} else if ((job->state == JOB_PRE_STOP)
		    && job->class->process[PROCESS_PRE_STOP]
		    && (job->pid[PROCESS_PRE_STOP] > 0)) {
			state = FALSE;
		}

		/* Dying when we killed it is perfectly normal and never
		 * considered a failure.  We also don't want to tamper with
		 * the goal since we might be restarting the job anyway.
		 */
		if (job->state == JOB_KILLED)
			break;

		/* Yet another corner case is terminating when we were
		 * already stopping, we don't to tamper with the goal or
		 * state because we're still waiting for the stopping
		 * event to finish and that might restart it anyway.
		 * We also don't want to consider it a failure, because
		 * we want the stopping and stopped events to match.
		 */
		if (job->state == JOB_STOPPING) {
			state = FALSE;
			break;
		}

		/* We don't assume that because the primary process was
		 * killed or exited with a non-zero status, it failed.
		 * Instead we check the normalexit list to see whether
		 * the exit signal or status is in that list, and only
		 * if not, do we consider it failed.
		 *
		 * For services that can be respawned, a zero exit status is
		 * also a failure unless listed.
		 */
		if ((status || (job->class->respawn && (! job->class->task)))
		    && (job->goal == JOB_START)) {
			failed = TRUE;
			for (size_t i = 0; i < job->class->normalexit_len; i++) {
				if (job->class->normalexit[i] == status) {
					failed = FALSE;
					break;
				}
			}

			/* We might be able to respawn the failed job;
			 * that's a simple matter of doing nothing.  Check
			 * the job isn't running away first though.
			 */
			if (failed && job->class->respawn && ! disable_respawn) {
				if (job_process_catch_runaway (job)) {
					nih_warn (_("%s respawning too fast, stopped"),
						  job_name (job));

					failed = FALSE;
					job_failed (job, PROCESS_INVALID, 0);
				} else {
					nih_warn (_("%s %s process ended, respawning"),
						  job_name (job),
						  process_name (process));
					failed = FALSE;

					/* If we're not going to change the
					 * state because there's a post-start
					 * or pre-stop script running, we need
					 * to remember to do it when that
					 * finishes.
					 */
					if (! state)
						job_change_goal (job, JOB_RESPAWN);
					break;
				}
			}
		}

		/* Otherwise whether it's failed or not, we should
		 * stop the job now.
		 */
		stop = TRUE;
		break;
	case PROCESS_SECURITY:
		nih_assert (job->state == JOB_SECURITY);

		/* We should always fail the job if the security profile
		 * failed to load
		 */
		if (status) {
			failed = TRUE;
			stop = TRUE;
		}
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
		nih_assert (job->state == JOB_POST_START || job->state == JOB_RUNNING);

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
		nih_unref (job->kill_timer, job);
		job->kill_timer = NULL;
		job->kill_process = PROCESS_INVALID;
	}

	if (job->class->console == CONSOLE_LOG && job->log[process] && ! state_only) {
		int  ret;

		/* It is imperative that we free the log at this stage to ensure
		 * that jobs which respawn have their log written _now_
		 * (and not just when the overall Job object is freed at
		 * some distant future point).
		 */
		ret = log_handle_unflushed (job->log, job->log[process]);

		if (ret != 0) {
			if (ret < 0) {
				/* Any lingering data will now be lost in what
				 * is probably a low-memory scenario.
				 */
				nih_warn (_("Failed to add log to unflushed queue"));
			}
			nih_free (job->log[process]);
		}

		/* Either the log has been freed, or it needs to be
		 * severed from its parent job fully.
		 */
		job->log[process] = NULL;
	}

	if (! state_only) {
		/* Find existing utmp entry for the process pid */
		setutxent();
		while ((utmptr = getutxent()) != NULL) {
			if (utmptr->ut_pid == job->pid[process]) {
				/* set type and clean ut_user, ut_host,
				 * ut_time as described in utmp(5)
				 */
				utmptr->ut_type = DEAD_PROCESS;
				memset(utmptr->ut_user, 0, UT_NAMESIZE);
				memset(utmptr->ut_host, 0, UT_HOSTSIZE);
				utmptr->ut_time = 0;
				/* Update existing utmp file. */
				pututxline(utmptr);

				/* set ut_time for log */
				gettimeofday(&tv, NULL);
				utmptr->ut_tv.tv_sec = tv.tv_sec;
				utmptr->ut_tv.tv_usec = tv.tv_usec;
				/* Write wtmp entry */
				updwtmpx (_PATH_WTMP, utmptr);

				break;
			}
		}
		endutxent();

		/* Clear the process pid field */
		job->pid[process] = 0;
	}

	/* Mark the job as failed */
	if (failed)
		job_failed (job, process, status);

	/* Cancel goal transition if the job state only should be
	 * changed.
	 */
	if (state_only && stop) {
		stop = 0;
		nih_assert (! failed);
	}

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
 * job_process_catch_runaway
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
job_process_catch_runaway (Job *job)
{
	struct timespec now;

	nih_assert (job != NULL);

	if (job->class->respawn_limit && job->class->respawn_interval) {
		time_t interval;

		/* Time since last respawn ... this goes very large if we
		 * haven't done one, which is fine
		 */
		nih_assert (clock_gettime (CLOCK_MONOTONIC, &now) == 0);

		interval = now.tv_sec - job->respawn_time;
		if (interval < job->class->respawn_interval) {
			job->respawn_count++;
			if (job->respawn_count > job->class->respawn_limit)
				return TRUE;
		} else {
			job->respawn_time = now.tv_sec;
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
	if ((process != PROCESS_MAIN) || ((job->state != JOB_SPAWNING) && job->state != JOB_SPAWNED))
		return;

	/* Send SIGCONT back and change the state to the next one, if this
	 * job behaves that way.
	 */
	if (job->class->expect == EXPECT_STOP) {
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
	if ((process != PROCESS_MAIN) || ((job->state != JOB_SPAWNING) && (job->state != JOB_SPAWNED))) {
		return;
	}

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
	if ((process != PROCESS_MAIN) || ((job->state != JOB_SPAWNING) && (job->state != JOB_SPAWNED)))
		return;

	/* We need to fork at least twice unless we're expecting a
	 * single fork when we only need to fork once; once that limit
	 * has been reached, end the trace.
	 */
	job->trace_forks++;
	if ((job->trace_forks > 1) || (job->class->expect == EXPECT_FORK))
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
	if ((process != PROCESS_MAIN) || ((job->state != JOB_SPAWNING) && (job->state != JOB_SPAWNED))
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
	if ((process != PROCESS_MAIN) || ((job->state != JOB_SPAWNING) && (job->state != JOB_SPAWNED))
	    || (job->trace_state != TRACE_NORMAL)) {
		return;
	}

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

	/* We may have already had the wait notification for the new child
	 * waiting at SIGSTOP, in which case a ptrace() call will succeed
	 * for it.
	 */
	if (ptrace (PTRACE_SETOPTIONS, job->pid[process], NULL, 0) < 0) {
		nih_debug ("Failed to set options for new %s %s process (%d), "
			   "probably not yet forked: %s",
			   job_name (job), process_name (process),
			   job->pid[process], strerror (errno));
		return;
	}

	job_process_trace_new_child (job, process);
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
	if ((process != PROCESS_MAIN) || ((job->state != JOB_SPAWNING) && job->state != JOB_SPAWNED)
	    || (job->trace_state != TRACE_NORMAL))
		return;

	nih_info (_("%s %s process (%d) executable changed"),
		  job_name (job), process_name (process), job->pid[process]);

	if (job->trace_forks) {
		if (ptrace (PTRACE_DETACH, job->pid[process], NULL, 0) < 0)
			nih_warn (_("Failed to detach traced "
				    "%s %s process (%d): %s"),
				  job_name (job), process_name (process),
				  job->pid[process], strerror (errno));

		job->trace_state = TRACE_NONE;
		job_change_state (job, job_next_state (job));
	} else {
		if (ptrace (PTRACE_CONT, job->pid[process], NULL, 0) < 0)
			nih_warn (_("Failed to continue traced %s %s process (%d): %s"),
				  job_name (job), process_name (process),
				  job->pid[process], strerror (errno));
	}
}


/**
 * job_process_find:
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
job_process_find (pid_t        pid,
		  ProcessType *process)
{
	nih_assert (pid > 0);

	job_class_init ();

	NIH_HASH_FOREACH (job_classes, iter) {
		JobClass *class = (JobClass *)iter;

		NIH_HASH_FOREACH (class->instances, job_iter) {
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
 * job_process_log_path:
 *
 * @job: Job,
 * @user_job: TRUE if @job refers to a non-root job, else FALSE.
 *
 * Determine full path to on-disk log file for specified @job.
 *
 * This differs depending on whether the job is a system job or a user job.
 * Instance names are appended to the log name.
 *
 * Returns: newly allocated log path string, or NULL on raised error.
 **/
char *
job_process_log_path (Job *job, int user_job)
{
	JobClass        *class = NULL;
	char            *log_path = NULL;
	nih_local char  *dir = NULL;
	nih_local char  *class_name = NULL;
	char            *p;

	nih_assert (job);
	nih_assert (job->class);

	/* Logging of user job output not currently supported */
	nih_assert (user_job == 0);

	class = job->class;

	nih_assert (class->name);

	/* Override, primarily for tests */
	if (getenv (LOGDIR_ENV) && ! user_mode) {
		dir = nih_strdup (NULL, getenv (LOGDIR_ENV));
		nih_debug ("Using alternative directory '%s' for logs", dir);
	} else {
		dir = nih_strdup (NULL, log_dir ? log_dir : JOB_LOGDIR);
	}

	if (! dir)
		nih_return_no_memory_error (NULL);

	/* If the job is running inside a chroot, it must be logged to a
	 * file within the chroot.
	 */
	if (job->class->session && job->class->session->chroot) {
		char *tmp = dir;

		dir = nih_sprintf (NULL, "%s%s",
				job->class->session->chroot,
				tmp);
		nih_free (tmp);
	}

	class_name = nih_strdup (NULL, class->name);

	if (! class_name)
		nih_return_no_memory_error (NULL);

	/* Remap slashes since we write all logs to the
	 * same directory.
	 */
	p = class_name;

	while (*p) {
		if (*p == JOB_PROCESS_LOG_REMAP_FROM_CHAR)
			*p = JOB_PROCESS_LOG_REMAP_TO_CHAR;
		p++;
	}

	/* Handle jobs with multiple instances */
	if (job->name && *job->name) {
		nih_local char *instance_name = NULL;

		instance_name = nih_strdup (NULL, job->name);
		if (! instance_name)
			nih_return_no_memory_error (NULL);

		p = instance_name;

		while (*p) {
			if (*p == JOB_PROCESS_LOG_REMAP_FROM_CHAR)
				*p = JOB_PROCESS_LOG_REMAP_TO_CHAR;
			p++;
		}


		log_path = nih_sprintf (NULL, "%s/%s-%s%s",
					dir,
					class_name,
					instance_name,
					JOB_PROCESS_LOG_FILE_EXT);
	} else {
		log_path = nih_sprintf (NULL, "%s/%s%s",
					dir,
					class_name,
					JOB_PROCESS_LOG_FILE_EXT);
	}

	if (! log_path)
		nih_return_no_memory_error (NULL);

	return log_path;
}

/**
 * job_process_remap_fd:
 *
 * @fd: pointer to fd to potentially remap,
 * @reserved_fd: reserved fd value,
 * @error_fd: fd to report errors on,
 *
 * Remap @fd to a new value iff it has the same value as @reserved_fd.
 *
 * Errors are reported via @error_fd and are fatal.
 *
 * Notes:
 *
 * - File descriptor flags are not retained.
 * - It is permissible for @error_fd to have the same value as @fd.
 **/
static void
job_process_remap_fd (int *fd, int reserved_fd, int error_fd)
{
	int new = -1;

	nih_assert (fd);
	nih_assert (reserved_fd);
	nih_assert (error_fd >= 0);

	if (*fd != reserved_fd) {
		/* fd does not need remapping */
		return;
	}

	new = dup (*fd);
	if (new < 0) {
		nih_error_raise_system ();
		job_process_error_abort (error_fd, JOB_PROCESS_ERROR_DUP, 0);
	}

	close (*fd);
	*fd = new;
}


/**
 * job_process_child_reader:
 *
 * @process_data: JobProcessHandlerData structure,
 * @io: NihIo with data to be read,
 * @buf: buffer data is available in,
 * @len: bytes in @buf.
 *
 * Called automatically when an error occurred setting up the child process.
 **/
void
job_process_child_reader (JobProcessData  *process_data,
			  NihIo           *io,
			  const char      *buf,
			  size_t           len)
{
	Job                     *job;
	ProcessType              process;
	NihError                *err;

	/* The job is in the process of being killed which must mean all
	 * the job pids are dead. Hence, there is no point in attempting
	 * to talk to them.
	 */
	if (! process_data)
		return;
	if (! process_data->valid)
		return;

	nih_assert (io);
	nih_assert (buf);

	job = process_data->job;
	process = process_data->process;

	/* Construct the NIH error from the data received from
	 * the child.
	 */
	job_process_error_handler (buf, len);

	err = nih_error_get ();

	if (err->number != JOB_PROCESS_ERROR) {
		nih_warn ("%s: %s", _("Temporary process spawn error"),
			  err->message);
		nih_free (err);
		return;
	}

	/* Wilco. Out. */
	nih_io_buffer_shrink (io->recv_buf, len);

	/* Non-temporary error condition */
	nih_warn (_("Failed to spawn %s %s process: %s"),
			job_name (job), process_name (process),
			err->message);
	nih_free (err);

	/* Non-temporary error condition, we're not going
	 * to be able to spawn this job.
	 */
	if (process_data->shell_fd != -1) {
		close (process_data->shell_fd);

		/* Invalidate */
		process_data->shell_fd = -1;
	}

	if (job && job->class->console == CONSOLE_LOG && job->log[process]) {
		/* Ensure the pty_master watch gets
		 * removed and the fd closed.
		 */
		nih_free (job->log[process]);
		job->log[process] = NULL;
	}

	/* Now call the handler registered via job_process_start()
	 * to deal with the error condition.
	 */
	job_child_error_handler (job, process);

	/* Note that pts_master is closed automatically in the parent
	 * when the log object is destroyed.
	 */
	process_data->valid = FALSE;

	nih_io_shutdown (io);

	/* Invalidate */
	process_data->job_process_fd = -1;
}

/**
 * @process_data: JobProcessData,
 * @io: NihIo.
 *
 * NihIoCloseHandler called when job process starts successfully.
 **/
void
job_process_close_handler (JobProcessData  *process_data,
			   NihIo           *io)
{
	Job         *job;
	ProcessType  process;
	int          status;

	/* The job is in the process of being killed which must mean all
	 * the job pids are dead. Hence, there is no point in attempting
	 * to talk to them.
	 */
	if (! process_data)
		return;
	if (! process_data->valid)
		return;

	nih_assert (io);

	job = process_data->job;
	process = process_data->process;
	status = process_data->status;

	/* Ensure the job process error fd is closed before attempting
	 * to handle any scripts.
	 */
	nih_free (io);

	/* invalidate */
	process_data->job_process_fd = -1;
	process_data->valid = FALSE;

	job_process_run_bottom (process_data);

	if (job && job->state == JOB_SPAWNED) {
		if (job->class->expect == EXPECT_NONE) {
			if (process == PROCESS_MAIN) {
				/* Job has not specified expect stanza so will
				 * not have its state automatically progressed
				 * by the ptrace handlers, hence bump it
				 * manually.
				 */
				job_change_state (job, job_next_state (job));
			}
		}
	}
       
	/* Don't change the jobs goal yet as the process may not have
	 * actually terminted (and hence will have
	 * job_process_terminated() called on it again later).
	 */
	switch (job->state) {
	case JOB_SECURITY_SPAWNING:
	case JOB_PRE_STARTING:
	case JOB_SPAWNING:
	case JOB_POST_STARTING:
	case JOB_PRE_STOPPING:
	case JOB_POST_STOPPING:
		job_process_terminated (job, process, status, TRUE);
		break;

	default:
		/* NOP */
		break;
	}
}


/**
 * job_process_run_bottom:
 *
 * @process_data: JobProcessData.
 *
 * Perform final job process setup.
 **/
void
job_process_run_bottom (JobProcessData *process_data)
{
	Job          *job;
	char         *script;
	int           shell_fd;

	nih_assert (process_data);

	job = process_data->job;
	shell_fd = process_data->shell_fd;
	script = process_data->script;

	/* Handle job process containing a script section by sending the
	 * script to the child shell.
	 */
	if (script && shell_fd != -1) {
		NihIo *io;


		/* Put the entire script into an NihIo send buffer and
		 * then mark it for closure so that the shell gets EOF
		 * and the structure gets cleaned up automatically.
		 */
		while (! (io = nih_io_reopen (job, shell_fd, NIH_IO_STREAM,
					      NULL, NULL, NULL, NULL))) {
			NihError *err;

			err = nih_error_get ();

			if (err->number != ENOMEM)
				nih_assert_not_reached ();
			nih_free (err);
		}

		/* We're feeding using a pipe, which has a file descriptor
		 * on the child end even though it open()s it again using
		 * a path. Instruct the shell to close this extra fd and
		 * not to leak it.
		 */
		NIH_ZERO (nih_io_printf (io, "exec %d<&-\n",
					 JOB_PROCESS_SCRIPT_FD));

		NIH_ZERO (nih_io_write (io, script, strlen (script)));

		/* We're only using the io for writing, so clear the
		 * default flag.
		 */
		io->watch->events &= ~NIH_IO_READ;

		nih_io_shutdown (io);

		/* Invalidate now that we've sent the script to the
		 * child.
		 */
		process_data->shell_fd = -1;
		process_data->valid = FALSE;
	}
}


/**
 * job_process_data_new:
 *
 * @parent: parent pointer,
 * @job: job,
 * @process: currently-running job process,
 * @job_process_fd: error file descriptor connected to job process used
 * to signal successful job process setup.
 *
 * Create a new job process data object to record meta-data for an
 * asynchronously-started job process. The lifetime of such objects is
 * limited to the time taken for the job process to fail or indicate
 * successful startup (both scenarios are handled by @job_process_fd).
 *
 * Returns: Newly-allocated JobProcess on success, or NULL on
 * insufficient memory.
 **/
JobProcessData *
job_process_data_new (void         *parent,
		      Job          *job,
		      ProcessType   process,
		      int           job_process_fd)
{
	JobProcessData *process_data;

	nih_assert (job);
	nih_assert (job_process_fd != -1);

	process_data = NIH_MUST (nih_new (parent, JobProcessData));
	if (! process_data)
		return NULL;

	process_data->process = process;
	process_data->job_process_fd = job_process_fd;

	process_data->script = NULL;
	process_data->shell_fd = -1;
	process_data->valid = TRUE;

	process_data->job = job;
	process_data->status = 0;

	return process_data;
}

/**
 * job_process_data_serialise:
 *
 * @job: job,
 * @process_data: job process data.
 *
 * Returns: JSON-serialised JobProcessData object, or NULL on error.
 **/
json_object *
job_process_data_serialise (const Job *job, const JobProcessData *process_data)
{
	json_object     *json;

	nih_assert (job);

	json = json_object_new_object ();
	if (! json)
		return NULL;

	/* Job has no "in-flight" process */
	if (! process_data)
		return json;

	nih_assert (process_data->job_process_fd != -1);

	/* XXX: Note that Job is not serialised; it's not necessary
	 * since the JobProcessData itself is serialised within its Job
	 * and the job pointer can be reinstated on deserialisation.
	 */

	if (! state_set_json_enum_var (json,
				process_type_enum_to_str,
				"process", process_data->process))
		goto error;

	if (! state_set_json_string_var_from_obj (json, process_data, script))
		goto error;

	/* Clear the cloexec flag to ensure the descriptors
	 * remains open across the re-exec.
	 */
	if (process_data->shell_fd != -1) {
		if (state_modify_cloexec (process_data->shell_fd, FALSE) < 0)
			goto error;
	}

	if (state_modify_cloexec (process_data->job_process_fd, FALSE) < 0)
		goto error;

	if (! state_set_json_int_var_from_obj (json, process_data, shell_fd))
		goto error;

	if (! state_set_json_int_var_from_obj (json, process_data, job_process_fd))
		goto error;

	if (! state_set_json_int_var_from_obj (json, process_data, status))
		goto error;

	if (! state_set_json_int_var_from_obj (json, process_data, valid))
		goto error;

	return json;

error:
	json_object_put (json);
	return NULL;
}

/**
 * job_process_data_deserialise:
 *
 * @parent: parent pointer,
 * @job: job,
 * @json: JSON-serialised JobProcessData object to deserialise.
 *
 * Returns: JobProcessData object, or NULL on error.
 **/
JobProcessData *
job_process_data_deserialise (void *parent, Job *job, json_object *json)
{
	JobProcessData  *process_data = NULL;
	ProcessType      process;
	int              job_process_fd;

	nih_assert (job);
	nih_assert (json);

	if (! state_check_json_type (json, object))
		return NULL;

	if (! state_get_json_enum_var (json,
				process_type_str_to_enum,
				"process", process))
		return NULL;

	if (! state_get_json_int_var (json, "job_process_fd", job_process_fd))
		return NULL;

	process_data = job_process_data_new (parent, job, process, job_process_fd);
	if (! process_data)
		return NULL;

	if (! state_get_json_string_var_to_obj (json, process_data, script))
		goto error;

	if (! state_get_json_int_var_to_obj (json, process_data, shell_fd))
		goto error;

	if (! state_get_json_int_var_to_obj (json, process_data, status))
		goto error;

	if (! state_get_json_int_var_to_obj (json, process_data, valid))
		goto error;

	/* Reset the cloexec flag to ensure the descriptors
	 * are not leaked to any child processes.
	 */
	if (process_data->shell_fd != -1) {
		if (state_modify_cloexec (process_data->shell_fd, TRUE) < 0)
			goto error;
	}

	if (state_modify_cloexec (process_data->job_process_fd, TRUE) < 0)
		goto error;

	return process_data;

error:
	if (process_data)
		nih_free (process_data);

	return NULL;
}
