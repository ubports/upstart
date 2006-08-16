/* upstart
 *
 * control.c - handling of control socket requests
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <sys/types.h>

#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/io.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <upstart/control.h>
#include <upstart/errors.h>

#include "job.h"
#include "control.h"


/* Prototypes for static functions */
static void control_cb     (void *data, NihIoWatch *watch, NihIoEvents events);
static void control_handle (pid_t pid, UpstartMsg *msg);


/**
 * io_watch:
 *
 * The NihIoWatch being used to watch the control socket.
 **/
static NihIoWatch *io_watch = NULL;

/**
 * send_queue:
 *
 * List of messages that are queued up to be sent when the socket is
 * available for writing; done this way to avoid blocking on malicious
 * clients.
 **/
static NihList *send_queue = NULL;

/**
 * subscriptions:
 *
 * List of processes that are subscribed to changes in events or job status.
 **/
static NihList *subscriptions = NULL;


/**
 * control_init:
 *
 * Initialise the send queue and subscriptions list.
 **/
static void
control_init (void)
{
	if (! send_queue)
		NIH_MUST (send_queue = nih_list_new ());

	if (! subscriptions)
		NIH_MUST (subscriptions = nih_list_new ());
}


/**
 * control_open:
 *
 * Opens the control socket and sets it up so that incoming messages are
 * dealt with and outgoing messages can be queued.
 *
 * Returns: watch on socket on success, %NULL on raised error.
 **/
NihIoWatch *
control_open (void)
{
	NihIoEvents events;
	int         sock;

	nih_assert (io_watch == NULL);

	control_init ();

	/* Open the socket */
	sock = upstart_open ();
	if (sock < 0)
		return NULL;

	/* Set various sensible flags */
	if (nih_io_set_nonblock (sock) < 0)
		goto error;
	if (nih_io_set_cloexec (sock) < 0)
		goto error;

	events = NIH_IO_READ;
	if (! NIH_LIST_EMPTY (send_queue))
		events |= NIH_IO_WRITE;

	io_watch = nih_io_add_watch (NULL, sock, events, control_cb, NULL);
	if (! io_watch) {
		close (sock);
		errno = ENOMEM;
		nih_return_system_error (NULL);
	}

	return io_watch;

error:
	close (sock);
	return NULL;
}

/**
 * control_close:
 *
 * Close the currently open control socket and free the structure watching
 * it.  This does NOT clear the queue of messages to be sent, those will
 * be sent should the socket be re-opened.
 **/
void
control_close (void)
{
	nih_assert (io_watch != NULL);

	close (io_watch->fd);

	nih_free (io_watch);
	io_watch = NULL;
}


/**
 * control_subscribe:
 * @pid: process id to send to,
 * @notify: notify events mask to change,
 * @set: %TRUE to add events, %FALSE to remove.
 *
 * Adjusts the subscription of process @pid by adding the events listed
 * in @notify if @set is %TRUE or removing if @set is %FALSE.  Removing
 * all subscribed events removes the subscription.
 *
 * Returns: subscription record on success, %NULL on insufficient memory
 * or removal of subscription.
 **/
ControlSub *
control_subscribe (pid_t        pid,
		   NotifyEvents notify,
		   int          set)
{
	ControlSub *sub;

	nih_assert (pid > 0);
	nih_assert (notify != 0);

	control_init ();

	/* Amend existing subscription record */
	NIH_LIST_FOREACH (subscriptions, iter) {
		sub = (ControlSub *)iter;

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
	if (! set)
		return NULL;

	/* Create new subscription record */
	sub = nih_new (NULL, ControlSub);
	if (! sub)
		return NULL;

	nih_list_init (&sub->entry);

	sub->pid = pid;
	sub->notify = notify;

	nih_list_add (subscriptions, &sub->entry);

	return sub;
}


/**
 * control_send:
 * @pid: destination,
 * @message: message to be sent.
 *
 * Queue @message to be send to process @pid when next possible.  @message
 * is copied internally, including any pointers (such as the name).
 *
 * The message can be cancelled by using #nih_list_free on the returned
 * structure.
 *
 * Returns: entry in the send queue on success, %NULL on insufficient memory.
 **/
ControlMsg *
control_send (pid_t       pid,
	      UpstartMsg *message)
{
	ControlMsg *msg;

	nih_assert (pid > 0);
	nih_assert (message != NULL);

	control_init ();

	msg = nih_new (NULL, ControlMsg);
	if (! msg)
		return NULL;

	nih_list_init (&msg->entry);

	msg->pid = pid;

	/* Deep copy the message */
	memcpy (&msg->message, message, sizeof (msg->message));
	switch (message->type) {
	case UPSTART_JOB_START:
	case UPSTART_JOB_STOP:
	case UPSTART_JOB_QUERY:
	case UPSTART_JOB_STATUS:
	case UPSTART_JOB_UNKNOWN:
		msg->message.job_query.name = nih_strdup (
			msg, message->job_query.name);
		break;
	case UPSTART_EVENT_TRIGGER_LEVEL:
	case UPSTART_EVENT_TRIGGERED:
		if (message->event_triggered.level) {
			msg->message.event_triggered.level = nih_strdup (
				msg, message->event_triggered.level);
		}
	case UPSTART_EVENT_TRIGGER_EDGE:
		msg->message.event_triggered.name = nih_strdup (
			msg, message->event_triggered.name);
		break;
	default:
		break;
	}

	nih_list_add (send_queue, &msg->entry);

	if (io_watch)
		io_watch->events |= NIH_IO_WRITE;

	return msg;
}


/**
 * control_cb:
 * @data: not used,
 * @watch: watch on socket,
 * @events: events that occurred.
 *
 * This function is called whenever we are able to read from the control
 * socket or there is data in the send queue and we are able to write to
 * the control socket.
 *
 * As many messages as possible are read from the socket and handled, and
 * as many queued messages as possible are sent.
 **/
static void control_cb (void        *data,
			NihIoWatch  *watch,
			NihIoEvents  events)
{
	nih_assert (watch != NULL);

	/* Messages to be read */
	if (events & NIH_IO_READ) {
		for (;;) {
			UpstartMsg *msg;
			pid_t       pid;

			msg = upstart_recv_msg (NULL, watch->fd, &pid);
			if (! msg) {
				NihError *err;

				err = nih_error_get ();
				if (err->number == EAGAIN) {
					nih_free (err);
					break;
				} else if (err->number == EINTR) {
					nih_free (err);
					continue;
				}

				nih_warn (_("Error reading control message: %s"),
					  err->message);

				if (err->number == UPSTART_INVALID_MESSAGE) {
					nih_free (err);
					continue;
				} else {
					nih_free (err);
					break;
				}
			}

			control_handle (pid, msg);
			nih_free (msg);
		}
	}

	/* Messages to send */
	if (events & NIH_IO_WRITE) {
		NIH_LIST_FOREACH_SAFE (send_queue, iter) {
			ControlMsg *msg = (ControlMsg *)iter;

			if (upstart_send_msg_to (msg->pid, watch->fd,
						 &msg->message) < 0) {
				NihError *err;

				err = nih_error_get ();
				if (err->number == EAGAIN) {
					nih_free (err);
					break;
				} else if (err->number == EINTR) {
					nih_free (err);
					continue;
				} else if (err->number == ECONNREFUSED) {
					nih_free (err);
					nih_list_free (&msg->entry);

					control_subscribe
						(msg->pid,
						 NOTIFY_JOBS | NOTIFY_EVENTS,
						 FALSE);

					continue;
				}

				nih_warn (_("Error sending control message: %s"),
					  err->message);

				if (err->number == UPSTART_INVALID_MESSAGE) {
					nih_free (err);
					nih_list_free (&msg->entry);
					continue;
				} else {
					nih_free (err);
					break;
				}
			}

			nih_list_free (&msg->entry);
		}

		/* Don't poll for write if we've nothing to write */
		if (NIH_LIST_EMPTY (send_queue))
			watch->events &= ~NIH_IO_WRITE;
	}
}


/**
 * control_handle:
 * @pid: sender process,
 * @message: message sent.
 *
 * This function is called to handle messages received from clients.  The
 * process id of the client that sent @message is given in @pid.
 *
 * Appropriate action, if any, is taken which often includes sending a
 * message back to the client.
 **/
static void
control_handle (pid_t       pid,
		UpstartMsg *msg)
{
	UpstartMsg *reply = NULL;

	nih_assert (pid > 0);
	nih_assert (msg != NULL);

	switch (msg->type) {
	case UPSTART_NO_OP:
		/* No action */
		break;
	case UPSTART_JOB_START:
	case UPSTART_JOB_STOP:
	case UPSTART_JOB_QUERY: {
		Job *job;

		job = job_find_by_name (msg->job_start.name);
		if (! job) {
			reply = nih_new (NULL, UpstartMsg);
			reply->type = UPSTART_JOB_UNKNOWN;
			reply->job_unknown.name = msg->job_start.name;
			break;
		}

		if (msg->type == UPSTART_JOB_START) {
			nih_info (_("Control request to start %s"), job->name);
			job_start (job);
		} else if (msg->type == UPSTART_JOB_STOP) {
			nih_info (_("Control request to stop %s"), job->name);
			job_stop (job);
		} else {
			nih_info (_("Control request for state of %s"),
				  job->name);
		}

		reply = nih_new (NULL, UpstartMsg);
		reply->type = UPSTART_JOB_STATUS;
		reply->job_status.name = msg->job_query.name;
		reply->job_status.goal = job->goal;
		reply->job_status.state = job->state;
		reply->job_status.process_state = job->process_state;
		reply->job_status.pid = job->pid;

		break;
	}
	case UPSTART_EVENT_TRIGGER_EDGE:
		nih_info (_("Control request to trigger %s"),
			  msg->event_trigger_edge.name);

		control_subscribe (pid, NOTIFY_JOBS, TRUE);
		event_trigger_edge (msg->event_trigger_edge.name);
		control_subscribe (pid, NOTIFY_JOBS, FALSE);

		reply = nih_new (NULL, UpstartMsg);
		reply->type = UPSTART_EVENT_TRIGGERED;
		reply->event_triggered.name = msg->event_trigger_edge.name;
		reply->event_triggered.level = NULL;

		break;
	case UPSTART_EVENT_TRIGGER_LEVEL:
		if (! msg->event_trigger_level.level)
			break;

		nih_info (_("Control request to trigger %s %s"),
			  msg->event_trigger_level.name,
			  msg->event_trigger_level.level);

		control_subscribe (pid, NOTIFY_JOBS, TRUE);
		event_trigger_level (msg->event_trigger_level.name,
				     msg->event_trigger_level.level);
		control_subscribe (pid, NOTIFY_JOBS, FALSE);

		reply = nih_new (NULL, UpstartMsg);
		reply->type = UPSTART_EVENT_TRIGGERED;
		reply->event_triggered.name = msg->event_trigger_level.name;
		reply->event_triggered.level = msg->event_trigger_level.level;

		break;
	default:
		/* Unknown message */
		nih_debug ("Unhandled control message %d", msg->type);
	}

	/* Send the reply */
	if (reply) {
		NIH_MUST (control_send (pid, reply));
		nih_free (reply);
	}
}


/**
 * control_handle_job:
 * @job: job that changed state,
 *
 * Called when a job's state changes.  Notifies subscribed processes with
 * an UPSTART_JOB_STATUS message.
 **/
void
control_handle_job (Job *job)
{
	UpstartMsg msg;

	nih_assert (job != NULL);

	control_init ();

	msg.type = UPSTART_JOB_STATUS;
	msg.job_status.name = job->name;
	msg.job_status.goal = job->goal;
	msg.job_status.state = job->state;
	msg.job_status.process_state = job->process_state;
	msg.job_status.pid = job->pid;

	NIH_LIST_FOREACH (subscriptions, iter) {
		ControlSub *sub = (ControlSub *)iter;

		if (sub->notify & NOTIFY_JOBS)
			NIH_MUST (control_send (sub->pid, &msg));
	}
}

/**
 * control_handle_event:
 * @event: event triggered.
 *
 * Called when an edge event is triggered or the value of a level event
 * is changed.  Notifies subscribed processes with an UPSTART_EVENT_TRIGGERED
 * message.
 **/
void
control_handle_event (Event *event)
{
	UpstartMsg msg;

	nih_assert (event != NULL);

	control_init ();

	msg.type = UPSTART_EVENT_TRIGGERED;
	msg.event_triggered.name = event->name;
	msg.event_triggered.level = event->value;

	NIH_LIST_FOREACH (subscriptions, iter) {
		ControlSub *sub = (ControlSub *)iter;

		if (sub->notify & NOTIFY_EVENTS)
			NIH_MUST (control_send (sub->pid, &msg));
	}
}
