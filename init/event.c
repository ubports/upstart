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
 * Do not process the event queue or detect a stalled system
 * while this is TRUE.
 **/
int paused = FALSE;


/**
 * emission_id
 *
 * This counter is used to assign unique emission ids and is incremented
 * each time we use it.  After a while (4 billion events) it'll wrap over,
 * in which case you should set emission_id_wrapped and take care to check
 * an id isn't taken.
 **/
unsigned int emission_id = 0;
int          emission_id_wrapped = FALSE;

/**
 * events:
 *
 * This list holds the list of events in the process of pending, being
 * handled or awaiting cleanup; each item is an EventEmission structure.
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
 * event_info_new:
 * @parent: parent of new event,
 * @name: name of new event.
 *
 * Allocates and returns a new EventInfo structure with the @name given, but
 * does not place it in the event queue.  Use when a lone EventInfo structure
 * is needed, such as for matching events.
 *
 * Both @args and @env are optional, and may be NULL; if they are given,
 * then the array itself is reparented to belong to the event structure
 * and should not be modified afterwards.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * Returns: newly allocated EventInfo structure or NULL if insufficient memory.
 **/
EventInfo *
event_info_new (const void  *parent,
		const char  *name,
		char       **args,
		char       **env)
{
	EventInfo *event;

	nih_assert (name != NULL);
	nih_assert (strlen (name) > 0);

	event = nih_new (parent, EventInfo);
	if (! event)
		return NULL;

	nih_list_init (&event->entry);

	event->name = nih_strdup (event, name);
	if (! event->name) {
		nih_free (event);
		return NULL;
	}

	event->args = args;
	if (event->args)
		nih_alloc_reparent (event->args, event);

	event->env = env;
	if (event->env)
		nih_alloc_reparent (event->env, event);

	return event;
}

/**
 * event_info_copy:
 * @parent: parent of new event,
 * @old_event: event to copy.
 *
 * Allocates and returns a new EventInfo structure which is an identical copy
 * of @old_event.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * Returns: newly allocated EventInfo structure or NULL if insufficient memory.
 **/
EventInfo *
event_info_copy (const void      *parent,
		 const EventInfo *old_event)
{
	EventInfo *event;

	nih_assert (old_event != NULL);

	event = event_info_new (parent, old_event->name, NULL, NULL);
	if (! event)
		return NULL;

	if (old_event->args) {
		event->args = nih_str_array_copy (event, NULL,
						  old_event->args);
		if (! event->args)
			goto error;
	}

	if (old_event->env) {
		event->env = nih_str_array_copy (event, NULL,
						 old_event->env);
		if (! event->env)
			goto error;
	}

	return event;

error:
	nih_free (event);
	return NULL;
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
event_match (EventInfo *event1,
	     EventInfo *event2)
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
 * event_emit_next_id:
 *
 * Returns the current value of the emission_id counter, unless that has
 * been wrapped before, in which case it checks whether the value is
 * currently in use before returning it.  If the value is in use, it
 * increments the counter until it finds a value that isn't, or until it
 * has checked the entire value space.
 *
 * This is most efficient while less than 4 billion events have been
 * generated, at which point it becomes slightly less efficient.  If there
 * are currently 4 billion events being handled (!!) we lose the ability
 * to generate unique ids, and emit an error -- if we start seeing this in
 * the field, we can always to a larger type or something.
 *
 * Returns: next usable id.
 **/
static inline unsigned int
event_emit_next_id (void)
{
	unsigned int id;

	/* If we've wrapped the emission_id counter, we can't just assume that
	 * the current value isn't taken, we need to make sure that nothing
	 * is using it first.
	 */
	if (emission_id_wrapped) {
		unsigned int start_id = emission_id;

		while (event_emit_find_by_id (emission_id)) {
			emission_id++;

			/* Make sure we don't end up in an infinite loop if
			 * we're currently handling 4 billion events.
			 */
			if (emission_id == start_id) {
				nih_error (_("Emission id %u is not unique"),
					   emission_id);
				break;
			}
		}
	}

	/* Use the current value of the counter, it's unique as we're ever
	 * going to get; increment the counter afterwards so the next time
	 * this runs, we have moved forwards.
	 */
	id = emission_id++;

	/* If incrementing the counter gave us zero, we consumed the entire
	 * id space.  This means that in future we can't assume that the ids
	 * are unique, next time we'll have to be more careful.
	 */
	if (! emission_id) {
		if (! emission_id_wrapped)
			nih_debug ("Wrapped emission_id counter");

		emission_id_wrapped = TRUE;
	}

	return id;
}

/**
 * event_emit:
 * @name: name of event to emit,
 * @args: arguments to event,
 * @env: environment for event.
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
 * are no longer any jobs referencing the event.
 *
 * Returns: new EventEmission structure in the pending queue.
 **/
EventEmission *
event_emit (const char       *name,
	    char            **args,
	    char            **env)
{
	EventEmission *emission;

	nih_assert (name != NULL);
	nih_assert (strlen (name) > 0);

	event_init ();

	NIH_MUST (emission = nih_new (NULL, EventEmission));

	emission->id = event_emit_next_id ();
	emission->progress = EVENT_PENDING;

	emission->jobs = 0;
	emission->failed = FALSE;


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
event_emit_find_by_id (unsigned int id)
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
 * state are moved into the handling state and job states changed.  Any
 * in the finished state are cleaned up, with subscribers and jobs notified
 * that the event has completed.
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

	nih_info (_("Handling %s event"), emission->event.name);
	emission->progress = EVENT_HANDLING;

	notify_event (emission);
	job_handle_event (emission);

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
 * state.  Subscribers and jobs are notified, then, if the event failed, a
 * new pending failed event is queued.  Finally the emission is freed and
 * removed from the list.
 **/
static void
event_finished (EventEmission *emission)
{
	nih_assert (emission != NULL);
	nih_assert (emission->progress == EVENT_FINISHED);

	nih_debug ("Finished %s event", emission->event.name);

	notify_event_finished (emission);
	job_handle_event_finished (emission);

	if (emission->failed) {
		char *name;

		name = strrchr (emission->event.name, '/');
		if ((! name) || strcmp (name, "/failed")) {
			EventEmission *new_emission;

			NIH_MUST (name = nih_sprintf (NULL, "%s/failed",
						      emission->event.name));
			new_emission = event_emit (name, NULL, NULL);
			nih_free (name);

			if (emission->event.args)
				NIH_MUST (new_emission->event.args
					  = nih_str_array_copy (
						  new_emission, NULL,
						  emission->event.args));

			if (emission->event.env)
				NIH_MUST (new_emission->event.env
					  = nih_str_array_copy (
						  new_emission, NULL,
						  emission->event.env));
		}
	}

	nih_list_free (&emission->event.entry);
}
