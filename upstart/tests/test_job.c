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

	ret |= test_state_name ();

	return ret;
}
