/* upstart
 *
 * Copyright © 2006 Canonical Ltd.
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

#ifndef INIT_EVENT_H
#define INIT_EVENT_H

#include <stdio.h>

#include <nih/macros.h>
#include <nih/list.h>
#include <nih/main.h>


/**
 * Event:
 * @entry: list header,
 * @name: string name of the event.
 *
 * Events occur whenever something, somewhere changes state.  They are
 * placed in the event queue and can cause jobs to change their goal to
 * start or stop.
 *
 * Once processed, they are forgotten about.  The state is stored by the
 * event generator (the job state machine or external process) and upstart
 * makes no attempt to track it.
 **/
typedef struct event {
	NihList  entry;

	char    *name;
} Event;


NIH_BEGIN_EXTERN

int paused;


Event *event_new         (void *parent, const char *name)
	__attribute__ ((warn_unused_result, malloc));

int    event_match       (Event *event1, Event *event2);

Event *event_queue       (const char *name);
void   event_queue_run   (void);

Event *event_read_state  (Event *event, char *buf);
void   event_write_state (FILE *state);

NIH_END_EXTERN

#endif /* INIT_EVENT_H */
