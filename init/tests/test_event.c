/* upstart
 *
 * test_event.c - test suite for init/event.c
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

#include "event.h"


int
test_new (void)
{
	Event *event;
	int    ret = 0;

	printf ("Testing event_new()\n");
	event = event_new (NULL, "test");

	/* Name should be that given */
	if (strcmp (event->name, "test")) {
		printf ("BAD: name wasn't what we expected.\n");
		ret = 1;
	}

	/* List entry header should be empty */
	if (! NIH_LIST_EMPTY (&event->entry)) {
		printf ("BAD: entry was placed in a list.\n");
		ret = 1;
	}

	/* Should have been allocated with nih_alloc */
	if (nih_alloc_size (event) != sizeof (Event)) {
		printf ("BAD: nih_alloc was not used.\n");
		ret = 1;
	}

	/* Name should be nih_alloc child of object */
	if (nih_alloc_parent (event->name) != event) {
		printf ("BAD: nih_alloc parent of name wasn't object.\n");
		ret = 1;
	}

	nih_list_free (&event->entry);

	return ret;
}

int
test_match (void)
{
	Event *event1, *event2;
	int    ret = 0;

	printf ("Testing event_match()\n");

	printf ("...with different name events\n");
	event1 = event_new (NULL, "foo");
	event2 = event_new (NULL, "bar");

	/* Should not match */
	if (event_match (event1, event2)) {
		printf ("BAD: events matched unexpectedly.\n");
		ret = 1;
	}


	printf ("...with same name events\n");
	nih_free (event2);
	event2 = event_new (NULL, "foo");

	/* Should match */
	if (! event_match (event1, event2)) {
		printf ("BAD: events did not match.\n");
		ret = 1;
	}


	nih_free (event2);
	nih_free (event1);

	return ret;
}

int
test_queue (void)
{
	Event *event;
	int    ret = 0;

	printf ("Testing event_queue()\n");
	event = event_queue ("test");

	/* Name should be set */
	if (strcmp (event->name, "test")) {
		printf ("BAD: queued name wasn't what we expected.\n");
		ret = 1;
	}

	nih_list_free (&event->entry);

	return ret;
}


int
main (int   argc,
      char *argv[])
{
	int ret = 0;

	ret |= test_new ();
	ret |= test_match ();
	ret |= test_queue ();

	return ret;
}
