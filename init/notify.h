/* upstart
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

#ifndef INIT_NOTIFY_H
#define INIT_NOTIFY_H

#include <sys/types.h>

#include <nih/macros.h>

#include "job.h"
#include "event.h"


/**
 * NotifyEvents:
 *
 * Events we notify subscribed processes of, used as a bitmask in the
 * Subscription structure.
 **/
typedef enum {
	NOTIFY_NONE   = 00,
	NOTIFY_JOBS   = 01,
	NOTIFY_EVENTS = 02
} NotifyEvents;


/**
 * NotifySubscription:
 * @entry: list header,
 * @pid: subscribed process,
 * @notify: notify events subscribed to.
 *
 * This structure is used to allow processes to subscribe to notification
 * of changes in event level or job status.  @notify is a bitmask of which
 * of the two events (or both) to receive, it is never allowed to be zero
 * as that's an unsubscription.
 **/
typedef struct notify_subscription {
	NihList      entry;
	pid_t        pid;
	NotifyEvents notify;
} NotifySubscription;


NIH_BEGIN_EXTERN

NotifySubscription *notify_subscribe (pid_t pid, NotifyEvents notify, int set);

void                notify_job       (Job *job);
void                notify_event     (Event *event);

NIH_END_EXTERN

#endif /* INIT_NOTIFY_H */
