/* upstart
 *
 * test_process.c - test suite for init/process.c
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

#include <config.h>

#include <sys/types.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>

#include "job.h"
#include "process.h"


extern pid_t getsid (pid_t);


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
	char  buf[4096];
	int   i;

	out = fopen (filename, "w");

	switch (test) {
	case TEST_PIDS:
		fprintf (out, "%d\n", getpid ());
		fprintf (out, "%d\n", getppid ());
		fprintf (out, "%d\n", getpgrp ());
		fprintf (out, "%d\n", getsid (0));
		exit (0);
	case TEST_CONSOLE:
		for (i = 0; i < 3; i++) {
			struct stat buf;

			fstat (i, &buf);
			fprintf (out, "%d %d\n", major (buf.st_rdev),
				 minor (buf.st_rdev));
		}
		exit (0);
	case TEST_PWD:
		getcwd (buf, sizeof (buf));
		fprintf (out, "%s\n", buf);
		exit (0);
	case TEST_ENVIRONMENT:
		for (char **env = __environ; *env; env++)
			fprintf (out, "%s\n", *env);
		exit (0);
	}
}

int
test_spawn (void)
{
	FILE  *output;
	char   function[6], filename[24], text[81], *env[2];
	char  *args[4];
	Job   *job;
	pid_t  pid;
	int    ret = 0, maj, min, i;

	printf ("Testing process_spawn()\n");
	args[0] = argv0;
	args[1] = function;
	args[2] = filename;
	args[3] = NULL;

	sprintf (filename, "/tmp/test_process.%d", getpid ());
	unlink (filename);

	printf ("...with simple job\n");
	sprintf (function, "%d", TEST_PIDS);
	job = job_new (NULL, "test");
	pid = process_spawn (job, args);
	waitpid (pid, NULL, 0);
	output = fopen (filename, "r");

	/* Return value should be pid and should be a positive integer */
	if (pid <= 0) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}

	/* Return value should not be our process id */
	if (pid == getpid ()) {
		printf ("BAD: process id was our own.\n");
		ret = 1;
	}

	/* Return value should be the process id of the child */
	fgets (text, sizeof (text), output);
	if (atoi (text) != pid) {
		printf ("BAD: process id of child wasn't what we expected.\n");
		ret = 1;
	}

	/* Child's parent should be us */
	fgets (text, sizeof (text), output);
	if (atoi (text) != getpid ()) {
		printf ("BAD: parent process wasn't what we expected.\n");
		ret = 1;
	}

	/* Child should be in its own process group */
	fgets (text, sizeof (text), output);
	if (atoi (text) != pid) {
		printf ("BAD: child process group wasn't what we expected.\n");
		ret = 1;
	}

	/* Child should be in its own session */
	fgets (text, sizeof (text), output);
	if (atoi (text) != pid) {
		printf ("BAD: child session wasn't what we expected.\n");
		ret = 1;
	}

	fclose (output);
	unlink (filename);
	nih_list_free (&job->entry);


	printf ("...with no console\n");
	sprintf (function, "%d", TEST_CONSOLE);
	job = job_new (NULL, "test");
	job->console = CONSOLE_NONE;
	pid = process_spawn (job, args);
	waitpid (pid, NULL, 0);
	output = fopen (filename, "r");

	/* Standard input should be /dev/null */
	fgets (text, sizeof (text), output);
	sscanf (text, "%d %d", &maj, &min);
	if ((maj != 1) || (min != 3)) {
		printf ("BAD: standard input wasn't what we expected.\n");
		ret = 1;
	}

	/* Standard output should be /dev/null */
	fgets (text, sizeof (text), output);
	sscanf (text, "%d %d", &maj, &min);
	if ((maj != 1) || (min != 3)) {
		printf ("BAD: standard output wasn't what we expected.\n");
		ret = 1;
	}

	/* Standard error should be /dev/null */
	fgets (text, sizeof (text), output);
	sscanf (text, "%d %d", &maj, &min);
	if ((maj != 1) || (min != 3)) {
		printf ("BAD: standard error wasn't what we expected.\n");
		ret = 1;
	}

	fclose (output);
	unlink (filename);
	nih_list_free (&job->entry);


	printf ("...with working directory\n");
	sprintf (function, "%d", TEST_PWD);
	job = job_new (NULL, "test");
	job->chdir = "/tmp";
	pid = process_spawn (job, args);
	waitpid (pid, NULL, 0);
	output = fopen (filename, "r");

	/* Should be in the right working directory */
	fgets (text, sizeof (text), output);
	if (strcmp (text, "/tmp\n")) {
		printf ("BAD: working directory wasn't what we expected.\n");
		ret = 1;
	}

	fclose (output);
	unlink (filename);
	nih_list_free (&job->entry);


	printf ("...with environment\n");
	sprintf (function, "%d", TEST_ENVIRONMENT);
	job = job_new (NULL, "test");
	job->env = env;
	env[0] = "FOO=bar";
	env[1] = NULL;
	/* Environment shouldn't leak */
	putenv ("BAR=baz");
	pid = process_spawn (job, args);
	waitpid (pid, NULL, 0);
	output = fopen (filename, "r");

	/* Check for unexpected environment */
	i = 0;
	while (fgets (text, sizeof (text), output) > 0) {
		if (! strcmp (text, "FOO=bar\n")) {
			i++;
		} else {
			printf ("BAD: environment wasn't what we expected.\n");
			ret = 1;
		}
	}

	/* Should have got all environment variables we expected */
	if (i != 1) {
		printf ("BAD: environment wasn't what we expected.\n");
		ret = 1;
	}

	fclose (output);
	unlink (filename);
	nih_list_free (&job->entry);

	return ret;
}


int
test_kill (void)
{
	Job   *job;
	pid_t  pid;
	int    ret = 0, retval, status;

	printf ("Testing process_kill()\n");
	job = job_new (NULL, "test");

	printf ("...with TERM signal\n");
	pid = fork ();
	if (pid == 0) {
		poll (NULL, 0, -1);

		exit (0);
	}

	assert (pid >= 0);
	usleep (1000); /* Urgh */
	retval = process_kill (job, pid, FALSE);
	waitpid (pid, &status, 0);

	/* Return value should be zero */
	if (retval != 0) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}

	/* Child should have exited by signal */
	if (! WIFSIGNALED (status)) {
		printf ("BAD: child not terminated by signal.\n");
		ret = 1;
	}

	/* Signal should have been SIGTERM */
	if (WTERMSIG (status) != SIGTERM) {
		printf ("BAD: child not terminated by TERM signal.\n");
		ret = 1;
	}


	printf ("...with KILL signal\n");
	pid = fork ();
	if (pid == 0) {
		poll (NULL, 0, -1);

		exit (0);
	}

	assert (pid >= 0);
	usleep (1000); /* Urgh */
	retval = process_kill (job, pid, TRUE);
	waitpid (pid, &status, 0);

	/* Return value should be zero */
	if (retval != 0) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}

	/* Child should have exited by signal */
	if (! WIFSIGNALED (status)) {
		printf ("BAD: child not terminated by signal.\n");
		ret = 1;
	}

	/* Signal should have been SIGKILL */
	if (WTERMSIG (status) != SIGKILL) {
		printf ("BAD: child not terminated by KILL signal.\n");
		ret = 1;
	}


	nih_list_free (&job->entry);

	return ret;
}


int
main (int   argc,
      char *argv[])
{
	int ret = 0;

	/* We re-exec this binary to test various children features */
	if (argv[0][0] != '/') {
		char buf[4096];

		getcwd (buf, sizeof (buf));
		strcat (buf, "/");
		strcat (buf, argv[0]);

		argv0 = buf;
	} else {
		argv0 = argv[0];
	}
	if (argc > 2)
		child (atoi (argv[1]), argv[2]);

	ret |= test_spawn ();
	ret |= test_kill ();

	return ret;
}
