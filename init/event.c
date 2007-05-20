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
static void event_pending  (Event *event);
static void event_finished (Event *event);


/**
 * paused:
 *
 * Do not process the event queue or detect a stalled system
 * while this is TRUE.
 **/
int paused = FALSE;


/**
 * event_id
 *
 * This counter is used to assign unique event ids and is incremented
 * each time we use it.  After a while (4 billion events) it'll wrap over,
 * in which case you should set event_id_wrapped and take care to check
 * an id isn't taken.
 **/
unsigned int event_id = 0;
int          event_id_wrapped = FALSE;

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
 * event_next_id:
 *
 * Returns the current value of the event_id counter, unless that has
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
event_next_id (void)
{
	unsigned int id;

	/* If we've wrapped the event_id counter, we can't just assume that
	 * the current value isn't taken, we need to make sure that nothing
	 * is using it first.
	 */
	if (event_id_wrapped) {
		unsigned int start_id = event_id;

		while (event_find_by_id (event_id)) {
			event_id++;

			/* Make sure we don't end up in an infinite loop if
			 * we're currently handling 4 billion events.
			 */
			if (event_id == start_id) {
				nih_error (_("Event id %u is not unique"),
					   event_id);
				break;
			}
		}
	}

	/* Use the current value of the counter, it's unique as we're ever
	 * going to get; increment the counter afterwards so the next time
	 * this runs, we have moved forwards.
	 */
	id = event_id++;

	/* If incrementing the counter gave us zero, we consumed the entire
	 * id space.  This means that in future we can't assume that the ids
	 * are unique, next time we'll have to be more careful.
	 */
	if (! event_id) {
		if (! event_id_wrapped)
			nih_debug ("Wrapped event_id counter");

		event_id_wrapped = TRUE;
	}

	return id;
}

/**
 * event_new:
 * @parent: parent of new event,
 * @name: name of event to emit,
 * @args: arguments to event,
 * @env: environment for event.
 *
 * Allocates an Event structure for the event details given and
 * appends it to the queue of events.
 *
 * Both @args and @env are optional, and may be NULL; if they are given,
 * then the array itself it reparented to belong to the event structure
 * and should not be modified.
 *
 * When the event reaches the top of the queue, it is taken off and placed
 * into the handling queue.  It is not removed from that queue until there
 * are no longer anything referencing it.
 *
 * The event is created with nothing referencing or blocking it.  Be sure
 * to call event_ref() or event_block() otherwise it will be automatically
 * freed next time through the main loop.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * Returns: new Event structure pending in the queue.
 **/
Event *
event_new (const void  *parent,
	   const char  *name,
	   char      **args,
	   char      **env)
{
	Event *event;

	nih_assert (name != NULL);
	nih_assert (strlen (name) > 0);

	event_init ();

	NIH_MUST (event = nih_new (parent, Event));

	event->id = event_next_id ();
	event->progress = EVENT_PENDING;
	event->failed = FALSE;

	event->refs = 0;
	event->blockers = 0;


	/* Fill in the event details */
	NIH_MUST (event->info.name = nih_strdup (event, name));

	event->info.args = args;
	if (event->info.args)
		nih_alloc_reparent (event->info.args, event);

	event->info.env = env;
	if (event->info.env)
		nih_alloc_reparent (event->info.env, event);


	/* Place it in the pending list */
	nih_list_init (&event->entry);
	nih_alloc_set_destructor (event, (NihDestructor)nih_list_destructor);

	nih_debug ("Pending %s event", name);
	nih_list_add (events, &event->entry);

	return event;
}

/**
 * event_find_by_id:
 * @id: id to find.
 *
 * Finds the event with the given id in the list of events currently being
 * dealt with.
 *
 * Returns: Event found or NULL if not found.
 **/
Event *
event_find_by_id (unsigned int id)
{
	Event *event;

	event_init ();

	NIH_LIST_FOREACH (events, iter) {
		event = (Event *)iter;

		if (event->id == id)
			return event;
	}

	return NULL;
}


/**
 * event_ref:
 * @event: event to reference.
 *
 * This function should be called by jobs that wish to hold a reference on
 * the event without blocking it from finishing.
 *
 * Once the reference is no longer needed, you must call event_unref()
 * otherwise it will never be freed.
 **/
void
event_ref (Event *event)
{
	nih_assert (event != NULL);

	event->refs++;
}

/**
 * event_unref:
 * @event: event to unreference.
 *
 * This function should be called by jobs that are holding a reference on the
 * event without blocking the it from finishing, and wish to discard
 * that reference.
 *
 * It must match a previous call to event_ref().
 **/
void
event_unref (Event *event)
{
	nih_assert (event != NULL);
	nih_assert (event->refs > 0);

	event->refs--;
}


/**
 * event_block:
 * @emission: event to block.
 *
 * This function should be called by jobs that wish to hold a reference on
 * the event and block it from finishing.
 *
 * Once the reference is no longer needed, you must call event_unblock()
 * to allow the event to be finished, and potentially freed.  If you wish
 * to continue to hold the reference afterwards, call event_ref() along
 * with the call to emission_unblock().
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
 * emission which blocks it from finishing, and wish to discard that reference.
 *
 * It must match a previous call to event_block().  If you wish to continue
 * to hold the reference afterwards, call event_ref() along with the call
 * to this function.
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
 * Events remain in the handling state while they have blocking jobs,
 * and remain in the done state while they have references.
 *
 * This function will only return once the events list is empty, or all
 * events are in the handling or done states; so any time an event queues
 * another, it will be processed immediately.
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

			/* Ignore events that we're handling and are not
			 * blocked, or that are done but still have references,
			 * there's nothing we can do to hurry them.
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

				/* fall through */
			case EVENT_DONE:
				if (event->refs)
					break;

				nih_list_free (&event->entry);
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

	nih_info (_("Handling %s event"), event->info.name);
	event->progress = EVENT_HANDLING;

	notify_event (event);
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

	nih_debug ("Finished %s event", event->info.name);

	notify_event_finished (event);
	job_handle_event_finished (event);

	if (event->failed) {
		char *name;

		name = strrchr (event->info.name, '/');
		if ((! name) || strcmp (name, "/failed")) {
			Event *new_event;

			NIH_MUST (name = nih_sprintf (NULL, "%s/failed",
						      event->info.name));
			new_event = event_new (NULL, name, NULL, NULL);
			nih_free (name);

			if (event->info.args)
				NIH_MUST (new_event->info.args
					  = nih_str_array_copy (
						  new_event, NULL,
						  event->info.args));

			if (event->info.env)
				NIH_MUST (new_event->info.env
					  = nih_str_array_copy (
						  new_event, NULL,
						  event->info.env));
		}
	}

	event->progress = EVENT_DONE;
}
