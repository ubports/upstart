/* upstart
 *
 * test_enum.c - test suite for init/enum.c
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

#include <nih/macros.h>

#include "enum.h"


void
test_goal_name (void)
{
	const char *name;

	TEST_FUNCTION ("job_goal_name");

	/* Check that the JOB_STOP goal returns the right string. */
	TEST_FEATURE ("with stop goal");
	name = job_goal_name (JOB_STOP);

	TEST_EQ_STR (name, "stop");


	/* Check that the JOB_START goal returns the right string. */
	TEST_FEATURE ("with start goal");
	name = job_goal_name (JOB_START);

	TEST_EQ_STR (name, "start");


	/* Check that an invalid goal returns NULL. */
	TEST_FEATURE ("with invalid goal");
	name = job_goal_name (1234);

	TEST_EQ_P (name, NULL);
}

void
test_goal_from_name (void)
{
	JobGoal goal;

	TEST_FUNCTION ("job_goal_from_name");

	/* Check that the JOB_STOP goal is returned for the right string. */
	TEST_FEATURE ("with stop goal");
	goal = job_goal_from_name ("stop");

	TEST_EQ (goal, JOB_STOP);


	/* Check that the JOB_START goal is returned for the right string. */
	TEST_FEATURE ("with start goal");
	goal = job_goal_from_name ("start");

	TEST_EQ (goal, JOB_START);


	/* Check that -1 is returned for an invalid string. */
	TEST_FEATURE ("with invalid goal");
	goal = job_goal_from_name ("wibble");

	TEST_EQ (goal, -1);
}


void
test_state_name (void)
{
	const char *name;

	TEST_FUNCTION ("job_state_name");

	/* Check that the JOB_WAITING state returns the right string. */
	TEST_FEATURE ("with waiting state");
	name = job_state_name (JOB_WAITING);

	TEST_EQ_STR (name, "waiting");


	/* Check that the JOB_STARTING state returns the right string. */
	TEST_FEATURE ("with starting state");
	name = job_state_name (JOB_STARTING);

	TEST_EQ_STR (name, "starting");


	/* Check that the JOB_PRE_START state returns the right string. */
	TEST_FEATURE ("with pre-start state");
	name = job_state_name (JOB_PRE_START);

	TEST_EQ_STR (name, "pre-start");


	/* Check that the JOB_SPAWNED state returns the right string. */
	TEST_FEATURE ("with spawned state");
	name = job_state_name (JOB_SPAWNED);

	TEST_EQ_STR (name, "spawned");


	/* Check that the JOB_POST_START state returns the right string. */
	TEST_FEATURE ("with post-start state");
	name = job_state_name (JOB_POST_START);

	TEST_EQ_STR (name, "post-start");


	/* Check that the JOB_RUNNING state returns the right string. */
	TEST_FEATURE ("with running state");
	name = job_state_name (JOB_RUNNING);

	TEST_EQ_STR (name, "running");


	/* Check that the JOB_PRE_STOP state returns the right string. */
	TEST_FEATURE ("with pre-stop state");
	name = job_state_name (JOB_PRE_STOP);

	TEST_EQ_STR (name, "pre-stop");


	/* Check that the JOB_STOPPING state returns the right string. */
	TEST_FEATURE ("with stopping state");
	name = job_state_name (JOB_STOPPING);

	TEST_EQ_STR (name, "stopping");


	/* Check that the JOB_KILLED state returns the right string. */
	TEST_FEATURE ("with killed state");
	name = job_state_name (JOB_KILLED);

	TEST_EQ_STR (name, "killed");


	/* Check that the JOB_POST_STOP state returns the right string. */
	TEST_FEATURE ("with post-stop state");
	name = job_state_name (JOB_POST_STOP);

	TEST_EQ_STR (name, "post-stop");


	/* Check that the JOB_DELETED state returns the right string. */
	TEST_FEATURE ("with deleted state");
	name = job_state_name (JOB_DELETED);

	TEST_EQ_STR (name, "deleted");


	/* Check that an invalid state returns NULL. */
	TEST_FEATURE ("with invalid state");
	name = job_state_name (1234);

	TEST_EQ_P (name, NULL);
}

void
test_state_from_name (void)
{
	JobState state;

	TEST_FUNCTION ("job_state_from_name");

	/* Check that JOB_WAITING is returned for the right string. */
	TEST_FEATURE ("with waiting state");
	state = job_state_from_name ("waiting");

	TEST_EQ (state, JOB_WAITING);


	/* Check that JOB_STARTING is returned for the right string. */
	TEST_FEATURE ("with starting state");
	state = job_state_from_name ("starting");

	TEST_EQ (state, JOB_STARTING);


	/* Check that JOB_PRE_START is returned for the right string. */
	TEST_FEATURE ("with pre-start state");
	state = job_state_from_name ("pre-start");

	TEST_EQ (state, JOB_PRE_START);


	/* Check that JOB_SPAWNED is returned for the right string. */
	TEST_FEATURE ("with spawned state");
	state = job_state_from_name ("spawned");

	TEST_EQ (state, JOB_SPAWNED);


	/* Check that JOB_POST_START is returned for the right string. */
	TEST_FEATURE ("with post-start state");
	state = job_state_from_name ("post-start");

	TEST_EQ (state, JOB_POST_START);


	/* Check that JOB_RUNNING is returned for the right string. */
	TEST_FEATURE ("with running state");
	state = job_state_from_name ("running");

	TEST_EQ (state, JOB_RUNNING);


	/* Check that JOB_PRE_STOP is returned for the right string. */
	TEST_FEATURE ("with pre-stop state");
	state = job_state_from_name ("pre-stop");

	TEST_EQ (state, JOB_PRE_STOP);


	/* Check that JOB_STOPPING is returned for the right string. */
	TEST_FEATURE ("with stopping state");
	state = job_state_from_name ("stopping");

	TEST_EQ (state, JOB_STOPPING);


	/* Check that JOB_KILLED is returned for the right string. */
	TEST_FEATURE ("with killed state");
	state = job_state_from_name ("killed");

	TEST_EQ (state, JOB_KILLED);


	/* Check that JOB_POST_STOP is returned for the right string. */
	TEST_FEATURE ("with post-stop state");
	state = job_state_from_name ("post-stop");

	TEST_EQ (state, JOB_POST_STOP);


	/* Check that JOB_DELETED is returned for the right string. */
	TEST_FEATURE ("with deleted state");
	state = job_state_from_name ("deleted");

	TEST_EQ (state, JOB_DELETED);


	/* Check that -1 is returned for an invalid string. */
	TEST_FEATURE ("with invalid state");
	state = job_state_from_name ("wibble");

	TEST_EQ (state, -1);
}


void
test_process_name (void)
{
	const char *name;

	TEST_FUNCTION ("process_name");

	/* Check that PROCESS_MAIN returns the right string. */
	TEST_FEATURE ("with main process");
	name = process_name (PROCESS_MAIN);

	TEST_EQ_STR (name, "main");


	/* Check that PROCESS_PRE_START returns the right string. */
	TEST_FEATURE ("with pre-start process");
	name = process_name (PROCESS_PRE_START);

	TEST_EQ_STR (name, "pre-start");


	/* Check that PROCESS_POST_START returns the right string. */
	TEST_FEATURE ("with post-start process");
	name = process_name (PROCESS_POST_START);

	TEST_EQ_STR (name, "post-start");


	/* Check that PROCESS_PRE_STOP returns the right string. */
	TEST_FEATURE ("with pre-stop process");
	name = process_name (PROCESS_PRE_STOP);

	TEST_EQ_STR (name, "pre-stop");


	/* Check that PROCESS_POST_STOP returns the right string. */
	TEST_FEATURE ("with post-stop process");
	name = process_name (PROCESS_POST_STOP);

	TEST_EQ_STR (name, "post-stop");


	/* Check that an invalid process returns NULL. */
	TEST_FEATURE ("with invalid process");
	name = process_name (1234);

	TEST_EQ_P (name, NULL);
}

void
test_process_from_name (void)
{
	ProcessType process;

	TEST_FUNCTION ("process_from_name");

	/* Check that PROCESS_MAIN is returned for the string. */
	TEST_FEATURE ("with main process");
	process = process_from_name ("main");

	TEST_EQ (process, PROCESS_MAIN);


	/* Check that PROCESS_PRE_START is returned for the string. */
	TEST_FEATURE ("with pre-start process");
	process = process_from_name ("pre-start");

	TEST_EQ (process, PROCESS_PRE_START);


	/* Check that PROCESS_POST_START is returned for the string. */
	TEST_FEATURE ("with post-start process");
	process = process_from_name ("post-start");

	TEST_EQ (process, PROCESS_POST_START);


	/* Check that PROCESS_PRE_STOP is returned for the string. */
	TEST_FEATURE ("with pre-stop process");
	process = process_from_name ("pre-stop");

	TEST_EQ (process, PROCESS_PRE_STOP);


	/* Check that PROCESS_POST_STOP is returned for the string. */
	TEST_FEATURE ("with post-stop process");
	process = process_from_name ("post-stop");

	TEST_EQ (process, PROCESS_POST_STOP);


	/* Check that -1 is returned for an invalid string. */
	TEST_FEATURE ("with invalid process");
	process = process_from_name ("wibble");

	TEST_EQ (process, -1);
}


int
main (int   argc,
      char *argv[])
{
	test_goal_name ();
	test_goal_from_name ();
	test_state_name ();
	test_state_from_name ();
	test_process_name ();
	test_process_from_name ();

	return 0;
}
