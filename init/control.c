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
 * control_init:
 *
 * Initialise the send queue.
 **/
static void
control_init (void)
{
	if (! send_queue)
		NIH_MUST (send_queue = nih_list_new ());
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
				}

				nih_warn (_("Error sending control message: %s"),
					  err->message);

				if ((err->number == UPSTART_INVALID_MESSAGE)
				    || (err->number == ECONNREFUSED)) {
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
	}
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