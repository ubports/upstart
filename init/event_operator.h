/* upstart
 *
 * Copyright © 2009 Canonical Ltd.
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

#ifndef INIT_EVENT_OPERATOR_H
#define INIT_EVENT_OPERATOR_H

#include <nih/macros.h>
#include <nih/tree.h>

#include "event.h"


/**
 * EventOperatorType:
 *
 * This is used to distinguish between the different boolean behaviours of
 * the EventOperator structure.
 **/
typedef enum event_operator_type {
	EVENT_OR,
	EVENT_AND,
	EVENT_MATCH
} EventOperatorType;

/**
 * EventOperator:
 * @node: tree node,
 * @type: operator type,
 * @value: operator value,
 * @name: name of event to match (EVENT_MATCH only),
 * @env: environment variables of event to match (EVENT_MATCH only),
 * @event: event matched (EVENT_MATCH only).
 *
 * This structure is used to build up an event expression tree; the leaf
 * nodes are all of EVENT_MATCH type which match a specific event, the other
 * nodes are built up of EVENT_OR and EVENT_AND operators that combine the
 * EventOperators to their left and right in interesting ways.
 *
 * @value indicates whether this operator is currently TRUE or FALSE.
 * For EVENT_MATCH operators, a TRUE @value means that @event is set to
 * the matched event; for EVENT_OR and EVENT_AND operators, @value is set
 * depending on the value of both immediate children.
 *
 * Once an event has been matched, the @event member is set and a reference
 * held until the structure is cleared.
 **/
typedef struct event_operator {
	NihTree             node;
	EventOperatorType   type;

	int                 value;

	char               *name;
	char              **env;

	Event              *event;
} EventOperator;


NIH_BEGIN_EXTERN

EventOperator *event_operator_new         (const void *parent,
					   EventOperatorType type,
					   const char *name, char **env)
	__attribute__ ((warn_unused_result));
EventOperator *event_operator_copy        (const void *parent,
					   const EventOperator *old_oper)
	__attribute__ ((warn_unused_result));

int            event_operator_destroy     (EventOperator *oper);

void           event_operator_update      (EventOperator *oper);
int            event_operator_match       (EventOperator *oper, Event *event,
					   char * const *env);

int            event_operator_handle      (EventOperator *root, Event *event,
					   char * const *env);

char **        event_operator_environment (EventOperator *root, char ***env,
					   const void *parent, size_t *len,
					   const char *key);
int *
event_operator_fds (EventOperator   *root,
		    const void      *parent,
		    int            **fds,
		    size_t          *num_fds,
		    char          ***env,
		    size_t          *len,
		    const char      *key);
void           event_operator_events      (EventOperator *root,
					   const void *parent, NihList *list);

void           event_operator_reset       (EventOperator *root);

char *event_operator_collapse (EventOperator *condition)
	__attribute__ ((warn_unused_result));

const char *
event_operator_type_enum_to_str (EventOperatorType type)
	__attribute__ ((warn_unused_result));

EventOperatorType
event_operator_type_str_to_enum (const char *type)
	__attribute__ ((warn_unused_result));

json_object *
event_operator_serialise (const EventOperator *oper)
	__attribute__ ((warn_unused_result));

json_object *
event_operator_serialise_all (EventOperator *root)
	__attribute__ ((warn_unused_result));

EventOperator *
event_operator_deserialise (void *parent, json_object *json)
	__attribute__ ((warn_unused_result));

EventOperator *
event_operator_deserialise_all (void *parent, json_object *json)
	__attribute__ ((warn_unused_result));

NIH_END_EXTERN

#endif /* INIT_EVENT_OPERATOR_H */
