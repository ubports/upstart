/* upstart
 *
 * test_job.c - test suite for upstart/job.c
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

#include <upstart/job.h>


int
test_goal_name (void)
{
	const char *name;
	int         ret = 0;

	printf ("Testing job_goal_name()\n");

	printf ("...with stop goal\n");
	name = job_goal_name (JOB_STOP);

	/* String should be stop */
	if (strcmp (name, "stop")) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with start goal\n");
	name = job_goal_name (JOB_START);

	/* String should be start */
	if (strcmp (name, "start")) {
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
test_process_state_name (void)
{
	const char *name;
	int         ret = 0;

	printf ("Testing process_state_name()\n");

	printf ("...with none state\n");
	name = process_state_name (PROCESS_NONE);

	/* String should be none */
	if (strcmp (name, "none")) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with spawned state\n");
	name = process_state_name (PROCESS_SPAWNED);

	/* String should be spawned */
	if (strcmp (name, "spawned")) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with active state\n");
	name = process_state_name (PROCESS_ACTIVE);

	/* String should be active */
	if (strcmp (name, "active")) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with killed state\n");
	name = process_state_name (PROCESS_KILLED);

	/* String should be killed */
	if (strcmp (name, "killed")) {
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

	ret |= test_goal_name ();
	ret |= test_state_name ();
	ret |= test_process_state_name ();

	return ret;
}
