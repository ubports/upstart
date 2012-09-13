/* upstart
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

#ifndef INIT_EVENT_H
#define INIT_EVENT_H

#include <nih/macros.h>
#include <nih/list.h>

#include "session.h"
#include "state.h"

#include <json.h>

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
 * @session: session the event is attached to,
 * @name: string name of the event,
 * @env: NULL-terminated array of environment variables,
 * @progress: progress of event,
 * @failed: whether this event has failed,
 * @blockers: number of blockers for finishing,
 * @blocking: messages and jobs we're blocking.
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

	Session *        session;
 	char            *name;
	char           **env;
	int              fd;

	EventProgress    progress;
	int              failed;

	unsigned int     blockers;
	NihList          blocking;
} Event;


NIH_BEGIN_EXTERN

extern int      paused;
extern NihList *events;


void   event_init    (void);

Event *event_new     (const void *parent, const char *name, char **env)
	__attribute__ ((malloc));

void   event_block   (Event *event);
void   event_unblock (Event *event);

void   event_poll    (void);

json_object *event_serialise (const Event *event)
	__attribute__ ((malloc, warn_unused_result));

Event *event_deserialise (json_object *json)
	__attribute__ ((malloc, warn_unused_result));

json_object  * event_serialise_all (void)
	__attribute__ ((malloc, warn_unused_result));

int            event_deserialise_all (json_object *json)
	__attribute__ ((warn_unused_result));

int    event_to_index (const Event *event)
	__attribute__ ((warn_unused_result));

Event * event_from_index (int event_index)
	__attribute__ ((warn_unused_result));

NIH_END_EXTERN

#endif /* INIT_EVENT_H */
