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

#ifndef UPSTART_EVENT_H
#define UPSTART_EVENT_H

#include <nih/macros.h>
#include <nih/list.h>


/**
 * Event:
 * @entry: list header,
 * @name: string name of the event; namespace shared with jobs,
 * @value: current or expected value for level events.
 *
 * We talk about two types of events, level events which have a @value
 * associated and edge events which do not (@value is %NULL).  The change
 * of any level event's @value is also considered an edge event.
 *
 * This structure is used within the event code to hold a record of previously
 * triggered edge events and the current value of level events; it is also
 * used within the job code to hold the list of events a job is waiting for.
 **/
typedef struct event {
	NihList  entry;

	char    *name;
	char    *value;
} Event;


NIH_BEGIN_EXTERN

Event *event_new           (void *parent, const char *name);
Event *event_record        (void *parent, const char *name);

Event *event_find_by_name  (const char *name);

int    event_change_value  (Event *event, const char *value);

void   event_trigger_edge  (const char *name);
void   event_trigger_level (const char *name, const char *value);

NIH_END_EXTERN

#endif /* UPSTART_EVENT_H */
