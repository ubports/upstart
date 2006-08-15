/* upstart
 *
 * event.c - event triggering and tracking
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <string.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/logging.h>

#include "event.h"


/**
 * events:
 *
 * This list holds the list the currently known value of all level events
 * and the history of edge events triggered so far; each entry is of the
 * Event structure.
 **/
static NihList *events = NULL;


/**
 * event_init:
 *
 * Initialise the event history list.
 **/
static inline void
event_init (void)
{
	if (! events)
		NIH_MUST (events = nih_list_new ());
}


/**
 * event_new:
 * @parent: parent of new event,
 * @name: name of new event.
 *
 * Allocates and returns a new Event structure with the @name given, but
 * does not record it in the history list.  This is used when a lone
 * Dvent structure is needed, such as for matching events.
 *
 * The value of the event is initialised to %NULL.
 *
 * Returns: newly allocated Event structure or %NULL if insufficient memory.
 **/
Event *
event_new (void       *parent,
	   const char *name)
{
	Event *event;

	nih_assert (name != NULL);
	nih_assert (strlen (name) > 0);

	event = nih_new (parent, Event);
	if (! event)
		return NULL;

	nih_list_init (&event->entry);

	event->name = nih_strdup (event, name);
	if (! event->name) {
		nih_free (event);
		return NULL;
	}

	event->value = NULL;

	return event;
}

/**
 * event_record:
 * @parent: parent of new event,
 * @name: name of new event.
 *
 * Checks whether @name already exists in the history list and either
 * allocates and returns a new Event structure with the @name given
 * (which is added to the list) or returns the existing entry.
  *
 * The value of the event is initialised to %NULL.
 *
 * Returns: newly allocated Event structure or %NULL if insufficient memory.
 **/
Event *
event_record (void       *parent,
	      const char *name)
{
	Event *event;

	nih_assert (name != NULL);
	nih_assert (strlen (name) > 0);

	event_init ();

	event = event_find_by_name (name);
	if (! event) {
		event = event_new (parent, name);
		if (event)
			nih_list_add (events, &event->entry);
	}

	return event;
}


/**
 * event_find_by_name:
 * @name: name of event.
 *
 * Finds an event with the given @name in the history list; this can be
 * used both to find out whether a particular event has ever been recorded
 * (return value is not %NULL) or what the current value of a level event
 * is (event->value is not %NULL).
 *
 * Returns: event record found or %NULL if never recorded.
 **/
Event *
event_find_by_name (const char *name)
{
	Event *event;

	nih_assert (name != NULL);

	event_init ();

	NIH_LIST_FOREACH (events, iter) {
		event = (Event *)iter;

		if (! strcmp (event->name, name))
			return event;
	}

	return NULL;
}

/**
 * event_match:
 * @event1: first event,
 * @event2: second event.
 *
 * Compares @event1 and @event2 to see whether they are identical in name
 * and value, or both names match and @event2's value is NULL.
 *
 * Returns: TRUE if the events match, FALSE otherwise.
 **/
int
event_match (Event *event1,
	     Event *event2)
{
	nih_assert (event1 != NULL);
	nih_assert (event2 != NULL);

	/* Names must match */
	if (strcmp (event1->name, event2->name))
		return FALSE;

	/* Special case: an edge event matches any level */
	if (event2->value == NULL)
		return TRUE;

	/* A level event does not match a level event however */
	if (event1->value == NULL)
		return FALSE;

	/* Otherwise values must match */
	if (strcmp (event1->value, event2->value))
		return FALSE;

	return TRUE;
}


/**
 * event_change_value:
 * @event: event to change,
 * @value: new value.
 *
 * This function changes the value of the given level @event to the new
 * @value given.  If no value has been previously recorded, or @value
 * is different from that currently set, this triggers the event.
 *
 * Returns: zero on success, negative value on insufficient memory.
 **/
int
event_change_value (Event      *event,
		    const char *value)
{
	nih_assert (event != NULL);
	nih_assert (value != NULL);
	nih_assert (strlen (value) > 0);

	if (event->value)
		nih_free (event->value);

	event->value = nih_strdup (event, value);
	nih_debug ("%s event level changed to %s", event->name, event->value);

	return 0;
}


/**
 * event_trigger_edge:
 * @name: name of event to trigger.
 *
 * Triggers an edge event called @name, recording it in the history of events
 * that have previously been triggered.
 **/
void
event_trigger_edge (const char *name)
{
	Event *event;

	nih_assert (name != NULL);
	nih_assert (strlen (name) > 0);

	NIH_MUST (event = event_record (NULL, name));

	nih_info (_("%s event triggered"), event->name);
}

/**
 * event_trigger_level:
 * @name: name of event to trigger,
 * @level: level to trigger at.
 *
 * Changes the level of the event called @name to @level, and if different to
 * that before triggers the level event and edge event, recording it in the
 * history of events.
 **/
void
event_trigger_level (const char *name,
		     const char *level)
{
	Event *event;

	nih_assert (name != NULL);
	nih_assert (strlen (name) > 0);
	nih_assert (level != NULL);
	nih_assert (strlen (level) > 0);

	NIH_MUST (event = event_record (NULL, name));

	if (event->value && (! strcmp (event->value, level))) {
		nih_debug ("%s event level unchanged (%s)",
			   event->name, event->value);
		return;
	}

	NIH_MUST (event_change_value (event, level) == 0);

	nih_info (_("%s %s event triggered"), event->name, event->value);
}
