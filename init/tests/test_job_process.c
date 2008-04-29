/* upstart
 *
 * test_job_process.c - test suite for init/job_process.c
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

#include "job_process.h"
#include "job.h"
#include "event.h"
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
		getcwd (path, sizeof (path));
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

	return 0;
}
