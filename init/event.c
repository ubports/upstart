/* upstart
 *
 * event.c - event queue and handling
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/main.h>
#include <nih/logging.h>

#include "event.h"
#include "job.h"
#include "notify.h"


/* Prototypes for static functions */
static void event_pending  (EventEmission *emission);
static void event_finished (EventEmission *emission);


/**
 * paused:
 *
 * Do not process the event queue or detect idle jobs while this is TRUE.
 **/
int paused = FALSE;


/**
 * emission_id
 *
 * This counter is used to assign unique emission ids and is incremented
 * each time we use it.  After a while (4 billion events) it'll wrap over,
 * so always check an id isn't taken.
 **/
static uint32_t emission_id = 0;

/**
 * events:
 *
 * This list holds the list of events in the process of pending, being
 * handled or awaiting cleanup; each item is an EventEmission structure.
 **/
static NihList *events = NULL;


/**
 * event_init:
 *
 * Initialise the event list.
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

	event->args = NULL;
	event->env = NULL;

	return event;
}

/**
 * event_match:
 * @event1: first event,
 * @event2: second event.
 *
 * Compares @event1 and @event2 to see whether they are identical in name,
 * and whether @event1 contains at least the number of arguments given in
 * @event2, and that each of those arguments matches as a glob.
 *
 * Returns: TRUE if the events match, FALSE otherwise.
 **/
int
event_match (Event *event1,
	     Event *event2)
{
	char **arg1, **arg2;

	nih_assert (event1 != NULL);
	nih_assert (event2 != NULL);

	/* Names must match */
	if (strcmp (event1->name, event2->name))
		return FALSE;

	/* Match arguments using the second argument as a glob */
	for (arg1 = event1->args, arg2 = event2->args;
	     arg1 && arg2 && *arg1 && *arg2; arg1++, arg2++)
		if (fnmatch (*arg2, *arg1, 0))
			return FALSE;

	/* Must be at least the same number of arguments in event1 as
	 * there are in event2
	 */
	if (arg2 && *arg2)
		return FALSE;

	return TRUE;
}


/**
 * event_emit:
 * @name: name of event to emit,
 * @args: arguments to event,
 * @env: environment for event,
 * @callback: function to call once handled,
 * @data: pointer to pass to @callback.
 *
 * Allocates an EventEmission structure for the event details given and
 * appends it to the queue of events pending emission.
 *
 * Both @args and @env are optional, and may be NULL; if they are given,
 * then the array itself it reparented to belong to the emission structure
 * and should not be modified.
 *
 * When the event reaches the top of the queue, it is taken off and placed
 * into the handling queue.  It is not removed from that queue until there
 * are no longer any jobs referencing the event.  At that point, @callback
 * is called, before the event is freed.
 *
 * Returns: new EventEmission structure in the pending queue.
 **/
EventEmission *
event_emit (const char       *name,
	    char            **args,
	    char            **env,
	    EventEmissionCb   callback,
	    void             *data)
{
	EventEmission *emission;

	nih_assert (name != NULL);
	nih_assert (strlen (name) > 0);

	event_init ();

	NIH_MUST (emission = nih_new (NULL, EventEmission));

	/* Use incrementing ids, skipping any that are still in use
	 * (in practice, we shouldn't reach 4 billion events, but better
	 * safe than sorry.
	 */
	while (event_emit_find_by_id (emission_id))
		emission_id++;

	emission->id = emission_id++;
	emission->progress = EVENT_PENDING;

	emission->jobs = 0;
	emission->failed = FALSE;

	emission->callback = callback;
	emission->data = data;


	/* Fill in the event details */
	NIH_MUST (emission->event.name = nih_strdup (emission, name));

	emission->event.args = args;
	if (emission->event.args)
		nih_alloc_reparent (emission->event.args, emission);

	emission->event.env = env;
	if (emission->event.env)
		nih_alloc_reparent (emission->event.env, emission);


	/* Place it in the pending list */
	nih_list_init (&emission->event.entry);
	nih_alloc_set_destructor (emission,
				  (NihDestructor)nih_list_destructor);

	nih_debug ("Pending %s event", name);
	nih_list_add (events, &emission->event.entry);

	return emission;
}

/**
 * event_emit_find_by_id:
 * @id: id to find.
 *
 * Finds the event emission with the given id in the list of events
 * currently being dealt with.
 *
 * Returns: emission found or NULL if not found.
 **/
EventEmission *
event_emit_find_by_id (uint32_t id)
{
	EventEmission *emission;

	event_init ();

	NIH_LIST_FOREACH (events, iter) {
		emission = (EventEmission *)iter;

		if (emission->id == id)
			return emission;
	}

	return NULL;
}

/**
 * event_emit_finished:
 * @emission: emission that has finished.
 *
 * This function should be called after a job has released the @emission
 * and decremented the jobs member.  If the jobs member has reached zero,
 * this moves the event into the finshed state so it can be cleaned up
 * when the queue is next run.
 **/
void
event_emit_finished (EventEmission *emission)
{
	nih_assert (emission != NULL);

	if (emission->jobs)
		return;

	emission->progress = EVENT_FINISHED;
}


/**
 * event_poll:
 *
 * This function is used to process the list of events; any in the pending
 * state are moved into the handling state and job states changed.  Any in
 * the finished state have their callbacks called, and are cleaned up.
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
			EventEmission *emission = (EventEmission *)iter;

			/* Ignore events that we're handling, there's nothing
			 * we can do to hurry them.
			 *
			 * Decide whether to poll again based on the state
			 * before handling the event; that way we always loop
			 * at least once more after finding a pending or
			 * finished event, in case they added new events as
			 * a side effect that we missed.
			 */
			switch (emission->progress) {
			case EVENT_PENDING:
				event_pending (emission);
				poll_again = TRUE;
				break;
			case EVENT_HANDLING:
				break;
			case EVENT_FINISHED:
				event_finished (emission);
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
 * @emission: pending event.
 *
 * This function is called for each event in the list that is in the pending
 * state.  Subscribers to emitted events are notified, and the event is
 * passed to the job system to start or stop any.
 *
 * The event is marked as handling; if no jobs took it, then it is
 * immediately finished.
 **/
static void
event_pending (EventEmission *emission)
{
	nih_assert (emission != NULL);
	nih_assert (emission->progress == EVENT_PENDING);

	nih_debug ("Handling %s event", emission->event.name);
	emission->progress = EVENT_HANDLING;

	notify_event (&emission->event);
	job_handle_event (&emission->event);

	/* Make sure we don't hang on to events with no jobs.
	 * (note that the progress might already be finished if all the jobs
	 *  quickly skipped through their states and released it).
	 */
	event_emit_finished (emission);
}

/**
 * event_finished:
 * @emission: finished event.
 *
 * This function is called for each event in the list that is in the finished
 * state.  The callback function, if set, is called.  Then, if the event
 * failed, a new pending failed event is queued.  Finally the emission is
 * freed and removed from the list.
 **/
static void
event_finished (EventEmission *emission)
{
	nih_assert (emission != NULL);
	nih_assert (emission->progress == EVENT_FINISHED);

	nih_debug ("Finished %s event", emission->event.name);

	if (emission->callback)
		emission->callback (emission->data, emission);

	if (emission->failed) {
		char *name;

		name = strrchr (emission->event.name, '/');
		if ((! name) || strcmp (name, "/failed")) {
			NIH_MUST (name = nih_sprintf (NULL, "%s/failed",
						      emission->event.name));

			event_emit (name, emission->event.args,
				    emission->event.env, NULL, NULL);

			nih_free (name);
		}
	}

	nih_list_free (&emission->event.entry);
}


/**
 * event_read_state:
 * @emission: event emission to update,
 * @buf: serialised state.
 *
 * Parse the serialised state and update the event queue if we recognise
 * the line.  We need to always retain knowledge of this so we can always
 * be re-exec'd by an earlier version of init.  That's why this is so
 * trivial.
 *
 * @event may be NULL if @buf begins "Event "
 **/
EventEmission *
event_read_state (EventEmission *emission,
		  char          *buf)
{
	char *ptr;

	nih_assert (buf != NULL);

	/* Every line must have a space, which splits the key and value */
	ptr = strchr (buf, ' ');
	if (ptr) {
		*(ptr++) = '\0';
	} else {
		return emission;
	}

	/* Handle the case where we don't have a event yet first */
	if (! emission) {
		if (! strcmp (buf, "Emission")) {
			uint32_t value;

			value = strtoul (ptr, &ptr, 10);
			if (! *ptr)
				emission_id = value;

			return NULL;

		} else if (strcmp (buf, "Event"))
			return emission;

		/* Add a new event emission record.  Arguments, environment,
		 * callback, etc. will be filled in as we go.
		 */
		emission = event_emit (ptr, NULL, NULL, NULL, NULL);
		return emission;
	}

	/* Otherwise handle the attributes */
	if (! strcmp (buf, ".arg")) {
		NIH_MUST (nih_str_array_add (&emission->event.args, emission,
					     NULL, ptr));

	} else if (! strcmp (buf, ".env")) {
		NIH_MUST (nih_str_array_add (&emission->event.env, emission,
					     NULL, ptr));

	} else if (! strcmp (buf, ".id")) {
		uint32_t value;

		value = strtoul (ptr, &ptr, 10);
		if (! *ptr)
			emission->id = value;

	} else if (! strcmp (buf, ".progress")) {
		long value;

		value = strtol (ptr, &ptr, 10);
		if ((! *ptr) && (value >= 0) && (value < INT_MAX))
			emission->progress = value;

	} else if (! strcmp (buf, ".failed")) {
		if (! strcmp (ptr, "TRUE")) {
			emission->failed = TRUE;
		} else if (! strcmp (ptr, "FALSE")) {
			emission->failed = FALSE;
		}

	}

	return emission;
}

/**
 * event_write_state:
 * @state: file to write to.
 *
 * This is the companion function to event_read_state(), it writes to @state
 * lines for each event in the queue, including the arguments and environment
 * of the event.
 **/
void
event_write_state (FILE *state)
{
	char **ptr;

	nih_assert (state != NULL);

	NIH_LIST_FOREACH (events, iter) {
		EventEmission *emission = (EventEmission *)iter;

		fprintf (state, "Event %s\n", emission->event.name);
		for (ptr = emission->event.args; ptr && *ptr; ptr++)
			fprintf (state, ".arg %s\n", *ptr);
		for (ptr = emission->event.env; ptr && *ptr; ptr++)
			fprintf (state, ".env %s\n", *ptr);

		fprintf (state, ".id %zu\n", emission->id);
		fprintf (state, ".progress %d\n", emission->progress);
		fprintf (state, ".failed %s\n",
			 emission->failed ? "TRUE" : "FALSE");
	}

	fprintf (state, "Emission %d\n", emission_id);
}
