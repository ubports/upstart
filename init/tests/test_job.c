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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/select.h>

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/io.h>

#include "event.h"
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

	/* Start events list should be empty */
	if (! NIH_LIST_EMPTY (&job->start_events)) {
		printf ("BAD: start events not initialised to empty list.\n");
		ret = 1;
	}

	/* Stop events list should be empty */
	if (! NIH_LIST_EMPTY (&job->stop_events)) {
		printf ("BAD: stop events not initialised to empty list.\n");
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
test_change_state (void)
{
	Job         *job;
	Event       *event;
	struct stat  statbuf;
	char         dirname[22], filename[40];
	int          ret = 0;

	printf ("Testing job_change_state()\n");
	sprintf (dirname, "/tmp/test_job.XXXXXX");
	mkdtemp (dirname);

	job = job_new (NULL, "test");
	job->start_script = nih_sprintf (job, "touch %s/start", dirname);
	job->stop_script = nih_sprintf (job, "touch %s/stop", dirname);
	job->respawn_script = nih_sprintf (job, "touch %s/respawn", dirname);
	job->command = nih_sprintf (job, "touch %s/run", dirname);


	printf ("...waiting to starting with script\n");
	job->goal = JOB_START;
	job->state = JOB_WAITING;
	job->process_state = PROCESS_NONE;
	job_change_state (job, JOB_STARTING);

	/* Goal should still be JOB_START */
	if (job->goal != JOB_START) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* State should be JOB_STARTING */
	if (job->state != JOB_STARTING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should be PROCESS_ACTIVE */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}

	/* Event should be starting */
	event = event_find_by_name (job->name);
	if (strcmp (event->value, "starting")) {
		printf ("BAD: event level wasn't what we expected.\n");
		ret = 1;
	}

	/* Start script should have been run */
	waitpid (job->pid, NULL, 0);
	sprintf (filename, "%s/start", dirname);
	if (stat (filename, &statbuf) < 0) {
		printf ("BAD: start script doesn't appear to have run.\n");
		ret = 1;
	}

	unlink (filename);


	printf ("...waiting to starting with no script\n");
	job->goal = JOB_START;
	job->state = JOB_WAITING;
	job->process_state = PROCESS_NONE;
	nih_free (job->start_script);
	job->start_script = NULL;
	job_change_state (job, JOB_STARTING);

	/* Goal should still be JOB_START */
	if (job->goal != JOB_START) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* State should be JOB_RUNNING */
	if (job->state != JOB_RUNNING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should be PROCESS_ACTIVE */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}

	/* Event should be running */
	event = event_find_by_name (job->name);
	if (strcmp (event->value, "running")) {
		printf ("BAD: event level wasn't what we expected.\n");
		ret = 1;
	}

	/* Command should have been run */
	waitpid (job->pid, NULL, 0);
	sprintf (filename, "%s/run", dirname);
	if (stat (filename, &statbuf) < 0) {
		printf ("BAD: command doesn't appear to have run.\n");
		ret = 1;
	}

	unlink (filename);
	job->start_script = nih_sprintf (job, "touch %s/start", dirname);


	printf ("...starting to running with command\n");
	job->goal = JOB_START;
	job->state = JOB_STARTING;
	job->process_state = PROCESS_NONE;
	job_change_state (job, JOB_RUNNING);

	/* Goal should still be JOB_START */
	if (job->goal != JOB_START) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* State should be JOB_RUNNING */
	if (job->state != JOB_RUNNING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should be PROCESS_ACTIVE */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}

	/* Event should be running */
	event = event_find_by_name (job->name);
	if (strcmp (event->value, "running")) {
		printf ("BAD: event level wasn't what we expected.\n");
		ret = 1;
	}

	/* Command should have been run */
	waitpid (job->pid, NULL, 0);
	sprintf (filename, "%s/run", dirname);
	if (stat (filename, &statbuf) < 0) {
		printf ("BAD: command doesn't appear to have run.\n");
		ret = 1;
	}

	unlink (filename);


	printf ("...starting to running with script\n");
	job->goal = JOB_START;
	job->state = JOB_STARTING;
	job->process_state = PROCESS_NONE;
	job->script = job->command;
	job->command = NULL;
	job_change_state (job, JOB_RUNNING);

	/* Goal should still be JOB_START */
	if (job->goal != JOB_START) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* State should be JOB_RUNNING */
	if (job->state != JOB_RUNNING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should be PROCESS_ACTIVE */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}

	/* Event should be running */
	event = event_find_by_name (job->name);
	if (strcmp (event->value, "running")) {
		printf ("BAD: event level wasn't what we expected.\n");
		ret = 1;
	}

	/* Script should have been run */
	waitpid (job->pid, NULL, 0);
	sprintf (filename, "%s/run", dirname);
	if (stat (filename, &statbuf) < 0) {
		printf ("BAD: script doesn't appear to have run.\n");
		ret = 1;
	}

	unlink (filename);


	printf ("...starting to running without either\n");
	job->goal = JOB_START;
	job->state = JOB_STARTING;
	job->process_state = PROCESS_NONE;
	nih_free (job->script);
	job->script = NULL;
	job_change_state (job, JOB_RUNNING);

	/* Goal should have become JOB_STOP */
	if (job->goal != JOB_STOP) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* State should be JOB_STOPPING */
	if (job->state != JOB_STOPPING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should be PROCESS_ACTIVE */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}

	/* Event should be stopping */
	event = event_find_by_name (job->name);
	if (strcmp (event->value, "stopping")) {
		printf ("BAD: event level wasn't what we expected.\n");
		ret = 1;
	}

	/* Stop script should have been run */
	waitpid (job->pid, NULL, 0);
	sprintf (filename, "%s/stop", dirname);
	if (stat (filename, &statbuf) < 0) {
		printf ("BAD: stop script doesn't appear to have run.\n");
		ret = 1;
	}

	unlink (filename);
	job->command = nih_sprintf (job, "touch %s/run", dirname);


	printf ("...running to respawning with script\n");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_NONE;
	job_change_state (job, JOB_RESPAWNING);

	/* Goal should still be JOB_START */
	if (job->goal != JOB_START) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* State should be JOB_RESPAWNING */
	if (job->state != JOB_RESPAWNING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should be PROCESS_ACTIVE */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}

	/* Event should be respawning */
	event = event_find_by_name (job->name);
	if (strcmp (event->value, "respawning")) {
		printf ("BAD: event level wasn't what we expected.\n");
		ret = 1;
	}

	/* Respawn script should have been run */
	waitpid (job->pid, NULL, 0);
	sprintf (filename, "%s/respawn", dirname);
	if (stat (filename, &statbuf) < 0) {
		printf ("BAD: respawn script doesn't appear to have run.\n");
		ret = 1;
	}

	unlink (filename);


	printf ("...running to respawning without script\n");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_NONE;
	nih_free (job->respawn_script);
	job->respawn_script = NULL;
	job_change_state (job, JOB_RESPAWNING);

	/* Goal should still be JOB_START */
	if (job->goal != JOB_START) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* State should be JOB_RUNNING */
	if (job->state != JOB_RUNNING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should be PROCESS_ACTIVE */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}

	/* Event should be running */
	event = event_find_by_name (job->name);
	if (strcmp (event->value, "running")) {
		printf ("BAD: event level wasn't what we expected.\n");
		ret = 1;
	}

	/* Command should have been run */
	waitpid (job->pid, NULL, 0);
	sprintf (filename, "%s/run", dirname);
	if (stat (filename, &statbuf) < 0) {
		printf ("BAD: command doesn't appear to have run.\n");
		ret = 1;
	}

	unlink (filename);


	printf ("...running to stopping with script\n");
	job->goal = JOB_STOP;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_NONE;
	job_change_state (job, JOB_STOPPING);

	/* Goal should still be JOB_STOP */
	if (job->goal != JOB_STOP) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* State should be JOB_STOPPING */
	if (job->state != JOB_STOPPING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should be PROCESS_ACTIVE */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}

	/* Event should be stopping */
	event = event_find_by_name (job->name);
	if (strcmp (event->value, "stopping")) {
		printf ("BAD: event level wasn't what we expected.\n");
		ret = 1;
	}

	/* Stop script should have been run */
	waitpid (job->pid, NULL, 0);
	sprintf (filename, "%s/stop", dirname);
	if (stat (filename, &statbuf) < 0) {
		printf ("BAD: stop script doesn't appear to have run.\n");
		ret = 1;
	}

	unlink (filename);


	printf ("...running to stopping without script\n");
	job->goal = JOB_STOP;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_NONE;
	nih_free (job->stop_script);
	job->stop_script = NULL;
	job_change_state (job, JOB_STOPPING);

	/* Goal should still be JOB_STOP */
	if (job->goal != JOB_STOP) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* State should be JOB_WAITING */
	if (job->state != JOB_WAITING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should be PROCESS_NONE */
	if (job->process_state != PROCESS_NONE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}

	/* Event should be waiting */
	event = event_find_by_name (job->name);
	if (strcmp (event->value, "waiting")) {
		printf ("BAD: event level wasn't what we expected.\n");
		ret = 1;
	}

	job->stop_script = nih_sprintf (job, "touch %s/stop", dirname);


	printf ("...stopping to waiting\n");
	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;
	job->process_state = PROCESS_NONE;
	job_change_state (job, JOB_WAITING);

	/* Goal should still be JOB_STOP */
	if (job->goal != JOB_STOP) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* State should be JOB_WAITING */
	if (job->state != JOB_WAITING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Event should be waiting */
	event = event_find_by_name (job->name);
	if (strcmp (event->value, "waiting")) {
		printf ("BAD: event level wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should be PROCESS_NONE */
	if (job->process_state != PROCESS_NONE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...stopping to starting\n");
	job->goal = JOB_START;
	job->state = JOB_STOPPING;
	job->process_state = PROCESS_NONE;
	job_change_state (job, JOB_STARTING);

	/* Goal should still be JOB_START */
	if (job->goal != JOB_START) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* State should be JOB_STARTING */
	if (job->state != JOB_STARTING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should be PROCESS_ACTIVE */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}

	/* Event should be starting */
	event = event_find_by_name (job->name);
	if (strcmp (event->value, "starting")) {
		printf ("BAD: event level wasn't what we expected.\n");
		ret = 1;
	}

	/* Start script should have been run */
	waitpid (job->pid, NULL, 0);
	sprintf (filename, "%s/start", dirname);
	if (stat (filename, &statbuf) < 0) {
		printf ("BAD: start script doesn't appear to have run.\n");
		ret = 1;
	}

	unlink (filename);


	/* Fun way to clean up */
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_NONE;
	job_run_command (job, nih_sprintf (job, "rm -rf %s", dirname));
	waitpid (job->pid, NULL, 0);

	nih_list_free (&job->entry);
	nih_list_free (&event->entry);

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

	/* Next state should be starting */
	if (state != JOB_STARTING) {
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
test_run_command (void)
{
	Job         *job;
	FILE        *output;
	struct stat  statbuf;
	char         filename[20], text[80];
	int          ret = 0;

	printf ("Testing job_run_command()\n");
	sprintf (filename, "/tmp/test_job.%d", getpid ());
	unlink (filename);

	printf ("...with simple command\n");
	job = job_new (NULL, "test");
	job->state = JOB_RUNNING;
	job->command = nih_sprintf (job, "touch %s", filename);
	job_run_command (job, job->command);

	/* Job process id must have been set */
	if (job->pid == 0) {
		printf ("BAD: pid not updated.\n");
		ret = 1;
	}

	/* Job process state should now be active */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state not updated.\n");
		ret = 1;
	}

	/* Wait for the job */
	waitpid (job->pid, NULL, 0);

	/* Filename should exist */
	if (stat (filename, &statbuf) != 0) {
		printf ("BAD: expected file not created.\n");
		ret = 1;
	}

	nih_list_free (&job->entry);
	unlink (filename);


	printf ("...with shell command\n");
	job = job_new (NULL, "test");
	job->state = JOB_RUNNING;
	job->command = nih_sprintf (job, "echo $$ > %s", filename);
	job_run_command (job, job->command);

	/* Job process id must have been set */
	if (job->pid == 0) {
		printf ("BAD: pid not updated.\n");
		ret = 1;
	}

	/* Job process state should now be active */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state not updated.\n");
		ret = 1;
	}

	/* Wait for the job */
	waitpid (job->pid, NULL, 0);

	/* Filename should exist */
	if (stat (filename, &statbuf) != 0) {
		printf ("BAD: expected file not created.\n");
		ret = 1;
	}

	/* Filename should contain the pid */
	output = fopen (filename, "r");
	fgets (text, sizeof (text), output);
	if (atoi (text) != job->pid) {
		printf ("BAD: command output not what we expected.\n");
		ret = 1;
	}

	nih_list_free (&job->entry);
	fclose (output);
	unlink (filename);

	return ret;
}

int
test_run_script (void)
{
	Job  *job;
	FILE *output;
	char  filename[20], text[80];
	int   ret = 0, status, first;

	printf ("Testing job_run_script()\n");
	sprintf (filename, "/tmp/test_job.%d", getpid ());
	unlink (filename);

	printf ("...with small script\n");
	job = job_new (NULL, "test");
	job->state = JOB_RUNNING;
	job->script = nih_sprintf (job, "exec > %s\necho $0\necho $@",
				   filename);
	job_run_script (job, job->script);

	/* Job process id must have been set */
	if (job->pid == 0) {
		printf ("BAD: pid not updated.\n");
		ret = 1;
	}

	/* Job process state should now be active */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state not updated.\n");
		ret = 1;
	}

	/* Wait for the job */
	waitpid (job->pid, &status, 0);

	/* Job should have terminated normally */
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0)) {
		printf ("BAD: job terminated badly.\n");
		ret = 1;
	}

	/* Filename should exist */
	output = fopen (filename, "r");
	if (output == NULL) {
		printf ("BAD: expected file not created.\n");
		ret = 1;
	}

	/* Script should have been run with the shell */
	fgets (text, sizeof (text), output);
	if (strcmp (text, "/bin/sh\n")) {
		printf ("BAD: program name wasn't what we expected.\n");
		ret = 1;
	}

	/* Script should have no arguments */
	fgets (text, sizeof (text), output);
	if (strcmp (text, "\n")) {
		printf ("BAD: arguments weren't what we expected.\n");
		ret = 1;
	}

	nih_list_free (&job->entry);
	fclose (output);
	unlink (filename);


	printf ("...with script that will fail\n");
	job = job_new (NULL, "test");
	job->state = JOB_RUNNING;
	job->script = nih_sprintf (job, "exec > %s\ntest -d %s\necho oops",
				   filename, filename);
	job_run_script (job, job->script);

	/* Job process id must have been set */
	if (job->pid == 0) {
		printf ("BAD: pid not updated.\n");
		ret = 1;
	}

	/* Job process state should now be active */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state not updated.\n");
		ret = 1;
	}

	/* Wait for the job */
	waitpid (job->pid, &status, 0);

	/* Job should have terminated badly */
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 1)) {
		printf ("BAD: job terminated by signal or normally.\n");
		ret = 1;
	}

	/* Filename should exist */
	output = fopen (filename, "r");
	if (output == NULL) {
		printf ("BAD: expected file not created.\n");
		ret = 1;
	}

	/* But should be empty */
	if (fgets (text, sizeof (text), output) != NULL) {
		printf ("BAD: unexpected data in output.\n");
		ret = 1;
	}

	nih_list_free (&job->entry);
	fclose (output);
	unlink (filename);


	printf ("...with long script\n");
	job = job_new (NULL, "test");
	job->state = JOB_RUNNING;
	job->script = nih_alloc (job, 4096);
	sprintf (job->script, "exec > %s\necho $0\necho $@\n", filename);
	while (strlen (job->script) < 4000)
		strcat (job->script, "# this just bulks it out a bit");
	job_run_script (job, job->script);

	/* Job process id must have been set */
	if (job->pid == 0) {
		printf ("BAD: pid not updated.\n");
		ret = 1;
	}

	/* Job process state should now be active */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state not updated.\n");
		ret = 1;
	}

	/* We should have an I/O watch to feed the data ... we need to do
	 * that by hand
	 */
	first = TRUE;
	for (;;) {
		fd_set readfds, writefds, exceptfds;
		int    nfds;

		nfds = 0;
		FD_ZERO (&readfds);
		FD_ZERO (&writefds);
		FD_ZERO (&exceptfds);

		nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
		if (! nfds) {
			if (first) {
				printf ("BAD: we expected to feed data.\n");
				ret = 1;
			}
			break;
		}
		first = FALSE;

		assert (select (nfds, &readfds, &writefds, &exceptfds,
				NULL) > 0);

		nih_io_handle_fds (&readfds, &writefds, &exceptfds);
	}

	/* Wait for the job */
	waitpid (job->pid, &status, 0);

	/* Job should have terminated normally */
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0)) {
		printf ("BAD: job terminated badly.\n");
		ret = 1;
	}

	/* Filename should exist */
	output = fopen (filename, "r");
	if (output == NULL) {
		printf ("BAD: expected file not created.\n");
		ret = 1;
	}

	/* Script should have been run as /dev/fd/N */
	fgets (text, sizeof (text), output);
	if (strncmp (text, "/dev/fd/", 8)) {
		printf ("BAD: program name wasn't what we expected.\n");
		ret = 1;
	}

	/* Script should have no arguments */
	fgets (text, sizeof (text), output);
	if (strcmp (text, "\n")) {
		printf ("BAD: arguments weren't what we expected.\n");
		ret = 1;
	}

	nih_list_free (&job->entry);
	fclose (output);
	unlink (filename);

	return ret;
}


int
test_kill_process (void)
{
	Job         *job;
	NihTimer    *timer;
	pid_t        pid;
	char         filename[20];
	struct stat  statbuf;
	int          ret = 0, status;

	printf ("Testing job_kill_process()\n");
	job = job_new (NULL, "test");
	job->goal = JOB_STOP;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;
	job->kill_timeout = 1000;


	printf ("...with easily killed process\n");
	job->pid = pid = fork ();
	if (pid == 0) {
		select (0, NULL, NULL, NULL, NULL);

		exit (0);
	}
	job_kill_process (job);
	waitpid (pid, &status, 0);

	/* Job should have been killed by the TERM signal */
	if ((! WIFSIGNALED (status)) || (WTERMSIG (status) != SIGTERM)) {
		printf ("BAD: process was not terminated by SIGTERM.\n");
		ret = 1;
	}

	/* Process id should not have been changed */
	if (job->pid != pid) {
		printf ("BAD: process id changed unexpectedly.\n");
		ret = 1;
	}

	/* Process state should have been changed to KILLED */
	if (job->process_state != PROCESS_KILLED) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}

	/* Kill timer should have been spawned */
	if (job->kill_timer == NULL) {
		printf ("BAD: kill timer was not spawned.\n");
		ret = 1;
	}

	/* Kill timer should be scheduled according to timeout */
	if ((job->kill_timer->due > time (NULL) + 1000)
	    || (job->kill_timer->due < time (NULL) + 950)) {
		printf ("BAD: kill timer not due when we expected.\n");
		ret = 1;
	}

	nih_free (job->kill_timer);
	job->kill_timer = NULL;


	printf ("...with hard to kill process\n");
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;
	job->pid = pid = fork ();
	if (pid == 0) {
		struct sigaction act;

		act.sa_handler = SIG_IGN;
		act.sa_flags = 0;
		sigemptyset (&act.sa_mask);
		sigaction (SIGTERM, &act, NULL);

		for (;;)
			select (0, NULL, NULL, NULL, NULL);

		exit (0);
	}
	job_kill_process (job);

	/* Job should still exist */
	if (kill (pid, 0) != 0) {
		printf ("BAD: process died unexpectedly.\n");
		ret = 1;
	}

	/* Run the kill timer */
	timer = job->kill_timer;
	timer->callback (timer->data, timer);
	nih_free (timer);
	waitpid (pid, &status, 0);

	/* Job should have been killed by the KILL signal */
	if ((! WIFSIGNALED (status)) || (WTERMSIG (status) != SIGKILL)) {
		printf ("BAD: process was not terminated by SIGKILL.\n");
		ret = 1;
	}

	/* Process id should now be zero (we killed it really hard) */
	if (job->pid != 0) {
		printf ("BAD: process id wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should have been changed to NONE */
	if (job->process_state != PROCESS_NONE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}

	/* Kill timer should have been ended */
	if (job->kill_timer != NULL) {
		printf ("BAD: kill timer was not ended.\n");
		ret = 1;
	}

	/* Job state should be WAITING (come via stopping) */
	if (job->state != JOB_WAITING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with hard to kill process and stop script\n");
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;
	sprintf (filename, "/tmp/test_job.%d", getpid ());
	unlink (filename);
	job->stop_script = nih_sprintf (job, "touch %s", filename);
	job->pid = pid = fork ();
	if (pid == 0) {
		struct sigaction act;

		act.sa_handler = SIG_IGN;
		act.sa_flags = 0;
		sigemptyset (&act.sa_mask);
		sigaction (SIGTERM, &act, NULL);

		for (;;)
			select (0, NULL, NULL, NULL, NULL);

		exit (0);
	}
	job_kill_process (job);

	/* Job should still exist */
	if (kill (pid, 0) != 0) {
		printf ("BAD: process died unexpectedly.\n");
		ret = 1;
	}

	/* Run the kill timer */
	timer = job->kill_timer;
	timer->callback (timer->data, timer);
	nih_free (timer);
	waitpid (pid, &status, 0);
	waitpid (job->pid, NULL, 0);

	/* Job state should be stopping */
	if (job->state != JOB_STOPPING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should be active */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}

	/* Check the job script was run */
	if (stat (filename, &statbuf) != 0) {
		printf ("BAD: stop script wasn't run.\n");
		ret = 1;
	}

	unlink (filename);
	nih_free (job->stop_script);
	job->stop_script = NULL;


	printf ("...with already dead process\n");
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;
	job->pid = pid = fork ();
	if (pid == 0) {
		exit (0);
	}
	waitpid (pid, &status, 0);
	job_kill_process (job);

	/* Process id should now be zero */
	if (job->pid != 0) {
		printf ("BAD: process id wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should have been changed to NONE */
	if (job->process_state != PROCESS_NONE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}

	/* Job state should be WAITING (come via stopping) */
	if (job->state != JOB_WAITING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* No kill timer should have been set */
	if (job->kill_timer != NULL) {
		printf ("BAD: kill timer started unexpectedly.\n");
		ret = 1;
	}


	nih_list_free (&job->entry);

	return ret;
}


static int was_called = 0;

static int
destructor_called (void *ptr)
{
	was_called++;
	return 0;
}

int
test_handle_child (void)
{
	Job *job;
	int  ret = 0, exitcodes[1] = { 0 };

	printf ("Testing job_handle_child()\n");
	job = job_new (NULL, "test");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;
	job->pid = 1000;
	job->command = "echo";
	job->stop_script = "echo";
	job->respawn_script = "echo";


	printf ("...with unknown pid\n");
	job_handle_child (NULL, 999, FALSE, 0);

	/* Check the job didn't change */
	if (job->state != JOB_RUNNING) {
		printf ("BAD: job changed unexpectedly.\n");
		ret = 1;
	}


	printf ("...with running task\n");
	job_handle_child (NULL, 1000, FALSE, 0);

	/* Job should no longer have that process */
	if (job->pid == 1000) {
		printf ("BAD: process id wasn't what we expected.\n");
		ret = 1;
	}

	/* Goal should now be STOP */
	if (job->goal != JOB_STOP) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* State should now be STOPPING */
	if (job->state != JOB_STOPPING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should be ACTIVE */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}

	waitpid (job->pid, NULL, 0);


	printf ("...with kill timer\n");
	job->goal = JOB_START;
	job->state = JOB_STARTING;
	job->process_state = PROCESS_ACTIVE;
	job->pid = 1000;
	job->kill_timer = (void *) nih_strdup (job, "test");
	nih_alloc_set_destructor (job->kill_timer, destructor_called);
	was_called = 0;
	job_handle_child (NULL, 1000, FALSE, 0);

	/* Timer should have been unset */
	if (job->kill_timer != NULL) {
		printf ("BAD: kill timer was not unset.\n");
		ret = 1;
	}

	/* Timer should have been freed */
	if (! was_called) {
		printf ("BAD: kill timer was not destroyed.\n");
		ret = 1;
	}

	waitpid (job->pid, NULL, 0);


	printf ("...with starting task\n");
	job->goal = JOB_START;
	job->state = JOB_STARTING;
	job->process_state = PROCESS_ACTIVE;
	job->pid = 1000;
	job_handle_child (NULL, 1000, FALSE, 0);

	/* Job should no longer have that process */
	if (job->pid == 1000) {
		printf ("BAD: process id wasn't what we expected.\n");
		ret = 1;
	}

	/* Goal should still be START */
	if (job->goal != JOB_START) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* State should now be RUNNING */
	if (job->state != JOB_RUNNING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should be ACTIVE */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}

	waitpid (job->pid, NULL, 0);


	printf ("...with starting task failure\n");
	job->goal = JOB_START;
	job->state = JOB_STARTING;
	job->process_state = PROCESS_ACTIVE;
	job->pid = 1000;
	job_handle_child (NULL, 1000, FALSE, 1);

	/* Job should no longer have that process */
	if (job->pid == 1000) {
		printf ("BAD: process id wasn't what we expected.\n");
		ret = 1;
	}

	/* Goal should now be STOP */
	if (job->goal != JOB_STOP) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* State should now be STOPPING */
	if (job->state != JOB_STOPPING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should be ACTIVE */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}

	waitpid (job->pid, NULL, 0);


	printf ("...with starting task kill\n");
	job->goal = JOB_START;
	job->state = JOB_STARTING;
	job->process_state = PROCESS_ACTIVE;
	job->pid = 1000;
	job_handle_child (NULL, 1000, TRUE, SIGTERM);

	/* Job should no longer have that process */
	if (job->pid == 1000) {
		printf ("BAD: process id wasn't what we expected.\n");
		ret = 1;
	}

	/* Goal should now be STOP */
	if (job->goal != JOB_STOP) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* State should now be STOPPING */
	if (job->state != JOB_STOPPING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should be ACTIVE */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}

	waitpid (job->pid, NULL, 0);


	printf ("...with running task to respawn\n");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;
	job->pid = 1000;
	job->respawn = TRUE;
	job_handle_child (NULL, 1000, FALSE, 0);

	/* Job should no longer have that process */
	if (job->pid == 1000) {
		printf ("BAD: process id wasn't what we expected.\n");
		ret = 1;
	}

	/* Goal should still be START */
	if (job->goal != JOB_START) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* State should now be RESPAWNING */
	if (job->state != JOB_RESPAWNING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should be ACTIVE */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}

	waitpid (job->pid, NULL, 0);


	printf ("...with running task and normal exit\n");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;
	job->pid = 1000;
	job->respawn = TRUE;
	job->normalexit = exitcodes;
	job->normalexit_len = 1;
	job_handle_child (NULL, 1000, FALSE, 0);

	/* Job should no longer have that process */
	if (job->pid == 1000) {
		printf ("BAD: process id wasn't what we expected.\n");
		ret = 1;
	}

	/* Goal should now be STOP */
	if (job->goal != JOB_STOP) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* State should now be STOPPING */
	if (job->state != JOB_STOPPING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should be ACTIVE */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}

	waitpid (job->pid, NULL, 0);


	nih_list_free (&job->entry);

	return ret;
}


int
test_start (void)
{
	Job *job;
	int  ret = 0;

	printf ("Testing job_start()\n");
	job = job_new (NULL, "test");
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->process_state = PROCESS_NONE;
	job->start_script = "echo";


	printf ("...with waiting job\n");
	job_start (job);

	/* Job goal should now be JOB_START */
	if (job->goal != JOB_START) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* Job state should now be JOB_STARTING */
	if (job->state != JOB_STARTING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should now be PROCESS_ACTIVE */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}

	waitpid (job->pid, NULL, 0);


	printf ("...with stopping job\n");
	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;
	job->process_state = PROCESS_ACTIVE;
	job_start (job);

	/* Job goal should now be JOB_START */
	if (job->goal != JOB_START) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* Job state should remain JOB_STOPPING */
	if (job->state != JOB_STOPPING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should remain PROCESS_ACTIVE */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with running job\n");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;
	job_start (job);

	/* Job goal should remain JOB_START */
	if (job->goal != JOB_START) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* Job state should remain JOB_RUNNING */
	if (job->state != JOB_RUNNING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should remain PROCESS_ACTIVE */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}


	nih_list_free (&job->entry);

	return ret;
}

int
test_stop (void)
{
	Job   *job;
	pid_t  pid;
	int    ret = 0, status;

	printf ("Testing job_stop()\n");
	job = job_new (NULL, "test");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;
	job->pid = pid = fork ();
	if (pid == 0) {
		select (0, NULL, NULL, NULL, NULL);

		exit (0);
	}


	printf ("...with running job\n");
	job_stop (job);
	waitpid (pid, &status, 0);

	/* Job goal should now be JOB_STOP */
	if (job->goal != JOB_STOP) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* Job state should remain JOB_RUNNING */
	if (job->state != JOB_RUNNING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should now be PROCESS_KILLED */
	if (job->process_state != PROCESS_KILLED) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}

	/* Kill timer should be active */
	if (job->kill_timer == NULL) {
		printf ("BAD: kill timer wasn't set.\n");
		ret = 1;
	}

	nih_free (job->kill_timer);
	job->kill_timer = NULL;

	/* Original running process should have been sent SIGTERM */
	if ((! WIFSIGNALED (status)) || (WTERMSIG (status) != SIGTERM)) {
		printf ("BAD: process terminated by unexpected means.\n");
		ret = 1;
	}


	printf ("...with starting job\n");
	job->goal = JOB_START;
	job->state = JOB_STARTING;
	job->process_state = PROCESS_ACTIVE;
	job->pid = pid = fork ();
	if (pid == 0) {
		select (0, NULL, NULL, NULL, NULL);

		exit (0);
	}
	job_stop (job);

	/* Job goal should now be JOB_STOP */
	if (job->goal != JOB_STOP) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* Job state should remain JOB_STARTING */
	if (job->state != JOB_STARTING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should remain PROCESS_ACTIVE */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process should not be changed */
	if (job->pid != pid) {
		printf ("BAD: process id wasn't what we expected.\n");
		ret = 1;
	}

	kill (pid, SIGTERM);
	waitpid (pid, NULL, 0);


	printf ("...with waiting job\n");
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->process_state = PROCESS_NONE;
	job_stop (job);

	/* Job goal should remain JOB_STOP */
	if (job->goal != JOB_STOP) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* Job state should remain JOB_WAITING */
	if (job->state != JOB_WAITING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should remain PROCESS_NONE */
	if (job->process_state != PROCESS_NONE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}


	nih_list_free (&job->entry);

	return ret;
}


int
test_start_event (void)
{
	Event *event;
	Job   *job;
	int    ret = 0;

	printf ("Testing job_start_event()\n");
	job = job_new (NULL, "test");
	job->command = "echo";

	event = event_new (job, "wibble");
	event->value = "up";
	nih_list_add (&job->start_events, &event->entry);


	printf ("...with non-matching event\n");
	event = event_new (NULL, "wibble");
	job_start_event (job, event);

	/* Job goal should still be JOB_STOP */
	if (job->goal != JOB_STOP) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* Job state should still be JOB_WAITING */
	if (job->state != JOB_WAITING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should still be PROCESS_NONE */
	if (job->process_state != PROCESS_NONE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with matching event\n");
	event->value = "up";
	job_start_event (job, event);

	/* Job goal should now be JOB_START */
	if (job->goal != JOB_START) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* Job state should now be JOB_RUNNING */
	if (job->state != JOB_RUNNING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should now be PROCESS_ACTIVE */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}

	kill (job->pid, SIGTERM);
	waitpid (job->pid, NULL, 0);

	nih_free (event);
	nih_list_free (&job->entry);

	return ret;
}

int
test_stop_event (void)
{
	Event *event;
	Job   *job;
	int    ret = 0, status;

	printf ("Testing job_stop_event()\n");
	job = job_new (NULL, "test");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;
	job->pid = fork ();
	if (job->pid == 0) {
		select (0, NULL, NULL, NULL, NULL);

		exit (0);
	}

	event = event_new (job, "wibble");
	event->value = "down";
	nih_list_add (&job->stop_events, &event->entry);


	printf ("...with non-matching event\n");
	event = event_new (NULL, "wibble");
	job_stop_event (job, event);

	/* Job goal should still be JOB_START */
	if (job->goal != JOB_START) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* Job state should still be JOB_RUNNING */
	if (job->state != JOB_RUNNING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should still be PROCESS_ACTIVE */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with matching event\n");
	event->value = "down";
	job_stop_event (job, event);

	/* Job goal should now be JOB_STOP */
	if (job->goal != JOB_STOP) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* Job state should still be JOB_RUNNING */
	if (job->state != JOB_RUNNING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should now be PROCESS_KILLED */
	if (job->process_state != PROCESS_KILLED) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}

	waitpid (job->pid, &status, 0);

	/* Process should be killed with SIGTERM */
	if ((! WIFSIGNALED (status)) || (WTERMSIG (status) != SIGTERM)) {
		printf ("BAD: process wasn't terminated by SIGTERM.\n");
		ret = 1;
	}

	nih_free (event);
	nih_list_free (&job->entry);

	return ret;
}

int
test_handle_event (void)
{
	Event *event;
	Job   *job1, *job2, *job3, *job4, *job5;
	int    ret = 0, status;

	printf ("Testing job_handle_event()\n");
	job1 = job_new (NULL, "foo");
	job1->goal = JOB_STOP;
	job1->state = JOB_WAITING;
	job1->process_state = PROCESS_NONE;
	job1->command = "echo";
	nih_list_add (&job1->start_events, &(event_new (job1, "poke")->entry));

	job2 = job_new (NULL, "bar");
	job2->goal = JOB_START;
	job2->state = JOB_RUNNING;
	job2->process_state = PROCESS_ACTIVE;
	job2->pid = fork ();
	if (job2->pid == 0) {
		select (0, NULL, NULL, NULL, NULL);

		exit (0);
	}
	nih_list_add (&job2->stop_events, &(event_new (job2, "poke")->entry));

	job3 = job_new (NULL, "baz");
	job3->goal = JOB_START;
	job3->state = JOB_RUNNING;
	job3->process_state = PROCESS_ACTIVE;
	job3->pid = fork ();
	if (job3->pid == 0) {
		select (0, NULL, NULL, NULL, NULL);

		exit (0);
	}
	nih_list_add (&job3->start_events, &(event_new (job3, "poke")->entry));
	nih_list_add (&job3->stop_events, &(event_new (job3, "poke")->entry));

	job4 = job_new (NULL, "frodo");
	job4->goal = JOB_STOP;
	job4->state = JOB_WAITING;
	job4->process_state = PROCESS_NONE;
	job4->command = "echo";

	job5 = job_new (NULL, "bilbo");
	job5->goal = JOB_START;
	job5->state = JOB_RUNNING;
	job5->process_state = PROCESS_ACTIVE;
	job5->pid = fork ();
	if (job5->pid == 0) {
		select (0, NULL, NULL, NULL, NULL);

		exit (0);
	}

	event = event_new (NULL, "poke");
	job_handle_event (event);

	/* First job goal should now be JOB_START */
	if (job1->goal != JOB_START) {
		printf ("BAD: first job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* First job state should now be JOB_RUNNING */
	if (job1->state != JOB_RUNNING) {
		printf ("BAD: first job state wasn't what we expected.\n");
		ret = 1;
	}

	/* First process state should now be PROCESS_ACTIVE */
	if (job1->process_state != PROCESS_ACTIVE) {
		printf ("BAD: first process state wasn't what we expected.\n");
		ret = 1;
	}

	waitpid (job1->pid, NULL, 0);


	/* Second job goal should now be JOB_STOP */
	if (job2->goal != JOB_STOP) {
		printf ("BAD: second job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* Second job state should still be JOB_RUNNING */
	if (job2->state != JOB_RUNNING) {
		printf ("BAD: second job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Second process state should now be PROCESS_KILLED */
	if (job2->process_state != PROCESS_KILLED) {
		printf ("BAD: second process state wasn't what we expected.\n");
		ret = 1;
	}

	waitpid (job2->pid, &status, 0);

	/* Second process should be killed with SIGTERM */
	if ((! WIFSIGNALED (status)) || (WTERMSIG (status) != SIGTERM)) {
		printf ("BAD: second process wasn't terminated by SIGTERM.\n");
		ret = 1;
	}


	/* Third job goal should still be JOB_START */
	if (job3->goal != JOB_START) {
		printf ("BAD: third job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* Third job state should still be JOB_RUNNING */
	if (job3->state != JOB_RUNNING) {
		printf ("BAD: third job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Third process state should now be PROCESS_KILLED */
	if (job3->process_state != PROCESS_KILLED) {
		printf ("BAD: third process state wasn't what we expected.\n");
		ret = 1;
	}

	waitpid (job3->pid, &status, 0);

	/* Third process should be killed with SIGTERM */
	if ((! WIFSIGNALED (status)) || (WTERMSIG (status) != SIGTERM)) {
		printf ("BAD: third process wasn't terminated by SIGTERM.\n");
		ret = 1;
	}


	/* Fourth job goal should still be JOB_STOP */
	if (job4->goal != JOB_STOP) {
		printf ("BAD: fourth job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* Fourth job state should still be JOB_WAITING */
	if (job4->state != JOB_WAITING) {
		printf ("BAD: fourth job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Fourth process state should still be PROCESS_NONE */
	if (job4->process_state != PROCESS_NONE) {
		printf ("BAD: fourth process state wasn't what we expected.\n");
		ret = 1;
	}


	/* Fifth job goal should still be JOB_START */
	if (job5->goal != JOB_START) {
		printf ("BAD: fifth job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* Fifth job state should still be JOB_RUNNING */
	if (job5->state != JOB_RUNNING) {
		printf ("BAD: fifth job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Fifth process state should now be PROCESS_ACTIVE */
	if (job5->process_state != PROCESS_ACTIVE) {
		printf ("BAD: fifth process state wasn't what we expected.\n");
		ret = 1;
	}

	kill (job5->pid, SIGTERM);
	waitpid (job5->pid, NULL, 0);


	nih_free (event);

	nih_list_free (&job1->entry);
	nih_list_free (&job2->entry);
	nih_list_free (&job3->entry);
	nih_list_free (&job4->entry);
	nih_list_free (&job5->entry);

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
	ret |= test_change_state ();
	ret |= test_next_state ();
	ret |= test_run_command ();
	ret |= test_run_script ();
	ret |= test_kill_process ();
	ret |= test_handle_child ();
	ret |= test_start ();
	ret |= test_stop ();
	ret |= test_start_event ();
	ret |= test_stop_event ();
	ret |= test_handle_event ();

	return ret;
}
