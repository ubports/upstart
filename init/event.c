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
#include <nih/error.h>

#include "environ.h"
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

	event->id = event_next_id ();
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


/**
 * event_operator_new:
 * @parent: parent of new operator,
 * @type: type of operator,
 * @name: name of event to match,
 * @env: NULL-terminated array of environment variables to match.
 *
 * Allocates and returns a new EventOperator structure with the @type given,
 * if @type is EVENT_MATCH then the operator will be used to match an event
 * with the given @name and @arguments using event_match().
 *
 * @env is optional, and may be NULL; if given it should be a NULL-terminated
 * array of environment variables in KEY=VALUE form.  The @env array itself
 * will be reparented to the event structure and should not be modified after
 * the call.
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
		    char              **env)
{
	EventOperator *oper;

	nih_assert ((type == EVENT_MATCH) || (name == NULL));
	nih_assert ((type == EVENT_MATCH) || (env == NULL));
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

		oper->env = env;
		if (oper->env)
			nih_alloc_reparent (oper->env, oper);
	} else {
		oper->name = NULL;
		oper->env = NULL;
	}

	oper->event = NULL;

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
 * If @old_oper is referencing an event, that status is also copied into the
 * newly returned operator; which will hold an additional block if
 * appropriate, on the event.
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

	if (old_oper->env) {
		oper->env = nih_str_array_copy (oper, NULL, old_oper->env);
		if (! oper->env) {
			nih_free (oper);
			return NULL;
		}
	}

	if (old_oper->event) {
		oper->event = old_oper->event;
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

	if (oper->event)
		event_unblock (oper->event);

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
 * @event: event to match,
 * @env: NULL-terminated array of environment variables for expansion.
 *
 * Compares @oper against @event to see whether they are identical in name,
 * and whether @event contains a superset of the environment variables
 * given in @oper.
 *
 * @env is optional, and may be NULL; if given it should be a NULL-terminated
 * array of environment variables in KEY=VALUE form.
 *
 * Matching of environment is done first by position until the first variable
 * in @oper with a name specified is found, and subsequently by name.  Each
 * value is matched against the equivalent in @event as a glob, undergoing
 * expansion against @env first.
 *
 * This may only be called if the type of @oper is EVENT_MATCH.
 *
 * Returns: TRUE if the events match, FALSE otherwise.
 **/
int
event_operator_match (EventOperator *oper,
		      Event         *event,
		      char * const  *env)
{
	char * const *oenv;
	char * const *eenv;

	nih_assert (oper != NULL);
	nih_assert (oper->type == EVENT_MATCH);
	nih_assert (oper->node.left == NULL);
	nih_assert (oper->node.right == NULL);
	nih_assert (event != NULL);

	/* Names must match */
	if (strcmp (oper->name, event->name))
		return FALSE;

	/* Match operator environment variables against those from the event,
	 * starting both from the beginning.
	 */
	for (oenv = oper->env, eenv = event->env; oenv && *oenv;
	     oenv++, eenv++) {
		char *oval, *expoval, *eval;
		int   ret;

		oval = strchr (*oenv, '=');
		if (oval) {
			/* Hunt through the event environment to find the
			 * equivalent entry */
			eenv = environ_lookup (event->env, *oenv,
					       oval - *oenv);

			/* Value to match against follows the equals. */
			oval++;
		} else {
			/* Value to match against is the whole string. */
			oval = *oenv;
		}

		/* Make sure we haven't gone off the end of the event
		 * environment array; this catches both too many positional
		 * matches and no such variable.
		 */
		if (! (eenv && *eenv))
			return FALSE;

		/* Grab the value out by looking for the equals, we don't
		 * care about the name if we're positional and we've already
		 * matched it when not.
		 */
		eval = strchr (*eenv, '=');
		nih_assert (eval != NULL);
		eval++;

		/* Expand operator value against given environment before
		 * matching; silently discard errors, since otherwise we'd
		 * be excessively noisy on every event.
		 */
		while (! (expoval = environ_expand (NULL, oval, env))) {
			NihError *err;

			err = nih_error_get ();
			if (err->number != ENOMEM) {
				nih_free (err);
				return FALSE;
			}
			nih_free (err);
		}

		ret = fnmatch (expoval, eval, 0);
		nih_free (expoval);

		if (ret)
			return FALSE;
	}

	return TRUE;
}


/**
 * event_operator_handle:
 * @root: operator tree to update,
 * @event: event to match against,
 * @env: NULL-terminated array of environment variables for expansion.
 *
 * Handles the emission of @event, matching it against EVENT_MATCH nodes in
 * the EventOperator tree rooted at @oper, and updating the values of other
 * nodes to match.
 *
 * @env is optional, and may be NULL; if given it should be a NULL-terminated
 * array of environment variables in KEY=VALUE form and will be used to expand
 * EVENT_MATCH nodes before matching them,
 *
 * If @event is matched within this tree, it will be referenced and blocked
 * by the nodes that match it.  The blockage and references can be cleared
 * using event_operator_reset().
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
		       Event         *event,
		       char * const  *env)
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
			if (event_operator_match (oper, event, env)) {
				oper->value = TRUE;

				oper->event = event;
				event_block (oper->event);

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
 * event_operator_filter:
 * @data: not used,
 * @oper: EventOperator to check.
 *
 * Used when iterating the operator tree to filter out those operators and
 * their children for which the value is not TRUE.
 *
 * Returns: TRUE if operator should be ignored, FALSE otherwise.
 **/
static int
event_operator_filter (void          *data,
		       EventOperator *oper)
{
	nih_assert (oper != NULL);

	return oper->value != TRUE;
}

/**
 * event_operator_collect:
 * @root: operator tree to collect from,
 * @parent: parent of @env,
 * @env: NULL-terminated array of environment variables to add to,
 * @len: length of @env,
 * @key: key of variable to contain event names,
 * @list: list to add events to.
 *
 * Collects events from the portion of the EventOperator tree rooted at @oper
 * that are TRUE, ignoring the rest.
 *
 * If @list is not NULL, an NihListEntry structure will be appended for each
 * event with the data pointer pointing at the Event itself, the event will
 * be both referenced and blocked (if blocked in the operator) by this;
 * the created structure will have @list as a parent.
 *
 * If @env is not NULL, environment variables from each event (in tree order)
 * will be added to the NULL-terminated array so that it contains the complete
 * environment of the operator.
 *
 * @len will be updated to contain the new array length and @env will
 * be updated to point to the new array pointer.
 *
 * If @key is not NULL, a key of that name will be set in @env (which must
 * not be NULL) containing a space-separated list of event names.
 **/
void
event_operator_collect (EventOperator   *root,
			char          ***env,
			const void      *parent,
			size_t          *len,
			const char      *key,
			NihList         *list)
{
	char *evlist;

	nih_assert (root != NULL);
	nih_assert ((parent == NULL) || (env != NULL));
	nih_assert ((len == NULL) || (env != NULL));
	nih_assert ((key == NULL) || (env != NULL));

	/* Initialise the event list variable with the name given. */
	if (key)
		NIH_MUST (evlist = nih_sprintf (NULL, "%s=", key));

	/* Iterate the operator tree, filtering out nodes with a non-TRUE
	 * value and their children.  The rationale for this is that this
	 * then matches only the events that had an active role in starting
	 * the job, not the ones that were also blocked, but the other half
	 * of their logic wasn't present.
	 */
	NIH_TREE_FOREACH_FULL (&root->node, iter,
			       (NihTreeFilter)event_operator_filter, NULL) {
		EventOperator  *oper = (EventOperator *)iter;

		if (oper->type != EVENT_MATCH)
			continue;

		nih_assert (oper->event != NULL);

		/* Add environment from the event */
		if (env) {
			char **e;

			for (e = oper->event->env; e && *e; e++)
				NIH_MUST (environ_add (env, parent, len, *e));
		}

		/* Append the name of the event to the string we're building */
		if (key) {
			if (evlist[strlen (evlist) - 1] != '=') {
				NIH_MUST (nih_strcat_sprintf (&evlist, NULL, " %s",
							      oper->event->name));
			} else {
				NIH_MUST (nih_strcat (&evlist, NULL,
						      oper->event->name));
			}
		}

		/* Append to list, referencing and blocking if necessary */
		if (list) {
			NihListEntry *entry;

			NIH_MUST (entry = nih_list_entry_new (list));
			entry->data = oper->event;

			nih_list_add (list, &entry->entry);

			event_block (oper->event);
		}
	}

	/* Append the event list to the environment */
	if (env && key) {
		NIH_MUST (environ_add (env, parent, len, evlist));
		nih_free (evlist);
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
				event_unblock (oper->event);
				oper->event = NULL;
			}
			break;
		default:
			nih_assert_not_reached ();
		}
	}
}
