/* upstart
 *
 * notify.c - subscription to and notification of job changes and events
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <sys/types.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/io.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <upstart/message.h>

#include "job.h"
#include "event.h"
#include "control.h"
#include "notify.h"


/**
 * subscriptions:
 *
 * List of processes that are subscribed to changes in events or job status.
 * Each item is a NotifySubscription structure, in no particular order.
 **/
static NihList *subscriptions = NULL;


/**
 * notify_init:
 *
 * Initialise the subscriptions list.
 **/
static void
notify_init (void)
{
	if (! subscriptions)
		NIH_MUST (subscriptions = nih_list_new (NULL));
}


/**
 * notify_subscribe:
 * @pid: process id to send to,
 * @notify: notify events mask to change,
 * @set: TRUE to add events, FALSE to remove.
 *
 * Adjusts the subscription of process @pid by adding the events listed
 * in @notify if @set is TRUE or removing if @set is FALSE.  Removing
 * all subscribed events removes the subscription.
 *
 * The current subscription can be found by passing NOTIFY_NONE.
 *
 * Returns: subscription record on success, NULL on removal of subscription.
 **/
NotifySubscription *
notify_subscribe (pid_t        pid,
		  NotifyEvents notify,
		  int          set)
{
	NotifySubscription *sub;

	nih_assert (pid > 0);

	notify_init ();

	/* Amend existing subscription record */
	NIH_LIST_FOREACH (subscriptions, iter) {
		sub = (NotifySubscription *)iter;

		if (sub->pid != pid)
			continue;

		if (set) {
			sub->notify |= notify;
		} else {
			sub->notify &= ~notify;
		}

		if (sub->notify) {
			return sub;
		} else {
			nih_list_free (&sub->entry);
			return NULL;
		}
	}

	/* Not adding anything, and no existing record */
	if ((! set) || (! notify))
		return NULL;

	/* Create new subscription record */
	NIH_MUST (sub = nih_new (NULL, NotifySubscription));

	nih_list_init (&sub->entry);

	sub->pid = pid;
	sub->notify = notify;

	nih_list_add (subscriptions, &sub->entry);

	return sub;
}


/**
 * notify_job:
 * @job: job that changed state,
 *
 * Called when a job's state changes.  Notifies subscribed processes with
 * an UPSTART_JOB_STATUS message.
 **/
void
notify_job (Job *job)
{
	nih_assert (job != NULL);

	notify_init ();

	if (! control_io)
		return;

	NIH_LIST_FOREACH (subscriptions, iter) {
		NotifySubscription *sub = (NotifySubscription *)iter;
		NihIoMessage       *message;

		if (! (sub->notify & NOTIFY_JOBS))
			continue;

		NIH_MUST (message = upstart_message_new (control_io, sub->pid,
							 UPSTART_JOB_STATUS,
							 job->name, job->goal,
							 job->state,
							 job->process_state,
							 job->pid,
							 job->description));
		nih_io_send_message (control_io, message);
	}
}

/**
 * notify_event:
 * @event: event emitted.
 *
 * Called when an event is emitted.  Notifies subscribed processes with
 * an UPSTART_EVENT message.
 **/
void
notify_event (Event *event)
{
	nih_assert (event != NULL);

	notify_init ();

	if (! control_io)
		return;

	NIH_LIST_FOREACH (subscriptions, iter) {
		NotifySubscription *sub = (NotifySubscription *)iter;
		NihIoMessage       *message;

		if (! (sub->notify & NOTIFY_EVENTS))
			continue;

		NIH_MUST (message = upstart_message_new (control_io, sub->pid,
							 UPSTART_EVENT,
							 event->name));
		nih_io_send_message (control_io, message);
	}
}
