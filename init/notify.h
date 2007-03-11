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
 * NotifyEvent:
 *
 * Types of changes we notify subscribed process about.
 **/
typedef enum notify_event {
	NOTIFY_JOB,
	NOTIFY_EVENT
} NotifyEvent;


/**
 * NotifySubscription:
 * @entry: list header,
 * @pid: subscribed process,
 * @type: event subscribed to,
 * @job: job being watched,
 * @emission: event emission being watched.
 *
 * This structure is used to allow processes to subscribe to notification
 * of events or changes to job status.  @notify specifies which of @job
 * or @emission to look at, this can be NULL to indicate that all jobs
 * or events are interesting.
 **/
typedef struct notify_subscription {
	NihList      entry;
	pid_t        pid;
	NotifyEvent  type;

	union {
		Job           *job;
		EventEmission *emission;
	};
} NotifySubscription;


NIH_BEGIN_EXTERN

NihList *subscriptions;


void                notify_init              (void);

NotifySubscription *notify_subscribe_job     (const void *parent, pid_t pid,
					      Job *job)
	__attribute__ ((malloc));

NotifySubscription *notify_subscribe_event   (const void *parent, pid_t pid,
					      EventEmission *emission)
	__attribute__ ((malloc));

void                notify_unsubscribe       (pid_t pid);

NotifySubscription *notify_subscription_find (pid_t pid, NotifyEvent type,
					      const void *ptr);

void                notify_job               (Job *job);
void                notify_job_event         (Job *job);
void                notify_job_finished      (Job *job);
void                notify_event             (EventEmission *emission);
void                notify_event_finished    (EventEmission *emission);

NIH_END_EXTERN

#endif /* INIT_NOTIFY_H */
