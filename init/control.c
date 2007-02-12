/* upstart
 *
 * control.c - handling of control socket requests
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

#include <errno.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/hash.h>
#include <nih/io.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <upstart/message.h>

#include "job.h"
#include "control.h"
#include "notify.h"


/* Prototypes for static functions */
static void control_error_handler  (void  *data, NihIo *io);
static int  control_job_start      (void *data, pid_t pid,
				    UpstartMessageType type, const char *name);
static int  control_job_stop       (void *data, pid_t pid,
				    UpstartMessageType type, const char *name);
static int  control_job_query      (void *data, pid_t pid,
				    UpstartMessageType type, const char *name);
static int  control_job_list       (void *data,  pid_t pid,
				    UpstartMessageType type);
static int  control_watch_jobs     (void *data, pid_t pid,
				    UpstartMessageType type);
static int  control_unwatch_jobs   (void *data, pid_t pid,
				    UpstartMessageType type);
static int  control_watch_events   (void *data, pid_t pid,
				    UpstartMessageType type);
static int  control_unwatch_events (void *data, pid_t pid,
				    UpstartMessageType type);
static int  control_event_emit     (void *data, pid_t pid,
				    UpstartMessageType type, const char *name,
				    char **args, char **env);


/**
 * control_io:
 *
 * The NihIo being used to handle the control socket.
 **/
NihIo *control_io = NULL;

/**
 * message_handlers:
 *
 * Functions to be run when we receive particular messages from other
 * processes.  Any message types not listed here will be discarded.
 **/
static UpstartMessage message_handlers[] = {
	{ -1, UPSTART_JOB_START,
	  (UpstartMessageHandler)control_job_start },
	{ -1, UPSTART_JOB_STOP,
	  (UpstartMessageHandler)control_job_stop },
	{ -1, UPSTART_JOB_QUERY,
	  (UpstartMessageHandler)control_job_query },
	{ -1, UPSTART_JOB_LIST,
	  (UpstartMessageHandler)control_job_list },
	{ -1, UPSTART_WATCH_JOBS,
	  (UpstartMessageHandler)control_watch_jobs },
	{ -1, UPSTART_UNWATCH_JOBS,
	  (UpstartMessageHandler)control_unwatch_jobs },
	{ -1, UPSTART_WATCH_EVENTS,
	  (UpstartMessageHandler)control_watch_events },
	{ -1, UPSTART_UNWATCH_EVENTS,
	  (UpstartMessageHandler)control_unwatch_events },
	{ -1, UPSTART_EVENT_EMIT,
	  (UpstartMessageHandler)control_event_emit },

	UPSTART_MESSAGE_LAST
};


/**
 * control_open:
 *
 * Opens the control socket and associates it with an NihIo structure
 * that ensures that all incoming messages are handled, outgoing messages
 * can be queued, and any errors caught and the control socket re-opened.
 *
 * Returns: NihIo for socket on success, NULL on raised error.
 **/
NihIo *
control_open (void)
{
	int sock;

	sock = upstart_open ();
	if (sock < 0)
		return NULL;

	nih_io_set_cloexec (sock);

	while (! (control_io = nih_io_reopen (NULL, sock, NIH_IO_MESSAGE,
					      (NihIoReader)upstart_message_reader,
					      NULL, control_error_handler,
					      message_handlers))) {
		NihError *err;

		err = nih_error_get ();
		if (err->number != ENOMEM) {
			nih_free (err);
			close (sock);
			return NULL;
		}

		nih_free (err);
	}

	return control_io;
}

/**
 * control_close:
 *
 * Close the currently open control socket and free the structure handling
 * it.  Any messages in the queue will be lost.
 **/
void
control_close (void)
{
	nih_assert (control_io != NULL);

	nih_io_close (control_io);
	control_io = NULL;
}

/**
 * control_error_handler:
 * @data: ignored,
 * @io: NihIo structure on which an error occurred.
 *
 * This function is called should an error occur while reading from or
 * writing to a descriptor.  We handle errors that we recognise, otherwise
 * we log them and carry on.
 **/
static void
control_error_handler (void  *data,
		       NihIo *io)
{
	NihError *err;

	nih_assert (io != NULL);
	nih_assert (io == control_io);

	err = nih_error_get ();

	switch (err->number) {
	case ECONNREFUSED: {
		NihIoMessage *message;

		/* Connection refused means that the process we're sending to
		 * has closed their socket or just died.  We don't need to
		 * error because of this, don't want to re-attempt delivery
		 * of this message and in fact don't want to send them any
		 * future notifications.
		 */
		message = (NihIoMessage *)io->send_q->next;

		notify_unsubscribe ((pid_t)message->int_data);

		nih_list_free (&message->entry);
		break;
	}
	default:
		nih_error (_("Error on control socket: %s"), err->message);
		break;
	}

	nih_free (err);
}


/**
 * control_job_start:
 * @data: data pointer,
 * @pid: origin process id,
 * @type: message type received,
 * @name: name of job to start.
 *
 * This function is called when another process on the system requests that
 * we start the job @name.
 *
 * If a job by that name exists, it is started and the other process receives
 * the job status as a reply.  If no job by that name exists, then other
 * process receives the unknown job message as a reply.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
control_job_start (void               *data,
		   pid_t               pid,
		   UpstartMessageType  type,
		   const char         *name)
{
	NihIoMessage *reply;
	Job          *job;
	NihList      *iter;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB_START);
	nih_assert (name != NULL);

	job = job_find_by_name (name);
	if (! job) {
		NIH_MUST (reply = upstart_message_new (control_io, pid,
						       UPSTART_JOB_UNKNOWN,
						       name));
		nih_io_send_message (control_io, reply);
		return 0;
	}

	nih_info (_("Control request to start %s"), job->name);
	job_change_goal (job, JOB_START, NULL);

	NIH_MUST (reply = upstart_message_new (control_io, pid,
					       UPSTART_JOB_STATUS, job->name,
					       job->goal, job->state,
					       job->pid));
	nih_io_send_message (control_io, reply);

	/* Also return all instances */
	for (iter = nih_hash_lookup (jobs, job->name); iter != NULL;
	     iter = nih_hash_search (jobs, job->name, iter)) {
		Job *instance = (Job *)iter;

		if (instance->instance_of != job)
			continue;

		NIH_MUST (reply = upstart_message_new (control_io, pid,
						       UPSTART_JOB_STATUS,
						       instance->name,
						       instance->goal,
						       instance->state,
						       instance->pid));
		nih_io_send_message (control_io, reply);
	}

	NIH_MUST (reply = upstart_message_new (control_io, pid,
					       UPSTART_JOB_LIST_END));
	nih_io_send_message (control_io, reply);

	return 0;
}

/**
 * control_job_stop:
 * @data: data pointer,
 * @pid: origin process id,
 * @type: message type received,
 * @name: name of job to start.
 *
 * This function is called when another process on the system requests that
 * we stop the job @name.
 *
 * If a job by that name exists, it is stopped and the other process receives
 * the job status as a reply.  If no job by that name exists, then other
 * process receives the unknown job message as a reply.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
control_job_stop (void               *data,
		  pid_t               pid,
		  UpstartMessageType  type,
		  const char         *name)
{
	NihIoMessage *reply;
	Job          *job;
	NihList      *iter;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB_STOP);
	nih_assert (name != NULL);

	job = job_find_by_name (name);
	if (! job) {
		NIH_MUST (reply = upstart_message_new (control_io, pid,
						       UPSTART_JOB_UNKNOWN,
						       name));
		nih_io_send_message (control_io, reply);
		return 0;
	}

	nih_info (_("Control request to stop %s"), job->name);
	job_change_goal (job, JOB_STOP, NULL);

	NIH_MUST (reply = upstart_message_new (control_io, pid,
					       UPSTART_JOB_STATUS, job->name,
					       job->goal, job->state,
					       job->pid));
	nih_io_send_message (control_io, reply);

	/* Also return all instances */
	for (iter = nih_hash_lookup (jobs, job->name); iter != NULL;
	     iter = nih_hash_search (jobs, job->name, iter)) {
		Job *instance = (Job *)iter;

		if (instance->instance_of != job)
			continue;

		NIH_MUST (reply = upstart_message_new (control_io, pid,
						       UPSTART_JOB_STATUS,
						       instance->name,
						       instance->goal,
						       instance->state,
						       instance->pid));
		nih_io_send_message (control_io, reply);
	}

	NIH_MUST (reply = upstart_message_new (control_io, pid,
					       UPSTART_JOB_LIST_END));
	nih_io_send_message (control_io, reply);

	return 0;
}

/**
 * control_job_query:
 * @data: data pointer,
 * @pid: origin process id,
 * @type: message type received,
 * @name: name of job to start.
 *
 * This function is called when another process on the system queries the
 * status of the job @name.
 *
 * If a job by that name exists, the other process receives the job status
 * as a reply.  If no job by that name exists, then the other process
 * receives the unknown job message as a reply.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
control_job_query (void               *data,
		   pid_t               pid,
		   UpstartMessageType  type,
		   const char         *name)
{
	NihIoMessage *reply;
	Job          *job;
	NihList      *iter;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB_QUERY);
	nih_assert (name != NULL);

	job = job_find_by_name (name);
	if (! job) {
		NIH_MUST (reply = upstart_message_new (control_io, pid,
						       UPSTART_JOB_UNKNOWN,
						       name));
		nih_io_send_message (control_io, reply);
		return 0;
	}

	nih_info (_("Control request for state of %s"), job->name);

	NIH_MUST (reply = upstart_message_new (control_io, pid,
					       UPSTART_JOB_STATUS, job->name,
					       job->goal, job->state,
					       job->pid));
	nih_io_send_message (control_io, reply);

	/* Also return all instances */
	for (iter = nih_hash_lookup (jobs, job->name); iter != NULL;
	     iter = nih_hash_search (jobs, job->name, iter)) {
		Job *instance = (Job *)iter;

		if (instance->instance_of != job)
			continue;

		NIH_MUST (reply = upstart_message_new (control_io, pid,
						       UPSTART_JOB_STATUS,
						       instance->name,
						       instance->goal,
						       instance->state,
						       instance->pid));
		nih_io_send_message (control_io, reply);
	}

	NIH_MUST (reply = upstart_message_new (control_io, pid,
					       UPSTART_JOB_LIST_END));
	nih_io_send_message (control_io, reply);

	return 0;
}

/**
 * control_job_list:
 * @data: data pointer,
 * @pid: origin process id,
 * @type: message type received.
 *
 * This function is called when another process on the system queries the
 * table of known jobs.  It receives a job status reply for each known job
 * followed by the list end message.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
control_job_list (void               *data,
		  pid_t               pid,
		  UpstartMessageType  type)
{
	NihIoMessage *reply;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB_LIST);

	nih_info (_("Control request to list jobs"));

	job_init ();

	NIH_HASH_FOREACH (jobs, iter) {
		Job *job = (Job *)iter;

		NIH_MUST (reply = upstart_message_new (control_io, pid,
						       UPSTART_JOB_STATUS,
						       job->name, job->goal,
						       job->state, job->pid));
		nih_io_send_message (control_io, reply);
	}

	NIH_MUST (reply = upstart_message_new (control_io, pid,
					       UPSTART_JOB_LIST_END));
	nih_io_send_message (control_io, reply);

	return 0;
}

/**
 * control_watch_jobs:
 * @data: data pointer,
 * @pid: origin process id,
 * @type: message type received.
 *
 * This function is called when another process on the system requests
 * status updates for all jobs to be sent to it.  It receives no reply.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
control_watch_jobs (void               *data,
		    pid_t               pid,
		    UpstartMessageType  type)
{
	nih_assert (pid > 0);
	nih_assert (type == UPSTART_WATCH_JOBS);

	nih_info (_("Control request to subscribe %d to jobs"), pid);

	notify_subscribe_job (NULL, pid, NULL);

	return 0;
}

/**
 * control_unwatch_jobs:
 * @data: data pointer,
 * @pid: origin process id,
 * @type: message type received.
 *
 * This function is called when another process on the system requests
 * status updates for all jobs no longer be sent to it.  It receives no reply.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
control_unwatch_jobs (void               *data,
		      pid_t               pid,
		      UpstartMessageType  type)
{
	NotifySubscription *sub;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_UNWATCH_JOBS);

	nih_info (_("Control request to unsubscribe %d from jobs"), pid);

	sub = notify_subscription_find (pid, NOTIFY_JOB, NULL);
	if (sub)
		nih_list_free (&sub->entry);

	return 0;
}

/**
 * control_watch_events:
 * @data: data pointer,
 * @pid: origin process id,
 * @type: message type received.
 *
 * This function is called when another process on the system requests
 * notification of all events be sent to it.  It receives no reply.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
control_watch_events (void               *data,
		      pid_t               pid,
		      UpstartMessageType  type)
{
	nih_assert (pid > 0);
	nih_assert (type == UPSTART_WATCH_EVENTS);

	nih_info (_("Control request to subscribe %d to events"), pid);

	notify_subscribe_event (NULL, pid, NULL);

	return 0;
}

/**
 * control_unwatch_events:
 * @data: data pointer,
 * @pid: origin process id,
 * @type: message type received.
 *
 * This function is called when another process on the system requests
 * notification of all events no longer be sent to it.  It receives no reply.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
control_unwatch_events (void               *data,
			pid_t               pid,
			UpstartMessageType  type)
{
	NotifySubscription *sub;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_UNWATCH_EVENTS);

	nih_info (_("Control request to unsubscribe %d from events"), pid);

	sub = notify_subscription_find (pid, NOTIFY_EVENT, NULL);
	if (sub)
		nih_list_free (&sub->entry);

	return 0;
}

/**
 * control_event_emit:
 * @data: data pointer,
 * @pid: origin process id,
 * @type: message type received,
 * @name: name of event to emit,
 * @args: optional arguments to event,
 * @end: optional environment for event.
 *
 * This function is called when another process on the system requests that
 * we emit a @name event, with the optional @args and @env supplied.
 *
 * We queue the pending event and subscribe the process to receive
 * notification when the event is being handled, all changes the event makes
 * and notification when the event has finished; including whether it
 * succeeded or failed.
 *
 * If given, @args and @env are re-parented to belong to the event emitted.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
control_event_emit (void                *data,
		    pid_t                pid,
		    UpstartMessageType   type,
		    const char          *name,
		    char               **args,
		    char               **env)
{
	EventEmission *emission;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_EVENT_EMIT);
	nih_assert (name != NULL);

	nih_info (_("Control request to emit %s event"), name);

	emission = event_emit (name, args, env);

	notify_subscribe_event (emission, pid, emission);

	return 0;
}
