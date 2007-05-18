/* upstart
 *
 * test_process.c - test suite for init/process.c
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

#include "job.h"
#include "event.h"
#include "process.h"


/* Sadly we can't test everything that process_spawn() does simply because
 * a lot of it can only be done by root, or in the case of the console stuff,
 * kills whatever had /dev/console (usually X).
 *
 * This set of tests at least ensures some level of code coverage.
 */
enum child_tests {
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
	FILE  *output;
	char   function[PATH_MAX], filename[PATH_MAX], buf[80];
	char  *env[2], *args[4];
	Job   *job;
	Event *event;
	pid_t  pid;

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

	job = job_new (NULL, "test");
	pid = process_spawn (job, args);

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

	nih_list_free (&job->entry);


	/* Check that a job spawned with no console has the file descriptors
	 * bound to the /dev/null device.
	 */
	TEST_FEATURE ("with no console");
	sprintf (function, "%d", TEST_CONSOLE);

	job = job_new (NULL, "test");
	job->console = CONSOLE_NONE;
	pid = process_spawn (job, args);

	waitpid (pid, NULL, 0);
	output = fopen (filename, "r");

	TEST_FILE_EQ (output, "0: 1 3\n");
	TEST_FILE_EQ (output, "1: 1 3\n");
	TEST_FILE_EQ (output, "2: 1 3\n");
	TEST_FILE_END (output);

	fclose (output);
	unlink (filename);

	nih_list_free (&job->entry);


	/* Check that a job with an alternate working directory is run from
	 * that directory.
	 */
	TEST_FEATURE ("with working directory");
	sprintf (function, "%d", TEST_PWD);

	job = job_new (NULL, "test");
	job->chdir = "/tmp";
	pid = process_spawn (job, args);

	waitpid (pid, NULL, 0);
	output = fopen (filename, "r");

	TEST_FILE_EQ (output, "wd: /tmp\n");
	TEST_FILE_END (output);

	fclose (output);
	unlink (filename);

	nih_list_free (&job->entry);


	/* Check that a job is run in a consistent environment containing
	 * only approved variables, or those set within the job.
	 */
	TEST_FEATURE ("with environment");
	sprintf (function, "%d", TEST_ENVIRONMENT);
	putenv ("BAR=baz");

	job = job_new (NULL, "test");
	job->id = 1000;
	job->env = env;
	env[0] = "FOO=bar";
	env[1] = NULL;
	pid = process_spawn (job, args);

	waitpid (pid, NULL, 0);
	output = fopen (filename, "r");

	TEST_FILE_EQ_N (output, "PATH=");
	TEST_FILE_EQ_N (output, "TERM=");
	TEST_FILE_EQ (output, "UPSTART_JOB_ID=1000\n");
	TEST_FILE_EQ (output, "UPSTART_JOB=test\n");
	TEST_FILE_EQ (output, "FOO=bar\n");
	TEST_FILE_END (output);

	fclose (output);
	unlink (filename);

	nih_list_free (&job->entry);


	/* Check that a job's environment includes the UPSTART_EVENT variable
	 * and any event environment if the cause member is set, but this
	 * should not override those specified in the job.
	 */
	TEST_FEATURE ("with environment and cause");
	sprintf (function, "%d", TEST_ENVIRONMENT);
	putenv ("BAR=baz");

	event = event_new (NULL, "wibble", NULL, NULL);
	NIH_MUST (nih_str_array_add (&event->info.env, event, NULL,
				     "FOO=APPLE"));
	NIH_MUST (nih_str_array_add (&event->info.env, event, NULL,
				     "TEA=YES"));

	job = job_new (NULL, "test");
	job->id = 1000;
	job->cause = event;
	job->env = env;
	env[0] = "FOO=bar";
	env[1] = NULL;
	pid = process_spawn (job, args);

	waitpid (pid, NULL, 0);
	output = fopen (filename, "r");

	TEST_FILE_EQ_N (output, "PATH=");
	TEST_FILE_EQ_N (output, "TERM=");
	TEST_FILE_EQ (output, "UPSTART_JOB_ID=1000\n");
	TEST_FILE_EQ (output, "UPSTART_JOB=test\n");
	TEST_FILE_EQ (output, "UPSTART_EVENT=wibble\n");
	TEST_FILE_EQ (output, "FOO=bar\n");
	TEST_FILE_EQ (output, "TEA=YES\n");
	TEST_FILE_END (output);

	fclose (output);
	unlink (filename);

	nih_list_free (&job->entry);
	nih_list_free (&event->entry);
}


void
test_kill (void)
{
	Job   *job;
	pid_t  pid;
	int    ret, status;

	TEST_FUNCTION ("process_kill");
	job = job_new (NULL, "test");

	/* Check that when we normally kill the process, the TERM signal
	 * is sent to it.
	 */
	TEST_FEATURE ("with TERM signal");
	TEST_CHILD (pid) {
		pause ();
	}

	ret = process_kill (job, pid, FALSE);
	waitpid (pid, &status, 0);

	TEST_EQ (ret, 0);
	TEST_TRUE (WIFSIGNALED (status));
	TEST_EQ (WTERMSIG (status), SIGTERM);


	/* Check that when we force the kill, the KILL signal is sent
	 * instead.
	 */
	TEST_FEATURE ("with KILL signal");
	TEST_CHILD (pid) {
		pause ();
	}

	ret = process_kill (job, pid, TRUE);
	waitpid (pid, &status, 0);

	TEST_EQ (ret, 0);
	TEST_TRUE (WIFSIGNALED (status));
	TEST_EQ (WTERMSIG (status), SIGKILL);


	nih_list_free (&job->entry);
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
