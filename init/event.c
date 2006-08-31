/* upstart
 *
 * event.c - event queue and handling
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
#include <nih/main.h>
#include <nih/logging.h>

#include "event.h"
#include "job.h"
#include "control.h"


/**
 * paused:
 *
 * Do not process the event queue or detect idle jobs while this is %TRUE.
 **/
int paused = FALSE;


/**
 * events:
 *
 * This list holds the list of events queued to be handled; each entry
 * is an Event structure.
 **/
static NihList *events = NULL;


/**
 * event_init:
 *
 * Initialise the event queue list.
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
 * does not place it in the event queue.  Use when a lone Event structure
 * is needed, such as for matching events.
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

	return event;
}

/**
 * event_match:
 * @event1: first event,
 * @event2: second event.
 *
 * Compares @event1 and @event2 to see whether they are identical in name.
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

	return TRUE;
}


/**
 * event_queue:
 * @name: name of event to queue.
 *
 * Queues an event called @name, which will be handled in the main loop
 * when @event_queue_run is called.
 *
 * Returns: event structure in the queue.
 **/
Event *
event_queue (const char *name)
{
	Event *event;

	nih_assert (name != NULL);
	nih_assert (strlen (name) > 0);

	event_init ();

	NIH_MUST (event = event_new (NULL, name));
	nih_alloc_set_destructor (event, (NihDestructor)nih_list_destructor);
	nih_list_add (events, &event->entry);

	return event;
}

/**
 * event_queue_run:
 *
 * This callback is called once during each iteration of the main loop.
 * It consumes all events in the queue and ensures that subscribed processes
 * are notified of them and jobs listening for them are handled.
 **/
void
event_queue_run (void)
{
	if (paused)
		return;


	event_init ();

	while (! NIH_LIST_EMPTY (events)) {
		NIH_LIST_FOREACH_SAFE (events, iter) {
			Event *event = (Event *)iter;

			control_handle_event (event);
			job_handle_event (event);

			nih_list_free (&event->entry);
		}
	}
}
