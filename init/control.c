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
#include <fnmatch.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/hash.h>
#include <nih/io.h>
#include <nih/main.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <upstart/message.h>

#include "job.h"
#include "control.h"
#include "notify.h"


/* Prototypes for static functions */
static void control_error_handler      (void  *data, NihIo *io);
static int  control_version_query      (void *data, pid_t pid,
					UpstartMessageType type);
static int  control_log_priority       (void *data, pid_t pid,
					UpstartMessageType type,
					NihLogLevel priority);
static int  control_job_find           (void *data, pid_t pid,
					UpstartMessageType type,
					const char *pattern);
static int  control_job_query          (void *data, pid_t pid,
					UpstartMessageType type,
					const char *name, unsigned int id);
static int  control_job_start          (void *data, pid_t pid,
					UpstartMessageType type,
					const char *name, unsigned int id);
static int  control_job_stop           (void *data, pid_t pid,
					UpstartMessageType type,
					const char *name, unsigned int id);
static int  control_event_emit         (void *data, pid_t pid,
					UpstartMessageType type,
					const char *name,
					char **args, char **env);
static int  control_subscribe_jobs     (void *data, pid_t pid,
					UpstartMessageType type);
static int  control_unsubscribe_jobs   (void *data, pid_t pid,
					UpstartMessageType type);
static int  control_subscribe_events   (void *data, pid_t pid,
					UpstartMessageType type);
static int  control_unsubscribe_events (void *data, pid_t pid,
					UpstartMessageType type);


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
	{ -1, UPSTART_VERSION_QUERY,
	  (UpstartMessageHandler)control_version_query },
	{ -1, UPSTART_LOG_PRIORITY,
	  (UpstartMessageHandler)control_log_priority },
	{ -1, UPSTART_JOB_FIND,
	  (UpstartMessageHandler)control_job_find },
	{ -1, UPSTART_JOB_QUERY,
	  (UpstartMessageHandler)control_job_query },
	{ -1, UPSTART_JOB_START,
	  (UpstartMessageHandler)control_job_start },
	{ -1, UPSTART_JOB_STOP,
	  (UpstartMessageHandler)control_job_stop },
	{ -1, UPSTART_EVENT_EMIT,
	  (UpstartMessageHandler)control_event_emit },
	{ -1, UPSTART_SUBSCRIBE_JOBS,
	  (UpstartMessageHandler)control_subscribe_jobs },
	{ -1, UPSTART_UNSUBSCRIBE_JOBS,
	  (UpstartMessageHandler)control_unsubscribe_jobs },
	{ -1, UPSTART_SUBSCRIBE_EVENTS,
	  (UpstartMessageHandler)control_subscribe_events },
	{ -1, UPSTART_UNSUBSCRIBE_EVENTS,
	  (UpstartMessageHandler)control_unsubscribe_events },

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
 * control_send_job_status:
 * @pid: destination process,
 * @job: job to send.
 *
 * Sends a series of messages to @pid containing the current status of @job
 * and its processes.  The UPSTART_JOB_STATUS message is sent first, giving
 * the id and name of the job, along with its current goal and state.  Then,
 * for each active process, an UPSTART_JOB_PROCESS message is sent containing
 * the process type and current pid.  Finally an UPSTART_JOB_STATUS_END
 * message is sent.
 **/
void
control_send_job_status (pid_t  pid,
			 Job   *job)
{
	NihIoMessage *message;
	int           i;

	nih_assert (pid > 0);
	nih_assert (job != NULL);
	nih_assert (control_io != NULL);

	NIH_MUST (message = upstart_message_new (
			  control_io, pid, UPSTART_JOB_STATUS,
			  job->id, job->name, job->goal, job->state));
	nih_io_send_message (control_io, message);

	for (i = 0; i < PROCESS_LAST; i++) {
		if (job->process[i] && (job->process[i]->pid > 0)) {
			NIH_MUST (message = upstart_message_new (
					  control_io, pid, UPSTART_JOB_PROCESS,
					  i, job->process[i]->pid));
			nih_io_send_message (control_io, message);
		}
	}

	NIH_MUST (message = upstart_message_new (
			  control_io, pid, UPSTART_JOB_STATUS_END,
			  job->id, job->name, job->goal, job->state));
	nih_io_send_message (control_io, message);
}

/**
 * control_send_instance:
 * @pid: destination process,
 * @job: job to send.
 *
 * Sends a series of job status messages to @pid for each instance of @job,
 * enclosed within UPSTART_JOB_INSTANCE and UPSTART_JOB_INSTANCE_END messages
 * giving the id and name of the instance itself.
 **/
void
control_send_instance (pid_t  pid,
		       Job   *job)
{
	NihIoMessage *message;

	nih_assert (pid > 0);
	nih_assert (job != NULL);
	nih_assert (job->instance);
	nih_assert (job->instance_of == NULL);
	nih_assert (control_io != NULL);

	NIH_MUST (message = upstart_message_new (
			  control_io, pid, UPSTART_JOB_INSTANCE,
			  job->id, job->name));
	nih_io_send_message (control_io, message);

	NIH_HASH_FOREACH (jobs, iter) {
		Job *instance = (Job *)iter;

		if (instance->instance_of != job)
			continue;

		if (instance->state == JOB_DELETED)
			continue;

		control_send_job_status (pid, instance);
	}

	NIH_MUST (message = upstart_message_new (
			  control_io, pid, UPSTART_JOB_INSTANCE_END,
			  job->id, job->name));
	nih_io_send_message (control_io, message);
}


/**
 * control_version_query:
 * @data: data pointer,
 * @pid: origin process id,
 * @type: message type received.
 *
 * This function is called when another process on the system asks for our
 * version.
 *
 * We return the autoconf-set package string, containing both the package
 * name and current version, in an UPSTART_VERSION reply.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
control_version_query (void               *data,
		       pid_t               pid,
		       UpstartMessageType  type)
{
	NihIoMessage *reply;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_VERSION_QUERY);

	nih_info (_("Control request for our version"));

	NIH_MUST (reply = upstart_message_new (control_io, pid,
					       UPSTART_VERSION,
					       nih_main_package_string ()));
	nih_io_send_message (control_io, reply);

	return 0;
}

/**
 * control_log_priority:
 * @data: data pointer,
 * @pid: origin process id,
 * @type: message type received.
 *
 * This function is called when another process on the system adjusts our
 * logging priority.
 *
 * We change the priority to the new one given and return no reply, since
 * this is always successful.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
control_log_priority (void               *data,
		      pid_t               pid,
		      UpstartMessageType  type,
		      NihLogLevel         priority)
{
	nih_assert (pid > 0);
	nih_assert (type == UPSTART_LOG_PRIORITY);

	nih_info (_("Control request to change logging priority"));

	nih_log_set_priority (priority);

	return 0;
}


/**
 * control_job_find:
 * @data: data pointer,
 * @pid: origin process id,
 * @type: message type received,
 * @pattern: pattern to match or NULL.
 *
 * This function is called when another process on the system asks for the
 * list of processes matching the wildcard @pattern, which may be NULL to
 * list all jobs.
 *
 * We iterate the jobs and return the job status for each one; instances are
 * collated together and sent as a job instance group.  Deleted and
 * replacement jobs are ignored.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
control_job_find (void                *data,
		  pid_t                pid,
		  UpstartMessageType   type,
		  const char          *pattern)
{
	NihIoMessage *reply;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB_FIND);

	if (pattern) {
		nih_info (_("Control request for jobs matching %s"), pattern);
	} else {
		nih_info (_("Control request for all jobs"));
	}

	NIH_MUST (reply = upstart_message_new (control_io, pid,
					       UPSTART_JOB_LIST, pattern));
	nih_io_send_message (control_io, reply);

	NIH_HASH_FOREACH (jobs, iter) {
		Job *job = (Job *)iter;

		if ((job->state == JOB_DELETED)
		    || (job->instance_of != NULL)
		    || (job->replacement_for != NULL))
			continue;

		if (pattern && fnmatch (pattern, job->name, 0))
			continue;

		if (! job->instance) {
			control_send_job_status (pid, job);
		} else {
			control_send_instance (pid, job);
		}
	}

	NIH_MUST (reply = upstart_message_new (control_io, pid,
					       UPSTART_JOB_LIST_END, pattern));
	nih_io_send_message (control_io, reply);

	return 0;
}

/**
 * control_job_query:
 * @data: data pointer,
 * @pid: origin process id,
 * @type: message type received,
 * @name: name of job to query,
 * @id: id of job to query if @name is NULL.
 *
 * This function is called when another process on the system queries the
 * state of the job named @name or with the unique @id.
 *
 * We locate the job and return the job status, either as single message set,
 * or if its an instance, the status of every instance.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
control_job_query (void                *data,
		   pid_t                pid,
		   UpstartMessageType   type,
		   const char          *name,
		   unsigned int         id)
{
	NihIoMessage *reply;
	Job          *job;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB_QUERY);

	if (name) {
		nih_info (_("Control request for status of %s"), name);

		job = job_find_by_name (name);
	} else {
		nih_info (_("Control request for status of job #%u"), id);

		job = job_find_by_id (id);
	}

	/* Reply with UPSTART_JOB_UNKNOWN if we couldn't find the job. */
	if (! job) {
		NIH_MUST (reply = upstart_message_new (control_io, pid,
						       UPSTART_JOB_UNKNOWN,
						       name, id));
		nih_io_send_message (control_io, reply);
		return 0;
	}

	if ((! job->instance) || (job->instance_of != NULL)) {
		control_send_job_status (pid, job);
	} else {
		control_send_instance (pid, job);
	}

	return 0;
}

/**
 * control_job_start:
 * @data: data pointer,
 * @pid: origin process id,
 * @type: message type received,
 * @name: name of job to start,
 * @id: id of job to start if @name is NULL.
 *
 * This function is called when another process on the system requests that
 * we start the job named @name or with the unique @id.
 *
 * We locate the job, subscribe the process to receive notification when the
 * job state changes and when the job reaches its goal, and then initiate
 * the goal change.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
control_job_start (void                *data,
		   pid_t                pid,
		   UpstartMessageType   type,
		   const char          *name,
		   unsigned int         id)
{
	NihIoMessage *reply;
	Job          *job;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB_START);

	if (name) {
		nih_info (_("Control request to start %s"), name);

		job = job_find_by_name (name);
	} else {
		nih_info (_("Control request to start job #%u"), id);

		job = job_find_by_id (id);
	}

	/* Reply with UPSTART_JOB_UNKNOWN if we couldn't find the job,
	 * and reply with UPSTART_JOB_INVALID if the job we found by id
	 * is deleted, an instance or a replacement.
	 */
	if (! job) {
		NIH_MUST (reply = upstart_message_new (control_io, pid,
						       UPSTART_JOB_UNKNOWN,
						       name, id));
		nih_io_send_message (control_io, reply);
		return 0;

	} else if ((job->state == JOB_DELETED)
		   || (job->instance_of != NULL)
		   || (job->replacement_for != NULL)) {
		NIH_MUST (reply = upstart_message_new (control_io, pid,
						       UPSTART_JOB_INVALID,
						       job->id, job->name));
		nih_io_send_message (control_io, reply);
		return 0;
	}

	/* Obtain an instance of the job that can be started.  Make sure
	 * that this instance isn't already started, since we might never
	 * send a reply if it's already at rest.  Send UPSTART_JOB_UNCHANGED
	 * so they know their command had no effect.
	 */
	job = job_instance (job);
	if (job->goal == JOB_START) {
		NIH_MUST (reply = upstart_message_new (control_io, pid,
						       UPSTART_JOB_UNCHANGED,
						       job->id, job->name));
		nih_io_send_message (control_io, reply);
		return 0;
	}

	notify_subscribe_job (job, pid, job);

	NIH_MUST (reply = upstart_message_new (control_io, pid,
					       UPSTART_JOB,
					       job->id, job->name));
	nih_io_send_message (control_io, reply);

	job_change_goal (job, JOB_START);

	return 0;
}

/**
 * control_job_stop:
 * @data: data pointer,
 * @pid: origin process id,
 * @type: message type received,
 * @name: name of job to stop,
 * @id: id of job to stop if @name is NULL.
 *
 * This function is called when another process on the system requests that
 * we stop the job named @name or with the unique @id.
 *
 * We locate the job, subscribe the process to receive notification when the
 * job state changes and when the job reaches its goal, and then initiate
 * the goal change.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
control_job_stop (void                *data,
		  pid_t                pid,
		  UpstartMessageType   type,
		  const char          *name,
		  unsigned int         id)
{
	NihIoMessage *reply;
	Job          *job;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB_STOP);

	if (name) {
		nih_info (_("Control request to stop %s"), name);

		job = job_find_by_name (name);
	} else {
		nih_info (_("Control request to stop job #%u"), id);

		job = job_find_by_id (id);
	}

	/* Reply with UPSTART_JOB_UNKNOWN if we couldn't find the job,
	 * and reply with UPSTART_JOB_INVALID if the job we found was
	 * deleted or a replacement, since we can't change those.
	 */
	if (! job) {
		NIH_MUST (reply = upstart_message_new (control_io, pid,
						       UPSTART_JOB_UNKNOWN,
						       name, id));
		nih_io_send_message (control_io, reply);
		return 0;

	} else if ((job->state == JOB_DELETED)
		   || (job->replacement_for != NULL)) {
		NIH_MUST (reply = upstart_message_new (control_io, pid,
						       UPSTART_JOB_INVALID,
						       job->id, job->name));
		nih_io_send_message (control_io, reply);
		return 0;
	}

	if ((! job->instance) || (job->instance_of != NULL)) {
		/* Make sure that the job isn't already stopped, since we
		 * might never send a reply if it's already at rest.  Send
		 * UPSTART_JOB_UNCHANGED so they know their command had no
		 * effect.
		 */
		if (job->goal == JOB_STOP) {
			NIH_MUST (reply = upstart_message_new (
					  control_io, pid,
					  UPSTART_JOB_UNCHANGED,
					  job->id, job->name));
			nih_io_send_message (control_io, reply);
			return 0;
		}

		notify_subscribe_job (job, pid, job);

		NIH_MUST (reply = upstart_message_new (control_io, pid,
						       UPSTART_JOB,
						       job->id, job->name));
		nih_io_send_message (control_io, reply);

		job_change_goal (job, JOB_STOP);

	} else {
		int has_instance = FALSE;

		/* We've been asked to stop an instance master, we can't
		 * directly change the goal of those since they never have
		 * any running processes.  Instead of returning INVALID,
		 * we're rather more helpful, and instead stop every single
		 * instance that's running.
		 */
		NIH_HASH_FOREACH (jobs, iter) {
			Job *instance = (Job *)iter;

			if (instance->instance_of != job)
				continue;

			has_instance = TRUE;

			notify_subscribe_job (instance, pid, instance);

			NIH_MUST (reply = upstart_message_new (
					  control_io, pid, UPSTART_JOB,
					  instance->id, instance->name));
			nih_io_send_message (control_io, reply);

			job_change_goal (instance, JOB_STOP);
		}

		/* If no instances were running, we send back
		 * UPSTART_JOB_UNCHANGED since they should at least receive
		 * something for their troubles.
		 */
		if (! has_instance) {
			NIH_MUST (reply = upstart_message_new (
					  control_io, pid,
					  UPSTART_JOB_UNCHANGED,
					  job->id, job->name));
			nih_io_send_message (control_io, reply);
			return 0;
		}
	}

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
	Event *event;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_EVENT_EMIT);
	nih_assert (name != NULL);

	nih_info (_("Control request to emit %s event"), name);

	event = event_new (NULL, name, args, env);

	notify_subscribe_event (event, pid, event);

	return 0;
}


/**
 * control_subscribe_jobs:
 * @data: data pointer,
 * @pid: origin process id,
 * @type: message type received.
 *
 * This function is called when another process on the system requests that
 * it be subscribed to job status updates.
 *
 * We add the subscription, no reply is sent.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
control_subscribe_jobs (void               *data,
			pid_t               pid,
			UpstartMessageType  type)
{
	nih_assert (pid > 0);
	nih_assert (type == UPSTART_SUBSCRIBE_JOBS);

	nih_info (_("Control request to subscribe %d to jobs"), pid);

	notify_subscribe_job (NULL, pid, NULL);

	return 0;
}

/**
 * control_unsubscribe_jobs:
 * @data: data pointer,
 * @pid: origin process id,
 * @type: message type received.
 *
 * This function is called when another process on the system requests that
 * it be unsubscribed from job status updates.
 *
 * We lookup their current subscription, and remove it if it exists.  No
 * reply is sent, and a non-existant subscription is ignored.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
control_unsubscribe_jobs (void               *data,
			  pid_t               pid,
			  UpstartMessageType  type)
{
	NotifySubscription *sub;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_UNSUBSCRIBE_JOBS);

	nih_info (_("Control request to unsubscribe %d from jobs"), pid);

	sub = notify_subscription_find (pid, NOTIFY_JOB, NULL);
	if (sub)
		nih_list_free (&sub->entry);

	return 0;
}

/**
 * control_subscribe_events:
 * @data: data pointer,
 * @pid: origin process id,
 * @type: message type received.
 *
 * This function is called when another process on the system requests that
 * it be subscribed to events.
 *
 * We add the subscription, no reply is sent.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
control_subscribe_events (void               *data,
			  pid_t               pid,
			  UpstartMessageType  type)
{
	nih_assert (pid > 0);
	nih_assert (type == UPSTART_SUBSCRIBE_EVENTS);

	nih_info (_("Control request to subscribe %d to events"), pid);

	notify_subscribe_event (NULL, pid, NULL);

	return 0;
}

/**
 * control_unsubscribe_events:
 * @data: data pointer,
 * @pid: origin process id,
 * @type: message type received.
 *
 * This function is called when another process on the system requests that
 * it be unsubscribed from events.
 *
 * We lookup their current subscription, and remove it if it exists.  No
 * reply is sent, and a non-existant subscription is ignored.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
control_unsubscribe_events (void               *data,
			    pid_t               pid,
			    UpstartMessageType  type)
{
	NotifySubscription *sub;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_UNSUBSCRIBE_EVENTS);

	nih_info (_("Control request to unsubscribe %d from events"), pid);

	sub = notify_subscription_find (pid, NOTIFY_EVENT, NULL);
	if (sub)
		nih_list_free (&sub->entry);

	return 0;
}
