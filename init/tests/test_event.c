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

	/* Value should be NULL */
	if (event->value != NULL) {
		printf ("BAD: value wasn't what we expected.\n");
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
test_record (void)
{
	Event *event1, *event2;
	int    ret = 0;

	printf ("Testing event_record()\n");

	printf ("...with new event name\n");
	event1 = event_record (NULL, "test");

	/* Name should be that given */
	if (strcmp (event1->name, "test")) {
		printf ("BAD: name wasn't what we expected.\n");
		ret = 1;
	}

	/* Value should be NULL */
	if (event1->value != NULL) {
		printf ("BAD: value wasn't what we expected.\n");
		ret = 1;
	}

	/* Entry should be added to events list */
	if (NIH_LIST_EMPTY (&event1->entry)) {
		printf ("BAD: entry was not placed in history list.\n");
		ret = 1;
	}

	/* Should have been allocated with nih_alloc */
	if (nih_alloc_size (event1) != sizeof (Event)) {
		printf ("BAD: nih_alloc was not used.\n");
		ret = 1;
	}

	/* Name should be nih_alloc child of object */
	if (nih_alloc_parent (event1->name) != event1) {
		printf ("BAD: nih_alloc parent of name wasn't object.\n");
		ret = 1;
	}


	printf ("...with existing event name\n");
	event2 = event_record (NULL, "test");

	/* First event should be returned */
	if (event2 != event1) {
		printf ("BAD: expected first event to be returned.\n");
		ret = 1;
	}


	nih_list_free (&event1->entry);

	return ret;
}

int
test_find_by_name (void)
{
	Event *event1, *event2, *event3, *ptr;
	int    ret = 0;

	printf ("Testing event_find_by_name()\n");
	event1 = event_record (NULL, "foo");
	event2 = event_record (NULL, "bar");
	event3 = event_record (NULL, "baz");

	printf ("...with name we expect to find\n");
	ptr = event_find_by_name ("bar");

	/* Pointer returned should be to event with that name */
	if (ptr != event2) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with name we do not expect to find\n");
	ptr = event_find_by_name ("frodo");

	/* Pointer returned should be NULL */
	if (ptr != NULL) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with empty event list\n");
	nih_list_free (&event3->entry);
	nih_list_free (&event2->entry);
	nih_list_free (&event1->entry);
	ptr = event_find_by_name ("bar");

	/* Pointer returned should be NULL */
	if (ptr != NULL) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}

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


	printf ("...with same edge events\n");
	nih_free (event2);
	event2 = event_new (NULL, "foo");

	/* Should match */
	if (! event_match (event1, event2)) {
		printf ("BAD: events did not match.\n");
		ret = 1;
	}


	printf ("...with edge event matching against level\n");
	event1->value = "test";

	/* Should match */
	if (! event_match (event1, event2)) {
		printf ("BAD: events did not match.\n");
		ret = 1;
	}


	printf ("...with same level events\n");
	event2->value = "test";

	/* Should match */
	if (! event_match (event1, event2)) {
		printf ("BAD: events did not match.\n");
		ret = 1;
	}


	printf ("...with different level events\n");
	event1->value = "wibble";

	/* Should not match */
	if (event_match (event1, event2)) {
		printf ("BAD: events matched unexpectedly.\n");
		ret = 1;
	}


	printf ("...with level event matching against edge\n");
	event1->value = NULL;

	/* Should not match */
	if (event_match (event1, event2)) {
		printf ("BAD: events matched unexpectedly.\n");
		ret = 1;
	}


	nih_free (event2);
	nih_free (event1);

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
test_change_value (void)
{
	Event *event;
	int    ret = 0;

	printf ("Testing event_change_value()\n");
	event = event_record (NULL, "test");


	printf ("...with no previous value\n");
	event_change_value (event, "foo");

	/* Value should be that set */
	if (strcmp (event->value, "foo")) {
		printf ("BAD: event value wasn't what we expected.\n");
		ret = 1;
	}

	/* Value should be nih_alloc child of event */
	if (nih_alloc_parent (event->value) != event) {
		printf ("BAD: value not nih_alloc child of event.\n");
		ret = 1;
	}


	printf ("...with previous value\n");
	nih_alloc_set_destructor (event->value, destructor_called);
	was_called = 0;
	event_change_value (event, "bar");

	/* Value should be new one set */
	if (strcmp (event->value, "bar")) {
		printf ("BAD: event value wasn't what we expected.\n");
		ret = 1;
	}

	/* Value should be nih_alloc child of event */
	if (nih_alloc_parent (event->value) != event) {
		printf ("BAD: value not nih_alloc child of event.\n");
		ret = 1;
	}

	/* Original value should have been freed */
	if (! was_called) {
		printf ("BAD: event value was not freed.\n");
		ret = 1;
	}

	nih_list_free (&event->entry);

	return ret;
}


int
test_queue_edge (void)
{
	Event *event, *queued;
	int    ret = 0;

	printf ("Testing event_queue_edge()\n");

	printf ("...with event not previously recorded.\n");
	queued = event_queue_edge ("test");
	event = event_find_by_name ("test");

	/* Queued name should be set */
	if (strcmp (queued->name, "test")) {
		printf ("BAD: queued name wasn't what we expected.\n");
		ret = 1;
	}

	/* Queued value should be NULL */
	if (queued->value != NULL) {
		printf ("BAD: queued value wasn't what we expected.\n");
		ret = 1;
	}

	/* Should be able to find event in history */
	if (event == NULL) {
		printf ("BAD: event not recorded in history.\n");
		ret = 1;
	}

	/* Event name should be set */
	if (strcmp (event->name, "test")) {
		printf ("BAD: event name wasn't what we expected.\n");
		ret = 1;
	}

	/* Event value should be NULL */
	if (event->value != NULL) {
		printf ("BAD: event value wasn't what we expected.\n");
		ret = 1;
	}

	nih_list_free (&event->entry);


	printf ("...with event previously recorded with value.\n");
	event = event_record (NULL, "foo");
	event_change_value (event, "bar");
	nih_alloc_set_destructor (event->value, destructor_called);
	was_called = 0;
	queued = event_queue_edge ("foo");

	/* Queued name should be set */
	if (strcmp (queued->name, "foo")) {
		printf ("BAD: queued name wasn't what we expected.\n");
		ret = 1;
	}

	/* Queued value should be NULL */
	if (queued->value != NULL) {
		printf ("BAD: queued value wasn't what we expected.\n");
		ret = 1;
	}

	/* Event value should be the same */
	if (strcmp (event->value, "bar")) {
		printf ("BAD: event value changed unexpectedly.\n");
		ret = 1;
	}

	/* Value should not have been freed */
	if (was_called) {
		printf ("BAD: event value freed unexpectedly.\n");
		ret = 1;
	}

	nih_list_free (&event->entry);

	return ret;
}

int
test_queue_level (void)
{
	Event *event, *queued;
	int    ret = 0;

	printf ("Testing event_queue_level()\n");

	printf ("...with event not previously recorded.\n");
	queued = event_queue_level ("test", "down");
	event = event_find_by_name ("test");

	/* Queued name should be set */
	if (strcmp (queued->name, "test")) {
		printf ("BAD: queued name wasn't what we expected.\n");
		ret = 1;
	}

	/* Queued value should be set */
	if (strcmp (queued->value, "down")) {
		printf ("BAD: queued value wasn't what we expected.\n");
		ret = 1;
	}

	/* Should be able to find event in history */
	if (event == NULL) {
		printf ("BAD: event not recorded in history.\n");
		ret = 1;
	}

	/* Event name should be set */
	if (strcmp (event->name, "test")) {
		printf ("BAD: event name wasn't what we expected.\n");
		ret = 1;
	}

	/* Event value should be set */
	if (strcmp (event->value, "down")) {
		printf ("BAD: event value wasn't what we expected.\n");
		ret = 1;
	}


	printf ("...with no change to level.\n");
	nih_alloc_set_destructor (event->value, destructor_called);
	was_called = 0;
	queued = event_queue_level ("test", "down");

	/* Queued should be NULL */
	if (queued != NULL) {
		printf ("BAD: event queued unexpectedly.\n");
		ret = 1;
	}

	/* Value should be that previously set */
	if (strcmp (event->value, "down")) {
		printf ("BAD: event value changed unexpectedly.\n");
		ret = 1;
	}

	/* Value should not have been freed */
	if (was_called) {
		printf ("BAD: event value freed unexpectedly.\n");
		ret = 1;
	}


	printf ("...with change to level.\n");
	was_called = 0;
	queued = event_queue_level ("test", "up");

	/* Queued value should be the new one */
	if (strcmp (queued->value, "up")) {
		printf ("BAD: queued value changed unexpectedly.\n");
		ret = 1;
	}

	/* Value should be the new one */
	if (strcmp (event->value, "up")) {
		printf ("BAD: event value changed unexpectedly.\n");
		ret = 1;
	}

	/* Value should have been freed */
	if (! was_called) {
		printf ("BAD: event value was not freed.\n");
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
	ret |= test_record ();
	ret |= test_find_by_name ();
	ret |= test_match ();
	ret |= test_change_value ();
	ret |= test_queue_edge ();
	ret |= test_queue_level ();

	return ret;
}
