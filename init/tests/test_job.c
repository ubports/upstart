/* upstart
 *
 * test_job.c - test suite for init/job.c
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
#include <sys/select.h>

#include <time.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/io.h>
#include <nih/main.h>

#include "event.h"
#include "job.h"
#include "control.h"


void
test_new (void)
{
	Job *job;
	int  i;

	/* Check that we can create a new job structure; the structure
	 * should be allocated with nih_alloc, placed in the jobs list
	 * and have sensible defaults.
	 */
	TEST_FUNCTION ("job_new");
	job_list ();
	TEST_ALLOC_FAIL {
		job = job_new (NULL, "test");

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (job, sizeof (Job));
		TEST_LIST_NOT_EMPTY (&job->entry);

		TEST_ALLOC_PARENT (job->name, job);
		TEST_EQ_STR (job->name, "test");
		TEST_EQ_P (job->description, NULL);
		TEST_EQ_P (job->author, NULL);
		TEST_EQ_P (job->version, NULL);

		TEST_EQ (job->delete, FALSE);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ_P (job->blocker, NULL);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);

		TEST_LIST_EMPTY (&job->start_events);
		TEST_LIST_EMPTY (&job->stop_events);
		TEST_LIST_EMPTY (&job->emits);

		TEST_EQ_P (job->normalexit, NULL);
		TEST_EQ (job->normalexit_len, 0);

		TEST_EQ (job->kill_timeout, JOB_DEFAULT_KILL_TIMEOUT);
		TEST_EQ_P (job->kill_timer, NULL);

		TEST_EQ (job->instance, FALSE);
		TEST_EQ (job->service, FALSE);
		TEST_EQ (job->respawn, FALSE);
		TEST_EQ (job->respawn_limit, JOB_DEFAULT_RESPAWN_LIMIT);
		TEST_EQ (job->respawn_interval, JOB_DEFAULT_RESPAWN_INTERVAL);
		TEST_EQ (job->respawn_count, 0);
		TEST_EQ (job->respawn_time, 0);

		TEST_EQ (job->daemon, FALSE);
		TEST_EQ_P (job->pid_file, NULL);
		TEST_EQ_P (job->pid_binary, NULL);
		TEST_EQ (job->pid_timeout, JOB_DEFAULT_PID_TIMEOUT);
		TEST_EQ_P (job->pid_timer, NULL);

		TEST_EQ_P (job->command, NULL);
		TEST_EQ_P (job->script, NULL);
		TEST_EQ_P (job->start_script, NULL);
		TEST_EQ_P (job->stop_script, NULL);

		TEST_EQ (job->console, CONSOLE_NONE);
		TEST_EQ_P (job->env, NULL);

		TEST_EQ (job->umask, JOB_DEFAULT_UMASK);
		TEST_EQ (job->nice, 0);

		for (i = 0; i < RLIMIT_NLIMITS; i++)
			TEST_EQ_P (job->limits[i], NULL);

		TEST_EQ_P (job->chroot, NULL);
		TEST_EQ_P (job->chdir, NULL);

		nih_list_free (&job->entry);
	}
}

void
test_find_by_name (void)
{
	Job *job1, *job2, *job3, *ptr;

	TEST_FUNCTION ("job_find_by_name");
	job1 = job_new (NULL, "foo");
	job2 = job_new (NULL, "bar");
	job3 = job_new (NULL, "baz");

	/* Check that we can find a job that exists by its name. */
	TEST_FEATURE ("with name we expect to find");
	ptr = job_find_by_name ("bar");

	TEST_EQ_P (ptr, job2);


	/* Check that we get NULL if the job doesn't exist. */
	TEST_FEATURE ("with name we do not expect to find");
	ptr = job_find_by_name ("frodo");

	TEST_EQ_P (ptr, NULL);


	/* Check that we get NULL if the job list is empty, and nothing
	 * bad happens.
	 */
	TEST_FEATURE ("with empty job list");
	nih_list_free (&job3->entry);
	nih_list_free (&job2->entry);
	nih_list_free (&job1->entry);
	ptr = job_find_by_name ("bar");

	TEST_EQ_P (ptr, NULL);
}

void
test_find_by_pid (void)
{
	Job *job1, *job2, *job3, *ptr;

	TEST_FUNCTION ("job_find_by_pid");
	job1 = job_new (NULL, "foo");
	job1->pid = 10;
	job2 = job_new (NULL, "bar");
	job3 = job_new (NULL, "baz");
	job3->pid = 20;

	/* Check that we can find a job that exists by the pid of its
	 * primary process.
	 */
	TEST_FEATURE ("with pid we expect to find");
	ptr = job_find_by_pid (20);

	TEST_EQ_P (ptr, job3);


	/* Check that we get NULL if no job has a process with that pid. */
	TEST_FEATURE ("with pid we do not expect to find");
	ptr = job_find_by_pid (30);

	TEST_EQ_P (ptr, NULL);


	/* Check that we get NULL if there are jobs in the list, but none
	 * have pids.
	 */
	TEST_FEATURE ("with no pids in job list");
	nih_list_free (&job3->entry);
	nih_list_free (&job1->entry);
	ptr = job_find_by_pid (20);

	TEST_EQ_P (ptr, NULL);


	/* Check that we get NULL if there are no jobs in the list. */
	TEST_FEATURE ("with empty job list");
	nih_list_free (&job2->entry);
	ptr = job_find_by_pid (20);

	TEST_EQ_P (ptr, NULL);
}


void
test_change_goal (void)
{
	EventEmission *em;
	Job           *job;

	TEST_FUNCTION ("job_change_goal");
	program_name = "test";

	job = job_new (NULL, "test");
	job->start_script = "echo";
	job->stop_script = "echo";


	/* Check that an attempt to start a waiting job results in the
	 * goal being changed to start, and the state transitioned to
	 * starting.
	 */
	TEST_FEATURE ("with waiting job");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_WAITING;
		job->pid = 0;

		job_change_goal (job, JOB_START, NULL);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STARTING);
		TEST_EQ (job->pid, 0);
	}


	/* Check that an attempt to start a job that's in the process of
	 * stopping changes only the goal, and leaves the rest of the
	 * state transition up to the normal process.
	 */
	TEST_FEATURE ("with stopping job");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_KILLED;
		job->pid = 1;

		job_change_goal (job, JOB_START, NULL);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_KILLED);
		TEST_EQ (job->pid, 1);
	}


	/* Check that starting a job with a cause set unreferences it
	 * and sets it to NULL in the job.
	 */
	TEST_FEATURE ("with existing cause");
	em = event_emit ("foo", NULL, NULL);

	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_STOPPING;
		job->pid = 1;
		job->cause = em;
		em->progress = EVENT_HANDLING;
		em->jobs = 1;

		job_change_goal (job, JOB_START, NULL);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid, 1);

		TEST_EQ_P (job->cause, NULL);

		TEST_EQ (em->jobs, 0);
		TEST_EQ (em->progress, EVENT_FINISHED);
	}

	nih_list_free (&em->event.entry);
	job->cause = NULL;


	/* Check that starting a job with a cause passed references that event
	 * and sets the cause in the job.
	 */
	TEST_FEATURE ("with new cause");
	em = event_emit ("foo", NULL, NULL);

	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;
		job->pid = 1;
		job->cause = NULL;
		em->jobs = 0;

		job_change_goal (job, JOB_START, em);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_POST_STOP);
		TEST_EQ (job->pid, 1);

		TEST_EQ_P (job->cause, em);

		TEST_EQ (em->jobs, 1);
	}

	nih_list_free (&em->event.entry);
	job->cause = NULL;


	/* Check that an attempt to start a job that's running and still
	 * with a start goal does nothing.
	 */
	TEST_FEATURE ("with running job");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid = 1;

		job_change_goal (job, JOB_START, NULL);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid, 1);
	}


	/* Check that an attempt to stop a running job results in the goal
	 * and the state being changed.
	 */
	TEST_FEATURE ("with running job");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid = 1;

		job_change_goal (job, JOB_STOP, NULL);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid, 1);
	}


	/* Check that an attempt to stop a running job withoug any process
	 * only results in the goal being changed.
	 */
	TEST_FEATURE ("with running job");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid = 0;

		job_change_goal (job, JOB_STOP, NULL);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid, 0);
	}


	/* Check that an attempt to stop a starting job only results in the
	 * goal being changed, the state should not be changed.
	 */
	TEST_FEATURE ("with starting job");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->pid = 1;

		job_change_goal (job, JOB_STOP, NULL);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_PRE_START);
		TEST_EQ (job->pid, 1);
	}


	/* Check that stopping a job with a cause event set unreferences the
	 * event and sets it to NULL.
	 */
	TEST_FEATURE ("with existing cause");
	em = event_emit ("foo", NULL, NULL);

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid = 1;
		job->cause = em;
		em->jobs = 1;
		em->progress = EVENT_HANDLING;

		job_change_goal (job, JOB_STOP, NULL);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_SPAWNED);
		TEST_EQ (job->pid, 1);
		TEST_EQ_P (job->cause, NULL);

		TEST_EQ (em->jobs, 0);
		TEST_EQ (em->progress, EVENT_FINISHED);
	}

	nih_list_free (&em->event.entry);
	job->cause = NULL;


	/* Check that stopping a job passing a cause references that event
	 * and sets the job's cause.
	 */
	TEST_FEATURE ("with existing cause");
	em = event_emit ("foo", NULL, NULL);

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_STARTING;
		job->pid = 0;
		job->cause = NULL;
		em->jobs = 0;

		job_change_goal (job, JOB_STOP, em);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STARTING);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->jobs, 1);
	}

	nih_list_free (&em->event.entry);
	job->cause = NULL;


	/* Check that an attempt to stop a waiting job does nothing. */
	TEST_FEATURE ("with waiting job");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_WAITING;
		job->pid = 0;

		job_change_goal (job, JOB_STOP, NULL);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ (job->pid, 0);
	}


	nih_list_free (&job->entry);
	event_poll ();
}


void
test_change_state (void)
{
	FILE          *output;
	Job           *job;
	EventEmission *cause, *emission;
	NihList       *events;
	struct stat    statbuf;
	char           dirname[PATH_MAX], filename[PATH_MAX], *tmp;
	pid_t          pid;
	int            status;

	TEST_FUNCTION ("job_change_state");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (dirname);
	mkdir (dirname, 0700);

	job = job_new (NULL, "test");
	job->start_script = nih_sprintf (job, "touch %s/start", dirname);
	job->stop_script = nih_sprintf (job, "touch %s/stop", dirname);
	job->command = nih_sprintf (job, "touch %s/run", dirname);
	job->respawn_limit = 0;

	/* This is a naughty way of getting a pointer to the event queue
	 * list head...  we keep hold of the emitted event, but remove it
	 * from the list (so it doesn't affect our "events emitted" check)
	 */
	event_poll ();
	cause = event_emit ("wibble", NULL, NULL);
	events = cause->event.entry.prev;
	nih_list_remove (&cause->event.entry);


	/* Check that a job can move from waiting to starting.  This
	 * should emit the starting event and block on it after clearing
	 * out any failed information from the last time the job was run.
	 */
	TEST_FEATURE ("waiting to starting");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_WAITING;
		job->pid = 0;

		job->cause = cause;
		job->blocker = NULL;

		job->failed = TRUE;
		job->failed_state = JOB_STOPPING;
		job->exit_status = 1;

		job->instance = FALSE;
		job->service = FALSE;

		job_change_state (job, JOB_STARTING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STARTING);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocker, (EventEmission *)events->next);

		emission = (EventEmission *)events->next;
		TEST_ALLOC_SIZE (emission, sizeof (EventEmission));
		TEST_EQ_STR (emission->event.name, "starting");
		TEST_EQ_STR (emission->event.args[0], "test");
		TEST_EQ_P (emission->event.args[1], NULL);
		nih_list_free (&emission->event.entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);
	}


	/* Check that if a job tries to move from waiting to starting too
	 * many times in a given period then it is caught, a message is
	 * output and the job sent back to stopped and waiting.  We get
	 * a stopped event, and the cause is cleared.  The failed information
	 * should reflect that we were stopped by the starting state.
	 */
	TEST_FEATURE ("waiting to starting too fast");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_WAITING;
		job->pid = 0;

		job->cause = cause;
		job->blocker = NULL;

		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;

		job->instance = FALSE;
		job->service = FALSE;

		job->respawn_limit = 10;
		job->respawn_interval = 1000;
		job->respawn_time = time (NULL);
		job->respawn_count = 10;

		TEST_DIVERT_STDERR (output) {
			job_change_state (job, JOB_STARTING);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ_P (job->blocker, NULL);

		emission = (EventEmission *)events->next;
		TEST_ALLOC_SIZE (emission, sizeof (EventEmission));
		TEST_EQ_STR (emission->event.name, "stopped");
		TEST_EQ_STR (emission->event.args[0], "test");
		TEST_EQ_STR (emission->event.args[1], "failed");
		TEST_EQ_STR (emission->event.args[2], "starting");
		TEST_EQ_P (emission->event.args[3], NULL);
		nih_list_free (&emission->event.entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_state, JOB_STARTING);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output,
			      "test: test respawning too fast, stopped");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	job->respawn_limit = 0;
	job->respawn_interval = JOB_DEFAULT_RESPAWN_INTERVAL;
	job->respawn_time = 0;
	job->respawn_count = 0;


	/* Check that a job with a start process can move from starting
	 * to pre-start, and have the process run.
	 */
	TEST_FEATURE ("starting to pre-start");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_STARTING;
		job->pid = 0;

		job->cause = cause;
		job->blocker = NULL;

		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;

		job->instance = FALSE;
		job->service = FALSE;

		job_change_state (job, JOB_PRE_START);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_PRE_START);
		TEST_NE (job->pid, 0);

		waitpid (job->pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/start");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocker, NULL);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);
	}


	/* Check that a job without a start process can move from starting
	 * to pre-start, skipping over that state, and instead going all
	 * the way through to the running state.  Because we get there,
	 * we should get a started event emitted.
	 */
	TEST_FEATURE ("starting to pre-start without process");
	tmp = job->start_script;
	job->start_script = NULL;

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_STARTING;
		job->pid = 0;

		job->cause = cause;
		job->blocker = NULL;

		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;

		job->instance = FALSE;
		job->service = FALSE;

		job_change_state (job, JOB_PRE_START);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_NE (job->pid, 0);

		waitpid (job->pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/run");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocker, NULL);

		emission = (EventEmission *)events->next;
		TEST_ALLOC_SIZE (emission, sizeof (EventEmission));
		TEST_EQ_STR (emission->event.name, "started");
		TEST_EQ_STR (emission->event.args[0], "test");
		TEST_EQ_P (emission->event.args[1], NULL);
		nih_list_free (&emission->event.entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);
	}

	job->start_script = tmp;


	/* Check that a job with a main process can move from pre-start to
	 * spawned and have the process run, and as it's not a daemon,
	 * the state will be skipped forwards to running and the started
	 * event emitted.
	 */
	TEST_FEATURE ("pre-start to spawned");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->pid = 0;

		job->cause = cause;
		job->blocker = NULL;

		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;

		job->instance = FALSE;
		job->service = FALSE;

		job_change_state (job, JOB_SPAWNED);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_NE (job->pid, 0);

		waitpid (job->pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/run");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocker, NULL);

		emission = (EventEmission *)events->next;
		TEST_ALLOC_SIZE (emission, sizeof (EventEmission));
		TEST_EQ_STR (emission->event.name, "started");
		TEST_EQ_STR (emission->event.args[0], "test");
		TEST_EQ_P (emission->event.args[1], NULL);
		nih_list_free (&emission->event.entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);
	}


 	/* Check that a job without a main process can move from pre-start
	 * straight to running skipping the interim steps, and has the
	 * started event emitted.
	 */
	TEST_FEATURE ("pre-start to spawned without process");
	tmp = job->command;
	job->command = NULL;

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->pid = 0;

		job->cause = cause;
		job->blocker = NULL;

		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;

		job->instance = FALSE;
		job->service = FALSE;

		job_change_state (job, JOB_SPAWNED);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocker, NULL);

		emission = (EventEmission *)events->next;
		TEST_ALLOC_SIZE (emission, sizeof (EventEmission));
		TEST_EQ_STR (emission->event.name, "started");
		TEST_EQ_STR (emission->event.args[0], "test");
		TEST_EQ_P (emission->event.args[1], NULL);
		nih_list_free (&emission->event.entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);
	}

	job->command = tmp;


	/* Check that a task can move from post-start to running, which will
	 * emit the started event but leave the cause alone.
	 */
	TEST_FEATURE ("post-start to running");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_POST_START;
		job->pid = 1;

		job->cause = cause;
		job->blocker = NULL;

		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;

		job->instance = FALSE;
		job->service = FALSE;

		job_change_state (job, JOB_RUNNING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid, 1);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocker, NULL);

		emission = (EventEmission *)events->next;
		TEST_ALLOC_SIZE (emission, sizeof (EventEmission));
		TEST_EQ_STR (emission->event.name, "started");
		TEST_EQ_STR (emission->event.args[0], "test");
		TEST_EQ_P (emission->event.args[1], NULL);
		nih_list_free (&emission->event.entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);
	}


	/* Check that a service can move from post-start to running, which
	 * will emit the started event and clear the cause since the job
	 * has reached the desired state.
	 */
	TEST_FEATURE ("post-start to running for service");
	job->service = TRUE;

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_POST_START;
		job->pid = 1;

		cause->jobs = 2;
		job->cause = cause;
		job->blocker = NULL;

		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;

		job->instance = FALSE;
		job->service = FALSE;

		job_change_state (job, JOB_RUNNING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid, 1);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ_P (job->blocker, NULL);

		TEST_EQ (cause->jobs, 1);

		emission = (EventEmission *)events->next;
		TEST_ALLOC_SIZE (emission, sizeof (EventEmission));
		TEST_EQ_STR (emission->event.name, "started");
		TEST_EQ_STR (emission->event.args[0], "test");
		TEST_EQ_P (emission->event.args[1], NULL);
		nih_list_free (&emission->event.entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);
	}


	/* Check that a job can move from running to stopping, by-passing
	 * pre-stop.  This should emit the stopping event, containing the
	 * failed information including the exit status, and block on it.
	 */
	TEST_FEATURE ("running to stopping");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;
		job->pid = 0;

		job->cause = cause;
		job->blocker = NULL;

		job->failed = TRUE;
		job->failed_state = JOB_RUNNING;
		job->exit_status = 1;

		job->instance = FALSE;
		job->service = FALSE;

		job_change_state (job, JOB_STOPPING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocker, (EventEmission *)events->next);

		emission = (EventEmission *)events->next;
		TEST_ALLOC_SIZE (emission, sizeof (EventEmission));
		TEST_EQ_STR (emission->event.name, "stopping");
		TEST_EQ_STR (emission->event.args[0], "test");
		TEST_EQ_STR (emission->event.args[1], "failed");
		TEST_EQ_STR (emission->event.args[2], "running");
		TEST_EQ_P (emission->event.args[3], NULL);
		TEST_EQ_STR (emission->event.env[0], "EXIT_STATUS=1");
		TEST_EQ_P (emission->event.env[1], NULL);
		nih_list_free (&emission->event.entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_state, JOB_RUNNING);
		TEST_EQ (job->exit_status, 1);
	}


	/* Check that a job killed by a signal can move from running to
	 * stopping, by-passing pre-stop.  This should emit the stopping
	 * event, containing the failed information including the exit
	 * signal, and block on it.
	 */
	TEST_FEATURE ("running to stopping for killed process");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;
		job->pid = 0;

		job->cause = cause;
		job->blocker = NULL;

		job->failed = TRUE;
		job->failed_state = JOB_RUNNING;
		job->exit_status = SIGSEGV | 0x80;

		job->instance = FALSE;
		job->service = FALSE;

		job_change_state (job, JOB_STOPPING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocker, (EventEmission *)events->next);

		emission = (EventEmission *)events->next;
		TEST_ALLOC_SIZE (emission, sizeof (EventEmission));
		TEST_EQ_STR (emission->event.name, "stopping");
		TEST_EQ_STR (emission->event.args[0], "test");
		TEST_EQ_STR (emission->event.args[1], "failed");
		TEST_EQ_STR (emission->event.args[2], "running");
		TEST_EQ_P (emission->event.args[3], NULL);
		TEST_EQ_STR (emission->event.env[0], "EXIT_SIGNAL=SEGV");
		TEST_EQ_P (emission->event.env[1], NULL);
		nih_list_free (&emission->event.entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_state, JOB_RUNNING);
		TEST_EQ (job->exit_status, SIGSEGV | 0x80);
	}


	/* Check that a job killed by an unknown signal can move from
	 * running to stopping, by-passing pre-stop.  This should emit
	 * the stopping event, containing the failed information
	 * including the exit signal number, and block on it.
	 */
	TEST_FEATURE ("running to stopping for unknown signal");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;
		job->pid = 0;

		job->cause = cause;
		job->blocker = NULL;

		job->failed = TRUE;
		job->failed_state = JOB_RUNNING;
		job->exit_status = 33 | 0x80;

		job->instance = FALSE;
		job->service = FALSE;

		job_change_state (job, JOB_STOPPING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocker, (EventEmission *)events->next);

		emission = (EventEmission *)events->next;
		TEST_ALLOC_SIZE (emission, sizeof (EventEmission));
		TEST_EQ_STR (emission->event.name, "stopping");
		TEST_EQ_STR (emission->event.args[0], "test");
		TEST_EQ_STR (emission->event.args[1], "failed");
		TEST_EQ_STR (emission->event.args[2], "running");
		TEST_EQ_P (emission->event.args[3], NULL);
		TEST_EQ_STR (emission->event.env[0], "EXIT_SIGNAL=33");
		TEST_EQ_P (emission->event.env[1], NULL);
		nih_list_free (&emission->event.entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_state, JOB_RUNNING);
		TEST_EQ (job->exit_status, 33 | 0x80);
	}


	/* Check that a job can move from pre-stop to stopping.  This
	 * should emit the stopping event, containing the failed information,
	 * and block on it.
	 */
	TEST_FEATURE ("pre-stop to stopping");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_PRE_STOP;
		job->pid = 0;

		job->cause = cause;
		job->blocker = NULL;

		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;

		job->instance = FALSE;
		job->service = FALSE;

		job_change_state (job, JOB_STOPPING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocker, (EventEmission *)events->next);

		emission = (EventEmission *)events->next;
		TEST_ALLOC_SIZE (emission, sizeof (EventEmission));
		TEST_EQ_STR (emission->event.name, "stopping");
		TEST_EQ_STR (emission->event.args[0], "test");
		TEST_EQ_STR (emission->event.args[1], "ok");
		TEST_EQ_P (emission->event.args[2], NULL);
		nih_list_free (&emission->event.entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);
	}


	/* Check that a job with an active process can move from stopping
	 * to killed, the process should be sent the TERM signal and a
	 * kill timer put in place to check up on it.
	 */
	TEST_FEATURE ("stopping to killed");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_STOPPING;
		TEST_CHILD (job->pid) {
			pause ();
		}
		pid = job->pid;

		job->cause = cause;
		job->blocker = NULL;

		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;

		job->instance = FALSE;
		job->service = FALSE;

		job_change_state (job, JOB_KILLED);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_KILLED);
		TEST_EQ (job->pid, pid);

		waitpid (job->pid, &status, 0);
		TEST_TRUE (WIFSIGNALED (status));
		TEST_EQ (WTERMSIG (status), SIGTERM);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocker, NULL);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);

		TEST_NE_P (job->kill_timer, NULL);

		nih_list_free (&job->kill_timer->entry);
		job->kill_timer = NULL;
	}


	/* Check that a job with no running process can move from stopping
	 * to killed, skipping over that state and ending up in post-stop
	 * instead.
	 */
	TEST_FEATURE ("stopping to killed without process");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_STOPPING;
		job->pid = 0;

		job->cause = cause;
		job->blocker = NULL;

		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;

		job->instance = FALSE;
		job->service = FALSE;

		job_change_state (job, JOB_KILLED);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_POST_STOP);
		TEST_NE (job->pid, 0);

		waitpid (job->pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/stop");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocker, NULL);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);

		TEST_EQ_P (job->kill_timer, NULL);
	}


	/* Check that a job with a stop process can move from killed
	 * to post-stop, and have the process run.
	 */
	TEST_FEATURE ("killed to post-stop");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_KILLED;
		job->pid = 0;

		job->cause = cause;
		job->blocker = NULL;

		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;

		job->instance = FALSE;
		job->service = FALSE;

		job_change_state (job, JOB_POST_STOP);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_POST_STOP);
		TEST_NE (job->pid, 0);

		waitpid (job->pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/stop");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocker, NULL);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);
	}


	/* Check that a job without a stop process can move from killed
	 * to post-stop, skipping over that state, and instead going all
	 * the way through to the waiting state.  Because we get there,
	 * we should get a stopped event emitted, and the cause forgotten.
	 */
	TEST_FEATURE ("killed to post-stop without process");
	tmp = job->stop_script;
	job->stop_script = NULL;

	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_KILLED;
		job->pid = 0;

		cause->jobs = 2;
		job->cause = cause;
		job->blocker = NULL;

		job->failed = TRUE;
		job->failed_state = JOB_RUNNING;
		job->exit_status = 1;

		job->instance = FALSE;
		job->service = FALSE;

		job_change_state (job, JOB_POST_STOP);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ_P (job->blocker, NULL);

		TEST_EQ (cause->jobs, 1);

		emission = (EventEmission *)events->next;
		TEST_ALLOC_SIZE (emission, sizeof (EventEmission));
		TEST_EQ_STR (emission->event.name, "stopped");
		TEST_EQ_STR (emission->event.args[0], "test");
		TEST_EQ_STR (emission->event.args[1], "failed");
		TEST_EQ_STR (emission->event.args[2], "running");
		TEST_EQ_P (emission->event.args[2], NULL);
		TEST_EQ_STR (emission->event.env[0], "EXIT_STATUS=1");
		TEST_EQ_P (emission->event.env[2], NULL);
		nih_list_free (&emission->event.entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_state, JOB_RUNNING);
		TEST_EQ (job->exit_status, 1);
	}

	job->stop_script = tmp;


	/* Check that a job can move from post-stop to waiting.  This
	 * should emit the stopped event and clear the cause, but not the
	 * failed information.
	 */
	TEST_FEATURE ("post-stop to waiting");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;
		job->pid = 0;

		job->cause = cause;
		job->blocker = NULL;

		job->failed = TRUE;
		job->failed_state = JOB_RUNNING;
		job->exit_status = 1;

		job->instance = FALSE;
		job->service = FALSE;

		job_change_state (job, JOB_WAITING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ_P (job->blocker, NULL);

		emission = (EventEmission *)events->next;
		TEST_ALLOC_SIZE (emission, sizeof (EventEmission));
		TEST_EQ_STR (emission->event.name, "stopped");
		TEST_EQ_STR (emission->event.args[0], "test");
		TEST_EQ_STR (emission->event.args[1], "failed");
		TEST_EQ_STR (emission->event.args[2], "running");
		TEST_EQ_P (emission->event.args[3], NULL);
		TEST_EQ_STR (emission->event.env[0], "EXIT_STATUS=1");
		TEST_EQ_P (emission->event.env[1], NULL);
		nih_list_free (&emission->event.entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_state, JOB_RUNNING);
		TEST_EQ (job->exit_status, 1);
	}


	/* Check that a job can move from post-stop to starting.  This
	 * should emit the starting event and block on it, as well as clear
	 * any failed state information; but not the cause.
	 */
	TEST_FEATURE ("post-stop to starting");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_POST_STOP;
		job->pid = 0;

		job->cause = cause;
		job->blocker = NULL;

		job->failed = TRUE;
		job->failed_state = JOB_RUNNING;
		job->exit_status = 1;

		job->instance = FALSE;
		job->service = FALSE;

		job_change_state (job, JOB_STARTING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STARTING);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocker, (EventEmission *)events->next);

		emission = (EventEmission *)events->next;
		TEST_ALLOC_SIZE (emission, sizeof (EventEmission));
		TEST_EQ_STR (emission->event.name, "starting");
		TEST_EQ_STR (emission->event.args[0], "test");
		TEST_EQ_P (emission->event.args[1], NULL);
		nih_list_free (&emission->event.entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);
	}


	/* Check that if a job tries to move from post-stop to starting too
	 * many times in a given period then it is caught, a message is
	 * output and the job sent back to stopped and waiting.  We get
	 * a stopped event, and the cause is cleared.
	 */
	TEST_FEATURE ("post-stop to starting too fast");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_POST_STOP;
		job->pid = 0;

		job->cause = cause;
		job->blocker = NULL;

		job->failed = TRUE;
		job->failed_state = JOB_RUNNING;
		job->exit_status = 1;

		job->instance = FALSE;
		job->service = FALSE;

		job->respawn_limit = 10;
		job->respawn_interval = 1000;
		job->respawn_time = time (NULL);
		job->respawn_count = 10;

		TEST_DIVERT_STDERR (output) {
			job_change_state (job, JOB_STARTING);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ_P (job->blocker, NULL);

		emission = (EventEmission *)events->next;
		TEST_ALLOC_SIZE (emission, sizeof (EventEmission));
		TEST_EQ_STR (emission->event.name, "stopped");
		TEST_EQ_STR (emission->event.args[0], "test");
		TEST_EQ_STR (emission->event.args[1], "failed");
		TEST_EQ_STR (emission->event.args[2], "running");
		TEST_EQ_P (emission->event.args[3], NULL);
		TEST_EQ_STR (emission->event.env[0], "EXIT_STATUS=1");
		TEST_EQ_P (emission->event.args[1], NULL);
		nih_list_free (&emission->event.entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output,
			      "test: test respawning too fast, stopped");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	job->respawn_limit = 0;
	job->respawn_interval = JOB_DEFAULT_RESPAWN_INTERVAL;
	job->respawn_time = 0;
	job->respawn_count = 0;


	fclose (output);
	rmdir (dirname);

	nih_list_free (&job->entry);
	nih_list_free (&cause->event.entry);
}

void
test_next_state (void)
{
	Job *job;

	TEST_FUNCTION ("job_next_state");
	job = job_new (NULL, "test");

	/* Check that the next state if we're stopping a waiting job is
	 * waiting.
	 */
	TEST_FEATURE ("with waiting job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;

	TEST_EQ (job_next_state (job), JOB_WAITING);


	/* Check that the next state if we're starting a waiting job is
	 * starting.
	 */
	TEST_FEATURE ("with waiting job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_WAITING;

	TEST_EQ (job_next_state (job), JOB_STARTING);


	/* Check that the next state if we're stopping a starting job is
	 * waiting.
	 */
	TEST_FEATURE ("with starting job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_STARTING;

	TEST_EQ (job_next_state (job), JOB_WAITING);


	/* Check that the next state if we're starting a starting job is
	 * pre-start.
	 */
	TEST_FEATURE ("with starting job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_STARTING;

	TEST_EQ (job_next_state (job), JOB_PRE_START);


	/* Check that the next state if we're stopping a pre-start job is
	 * stopping.
	 */
	TEST_FEATURE ("with pre-start job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_PRE_START;

	TEST_EQ (job_next_state (job), JOB_STOPPING);


	/* Check that the next state if we're starting a pre-start job is
	 * spawned.
	 */
	TEST_FEATURE ("with pre-start job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_PRE_START;

	TEST_EQ (job_next_state (job), JOB_SPAWNED);


	/* Check that the next state if we're stopping a spawned job is
	 * stopping.
	 */
	TEST_FEATURE ("with spawned job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_SPAWNED;

	TEST_EQ (job_next_state (job), JOB_STOPPING);


	/* Check that the next state if we're starting a spawned job is
	 * post-start.
	 */
	TEST_FEATURE ("with spawned job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_SPAWNED;

	TEST_EQ (job_next_state (job), JOB_POST_START);


	/* Check that the next state if we're stopping a post-start job is
	 * stopping.
	 */
	TEST_FEATURE ("with post-start job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_POST_START;

	TEST_EQ (job_next_state (job), JOB_STOPPING);


	/* Check that the next state if we're starting a post-start job is
	 * running.
	 */
	TEST_FEATURE ("with post-start job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_POST_START;

	TEST_EQ (job_next_state (job), JOB_RUNNING);


	/* Check that the next state if we're stopping a running job is
	 * pre-stop.  This is the "normal" stop process, as called from the
	 * goal change event.
	 */
	TEST_FEATURE ("with running job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_RUNNING;
	job->pid = 1;

	TEST_EQ (job_next_state (job), JOB_PRE_STOP);


	/* Check that the next state if we're stopping a running job that
	 * has no process is stopping.  This is the stop process if the
	 * process goes away on its own, as called from the child reaper.
	 */
	TEST_FEATURE ("with dead running job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_RUNNING;
	job->pid = 0;

	TEST_EQ (job_next_state (job), JOB_STOPPING);


	/* Check that the next state if we're starting a running job is
	 * stopping.  This assumes that the job has exited, but we didn't
	 * change the goal, so it should be respawned.
	 */
	TEST_FEATURE ("with running job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;

	TEST_EQ (job_next_state (job), JOB_STOPPING);


	/* Check that the next state if we're starting a pre-stop job is
	 * running.  This assumes that the pre-stop job decided that the
	 * job should not stop.
	 */
	TEST_FEATURE ("with pre-stop job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_PRE_STOP;

	TEST_EQ (job_next_state (job), JOB_RUNNING);


	/* Check that the next state if we're stopping a pre-stop job is
	 * stopping.
	 */
	TEST_FEATURE ("with pre-stop job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_PRE_STOP;

	TEST_EQ (job_next_state (job), JOB_STOPPING);


	/* Check that the next state if we're starting a stopping job is
	 * killed.  This is because we need to clean up before we can start
	 * again.
	 */
	TEST_FEATURE ("with stopping job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_STOPPING;

	TEST_EQ (job_next_state (job), JOB_KILLED);


	/* Check that the next state if we're stopping a stopping job is
	 * killed.
	 */
	TEST_FEATURE ("with stopping job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;

	TEST_EQ (job_next_state (job), JOB_KILLED);


	/* Check that the next state if we're starting a killed job is
	 * post-stop.  This is because we need to clean up before we can
	 * start again.
	 */
	TEST_FEATURE ("with killed job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_KILLED;

	TEST_EQ (job_next_state (job), JOB_POST_STOP);


	/* Check that the next state if we're stopping a killed job is
	 * post-stop.
	 */
	TEST_FEATURE ("with killed job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_KILLED;

	TEST_EQ (job_next_state (job), JOB_POST_STOP);


	/* Check that the next state if we're starting a post-stop job is
	 * starting.
	 */
	TEST_FEATURE ("with post-stop job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_POST_STOP;

	TEST_EQ (job_next_state (job), JOB_STARTING);


	/* Check that the next state if we're stopping a post-stop job is
	 * waiting.
	 */
	TEST_FEATURE ("with post-stop job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_POST_STOP;

	TEST_EQ (job_next_state (job), JOB_WAITING);


	nih_list_free (&job->entry);
}


void
test_run_command (void)
{
	Job         *job;
	FILE        *output;
	struct stat  statbuf;
	char         filename[PATH_MAX], buf[80];

	TEST_FUNCTION ("job_run_command");
	TEST_FILENAME (filename);

	/* Check that we can run a simple command, and have the process id
	 * and state filled in.  We should be able to wait for the pid to
	 * finish and see that it has been run as expected.
	 */
	TEST_FEATURE ("with simple command");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (NULL, "test");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
			job->command = nih_sprintf (job, "touch %s", filename);
		}

		job_run_command (job, job->command);

		TEST_NE (job->pid, 0);

		waitpid (job->pid, NULL, 0);
		TEST_EQ (stat (filename, &statbuf), 0);

		unlink (filename);
		nih_list_free (&job->entry);
	}


	/* Check that we can run a command that requires a shell to be
	 * intepreted correctly, a shell should automatically be used to
	 * make this work.  Check the contents of a file we'll create to
	 * check that a shell really was used.
	 */
	TEST_FEATURE ("with shell command");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (NULL, "test");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
			job->command = nih_sprintf (job, "echo $$ > %s",
						    filename);
		}

		job_run_command (job, job->command);

		TEST_NE (job->pid, 0);

		waitpid (job->pid, NULL, 0);
		TEST_EQ (stat (filename, &statbuf), 0);

		/* Filename should contain the pid */
		output = fopen (filename, "r");
		sprintf (buf, "%d\n", job->pid);
		TEST_FILE_EQ (output, buf);
		TEST_FILE_END (output);
		fclose (output);
		unlink (filename);

		nih_list_free (&job->entry);
	}
}

void
test_run_script (void)
{
	Job           *job;
	EventEmission *em;
	FILE          *output;
	char           filename[PATH_MAX], **args;
	int            status, first;

	TEST_FUNCTION ("job_run_script");
	TEST_FILENAME (filename);

	/* Check that we can run a small shell script, and that it's run
	 * by using the shell directly and passing the script in on the
	 * command-line.
	 */
	TEST_FEATURE ("with small script");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (NULL, "test");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
			job->script = nih_sprintf (job, ("exec > %s\n"
							 "echo $0\necho $@"),
						   filename);
		}

		job_run_script (job, job->script);

		TEST_NE (job->pid, 0);

		waitpid (job->pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		output = fopen (filename, "r");
		TEST_FILE_EQ (output, "/bin/sh\n");
		TEST_FILE_EQ (output, "\n");
		TEST_FILE_END (output);
		fclose (output);
		unlink (filename);

		nih_list_free (&job->entry);
	}


	/* Check that shell scripts are run with the -e option set, so that
	 * any failing command causes the entire script to fail.
	 */
	TEST_FEATURE ("with script that will fail");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (NULL, "test");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
			job->script = nih_sprintf (job, ("exec > %s\n"
							 "test -d %s\n"
							 "echo oops"),
						   filename, filename);
		}

		job_run_script (job, job->script);

		TEST_NE (job->pid, 0);

		waitpid (job->pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 1);

		output = fopen (filename, "r");
		TEST_FILE_END (output);
		fclose (output);
		unlink (filename);

		nih_list_free (&job->entry);
	}


	/* Check that a small shell script will have arguments from the
	 * cause passed to it, if one exists.
	 */
	TEST_FEATURE ("with small script and cause");
	args = nih_str_array_new (NULL);
	NIH_MUST (nih_str_array_add (&args, NULL, NULL, "foo"));
	NIH_MUST (nih_str_array_add (&args, NULL, NULL, "bar"));
	em = event_emit ("test", args, NULL);

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (NULL, "test");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
			job->script = nih_sprintf (job, ("exec > %s\n"
							 "echo $0\necho $@"),
						   filename);

			job->cause = em;
		}

		job_run_script (job, job->script);

		TEST_NE (job->pid, 0);

		waitpid (job->pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		output = fopen (filename, "r");
		TEST_FILE_EQ (output, "/bin/sh\n");
		TEST_FILE_EQ (output, "foo bar\n");
		TEST_FILE_END (output);
		fclose (output);
		unlink (filename);

		nih_list_free (&job->entry);
	}

	nih_list_free (&em->event.entry);


	/* Check that a particularly long script is instead invoked by
	 * using the /dev/fd feature, with the shell script fed to the
	 * child process by an NihIo structure.
	 */
	TEST_FEATURE ("with long script");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (NULL, "test");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
			job->script = nih_alloc (job, 4096);
			sprintf (job->script, "exec > %s\necho $0\necho $@\n",
				 filename);
			while (strlen (job->script) < 4000)
				strcat (job->script,
					"# this just bulks it out a bit");
		}

		job_run_script (job, job->script);

		TEST_NE (job->pid, 0);

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

		waitpid (job->pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		output = fopen (filename, "r");
		TEST_FILE_EQ_N (output, "/dev/fd/");
		TEST_FILE_EQ (output, "\n");
		TEST_FILE_END (output);
		fclose (output);
		unlink (filename);

		nih_list_free (&job->entry);
	}


	/* Check that a long shell script will have arguments from the
	 * cause passed to it, if one exists.
	 */
	TEST_FEATURE ("with long script and cause");
	args = nih_str_array_new (NULL);
	NIH_MUST (nih_str_array_add (&args, NULL, NULL, "foo"));
	NIH_MUST (nih_str_array_add (&args, NULL, NULL, "bar"));
	em = event_emit ("test", args, NULL);

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (NULL, "test");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
			job->script = nih_alloc (job, 4096);
			sprintf (job->script, "exec > %s\necho $0\necho $@\n",
				 filename);
			while (strlen (job->script) < 4000)
				strcat (job->script,
					"# this just bulks it out a bit");

			job->cause = em;
		}

		job_run_script (job, job->script);

		TEST_NE (job->pid, 0);

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

		waitpid (job->pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		output = fopen (filename, "r");
		TEST_FILE_EQ_N (output, "/dev/fd/");
		TEST_FILE_EQ (output, "foo bar\n");
		TEST_FILE_END (output);
		fclose (output);
		unlink (filename);

		nih_list_free (&job->entry);
	}

	nih_list_free (&em->event.entry);
}


void
test_kill_process (void)
{
	Job      *job;
	NihTimer *timer;
	pid_t     pid;
	int       status;

	TEST_FUNCTION ("job_kill_process");
	job = job_new (NULL, "test");
	job->kill_timeout = 1000;
	job->respawn_limit = 0;


	/* Check that an easily killed process goes away with just a single
	 * call to job_kill_process, having received the TERM signal.
	 * A kill timer should be set to handle the case where the child
	 * doesn't get reaped.
	 */
	TEST_FEATURE ("with easily killed process");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_KILLED;
		TEST_CHILD (job->pid) {
			pause ();
		}
		pid = job->pid;

		job_kill_process (job);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_KILLED);
		TEST_EQ (job->pid, pid);

		waitpid (job->pid, &status, 0);
		TEST_TRUE (WIFSIGNALED (status));
		TEST_EQ (WTERMSIG (status), SIGTERM);

		TEST_NE_P (job->kill_timer, NULL);
		TEST_ALLOC_SIZE (job->kill_timer, sizeof (NihTimer));
		TEST_ALLOC_PARENT (job->kill_timer, job);
		TEST_GE (job->kill_timer->due, time (NULL) + 950);
		TEST_LE (job->kill_timer->due, time (NULL) + 1000);

		nih_free (job->kill_timer);
		job->kill_timer = NULL;
	}


	/* Check that a process that's hard to kill doesn't go away, but
	 * that the kill timer sends the KILL signal and makes out that the
	 * job has in fact died.
	 */
	TEST_FEATURE ("with hard to kill process");
	TEST_ALLOC_FAIL {
		int wait_fd;

		job->goal = JOB_STOP;
		job->state = JOB_KILLED;
		TEST_CHILD_WAIT (job->pid, wait_fd) {
			struct sigaction act;

			act.sa_handler = SIG_IGN;
			act.sa_flags = 0;
			sigemptyset (&act.sa_mask);
			sigaction (SIGTERM, &act, NULL);

			TEST_CHILD_RELEASE (wait_fd);

			for (;;)
				pause ();
		}
		pid = job->pid;

		job_kill_process (job);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_KILLED);
		TEST_EQ (job->pid, pid);

		TEST_EQ (kill (job->pid, 0), 0);

		TEST_NE_P (job->kill_timer, NULL);
		TEST_ALLOC_SIZE (job->kill_timer, sizeof (NihTimer));
		TEST_ALLOC_PARENT (job->kill_timer, job);
		TEST_GE (job->kill_timer->due, time (NULL) + 950);
		TEST_LE (job->kill_timer->due, time (NULL) + 1000);

		/* Run the kill timer */
		timer = job->kill_timer;
		timer->callback (timer->data, timer);
		nih_free (timer);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ (job->pid, 0);

		waitpid (pid, &status, 0);
		TEST_TRUE (WIFSIGNALED (status));
		TEST_EQ (WTERMSIG (status), SIGKILL);

		TEST_EQ_P (job->kill_timer, NULL);
	}


	/* Check that if we kill an already dead process, the process is
	 * forgotten and the state transitioned immediately.
	 */
	TEST_FEATURE ("with already dead process");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_WAITING;
		TEST_CHILD (job->pid) {
			exit (0);
		}

		waitpid (job->pid, NULL, 0);

		job_kill_process (job);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->kill_timer, NULL);
	}


	nih_list_free (&job->entry);
	event_poll ();
}


static int was_called = 0;

static int
destructor_called (void *ptr)
{
	was_called++;
	return 0;
}

void
test_child_reaper (void)
{
	Job           *job;
	EventEmission *em;
	FILE          *output;
	int            exitcodes[2] = { 100, SIGINT | 0x80 };

	TEST_FUNCTION ("job_child_reaper");
	program_name = "test";
	output = tmpfile ();

	job = job_new (NULL, "test");
	job->command = "echo";

	em = event_emit ("foo", NULL, NULL);


	/* Check that the child reaper can be called with a pid that doesn't
	 * match the job, and that the job state doesn't change.
	 */
	TEST_FEATURE ("with unknown pid");
	TEST_ALLOC_FAIL {
		job_child_reaper (NULL, 999, FALSE, 0);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid, 1);
	}


	/* Check that we can reap the running task of the job, which should
	 * set the goal to stop and transition a state change into the
	 * stopping state.  This should not be considered a failure.
	 */
	TEST_FEATURE ("with running process");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid = 1;

		job->cause = em;
		em->failed = FALSE;

		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;

		job_child_reaper (NULL, 1, FALSE, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid, 0);

		waitpid (job->pid, NULL, 0);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->failed, FALSE);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);
	}


	/* Check that we can reap a running task of the job after it's been
	 * sent the TERM signal and a kill timer set.  The kill timer should
	 * be cancelled and freed, and since we killed it, the job should
	 * still not be considered failed.
	 */
	TEST_FEATURE ("with kill timer");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_KILLED;
		job->pid = 1;

		job->cause = em;
		em->failed = FALSE;

		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;

		TEST_ALLOC_SAFE {
			was_called = 0;
			job->kill_timer = (void *) nih_strdup (job, "test");
			nih_alloc_set_destructor (job->kill_timer,
						  destructor_called);
		}

		job_child_reaper (NULL, 1, FALSE, 0);

		TEST_TRUE (was_called);
		TEST_EQ_P (job->kill_timer, NULL);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->failed, FALSE);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);

		TEST_NE (job->pid, 1);

		waitpid (job->pid, NULL, 0);
	}


	/* Check that we can reap the pre-start process of the job, and if it
	 * terminates with a good error code, end up in the running state.
	 */
	TEST_FEATURE ("with pre-start process");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->pid = 1;

		job->cause = em;
		em->failed = FALSE;

		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;

		job_child_reaper (NULL, 1, FALSE, 0);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_GT (job->pid, 1);

		waitpid (job->pid, NULL, 0);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->failed, FALSE);
	}


	/* Check that we can reap a failing pre-start process of the job, which
	 * changes the goal to stop and transitions a state change in that
	 * direction to the stopping state.  An error should be emitted
	 * and the job and event should be marked as failed.
	 */
	TEST_FEATURE ("with failed pre-start process");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->pid = 1;

		job->cause = em;
		em->failed = FALSE;

		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, FALSE, 1);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->failed, TRUE);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_state, JOB_PRE_START);
		TEST_EQ (job->exit_status, 1);

		TEST_FILE_EQ (output, ("test: test process (1) terminated "
				       "with status 1\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}


	/* Check that we can reap a killed starting task, which should
	 * act as if it failed.  A different error should be output and
	 * the failed exit status should contain the signal and the high bit.
	 */
	TEST_FEATURE ("with killed pre-start process");
	em = event_emit ("foo", NULL, NULL);

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->pid = 1;

		job->cause = em;
		em->failed = FALSE;

		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, TRUE, SIGTERM);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->failed, TRUE);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_state, JOB_PRE_START);
		TEST_EQ (job->exit_status, SIGTERM | 0x80);

		TEST_FILE_EQ (output, ("test: test process (1) killed "
				       "by TERM signal\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}


	/* Check that we can catch the running task failing, and if the job
	 * is to be respawned, go into the stopping state but don't change
	 * the goal to stop.
	 *
	 * This should also emit a warning, but should not set the failed
	 * state since we're dealing with it.
	 */
	TEST_FEATURE ("with respawn of running process");
	job->respawn = TRUE;

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid = 1;

		job->cause = em;
		em->failed = FALSE;

		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, FALSE, 0);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_GT (job->pid, 1);

		waitpid (job->pid, NULL, 0);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output,
			      "test: test process ended, respawning\n");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	job->respawn = FALSE;


	/* Check that we can catch a running task exiting with a "normal"
	 * exit code, and even if it's marked respawn, set the goal to
	 * stop and transition into the stopping state.
	 */
	TEST_FEATURE ("with normal exit of running respawn process");
	job->respawn = TRUE;
	job->normalexit = exitcodes;
	job->normalexit_len = 1;

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid = 1;

		job->cause = em;
		em->failed = FALSE;

		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, FALSE, 100);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->failed, FALSE);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test process (1) terminated "
				       "with status 100\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	job->respawn = FALSE;
	job->normalexit = NULL;
	job->normalexit_len = 0;


	/* Check that a running task that fails with an exit status not
	 * listed in normalexit causes the job to be marked as failed.
	 */
	TEST_FEATURE ("with abnormal exit of running process");
	job->normalexit = exitcodes;
	job->normalexit_len = 2;

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid = 1;

		job->cause = em;
		em->failed = FALSE;

		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, FALSE, 99);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->failed, TRUE);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_state, JOB_RUNNING);
		TEST_EQ (job->exit_status, 99);

		TEST_FILE_EQ (output, ("test: test process (1) terminated "
				       "with status 99\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	job->normalexit = NULL;
	job->normalexit_len = 0;


	/* Check that a running task that fails doesn't mark the job or
	 * event as failed if the goal was already to stop the job (since
	 * it's probably failed because of the TERM or KILL signal).
	 */
	TEST_FEATURE ("with killed running process");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_KILLED;
		job->pid = 1;

		job->cause = em;
		em->failed = FALSE;

		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, TRUE, SIGTERM);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->failed, FALSE);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test process (1) killed "
				       "by TERM signal\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}


	/* Check that a running task that fails with an exit status
	 * listed in normalexit does not cause the job to be marked as
	 * failed, but instead just stops it normally.
	 */
	TEST_FEATURE ("with normal exit of running process");
	job->normalexit = exitcodes;
	job->normalexit_len = 2;

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid = 1;

		job->cause = em;
		em->failed = FALSE;

		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, FALSE, 100);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->failed, FALSE);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test process (1) terminated "
				       "with status 100\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	job->normalexit = NULL;
	job->normalexit_len = 0;


	/* Check that a running task that fails with an signal
	 * listed in normalexit does not cause the job to be marked as
	 * failed, but instead just stops it normally.
	 */
	TEST_FEATURE ("with normal signal killed running process");
	job->normalexit = exitcodes;
	job->normalexit_len = 2;

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid = 1;

		job->cause = em;
		em->failed = FALSE;

		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, TRUE, SIGINT);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->failed, FALSE);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test process (1) killed "
				       "by INT signal\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	job->normalexit = NULL;
	job->normalexit_len = 0;


	/* A running task exiting with the zero exit code is considered
	 * a normal termination if not marked respawn.
	 */
	TEST_FEATURE ("with running task and zero exit");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid = 1;

		job->cause = em;
		em->failed = FALSE;

		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;

		job_child_reaper (NULL, 1, FALSE, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->failed, FALSE);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);
	}


	/* Check that we can reap the post-stop process of the job, and end up
	 * in the waiting state.
	 */
	TEST_FEATURE ("with post-stop process");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;
		job->pid = 1;

		job->cause = em;
		em->failed = FALSE;

		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;

		job_child_reaper (NULL, 1, FALSE, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ (em->failed, FALSE);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);
	}


	/* Check that we can reap a failing post-stop process of the job, which
	 * should get marked as failed if the job hasn't been already.
	 */
	TEST_FEATURE ("with failed post-stop process");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;
		job->pid = 1;

		job->cause = em;
		em->failed = FALSE;

		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, FALSE, 1);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ (em->failed, TRUE);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_state, JOB_POST_STOP);
		TEST_EQ (job->exit_status, 1);

		TEST_FILE_EQ (output, ("test: test process (1) terminated "
				       "with status 1\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	/* Check that a failing stopping task doesn't overwrite the record
	 * of a failing earlier task.
	 */
	TEST_FEATURE ("with stopping task failure after failure");
	em = event_emit ("foo", NULL, NULL);

	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;
		job->pid = 1;

		job->cause = em;
		em->failed = TRUE;

		job->failed = TRUE;
		job->failed_state = JOB_RUNNING;
		job->exit_status = SIGSEGV | 0x80;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, FALSE, 1);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ (em->failed, TRUE);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_state, JOB_RUNNING);
		TEST_EQ (job->exit_status, SIGSEGV | 0x80);

		TEST_FILE_EQ (output, ("test: test process (1) terminated "
				       "with status 1\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}


	fclose (output);

	nih_list_free (&job->entry);

	event_poll ();
}


void
test_handle_event (void)
{
	Job            *job1, *job2;
	Event          *event;
	EventEmission  *em;
	int             status;

	TEST_FUNCTION ("job_handle_event");
	job1 = job_new (NULL, "foo");
	job1->command = "echo";
	job1->respawn_limit = 0;

	event = event_new (job1, "wibble");
	nih_list_add (&job1->start_events, &event->entry);

	job2 = job_new (NULL, "bar");
	job2->command = "echo";
	job2->respawn_limit = 0;

	event = event_new (job2, "wibble");
	nih_list_add (&job2->stop_events, &event->entry);


	/* Check that a non matching event has no effect on either job,
	 * and doesn't result in the emission being given any jobs.
	 */
	TEST_FEATURE ("with non-matching event");
	em = event_emit ("biscuit", NULL, NULL);

	TEST_ALLOC_FAIL {
		em->jobs = 0;

		job1->goal = JOB_STOP;
		job1->state = JOB_WAITING;
		job1->process_state = PROCESS_NONE;
		job1->pid = -1;
		job1->cause = NULL;

		job2->goal = JOB_START;
		job2->state = JOB_RUNNING;
		job2->process_state = PROCESS_ACTIVE;
		job2->cause = NULL;

		TEST_CHILD (job2->pid) {
			pause ();
		}

		job_handle_event (em);

		TEST_EQ (em->jobs, 0);

		TEST_EQ (job1->goal, JOB_STOP);
		TEST_EQ (job1->state, JOB_WAITING);
		TEST_EQ (job1->process_state, PROCESS_NONE);
		TEST_EQ_P (job1->cause, NULL);

		TEST_EQ (job2->goal, JOB_START);
		TEST_EQ (job2->state, JOB_RUNNING);
		TEST_EQ (job2->process_state, PROCESS_ACTIVE);
		TEST_EQ_P (job2->cause, NULL);

		kill (job2->pid, SIGTERM);
		waitpid (job2->pid, NULL, 0);
	}

	nih_list_free (&em->event.entry);


	/* Check that a matching event results in the jobs being started or
	 * stopped as appropriate.
	 */
	TEST_FEATURE ("with matching event");
	em = event_emit ("wibble", NULL, NULL);

	TEST_ALLOC_FAIL {
		em->jobs = 0;

		job1->goal = JOB_STOP;
		job1->state = JOB_WAITING;
		job1->process_state = PROCESS_NONE;
		job1->pid = -1;
		job1->cause = NULL;

		job2->goal = JOB_START;
		job2->state = JOB_RUNNING;
		job2->process_state = PROCESS_ACTIVE;
		job2->cause = NULL;

		TEST_CHILD (job2->pid) {
			pause ();
		}

		job_handle_event (em);

		TEST_EQ (em->jobs, 2);

		TEST_EQ (job1->goal, JOB_START);
		TEST_EQ (job1->state, JOB_RUNNING);
		TEST_EQ (job1->process_state, PROCESS_ACTIVE);
		TEST_EQ_P (job1->cause, em);

		TEST_NE (job1->pid, 0);
		waitpid (job1->pid, NULL, 0);

		TEST_EQ (job2->goal, JOB_STOP);
		TEST_EQ (job2->state, JOB_RUNNING);
		TEST_EQ (job2->process_state, PROCESS_KILLED);
		TEST_EQ_P (job2->cause, em);

		waitpid (job2->pid, &status, 0);
		TEST_TRUE (WIFSIGNALED (status));
		TEST_EQ (WTERMSIG (status), SIGTERM);
	}

	nih_list_free (&em->event.entry);
	event_poll ();

	nih_list_free (&job2->entry);
	nih_list_free (&job1->entry);
}

void
test_detect_stalled (void)
{
	Job           *job1, *job2;
	Event         *event;
	EventEmission *em;
	NihList       *list;

	TEST_FUNCTION ("job_detect_stalled");

	/* This is a naughty way of getting a pointer to the event queue
	 * list head...
	 */
	event_poll ();
	em = event_emit ("wibble", NULL, NULL);
	list = em->event.entry.prev;
	nih_list_free (&em->event.entry);

	job1 = job_new (NULL, "foo");
	job1->goal = JOB_STOP;
	job1->state = JOB_WAITING;
	job1->process_state = PROCESS_NONE;

	job2 = job_new (NULL, "bar");
	job2->goal = JOB_STOP;
	job2->state = JOB_WAITING;
	job2->process_state = PROCESS_NONE;


	/* Check that even if we detect the stalled state, we do nothing
	 * if there's no handled for it.
	 */
	TEST_FEATURE ("with stalled state and no handler");
	job_detect_stalled ();

	TEST_LIST_EMPTY (list);


	/* Check that we can detect the stalled state, when all jobs are
	 * stopped, which results in the stalled event being queued.
	 */
	TEST_FEATURE ("with stalled state");
	event = event_new (job1, "stalled");
	nih_list_add (&job1->start_events, &event->entry);
	job_detect_stalled ();

	event = (Event *)list->prev;
	TEST_EQ_STR (event->name, "stalled");
	nih_list_free (&event->entry);

	TEST_LIST_EMPTY (list);


	/* Check that we don't detect the stalled state if one of the jobs
	 * is waiting to be started.
	 */
	TEST_FEATURE ("with waiting job");
	job1->goal = JOB_START;
	job_detect_stalled ();

	TEST_LIST_EMPTY (list);


	/* Check that we don't detect the stalled state if one of the jobs
	 * is starting.
	 */
	TEST_FEATURE ("with starting job");
	job1->state = JOB_STARTING;
	job_detect_stalled ();

	TEST_LIST_EMPTY (list);


	/* Check that we don't detect the stalled state if one of the jobs
	 * is running.
	 */
	TEST_FEATURE ("with running job");
	job1->state = JOB_RUNNING;
	job1->process_state = PROCESS_ACTIVE;
	job_detect_stalled ();

	TEST_LIST_EMPTY (list);


	/* Check that we don't detect the stalled if one of the jobs is
	 * stopping.
	 */
	TEST_FEATURE ("with stopping job");
	job1->goal = JOB_STOP;
	job1->state = JOB_STOPPING;
	job1->process_state = PROCESS_NONE;
	job_detect_stalled ();

	TEST_LIST_EMPTY (list);


	nih_list_free (&job1->entry);
	nih_list_free (&job2->entry);
}


int
main (int   argc,
      char *argv[])
{
	test_new ();
	test_find_by_name ();
	test_find_by_pid ();
	test_change_goal ();
	test_change_state ();
	test_next_state ();
	test_run_command ();
	test_run_script ();
	test_kill_process ();
	test_child_reaper ();
	test_handle_event ();
	test_detect_stalled ();

	return 0;
}
