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
 * @event: event to watch.
 *
 * Adjusts the subscription of process @pid by adding a subscription to
 * all changes caused by @event, which may be NULL to indicate that
 * notification of all events should be sent.
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
notify_subscribe_event (const void *parent,
			pid_t       pid,
			Event      *event)
{
	NotifySubscription *sub;

	nih_assert (pid > 0);

	notify_init ();

	NIH_MUST (sub = nih_new (parent, NotifySubscription));
	nih_list_init (&sub->entry);

	sub->pid = pid;
	sub->type = NOTIFY_EVENT;
	sub->event = event;

	nih_alloc_set_destructor (sub, (NihDestructor)nih_list_destructor);

	nih_list_add (subscriptions, &sub->entry);

	return sub;
}

/**
 * notify_subscription_find:
 * @pid: process id subscribed,
 * @type: type of subscription,
 * @ptr: Job or Event, depending on @type.
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

		if ((sub->type == NOTIFY_EVENT) && (sub->event != ptr))
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
 * Called when a job's state changes.  Notifies all processes subscribed to
 * that job, those subscribed to any job state change event and also those
 * subscribed to the event that caused the change.
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

		if (sub->type != NOTIFY_JOB)
			continue;

		if (sub->job && (sub->job != job))
			continue;

		control_send_job_status (sub->pid, job);
	}

	if (job->cause)
		notify_job_event (job);
}

/**
 * notify_job_event:
 * @job: job that is changing state,
 *
 * Called when a job's state changes, and before the job's cause changes.
 * Notifies all processes subscribed to the event, prefixing the job status
 * message with an UPSTART_EVENT_CAUSED message to link the two.
 **/
void
notify_job_event (Job *job)
{
	nih_assert (job != NULL);
	nih_assert (job->cause != NULL);

	notify_init ();

	if (! control_io)
		return;

	NIH_LIST_FOREACH (subscriptions, iter) {
		NotifySubscription *sub = (NotifySubscription *)iter;
		NihIoMessage       *message;

		if (sub->type != NOTIFY_EVENT)
			continue;

		if (sub->event != job->cause)
			continue;

		NIH_MUST (message = upstart_message_new (
				  control_io, sub->pid, UPSTART_EVENT_CAUSED,
				  job->cause->id));
		nih_io_send_message (control_io, message);

		control_send_job_status (sub->pid, job);
	}
}

/**
 * notify_job_finished:
 * @job: job that changed state,
 *
 * Called when a job's state reaches the goal rest state.  Notifies all
 * processes subscribed to that job with a final status update followed by
 * an UPSTART_JOB_FINISHED message and unsubscribes them from future
 * notifications.
 **/
void
notify_job_finished (Job *job)
{
	nih_assert (job != NULL);

	notify_init ();

	if (! control_io)
		return;

	NIH_LIST_FOREACH_SAFE (subscriptions, iter) {
		NotifySubscription *sub = (NotifySubscription *)iter;
		NihIoMessage       *message;

		if (sub->type != NOTIFY_JOB)
			continue;

		if (sub->job != job)
			continue;

		control_send_job_status (sub->pid, job);

		NIH_MUST (message = upstart_message_new (
				  control_io, sub->pid, UPSTART_JOB_FINISHED,
				  job->id, job->name, job->failed,
				  job->failed_process, job->exit_status));
		nih_io_send_message (control_io, message);

		nih_list_free (&sub->entry);
	}
}

/**
 * notify_event:
 * @event: event now being handled.
 *
 * Called when an event begins being handled.  Notifies subscribed processes
 * with an UPSTART_EVENT message.
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

		if (sub->type != NOTIFY_EVENT)
			continue;

		if (sub->event && (sub->event != event))
			continue;

		NIH_MUST (message = upstart_message_new (
				  control_io, sub->pid, UPSTART_EVENT,
				  event->id, event->info.name,
				  event->info.args, event->info.env));
		nih_io_send_message (control_io, message);
	}
}

/**
 * notify_event_finished:
 * @event: event now finished.
 *
 * Called when an event has finished.  Notifies all processes
 * subscribed to that event with an UPSTART_EVENT_FINISHED message
 * and unsubscribes them from future notifications.
 **/
void
notify_event_finished (Event *event)
{
	nih_assert (event != NULL);

	notify_init ();

	if (! control_io)
		return;

	NIH_LIST_FOREACH_SAFE (subscriptions, iter) {
		NotifySubscription *sub = (NotifySubscription *)iter;
		NihIoMessage       *message;

		if (sub->type != NOTIFY_EVENT)
			continue;

		if (sub->event != event)
			continue;

		NIH_MUST (message = upstart_message_new (
				  control_io, sub->pid, UPSTART_EVENT_FINISHED,
				  event->id, event->failed, event->info.name,
				  event->info.args, event->info.env));
		nih_io_send_message (control_io, message);

		nih_list_free (&sub->entry);
	}
}
