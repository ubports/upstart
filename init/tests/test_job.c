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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/io.h>

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
main (int   argc,
      char *argv[])
{
	int ret = 0;

	ret |= test_new ();
	ret |= test_find_by_name ();
	ret |= test_find_by_pid ();
	ret |= test_next_state ();
	ret |= test_state_name ();
	ret |= test_run_command ();
	ret |= test_run_script ();

	return ret;
}
