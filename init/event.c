/* upstart
 *
 * event.c - event queue and handling
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <string.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/logging.h>
#include <nih/error.h>

#include "event.h"
#include "job.h"
#include "errors.h"


/* Prototypes for static functions */
static void event_pending  (Event *event);
static void event_finished (Event *event);


/**
 * paused:
 *
 * Do not process the event queue while this is TRUE.
 **/
int paused = FALSE;

/**
 * events:
 *
 * This list holds the list of events in the process of pending, being
 * handled or awaiting cleanup; each item is an Event structure.
 **/
NihList *events = NULL;


/**
 * event_init:
 *
 * Initialise the event list.
 **/
void
event_init (void)
{
	if (! events)
		NIH_MUST (events = nih_list_new (NULL));
}


/**
 * event_new:
 * @parent: parent of new event,
 * @name: name of event to emit,
 * @env: NULL-terminated array of environment variables for event.
 *
 * Allocates an Event structure for the event details given and
 * appends it to the queue of events.
 *
 * @env is optional, and may be NULL; if given it should be a NULL-terminated
 * array of environment variables in KEY=VALUE form.  The @env array itself
 * will be reparented to the event structure and should not be modified after
 * the call.
 *
 * When the event reaches the top of the queue, it is taken off and placed
 * into the handling queue.  It is not removed from that queue until there
 * are no longer anything referencing it.
 *
 * The event is created with nothing blocking it.  Be sure to call
 * event_block() otherwise it will be automatically freed next time
 * through the main loop.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.
 *
 * Returns: new Event structure pending in the queue.
 **/
Event *
event_new (const void  *parent,
	   const char  *name,
	   char      **env)
{
	Event *event;

	nih_assert (name != NULL);
	nih_assert (strlen (name) > 0);

	event_init ();

	NIH_MUST (event = nih_new (parent, Event));

	nih_list_init (&event->entry);

	event->progress = EVENT_PENDING;
	event->failed = FALSE;

	event->blockers = 0;

	nih_alloc_set_destructor (event, (NihDestructor)nih_list_destroy);


	/* Fill in the event details */
	NIH_MUST (event->name = nih_strdup (event, name));

	event->env = env;
	if (event->env)
		nih_alloc_reparent (event->env, event);


	/* Place it in the pending list */
	nih_debug ("Pending %s event", name);
	nih_list_add (events, &event->entry);

	return event;
}


/**
 * event_block:
 * @emission: event to block.
 *
 * This function should be called by jobs that wish to hold a reference on
 * the event and block it from finishing.
 *
 * Once the reference is no longer needed, you must call event_unblock()
 * to allow the event to be finished, and potentially freed.
 **/
void
event_block (Event *event)
{
	nih_assert (event != NULL);

	event->blockers++;
}

/**
 * event_unblock:
 * @event: event to unblock.
 *
 * This function should be called by jobs that are holding a reference on the
 * event which blocks it from finishing, and wish to discard that reference.
 *
 * It must match a previous call to event_block().
 **/
void
event_unblock (Event *event)
{
	nih_assert (event != NULL);
	nih_assert (event->blockers > 0);

	event->blockers--;
}


/**
 * event_poll:
 *
 * This function is used to process the list of events; any in the pending
 * state are moved into the handling state and job states changed.  Any
 * in the finished state will have subscribers and jobs notified that the
 * event has completed.
 *
 * Events remain in the handling state while they have blocking jobs.
 *
 * This function will only return once the events list is empty, or all
 * events are in the handling state; so any time an event queues another,
 * it will be processed immediately.
 *
 * Normally this function is used as a main loop callback.
 **/
void
event_poll (void)
{
	int poll_again;

	if (paused)
		return;

	event_init ();

	do {
		poll_again = FALSE;

		NIH_LIST_FOREACH_SAFE (events, iter) {
			Event *event = (Event *)iter;

			/* Ignore events that we're handling and are
			 * blocked, there's nothing we can do to hurry them.
			 *
			 * Decide whether to poll again based on the state
			 * before handling the event; that way we always loop
			 * at least once more after finding a pending or
			 * finished event, in case they added new events as
			 * a side effect that we missed.
			 */
			switch (event->progress) {
			case EVENT_PENDING:
				event_pending (event);
				poll_again = TRUE;

				/* fall through */
			case EVENT_HANDLING:
				if (event->blockers)
					break;

				event->progress = EVENT_FINISHED;
				/* fall through */
			case EVENT_FINISHED:
				event_finished (event);
				poll_again = TRUE;
				break;
			default:
				nih_assert_not_reached ();
			}
		}
	} while (poll_again);
}

/**
 * event_pending:
 * @event: pending event.
 *
 * This function is called for each event in the list that is in the pending
 * state.  Subscribers to emitted events are notified, and the event is
 * passed to the job system to start or stop any.
 *
 * The event is marked as handling; if no jobs took it, then it is
 * immediately finished.
 **/
static void
event_pending (Event *event)
{
	nih_assert (event != NULL);
	nih_assert (event->progress == EVENT_PENDING);

	nih_info (_("Handling %s event"), event->name);
	event->progress = EVENT_HANDLING;

	job_handle_event (event);
}

/**
 * event_finished:
 * @event: finished event.
 *
 * This function is called for each event in the list that is in the finished
 * state.  Subscribers and jobs are notified, then, if the event failed, a
 * new pending failed event is queued.  Finally the event is freed and
 * removed from the list.
 **/
static void
event_finished (Event *event)
{
	nih_assert (event != NULL);
	nih_assert (event->progress == EVENT_FINISHED);

	nih_debug ("Finished %s event", event->name);

	job_handle_event_finished (event);

	if (event->failed) {
		char *name;

		name = strrchr (event->name, '/');
		if ((! name) || strcmp (name, "/failed")) {
			Event *new_event;

			NIH_MUST (name = nih_sprintf (NULL, "%s/failed",
						      event->name));
			new_event = event_new (NULL, name, NULL);
			nih_free (name);

			if (event->env)
				NIH_MUST (new_event->env = nih_str_array_copy (
						  new_event, NULL, event->env));
		}
	}

	nih_free (event);
}
