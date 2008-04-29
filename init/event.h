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

#include <nih/macros.h>
#include <nih/list.h>


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
 * @name: string name of the event,
 * @env: NULL-terminated array of environment variables,
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

 	char            *name;
	char           **env;

	EventProgress    progress;
	int              failed;

	unsigned int     blockers;
} Event;


NIH_BEGIN_EXTERN

int      paused;
NihList *events;


void   event_init    (void);

Event *event_new     (const void *parent, const char *name, char **env)
	__attribute__ ((malloc));

void   event_block   (Event *event);
void   event_unblock (Event *event);

void   event_poll    (void);

NIH_END_EXTERN

#endif /* INIT_EVENT_H */
