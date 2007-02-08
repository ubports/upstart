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
		TEST_LIST_EMPTY (&job->start_events);
		TEST_LIST_EMPTY (&job->stop_events);
		TEST_LIST_EMPTY (&job->emits);

		TEST_EQ_P (job->cause, NULL);

		TEST_EQ_STR (job->name, "test");
		TEST_ALLOC_PARENT (job->name, job);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ (job->process_state, PROCESS_NONE);

		TEST_EQ (job->kill_timeout, JOB_DEFAULT_KILL_TIMEOUT);
		TEST_EQ (job->pid_timeout, JOB_DEFAULT_PID_TIMEOUT);
		TEST_EQ (job->respawn_limit, JOB_DEFAULT_RESPAWN_LIMIT);
		TEST_EQ (job->respawn_interval, JOB_DEFAULT_RESPAWN_INTERVAL);

		TEST_EQ (job->console, CONSOLE_NONE);
		TEST_EQ (job->umask, JOB_DEFAULT_UMASK);

		for (i = 0; i < RLIMIT_NLIMITS; i++)
			TEST_EQ_P (job->limits[i], NULL);

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
	int            status;
	pid_t          pid;

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
		job->process_state = PROCESS_NONE;
		job->pid = 0;

		job_change_goal (job, JOB_START, NULL);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STARTING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

		TEST_NE (job->pid, 0);

		waitpid (job->pid, NULL, 0);
	}


	/* Check that an attempt to start a job that's in the process of
	 * stopping changes only the goal, and leaves the rest of the
	 * state transition up to the normal process.
	 */
	TEST_FEATURE ("with stopping job");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_STOPPING;
		job->process_state = PROCESS_ACTIVE;
		job->pid = 1;

		job_change_goal (job, JOB_START, NULL);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);
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
		job->process_state = PROCESS_ACTIVE;
		job->pid = 1;
		job->cause = em;
		em->progress = EVENT_HANDLING;
		em->jobs = 1;

		job_change_goal (job, JOB_START, NULL);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);
		TEST_EQ (job->pid, 1);

		TEST_EQ_P (job->cause, NULL);

		TEST_EQ (em->jobs, 0);
		TEST_EQ (em->progress, EVENT_FINISHED);
	}

	nih_list_free (&em->event.entry);
	job->cause = NULL;


	/* Check that start a job with a cause passed references that event
	 * and sets the cause in the job.
	 */
	TEST_FEATURE ("with new cause");
	em = event_emit ("foo", NULL, NULL);

	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_STOPPING;
		job->process_state = PROCESS_ACTIVE;
		job->pid = 1;
		job->cause = NULL;
		em->jobs = 0;

		job_change_goal (job, JOB_START, em);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);
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
		job->process_state = PROCESS_ACTIVE;
		job->pid = 1;

		job_change_goal (job, JOB_START, NULL);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);
		TEST_EQ (job->pid, 1);
	}


	/* Check that an attempt to stop a running job results in the goal
	 * being changed, the job killed and the state left to be changed by
	 * the child reaper.
	 */
	TEST_FEATURE ("with running job");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->process_state = PROCESS_ACTIVE;

		TEST_CHILD (job->pid) {
			pause ();
		}
		pid = job->pid;

		job_change_goal (job, JOB_STOP, NULL);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->process_state, PROCESS_KILLED);
		TEST_EQ (job->pid, pid);

		TEST_NE_P (job->kill_timer, NULL);
		TEST_ALLOC_SIZE (job->kill_timer, sizeof (NihTimer));
		TEST_ALLOC_PARENT (job->kill_timer, job);

		nih_free (job->kill_timer);
		job->kill_timer = NULL;

		waitpid (pid, &status, 0);
		TEST_TRUE (WIFSIGNALED (status));
		TEST_EQ (WTERMSIG (status), SIGTERM);
	}


	/* Check that an attempt to stop a starting job only results in the
	 * goal being changed, the starting process should not be killed and
	 * instead left to terminate normally.
	 */
	TEST_FEATURE ("with starting job");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_STARTING;
		job->process_state = PROCESS_ACTIVE;

		TEST_CHILD (job->pid) {
			pause ();
		}
		pid = job->pid;

		job_change_goal (job, JOB_STOP, NULL);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STARTING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);
		TEST_EQ (job->pid, pid);

		TEST_EQ_P (job->kill_timer, NULL);

		kill (pid, SIGTERM);
		waitpid (pid, NULL, 0);
	}


	/* Check that stopping a job with a cause event set unreferences the
	 * event and sets it to NULL.
	 */
	TEST_FEATURE ("with existing cause");
	em = event_emit ("foo", NULL, NULL);

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_STARTING;
		job->process_state = PROCESS_ACTIVE;
		job->cause = em;
		em->jobs = 1;
		em->progress = EVENT_HANDLING;

		TEST_CHILD (job->pid) {
			pause ();
		}
		pid = job->pid;

		job_change_goal (job, JOB_STOP, NULL);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STARTING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);
		TEST_EQ (job->pid, pid);
		TEST_EQ_P (job->cause, NULL);

		TEST_EQ (em->jobs, 0);
		TEST_EQ (em->progress, EVENT_FINISHED);

		TEST_EQ_P (job->kill_timer, NULL);

		kill (pid, SIGTERM);
		waitpid (pid, NULL, 0);
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
		job->process_state = PROCESS_ACTIVE;
		job->cause = NULL;
		em->jobs = 0;

		TEST_CHILD (job->pid) {
			pause ();
		}
		pid = job->pid;

		job_change_goal (job, JOB_STOP, em);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STARTING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);
		TEST_EQ (job->pid, pid);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->jobs, 1);

		TEST_EQ_P (job->kill_timer, NULL);

		kill (pid, SIGTERM);
		waitpid (pid, NULL, 0);
	}

	nih_list_free (&em->event.entry);
	job->cause = NULL;


	/* Check that an attempt to stop a waiting job does nothing. */
	TEST_FEATURE ("with waiting job");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_WAITING;
		job->process_state = PROCESS_NONE;
		job->pid = 0;

		job_change_goal (job, JOB_STOP, NULL);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ (job->process_state, PROCESS_NONE);
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
	Event         *event;
	EventEmission *em;
	NihList       *list;
	struct stat    statbuf;
	char           dirname[PATH_MAX], filename[PATH_MAX];
	int            i;

	TEST_FUNCTION ("job_change_state");
	program_name = "test";
	TEST_FILENAME (dirname);
	mkdir (dirname, 0700);

	/* This is a naughty way of getting a pointer to the event queue
	 * list head...  we keep hold of the emitted event, but remove it
	 * from the list (so it doesn't affect our "events emitted" check)
	 */
	event_poll ();
	em = event_emit ("wibble", NULL, NULL);
	list = em->event.entry.prev;
	nih_list_remove (&em->event.entry);

	job = job_new (NULL, "test");
	job->start_script = nih_sprintf (job, "touch %s/start", dirname);
	job->stop_script = nih_sprintf (job, "touch %s/stop", dirname);
	job->respawn_script = nih_sprintf (job, "touch %s/respawn", dirname);
	job->command = nih_sprintf (job, "touch %s/run", dirname);
	job->respawn_limit = 0;


	/* Check that a job can move from waiting to starting, and that if
	 * it has a start script, that is run and the job left in the starting
	 * state.  The start event should be emitted and any record of
	 * a failed state cleared.
	 */
	TEST_FEATURE ("waiting to starting with script");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_WAITING;
		job->process_state = PROCESS_NONE;

		job->failed = TRUE;
		job->failed_state = JOB_RUNNING;
		job->exit_status = 1;

		job_change_state (job, JOB_STARTING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STARTING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

		TEST_EQ (job->failed, FALSE);

		event = (Event *)list->prev;
		TEST_EQ_STR (event->name, "start");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_P (event->args[1], NULL);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (list);

		waitpid (job->pid, NULL, 0);
		sprintf (filename, "%s/start", dirname);
		TEST_EQ (stat (filename, &statbuf), 0);

		unlink (filename);
	}


	/* Check that a job can move directly from waiting to running if it
	 * has no start script, emitting both the start and started events;
	 * since the job is not a service, this should not clear the cause.
	 */
	TEST_FEATURE ("waiting to starting with no script");
	nih_free (job->start_script);
	job->start_script = NULL;

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_WAITING;
		job->process_state = PROCESS_NONE;
		job->failed = FALSE;

		job->cause = em;
		em->jobs = 1;

		job_change_state (job, JOB_STARTING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->jobs, 1);

		event = (Event *)list->prev;
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_P (event->args[1], NULL);
		nih_list_free (&event->entry);

		event = (Event *)list->prev;
		TEST_EQ_STR (event->name, "start");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_P (event->args[1], NULL);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (list);

		waitpid (job->pid, NULL, 0);
		sprintf (filename, "%s/run", dirname);
		TEST_EQ (stat (filename, &statbuf), 0);

		unlink (filename);
	}

	job->cause = NULL;


	/* Check that a job can move directly from waiting to running if it
	 * has no start script, emitting both the start and started events;
	 * and as the job is a service, the cause should be cleared too.
	 */
	TEST_FEATURE ("waiting to starting with no script for service");
	job->service = TRUE;

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_WAITING;
		job->process_state = PROCESS_NONE;
		job->failed = FALSE;

		job->cause = em;
		em->jobs = 1;

		job_change_state (job, JOB_STARTING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ (em->jobs, 0);

		event = (Event *)list->prev;
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_P (event->args[1], NULL);
		nih_list_free (&event->entry);

		event = (Event *)list->prev;
		TEST_EQ_STR (event->name, "start");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_P (event->args[1], NULL);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (list);

		waitpid (job->pid, NULL, 0);
		sprintf (filename, "%s/run", dirname);
		TEST_EQ (stat (filename, &statbuf), 0);

		unlink (filename);
	}

	job->start_script = nih_sprintf (job, "touch %s/start", dirname);
	job->service = FALSE;
	job->cause = NULL;


	/* Check that a job in the starting state moves into the running state,
	 * emitting the started event; since this is not a service, this
	 * should not clear the cause.
	 */
	TEST_FEATURE ("starting to running with command");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_STARTING;
		job->process_state = PROCESS_NONE;
		job->failed = FALSE;
		job->cause = em;
		em->jobs = 1;

		job_change_state (job, JOB_RUNNING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->jobs, 1);

		event = (Event *)list->prev;
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_P (event->args[1], NULL);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (list);

		waitpid (job->pid, NULL, 0);
		sprintf (filename, "%s/run", dirname);
		TEST_EQ (stat (filename, &statbuf), 0);

		unlink (filename);
	}

	job->cause = NULL;


	/* Check that a job in the starting state moves into the running state,
	 * emitting the started event; and because it is a service, the
	 * cause is cleared.
	 */
	TEST_FEATURE ("starting to running for service");
	job->service = TRUE;

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_STARTING;
		job->process_state = PROCESS_NONE;
		job->failed = FALSE;
		job->cause = em;
		em->jobs = 1;

		job_change_state (job, JOB_RUNNING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ (em->jobs, 0);

		event = (Event *)list->prev;
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_P (event->args[1], NULL);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (list);

		waitpid (job->pid, NULL, 0);
		sprintf (filename, "%s/run", dirname);
		TEST_EQ (stat (filename, &statbuf), 0);

		unlink (filename);
	}

	job->service = FALSE;
	job->cause = NULL;


	/* Check that a job in the starting state can move into the running
	 * state, and that a script instead of a command can be run.
	 * The started event should be emitted, and since this isn't a
	 * service, the cause should be left alone.
	 */
	TEST_FEATURE ("starting to running with script");
	job->script = job->command;
	job->command = NULL;

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_STARTING;
		job->process_state = PROCESS_NONE;
		job->failed = FALSE;
		job->cause = em;
		em->jobs = 1;

		job_change_state (job, JOB_RUNNING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->jobs, 1);

		event = (Event *)list->prev;
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_P (event->args[1], NULL);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (list);

		waitpid (job->pid, NULL, 0);
		sprintf (filename, "%s/run", dirname);
		TEST_EQ (stat (filename, &statbuf), 0);

		unlink (filename);
	}

	job->cause = NULL;


	/* Check that a job in the starting state moves into the running state,
	 * emitting the started event; even if there is no script or command
	 * for it.  It should not leave this state until forced.
	 */
	TEST_FEATURE ("starting to running with no command or script");
	nih_free (job->script);
	job->script = NULL;

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_STARTING;
		job->process_state = PROCESS_NONE;
		job->pid = 0;
		job->failed = FALSE;
		job->cause = em;
		em->jobs = 1;

		job_change_state (job, JOB_RUNNING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->process_state, PROCESS_NONE);
		TEST_EQ (job->pid, 0);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->jobs, 1);

		event = (Event *)list->prev;
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_P (event->args[1], NULL);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (list);
	}

	job->cause = NULL;
	job->command = nih_sprintf (job, "touch %s/run", dirname);


	/* Check that a job in the running state can move into the respawning
	 * state, resulting in the respawn script being run and the respawn
	 * event being emitted.
	 */
	TEST_FEATURE ("running to respawning with script");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->process_state = PROCESS_NONE;
		job->failed = FALSE;

		job_change_state (job, JOB_RESPAWNING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RESPAWNING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

		TEST_LIST_EMPTY (list);

		waitpid (job->pid, NULL, 0);
		sprintf (filename, "%s/respawn", dirname);
		TEST_EQ (stat (filename, &statbuf), 0);

		unlink (filename);
	}


	/* Check that a job in the running state can move straight through
	 * the respawning state back into the running state if there's no
	 * script; emitting the started events as it goes.
	 */
	TEST_FEATURE ("running to respawning without script");
	nih_free (job->respawn_script);
	job->respawn_script = NULL;

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->process_state = PROCESS_NONE;
		job->failed = FALSE;

		job_change_state (job, JOB_RESPAWNING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

		event = (Event *)list->prev;
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_P (event->args[1], NULL);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (list);

		waitpid (job->pid, NULL, 0);
		sprintf (filename, "%s/run", dirname);
		TEST_EQ (stat (filename, &statbuf), 0);

		unlink (filename);
	}


	/* Check that a job in the running state can move into the stopping
	 * state, running the script.  The stop event should be emitted
	 * with no indication of failure.
	 */
	TEST_FEATURE ("running to stopping with script");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;
		job->process_state = PROCESS_NONE;
		job->failed = FALSE;

		job_change_state (job, JOB_STOPPING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

		event = (Event *)list->prev;
		TEST_EQ_STR (event->name, "stop");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_STR (event->args[1], "ok");
		TEST_EQ_P (event->args[2], NULL);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (list);

		waitpid (job->pid, NULL, 0);
		sprintf (filename, "%s/stop", dirname);
		TEST_EQ (stat (filename, &statbuf), 0);

		unlink (filename);
	}


	/* Check that a job in the running state can move into the stopping
	 * state, running the script.  The stop event should be emitted
	 * with an indication of what failed.
	 */
	TEST_FEATURE ("running to stopping with script and failed state");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;
		job->process_state = PROCESS_NONE;
		job->failed = TRUE;
		job->failed_state = JOB_START;
		job->exit_status = SIGSEGV | 0x80;

		job_change_state (job, JOB_STOPPING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

		event = (Event *)list->prev;
		TEST_EQ_STR (event->name, "stop");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_STR (event->args[1], "failed");
		TEST_EQ_STR (event->args[2], "start");
		TEST_EQ_P (event->args[3], NULL);
		TEST_EQ_STR (event->env[0], "EXIT_SIGNAL=SEGV");
		TEST_EQ_P (event->env[1], NULL);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (list);

		waitpid (job->pid, NULL, 0);
		sprintf (filename, "%s/stop", dirname);
		TEST_EQ (stat (filename, &statbuf), 0);

		unlink (filename);
	}


	/* Check that a job in the running state can move directly into the
	 * waiting state, emitting the stop and stopped events as it goes,
	 * if there's no script.
	 */
	TEST_FEATURE ("running to stopping without script");
	nih_free (job->stop_script);
	job->stop_script = NULL;

	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;
		job->process_state = PROCESS_NONE;
		job->failed = FALSE;

		job_change_state (job, JOB_STOPPING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ (job->process_state, PROCESS_NONE);

		event = (Event *)list->prev;
		TEST_EQ_STR (event->name, "stopped");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_STR (event->args[1], "ok");
		TEST_EQ_P (event->args[2], NULL);
		nih_list_free (&event->entry);

		event = (Event *)list->prev;
		TEST_EQ_STR (event->name, "stop");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_STR (event->args[1], "ok");
		TEST_EQ_P (event->args[2], NULL);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (list);
	}


	/* Check that a job in the running state can move directly into the
	 * waiting state, emitting the stop and stopped events as it goes,
	 * if there's no script.
	 */
	TEST_FEATURE ("running to stopping without script and failure");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;
		job->process_state = PROCESS_NONE;
		job->failed = TRUE;
		job->failed_state = JOB_RUNNING;
		job->exit_status = 1;

		job_change_state (job, JOB_STOPPING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ (job->process_state, PROCESS_NONE);

		event = (Event *)list->prev;
		TEST_EQ_STR (event->name, "stopped");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_STR (event->args[1], "failed");
		TEST_EQ_STR (event->args[2], "main");
		TEST_EQ_P (event->args[3], NULL);
		TEST_EQ_STR (event->env[0], "EXIT_STATUS=1");
		TEST_EQ_P (event->env[1], NULL);
		nih_list_free (&event->entry);

		event = (Event *)list->prev;
		TEST_EQ_STR (event->name, "stop");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_STR (event->args[1], "failed");
		TEST_EQ_STR (event->args[2], "main");
		TEST_EQ_P (event->args[3], NULL);
		TEST_EQ_STR (event->env[0], "EXIT_STATUS=1");
		TEST_EQ_P (event->env[1], NULL);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (list);
	}

	job->stop_script = nih_sprintf (job, "touch %s/stop", dirname);


	/* Check that a job in the stopping state can move into the waiting
	 * state, emitting the stopped event as it goes and clearing the
	 * cause.
	 */
	TEST_FEATURE ("stopping to waiting");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_STOPPING;
		job->process_state = PROCESS_NONE;
		job->failed = FALSE;
		job->cause = em;
		em->jobs = 1;

		job_change_state (job, JOB_WAITING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ (job->process_state, PROCESS_NONE);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ (em->jobs, 0);

		event = (Event *)list->prev;
		TEST_EQ_STR (event->name, "stopped");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_STR (event->args[1], "ok");
		TEST_EQ_P (event->args[2], NULL);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (list);
	}

	job->cause = NULL;


	/* Check that a job in the stopping state can move into the waiting
	 * state, emitting the stopped event as it goes; which should include
	 * failed state information.
	 */
	TEST_FEATURE ("stopping to waiting with failed state");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_STOPPING;
		job->process_state = PROCESS_NONE;
		job->failed = TRUE;
		job->failed_state = JOB_STOPPING;
		job->exit_status = 32 | 0x80;

		job_change_state (job, JOB_WAITING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ (job->process_state, PROCESS_NONE);

		event = (Event *)list->prev;
		TEST_EQ_STR (event->name, "stopped");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_STR (event->args[1], "failed");
		TEST_EQ_STR (event->args[2], "stop");
		TEST_EQ_P (event->args[3], NULL);
		TEST_EQ_STR (event->env[0], "EXIT_SIGNAL=32");
		TEST_EQ_P (event->env[1], NULL);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (list);
	}


	/* Check that a job in the stopping state can move round into the
	 * starting state if the goal is JOB_START, emitting only the
	 * start event.
	 */
	TEST_FEATURE ("stopping to starting");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_STOPPING;
		job->process_state = PROCESS_NONE;
		job->failed = FALSE;

		job_change_state (job, JOB_STARTING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STARTING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

		event = (Event *)list->prev;
		TEST_EQ_STR (event->name, "start");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_P (event->args[1], NULL);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (list);

		waitpid (job->pid, NULL, 0);
		sprintf (filename, "%s/start", dirname);
		TEST_EQ (stat (filename, &statbuf), 0);

		unlink (filename);
	}


	/* Check that a job that tries to enter the running state from the
	 * starting state too fast results in it being stopped.  An error
	 * message should be emitted; but the cause shouldn't be
	 * cleared since this is a caught failure.
	 */
	TEST_FEATURE ("starting to running too fast");
	job->failed = FALSE;
	job->respawn_count = 0;
	job->respawn_time = 0;
	job->respawn_limit = 10;
	job->respawn_interval = 100;

	job->cause = em;
	em->jobs = 1;

	output = tmpfile ();
	TEST_DIVERT_STDERR (output) {
		for (i = 0; i < 11; i++) {
			job->goal = JOB_START;
			job->state = JOB_STARTING;
			job->process_state = PROCESS_NONE;

			job_change_state (job, JOB_RUNNING);

			if (job->goal == JOB_START)
				waitpid (job->pid, NULL, 0);
		}
	}
	rewind (output);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_STOPPING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	TEST_EQ_P (job->cause, em);
	TEST_EQ (em->jobs, 1);

	waitpid (job->pid, NULL, 0);
	sprintf (filename, "%s/stop", dirname);
	TEST_EQ (stat (filename, &statbuf), 0);

	unlink (filename);

	sprintf (filename, "%s/run", dirname);
	unlink (filename);

	TEST_FILE_EQ (output, "test: test respawning too fast, stopped\n");
	TEST_FILE_RESET (output);

	job->cause = NULL;
	event_poll ();


	/* Check that a job entering the respawning state from the running
	 * state too fast results in the job being stopped.  An error
	 * message should be emitted.
	 */
	TEST_FEATURE ("running to respawning too fast");
	job->failed = FALSE;
	job->respawn_count = 0;
	job->respawn_time = 0;
	job->respawn_limit = 10;
	job->respawn_interval = 100;

	TEST_DIVERT_STDERR (output) {
		for (i = 0; i < 11; i++) {
			job->goal = JOB_START;
			job->state = JOB_RUNNING;
			job->process_state = PROCESS_NONE;
			job_change_state (job, JOB_RESPAWNING);
		}
	}
	rewind (output);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_STOPPING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);

	waitpid (job->pid, NULL, 0);
	sprintf (filename, "%s/stop", dirname);
	TEST_EQ (stat (filename, &statbuf), 0);

	unlink (filename);

	sprintf (filename, "%s/run", dirname);
	unlink (filename);

	TEST_FILE_EQ (output, "test: test respawning too fast, stopped\n");

	event_poll ();


	fclose (output);
	rmdir (dirname);

	nih_list_free (&job->entry);
	nih_list_free (&em->event.entry);
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
	 * stopping.
	 */
	TEST_FEATURE ("with starting job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_STARTING;

	TEST_EQ (job_next_state (job), JOB_STOPPING);


	/* Check that the next state if we're starting a starting job is
	 * running.
	 */
	TEST_FEATURE ("with starting job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_STARTING;

	TEST_EQ (job_next_state (job), JOB_RUNNING);


	/* Check that the next state if we're stopping a running job is
	 * stopping.
	 */
	TEST_FEATURE ("with running job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_RUNNING;

	TEST_EQ (job_next_state (job), JOB_STOPPING);


	/* Check that the next state if we're starting a running job is
	 * respawning.
	 */
	TEST_FEATURE ("with running job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;

	TEST_EQ (job_next_state (job), JOB_RESPAWNING);


	/* Check that the next state if we're stopping a stopping job is
	 * waiting.
	 */
	TEST_FEATURE ("with stopping job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;

	TEST_EQ (job_next_state (job), JOB_WAITING);


	/* Check that the next state if we're starting a stopping job is
	 * starting.
	 */
	TEST_FEATURE ("with stopping job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_STOPPING;

	TEST_EQ (job_next_state (job), JOB_STARTING);


	/* Check that the next state if we're stopping a respawning job is
	 * stopping.
	 */
	TEST_FEATURE ("with respawning job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_RESPAWNING;

	TEST_EQ (job_next_state (job), JOB_STOPPING);


	/* Check that the next state if we're starting a respawning job is
	 * running.
	 */
	TEST_FEATURE ("with respawning job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_RESPAWNING;

	TEST_EQ (job_next_state (job), JOB_RUNNING);


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
			job->state = JOB_RUNNING;
			job->command = nih_sprintf (job, "touch %s", filename);
		}

		job_run_command (job, job->command);

		TEST_NE (job->pid, 0);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

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
			job->state = JOB_RUNNING;
			job->command = nih_sprintf (job, "echo $$ > %s",
						    filename);
		}

		job_run_command (job, job->command);

		TEST_NE (job->pid, 0);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

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
			job->state = JOB_RUNNING;
			job->script = nih_sprintf (job, ("exec > %s\n"
							 "echo $0\necho $@"),
						   filename);
		}

		job_run_script (job, job->script);

		TEST_NE (job->pid, 0);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

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
			job->state = JOB_RUNNING;
			job->script = nih_sprintf (job, ("exec > %s\n"
							 "test -d %s\n"
							 "echo oops"),
						   filename, filename);
		}

		job_run_script (job, job->script);

		TEST_NE (job->pid, 0);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

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
			job->state = JOB_RUNNING;
			job->script = nih_sprintf (job, ("exec > %s\n"
							 "echo $0\necho $@"),
						   filename);

			job->cause = em;
		}

		job_run_script (job, job->script);

		TEST_NE (job->pid, 0);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

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
			job->state = JOB_RUNNING;
			job->script = nih_alloc (job, 4096);
			sprintf (job->script, "exec > %s\necho $0\necho $@\n",
				 filename);
			while (strlen (job->script) < 4000)
				strcat (job->script,
					"# this just bulks it out a bit");
		}

		job_run_script (job, job->script);

		TEST_NE (job->pid, 0);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

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
			job->state = JOB_RUNNING;
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
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

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
	Job         *job;
	NihTimer    *timer;
	pid_t        pid;
	char         filename[PATH_MAX];
	struct stat  statbuf;
	int          status;

	TEST_FUNCTION ("job_kill_process");
	job = job_new (NULL, "test");
	job->goal = JOB_STOP;
	job->kill_timeout = 1000;
	job->respawn_limit = 0;


	/* Check that an easily killed process goes away with just a single
	 * call to job_kill_process, having received the TERM signal.  The
	 * process state should be changed to KILLED and a kill timer set
	 * to handle it if it doesn't get reaped.
	 */
	TEST_FEATURE ("with easily killed process");
	TEST_ALLOC_FAIL {
		job->state = JOB_RUNNING;
		job->process_state = PROCESS_ACTIVE;

		TEST_CHILD (job->pid) {
			pause ();
		}
		pid = job->pid;

		job_kill_process (job);
		waitpid (pid, &status, 0);

		TEST_TRUE (WIFSIGNALED (status));
		TEST_EQ (WTERMSIG (status), SIGTERM);

		TEST_EQ (job->pid, pid);
		TEST_EQ (job->process_state, PROCESS_KILLED);

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

		job->state = JOB_RUNNING;
		job->process_state = PROCESS_ACTIVE;
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

		TEST_EQ (kill (pid, 0), 0);

		TEST_EQ (job->pid, pid);
		TEST_EQ (job->process_state, PROCESS_KILLED);

		TEST_NE_P (job->kill_timer, NULL);
		TEST_ALLOC_SIZE (job->kill_timer, sizeof (NihTimer));
		TEST_ALLOC_PARENT (job->kill_timer, job);
		TEST_GE (job->kill_timer->due, time (NULL) + 950);
		TEST_LE (job->kill_timer->due, time (NULL) + 1000);

		/* Run the kill timer */
		timer = job->kill_timer;
		timer->callback (timer->data, timer);
		nih_free (timer);

		waitpid (pid, &status, 0);

		TEST_TRUE (WIFSIGNALED (status));
		TEST_EQ (WTERMSIG (status), SIGKILL);

		TEST_EQ (job->pid, 0);
		TEST_EQ (job->process_state, PROCESS_NONE);

		TEST_EQ_P (job->kill_timer, NULL);

		TEST_EQ (job->state, JOB_WAITING);
	}


	/* Check that a process that's hard to kill doesn't go away, but
	 * that the kill timer sends the KILL signal and makes out that the
	 * job has in fact died.  Also make sure it triggers a state
	 * transition by using a stop sdcript and checking that it has run.
	 */
	TEST_FEATURE ("with hard to kill process and stop script");
	TEST_FILENAME (filename);
	job->stop_script = nih_sprintf (job, "touch %s", filename);

	TEST_ALLOC_FAIL {
		job->state = JOB_RUNNING;
		job->process_state = PROCESS_ACTIVE;

		TEST_CHILD (job->pid) {
			struct sigaction act;

			act.sa_handler = SIG_IGN;
			act.sa_flags = 0;
			sigemptyset (&act.sa_mask);
			sigaction (SIGTERM, &act, NULL);

			for (;;)
				pause ();
		}
		pid = job->pid;

		job_kill_process (job);

		TEST_EQ (kill (pid, 0), 0);

		TEST_EQ (job->pid, pid);
		TEST_EQ (job->process_state, PROCESS_KILLED);

		TEST_NE_P (job->kill_timer, NULL);
		TEST_ALLOC_SIZE (job->kill_timer, sizeof (NihTimer));
		TEST_ALLOC_PARENT (job->kill_timer, job);
		TEST_GE (job->kill_timer->due, time (NULL) + 950);
		TEST_LE (job->kill_timer->due, time (NULL) + 1000);

		/* Run the kill timer */
		timer = job->kill_timer;
		timer->callback (timer->data, timer);
		nih_free (timer);

		TEST_EQ_P (job->kill_timer, NULL);

		waitpid (pid, &status, 0);

		TEST_TRUE (WIFSIGNALED (status));
		TEST_EQ (WTERMSIG (status), SIGKILL);

		TEST_NE (job->pid, 0);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

		waitpid (job->pid, NULL, 0);

		TEST_EQ (stat (filename, &statbuf), 0);

		unlink (filename);
	}

	nih_free (job->stop_script);
	job->stop_script = NULL;


	/* Check that if we kill an already dead process, the process is
	 * forgotten and the state transitioned immediately.
	 */
	TEST_FEATURE ("with already dead process");
	TEST_ALLOC_FAIL {
		job->state = JOB_RUNNING;
		job->process_state = PROCESS_ACTIVE;
		TEST_CHILD (job->pid) {
			exit (0);
		}
		waitpid (job->pid, NULL, 0);

		job_kill_process (job);

		TEST_EQ (job->pid, 0);
		TEST_EQ (job->process_state, PROCESS_NONE);
		TEST_EQ (job->state, JOB_WAITING);

		TEST_EQ_P (job->kill_timer, NULL);
	}


	nih_list_free (&job->entry);
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

	job = job_new (NULL, "test");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;
	job->pid = 1;
	job->command = "echo";
	job->stop_script = "echo";
	job->respawn_script = "echo";
	job->respawn_limit = 0;


	/* Check that the child reaper can be called with a pid that doesn't
	 * match the job, and that the job state doesn't change.
	 */
	TEST_FEATURE ("with unknown pid");
	TEST_ALLOC_FAIL {
		job_child_reaper (NULL, 999, FALSE, 0);

		TEST_EQ (job->state, JOB_RUNNING);
	}


	/* Check that we can reap the running task of the job, which should
	 * set the goal to stop and transition a state change into the
	 * stopping state.  This should not alter the cause.
	 */
	TEST_FEATURE ("with running task");
	em = event_emit ("foo", NULL, NULL);

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->process_state = PROCESS_ACTIVE;
		job->pid = 1;
		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;
		job->cause = em;
		em->failed = FALSE;

		job_child_reaper (NULL, 1, FALSE, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->failed, FALSE);

		TEST_NE (job->pid, 1);

		waitpid (job->pid, NULL, 0);
	}

	nih_list_free (&em->event.entry);
	job->cause = NULL;


	/* Check that we can reap a running task of the job after it's been
	 * sent the TERM signal and a kill timer set.  The kill timer should
	 * be cancelled and freed.
	 */
	TEST_FEATURE ("with kill timer");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->process_state = PROCESS_ACTIVE;
		job->pid = 1;
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
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);

		TEST_NE (job->pid, 1);

		waitpid (job->pid, NULL, 0);
	}


	/* Check that we can reap the starting task of the job, and if it
	 * terminates with a good error code, end up in the running state.
	 */
	TEST_FEATURE ("with starting task");
	em = event_emit ("foo", NULL, NULL);

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_STARTING;
		job->process_state = PROCESS_ACTIVE;
		job->pid = 1;
		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;
		job->cause = em;
		em->failed = FALSE;

		job_child_reaper (NULL, 1, FALSE, 0);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->failed, FALSE);

		TEST_NE (job->pid, 1);

		waitpid (job->pid, NULL, 0);
	}

	nih_list_free (&em->event.entry);
	job->cause = NULL;


	/* Check that we can reap a failing starting task of the job, which
	 * changes the goal to stop and transitions a state change in that
	 * direction to the stopping state.  An error should be emitted
	 * and the job and event should be marked as failed.
	 */
	TEST_FEATURE ("with starting task failure");
	output = tmpfile ();
	em = event_emit ("foo", NULL, NULL);

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_STARTING;
		job->process_state = PROCESS_ACTIVE;
		job->pid = 1;
		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;
		job->cause = em;
		em->failed = FALSE;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, FALSE, 1);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_state, JOB_STARTING);
		TEST_EQ (job->exit_status, 1);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->failed, TRUE);

		TEST_NE (job->pid, 1);

		waitpid (job->pid, NULL, 0);

		TEST_FILE_EQ (output, ("test: test process (1) terminated "
				       "with status 1\n"));
		TEST_FILE_END (output);

		TEST_FILE_RESET (output);
	}

	nih_list_free (&em->event.entry);
	job->cause = NULL;


	/* Check that we can reap a killed starting task, which should
	 * act as if it failed.  A different error should be output and
	 * the failed exit status should contain the signal and the high bit.
	 */
	TEST_FEATURE ("with starting task kill");
	em = event_emit ("foo", NULL, NULL);

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_STARTING;
		job->process_state = PROCESS_ACTIVE;
		job->pid = 1;
		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;
		job->cause = em;
		em->failed = FALSE;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, TRUE, SIGTERM);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_state, JOB_STARTING);
		TEST_EQ (job->exit_status, SIGTERM | 0x80);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->failed, TRUE);

		TEST_NE (job->pid, 1);

		waitpid (job->pid, NULL, 0);

		TEST_FILE_EQ (output, ("test: test process (1) killed "
				       "by TERM signal\n"));
		TEST_FILE_END (output);

		TEST_FILE_RESET (output);
	}

	nih_list_free (&em->event.entry);
	job->cause = NULL;


	/* Check that we can catch the running task failing, and if the job
	 * is to be respawned, go into the respawning state instead of
	 * stopping the job.  This should also emit a warning, but should
	 * not set the failed state since we're dealing with it.
	 */
	TEST_FEATURE ("with running task to respawn");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->process_state = PROCESS_ACTIVE;
		job->pid = 1;
		job->respawn = TRUE;
		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, FALSE, 0);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RESPAWNING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

		TEST_NE (job->pid, 1);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);

		waitpid (job->pid, NULL, 0);

		TEST_FILE_EQ (output,
			      "test: test process ended, respawning\n");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}


	/* Check that we can catch a running task exiting with a "normal"
	 * exit code, and even if it's marked respawn, setting the goal to
	 * stop and transitioning into the stopping state.
	 */
	TEST_FEATURE ("with running task and normal exit");
	em = event_emit ("foo", NULL, NULL);

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->process_state = PROCESS_ACTIVE;
		job->pid = 1;
		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;
		job->cause = em;
		em->failed = FALSE;
		job->respawn = TRUE;
		job->normalexit = exitcodes;
		job->normalexit_len = 1;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, FALSE, 100);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->failed, FALSE);

		TEST_NE (job->pid, 1);

		waitpid (job->pid, NULL, 0);

		TEST_FILE_EQ (output, ("test: test process (1) terminated "
				       "with status 100\n"));
		TEST_FILE_END (output);

		TEST_FILE_RESET (output);
	}

	nih_list_free (&em->event.entry);
	job->cause = NULL;


	/* Check that a running task that fails with an exit status not
	 * listed in normalexit causes the job to be marked as failed.
	 */
	TEST_FEATURE ("with running task and abnormal exit");
	em = event_emit ("foo", NULL, NULL);

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->process_state = PROCESS_ACTIVE;
		job->pid = 1;
		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;
		job->cause = em;
		em->failed = FALSE;
		job->respawn = FALSE;
		job->normalexit = exitcodes;
		job->normalexit_len = 2;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, FALSE, 99);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_state, JOB_RUNNING);
		TEST_EQ (job->exit_status, 99);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->failed, TRUE);

		TEST_NE (job->pid, 1);

		waitpid (job->pid, NULL, 0);

		TEST_FILE_EQ (output, ("test: test process (1) terminated "
				       "with status 99\n"));
		TEST_FILE_END (output);

		TEST_FILE_RESET (output);
	}

	nih_list_free (&em->event.entry);
	job->cause = NULL;


	/* Check that a running task that fails doesn't mark the job or
	 * event as failed if the goal was already to stop the job (since
	 * it's probably failed because of the TERM or KILL signal).
	 */
	TEST_FEATURE ("with running task being killed");
	em = event_emit ("foo", NULL, NULL);

	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;
		job->process_state = PROCESS_KILLED;
		job->pid = 1;
		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;
		job->cause = em;
		em->failed = FALSE;
		job->respawn = FALSE;
		job->normalexit = exitcodes;
		job->normalexit_len = 2;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, TRUE, SIGTERM);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->failed, FALSE);

		TEST_NE (job->pid, 1);

		waitpid (job->pid, NULL, 0);

		TEST_FILE_EQ (output, ("test: test process (1) killed "
				       "by TERM signal\n"));
		TEST_FILE_END (output);

		TEST_FILE_RESET (output);
	}

	nih_list_free (&em->event.entry);
	job->cause = NULL;


	/* Check that a running task that fails with an exit status
	 * listed in normalexit does not cause the job to be marked as
	 * failed, but instead just stops it normally.
	 */
	TEST_FEATURE ("with running task and normal exit");
	em = event_emit ("foo", NULL, NULL);

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->process_state = PROCESS_ACTIVE;
		job->pid = 1;
		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;
		job->cause = em;
		em->failed = FALSE;
		job->respawn = FALSE;
		job->normalexit = exitcodes;
		job->normalexit_len = 2;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, FALSE, 100);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->failed, FALSE);

		TEST_NE (job->pid, 1);

		waitpid (job->pid, NULL, 0);

		TEST_FILE_EQ (output, ("test: test process (1) terminated "
				       "with status 100\n"));
		TEST_FILE_END (output);

		TEST_FILE_RESET (output);
	}

	nih_list_free (&em->event.entry);
	job->cause = NULL;


	/* Check that a running task that fails with an signal
	 * listed in normalexit does not cause the job to be marked as
	 * failed, but instead just stops it normally.
	 */
	TEST_FEATURE ("with running task and normal signal");
	em = event_emit ("foo", NULL, NULL);

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->process_state = PROCESS_ACTIVE;
		job->pid = 1;
		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;
		job->cause = em;
		em->failed = FALSE;
		job->respawn = FALSE;
		job->normalexit = exitcodes;
		job->normalexit_len = 2;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, TRUE, SIGINT);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->failed, FALSE);

		TEST_NE (job->pid, 1);

		waitpid (job->pid, NULL, 0);

		TEST_FILE_EQ (output, ("test: test process (1) killed "
				       "by INT signal\n"));
		TEST_FILE_END (output);

		TEST_FILE_RESET (output);
	}

	nih_list_free (&em->event.entry);
	job->cause = NULL;


	/* A running task exiting with the zero exit code is considered
	 * a normal termination if not marked respawn.
	 */
	TEST_FEATURE ("with running task and zero exit");
	em = event_emit ("foo", NULL, NULL);

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->process_state = PROCESS_ACTIVE;
		job->pid = 1;
		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;
		job->cause = em;
		em->failed = FALSE;
		job->respawn = FALSE;
		job->normalexit = exitcodes;
		job->normalexit_len = 2;

		job_child_reaper (NULL, 1, FALSE, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process_state, PROCESS_ACTIVE);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);

		TEST_EQ_P (job->cause, em);
		TEST_EQ (em->failed, FALSE);

		TEST_NE (job->pid, 1);

		waitpid (job->pid, NULL, 0);
	}

	nih_list_free (&em->event.entry);
	job->cause = NULL;


	/* Check that we can reap the stopping task of the job, and end up
	 * in the waiting state.
	 */
	TEST_FEATURE ("with stopping task");
	em = event_emit ("foo", NULL, NULL);

	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_STOPPING;
		job->process_state = PROCESS_ACTIVE;
		job->pid = 1;
		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;
		job->cause = em;
		em->failed = FALSE;

		job_child_reaper (NULL, 1, FALSE, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ (job->process_state, PROCESS_NONE);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_state, JOB_WAITING);
		TEST_EQ (job->exit_status, 0);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ (em->failed, FALSE);

		TEST_EQ (job->pid, 0);
	}

	nih_list_free (&em->event.entry);
	job->cause = NULL;


	/* Check that we can reap a failing stopping task of the job, which
	 * should get marked as failed if the job hasn't been already.
	 */
	TEST_FEATURE ("with stopping task failure");
	em = event_emit ("foo", NULL, NULL);

	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_STOPPING;
		job->process_state = PROCESS_ACTIVE;
		job->pid = 1;
		job->failed = FALSE;
		job->failed_state = JOB_WAITING;
		job->exit_status = 0;
		job->cause = em;
		em->failed = FALSE;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, FALSE, 1);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ (job->process_state, PROCESS_NONE);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_state, JOB_STOPPING);
		TEST_EQ (job->exit_status, 1);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ (em->failed, TRUE);

		TEST_EQ (job->pid, 0);

		TEST_FILE_EQ (output, ("test: test process (1) terminated "
				       "with status 1\n"));
		TEST_FILE_END (output);

		TEST_FILE_RESET (output);
	}

	nih_list_free (&em->event.entry);
	job->cause = NULL;


	/* Check that a failing stopping task doesn't overwrite the record
	 * of a failing earlier task.
	 */
	TEST_FEATURE ("with stopping task failure after failure");
	em = event_emit ("foo", NULL, NULL);

	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_STOPPING;
		job->process_state = PROCESS_ACTIVE;
		job->pid = 1;
		job->failed = TRUE;
		job->failed_state = JOB_RUNNING;
		job->exit_status = SIGSEGV | 0x80;
		job->cause = em;
		em->failed = TRUE;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, FALSE, 1);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ (job->process_state, PROCESS_NONE);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_state, JOB_RUNNING);
		TEST_EQ (job->exit_status, SIGSEGV | 0x80);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ (em->failed, TRUE);

		TEST_EQ (job->pid, 0);

		TEST_FILE_EQ (output, ("test: test process (1) terminated "
				       "with status 1\n"));
		TEST_FILE_END (output);

		TEST_FILE_RESET (output);
	}

	nih_list_free (&em->event.entry);
	job->cause = NULL;


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
