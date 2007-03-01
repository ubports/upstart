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
NihList *subscriptions = NULL;


/**
 * notify_init:
 *
 * Initialise the subscriptions list.
 **/
void
notify_init (void)
{
	if (! subscriptions)
		NIH_MUST (subscriptions = nih_list_new (NULL));
}


/**
 * notify_subscribe_job:
 * @parent: parent of block,
 * @pid: process id to send to,
 * @job: job to watch.
 *
 * Adjusts the subscription of process @pid by adding a subscription to
 * all changes to @job, which may be NULL to indicate that all job changes
 * should be sent.
 *
 * The subscription is allocated with nih_alloc() and stored in a linked
 * list, with a destructor set to remove it should the object be freed.
 * Removing the subscription from the list will cease notification to the
 * client.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * Returns: new EventSubscription object.
 **/
NotifySubscription *
notify_subscribe_job (const void *parent,
		      pid_t       pid,
		      Job        *job)
{
	NotifySubscription *sub;

	nih_assert (pid > 0);

	notify_init ();

	NIH_MUST (sub = nih_new (parent, NotifySubscription));
	nih_list_init (&sub->entry);

	sub->pid = pid;
	sub->type = NOTIFY_JOB;
	sub->job = job;

	nih_alloc_set_destructor (sub, (NihDestructor)nih_list_destructor);

	nih_list_add (subscriptions, &sub->entry);

	return sub;
}

/**
 * notify_subscribe_event:
 * @parent: parent of block,
 * @pid: process id to send to,
 * @emission: event emission to watch.
 *
 * Adjusts the subscription of process @pid by adding a subscription to
 * all changes caused by @emission, which may be NULL to indicate that
 * emission notification of events should be sent.
 *
 * The subscription is allocated with nih_alloc() and stored in a linked
 * list, with a destructor set to remove it should the object be freed.
 * Removing the subscription from the list will cease notification to the
 * client.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * Returns: new EventSubscription object.
 **/
NotifySubscription *
notify_subscribe_event (const void    *parent,
			pid_t          pid,
			EventEmission *emission)
{
	NotifySubscription *sub;

	nih_assert (pid > 0);

	notify_init ();

	NIH_MUST (sub = nih_new (parent, NotifySubscription));
	nih_list_init (&sub->entry);

	sub->pid = pid;
	sub->type = NOTIFY_EVENT;
	sub->emission = emission;

	nih_alloc_set_destructor (sub, (NihDestructor)nih_list_destructor);

	nih_list_add (subscriptions, &sub->entry);

	return sub;
}

/**
 * notify_subscription_find:
 * @pid: process id subscribed,
 * @type: type of subscription,
 * @ptr: Job or EventEmission, depending on @type.
 *
 * Finds the first subscription exactly matching the given details.
 *
 * Returns: subscription found or NULL if not found.
 **/
NotifySubscription *
notify_subscription_find (pid_t        pid,
			  NotifyEvent  type,
			  const void  *ptr)
{
	nih_assert (pid > 0);

	notify_init ();

	NIH_LIST_FOREACH_SAFE (subscriptions, iter) {
		NotifySubscription *sub = (NotifySubscription *)iter;

		if ((sub->pid != pid) || (sub->type != type))
			continue;

		if ((sub->type == NOTIFY_JOB) && (sub->job != ptr))
			continue;

		if ((sub->type == NOTIFY_EVENT) && (sub->emission != ptr))
			continue;

		return sub;
	}

	return NULL;
}

/**
 * notify_unsubscribe:
 * @pid: process id to remove.
 *
 * Removes all subscriptions for process @pid, normally because we have
 * received a connection refused indication for it.  Individual subscriptions
 * can be removed using the pointer returned when the subscription was made,
 * or found with notify_subscription_find().
 **/
void
notify_unsubscribe (pid_t pid)
{
	nih_assert (pid > 0);

	notify_init ();

	NIH_LIST_FOREACH_SAFE (subscriptions, iter) {
		NotifySubscription *sub = (NotifySubscription *)iter;

		if (sub->pid == pid)
			nih_list_free (&sub->entry);
	}
}


/**
 * notify_job:
 * @job: job that changed state,
 *
 * Called when a job's state changes.  Notifies subscribed processes with
 * an UPSTART_JOB_STATUS message, and if the cause is set, also
 * sends notification to subscribed process for that event with an
 * UPSTART_EVENT_JOB_STATUS message.
 **/
void
notify_job (Job *job)
{
	nih_assert (job != NULL);

	notify_init ();

	if (! control_io)
		return;

	/* First send to processes subscribed for the job. */
	NIH_LIST_FOREACH (subscriptions, iter) {
		NotifySubscription *sub = (NotifySubscription *)iter;
		NihIoMessage       *message;

		if (sub->type != NOTIFY_JOB)
			continue;

		if (sub->job && (sub->job != job))
			continue;

		NIH_MUST (message = upstart_message_new (
				  control_io, sub->pid, UPSTART_JOB_STATUS,
				  job->name, job->goal, job->state, job->pid));
		nih_io_send_message (control_io, message);
	}

	if (job->cause)
		notify_job_event (job);
}

/**
 * notify_job_event:
 * @job: job that is changing state,
 *
 * Called when a job changes state, and before a job changes cause.  Notifies
 * processes subscribed to the job's cause emission with an
 * UPSTART_EVENT_JOB_STATUS message containing the job state.
 **/
void
notify_job_event (Job *job)
{
	nih_assert (job != NULL);
	nih_assert (job->cause != NULL);

	notify_init ();

	if (! control_io)
		return;

	/* Send job status information to processes subscribed to the
	 * cause event; only send to those specifically subscribed, not to
	 * global.
	 */
	NIH_LIST_FOREACH (subscriptions, iter) {
		NotifySubscription *sub = (NotifySubscription *)iter;
		NihIoMessage       *message;

		if (sub->type != NOTIFY_EVENT)
			continue;

		if (sub->emission != job->cause)
			continue;

		NIH_MUST (message = upstart_message_new (
				  control_io, sub->pid,
				  UPSTART_EVENT_JOB_STATUS, job->cause->id,
				  job->name, job->goal, job->state, job->pid));
		nih_io_send_message (control_io, message);
	}
}

/**
 * notify_event:
 * @emission: event emission now being handled.
 *
 * Called when an event begins being handled.  Notifies subscribed processes
 * with an UPSTART_EVENT message.
 **/
void
notify_event (EventEmission *emission)
{
	nih_assert (emission != NULL);

	notify_init ();

	if (! control_io)
		return;

	NIH_LIST_FOREACH (subscriptions, iter) {
		NotifySubscription *sub = (NotifySubscription *)iter;
		NihIoMessage       *message;

		if (sub->type != NOTIFY_EVENT)
			continue;

		if (sub->emission && (sub->emission != emission))
			continue;

		NIH_MUST (message = upstart_message_new (
				  control_io, sub->pid, UPSTART_EVENT,
				  emission->id, emission->event.name,
				  emission->event.args, emission->event.env));
		nih_io_send_message (control_io, message);
	}
}

/**
 * notify_event_finished:
 * @emission: event emission now finished.
 *
 * Called when an event emission has finished.  Notifies subscribed processes
 * with an UPSTART_EVENT_FINISHED message.
 **/
void
notify_event_finished (EventEmission *emission)
{
	nih_assert (emission != NULL);

	notify_init ();

	if (! control_io)
		return;

	NIH_LIST_FOREACH (subscriptions, iter) {
		NotifySubscription *sub = (NotifySubscription *)iter;
		NihIoMessage       *message;

		if (sub->type != NOTIFY_EVENT)
			continue;

		if (sub->emission && (sub->emission != emission))
			continue;

		NIH_MUST (message = upstart_message_new (
				  control_io, sub->pid, UPSTART_EVENT_FINISHED,
				  emission->id, emission->failed,
				  emission->event.name,
				  emission->event.args, emission->event.env));
		nih_io_send_message (control_io, message);
	}
}
