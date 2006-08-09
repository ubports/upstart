/* upstart
 *
 * test_job.c - test suite for init/job.c
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

#include <stdio.h>
#include <string.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>

#include "job.h"


int
test_new (void)
{
	Job *job;
	int  ret = 0, i;

	printf ("Testing job_new()\n");
	job = job_new (NULL, "test");

	/* Name should be set */
	if (strcmp (job->name, "test")) {
		printf ("BAD: job name set incorrectly.\n");
		ret = 1;
	}

	/* Name should be a copy attached to the job */
	if (nih_alloc_parent (job->name) != job) {
		printf ("BAD: nih_alloc was not used for job name.\n");
		ret = 1;
	}

	/* Goal should be to stop the process */
	if (job->goal != JOB_STOP) {
		printf ("BAD: job goal set incorrectly.\n");
		ret = 1;
	}

	/* State should be waiting for event */
	if (job->state != JOB_WAITING) {
		printf ("BAD: job state set incorrectly.\n");
		ret = 1;
	}

	/* There should be no process */
	if (job->process_state != PROCESS_NONE) {
		printf ("BAD: job process state set incorrectly.\n");
		ret = 1;
	}

	/* Kill timeout should be the default */
	if (job->kill_timeout != JOB_DEFAULT_KILL_TIMEOUT) {
		printf ("BAD: job kill timeout set incorrectly.\n");
		ret = 1;
	}

	/* PID timeout should be the default */
	if (job->pid_timeout != JOB_DEFAULT_PID_TIMEOUT) {
		printf ("BAD: job pid timeout set incorrectly.\n");
		ret = 1;
	}

	/* The console should be logged */
	if (job->console != CONSOLE_LOGGED) {
		printf ("BAD: job console type set incorrectly.\n");
		ret = 1;
	}

	/* Umask should be the default */
	if (job->umask != JOB_DEFAULT_UMASK) {
		printf ("BAD: job umask set incorrectly.\n");
		ret = 1;
	}

	/* Limits should be all NULL (unset) */
	for (i = 0; i < RLIMIT_NLIMITS; i++) {
		if (job->limits[i] != NULL) {
			printf ("BAD: job limits set incorrectly.\n");
			ret = 1;
			break;
		}
	}

	/* Should be in jobs list */
	if (NIH_LIST_EMPTY (&job->entry)) {
		printf ("BAD: not placed into jobs list.\n");
		ret = 1;
	}

	/* Should have been allocated using nih_alloc */
	if (nih_alloc_size (job) != sizeof (Job)) {
		printf ("BAD: nih_alloc was not used for job.\n");
		ret = 1;
	}

	nih_list_free (&job->entry);

	return ret;
}


int
test_find_by_name (void)
{
	Job *job1, *job2, *job3, *ptr;
	int  ret = 0;

	printf ("Testing job_find_by_name()\n");
	job1 = job_new (NULL, "foo");
	job2 = job_new (NULL, "bar");
	job3 = job_new (NULL, "baz");

	printf ("...with name we expect to find\n");
	ptr = job_find_by_name ("bar");

	/* Pointer returned should be to job with that name */
	if (ptr != job2) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with name we do not expect to find\n");
	ptr = job_find_by_name ("frodo");

	/* Pointer returned should be NULL */
	if (ptr != NULL) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with empty job list\n");
	nih_list_free (&job3->entry);
	nih_list_free (&job2->entry);
	nih_list_free (&job1->entry);
	ptr = job_find_by_name ("bar");

	/* Pointer returned should be NULL */
	if (ptr != NULL) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}

	return ret;
}

int
test_find_by_pid (void)
{
	Job *job1, *job2, *job3, *ptr;
	int  ret = 0;

	printf ("Testing job_find_by_pid()\n");
	job1 = job_new (NULL, "foo");
	job1->pid = 10;
	job2 = job_new (NULL, "bar");
	job3 = job_new (NULL, "baz");
	job3->pid = 20;

	printf ("...with pid we expect to find\n");
	ptr = job_find_by_pid (20);

	/* Pointer returned should be to job with that pid */
	if (ptr != job3) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with pid we do not expect to find\n");
	ptr = job_find_by_pid (30);

	/* Pointer returned should be NULL */
	if (ptr != NULL) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with no pids in job list\n");
	nih_list_free (&job3->entry);
	nih_list_free (&job1->entry);
	ptr = job_find_by_pid (20);

	/* Pointer returned should be NULL */
	if (ptr != NULL) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with empty job list\n");
	nih_list_free (&job2->entry);
	ptr = job_find_by_pid (20);

	/* Pointer returned should be NULL */
	if (ptr != NULL) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}

	return ret;
}


int
test_next_state (void)
{
	Job      *job;
	JobState  state;
	int       ret = 0;

	printf ("Testing job_next_state()\n");
	job = job_new (NULL, "test");

	printf ("...with waiting job and a goal of stop\n");
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	state = job_next_state (job);

	/* Next state should be waiting */
	if (state != JOB_WAITING) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with waiting job and a goal of start\n");
	job->goal = JOB_START;
	job->state = JOB_WAITING;
	state = job_next_state (job);

	/* Next state should still be waiting (for dependency) */
	if (state != JOB_WAITING) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with starting job and a goal of stop\n");
	job->goal = JOB_STOP;
	job->state = JOB_STARTING;
	state = job_next_state (job);

	/* Next state should be stopping */
	if (state != JOB_STOPPING) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with starting job and a goal of start\n");
	job->goal = JOB_START;
	job->state = JOB_STARTING;
	state = job_next_state (job);

	/* Next state should be running */
	if (state != JOB_RUNNING) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with running job and a goal of stop\n");
	job->goal = JOB_STOP;
	job->state = JOB_RUNNING;
	state = job_next_state (job);

	/* Next state should be stopping */
	if (state != JOB_STOPPING) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with running job and a goal of start\n");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	state = job_next_state (job);

	/* Next state should be respawning (goal gets changed if not daemon) */
	if (state != JOB_RESPAWNING) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with stopping job and a goal of stop\n");
	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;
	state = job_next_state (job);

	/* Next state should be waiting */
	if (state != JOB_WAITING) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with stopping job and a goal of start\n");
	job->goal = JOB_START;
	job->state = JOB_STOPPING;
	state = job_next_state (job);

	/* Next state should be starting again */
	if (state != JOB_STARTING) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with respawning job and a goal of stop\n");
	job->goal = JOB_STOP;
	job->state = JOB_RESPAWNING;
	state = job_next_state (job);

	/* Next state should be stopping */
	if (state != JOB_STOPPING) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with respawning job and a goal of start\n");
	job->goal = JOB_START;
	job->state = JOB_RESPAWNING;
	state = job_next_state (job);

	/* Next state should be running */
	if (state != JOB_RUNNING) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}

	return ret;
}

int
test_state_name (void)
{
	const char *name;
	int         ret = 0;

	printf ("Testing job_state_name()\n");

	printf ("...with waiting state\n");
	name = job_state_name (JOB_WAITING);

	/* String should be waiting */
	if (strcmp (name, "waiting")) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with starting state\n");
	name = job_state_name (JOB_STARTING);

	/* String should be starting */
	if (strcmp (name, "starting")) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with running state\n");
	name = job_state_name (JOB_RUNNING);

	/* String should be running */
	if (strcmp (name, "running")) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with stopping state\n");
	name = job_state_name (JOB_STOPPING);

	/* String should be stopping */
	if (strcmp (name, "stopping")) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with respawning state\n");
	name = job_state_name (JOB_RESPAWNING);

	/* String should be respawning */
	if (strcmp (name, "respawning")) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}

	return ret;
}


int
main (int   argc,
      char *argv[])
{
	int ret = 0;

	ret |= test_new ();
	ret |= test_find_by_name ();
	ret |= test_find_by_pid ();
	ret |= test_next_state ();
	ret |= test_state_name ();

	return ret;
}
