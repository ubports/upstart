/* upstart
 *
 * test_job_process.c - test suite for init/job_process.c
 *
 * Copyright © 2009 Canonical Ltd.
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

#include <nih/test.h>

#if HAVE_VALGRIND_VALGRIND_H
#include <valgrind/valgrind.h>
#endif /* HAVE_VALGRIND_VALGRIND_H */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/io.h>
#include <nih/main.h>
#include <nih/error.h>

#include "job_process.h"
#include "job.h"
#include "event.h"
#include "blocked.h"
#include "conf.h"
#include "errors.h"


/* Sadly we can't test everything that job_process_spawn() does simply because
 * a lot of it can only be done by root, or in the case of the console stuff,
 * kills whatever had /dev/console (usually X).
 *
 * This set of tests at least ensures some level of code coverage.
 */
enum child_tests {
	TEST_SIMPLE,
	TEST_PIDS,
	TEST_CONSOLE,
	TEST_PWD,
	TEST_ENVIRONMENT
};

static char *argv0;

static void
child (enum child_tests  test,
       const char       *filename)
{
	FILE *out;
	char  tmpname[PATH_MAX], path[PATH_MAX];
	int   i;

	strcpy (tmpname, filename);
	strcat (tmpname, ".tmp");

	out = fopen (tmpname, "w");

	switch (test) {
	case TEST_SIMPLE:
		break;
	case TEST_PIDS:
		fprintf (out, "pid: %d\n", getpid ());
		fprintf (out, "ppid: %d\n", getppid ());
		fprintf (out, "pgrp: %d\n", getpgrp ());
		fprintf (out, "sid: %d\n", getsid (0));
		break;
	case TEST_CONSOLE:
		for (i = 0; i < 3; i++) {
			struct stat buf;

			fstat (i, &buf);
			fprintf (out, "%d: %d %d\n", i,
				 major (buf.st_rdev),
				 minor (buf.st_rdev));
		}
		break;
	case TEST_PWD:
		assert (getcwd (path, sizeof (path)));
		fprintf (out, "wd: %s\n", path);
		break;
	case TEST_ENVIRONMENT:
		for (char **env = environ; *env; env++)
			fprintf (out, "%s\n", *env);
		break;
	}

	fsync (fileno (out));
	fclose (out);

	rename (tmpname, filename);

	exit (0);
}


void
test_run (void)
{
	JobClass   *class = NULL;
	Job         *job = NULL;
	FILE        *output;
	struct stat  statbuf;
	char         filename[PATH_MAX], buf[80];
	int          ret = -1, status, first;
	siginfo_t    info;

	TEST_FUNCTION ("job_process_run");
	job_class_init ();
	nih_error_init ();
	nih_io_init ();

	TEST_FILENAME (filename);
	program_name = "test";

	/* Check that we can run a simple command, and have the process id
	 * and state filled in.  We should be able to wait for the pid to
	 * finish and see that it has been run as expected.
	 */
	TEST_FEATURE ("with simple command");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			class = job_class_new (NULL, "test");
			class->leader = TRUE;
			class->process[PROCESS_MAIN] = process_new (class);
			class->process[PROCESS_MAIN]->command = nih_sprintf (
				class->process[PROCESS_MAIN],
				"touch %s", filename);

			job = job_new (class, "");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
		}

		ret = job_process_run (job, PROCESS_MAIN);
		TEST_EQ (ret, 0);

		TEST_NE (job->pid[PROCESS_MAIN], 0);

		waitpid (job->pid[PROCESS_MAIN], NULL, 0);
		TEST_EQ (stat (filename, &statbuf), 0);

		unlink (filename);
		nih_free (class);
	}


	/* Check that we can run a command that requires a shell to be
	 * intepreted correctly, a shell should automatically be used to
	 * make this work.  Check the contents of a file we'll create to
	 * check that a shell really was used.
	 */
	TEST_FEATURE ("with shell command");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			class = job_class_new (NULL, "test");
			class->leader = TRUE;
			class->process[PROCESS_MAIN] = process_new (class);
			class->process[PROCESS_MAIN]->command = nih_sprintf (
				class->process[PROCESS_MAIN],
				"echo $$ > %s\n", filename);

			job = job_new (class, "");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
		}

		ret = job_process_run (job, PROCESS_MAIN);
		TEST_EQ (ret, 0);

		TEST_NE (job->pid[PROCESS_MAIN], 0);

		waitpid (job->pid[PROCESS_MAIN], NULL, 0);
		TEST_EQ (stat (filename, &statbuf), 0);

		/* Filename should contain the pid */
		output = fopen (filename, "r");
		sprintf (buf, "%d\n", job->pid[PROCESS_MAIN]);
		TEST_FILE_EQ (output, buf);
		TEST_FILE_END (output);
		fclose (output);
		unlink (filename);

		nih_free (class);
	}

	/* Check that we can run a small shell script, and that it's run
	 * by using the shell directly and passing the script in on the
	 * command-line.
	 */
	TEST_FEATURE ("with small script");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			class = job_class_new (NULL, "test");
			class->leader = TRUE;
			class->process[PROCESS_MAIN] = process_new (class);
			class->process[PROCESS_MAIN]->script = TRUE;
			class->process[PROCESS_MAIN]->command = nih_sprintf (
				class->process[PROCESS_MAIN],
				"echo $0 $@ > %s\n", filename);

			job = job_new (class, "");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
		}

		ret = job_process_run (job, PROCESS_MAIN);
		TEST_EQ (ret, 0);

		TEST_NE (job->pid[PROCESS_MAIN], 0);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		output = fopen (filename, "r");
		TEST_FILE_EQ (output, "/bin/sh\n");
		TEST_FILE_END (output);
		fclose (output);
		unlink (filename);

		nih_free (class);
	}


	/* Check that we can run a small shell script that has many newlines
	 * to be stripped from the end before passing it on the command-line.
	 */
	TEST_FEATURE ("with small script and trailing newlines");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			class = job_class_new (NULL, "test");
			class->leader = TRUE;
			class->process[PROCESS_MAIN] = process_new (class);
			class->process[PROCESS_MAIN]->script = TRUE;
			class->process[PROCESS_MAIN]->command = nih_sprintf (
				class->process[PROCESS_MAIN],
				"echo $0 $@ > %s\n\n\n", filename);

			job = job_new (class, "");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
		}

		ret = job_process_run (job, PROCESS_MAIN);
		TEST_EQ (ret, 0);

		TEST_NE (job->pid[PROCESS_MAIN], 0);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		output = fopen (filename, "r");
		TEST_FILE_EQ (output, "/bin/sh\n");
		TEST_FILE_END (output);
		fclose (output);
		unlink (filename);

		nih_free (class);
	}


	/* Check that shell scripts are run with the -e option set, so that
	 * any failing command causes the entire script to fail.
	 */
	TEST_FEATURE ("with script that will fail");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			class = job_class_new (NULL, "test");
			class->leader = TRUE;
			class->process[PROCESS_MAIN] = process_new (class);
			class->process[PROCESS_MAIN]->script = TRUE;
			class->process[PROCESS_MAIN]->command = nih_sprintf (
				class->process[PROCESS_MAIN],
				"test -d %s > %s\n",
				filename, filename);

			job = job_new (class, "");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
		}

		ret = job_process_run (job, PROCESS_MAIN);
		TEST_EQ (ret, 0);

		TEST_NE (job->pid[PROCESS_MAIN], 0);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 1);

		output = fopen (filename, "r");
		TEST_FILE_END (output);
		fclose (output);
		unlink (filename);

		nih_free (class);
	}


	/* Check that a job is run with the environment from its env member,
	 * with the job name appended to it.
	 */
	TEST_FEATURE ("with environment of unnamed instance");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			class = job_class_new (NULL, "test");
			class->leader = TRUE;
			class->process[PROCESS_MAIN] = process_new (class);
			class->process[PROCESS_MAIN]->command = nih_sprintf (
				class->process[PROCESS_MAIN],
				"%s %d %s", argv0, TEST_ENVIRONMENT, filename);

			job = job_new (class, "");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;

			assert (nih_str_array_add (&job->env, job, NULL, "FOO=BAR"));
			assert (nih_str_array_add (&job->env, job, NULL, "BAR=BAZ"));

			assert (nih_str_array_add (&job->stop_env, job, NULL,
						   "FOO=SMACK"));
			assert (nih_str_array_add (&job->stop_env, job, NULL,
						   "CRACKLE=FIZZ"));
		}

		ret = job_process_run (job, PROCESS_MAIN);
		TEST_EQ (ret, 0);

		TEST_NE (job->pid[PROCESS_MAIN], 0);

		waitpid (job->pid[PROCESS_MAIN], NULL, 0);
		TEST_EQ (stat (filename, &statbuf), 0);

		/* Read back the environment to make sure it matched that from
		 * the job.
		 */
		output = fopen (filename, "r");
		TEST_FILE_EQ (output, "FOO=BAR\n");
		TEST_FILE_EQ (output, "BAR=BAZ\n");
		TEST_FILE_EQ (output, "UPSTART_JOB=test\n");
		TEST_FILE_EQ (output, "UPSTART_INSTANCE=\n");
		TEST_FILE_END (output);
		fclose (output);
		unlink (filename);

		nih_free (class);
	}


	/* Check that a job is run with the environment from its env member,
	 * with the job name and instance name appended to it.
	 */
	TEST_FEATURE ("with environment of named instance");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			class = job_class_new (NULL, "test");
			class->leader = TRUE;
			class->process[PROCESS_MAIN] = process_new (class);
			class->process[PROCESS_MAIN]->command = nih_sprintf (
				class->process[PROCESS_MAIN],
				"%s %d %s", argv0, TEST_ENVIRONMENT, filename);

			job = job_new (class, "foo");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;

			assert (nih_str_array_add (&job->env, job, NULL, "FOO=BAR"));
			assert (nih_str_array_add (&job->env, job, NULL, "BAR=BAZ"));

			assert (nih_str_array_add (&job->stop_env, job, NULL,
						   "FOO=SMACK"));
			assert (nih_str_array_add (&job->stop_env, job, NULL,
						   "CRACKLE=FIZZ"));
		}

		ret = job_process_run (job, PROCESS_MAIN);
		TEST_EQ (ret, 0);

		TEST_NE (job->pid[PROCESS_MAIN], 0);

		waitpid (job->pid[PROCESS_MAIN], NULL, 0);
		TEST_EQ (stat (filename, &statbuf), 0);

		/* Read back the environment to make sure it matched that from
		 * the job.
		 */
		output = fopen (filename, "r");
		TEST_FILE_EQ (output, "FOO=BAR\n");
		TEST_FILE_EQ (output, "BAR=BAZ\n");
		TEST_FILE_EQ (output, "UPSTART_JOB=test\n");
		TEST_FILE_EQ (output, "UPSTART_INSTANCE=foo\n");
		TEST_FILE_END (output);
		fclose (output);
		unlink (filename);

		nih_free (class);
	}


	/* Check that the pre-stop job is run with the environment from the
	 * stop_env member as well as from the env member, overriding where
	 * necessary, and the job name and id appended.
	 */
	TEST_FEATURE ("with environment for pre-stop");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			class = job_class_new (NULL, "test");
			class->leader = TRUE;
			class->process[PROCESS_PRE_STOP] = process_new (class);
			class->process[PROCESS_PRE_STOP]->command = nih_sprintf (
				class->process[PROCESS_PRE_STOP],
				"%s %d %s", argv0, TEST_ENVIRONMENT, filename);

			job = job_new (class, "");
			job->goal = JOB_STOP;
			job->state = JOB_PRE_STOP;

			assert (nih_str_array_add (&job->env, job, NULL, "FOO=BAR"));
			assert (nih_str_array_add (&job->env, job, NULL, "BAR=BAZ"));

			assert (nih_str_array_add (&job->stop_env, job, NULL,
						   "FOO=SMACK"));
			assert (nih_str_array_add (&job->stop_env, job, NULL,
						   "CRACKLE=FIZZ"));
		}

		ret = job_process_run (job, PROCESS_PRE_STOP);
		TEST_EQ (ret, 0);

		TEST_NE (job->pid[PROCESS_PRE_STOP], 0);

		waitpid (job->pid[PROCESS_PRE_STOP], NULL, 0);
		TEST_EQ (stat (filename, &statbuf), 0);

		/* Read back the environment to make sure it matched that from
		 * the job.
		 */
		output = fopen (filename, "r");
		TEST_FILE_EQ (output, "FOO=SMACK\n");
		TEST_FILE_EQ (output, "BAR=BAZ\n");
		TEST_FILE_EQ (output, "CRACKLE=FIZZ\n");
		TEST_FILE_EQ (output, "UPSTART_JOB=test\n");
		TEST_FILE_EQ (output, "UPSTART_INSTANCE=\n");
		TEST_FILE_END (output);
		fclose (output);
		unlink (filename);

		nih_free (class);
	}


	/* Check that the post-stop job is run with the environment from the
	 * stop_env member as well as from the env member, overriding where
	 * necessary, and the job name and id appended.
	 */
	TEST_FEATURE ("with environment for post-stop");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			class = job_class_new (NULL, "test");
			class->leader = TRUE;
			class->process[PROCESS_POST_STOP] = process_new (class);
			class->process[PROCESS_POST_STOP]->command = nih_sprintf (
				class->process[PROCESS_POST_STOP],
				"%s %d %s", argv0, TEST_ENVIRONMENT, filename);

			job = job_new (class, "");
			job->goal = JOB_STOP;
			job->state = JOB_POST_STOP;

			assert (nih_str_array_add (&job->env, job, NULL, "FOO=BAR"));
			assert (nih_str_array_add (&job->env, job, NULL, "BAR=BAZ"));

			assert (nih_str_array_add (&job->stop_env, job, NULL,
						   "FOO=SMACK"));
			assert (nih_str_array_add (&job->stop_env, job, NULL,
						   "CRACKLE=FIZZ"));
		}

		ret = job_process_run (job, PROCESS_POST_STOP);
		TEST_EQ (ret, 0);

		TEST_NE (job->pid[PROCESS_POST_STOP], 0);

		waitpid (job->pid[PROCESS_POST_STOP], NULL, 0);
		TEST_EQ (stat (filename, &statbuf), 0);

		/* Read back the environment to make sure it matched that from
		 * the job.
		 */
		output = fopen (filename, "r");
		TEST_FILE_EQ (output, "FOO=SMACK\n");
		TEST_FILE_EQ (output, "BAR=BAZ\n");
		TEST_FILE_EQ (output, "CRACKLE=FIZZ\n");
		TEST_FILE_EQ (output, "UPSTART_JOB=test\n");
		TEST_FILE_EQ (output, "UPSTART_INSTANCE=\n");
		TEST_FILE_END (output);
		fclose (output);
		unlink (filename);

		nih_free (class);
	}


	if (stat ("/dev/fd", &statbuf) < 0) {
		printf ("SKIP: no /dev/fd\n");
		goto no_devfd;
	}

	/* Check that a particularly long script is instead invoked by
	 * using the /dev/fd feature, with the shell script fed to the
	 * child process by an NihIo structure.
	 */
	TEST_FEATURE ("with long script");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			class = job_class_new (NULL, "test");
			class->leader = TRUE;
			class->process[PROCESS_MAIN] = process_new (class);
			class->process[PROCESS_MAIN]->script = TRUE;
			class->process[PROCESS_MAIN]->command = nih_alloc (
				class->process[PROCESS_MAIN], 4096);
			sprintf (class->process[PROCESS_MAIN]->command,
				 "exec > %s\necho $0\necho $@\n", filename);

			job = job_new (class, "");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
		}

		ret = job_process_run (job, PROCESS_MAIN);
		TEST_EQ (ret, 0);

		TEST_NE (job->pid[PROCESS_MAIN], 0);

		/* Loop until we've fed all of the data. */
		first = TRUE;
		for (;;) {
			fd_set readfds, writefds, exceptfds;
			int    nfds;

			nfds = 0;
			FD_ZERO (&readfds);
			FD_ZERO (&writefds);
			FD_ZERO (&exceptfds);

			nih_io_select_fds (&nfds, &readfds,
					   &writefds, &exceptfds);
			if (! nfds) {
				if (first)
					TEST_FAILED ("expected to have "
						     "data to feed.");
				break;
			}
			first = FALSE;

			select (nfds, &readfds, &writefds, &exceptfds, NULL);

			nih_io_handle_fds (&readfds, &writefds, &exceptfds);
		}

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		output = fopen (filename, "r");
		TEST_FILE_EQ_N (output, "/dev/fd/");
		TEST_FILE_EQ (output, "\n");
		TEST_FILE_END (output);
		fclose (output);
		unlink (filename);

		nih_free (class);
	}


no_devfd:
	/* Check that if we're running a non-daemon job, the trace state
	 * is reset and no process trace is established.
	 */
	TEST_FEATURE ("with non-daemon job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			class = job_class_new (NULL, "test");
			class->leader = TRUE;
			class->process[PROCESS_MAIN] = process_new (class);
			class->process[PROCESS_MAIN]->command = "true";

			job = job_new (class, "");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;

			job->trace_forks = 2;
			job->trace_state = TRACE_NORMAL;
		}

		ret = job_process_run (job, PROCESS_MAIN);
		TEST_EQ (ret, 0);

		TEST_EQ (job->trace_forks, 0);
		TEST_EQ (job->trace_state, TRACE_NONE);

		TEST_NE (job->pid[PROCESS_MAIN], 0);

		assert0 (waitid (P_PID, job->pid[PROCESS_MAIN], &info,
				 WEXITED | WSTOPPED));
		TEST_EQ (info.si_pid, job->pid[PROCESS_MAIN]);
		TEST_EQ (info.si_code, CLD_EXITED);
		TEST_EQ (info.si_status, 0);

		nih_free (class);
	}


	/* Check that if we're running a script for a daemon job, the
	 * trace state is reset and no process trace is established.
	 */
	TEST_FEATURE ("with script for daemon job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			class = job_class_new (NULL, "test");
			class->leader = TRUE;
			class->process[PROCESS_PRE_START] = process_new (class);
			class->process[PROCESS_PRE_START]->command = "true";

			job = job_new (class, "");
			job->goal = JOB_START;
			job->state = JOB_PRE_START;

			job->trace_forks = 2;
			job->trace_state = TRACE_NORMAL;
		}

		ret = job_process_run (job, PROCESS_PRE_START);
		TEST_EQ (ret, 0);

		TEST_EQ (job->trace_forks, 0);
		TEST_EQ (job->trace_state, TRACE_NONE);

		TEST_NE (job->pid[PROCESS_PRE_START], 0);

		assert0 (waitid (P_PID, job->pid[PROCESS_PRE_START], &info,
				 WEXITED | WSTOPPED));
		TEST_EQ (info.si_pid, job->pid[PROCESS_PRE_START]);
		TEST_EQ (info.si_code, CLD_EXITED);
		TEST_EQ (info.si_status, 0);

		nih_free (class);
	}


	/* Check that if we're running a daemon job, the trace state
	 * is reset and a process trace is established so that we can
	 * follow the forks.
	 */
	TEST_FEATURE ("with daemon job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			class = job_class_new (NULL, "test");
			class->leader = TRUE;
			class->expect = EXPECT_DAEMON;
			class->process[PROCESS_MAIN] = process_new (class);
			class->process[PROCESS_MAIN]->command = "true";

			job = job_new (class, "");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;

			job->trace_forks = 2;
			job->trace_state = TRACE_NORMAL;
		}

		ret = job_process_run (job, PROCESS_MAIN);
		TEST_EQ (ret, 0);

		TEST_EQ (job->trace_forks, 0);
		TEST_EQ (job->trace_state, TRACE_NEW);

		TEST_NE (job->pid[PROCESS_MAIN], 0);

		assert0 (waitid (P_PID, job->pid[PROCESS_MAIN], &info,
				 WEXITED | WSTOPPED));
		TEST_EQ (info.si_pid, job->pid[PROCESS_MAIN]);
		TEST_EQ (info.si_code, CLD_TRAPPED);
		TEST_EQ (info.si_status, SIGTRAP);

		assert0 (ptrace (PTRACE_DETACH, job->pid[PROCESS_MAIN],
				 NULL, 0));

		assert0 (waitid (P_PID, job->pid[PROCESS_MAIN], &info,
				 WEXITED | WSTOPPED));
		TEST_EQ (info.si_pid, job->pid[PROCESS_MAIN]);
		TEST_EQ (info.si_code, CLD_EXITED);
		TEST_EQ (info.si_status, 0);

		nih_free (class);
	}


	/* Check that if we're running a forking job, the trace state
	 * is reset and a process trace is established so that we can
	 * follow the fork.
	 */
	TEST_FEATURE ("with forking job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			class = job_class_new (NULL, "test");
			class->leader = TRUE;
			class->expect = EXPECT_FORK;
			class->process[PROCESS_MAIN] = process_new (class);
			class->process[PROCESS_MAIN]->command = "true";

			job = job_new (class, "");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;

			job->trace_forks = 2;
			job->trace_state = TRACE_NORMAL;
		}

		ret = job_process_run (job, PROCESS_MAIN);
		TEST_EQ (ret, 0);

		TEST_EQ (job->trace_forks, 0);
		TEST_EQ (job->trace_state, TRACE_NEW);

		TEST_NE (job->pid[PROCESS_MAIN], 0);

		assert0 (waitid (P_PID, job->pid[PROCESS_MAIN], &info,
				 WEXITED | WSTOPPED));
		TEST_EQ (info.si_pid, job->pid[PROCESS_MAIN]);
		TEST_EQ (info.si_code, CLD_TRAPPED);
		TEST_EQ (info.si_status, SIGTRAP);

		assert0 (ptrace (PTRACE_DETACH, job->pid[PROCESS_MAIN],
				 NULL, 0));

		assert0 (waitid (P_PID, job->pid[PROCESS_MAIN], &info,
				 WEXITED | WSTOPPED));
		TEST_EQ (info.si_pid, job->pid[PROCESS_MAIN]);
		TEST_EQ (info.si_code, CLD_EXITED);
		TEST_EQ (info.si_status, 0);

		nih_free (class);
	}


	/* Check that if we try and run a command that doesn't exist,
	 * job_process_run() raises a ProcessError and the command doesn't
	 * have any stored process id for it.
	 */
	TEST_FEATURE ("with no such file");
	output = tmpfile ();

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			class = job_class_new (NULL, "test");
			class->leader = TRUE;
			class->process[PROCESS_MAIN] = process_new (class);
			class->process[PROCESS_MAIN]->command = filename;

			job = job_new (class, "foo");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
		}

		TEST_DIVERT_STDERR (output) {
			ret = job_process_run (job, PROCESS_MAIN);
		}
		rewind (output);
		TEST_LT (ret, 0);

		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_FILE_EQ (output, ("test: Failed to spawn test (foo) main "
				       "process: unable to execute: "
				       "No such file or directory\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (class);
	}
}


void
test_spawn (void)
{
	FILE             *output;
	char              function[PATH_MAX], filename[PATH_MAX];
	char              buf[80], filebuf[80];
	struct stat       statbuf;
	char             *env[3], *args[4];
	JobClass         *class;
	pid_t             pid, ppid, pgrp;
	siginfo_t         info;
	NihError         *err;
	JobProcessError  *perr;

	TEST_FUNCTION ("job_process_spawn");
	TEST_FILENAME (filename);

	args[0] = argv0;
	args[1] = function;
	args[2] = filename;
	args[3] = NULL;

	/* Check that we can spawn a simple job, since this will not be a
	 * session leader, we spin for the child process to complete and
	 * then read from the file written to check that the process tree
	 * is what we expect it to look like.
	 */
	TEST_FEATURE ("with simple job");
	sprintf (function, "%d", TEST_PIDS);

	class = job_class_new (NULL, "test");

	pid = job_process_spawn (class, args, NULL, FALSE);
	TEST_GT (pid, 0);

	while (stat (filename, &statbuf) < 0)
		;

	output = fopen (filename, "r");

	TEST_GT (pid, 0);
	TEST_NE (pid, getpid ());

	sprintf (buf, "pid: %d\n", pid);
	TEST_FILE_EQ (output, buf);

	/* Get the parent process id out, it may be 1 or an intermediate
	 * depending on racy things
	 */
	if (! fgets (filebuf, sizeof (filebuf), output))
		TEST_FAILED ("eof on file %p (output), expected pgrp line",
			     output);

	TEST_EQ_STRN (filebuf, "ppid: ");
	sscanf (filebuf, "ppid: %d\n", &ppid);
	TEST_NE (ppid, pid);
	TEST_NE (ppid, getpid ());

	/* Get the process group id out, it must only ever be an intermediate
	 * and must match parent id unless that was 1.
	 */
	if (! fgets (filebuf, sizeof (filebuf), output))
		TEST_FAILED ("eof on file %p (output), expected pgrp line",
			     output);

	TEST_EQ_STRN (filebuf, "pgrp: ");
	sscanf (filebuf, "pgrp: %d\n", &pgrp);
	TEST_NE (pgrp, pid);
	TEST_NE (pgrp, getpid ());
	if (ppid != 1)
		TEST_EQ (pgrp, ppid);

	/* Session id must match process group - compare normally */
	sprintf (buf, "sid: %d\n", pgrp);
	TEST_FILE_EQ (output, buf);

	TEST_FILE_END (output);

	fclose (output);
	unlink (filename);

	nih_free (class);


	/* Check that we can spawn a job we expect to be the session
	 * leader, again wait for the child process and read from the file
	 * written to check the process tree is shwat we expect it to look
	 * like.
	 */
	TEST_FEATURE ("with session leader");
	sprintf (function, "%d", TEST_PIDS);

	class = job_class_new (NULL, "test");
	class->leader = TRUE;

	pid = job_process_spawn (class, args, NULL, FALSE);
	TEST_GT (pid, 0);

	waitpid (pid, NULL, 0);
	output = fopen (filename, "r");

	TEST_GT (pid, 0);
	TEST_NE (pid, getpid ());

	sprintf (buf, "pid: %d\n", pid);
	TEST_FILE_EQ (output, buf);

	sprintf (buf, "ppid: %d\n", getpid ());
	TEST_FILE_EQ (output, buf);

	sprintf (buf, "pgrp: %d\n", pid);
	TEST_FILE_EQ (output, buf);

	sprintf (buf, "sid: %d\n", pid);
	TEST_FILE_EQ (output, buf);

	TEST_FILE_END (output);

	fclose (output);
	unlink (filename);

	nih_free (class);


	/* Check that a job spawned with no console has the file descriptors
	 * bound to the /dev/null device.
	 */
	TEST_FEATURE ("with no console");
	sprintf (function, "%d", TEST_CONSOLE);

	class = job_class_new (NULL, "test");
	class->leader = TRUE;
	class->console = CONSOLE_NONE;

	pid = job_process_spawn (class, args, NULL, FALSE);
	TEST_GT (pid, 0);

	waitpid (pid, NULL, 0);
	output = fopen (filename, "r");

	TEST_FILE_EQ (output, "0: 1 3\n");
	TEST_FILE_EQ (output, "1: 1 3\n");
	TEST_FILE_EQ (output, "2: 1 3\n");
	TEST_FILE_END (output);

	fclose (output);
	unlink (filename);

	nih_free (class);


	/* Check that a job with an alternate working directory is run from
	 * that directory.
	 */
	TEST_FEATURE ("with working directory");
	sprintf (function, "%d", TEST_PWD);

	class = job_class_new (NULL, "test");
	class->leader = TRUE;
	class->chdir = "/tmp";

	pid = job_process_spawn (class, args, NULL, FALSE);
	TEST_GT (pid, 0);

	waitpid (pid, NULL, 0);
	output = fopen (filename, "r");

	TEST_FILE_EQ (output, "wd: /tmp\n");
	TEST_FILE_END (output);

	fclose (output);
	unlink (filename);

	nih_free (class);


	/* Check that a job is run with only the environment variables
	 * specifiec in the function call.
	 */
	TEST_FEATURE ("with environment");
	sprintf (function, "%d", TEST_ENVIRONMENT);
	setenv ("BAR", "baz", TRUE);

	env[0] = "PATH=/bin";
	env[1] = "FOO=bar";
	env[2] = NULL;

	class = job_class_new (NULL, "test");
	class->leader = TRUE;

	pid = job_process_spawn (class, args, env, FALSE);
	TEST_GT (pid, 0);

	waitpid (pid, NULL, 0);
	output = fopen (filename, "r");

	TEST_FILE_EQ (output, "PATH=/bin\n");
	TEST_FILE_EQ (output, "FOO=bar\n");
	TEST_FILE_END (output);

	fclose (output);
	unlink (filename);

	nih_free (class);


	/* Check that when we spawn an ordinary job, it isn't usually ptraced
	 * since that's a special honour reserved for daemons that we expect
	 * to fork.
	 */
	TEST_FEATURE ("with non-daemon job");
	sprintf (function, "%d", TEST_SIMPLE);

	class = job_class_new (NULL, "test");
	class->leader = TRUE;

	pid = job_process_spawn (class, args, NULL, FALSE);
	TEST_GT (pid, 0);

	assert0 (waitid (P_PID, pid, &info, WEXITED | WSTOPPED | WCONTINUED));
	TEST_EQ (info.si_code, CLD_EXITED);
	TEST_EQ (info.si_status, 0);

	unlink (filename);

	nih_free (class);


	/* Check that when we spawn a daemon job, we can request that the
	 * parent be traced.
	 */
	TEST_FEATURE ("with daemon job");
	sprintf (function, "%d", TEST_SIMPLE);

	class = job_class_new (NULL, "test");
	class->leader = TRUE;

	pid = job_process_spawn (class, args, NULL, TRUE);
	TEST_GT (pid, 0);

	assert0 (waitid (P_PID, pid, &info, WEXITED | WSTOPPED | WCONTINUED));
	TEST_EQ (info.si_code, CLD_TRAPPED);
	TEST_EQ (info.si_status, SIGTRAP);

	assert0 (ptrace (PTRACE_DETACH, pid, NULL, 0));

	assert0 (waitid (P_PID, pid, &info, WEXITED | WSTOPPED | WCONTINUED));
	TEST_EQ (info.si_code, CLD_EXITED);
	TEST_EQ (info.si_status, 0);

	unlink (filename);

	nih_free (class);


	/* Check that attempting to spawn a binary that doesn't exist returns
	 * an error immediately with all of the expected information in the
	 * error structure.
	 */
	TEST_FEATURE ("with no such file");
	args[0] = filename;
	args[1] = filename;
	args[2] = NULL;

	class = job_class_new (NULL, "test");
	class->leader = TRUE;

	pid = job_process_spawn (class, args, NULL, FALSE);
	TEST_LT (pid, 0);

	err = nih_error_get ();
	TEST_EQ (err->number, JOB_PROCESS_ERROR);
	TEST_ALLOC_SIZE (err, sizeof (JobProcessError));

	perr = (JobProcessError *)err;
	TEST_EQ (perr->type, JOB_PROCESS_ERROR_EXEC);
	TEST_EQ (perr->arg, 0);
	TEST_EQ (perr->errnum, ENOENT);
	nih_free (perr);

	nih_free (class);
}


void
test_kill (void)
{
	JobClass *class;
	Job       *job = NULL;
	NihTimer  *timer;
	pid_t      pid;
	int        status;

	TEST_FUNCTION ("job_process_kill");
	nih_timer_init ();
	event_init ();

	class = job_class_new (NULL, "test");
	class->leader = TRUE;
	class->kill_timeout = 1000;

	class->process[PROCESS_MAIN] = process_new (class);
	class->process[PROCESS_MAIN]->command = nih_strdup (
		class->process[PROCESS_MAIN], "echo");


	/* Check that an easily killed process goes away with just a single
	 * call to job_process_kill, having received the TERM signal.
	 * A kill timer should be set to handle the case where the child
	 * doesn't get reaped.
	 */
	TEST_FEATURE ("with easily killed process");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");
		}

		job->goal = JOB_STOP;
		job->state = JOB_KILLED;
		TEST_CHILD (job->pid[PROCESS_MAIN]) {
			pause ();
		}
		pid = job->pid[PROCESS_MAIN];
		setpgid (pid, pid);

		job_process_kill (job, PROCESS_MAIN);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_KILLED);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFSIGNALED (status));
		TEST_EQ (WTERMSIG (status), SIGTERM);

		TEST_NE_P (job->kill_timer, NULL);
		TEST_ALLOC_SIZE (job->kill_timer, sizeof (NihTimer));
		TEST_ALLOC_PARENT (job->kill_timer, job);
		TEST_GE (job->kill_timer->due, time (NULL) + 950);
		TEST_LE (job->kill_timer->due, time (NULL) + 1000);

		TEST_EQ (job->kill_process, PROCESS_MAIN);

		nih_free (job->kill_timer);
		job->kill_timer = NULL;
		job->kill_process = -1;

		nih_free (job);

		event_poll ();
	}


	/* Check that a process that's hard to kill doesn't go away, but
	 * that the kill timer sends the KILL signal which should finally
	 * get rid of it.
	 */
	TEST_FEATURE ("with hard to kill process");
	TEST_ALLOC_FAIL {
		int wait_fd = 0;

		TEST_ALLOC_SAFE {
			job = job_new (class, "");
		}

		job->goal = JOB_STOP;
		job->state = JOB_KILLED;
		TEST_CHILD_WAIT (job->pid[PROCESS_MAIN], wait_fd) {
			struct sigaction act;

			act.sa_handler = SIG_IGN;
			act.sa_flags = 0;
			sigemptyset (&act.sa_mask);
			sigaction (SIGTERM, &act, NULL);

			TEST_CHILD_RELEASE (wait_fd);

			for (;;)
				pause ();
		}
		pid = job->pid[PROCESS_MAIN];
		setpgid (pid, pid);

		job_process_kill (job, PROCESS_MAIN);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_KILLED);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);

		TEST_EQ (kill (job->pid[PROCESS_MAIN], 0), 0);

		TEST_NE_P (job->kill_timer, NULL);
		TEST_ALLOC_SIZE (job->kill_timer, sizeof (NihTimer));
		TEST_ALLOC_PARENT (job->kill_timer, job);
		TEST_GE (job->kill_timer->due, time (NULL) + 950);
		TEST_LE (job->kill_timer->due, time (NULL) + 1000);

		TEST_EQ (job->kill_process, PROCESS_MAIN);

		/* Run the kill timer */
		timer = job->kill_timer;
		timer->callback (timer->data, timer);
		nih_free (timer);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_KILLED);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFSIGNALED (status));
		TEST_EQ (WTERMSIG (status), SIGKILL);

		TEST_EQ_P (job->kill_timer, NULL);
		TEST_EQ (job->kill_process, (ProcessType)-1);

		nih_free (job);

		event_poll ();
	}

	nih_free (class);
}


void
test_handler (void)
{
	ConfSource   *source;
	ConfFile     *file;
	JobClass     *class;
	Job          *job = NULL;
	Blocked      *blocked = NULL;
	Event        *event;
	FILE         *output;
	int           exitcodes[2] = { 100, SIGINT << 8 }, status;
	pid_t         pid;
	siginfo_t     info;

	TEST_FUNCTION ("job_process_handler");
	program_name = "test";
	output = tmpfile ();

	source = conf_source_new (NULL, "/tmp", CONF_JOB_DIR);
	file = conf_file_new (source, "/tmp/test");
	file->job = class = job_class_new (NULL, "test");
	class->leader = TRUE;
	class->process[PROCESS_MAIN] = process_new (class);
	class->process[PROCESS_MAIN]->command = "echo";

	class->start_on = event_operator_new (class, EVENT_MATCH,
					       "foo", NULL);
	class->stop_on = event_operator_new (class, EVENT_MATCH,
					      "foo", NULL);
	nih_hash_add (job_classes, &class->entry);

	event = event_new (NULL, "foo", NULL);


	/* Check that the child handler can be called with a pid that doesn't
	 * match the job, and that the job state doesn't change.
	 */
	TEST_FEATURE ("with unknown pid");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_process_handler (NULL, 999, NIH_CHILD_EXITED, 0);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked);
		TEST_EQ_P (blocked->event, event);
		event_unblock (event);

		TEST_EQ_P (job->blocker, NULL);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that we can handle the running task of the job terminating,
	 * which should set the goal to stop and transition a state change
	 * into the stopping state.  This should not be considered a failure.
	 */
	TEST_FEATURE ("with running process");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_process_handler (NULL, 1, NIH_CHILD_EXITED, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked);
		TEST_EQ_P (blocked->event, event);
		event_unblock (event);

		TEST_NE_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocker->blocking);

		blocked = (Blocked *)job->blocker->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job->blocker);
		TEST_EQ (blocked->type, BLOCKED_JOB);
		TEST_EQ_P (blocked->job, job);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocker->blocking);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that we can handle a running task of the job after it's been
	 * sent the TERM signal and a kill timer set.  The kill timer should
	 * be cancelled and freed, and since we killed it, the job should
	 * still not be considered failed.
	 */
	TEST_FEATURE ("with kill timer");
	TEST_ALLOC_FAIL {
		NihTimer *timer = NULL;

		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_KILLED;
		job->pid[PROCESS_MAIN] = 1;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_ALLOC_SAFE {
			timer = (void *) nih_strdup (job, "test");
		}

		TEST_FREE_TAG (timer);
		job->kill_timer = timer;
		job->kill_process = PROCESS_MAIN;

		TEST_FREE_TAG (job);

		job_process_handler (NULL, 1, NIH_CHILD_EXITED, 0);

		TEST_FREE (timer);
		TEST_FREE (job);

		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->failed, FALSE);

		TEST_FREE (blocked);
	}


	/* Check that if the process is restarting, and died when we killed
	 * it, the goal remains as start and a state change is still
	 * transitioned.  This should also not be considered a failure.
	 */
	TEST_FEATURE ("with restarting process");
	TEST_ALLOC_FAIL {
		NihTimer *timer = NULL;

		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_KILLED;
		job->pid[PROCESS_MAIN] = 1;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_ALLOC_SAFE {
			timer = (void *) nih_strdup (job, "test");
		}

		TEST_FREE_TAG (timer);
		job->kill_timer = timer;
		job->kill_process = PROCESS_MAIN;

		job_process_handler (NULL, 1, NIH_CHILD_EXITED, 0);

		TEST_FREE (timer);

		TEST_EQ_P (job->kill_timer, NULL);
		TEST_EQ (job->kill_process, (ProcessType)-1);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STARTING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked);
		TEST_EQ_P (blocked->event, event);
		event_unblock (event);

		TEST_NE_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocker->blocking);

		blocked = (Blocked *)job->blocker->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job->blocker);
		TEST_EQ (blocked->type, BLOCKED_JOB);
		TEST_EQ_P (blocked->job, job);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocker->blocking);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that we can handle the pre-start process of the job exiting,
	 * and if it terminates with a good error code, end up in the running
	 * state.
	 */
	TEST_FEATURE ("with pre-start process");
	class->process[PROCESS_PRE_START] = process_new (class);
	class->process[PROCESS_PRE_START]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->pid[PROCESS_MAIN] = 0;
		job->pid[PROCESS_PRE_START] = 1;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_process_handler (NULL, 1, NIH_CHILD_EXITED, 0);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid[PROCESS_PRE_START], 0);
		TEST_GT (job->pid[PROCESS_MAIN], 0);

		waitpid (job->pid[PROCESS_MAIN], NULL, 0);

		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ_P (job->blocker, NULL);

		TEST_LIST_EMPTY (&job->blocking);
		TEST_FREE (blocked);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	nih_free (class->process[PROCESS_PRE_START]);
	class->process[PROCESS_PRE_START] = NULL;


	/* Check that we can handle a failing pre-start process of the job,
	 * which changes the goal to stop and transitions a state change in
	 * that direction to the stopping state.  An error should be emitted
	 * and the job and event should be marked as failed.
	 */
	TEST_FEATURE ("with failed pre-start process");
	class->process[PROCESS_PRE_START] = process_new (class);
	class->process[PROCESS_PRE_START]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->pid[PROCESS_PRE_START] = 1;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, 1, NIH_CHILD_EXITED, 1);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_PRE_START], 0);

		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->failed, TRUE);

		TEST_LIST_EMPTY (&job->blocking);
		TEST_FREE (blocked);

		TEST_NE_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocker->blocking);

		blocked = (Blocked *)job->blocker->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job->blocker);
		TEST_EQ (blocked->type, BLOCKED_JOB);
		TEST_EQ_P (blocked->job, job);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocker->blocking);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_PRE_START);
		TEST_EQ (job->exit_status, 1);

		TEST_FILE_EQ (output, ("test: test pre-start process (1) "
				       "terminated with status 1\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	nih_free (class->process[PROCESS_PRE_START]);
	class->process[PROCESS_PRE_START] = NULL;


	/* Check that we can handle a killed starting task, which should
	 * act as if it failed.  A different error should be output and
	 * the failed exit status should contain the signal and the high bit.
	 */
	TEST_FEATURE ("with killed pre-start process");
	class->process[PROCESS_PRE_START] = process_new (class);
	class->process[PROCESS_PRE_START]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->pid[PROCESS_PRE_START] = 1;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, 1, NIH_CHILD_KILLED, SIGTERM);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_PRE_START], 0);

		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->failed, TRUE);

		TEST_LIST_EMPTY (&job->blocking);
		TEST_FREE (blocked);

		TEST_NE_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocker->blocking);

		blocked = (Blocked *)job->blocker->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job->blocker);
		TEST_EQ (blocked->type, BLOCKED_JOB);
		TEST_EQ_P (blocked->job, job);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocker->blocking);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_PRE_START);
		TEST_EQ (job->exit_status, SIGTERM << 8);

		TEST_FILE_EQ (output, ("test: test pre-start process (1) "
				       "killed by TERM signal\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	nih_free (class->process[PROCESS_PRE_START]);
	class->process[PROCESS_PRE_START] = NULL;


	/* Check that we can catch the running task of a service stopping
	 * with an error, and if the job is to be respawned, go into
	 * the stopping state but don't change the goal to stop.
	 *
	 * This should also emit a warning, but should not set the failed
	 * state since we're dealing with it.
	 */
	TEST_FEATURE ("with respawn of running service process");
	class->respawn = TRUE;
	class->respawn_limit = 5;
	class->respawn_interval = 10;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, 1, NIH_CHILD_EXITED, 1);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_EQ (job->respawn_count, 1);
		TEST_LE (job->respawn_time, time (NULL));

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked);
		TEST_EQ_P (blocked->event, event);
		event_unblock (event);

		TEST_NE_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocker->blocking);

		blocked = (Blocked *)job->blocker->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job->blocker);
		TEST_EQ (blocked->type, BLOCKED_JOB);
		TEST_EQ_P (blocked->job, job);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocker->blocking);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test main process (1) "
				       "terminated with status 1\n"));
		TEST_FILE_EQ (output, ("test: test main process ended, "
				       "respawning\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	class->respawn = FALSE;


	/* Check that we can catch the running task of a service stopping
	 * with an error, and if the job is to be respawned, go into
	 * the stopping state but don't change the goal to stop.
	 *
	 * This should also emit a warning, but should not set the failed
	 * state since we're dealing with it.
	 */
	TEST_FEATURE ("with respawn of running task process");
	class->task = TRUE;
	class->respawn = TRUE;
	class->respawn_limit = 5;
	class->respawn_interval = 10;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, 1, NIH_CHILD_EXITED, 1);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_EQ (job->respawn_count, 1);
		TEST_LE (job->respawn_time, time (NULL));

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked);
		TEST_EQ_P (blocked->event, event);
		event_unblock (event);

		TEST_NE_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocker->blocking);

		blocked = (Blocked *)job->blocker->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job->blocker);
		TEST_EQ (blocked->type, BLOCKED_JOB);
		TEST_EQ_P (blocked->job, job);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocker->blocking);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test main process (1) "
				       "terminated with status 1\n"));
		TEST_FILE_EQ (output, ("test: test main process ended, "
				       "respawning\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	class->respawn = FALSE;
	class->task = FALSE;


	/* Check that if the process has been respawned too many times
	 * recently, the goal is changed to stop and the process moved into
	 * the stopping state.
	 */
	TEST_FEATURE ("with too many respawns of running process");
	class->respawn = TRUE;
	class->respawn_limit = 5;
	class->respawn_interval = 10;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);

			job->respawn_count = 5;
			job->respawn_time = time (NULL) - 5;
		}

		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, 1, NIH_CHILD_EXITED, 0);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_EQ (job->respawn_count, 6);

		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->failed, TRUE);

		TEST_LIST_EMPTY (&job->blocking);
		TEST_FREE (blocked);

		TEST_NE_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocker->blocking);

		blocked = (Blocked *)job->blocker->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job->blocker);
		TEST_EQ (blocked->type, BLOCKED_JOB);
		TEST_EQ_P (blocked->job, job);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocker->blocking);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test respawning too fast, "
				       "stopped\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	class->respawn = FALSE;


	/* Check that we can catch a running task exiting with a "normal"
	 * exit code, and even if it's marked respawn, set the goal to
	 * stop and transition into the stopping state.
	 */
	TEST_FEATURE ("with normal exit of running respawn process");
	class->respawn = TRUE;
	class->normalexit = exitcodes;
	class->normalexit_len = 1;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, 1, NIH_CHILD_EXITED, 100);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked);
		TEST_EQ_P (blocked->event, event);
		event_unblock (event);

		TEST_NE_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocker->blocking);

		blocked = (Blocked *)job->blocker->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job->blocker);
		TEST_EQ (blocked->type, BLOCKED_JOB);
		TEST_EQ_P (blocked->job, job);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocker->blocking);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test main process (1) "
				       "terminated with status 100\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	class->respawn = FALSE;
	class->normalexit = NULL;
	class->normalexit_len = 0;


	/* Check that a zero exit is not considered normal for a service
	 * by default.
	 */
	TEST_FEATURE ("with respawn of service process and zero exit code");
	class->respawn = TRUE;
	class->respawn_limit = 5;
	class->respawn_interval = 10;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, 1, NIH_CHILD_EXITED, 0);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_EQ (job->respawn_count, 1);
		TEST_LE (job->respawn_time, time (NULL));

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked);
		TEST_EQ_P (blocked->event, event);
		event_unblock (event);

		TEST_NE_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocker->blocking);

		blocked = (Blocked *)job->blocker->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job->blocker);
		TEST_EQ (blocked->type, BLOCKED_JOB);
		TEST_EQ_P (blocked->job, job);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocker->blocking);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test main process ended, "
				       "respawning\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	class->respawn = FALSE;


	/* Check that zero is considered a normal exit code for a task.
	 */
	TEST_FEATURE ("with respawn of task process and zero exit code");
	class->task = TRUE;
	class->respawn = TRUE;
	class->respawn_limit = 5;
	class->respawn_interval = 10;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_process_handler (NULL, 1, NIH_CHILD_EXITED, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked);
		TEST_EQ_P (blocked->event, event);
		event_unblock (event);

		TEST_NE_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocker->blocking);

		blocked = (Blocked *)job->blocker->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job->blocker);
		TEST_EQ (blocked->type, BLOCKED_JOB);
		TEST_EQ_P (blocked->job, job);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocker->blocking);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	class->respawn = FALSE;
	class->task = FALSE;


	/* Check that a running task that fails with an exit status not
	 * listed in normalexit causes the job to be marked as failed.
	 */
	TEST_FEATURE ("with abnormal exit of running process");
	class->normalexit = exitcodes;
	class->normalexit_len = 2;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, 1, NIH_CHILD_EXITED, 99);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->failed, TRUE);

		TEST_LIST_EMPTY (&job->blocking);
		TEST_FREE (blocked);

		TEST_NE_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocker->blocking);

		blocked = (Blocked *)job->blocker->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job->blocker);
		TEST_EQ (blocked->type, BLOCKED_JOB);
		TEST_EQ_P (blocked->job, job);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocker->blocking);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, 99);

		TEST_FILE_EQ (output, ("test: test main process (1) "
				       "terminated with status 99\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	class->normalexit = NULL;
	class->normalexit_len = 0;


	/* Check that a running task that fails doesn't mark the job or
	 * event as failed if the goal was already to stop the job (since
	 * it's probably failed because of the TERM or KILL signal).
	 */
	TEST_FEATURE ("with killed running process");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_KILLED;
		job->pid[PROCESS_MAIN] = 1;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_FREE_TAG (job);

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, 1, NIH_CHILD_KILLED, SIGTERM);
		}
		rewind (output);

		TEST_FREE (job);

		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->failed, FALSE);

		TEST_FREE (blocked);

		TEST_FILE_EQ (output, ("test: test main process (1) "
				       "killed by TERM signal\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}


	/* Check that a running task that fails with an exit status
	 * listed in normalexit does not cause the job to be marked as
	 * failed, but instead just stops it normally.
	 */
	TEST_FEATURE ("with normal exit of running process");
	class->normalexit = exitcodes;
	class->normalexit_len = 2;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, 1, NIH_CHILD_EXITED, 100);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked);
		TEST_EQ_P (blocked->event, event);
		event_unblock (event);

		TEST_NE_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocker->blocking);

		blocked = (Blocked *)job->blocker->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job->blocker);
		TEST_EQ (blocked->type, BLOCKED_JOB);
		TEST_EQ_P (blocked->job, job);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocker->blocking);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test main process (1) "
				       "terminated with status 100\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	class->normalexit = NULL;
	class->normalexit_len = 0;


	/* Check that a running task that fails with an signal
	 * listed in normalexit does not cause the job to be marked as
	 * failed, but instead just stops it normally.
	 */
	TEST_FEATURE ("with normal signal killed running process");
	class->normalexit = exitcodes;
	class->normalexit_len = 2;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, 1, NIH_CHILD_KILLED, SIGINT);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked);
		TEST_EQ_P (blocked->event, event);
		event_unblock (event);

		TEST_NE_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocker->blocking);

		blocked = (Blocked *)job->blocker->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job->blocker);
		TEST_EQ (blocked->type, BLOCKED_JOB);
		TEST_EQ_P (blocked->job, job);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocker->blocking);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test main process (1) "
				       "killed by INT signal\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	class->normalexit = NULL;
	class->normalexit_len = 0;


	/* A running task exiting with the zero exit code is considered
	 * a normal termination if not marked respawn.
	 */
	TEST_FEATURE ("with running task and zero exit");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_process_handler (NULL, 1, NIH_CHILD_EXITED, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked);
		TEST_EQ_P (blocked->event, event);
		event_unblock (event);

		TEST_NE_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocker->blocking);

		blocked = (Blocked *)job->blocker->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job->blocker);
		TEST_EQ (blocked->type, BLOCKED_JOB);
		TEST_EQ_P (blocked->job, job);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocker->blocking);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that we can handle the post-stop process of the job exiting,
	 * and end up in the waiting state.
	 */
	TEST_FEATURE ("with post-stop process");
	class->process[PROCESS_POST_STOP] = process_new (class);
	class->process[PROCESS_POST_STOP]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;
		job->pid[PROCESS_POST_STOP] = 1;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_FREE_TAG (job);

		job_process_handler (NULL, 1, NIH_CHILD_EXITED, 0);

		TEST_FREE (job);

		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->failed, FALSE);

		TEST_FREE (blocked);
	}

	nih_free (class->process[PROCESS_POST_STOP]);
	class->process[PROCESS_POST_STOP] = NULL;


	/* Check that we can handle a failing post-stop process of the job,
	 * which should get marked as failed if the job hasn't been already.
	 */
	TEST_FEATURE ("with failed post-stop process");
	class->process[PROCESS_POST_STOP] = process_new (class);
	class->process[PROCESS_POST_STOP]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;
		job->pid[PROCESS_POST_STOP] = 1;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_FREE_TAG (job);

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, 1, NIH_CHILD_EXITED, 1);
		}
		rewind (output);

		TEST_FREE (job);

		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->failed, TRUE);

		TEST_FREE (blocked);

		TEST_FILE_EQ (output, ("test: test post-stop process (1) "
				       "terminated with status 1\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	nih_free (class->process[PROCESS_POST_STOP]);
	class->process[PROCESS_POST_STOP] = NULL;


	/* Check that a failing stopping task doesn't overwrite the record
	 * of a failing earlier task.
	 */
	TEST_FEATURE ("with stopping task failure after failure");
	class->process[PROCESS_POST_STOP] = process_new (class);
	class->process[PROCESS_POST_STOP]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;
		job->pid[PROCESS_POST_STOP] = 1;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = TRUE;

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = SIGSEGV << 8;

		TEST_FREE_TAG (job);

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, 1, NIH_CHILD_EXITED, 1);
		}
		rewind (output);

		TEST_FREE (job);

		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->failed, TRUE);

		TEST_FREE (blocked);

		TEST_FILE_EQ (output, ("test: test post-stop process (1) "
				       "terminated with status 1\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	nih_free (class->process[PROCESS_POST_STOP]);
	class->process[PROCESS_POST_STOP] = NULL;


	/* Check that we can handle the post-start task of the job exiting,
	 * the exit status should be ignored and the job transitioned into
	 * the running state.  The pid of the job shouldn't be cleared,
	 * but the aux pid should be.
	 */
	TEST_FEATURE ("with post-start process");
	class->process[PROCESS_POST_START] = process_new (class);
	class->process[PROCESS_POST_START]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_POST_START;
		job->pid[PROCESS_MAIN] = 1;
		job->pid[PROCESS_POST_START] = 2;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, 2, NIH_CHILD_EXITED, 1);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);
		TEST_EQ (job->pid[PROCESS_POST_START], 0);

		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ_P (job->blocker, NULL);

		TEST_LIST_EMPTY (&job->blocking);
		TEST_FREE (blocked);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test post-start process (2) "
				       "terminated with status 1\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	nih_free (class->process[PROCESS_POST_START]);
	class->process[PROCESS_POST_START] = NULL;


	/* Check that we can handle the running task of the job exiting, even
	 * if it dies during the post-start state, which should set the goal to
	 * stop and transition a state change into the stopping state.
	 */
	TEST_FEATURE ("with running process in post-start state");
	class->process[PROCESS_POST_START] = process_new (class);
	class->process[PROCESS_POST_START]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_POST_START;
		job->pid[PROCESS_MAIN] = 1;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_process_handler (NULL, 1, NIH_CHILD_EXITED, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked);
		TEST_EQ_P (blocked->event, event);
		event_unblock (event);

		TEST_NE_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocker->blocking);

		blocked = (Blocked *)job->blocker->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job->blocker);
		TEST_EQ (blocked->type, BLOCKED_JOB);
		TEST_EQ_P (blocked->job, job);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocker->blocking);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	nih_free (class->process[PROCESS_POST_START]);
	class->process[PROCESS_POST_START] = NULL;


	/* Check that we can handle the running task of the job exiting while
	 * there is a post-start script running; this should only set the goal
	 * to stop since we also have to wait for the post-start script to
	 * stop.
	 */
	TEST_FEATURE ("with running process while post-start running");
	class->process[PROCESS_POST_START] = process_new (class);
	class->process[PROCESS_POST_START]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_POST_START;
		job->pid[PROCESS_MAIN] = 1;
		job->pid[PROCESS_POST_START] = 2;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_process_handler (NULL, 1, NIH_CHILD_EXITED, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_POST_START);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);
		TEST_EQ (job->pid[PROCESS_POST_START], 2);

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked);
		TEST_EQ_P (blocked->event, event);
		event_unblock (event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	nih_free (class->process[PROCESS_POST_START]);
	class->process[PROCESS_POST_START] = NULL;


	/* Check that we can handle the running process exiting before the
	 * post-start process finishes.  This should mark the job to be
	 * stopped, but not change the state, handling the post-start process
	 * exiting afterwards should change the state.
	 */
	TEST_FEATURE ("with running then post-start process");
	class->process[PROCESS_POST_START] = process_new (class);
	class->process[PROCESS_POST_START]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_POST_START;
		job->pid[PROCESS_MAIN] = 1;
		job->pid[PROCESS_POST_START] = 2;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_process_handler (NULL, 1, NIH_CHILD_EXITED, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_POST_START);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);
		TEST_EQ (job->pid[PROCESS_POST_START], 2);

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked);
		TEST_EQ_P (blocked->event, event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		job_process_handler (NULL, 2, NIH_CHILD_EXITED, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);
		TEST_EQ (job->pid[PROCESS_POST_START], 0);

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked);
		TEST_EQ_P (blocked->event, event);
		event_unblock (event);

		TEST_NE_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocker->blocking);

		blocked = (Blocked *)job->blocker->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job->blocker);
		TEST_EQ (blocked->type, BLOCKED_JOB);
		TEST_EQ_P (blocked->job, job);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocker->blocking);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	nih_free (class->process[PROCESS_POST_START]);
	class->process[PROCESS_POST_START] = NULL;


	/* Check that we can handle a failed running process before the
	 * post-start process finishes.  This should mark the job to be
	 * stopped, but not change the state, then handling the post-start
	 * process exiting afterwards should change the state.
	 */
	TEST_FEATURE ("with failed running then post-start process");
	class->process[PROCESS_POST_START] = process_new (class);
	class->process[PROCESS_POST_START]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_POST_START;
		job->pid[PROCESS_MAIN] = 1;
		job->pid[PROCESS_POST_START] = 2;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, 1, NIH_CHILD_KILLED, SIGSEGV);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_POST_START);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);
		TEST_EQ (job->pid[PROCESS_POST_START], 2);

		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->failed, TRUE);

		TEST_EQ_P (job->blocker, NULL);

		TEST_LIST_EMPTY (&job->blocking);
		TEST_FREE (blocked);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, SIGSEGV << 8);

		TEST_FILE_EQ (output, ("test: test main process (1) "
				       "killed by SEGV signal\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		job_process_handler (NULL, 2, NIH_CHILD_EXITED, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);
		TEST_EQ (job->pid[PROCESS_POST_START], 0);

		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->failed, TRUE);

		TEST_LIST_EMPTY (&job->blocking);
		TEST_FREE (blocked);

		TEST_NE_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocker->blocking);

		blocked = (Blocked *)job->blocker->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job->blocker);
		TEST_EQ (blocked->type, BLOCKED_JOB);
		TEST_EQ_P (blocked->job, job);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocker->blocking);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, SIGSEGV << 8);

		nih_free (job);
	}

	nih_free (class->process[PROCESS_POST_START]);
	class->process[PROCESS_POST_START] = NULL;


	/* Check that we can handle the running process of a respawn job
	 * exiting before the post-start process finishes.  This should
	 * mark the job to be respawned when the post-start script finishes
	 * instead of making any state change.
	 */
	TEST_FEATURE ("with respawn of running while post-start process");
	class->respawn = TRUE;
	class->respawn_limit = 5;
	class->respawn_interval = 10;

	class->process[PROCESS_POST_START] = process_new (class);
	class->process[PROCESS_POST_START]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_POST_START;
		job->pid[PROCESS_MAIN] = 1;
		job->pid[PROCESS_POST_START] = 2;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, 1, NIH_CHILD_EXITED, 0);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_RESPAWN);
		TEST_EQ (job->state, JOB_POST_START);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);
		TEST_EQ (job->pid[PROCESS_POST_START], 2);

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked);
		TEST_EQ_P (blocked->event, event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		job_process_handler (NULL, 2, NIH_CHILD_EXITED, 0);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);
		TEST_EQ (job->pid[PROCESS_POST_START], 0);

		TEST_EQ (job->respawn_count, 1);
		TEST_LE (job->respawn_time, time (NULL));

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked);
		TEST_EQ_P (blocked->event, event);
		event_unblock (event);

		TEST_NE_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocker->blocking);

		blocked = (Blocked *)job->blocker->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job->blocker);
		TEST_EQ (blocked->type, BLOCKED_JOB);
		TEST_EQ_P (blocked->job, job);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocker->blocking);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test main process ended, "
				       "respawning\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	nih_free (class->process[PROCESS_POST_START]);
	class->process[PROCESS_POST_START] = NULL;

	class->respawn = FALSE;


	/* Check that we can handle the pre-stop task of the job exiting, the
	 * exit status should be ignored and the job transitioned into
	 * the stopping state.  The pid of the job shouldn't be cleared,
	 * but the aux pid should be.
	 */
	TEST_FEATURE ("with pre-stop process");
	class->process[PROCESS_PRE_STOP] = process_new (class);
	class->process[PROCESS_PRE_STOP]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_PRE_STOP;
		job->pid[PROCESS_MAIN] = 1;
		job->pid[PROCESS_PRE_STOP] = 2;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, 2, NIH_CHILD_EXITED, 1);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);
		TEST_EQ (job->pid[PROCESS_PRE_STOP], 0);

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked);
		TEST_EQ_P (blocked->event, event);
		event_unblock (event);

		TEST_NE_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocker->blocking);

		blocked = (Blocked *)job->blocker->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job->blocker);
		TEST_EQ (blocked->type, BLOCKED_JOB);
		TEST_EQ_P (blocked->job, job);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocker->blocking);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test pre-stop process (2) "
				       "terminated with status 1\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	nih_free (class->process[PROCESS_PRE_STOP]);
	class->process[PROCESS_PRE_STOP] = NULL;


	/* Check that we can handle the running task of the job exiting, even
	 * if it dies during the pre-stop state, which transition a state
	 * change into the stopping state.
	 */
	TEST_FEATURE ("with running process in pre-stop state");
	class->process[PROCESS_PRE_STOP] = process_new (class);
	class->process[PROCESS_PRE_STOP]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_PRE_STOP;
		job->pid[PROCESS_MAIN] = 1;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_process_handler (NULL, 1, NIH_CHILD_EXITED, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked);
		TEST_EQ_P (blocked->event, event);
		event_unblock (event);

		TEST_NE_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocker->blocking);

		blocked = (Blocked *)job->blocker->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job->blocker);
		TEST_EQ (blocked->type, BLOCKED_JOB);
		TEST_EQ_P (blocked->job, job);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocker->blocking);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	nih_free (class->process[PROCESS_PRE_STOP]);
	class->process[PROCESS_PRE_STOP] = NULL;


	/* Check that we can handle the running task of the job exiting while
	 * there is a pre-stop script running; this should have no other effect
	 * since we also have to wait for the pre-stop script to stop.
	 */
	TEST_FEATURE ("with running process while pre-stop running");
	class->process[PROCESS_PRE_STOP] = process_new (class);
	class->process[PROCESS_PRE_STOP]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_PRE_STOP;
		job->pid[PROCESS_MAIN] = 1;
		job->pid[PROCESS_PRE_STOP] = 2;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_process_handler (NULL, 1, NIH_CHILD_EXITED, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_PRE_STOP);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);
		TEST_EQ (job->pid[PROCESS_PRE_STOP], 2);

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked);
		TEST_EQ_P (blocked->event, event);
		event_unblock (event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	nih_free (class->process[PROCESS_PRE_STOP]);
	class->process[PROCESS_PRE_STOP] = NULL;


	/* Check that we can handle the running process of a respawn job
	 * exiting before the pre-stop process finishes.  This should
	 * mark the job to be respawned when the pre-stop script finishes
	 * instead of making any state change.
	 */
	TEST_FEATURE ("with respawn of running while pre-stop process");
	class->respawn = TRUE;
	class->respawn_limit = 5;
	class->respawn_interval = 10;

	class->process[PROCESS_PRE_STOP] = process_new (class);
	class->process[PROCESS_PRE_STOP]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_PRE_STOP;
		job->pid[PROCESS_MAIN] = 1;
		job->pid[PROCESS_PRE_STOP] = 2;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, 1, NIH_CHILD_EXITED, 0);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_RESPAWN);
		TEST_EQ (job->state, JOB_PRE_STOP);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);
		TEST_EQ (job->pid[PROCESS_PRE_STOP], 2);

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked);
		TEST_EQ_P (blocked->event, event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		job_process_handler (NULL, 2, NIH_CHILD_EXITED, 0);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);
		TEST_EQ (job->pid[PROCESS_PRE_STOP], 0);

		TEST_EQ (job->respawn_count, 1);
		TEST_LE (job->respawn_time, time (NULL));

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked);
		TEST_EQ_P (blocked->event, event);
		event_unblock (event);

		TEST_NE_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocker->blocking);

		blocked = (Blocked *)job->blocker->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job->blocker);
		TEST_EQ (blocked->type, BLOCKED_JOB);
		TEST_EQ_P (blocked->job, job);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocker->blocking);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test main process ended, "
				       "respawning\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	nih_free (class->process[PROCESS_PRE_STOP]);
	class->process[PROCESS_PRE_STOP] = NULL;

	class->respawn = FALSE;


#if HAVE_VALGRIND_VALGRIND_H
	/* These tests fail when running under valgrind.
	 */
	if (! RUNNING_ON_VALGRIND) {
#endif
	/* Check that we ignore a process stopping on a signal if it isn't
	 * the main process of the job.
	 */
	TEST_FEATURE ("with stopped non-main process");
	class->expect = EXPECT_STOP;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		TEST_CHILD (pid) {
			raise (SIGSTOP);
			exit (0);
		}

		assert0 (waitid (P_PID, pid, &info, WSTOPPED | WNOWAIT));

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = 1;
		job->pid[PROCESS_POST_START] = pid;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, pid,
					   NIH_CHILD_STOPPED, SIGSTOP);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_SPAWNED);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);
		TEST_EQ (job->pid[PROCESS_POST_START], pid);

		TEST_EQ (kill (pid, SIGCONT), 0);

		waitpid (job->pid[PROCESS_POST_START], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked);
		TEST_EQ_P (blocked->event, event);
		event_unblock (event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	class->expect = EXPECT_NONE;


	/* Check that we ignore the main process stopping on a signal if the
	 * job isn't in the spawned state.
	 */
	TEST_FEATURE ("with stopped main process outside of spawned");
	class->expect = EXPECT_STOP;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		TEST_CHILD (pid) {
			raise (SIGSTOP);
			exit (0);
		}

		assert0 (waitid (P_PID, pid, &info, WSTOPPED | WNOWAIT));

		job->goal = JOB_START;
		job->state = JOB_POST_START;
		job->pid[PROCESS_MAIN] = pid;
		job->pid[PROCESS_POST_START] = 1;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, pid,
					   NIH_CHILD_STOPPED, SIGSTOP);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_POST_START);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);
		TEST_EQ (job->pid[PROCESS_POST_START], 1);

		TEST_EQ (kill (pid, SIGCONT), 0);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked);
		TEST_EQ_P (blocked->event, event);
		event_unblock (event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	class->expect = EXPECT_NONE;


	/* Check that we ignore the main process stopping on a signal in
	 * the spawned state if we're not waiting for it to do so.
	 */
	TEST_FEATURE ("with stopped main process for non-wait job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		TEST_CHILD (pid) {
			raise (SIGSTOP);
			exit (0);
		}

		assert0 (waitid (P_PID, pid, &info, WSTOPPED | WNOWAIT));

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = pid;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, pid,
					   NIH_CHILD_STOPPED, SIGSTOP);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_SPAWNED);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);

		TEST_EQ (kill (pid, SIGCONT), 0);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked);
		TEST_EQ_P (blocked->event, event);
		event_unblock (event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that we ignore the main process stopping on the wrong
	 * signal.
	 */
	TEST_FEATURE ("with stopped main process but wrong signal");
	class->expect = EXPECT_STOP;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		TEST_CHILD (pid) {
			raise (SIGTSTP);
			exit (0);
		}

		assert0 (waitid (P_PID, pid, &info, WSTOPPED | WNOWAIT));

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = pid;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, pid,
					   NIH_CHILD_STOPPED, SIGTSTP);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_SPAWNED);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);

		TEST_EQ (kill (pid, SIGCONT), 0);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ_P (job->blocker, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked);
		TEST_EQ_P (blocked->event, event);
		event_unblock (event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	class->expect = EXPECT_NONE;


	/* Check that if we're waiting in spawned for the main process to
	 * stop, and it does so, the process is continued and the job state
	 * changed to running.
	 */
	TEST_FEATURE ("with stopped main process waiting in spawned");
	class->expect = EXPECT_STOP;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");

			blocked = blocked_new (job, BLOCKED_EVENT, event);
			event_block (event);
			nih_list_add (&job->blocking, &blocked->entry);
		}

		TEST_CHILD (pid) {
			raise (SIGSTOP);
			exit (0);
		}

		assert0 (waitid (P_PID, pid, &info, WSTOPPED | WNOWAIT));

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = pid;

		TEST_FREE_TAG (blocked);

		job->blocker = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, pid,
					   NIH_CHILD_STOPPED, SIGSTOP);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ_P (job->blocker, NULL);

		TEST_LIST_EMPTY (&job->blocking);
		TEST_FREE (blocked);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, (ProcessType)-1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	class->expect = EXPECT_NONE;


	/* Check that a traced process has a signal delivered to it
	 * unchanged.
	 */
	TEST_FEATURE ("with signal delivered to traced process");
	class->expect = EXPECT_DAEMON;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");
			job->trace_state = TRACE_NORMAL;
		}

		TEST_CHILD (pid) {
			assert0 (ptrace (PTRACE_TRACEME, 0, NULL, 0));
			signal (SIGTERM, SIG_IGN);
			raise (SIGTERM);
			exit (0);
		}

		assert0 (waitid (P_PID, pid, &info, WSTOPPED | WNOWAIT));

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = pid;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, pid,
					   NIH_CHILD_TRAPPED, SIGTERM);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_SPAWNED);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);

		TEST_EQ (job->trace_forks, 0);
		TEST_EQ (job->trace_state, TRACE_NORMAL);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		nih_free (job);
	}

	class->expect = EXPECT_NONE;


	/* Check that a new traced process which receives SIGTRAP doesn't
	 * have it delivered, and instead has its options set.
	 */
	TEST_FEATURE ("with trapped new traced process");
	class->expect = EXPECT_DAEMON;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");
			job->trace_state = TRACE_NEW;
		}

		TEST_CHILD (pid) {
			assert0 (ptrace (PTRACE_TRACEME, 0, NULL, 0));
			raise (SIGTRAP);
			exit (0);
		}

		assert0 (waitid (P_PID, pid, &info, WSTOPPED | WNOWAIT));

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = pid;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, pid,
					   NIH_CHILD_TRAPPED, SIGTRAP);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_SPAWNED);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);

		TEST_EQ (job->trace_forks, 0);
		TEST_EQ (job->trace_state, TRACE_NORMAL);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		nih_free (job);
	}

	class->expect = EXPECT_NONE;


	/* Check that a new traced process child which receives SIGSTOP
	 * doesn't have it delivered, and instead has its fork count
	 * incremented and its options set.
	 */
	TEST_FEATURE ("with trapped new traced process");
	class->expect = EXPECT_DAEMON;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");
			job->trace_state = TRACE_NEW_CHILD;
		}

		TEST_CHILD (pid) {
			assert0 (ptrace (PTRACE_TRACEME, 0, NULL, 0));
			raise (SIGSTOP);
			exit (0);
		}

		assert0 (waitid (P_PID, pid, &info, WSTOPPED | WNOWAIT));

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = pid;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, pid,
					   NIH_CHILD_TRAPPED, SIGSTOP);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_SPAWNED);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);

		TEST_EQ (job->trace_forks, 1);
		TEST_EQ (job->trace_state, TRACE_NORMAL);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		nih_free (job);
	}

	class->expect = EXPECT_NONE;


	/* Check that the second child of a daemon process is detached
	 * and ends the trace, moving the job into the running state.
	 */
	TEST_FEATURE ("with second child of daemon process");
	class->expect = EXPECT_DAEMON;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");
			job->trace_forks = 1;
			job->trace_state = TRACE_NEW_CHILD;
		}

		TEST_CHILD (pid) {
			assert0 (ptrace (PTRACE_TRACEME, 0, NULL, 0));
			raise (SIGSTOP);
			pause ();
			exit (0);
		}

		assert0 (waitid (P_PID, pid, &info, WSTOPPED | WNOWAIT));

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = pid;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, pid,
					   NIH_CHILD_TRAPPED, SIGSTOP);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);

		TEST_EQ (job->trace_forks, 2);
		TEST_EQ (job->trace_state, TRACE_NONE);

		kill (job->pid[PROCESS_MAIN], SIGTERM);
		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFSIGNALED (status));
		TEST_EQ (WTERMSIG (status), SIGTERM);

		nih_free (job);
	}

	class->expect = EXPECT_NONE;


	/* Check that the first child of a forking process is detached
	 * and ends the trace, moving the job into the running state.
	 */
	TEST_FEATURE ("with first child of forking process");
	class->expect = EXPECT_FORK;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");
			job->trace_forks = 0;
			job->trace_state = TRACE_NEW_CHILD;
		}

		TEST_CHILD (pid) {
			assert0 (ptrace (PTRACE_TRACEME, 0, NULL, 0));
			raise (SIGSTOP);
			pause ();
			exit (0);
		}

		assert0 (waitid (P_PID, pid, &info, WSTOPPED | WNOWAIT));

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = pid;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, pid,
					   NIH_CHILD_TRAPPED, SIGSTOP);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);

		TEST_EQ (job->trace_forks, 1);
		TEST_EQ (job->trace_state, TRACE_NONE);

		kill (job->pid[PROCESS_MAIN], SIGTERM);
		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFSIGNALED (status));
		TEST_EQ (WTERMSIG (status), SIGTERM);

		nih_free (job);
	}

	class->expect = EXPECT_NONE;


	/* Check that when a process forks, the trace state is set to expect
	 * a new child, the job is updated to the new child and the old
	 * parent is detached.
	 */
	TEST_FEATURE ("with forked process");
	class->expect = EXPECT_DAEMON;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");
			job->trace_state = TRACE_NORMAL;
		}

		TEST_CHILD (pid) {
			assert0 (ptrace (PTRACE_TRACEME, 0, NULL, 0));
			raise (SIGSTOP);
			fork ();
			exit (0);
		}

		assert0 (waitid (P_PID, pid, &info, WSTOPPED | WNOWAIT));
		assert0 (ptrace (PTRACE_SETOPTIONS, pid, NULL,
				 PTRACE_O_TRACEFORK | PTRACE_O_TRACEEXEC));
		assert0 (ptrace (PTRACE_CONT, pid, NULL, 0));

		assert0 (waitid (P_PID, pid, &info, WSTOPPED | WNOWAIT));

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = pid;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, pid, NIH_CHILD_PTRACE,
					   PTRACE_EVENT_FORK);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_SPAWNED);
		TEST_NE (job->pid[PROCESS_MAIN], pid);

		TEST_EQ (job->trace_forks, 0);
		TEST_EQ (job->trace_state, TRACE_NEW_CHILD);

		waitpid (pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		assert0 (waitid (P_PID, job->pid[PROCESS_MAIN],
				 &info, WSTOPPED | WNOWAIT));
		TEST_EQ (info.si_pid, job->pid[PROCESS_MAIN]);
		TEST_EQ (info.si_code, CLD_TRAPPED);
		TEST_EQ (info.si_status, SIGSTOP);

		assert0 (ptrace (PTRACE_DETACH, job->pid[PROCESS_MAIN],
				 NULL, 0));

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		nih_free (job);
	}

	class->expect = EXPECT_NONE;


	/* Check that should the process call exec() it ends the tracing
	 * even if we haven't had enough forks yet and moves the job into
	 * the running state.
	 */
	TEST_FEATURE ("with exec call by process");
	class->expect = EXPECT_DAEMON;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "");
			job->trace_forks = 1;
			job->trace_state = TRACE_NORMAL;
		}

		TEST_CHILD (pid) {
			assert0 (ptrace (PTRACE_TRACEME, 0, NULL, 0));
			raise (SIGSTOP);
			execl ("/bin/true", "true", NULL);
			exit (15);
		}

		assert0 (waitid (P_PID, pid, &info, WSTOPPED | WNOWAIT));
		assert0 (ptrace (PTRACE_SETOPTIONS, pid, NULL,
				 PTRACE_O_TRACEFORK | PTRACE_O_TRACEEXEC));
		assert0 (ptrace (PTRACE_CONT, pid, NULL, 0));

		assert0 (waitid (P_PID, pid, &info, WSTOPPED | WNOWAIT));

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = pid;

		TEST_DIVERT_STDERR (output) {
			job_process_handler (NULL, pid, NIH_CHILD_PTRACE,
					   PTRACE_EVENT_EXEC);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);

		TEST_EQ (job->trace_forks, 1);
		TEST_EQ (job->trace_state, TRACE_NONE);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		nih_free (job);
	}

	class->expect = EXPECT_NONE;
#if HAVE_VALGRIND_VALGRIND_H
	}
#endif


	fclose (output);

	nih_free (class);
	file->job = NULL;
	nih_free (source);

	nih_free (event);
	event_poll ();
}


void
test_find (void)
{
	JobClass    *class1, *class2, *class3;
	Job         *job1, *job2, *job3, *job4, *job5, *ptr;
	ProcessType  process;

	TEST_FUNCTION ("job_process_find");
	class1 = job_class_new (NULL, "foo");
	class1->process[PROCESS_MAIN] = process_new (class1);
	class1->process[PROCESS_POST_START] = process_new (class1);
	class1->instance = "$FOO";
	nih_hash_add (job_classes, &class1->entry);

	class2 = job_class_new (NULL, "bar");
	class2->process[PROCESS_PRE_START] = process_new (class2);
	class2->process[PROCESS_MAIN] = process_new (class2);
	class2->process[PROCESS_PRE_STOP] = process_new (class2);
	class2->instance = "$FOO";
	nih_hash_add (job_classes, &class2->entry);

	class3 = job_class_new (NULL, "baz");
	class3->process[PROCESS_POST_STOP] = process_new (class3);
	nih_hash_add (job_classes, &class3->entry);

	job1 = job_new (class1, "foo");
	job1->pid[PROCESS_MAIN] = 10;
	job1->pid[PROCESS_POST_START] = 15;

	job2 = job_new (class1, "bar");

	job3 = job_new (class2, "foo");
	job3->pid[PROCESS_PRE_START] = 20;

	job4 = job_new (class2, "bar");
	job4->pid[PROCESS_MAIN] = 25;
	job4->pid[PROCESS_PRE_STOP] = 30;

	job5 = job_new (class3, "");
	job5->pid[PROCESS_POST_STOP] = 35;


	/* Check that we can find a job that exists by the pid of its
	 * primary process.
	 */
	TEST_FEATURE ("with pid we expect to find");
	ptr = job_process_find (10, &process);

	TEST_EQ_P (ptr, job1);
	TEST_EQ (process, PROCESS_MAIN);


	/* Check that we can find a job that exists by the pid of its
	 * pre-start process.
	 */
	TEST_FEATURE ("with pre-start pid we expect to find");
	ptr = job_process_find (20, &process);

	TEST_EQ_P (ptr, job3);
	TEST_EQ (process, PROCESS_PRE_START);


	/* Check that we can find a job that exists by the pid of its
	 * post-start process.
	 */
	TEST_FEATURE ("with post-start pid we expect to find");
	ptr = job_process_find (15, &process);

	TEST_EQ_P (ptr, job1);
	TEST_EQ (process, PROCESS_POST_START);


	/* Check that we can find a job that exists by the pid of its
	 * pre-stop process.
	 */
	TEST_FEATURE ("with pre-stop pid we expect to find");
	ptr = job_process_find (30, &process);

	TEST_EQ_P (ptr, job4);
	TEST_EQ (process, PROCESS_PRE_STOP);


	/* Check that we can find a job that exists by the pid of its
	 * pre-stop process.
	 */
	TEST_FEATURE ("with post-stop pid we expect to find");
	ptr = job_process_find (35, &process);

	TEST_EQ_P (ptr, job5);
	TEST_EQ (process, PROCESS_POST_STOP);


	/* Check that we get NULL if no job has a process with that pid. */
	TEST_FEATURE ("with pid we do not expect to find");
	ptr = job_process_find (100, NULL);

	TEST_EQ_P (ptr, NULL);


	/* Check that we get NULL if there are jobs in the hash, but none
	 * have pids.
	 */
	TEST_FEATURE ("with no pids in job table");
	nih_free (job5);
	nih_free (job4);
	nih_free (job3);
	nih_free (job1);
	ptr = job_process_find (20, NULL);

	TEST_EQ_P (ptr, NULL);


	/* Check that we get NULL if there are no instances running. */
	TEST_FEATURE ("with no instances");
	nih_free (job2);
	ptr = job_process_find (20, NULL);

	TEST_EQ_P (ptr, NULL);


	/* Check that we get NULL if there are no jobs in the hash. */
	TEST_FEATURE ("with empty job table");
	nih_free (class1);
	nih_free (class2);
	nih_free (class3);
	ptr = job_process_find (20, NULL);

	TEST_EQ_P (ptr, NULL);
}


int
main (int   argc,
      char *argv[])
{
	/* We re-exec this binary to test various children features.  To
	 * do that, we need to know the full path to the program.
	 */
	argv0 = argv[0];
	if (argv0[0] != '/') {
		char path[PATH_MAX];

		assert (getcwd (path, sizeof (path)));
		strcat (path, "/");
		strcat (path, argv0);

		argv0 = path;
	}

	/* If two arguments are given, the first is the child enum and the
	 * second is a filename to write the result to.
	 */
	if (argc == 3) {
		child (atoi (argv[1]), argv[2]);
		exit (1);
	}

	/* Otherwise run the tests as normal */
	test_run ();
	test_spawn ();
	test_kill ();
	test_handler ();

	test_find ();

	return 0;
}
