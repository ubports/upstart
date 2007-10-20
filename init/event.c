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
 * is freed, the returned block will be freed too.
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

	nih_list_init (&event->entry);

	event->id = event_next_id ();
	event->progress = EVENT_PENDING;
	event->failed = FALSE;

	event->refs = 0;
	event->blockers = 0;

	nih_alloc_set_destructor (event, (NihDestructor)nih_list_destroy);


	/* Fill in the event details */
	NIH_MUST (event->name = nih_strdup (event, name));

	event->args = args;
	if (event->args)
		nih_alloc_reparent (event->args, event);

	event->env = env;
	if (event->env)
		nih_alloc_reparent (event->env, event);


	/* Place it in the pending list */
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

				nih_free (event);
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
			new_event = event_new (NULL, name, NULL, NULL);
			nih_free (name);

			if (event->args)
				NIH_MUST (new_event->args = nih_str_array_copy (
						  new_event, NULL, event->args));

			if (event->env)
				NIH_MUST (new_event->env = nih_str_array_copy (
						  new_event, NULL, event->env));
		}
	}

	event->progress = EVENT_DONE;
}


/**
 * event_operator_new:
 * @parent: parent of new operator,
 * @type: type of operator,
 * @name: name of event to match.
 * @args: arguments of event to match.
 *
 * Allocates and returns a new EventOperator structure with the @type given,
 * if @type is EVENT_MATCH then the operator will be used to match an event
 * with the given @name and @arguments using event_match().
 *
 * @args is optional, and may be NULL; if given, then the array itself is
 * reparented to belong to the EventOperator structure and should not be
 * modified.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.
 *
 * Returns: newly allocated EventOperator structure, or NULL if
 * insufficient memory.
 **/
EventOperator *
event_operator_new (const void         *parent,
		    EventOperatorType   type,
		    const char         *name,
		    char              **args)
{
	EventOperator *oper;

	nih_assert ((type == EVENT_MATCH) || (name == NULL));
	nih_assert ((type == EVENT_MATCH) || (args == NULL));
	nih_assert ((type != EVENT_MATCH) || (name != NULL));

	oper = nih_new (parent, EventOperator);
	if (! oper)
		return NULL;

	nih_tree_init (&oper->node);

	oper->type = type;
	oper->value = FALSE;

	if (oper->type == EVENT_MATCH) {
		oper->name = nih_strdup (oper, name);
		if (! oper->name) {
			nih_free (oper);
			return NULL;
		}

		oper->args = args;
		if (oper->args)
			nih_alloc_reparent (oper->args, oper);
	} else {
		oper->name = NULL;
		oper->args = NULL;
	}

	oper->event = NULL;
	oper->blocked = FALSE;

	nih_alloc_set_destructor (oper, (NihDestructor)event_operator_destroy);

	return oper;
}

/**
 * event_operator_copy:
 * @parent: parent of new operator,
 * @old_oper: operator to copy.
 *
 * Allocates and returns a new EventOperator structure which is an identical
 * copy of @old_oper; including any matched state or events.
 *
 * If @old_oper is referencing or blocking an event, that status is also
 * copied into the newly returned operator; which will hold an additional
 * reference, and additional block if appropriate, on the event.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.
 *
 * If @old_oper has children, these will be copied as well, and will be
 * given their parent as their nih_alloc() parent.
 *
 * Returns: newly allocated EventOperator structure,
 * or NULL if insufficient memory.
 **/
EventOperator *
event_operator_copy (const void          *parent,
		     const EventOperator *old_oper)
{
	EventOperator *oper, *child;

	nih_assert (old_oper != NULL);

	oper = event_operator_new (parent, old_oper->type,
				   old_oper->name, NULL);
	if (! oper)
		return NULL;

	oper->value = old_oper->value;

	if (old_oper->args) {
		oper->args = nih_str_array_copy (oper, NULL, old_oper->args);
		if (! oper->args) {
			nih_free (oper);
			return NULL;
		}
	}

	if (old_oper->event) {
		oper->event = old_oper->event;
		event_ref (oper->event);

		oper->blocked = old_oper->blocked;
		if (oper->blocked)
			event_block (oper->event);
	}

	if (old_oper->node.left) {
		child = event_operator_copy (
			oper, (EventOperator *)old_oper->node.left);
		if (! child) {
			nih_free (oper);
			return NULL;
		}

		nih_tree_add (&oper->node, &child->node, NIH_TREE_LEFT);
	}


	if (old_oper->node.right) {
		child = event_operator_copy (
			oper, (EventOperator *)old_oper->node.right);
		if (! child) {
			nih_free (oper);
			return NULL;
		}

		nih_tree_add (&oper->node, &child->node, NIH_TREE_RIGHT);
	}

	return oper;
}

/**
 * event_operator_destroy:
 * @oper: operator to be destroyed.
 *
 * Unblocks and unreferences the event referenced by @oper and unlinks it
 * from the event tree.
 *
 * Normally used or called from an nih_alloc() destructor so that the list
 * item is automatically removed from its containing list when freed.
 *
 * Returns: zero.
 **/
int
event_operator_destroy (EventOperator *oper)
{
	nih_assert (oper != NULL);

	if (oper->event) {
		if (oper->blocked)
			event_unblock (oper->event);

		event_unref (oper->event);
	}

	nih_tree_destroy (&oper->node);

	return 0;
}


/**
 * event_operator_update:
 * @oper: operator to update.
 *
 * Updates the value of @oper to reflect the value of its child nodes
 * when combined with the particular operation this represents.
 *
 * This may only be called if the type of @oper is EVENT_OR or EVENT_AND.
 **/
void
event_operator_update (EventOperator *oper)
{
	EventOperator *left, *right;

	nih_assert (oper != NULL);
	nih_assert (oper->node.left != NULL);
	nih_assert (oper->node.right != NULL);

	left = (EventOperator *)oper->node.left;
	right = (EventOperator *)oper->node.right;

	switch (oper->type) {
	case EVENT_OR:
		oper->value = (left->value || right->value);
		break;
	case EVENT_AND:
		oper->value = (left->value && right->value);
		break;
	default:
		nih_assert_not_reached ();
	}
}

/**
 * event_operator_match:
 * @oper: operator to match against.
 * @event: event to match.
 *
 * Compares @oper against @event to see whether they are identical in name,
 * and whether @event contains at least the number of arguments given in
 * @oper, and that each of those arguments matches as a glob.
 *
 * This may only be called if the type of @oper is EVENT_MATCH.
 *
 * Returns: TRUE if the events match, FALSE otherwise.
 **/
int
event_operator_match (EventOperator *oper,
		      Event         *event)
{
	char **arg1, **arg2;

	nih_assert (oper != NULL);
	nih_assert (oper->type == EVENT_MATCH);
	nih_assert (oper->node.left == NULL);
	nih_assert (oper->node.right == NULL);
	nih_assert (event != NULL);

	/* Names must match */
	if (strcmp (oper->name, event->name))
		return FALSE;

	/* Match arguments using the operator's argument as a glob */
	for (arg1 = oper->args, arg2 = event->args;
	     arg1 && arg2 && *arg1 && *arg2; arg1++, arg2++)
		if (fnmatch (*arg1, *arg2, 0))
			return FALSE;

	/* Must be at least the same number of arguments in event as
	 * there are in oper
	 */
	if (arg1 && *arg1)
		return FALSE;

	return TRUE;
}


/**
 * event_operator_handle:
 * @root: operator tree to update,
 * @event: event to match against.
 *
 * Handles the emission of @event, matching it against EVENT_MATCH nodes in
 * the EventOperator tree rooted at @oper, and updating the values of other
 * nodes to match.
 *
 * If @event is matched within this tree, it will be referenced and blocked
 * by the nodes that match it.  The blockage can be cleared by using
 * event_operator_unblock() and the references cleared by using
 * event_operator_reset().
 *
 * Note that this returns to indicate whether a successful match was made;
 * you should also check the value of @oper to make sure you react to this,
 * as that still may be FALSE.
 *
 * Returns: TRUE if @event matched an entry in the tree under @oper, FALSE
 * otherwise.
 **/
int
event_operator_handle (EventOperator *root,
		       Event         *event)
{
	int ret = FALSE;

	nih_assert (root != NULL);
	nih_assert (event != NULL);

	/* A post-order traversal will give us the nodes in exactly the
	 * order we want.  We get a chance to update all of a node's children
	 * before we update the node itself.  Simply iterate the tree and
	 * update the nodes.
	 */
	NIH_TREE_FOREACH_POST (&root->node, iter) {
		EventOperator *oper = (EventOperator *)iter;

		switch (oper->type) {
		case EVENT_OR:
		case EVENT_AND:
			event_operator_update (oper);
			break;
		case EVENT_MATCH:
			if (event_operator_match (oper, event)) {
				oper->value = TRUE;

				oper->event = event;
				event_ref (oper->event);

				event_block (oper->event);
				oper->blocked = TRUE;

				ret = TRUE;
			}
			break;
		default:
			nih_assert_not_reached ();
		}

	}

	return ret;
}

/**
 * event_operator_unblock:
 * @root: operator tree to update.
 *
 * Updates the EventOperator tree rooted at @oper, unblocking any events
 * that were matched while retaining the references on them.
 *
 * This makes no change to the values of the operator tree.
 **/
void
event_operator_unblock (EventOperator *root)
{
	nih_assert (root != NULL);

	NIH_TREE_FOREACH_POST (&root->node, iter) {
		EventOperator *oper = (EventOperator *)iter;

		if (oper->type != EVENT_MATCH)
			continue;

		if (oper->event && oper->blocked) {
			event_unblock (oper->event);
			oper->blocked = FALSE;
		}
	}
}

/**
 * event_operator_reset:
 * @root: operator tree to update.
 *
 * Resets the EventOperator tree rooted at @oper, unblocking and
 * unreferencing any events that were matched by the tree and changing
 * the values of other operators to match.
 **/
void
event_operator_reset (EventOperator *root)
{
	nih_assert (root != NULL);

	/* A post-order iteration means we visit children first, perfect! */
	NIH_TREE_FOREACH_POST (&root->node, iter) {
		EventOperator *oper = (EventOperator *)iter;

		switch (oper->type) {
		case EVENT_OR:
		case EVENT_AND:
			event_operator_update (oper);
			break;
		case EVENT_MATCH:
			oper->value = FALSE;

			if (oper->event) {
				if (oper->blocked) {
					event_unblock (oper->event);
					oper->blocked = FALSE;
				}

				event_unref (oper->event);
				oper->event = NULL;
			}
			break;
		default:
			nih_assert_not_reached ();
		}
	}
}
