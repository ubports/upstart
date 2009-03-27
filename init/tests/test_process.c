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

#include <nih/macros.h>
#include <nih/alloc.h>

#include "process.h"


void
test_new (void)
{
	Process *process;

	/* Check that we can create a new Process structure; the structure
	 * should be allocated with nih_alloc and have sensible defaults.
	 */
	TEST_FUNCTION ("process_new");
	TEST_ALLOC_FAIL {
		process = process_new (NULL);

		if (test_alloc_failed) {
			TEST_EQ_P (process, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (process, sizeof (Process));

		TEST_EQ (process->script, FALSE);
		TEST_EQ_P (process->command, NULL);

		nih_free (process);
	}
}


void
test_name (void)
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
test_from_name (void)
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

	TEST_EQ (process, (ProcessType)-1);
}


int
main (int   argc,
      char *argv[])
{
	test_new ();

	test_name ();
	test_from_name ();

	return 0;
}
