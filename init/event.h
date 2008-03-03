/* upstart
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

#ifndef INIT_EVENT_H
#define INIT_EVENT_H

#include <stdio.h>

#include <nih/macros.h>
#include <nih/list.h>
#include <nih/tree.h>
#include <nih/main.h>


/**
 * EventProgress:
 *
 * This is used to record the progress of an event, starting at
 * being pending, then being handled and finally waiting for the callback
 * to be called and any cleanup performed.
 **/
typedef enum event_progress {
	EVENT_PENDING,
	EVENT_HANDLING,
	EVENT_FINISHED
} EventProgress;

/**
 * Event:
 * @entry: list header,
 * @id: unique id assigned to each event,
 * @name: string name of the event,
 * @env: NULL-terminated list of environment variables.
 * @progress: progress of event,
 * @failed: whether this event has failed,
 * @blockers: number of blockers for finishing.
 *
 * Events are one of the core concepts of upstart; they occur whenever
 * something, somewhere changes state.  They are idenitied by a unique
 * @name string, and can carry further information in the form of @env
 * which are passed to any jobs whose goal is changed by this event.
 *
 * This structure holds all the information on an active event, including
 * the information contained within the event and the current progress of
 * that event through the queue.
 *
 * Events remain in the handling state while @blockers is non-zero.
 **/
typedef struct event {
	NihList          entry;
	unsigned int     id;

 	char            *name;
	char           **env;

	EventProgress    progress;
	int              failed;

	unsigned int     blockers;
} Event;


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


/**
 * STARTUP_EVENT:
 *
 * Name of the event that we generate when init is first executed.
 **/
#ifndef DEBUG
#define STARTUP_EVENT "startup"
#else /* DEBUG */
#define STARTUP_EVENT "debug"
#endif

/**
 * STALLED_EVENT:
 *
 * Name of the event that we generate if the system stalls (all jobs are
 * stopped/waiting)
 **/
#define STALLED_EVENT "stalled"

/**
 * CTRLALTDEL_EVENT:
 *
 * Name of the event that we generate when the Control-Alt-Delete key
 * combination is pressed.
 **/
#define CTRLALTDEL_EVENT "control-alt-delete"

/**
 * KBDREQUEST_EVENT:
 *
 * Name of the event that we generate when the Alt-UpArrow key combination
 * is pressed.
 **/
#define KBDREQUEST_EVENT "kbdrequest"

/**
 * PWRSTATUS_EVENT:
 *
 * Name of the event that we generate when we receive SIGPWR, indicating
 * that the power status has changed.
 **/
#define PWRSTATUS_EVENT "power-status-changed"


/**
 * JOB_STARTING_EVENT:
 *
 * Name of the event we generate when we're ready to start a job; the job
 * is not actually started until the handling of this event finishes.
 **/
#define JOB_STARTING_EVENT "starting"

/**
 * JOB_STARTED_EVENT:
 *
 * Name of the event we generate once a job has been started and is now
 * running.  This is not generated until the spawned pid is located (if
 * appropriate) and the post-start script has finished.
 **/
#define JOB_STARTED_EVENT "started"

/**
 * JOB_STOPPING_EVENT:
 *
 * Name of the event we generate when we're ready to stop a job, which
 * includes arguments and environment indicating whether the job failed.
 * This is run after the pre-stop script has finished without setting the
 * goal back to start.  The job is not actually stopped until the handling
 * of this event finishes.
 **/
#define JOB_STOPPING_EVENT "stopping"

/**
 * JOB_STOPPED_EVENT:
 *
 * Name of the event we generate once a job has been stopped and is now
 * waiting.
 **/
#define JOB_STOPPED_EVENT "stopped"


NIH_BEGIN_EXTERN

int           paused;
unsigned int  event_id;
int           event_id_wrapped;
NihList      *events;


void           event_init             (void);

Event         *event_new              (const void *parent, const char *name,
				       char **env)
	__attribute__ ((malloc));

Event         *event_find_by_id       (unsigned int id);

void           event_block            (Event *event);
void           event_unblock          (Event *event);

void           event_poll             (void);


EventOperator *event_operator_new     (const void *parent,
				       EventOperatorType type,
				       const char *name, char **env)
	__attribute__ ((warn_unused_result, malloc));
EventOperator *event_operator_copy    (const void *parent,
				       const EventOperator *old_oper)
	__attribute__ ((warn_unused_result, malloc));

int            event_operator_destroy (EventOperator *oper);

void           event_operator_update  (EventOperator *oper);
int            event_operator_match   (EventOperator *oper, Event *event);

int            event_operator_handle  (EventOperator *root, Event *event);

void           event_operator_collect (EventOperator *root, char ***env,
				       const void *parent, size_t *len,
				       const char *key, NihList *list);

void           event_operator_reset   (EventOperator *root);

NIH_END_EXTERN

#endif /* INIT_EVENT_H */
