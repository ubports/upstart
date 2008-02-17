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
	char          *env[2], *args[4];
	JobConfig     *config;
	Job           *job;
	EventOperator *oper;
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

	job = job_instance (config);
	pid = process_spawn (job, args, FALSE);
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

	job = job_instance (config);
	pid = process_spawn (job, args, FALSE);
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

	job = job_instance (config);
	pid = process_spawn (job, args, FALSE);
	TEST_GT (pid, 0);

	waitpid (pid, NULL, 0);
	output = fopen (filename, "r");

	TEST_FILE_EQ (output, "wd: /tmp\n");
	TEST_FILE_END (output);

	fclose (output);
	unlink (filename);

	nih_free (config);


	/* Check that a job is run in a consistent environment containing
	 * only approved variables, or those set within the job.
	 */
	TEST_FEATURE ("with environment");
	sprintf (function, "%d", TEST_ENVIRONMENT);
	putenv ("BAR=baz");

	config = job_config_new (NULL, "test");
	config->env = env;
	env[0] = "FOO=bar";
	env[1] = NULL;

	job = job_instance (config);
	job->id = 1000;
	pid = process_spawn (job, args, FALSE);
	TEST_GT (pid, 0);

	waitpid (pid, NULL, 0);
	output = fopen (filename, "r");

	TEST_FILE_EQ_N (output, "PATH=");
	TEST_FILE_EQ_N (output, "TERM=");
	TEST_FILE_EQ (output, "FOO=bar\n");
	TEST_FILE_EQ (output, "UPSTART_JOB=test\n");
	TEST_FILE_EQ (output, "UPSTART_JOB_ID=1000\n");
	TEST_FILE_END (output);

	fclose (output);
	unlink (filename);

	nih_free (config);


	/* Check that a job's environment includes the variables from all
	 * events that started the job, overriding those specified
	 * in the job.
	 */
	TEST_FEATURE ("with environment from start events");
	sprintf (function, "%d", TEST_ENVIRONMENT);
	putenv ("BAZ=baz");
	putenv ("COFFEE=YES");

	config = job_config_new (NULL, "test");
	config->env = env;
	env[0] = "FOO=bar";
	env[1] = NULL;

	job = job_instance (config);
	job->id = 1000;

	job->start_on = event_operator_new (job, EVENT_AND, NULL, NULL);

	oper = event_operator_new (job->start_on, EVENT_MATCH, "wibble", NULL);
	oper->value = TRUE;
	oper->event = event_new (oper, "wibble", NULL, NULL);
	NIH_MUST (nih_str_array_add (&oper->event->env, oper->event,
				     NULL, "FOO=APPLE"));
	NIH_MUST (nih_str_array_add (&oper->event->env, oper->event,
				     NULL, "TEA=YES"));
	event_ref (oper->event);
	oper->blocked = TRUE;
	event_block (oper->event);
	nih_tree_add (&job->start_on->node, &oper->node, NIH_TREE_LEFT);

	oper = event_operator_new (job->start_on, EVENT_MATCH, "wobble", NULL);
	oper->value = TRUE;
	oper->event = event_new (oper, "wobble", NULL, NULL);
	NIH_MUST (nih_str_array_add (&oper->event->env, oper->event,
				     NULL, "BAR=ORANGE"));
	NIH_MUST (nih_str_array_add (&oper->event->env, oper->event,
				     NULL, "COFFEE=NO"));
	event_ref (oper->event);
	oper->blocked = TRUE;
	event_block (oper->event);
	nih_tree_add (&job->start_on->node, &oper->node, NIH_TREE_RIGHT);

	pid = process_spawn (job, args, FALSE);
	TEST_GT (pid, 0);

	waitpid (pid, NULL, 0);
	output = fopen (filename, "r");

	TEST_FILE_EQ_N (output, "PATH=");
	TEST_FILE_EQ_N (output, "TERM=");
	TEST_FILE_EQ (output, "FOO=APPLE\n");
	TEST_FILE_EQ (output, "TEA=YES\n");
	TEST_FILE_EQ (output, "BAR=ORANGE\n");
	TEST_FILE_EQ (output, "COFFEE=NO\n");
	TEST_FILE_EQ (output, "UPSTART_EVENTS=wibble wobble\n");
	TEST_FILE_EQ (output, "UPSTART_JOB=test\n");
	TEST_FILE_EQ (output, "UPSTART_JOB_ID=1000\n");
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

	job = job_instance (config);
	pid = process_spawn (job, args, FALSE);
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

	job = job_instance (config);
	pid = process_spawn (job, args, TRUE);
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

	job = job_instance (config);
	pid = process_spawn (job, args, FALSE);
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
	Job       *job;
	pid_t      pid1, pid2;
	int        ret, status;

	TEST_FUNCTION ("process_kill");
	config = job_config_new (NULL, "test");
	job = job_instance (config);

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

	ret = process_kill (job, pid1, FALSE);
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

	ret = process_kill (job, pid1, TRUE);
	waitpid (pid1, &status, 0);

	TEST_EQ (ret, 0);
	TEST_TRUE (WIFSIGNALED (status));
	TEST_EQ (WTERMSIG (status), SIGKILL);

	ret = process_kill (job, pid1, TRUE);
	waitpid (pid2, &status, 0);

	TEST_EQ (ret, 0);
	TEST_TRUE (WIFSIGNALED (status));
	TEST_EQ (WTERMSIG (status), SIGKILL);


	nih_free (config);
}


void
test_environment (void)
{
	JobConfig      *config;
	Job            *job;
	EventOperator  *oper;
	char          **env;

	TEST_FUNCTION ("process_environment");

	/* Check that a job created with an empty environment will just have
	 * the built-ins and special variables in its environment.
	 */
	TEST_FEATURE ("with empty environment");
	config = job_config_new (NULL, "test");

	job = job_instance (config);
	job->id = 99;

	TEST_ALLOC_FAIL {
		env = process_environment (job);

		if (test_alloc_failed) {
			TEST_EQ_P (env, NULL);
			continue;
		}

		TEST_ALLOC_PARENT (env, job);
		TEST_ALLOC_SIZE (env, sizeof (char *) * 5);
		TEST_ALLOC_PARENT (env[0], env);
		TEST_EQ_STRN (env[0], "PATH=");
		TEST_ALLOC_PARENT (env[1], env);
		TEST_EQ_STRN (env[1], "TERM=");
		TEST_ALLOC_PARENT (env[2], env);
		TEST_EQ_STR (env[2], "UPSTART_JOB=test");
		TEST_ALLOC_PARENT (env[3], env);
		TEST_EQ_STR (env[3], "UPSTART_JOB_ID=99");
		TEST_EQ_P (env[4], NULL);

		nih_free (env);
	}

	nih_free (job);
	nih_free (config);


	/* Check that a job created with defined environment variables will
	 * have those appended to the environment as well as the builtins
	 * and specials.
	 */
	TEST_FEATURE ("with configured environment");
	config = job_config_new (NULL, "test");
	config->env = nih_str_array_new (config);
	assert (nih_str_array_add (&(config->env), config, NULL, "FOO=BAR"));
	assert (nih_str_array_add (&(config->env), config, NULL, "BAR=BAZ"));

	job = job_instance (config);
	job->id = 99;

	TEST_ALLOC_FAIL {
		env = process_environment (job);

		if (test_alloc_failed) {
			TEST_EQ_P (env, NULL);
			continue;
		}

		TEST_ALLOC_PARENT (env, job);
		TEST_ALLOC_SIZE (env, sizeof (char *) * 7);
		TEST_ALLOC_PARENT (env[0], env);
		TEST_EQ_STRN (env[0], "PATH=");
		TEST_ALLOC_PARENT (env[1], env);
		TEST_EQ_STRN (env[1], "TERM=");
		TEST_ALLOC_PARENT (env[2], env);
		TEST_EQ_STR (env[2], "FOO=BAR");
		TEST_ALLOC_PARENT (env[3], env);
		TEST_EQ_STR (env[3], "BAR=BAZ");
		TEST_ALLOC_PARENT (env[4], env);
		TEST_EQ_STR (env[4], "UPSTART_JOB=test");
		TEST_ALLOC_PARENT (env[5], env);
		TEST_EQ_STR (env[5], "UPSTART_JOB_ID=99");
		TEST_EQ_P (env[6], NULL);

		nih_free (env);
	}

	nih_free (job);
	nih_free (config);


	/* Check that a job created with environment in its start events will
	 * have those added to the environment as well as built-ins,
	 * specials and one containing the list of events.
	 */
	TEST_FEATURE ("with environment from start events");
	config = job_config_new (NULL, "test");

	job = job_instance (config);
	job->id = 99;

	job->start_on = event_operator_new (job, EVENT_AND, NULL, NULL);

	oper = event_operator_new (job->start_on, EVENT_MATCH, "wibble", NULL);
	oper->value = TRUE;
	oper->event = event_new (oper, "wibble", NULL, NULL);
	NIH_MUST (nih_str_array_add (&oper->event->env, oper->event,
				     NULL, "FOO=APPLE"));
	NIH_MUST (nih_str_array_add (&oper->event->env, oper->event,
				     NULL, "TEA=YES"));
	event_ref (oper->event);
	oper->blocked = TRUE;
	event_block (oper->event);
	nih_tree_add (&job->start_on->node, &oper->node, NIH_TREE_LEFT);

	oper = event_operator_new (job->start_on, EVENT_MATCH, "wobble", NULL);
	oper->value = TRUE;
	oper->event = event_new (oper, "wobble", NULL, NULL);
	NIH_MUST (nih_str_array_add (&oper->event->env, oper->event,
				     NULL, "BAR=ORANGE"));
	NIH_MUST (nih_str_array_add (&oper->event->env, oper->event,
				     NULL, "COFFEE=NO"));
	event_ref (oper->event);
	oper->blocked = TRUE;
	event_block (oper->event);
	nih_tree_add (&job->start_on->node, &oper->node, NIH_TREE_RIGHT);

	TEST_ALLOC_FAIL {
		env = process_environment (job);

		if (test_alloc_failed) {
			TEST_EQ_P (env, NULL);
			continue;
		}

		TEST_ALLOC_PARENT (env, job);
		TEST_ALLOC_SIZE (env, sizeof (char *) * 10);
		TEST_ALLOC_PARENT (env[0], env);
		TEST_EQ_STRN (env[0], "PATH=");
		TEST_ALLOC_PARENT (env[1], env);
		TEST_EQ_STRN (env[1], "TERM=");
		TEST_ALLOC_PARENT (env[2], env);
		TEST_EQ_STR (env[2], "FOO=APPLE");
		TEST_ALLOC_PARENT (env[3], env);
		TEST_EQ_STR (env[3], "TEA=YES");
		TEST_ALLOC_PARENT (env[4], env);
		TEST_EQ_STR (env[4], "BAR=ORANGE");
		TEST_ALLOC_PARENT (env[5], env);
		TEST_EQ_STR (env[5], "COFFEE=NO");
		TEST_ALLOC_PARENT (env[6], env);
		TEST_EQ_STR (env[6], "UPSTART_EVENTS=wibble wobble");
		TEST_ALLOC_PARENT (env[7], env);
		TEST_EQ_STR (env[7], "UPSTART_JOB=test");
		TEST_ALLOC_PARENT (env[8], env);
		TEST_EQ_STR (env[8], "UPSTART_JOB_ID=99");
		TEST_EQ_P (env[9], NULL);

		nih_free (env);
	}

	nih_free (job);
	nih_free (config);


	/* Check that configured environment and that from start events can
	 * override built-ins, that those from start events can override
	 * configured environment and that nothing can override the specials.
	 */
	TEST_FEATURE ("with environment from multiple sources");
	config = job_config_new (NULL, "test");
	config->env = nih_str_array_new (config);
	assert (nih_str_array_add (&(config->env), config, NULL, "FOO=BAR"));
	assert (nih_str_array_add (&(config->env), config, NULL, "BAR=BAZ"));
	assert (nih_str_array_add (&(config->env), config, NULL, "TERM=elmo"));
	assert (nih_str_array_add (&(config->env), config, NULL,
				   "UPSTART_JOB=evil"));

	job = job_instance (config);
	job->id = 99;

	job->start_on = event_operator_new (job, EVENT_AND, NULL, NULL);

	oper = event_operator_new (job->start_on, EVENT_MATCH, "wibble", NULL);
	oper->value = TRUE;
	oper->event = event_new (oper, "wibble", NULL, NULL);
	NIH_MUST (nih_str_array_add (&oper->event->env, oper->event,
				     NULL, "FOO=APPLE"));
	NIH_MUST (nih_str_array_add (&oper->event->env, oper->event,
				     NULL, "TEA=YES"));
	event_ref (oper->event);
	oper->blocked = TRUE;
	event_block (oper->event);
	nih_tree_add (&job->start_on->node, &oper->node, NIH_TREE_LEFT);

	oper = event_operator_new (job->start_on, EVENT_MATCH, "wobble", NULL);
	oper->value = TRUE;
	oper->event = event_new (oper, "wobble", NULL, NULL);
	NIH_MUST (nih_str_array_add (&oper->event->env, oper->event,
				     NULL, "PATH=/tmp"));
	NIH_MUST (nih_str_array_add (&oper->event->env, oper->event,
				     NULL, "UPSTART_JOB_ID=nonesuch"));
	event_ref (oper->event);
	oper->blocked = TRUE;
	event_block (oper->event);
	nih_tree_add (&job->start_on->node, &oper->node, NIH_TREE_RIGHT);

	TEST_ALLOC_FAIL {
		env = process_environment (job);

		if (test_alloc_failed) {
			TEST_EQ_P (env, NULL);
			continue;
		}

		TEST_ALLOC_PARENT (env, job);
		TEST_ALLOC_SIZE (env, sizeof (char *) * 9);
		TEST_ALLOC_PARENT (env[0], env);
		TEST_EQ_STR (env[0], "PATH=/tmp");
		TEST_ALLOC_PARENT (env[1], env);
		TEST_EQ_STR (env[1], "TERM=elmo");
		TEST_ALLOC_PARENT (env[2], env);
		TEST_EQ_STR (env[2], "FOO=APPLE");
		TEST_ALLOC_PARENT (env[3], env);
		TEST_EQ_STR (env[3], "BAR=BAZ");
		TEST_ALLOC_PARENT (env[4], env);
		TEST_EQ_STR (env[4], "UPSTART_JOB=test");
		TEST_ALLOC_PARENT (env[5], env);
		TEST_EQ_STR (env[5], "TEA=YES");
		TEST_ALLOC_PARENT (env[6], env);
		TEST_EQ_STR (env[6], "UPSTART_JOB_ID=99");
		TEST_ALLOC_PARENT (env[7], env);
		TEST_EQ_STR (env[7], "UPSTART_EVENTS=wibble wobble");
		TEST_EQ_P (env[8], NULL);

		nih_free (env);
	}

	nih_free (job);
	nih_free (config);
}

void
test_environment_add (void)
{
	char   **env = NULL, **ret;
	size_t   len = 0;

	TEST_FUNCTION ("process_environment_add");

	/* Check that we can add a variable to a new environment table
	 * and that it is appended to the array.
	 */
	TEST_FEATURE ("with empty table");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			len = 0;
			env = nih_str_array_new (NULL);
		}

		ret = process_environment_add (&env, NULL, &len, "FOO=BAR");

		if (test_alloc_failed) {
			TEST_EQ_P (ret, NULL);

			TEST_EQ (len, 0);
			TEST_EQ_P (env[0], NULL);

			nih_free (env);
			continue;
		}

		TEST_NE_P (ret, NULL);

		TEST_EQ (len, 1);
		TEST_ALLOC_PARENT (env[0], env);
		TEST_ALLOC_SIZE (env[0], 8);
		TEST_EQ_STR (env[0], "FOO=BAR");
		TEST_EQ_P (env[1], NULL);

		nih_free (env);
	}


	/* Check that we can add a variable to an environment table with
	 * existing different entries and that it is appended to the array.
	 */
	TEST_FEATURE ("with new variable");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			len = 0;
			env = nih_str_array_new (NULL);
			assert (nih_str_array_add (&env, NULL, &len,
						   "FOO=BAR"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "BAR=BAZ"));
		}

		ret = process_environment_add (&env, NULL, &len,
					       "FRODO=BAGGINS");

		if (test_alloc_failed) {
			TEST_EQ_P (ret, NULL);

			TEST_EQ (len, 2);
			TEST_EQ_STR (env[0], "FOO=BAR");
			TEST_EQ_STR (env[1], "BAR=BAZ");
			TEST_EQ_P (env[2], NULL);

			nih_free (env);
			continue;
		}

		TEST_NE_P (ret, NULL);

		TEST_EQ (len, 3);
		TEST_EQ_STR (env[0], "FOO=BAR");
		TEST_EQ_STR (env[1], "BAR=BAZ");
		TEST_ALLOC_PARENT (env[2], env);
		TEST_ALLOC_SIZE (env[2], 14);
		TEST_EQ_STR (env[2], "FRODO=BAGGINS");
		TEST_EQ_P (env[3], NULL);

		nih_free (env);
	}


	/* Check that we can add a variable from the environment to the table
	 * and that it is appended to the array.
	 */
	TEST_FEATURE ("with new variable from environment");
	putenv ("FRODO=BAGGINS");

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			len = 0;
			env = nih_str_array_new (NULL);
			assert (nih_str_array_add (&env, NULL, &len,
						   "FOO=BAR"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "BAR=BAZ"));
		}

		ret = process_environment_add (&env, NULL, &len, "FRODO");

		if (test_alloc_failed) {
			TEST_EQ_P (ret, NULL);

			TEST_EQ (len, 2);
			TEST_EQ_STR (env[0], "FOO=BAR");
			TEST_EQ_STR (env[1], "BAR=BAZ");
			TEST_EQ_P (env[2], NULL);

			nih_free (env);
			continue;
		}

		TEST_NE_P (ret, NULL);

		TEST_EQ (len, 3);
		TEST_EQ_STR (env[0], "FOO=BAR");
		TEST_EQ_STR (env[1], "BAR=BAZ");
		TEST_ALLOC_PARENT (env[2], env);
		TEST_ALLOC_SIZE (env[2], 14);
		TEST_EQ_STR (env[2], "FRODO=BAGGINS");
		TEST_EQ_P (env[3], NULL);

		nih_free (env);
	}

	unsetenv ("FRODO");


	/* Check that when we attempt to add a variable that's not in the
	 * environment, the table is not extended.
	 */
	TEST_FEATURE ("with new variable unset in environment");
	unsetenv ("FRODO");

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			len = 0;
			env = nih_str_array_new (NULL);
			assert (nih_str_array_add (&env, NULL, &len,
						   "FOO=BAR"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "BAR=BAZ"));
		}

		ret = process_environment_add (&env, NULL, &len, "FRODO");

		if (test_alloc_failed) {
			TEST_EQ_P (ret, NULL);

			TEST_EQ (len, 2);
			TEST_EQ_STR (env[0], "FOO=BAR");
			TEST_EQ_STR (env[1], "BAR=BAZ");
			TEST_EQ_P (env[2], NULL);

			nih_free (env);
			continue;
		}

		TEST_NE_P (ret, NULL);

		TEST_EQ (len, 2);
		TEST_EQ_STR (env[0], "FOO=BAR");
		TEST_EQ_STR (env[1], "BAR=BAZ");
		TEST_EQ_P (env[2], NULL);

		nih_free (env);
	}


	/* Check that we can replace a variable in the environment table
	 * when one already exists with the same or different value.
	 */
	TEST_FEATURE ("with replacement variable");
	TEST_ALLOC_FAIL {
		char *old_env;

		TEST_ALLOC_SAFE {
			len = 0;
			env = nih_str_array_new (NULL);
			assert (nih_str_array_add (&env, NULL, &len,
						   "FOO=BAR"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "BAR=BAZ"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "FRODO=BAGGINS"));
		}

		old_env = env[1];
		TEST_FREE_TAG (old_env);

		ret = process_environment_add (&env, NULL, &len,
					       "BAR=WIBBLE");

		if (test_alloc_failed) {
			TEST_EQ_P (ret, NULL);
			TEST_NOT_FREE (old_env);

			TEST_EQ (len, 3);
			TEST_EQ_STR (env[0], "FOO=BAR");
			TEST_EQ_STR (env[1], "BAR=BAZ");
			TEST_EQ_STR (env[2], "FRODO=BAGGINS");
			TEST_EQ_P (env[3], NULL);

			nih_free (env);
			continue;
		}

		TEST_FREE (old_env);

		TEST_NE_P (ret, NULL);

		TEST_EQ (len, 3);
		TEST_EQ_STR (env[0], "FOO=BAR");
		TEST_ALLOC_PARENT (env[1], env);
		TEST_ALLOC_SIZE (env[1], 11);
		TEST_EQ_STR (env[1], "BAR=WIBBLE");
		TEST_EQ_STR (env[2], "FRODO=BAGGINS");
		TEST_EQ_P (env[3], NULL);

		nih_free (env);
	}


	/* Check that we can replace a variable from the environment in the
	 * environment table when one already exists with the same or
	 * different value.
	 */
	TEST_FEATURE ("with replacement variable from environment");
	putenv ("BAR=WIBBLE");

	TEST_ALLOC_FAIL {
		char *old_env;

		TEST_ALLOC_SAFE {
			len = 0;
			env = nih_str_array_new (NULL);
			assert (nih_str_array_add (&env, NULL, &len,
						   "FOO=BAR"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "BAR=BAZ"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "FRODO=BAGGINS"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "BILBO=TOOK"));
		}

		old_env = env[1];
		TEST_FREE_TAG (old_env);

		ret = process_environment_add (&env, NULL, &len, "BAR");

		if (test_alloc_failed) {
			TEST_EQ_P (ret, NULL);
			TEST_NOT_FREE (old_env);

			TEST_EQ (len, 4);
			TEST_EQ_STR (env[0], "FOO=BAR");
			TEST_EQ_STR (env[1], "BAR=BAZ");
			TEST_EQ_STR (env[2], "FRODO=BAGGINS");
			TEST_EQ_STR (env[3], "BILBO=TOOK");
			TEST_EQ_P (env[4], NULL);

			nih_free (env);
			continue;
		}

		TEST_FREE (old_env);

		TEST_NE_P (ret, NULL);

		TEST_EQ (len, 4);
		TEST_EQ_STR (env[0], "FOO=BAR");
		TEST_ALLOC_PARENT (env[1], env);
		TEST_ALLOC_SIZE (env[1], 11);
		TEST_EQ_STR (env[1], "BAR=WIBBLE");
		TEST_EQ_STR (env[2], "FRODO=BAGGINS");
		TEST_EQ_STR (env[3], "BILBO=TOOK");
		TEST_EQ_P (env[4], NULL);

		nih_free (env);
	}

	unsetenv ("BAR");


	/* Check that when we attempt to replace a variable that's unset
	 * in the environment, the existing variable is removed from the
	 * table.
	 */
	TEST_FEATURE ("with replacement variable unset in environment");
	unsetenv ("BAR");

	TEST_ALLOC_FAIL {
		char *old_env;

		TEST_ALLOC_SAFE {
			len = 0;
			env = nih_str_array_new (NULL);
			assert (nih_str_array_add (&env, NULL, &len,
						   "FOO=BAR"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "BAR=BAZ"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "FRODO=BAGGINS"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "BILBO=TOOK"));
		}

		old_env = env[1];
		TEST_FREE_TAG (old_env);

		ret = process_environment_add (&env, NULL, &len, "BAR");

		if (test_alloc_failed) {
			TEST_EQ_P (ret, NULL);
			TEST_NOT_FREE (old_env);

			TEST_EQ (len, 4);
			TEST_EQ_STR (env[0], "FOO=BAR");
			TEST_EQ_STR (env[1], "BAR=BAZ");
			TEST_EQ_STR (env[2], "FRODO=BAGGINS");
			TEST_EQ_STR (env[3], "BILBO=TOOK");
			TEST_EQ_P (env[4], NULL);

			nih_free (env);
			continue;
		}

		TEST_FREE (old_env);

		TEST_NE_P (ret, NULL);

		TEST_EQ (len, 3);
		TEST_EQ_STR (env[0], "FOO=BAR");
		TEST_EQ_STR (env[1], "FRODO=BAGGINS");
		TEST_EQ_STR (env[2], "BILBO=TOOK");
		TEST_EQ_P (env[3], NULL);

		nih_free (env);
	}

	unsetenv ("BAR");
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
	test_environment ();
	test_environment_add ();

	return 0;
}
