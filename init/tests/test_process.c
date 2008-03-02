/* upstart
 *
 * test_process.c - test suite for init/process.c
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

#include <nih/test.h>

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
#include <nih/error.h>

#include "job.h"
#include "event.h"
#include "process.h"
#include "errors.h"


/* Sadly we can't test everything that process_spawn() does simply because
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
	char  path[PATH_MAX];
	int   i;

	out = fopen (filename, "w");

	switch (test) {
	case TEST_SIMPLE:
		exit (0);
	case TEST_PIDS:
		fprintf (out, "pid: %d\n", getpid ());
		fprintf (out, "ppid: %d\n", getppid ());
		fprintf (out, "pgrp: %d\n", getpgrp ());
		fprintf (out, "sid: %d\n", getsid (0));
		exit (0);
	case TEST_CONSOLE:
		for (i = 0; i < 3; i++) {
			struct stat buf;

			fstat (i, &buf);
			fprintf (out, "%d: %d %d\n", i,
				 major (buf.st_rdev),
				 minor (buf.st_rdev));
		}
		exit (0);
	case TEST_PWD:
		getcwd (path, sizeof (path));
		fprintf (out, "wd: %s\n", path);
		exit (0);
	case TEST_ENVIRONMENT:
		for (char **env = environ; *env; env++)
			fprintf (out, "%s\n", *env);
		exit (0);
	}
}


void
test_spawn (void)
{
	FILE          *output;
	char           function[PATH_MAX], filename[PATH_MAX], buf[80];
	char          *env[3], *args[4];
	JobConfig     *config;
	pid_t          pid;
	siginfo_t      info;
	NihError      *err;
	ProcessError  *perr;

	TEST_FUNCTION ("process_spawn");
	TEST_FILENAME (filename);

	args[0] = argv0;
	args[1] = function;
	args[2] = filename;
	args[3] = NULL;

	/* Check that we can spawn a simple job; we wait for the child
	 * process and then read from the file written to check that the
	 * process tree is what we expect it to look like.
	 */
	TEST_FEATURE ("with simple job");
	sprintf (function, "%d", TEST_PIDS);

	config = job_config_new (NULL, "test");

	pid = process_spawn (config, args, NULL, FALSE);
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

	nih_free (config);


	/* Check that a job spawned with no console has the file descriptors
	 * bound to the /dev/null device.
	 */
	TEST_FEATURE ("with no console");
	sprintf (function, "%d", TEST_CONSOLE);

	config = job_config_new (NULL, "test");
	config->console = CONSOLE_NONE;

	pid = process_spawn (config, args, NULL, FALSE);
	TEST_GT (pid, 0);

	waitpid (pid, NULL, 0);
	output = fopen (filename, "r");

	TEST_FILE_EQ (output, "0: 1 3\n");
	TEST_FILE_EQ (output, "1: 1 3\n");
	TEST_FILE_EQ (output, "2: 1 3\n");
	TEST_FILE_END (output);

	fclose (output);
	unlink (filename);

	nih_free (config);


	/* Check that a job with an alternate working directory is run from
	 * that directory.
	 */
	TEST_FEATURE ("with working directory");
	sprintf (function, "%d", TEST_PWD);

	config = job_config_new (NULL, "test");
	config->chdir = "/tmp";

	pid = process_spawn (config, args, NULL, FALSE);
	TEST_GT (pid, 0);

	waitpid (pid, NULL, 0);
	output = fopen (filename, "r");

	TEST_FILE_EQ (output, "wd: /tmp\n");
	TEST_FILE_END (output);

	fclose (output);
	unlink (filename);

	nih_free (config);


	/* Check that a job is run with only the environment variables
	 * specifiec in the function call.
	 */
	TEST_FEATURE ("with environment");
	sprintf (function, "%d", TEST_ENVIRONMENT);
	putenv ("BAR=baz");

	env[0] = "PATH=/bin";
	env[1] = "FOO=bar";
	env[2] = NULL;

	config = job_config_new (NULL, "test");

	pid = process_spawn (config, args, env, FALSE);
	TEST_GT (pid, 0);

	waitpid (pid, NULL, 0);
	output = fopen (filename, "r");

	TEST_FILE_EQ (output, "PATH=/bin\n");
	TEST_FILE_EQ (output, "FOO=bar\n");
	TEST_FILE_END (output);

	fclose (output);
	unlink (filename);

	nih_free (config);


	/* Check that when we spawn an ordinary job, it isn't usually ptraced
	 * since that's a special honour reserved for daemons that we expect
	 * to fork.
	 */
	TEST_FEATURE ("with non-daemon job");
	sprintf (function, "%d", TEST_SIMPLE);

	config = job_config_new (NULL, "test");

	pid = process_spawn (config, args, NULL, FALSE);
	TEST_GT (pid, 0);

	assert0 (waitid (P_PID, pid, &info, WEXITED | WSTOPPED | WCONTINUED));
	TEST_EQ (info.si_code, CLD_EXITED);
	TEST_EQ (info.si_status, 0);

	unlink (filename);

	nih_free (config);


	/* Check that when we spawn a daemon job, we can request that the
	 * parent be traced.
	 */
	TEST_FEATURE ("with daemon job");
	sprintf (function, "%d", TEST_SIMPLE);

	config = job_config_new (NULL, "test");
	config->wait_for = JOB_WAIT_DAEMON;

	pid = process_spawn (config, args, NULL, TRUE);
	TEST_GT (pid, 0);

	assert0 (waitid (P_PID, pid, &info, WEXITED | WSTOPPED | WCONTINUED));
	TEST_EQ (info.si_code, CLD_TRAPPED);
	TEST_EQ (info.si_status, SIGTRAP);

	assert0 (ptrace (PTRACE_DETACH, pid, NULL, 0));

	assert0 (waitid (P_PID, pid, &info, WEXITED | WSTOPPED | WCONTINUED));
	TEST_EQ (info.si_code, CLD_EXITED);
	TEST_EQ (info.si_status, 0);

	unlink (filename);

	nih_free (config);


	/* Check that attempting to spawn a binary that doesn't exist returns
	 * an error immediately with all of the expected information in the
	 * error structure.
	 */
	TEST_FEATURE ("with no such file");
	args[0] = filename;
	args[1] = filename;
	args[2] = NULL;

	config = job_config_new (NULL, "test");

	pid = process_spawn (config, args, NULL, FALSE);
	TEST_LT (pid, 0);

	err = nih_error_get ();
	TEST_EQ (err->number, PROCESS_ERROR);
	TEST_ALLOC_SIZE (err, sizeof (ProcessError));

	perr = (ProcessError *)err;
	TEST_EQ (perr->type, PROCESS_ERROR_EXEC);
	TEST_EQ (perr->arg, 0);
	TEST_EQ (perr->errnum, ENOENT);
	nih_free (perr);

	nih_free (config);
}


void
test_kill (void)
{
	JobConfig *config;
	pid_t      pid1, pid2;
	int        ret, status;

	TEST_FUNCTION ("process_kill");
	config = job_config_new (NULL, "test");

	/* Check that when we normally kill the process, the TERM signal
	 * is sent to all processes in its process group.
	 */
	TEST_FEATURE ("with TERM signal");
	TEST_CHILD (pid1) {
		pause ();
	}
	TEST_CHILD (pid2) {
		pause ();
	}

	setpgid (pid1, pid1);
	setpgid (pid2, pid1);

	ret = process_kill (config, pid1, FALSE);
	waitpid (pid1, &status, 0);

	TEST_EQ (ret, 0);
	TEST_TRUE (WIFSIGNALED (status));
	TEST_EQ (WTERMSIG (status), SIGTERM);

	waitpid (pid2, &status, 0);

	TEST_EQ (ret, 0);
	TEST_TRUE (WIFSIGNALED (status));
	TEST_EQ (WTERMSIG (status), SIGTERM);


	/* Check that when we force the kill, the KILL signal is sent
	 * instead.
	 */
	TEST_FEATURE ("with KILL signal");
	TEST_CHILD (pid1) {
		pause ();
	}
	TEST_CHILD (pid2) {
		pause ();
	}

	setpgid (pid1, pid1);
	setpgid (pid2, pid1);

	ret = process_kill (config, pid1, TRUE);
	waitpid (pid1, &status, 0);

	TEST_EQ (ret, 0);
	TEST_TRUE (WIFSIGNALED (status));
	TEST_EQ (WTERMSIG (status), SIGKILL);

	ret = process_kill (config, pid1, TRUE);
	waitpid (pid2, &status, 0);

	TEST_EQ (ret, 0);
	TEST_TRUE (WIFSIGNALED (status));
	TEST_EQ (WTERMSIG (status), SIGKILL);


	nih_free (config);
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

		getcwd (path, sizeof (path));
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
	test_spawn ();
	test_kill ();

	return 0;
}
