/* upstart
 *
 * event.c - event queue and handling
 *
 * Copyright © 2010 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/tree.h>
#include <nih/logging.h>
#include <nih/error.h>

#include "environ.h"
#include "event.h"
#include "event_operator.h"
#include "blocked.h"
#include "errors.h"


/**
 * event_operator_new:
 * @parent: parent object for new operator,
 * @type: type of operator,
 * @name: name of event to match,
 * @env: NULL-terminated array of environment variables to match.
 *
 * Allocates and returns a new EventOperator structure with the @type given,
 * if @type is EVENT_MATCH then the operator will be used to match an event
 * with the given @name and @arguments using event_match().
 *
 * @env is optional, and may be NULL; if given it should be a NULL-terminated
 * array of environment variables in KEY=VALUE form.  @env will be referenced
 * by the new event.  After calling this function, you should never use
 * nih_free() to free @env and instead use nih_unref() or nih_discard() if
 * you longer need to use it.
 *
 * If @parent is not NULL, it should be a pointer to another object which
 * will be used as a parent for the returned operator.  When all parents
 * of the returned operator are freed, the returned operator will also be
 * freed.
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
			nih_ref (oper->env, oper);
	} else {
		oper->name = NULL;
		oper->env = NULL;
	}

	oper->event = NULL;

	nih_alloc_set_destructor (oper, event_operator_destroy);

	return oper;
}

/**
 * event_operator_copy:
 * @parent: parent object for new operator,
 * @old_oper: operator to copy.
 *
 * Allocates and returns a new EventOperator structure which is an identical
 * copy of @old_oper; including any matched state or events.
 *
 * If @old_oper is referencing an event, that status is also copied into the
 * newly returned operator; which will hold an additional block if
 * appropriate, on the event.
 *
 * If @old_oper has children, these will be copied as well, and will be
 * given their parent as their nih_alloc() parent.
 *
 * If @parent is not NULL, it should be a pointer to another object which
 * will be used as a parent for the returned operator.  When all parents
 * of the returned operator are freed, the returned operator will also be
 * freed.
 *
 * Returns: newly allocated EventOperator structure, or NULL if
 * insufficient memory.
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
		nih_local char *expoval = NULL;
		char           *oval, *eval;
		int             negate = FALSE;
		int             ret;

		oval = strstr (*oenv, "!=");
		if (! oval)
			oval = strchr (*oenv, '=');

		if (oval) {
			/* Hunt through the event environment to find the
			 * equivalent entry */
			eenv = environ_lookup (event->env, *oenv,
					       oval - *oenv);

			/* != means we negate the result (and skip the !) */
			if (*oval == '!') {
				negate = TRUE;
				oval++;
			}

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

		if (negate ? (! ret) : ret)
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
			if ((! oper->value)
			    && event_operator_match (oper, event, env)) {
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
 * event_operator_environment:
 * @root: operator tree to collect from,
 * @env: NULL-terminated array of environment variables to add to,
 * @parent: parent object for new array,
 * @len: length of @env,
 * @key: key of variable to contain event names.
 *
 * Collects environment from the portion of the EventOperator tree rooted at
 * @oper that are TRUE, ignoring the rest.
 *
 * Environment variables from each event (in tree order) will be added to
 * the NULL-terminated array at @env so that it contains the complete
 * environment of the operator.
 *
 * @len will be updated to contain the new array length and @env will
 * be updated to point to the new array pointer.
 *
 * Note that on failure, some of the entries may have been appended to
 * @env already.  This is normally not a problem, since entries will be
 * replaced in @env if this is repeated.
 *
 * If @key is not NULL, a key of that name will be set in @env (which must
 * not be NULL) containing a space-separated list of event names.
 *
 * If the array pointed to by @env is NULL, the array will be allocated and
 * if @parent is not NULL, it should be a pointer to another object which
 * will be used as a parent for the returned array.  When all parents of the
 * returned array are freed, the returned array will also be freed.
 *
 * When the array pointed to by @env is not NULL, @parent is ignored;
 * though it usual to pass a parent of @env for style reasons.
 *
 * Returns: pointer to new array on success, NULL on insufficient memory.
 **/
char **
event_operator_environment (EventOperator   *root,
			    char          ***env,
			    const void      *parent,
			    size_t          *len,
			    const char      *key)
{
	nih_local char *evlist = NULL;

	nih_assert (root != NULL);
	nih_assert (env != NULL);
	nih_assert (len != NULL);

	/* Initialise the event list variable with the name given. */
	if (key) {
		evlist = nih_sprintf (NULL, "%s=", key);
		if (! evlist)
			return NULL;
	}

	/* Always return an array, even if its zero length */
	if (! *env) {
		*env = nih_str_array_new (parent);
		if (! *env)
			return NULL;
	}

	/* Iterate the operator tree, filtering out nodes with a non-TRUE
	 * value and their children.  The rationale for this is that this
	 * then matches only the events that had an active role in starting
	 * the job, not the ones that were also blocked, but the other half
	 * of their logic wasn't present.
	 */
	NIH_TREE_FOREACH_FULL (&root->node, iter,
			       (NihTreeFilter)event_operator_filter, NULL) {
		EventOperator *oper = (EventOperator *)iter;

		if (oper->type != EVENT_MATCH)
			continue;

		nih_assert (oper->event != NULL);

		/* Add environment from the event */
		if (! environ_append (env, parent, len, TRUE, oper->event->env))
			return NULL;

		/* Append the name of the event to the string we're building */
		if (evlist) {
			if (evlist[strlen (evlist) - 1] != '=') {
				if (! nih_strcat_sprintf (&evlist, NULL, " %s",
							  oper->event->name))
					return NULL;
			} else {
				if (! nih_strcat (&evlist, NULL,
						  oper->event->name))
					return NULL;
			}
		}
	}

	/* Append the event list to the environment */
	if (evlist)
		if (! environ_add (env, parent, len, TRUE, evlist))
			return NULL;

	return *env;
}

/**
 * event_operator_events:
 * @root: operator tree to collect from,
 * @parent: parent object for blocked structures,
 * @list: list to add events to.
 *
 * Collects events from the portion of the EventOperator tree rooted at @oper
 * that are TRUE, ignoring the rest.
 *
 * Each event is blocked and a Blocked structure will be appended to @list
 * for it.
 *
 * If @parent is not NULL, it should be a pointer to another object which
 * will be used as a parent for the returned structures.  When all parents
 * of a returned structure are freed, the returned structure will also be
 * freed.
 **/
void
event_operator_events (EventOperator *root,
		       const void    *parent,
		       NihList       *list)
{
	nih_assert (root != NULL);
	nih_assert (list != NULL);

	/* Iterate the operator tree, filtering out nodes with a non-TRUE
	 * value and their children.  The rationale for this is that this
	 * then matches only the events that had an active role in starting
	 * the job, not the ones that were also blocked, but the other half
	 * of their logic wasn't present.
	 */
	NIH_TREE_FOREACH_FULL (&root->node, iter,
			       (NihTreeFilter)event_operator_filter, NULL) {
		EventOperator *oper = (EventOperator *)iter;
		Blocked       *blocked;

		if (oper->type != EVENT_MATCH)
			continue;

		nih_assert (oper->event != NULL);

		blocked = NIH_MUST (blocked_new (parent, BLOCKED_EVENT,
						 oper->event));
		nih_list_add (list, &blocked->entry);

		event_block (blocked->event);
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
