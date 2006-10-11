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


#include <stdio.h>
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
 * Do not process the event queue or detect idle jobs while this is TRUE.
 **/
int paused = FALSE;


/**
 * events:
 *
 * This list holds the list of events queued to be handled; each item
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
		NIH_MUST (events = nih_list_new (NULL));
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
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * Returns: newly allocated Event structure or NULL if insufficient memory.
 **/
Event *
event_new (const void *parent,
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

	nih_debug ("Queued %s event", name);

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

			nih_debug ("Handling %s event", event->name);
			control_handle_event (event);
			job_handle_event (event);

			nih_list_free (&event->entry);
		}
	}
}


/**
 * event_read_state:
 * @event: event to update,
 * @buf: serialised state.
 *
 * Parse the serialised state and update the event queue if we recognise
 * the line.  We need to always retain knowledge of this so we can always
 * be re-exec'd by an earlier version of init.  That's why this is so
 * trivial.
 *
 * @event may be NULL if @buf begins "Event "
 **/
Event *
event_read_state (Event *event,
		  char  *buf)
{
	char *ptr;

	nih_assert (buf != NULL);

	/* Every line must have a space, which splits the key and value */
	ptr = strchr (buf, ' ');
	if (ptr) {
		*(ptr++) = '\0';
	} else {
		return event;
	}

	/* Handle the case where we don't have a event yet first */
	if (! event) {
		if (strcmp (buf, "Event"))
			return event;

		/* Add a new event record */
		NIH_MUST (event = event_queue (ptr));
		return event;
	}

	/* No attributes on events yet */

	return event;
}

/**
 * event_write_state:
 * @state: file to write to.
 *
 * This is the companion function to event_read_state(), it writes to @state
 * lines for each event in the queue.
 **/
void
event_write_state (FILE *state)
{
	nih_assert (state != NULL);

	NIH_LIST_FOREACH (events, iter) {
		Event *event = (Event *)iter;

		fprintf (state, "Event %s\n", event->name);
	}
}
