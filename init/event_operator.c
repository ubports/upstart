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
 * you no longer need to use it.
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
 * the EventOperator tree rooted at @root, and updating the values of other
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
 * you should also check the value of @root to make sure you react to this,
 * as that still may be FALSE.
 *
 * Returns: TRUE if @event matched an entry in the tree under @root, FALSE
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
 * event_operator_fds:
 * @root: operator tree to update,
 * @parent: parent object for new array,
 * @fds: output location for array of ints
 * @num_fds: number of elements in @fds,
 * @env: NULL-terminated array of environment variables to add to,
 * @len: length of @env,
 * @key: key of variable to contain event names.
 *
 * Iterate over tree rooted at @root adding all file descriptor values found
 * to the dynamically allocated @fds array. In addition, all file
 * descriptors found are also added to @env will contain a new entry with key @key
 * whose value is a space-separated list of file descriptor numbers.
 *
 * Returns: 1 on success, NULL on failure.
 **/
int *
event_operator_fds (EventOperator  *root,
		    const void     *parent,
		    int           **fds,
		    size_t         *num_fds,
		    char         ***env,
		    size_t         *len,
		    const char     *key)
{
	nih_local char *evlist = NULL;

	nih_assert (root != NULL);
	nih_assert (fds != NULL);
	nih_assert (num_fds != NULL);
	nih_assert (env != NULL);
	nih_assert (len != NULL);
	nih_assert (key != NULL);

	/* Initialise the event list variable with the name given. */
	evlist = nih_sprintf (NULL, "%s=", key);
	if (! evlist)
		return NULL;

	*num_fds = 0;
	NIH_TREE_FOREACH_FULL (&root->node, iter,
			       (NihTreeFilter)event_operator_filter, NULL) {
		EventOperator *oper = (EventOperator *)iter;

		if (oper->type != EVENT_MATCH)
			continue;

		nih_assert (oper->event != NULL);

		if (oper->event->fd >= 0) {
			*fds = nih_realloc (*fds, parent, sizeof (int) * (*num_fds + 1));
			if (! *fds)
				return NULL;

			(*fds)[(*num_fds)++] = oper->event->fd;

			if (evlist[strlen (evlist) - 1] != '=') {
				if (! nih_strcat_sprintf (&evlist, NULL, " %d",
							  oper->event->fd))
					return NULL;
			} else {
				if (! nih_strcat_sprintf (&evlist, NULL, "%d",
							  oper->event->fd))
					return NULL;
			}
		}
	}

	if (*num_fds)
		if (! environ_add (env, parent, len, TRUE, evlist))
			return NULL;

	return (void *)1;
}

/**
 * event_operator_events:
 * @root: operator tree to collect from,
 * @parent: parent object for blocked structures,
 * @list: list to add events to.
 *
 * Collects events from the portion of the EventOperator tree rooted at @root
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

/**
 * event_operator_collapse:
 *
 * @condition: start on/stop on condition.
 *
 * Collapsed condition will be fully bracketed. Note that as such it may
 * not be lexicographically identical to the original expression that
 * resulted in @condition, but it will be logically identical.
 *
 * The condition is reconstructed from the EventOperator tree by using
 * a post-order traversal (since this allows the tree to be traversed
 * bottom-to-top). Leaf nodes (EVENT_MATCH) are ignored when visited,
 * allowing non-leaf nodes (EVENT_AND and EVENT_OR) to simply grab the
 * value of their children, construct a bracketed expression and add it
 * to a stack. If a child is a leaf node, the value can be read
 * directly. If a child is a non-leaf node, the value is obtained by
 * popping the stack before adding the new value back onto the stack.
 * When finally the root node is visited, the final expression can be
 * removed from the stack and returned. A single-node tree (comprising a
 * lone EVENT_MATCH at the root) is special-cased.
 *
 * Returns: newly-allocated flattened string representing @condition
 * on success, or NULL on error.
 **/
char *
event_operator_collapse (EventOperator *condition)
{
	nih_local NihList       *stack = NULL;
	NihListEntry            *latest = NULL;
	NihTree                 *root;

	nih_assert (condition);

	root = &condition->node;

	stack = NIH_MUST (nih_list_new (NULL));

	NIH_TREE_FOREACH_POST (root, iter) {
		EventOperator   *oper = (EventOperator *)iter;
		EventOperator   *left;
		EventOperator   *right;
		NihListEntry    *expr;
		NihTree         *tree;
		nih_local char  *left_expr = NULL;
		nih_local char  *right_expr = NULL;

		tree = &oper->node;

		left = (EventOperator *)tree->left;
		right = (EventOperator *)tree->right;

		if (oper->type == EVENT_MATCH) {
			/* Entire expression comprises a single event,
			 * so push it and leave.
			 */
			if (tree == root) {
				nih_local char *env = NULL;

				if (oper->env)
					env = NIH_MUST (state_collapse_env ((const char **)oper->env));

				expr = NIH_MUST (nih_list_entry_new (stack));
				expr->str = NIH_MUST (nih_sprintf (expr, "%s%s%s",
							oper->name,
							env ? " " : "",
							env ? env : ""));
				nih_list_add_after (stack, &expr->entry);
				break;
			} else {
				/* We build the expression from visiting the logical
				 * operators (and their children) only.
				 */
				continue;
			}
		}

		/* oper cannot now be a leaf node, so must have children */
		nih_assert (left);
		nih_assert (right);

		expr = NIH_MUST (nih_list_entry_new (stack));

		/* If a child is an EVENT_MATCH, expand its event
		 * details and push onto the stack.
		 * If a child is not an EVENT_MATCH, to obtains it
		 * value, pop the stack.
		 *
		 * Having obtained the child values, construct a new
		 * bracketed expression and push onto the stack.
		 *
		 * Note that we must consider the right child first.
		 * This is because since the tree is traversed
		 * left-child first, any value pushed onto the stack by
		 * a right child is at the top so must be removed before
		 * any left child value. Failure to do this results in
		 * tree reflection which although logically equivalent
		 * to the original could confuse as the resultant
		 * expression will look rather different.
		 */
		if (right->type != EVENT_MATCH) {
			nih_assert (! NIH_LIST_EMPTY (stack));

			latest = (NihListEntry *)nih_list_remove (stack->next);
			right_expr = NIH_MUST (nih_strdup (NULL, latest->str));
		} else {
			nih_local char *env = NULL;

			if (right->env)
				env = NIH_MUST (state_collapse_env ((const char **)right->env));

			right_expr = NIH_MUST (nih_sprintf (NULL, "%s%s%s",
						right->name,
						env ? " " : "",
						env ? env : ""));
		}

		if (left->type != EVENT_MATCH) {
			nih_assert (! NIH_LIST_EMPTY (stack));

			latest = (NihListEntry *)nih_list_remove (stack->next);
			left_expr = NIH_MUST (nih_strdup (NULL, latest->str));
		} else {
			nih_local char *env = NULL;

			if (left->env)
				env = NIH_MUST (state_collapse_env ((const char **)left->env));

			left_expr = NIH_MUST (nih_sprintf (NULL, "%s%s%s",
						left->name,
						env ? " " : "",
						env ? env : ""));
		}

		expr->str = NIH_MUST (nih_sprintf (expr, "(%s %s %s)",
					left_expr,
					oper->type == EVENT_OR ? "or" : "and",
					right_expr));

		nih_list_add_after (stack, &expr->entry);
	}

	nih_assert (! NIH_LIST_EMPTY (stack));

	latest = (NihListEntry *)nih_list_remove (stack->next);

	nih_assert (NIH_LIST_EMPTY (stack));

	return NIH_MUST (nih_strdup (NULL, latest->str));
}

/**
 * event_operator_type_enum_to_str:
 *
 * @type: EventOperatorType.
 *
 * Convert EventOperatorType to a string representation.
 *
 * Returns: string representation of @type, or NULL if not known.
 **/
const char *
event_operator_type_enum_to_str (EventOperatorType type)
{
	state_enum_to_str (EVENT_OR, type);
	state_enum_to_str (EVENT_AND, type);
	state_enum_to_str (EVENT_MATCH, type);

	return NULL;
}

/**
 * event_operator_type_str_to_enum:
 *
 * @type: string EventOperatorType value.
 *
 * Convert @expect back into an enum value.
 *
 * Returns: EventOperatorType representing @type, or -1 if not known.
 **/
EventOperatorType
event_operator_type_str_to_enum (const char *type)
{
	nih_assert (type);

	state_str_to_enum (EVENT_OR, type);
	state_str_to_enum (EVENT_AND, type);
	state_str_to_enum (EVENT_MATCH, type);

	return -1;
}

/**
 * event_operator_serialise:
 * @oper: EventOperator to serialise.
 *
 * Convert @oper into a JSON representation for serialisation.
 * Caller must free returned value using json_object_put().
 *
 * Returns: JSON-serialised EventOperator object, or NULL on error.
 **/
json_object *
event_operator_serialise (const EventOperator *oper)
{
	json_object  *json;
	int           event_index;

	nih_assert (oper);

	json = json_object_new_object ();
	if (! json)
		return NULL;

	if (! state_set_json_enum_var (json,
				event_operator_type_enum_to_str,
				"type", oper->type))
		goto error;

	if (! state_set_json_int_var_from_obj (json, oper, value))
		goto error;

	if (oper->name) {
		if (! state_set_json_string_var_from_obj (json, oper, name))
			goto error;
	}

	if (oper->env) {
		if (! state_set_json_str_array_from_obj (json, oper, env))
			goto error;
	}

	if (oper->event) {
		event_index = event_to_index (oper->event);
		if (event_index < 0)
			goto error;

		if (! state_set_json_int_var (json, "event", event_index))
			goto error;
	}

	return json;

error:
	json_object_put (json);
	return NULL;

}

/**
 * event_operator_serialise_all:
 *
 * @root: operator tree to serialise,
 *
 * Convert EventOperator tree to JSON representation.
 *
 * Returns: JSON object containing array of EventOperator nodes in post-order,
 * or NULL on error.
 */
json_object *
event_operator_serialise_all (EventOperator *root)
{
	json_object *json;
	json_object *json_node;

	nih_assert (root);

	json = json_object_new_array ();
	if (! json)
		return NULL;

	NIH_TREE_FOREACH_POST (&root->node, iter) {
		EventOperator *oper = (EventOperator *)iter;

		json_node = event_operator_serialise (oper);
		if (! json_node)
			goto error;

		if (json_object_array_add (json, json_node) < 0)
			goto error;
	}

	return json;

error:
	json_object_put (json);
	return NULL;
}

/**
 * event_operator_deserialise:
 * @parent: parent,
 * @json: JSON-serialised EventOperator object to deserialise.
 *
 * Create EventOperator from provided JSON.
 *
 * Returns: EventOperator object, or NULL on error.
 **/
EventOperator *
event_operator_deserialise (void *parent, json_object *json)
{
	EventOperator      *oper = NULL;
	EventOperatorType   type = -1;
	nih_local char     *name = NULL;
	nih_local char    **env = NULL;

	nih_assert (json);

	if (! state_check_json_type (json, object))
		goto error;

	if (json_object_object_get (json, "name")) {
		if (! state_get_json_string_var_strict (json, "name", NULL, name))
			goto error;
	}

	if (! state_get_json_enum_var (json,
				event_operator_type_str_to_enum,
				"type", type))
		goto error;

	if (json_object_object_get (json, "env")) {
		json_object  *json_env;
		if (! state_get_json_var_full (json, "env", array, json_env))
			goto error;

		/* XXX: note that we have to treat the environment array
		 * as a plain string array (rather than an environ
		 * array) at this point since the values are not
		 * expanded (do not necessarily contain '='), and hence
		 * would be discarded by the environ-handling routines.
		 */
		if (! state_deserialise_str_array (NULL, json_env, &env))
			goto error;
	}

	oper = event_operator_new (parent, type, name, env);
	if (! oper)
		goto error;

	if (! state_get_json_int_var_to_obj (json, oper, value))
		goto error;

	if (json_object_object_get (json, "event")) {
		int event_index;

		if (! state_get_json_int_var (json, "event", event_index))
			goto error;

		oper->event = event_from_index (event_index);
		if (! oper->event)
			goto error;
	}

	return oper;

error:
	if (oper)
		nih_free (oper);

	return NULL;
}

/**
 * event_operator_deserialise_all:
 *
 * @parent: parent,
 * @json: root of JSON-serialised state.
 *
 * Convert EventOperator tree to JSON representation.
 *
 * Returns: EventOperator tree root node on success, or NULL on error.
 */
EventOperator *
event_operator_deserialise_all (void *parent, json_object *json)
{
	EventOperator      *oper = NULL;
	EventOperator      *left_oper = NULL;
	EventOperator      *right_oper = NULL;
	nih_local NihList  *stack = NULL;
	NihListEntry       *item;

	nih_assert (json);

	stack = NIH_MUST (nih_list_new (NULL));

	if (! state_check_json_type (json, array))
		goto error;

	for (int i = 0; i < json_object_array_length (json); i++) {
		json_object        *json_event_operator;
		nih_local NihList  *left = NULL;
		nih_local NihList  *right = NULL;

		json_event_operator = json_object_array_get_idx (json, i);
		if (! json_event_operator)
			goto error;

		if (! state_check_json_type (json_event_operator, object))
			goto error;

		oper = event_operator_deserialise (parent, json_event_operator);
		if (! oper)
			goto error;

		item = nih_list_entry_new (stack);
		if (! item)
			goto error;

		switch (oper->type) {
		case EVENT_AND:
		case EVENT_OR:
			left = NIH_MUST (nih_list_new (NULL));
			right = NIH_MUST (nih_list_new (NULL));

			/* pop the top two stack elements */
			nih_assert (! NIH_LIST_EMPTY (stack));
			right = nih_list_add (right, stack->next);

			nih_assert (! NIH_LIST_EMPTY (stack));
			left = nih_list_add (left, stack->next);

			left_oper = (EventOperator *)((NihListEntry *)left)->data;
			right_oper = (EventOperator *)((NihListEntry *)right)->data;

			nih_assert (left_oper);
			nih_assert (right_oper);

			/* Attach them as children of the new operator */
			nih_tree_add (&oper->node, &left_oper->node, NIH_TREE_LEFT);
			nih_tree_add (&oper->node, &right_oper->node, NIH_TREE_RIGHT);

			/* FALL THROUGH:
			 *
			 * This will re-add the operator to the stack.
			 */

		case EVENT_MATCH:
			item->data = oper;
			nih_list_add_after (stack, &item->entry);
			break;
		default:
			nih_assert_not_reached ();
		}
	}

	nih_assert (! NIH_LIST_EMPTY (stack));

	oper = ((NihListEntry *)stack->next)->data;

	nih_list_remove (stack->next);
	nih_assert (NIH_LIST_EMPTY (stack));

	return oper;

error:
	if (oper)
		nih_free (oper);

	return NULL;
}
