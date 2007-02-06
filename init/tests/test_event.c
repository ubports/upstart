/* upstart
 *
 * test_event.c - test suite for init/event.c
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
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/string.h>

#include "event.h"


void
test_new (void)
{
	Event *event;

	/* Check that we can create a new Event structure, and have the
	 * details filled in and returned.  It should not be placed in
	 * any kind of list.
	 */
	TEST_FUNCTION ("event_new");
	event_queue_run ();
	TEST_ALLOC_FAIL {
		event = event_new (NULL, "test");

		if (test_alloc_failed) {
			TEST_EQ_P (event, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_EMPTY (&event->entry);
		TEST_EQ_STR (event->name, "test");
		TEST_ALLOC_PARENT (event->name, event);

		TEST_EQ_P (event->args, NULL);
		TEST_EQ_P (event->env, NULL);

		nih_list_free (&event->entry);
	}
}

void
test_match (void)
{
	Event *event1, *event2;
	char  *args1[] = { "foo", "bar", "baz", NULL };
	char  *args2[] = { "foo", "bar", "baz", NULL };

	TEST_FUNCTION ("event_match");

	/* Check that two events with different names do not match. */
	TEST_FEATURE ("with different name events");
	event1 = event_new (NULL, "foo");
	event2 = event_new (NULL, "bar");

	TEST_FALSE (event_match (event1, event2));


	/* Check that two events with the same names match. */
	TEST_FEATURE ("with same name events");
	nih_free (event2);
	event2 = event_new (NULL, "foo");

	TEST_TRUE (event_match (event1, event2));


	/* Check that two events with the same arguments lists match. */
	TEST_FEATURE ("with same argument lists");
	event1->args = args1;
	event2->args = args2;

	TEST_TRUE (event_match (event1, event2));


	/* Check that the argument list in event2 may be shorter. */
	TEST_FEATURE ("with shorter list in event2");
	args2[2] = NULL;

	TEST_TRUE (event_match (event1, event2));


	/* Check that the argument list in event1 may not be shorter. */
	TEST_FEATURE ("with shorter list in event1");
	args2[2] = args1[2];
	args1[2] = NULL;

	TEST_FALSE (event_match (event1, event2));


	/* Check that the second events argument lists may be globs. */
	TEST_FEATURE ("with globs in arguments");
	args1[2] = args2[2];
	args2[2] = "b?z*";

	TEST_TRUE (event_match (event1, event2));


	nih_free (event2);
	nih_free (event1);
}

void
test_queue (void)
{
	Event *event;

	/* Check that an event can be queued, the structure returned should
	 * be allocated with nih_alloc and placed in a list.
	 */
	TEST_FUNCTION ("event_queue");
	TEST_ALLOC_FAIL {
		event = event_queue ("test");

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);
		TEST_EQ_STR (event->name, "test");
		TEST_ALLOC_PARENT (event->name, event);

		nih_list_free (&event->entry);
	}
}


void
test_read_state (void)
{
	Event *event, *ptr;
	char   buf[80];

	TEST_FUNCTION ("event_read_state");

	/* Check that an event can be created from a text state that contains
	 * the name, and queued automatically.
	 */
	TEST_FEATURE ("with event name");
	TEST_ALLOC_FAIL {
		sprintf (buf, "Event bang");
		event = event_read_state (NULL, buf);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);
		TEST_EQ_STR (event->name, "bang");
		TEST_ALLOC_PARENT (event->name, event);

		nih_list_free (&event->entry);
	}


	/* Check that an event in the buffer can contain arguments, which
	 * are appended to the event.
	 */
	TEST_FEATURE ("with argument to event");
	event = event_queue ("foo");
	TEST_ALLOC_FAIL {
		sprintf (buf, ".arg frodo");
		ptr = event_read_state (event, buf);

		TEST_EQ_P (ptr, event);
		TEST_ALLOC_PARENT (event->args, event);
		TEST_ALLOC_SIZE (event->args, sizeof (char *) * 2);
		TEST_ALLOC_PARENT (event->args[0], event->args);
		TEST_EQ_STR (event->args[0], "frodo");
		TEST_EQ_P (event->args[1], NULL);

		nih_free (event->args);
		event->args = NULL;
	}


	/* Check that an event in the buffer can contain environment, which
	 * are appended to the event.
	 */
	TEST_FEATURE ("with environment for event");
	TEST_ALLOC_FAIL {
		sprintf (buf, ".env FOO=BAR");
		ptr = event_read_state (event, buf);

		TEST_EQ_P (ptr, event);
		TEST_ALLOC_PARENT (event->env, event);
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 2);
		TEST_ALLOC_PARENT (event->env[0], event->env);
		TEST_EQ_STR (event->env[0], "FOO=BAR");
		TEST_EQ_P (event->env[1], NULL);

		nih_free (event->env);
		event->env = NULL;
	}

	nih_list_free (&event->entry);
}

void
test_write_state (void)
{
	FILE  *output;
	Event *event1, *event2, *event3;

	/* Check that the state of the event queue can be written out to
	 * a file descriptor.
	 */
	TEST_FUNCTION ("event_write_state");
	event1 = event_queue ("frodo");

	event2 = event_queue ("bilbo");
	NIH_MUST (nih_str_array_add (&event2->args, event2, NULL, "foo"));
	NIH_MUST (nih_str_array_add (&event2->args, event2, NULL, "bar"));

	event3 = event_queue ("drogo");
	NIH_MUST (nih_str_array_add (&event3->args, event3, NULL, "baggins"));
	NIH_MUST (nih_str_array_add (&event3->env, event3, NULL, "FOO=BAR"));
	NIH_MUST (nih_str_array_add (&event3->env, event3, NULL, "TEA=YES"));

	output = tmpfile ();
	event_write_state (output);
	rewind (output);

	TEST_FILE_EQ (output, "Event frodo\n");
	TEST_FILE_EQ (output, "Event bilbo\n");
	TEST_FILE_EQ (output, ".arg foo\n");
	TEST_FILE_EQ (output, ".arg bar\n");
	TEST_FILE_EQ (output, "Event drogo\n");
	TEST_FILE_EQ (output, ".arg baggins\n");
	TEST_FILE_EQ (output, ".env FOO=BAR\n");
	TEST_FILE_EQ (output, ".env TEA=YES\n");
	TEST_FILE_END (output);

	fclose (output);

	nih_list_free (&event1->entry);
	nih_list_free (&event2->entry);
	nih_list_free (&event3->entry);
}


int
main (int   argc,
      char *argv[])
{
	test_new ();
	test_match ();
	test_queue ();
	test_read_state ();
	test_write_state ();

	return 0;
}
