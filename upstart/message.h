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

#ifndef UPSTART_MESSAGE_H
#define UPSTART_MESSAGE_H

#include <sys/types.h>

#include <stdarg.h>

#include <nih/macros.h>
#include <nih/io.h>


/**
 * UPSTART_INIT_DAEMON:
 *
 * Macro that can be used in place of a pid for the init daemon, simply to
 * make it clear what you're doing.
 **/
#define UPSTART_INIT_DAEMON 1


/**
 * UpstartMessageType:
 *
 * This identifies the types of messages that can be passed between clients
 * and the init daemon over the control socket.  The type of the message
 * determines what information must be given for that message, or what
 * information is received with it.  See the documentation of
 * UpstartMsgHandler for more details.
 **/
typedef enum upstart_message_type {
	/* General messages.
	 *
	 * Clients or the server may send UPSTART_NO_OP at any time, without
	 * any effect or reply.
	 *
	 * Clients may send UPSTART_VERSION_QUERY and receive an
	 * UPSTART_VERSION message in reply.
	 *
	 * Clients may send UPSTART_LOG_PRIORITY, without any reply.
	 */
	UPSTART_NO_OP              = 0x0000,
	UPSTART_VERSION_QUERY      = 0x0001,
	UPSTART_LOG_PRIORITY       = 0x0002,
	UPSTART_VERSION            = 0x0010,

	/* Job requests and responses.
	 *
	 * Clients may send UPSTART_JOB_FIND to request a list of jobs
	 * matching a wildcard pattern.  They receive an UPSTART_JOB_LIST
	 * message in response, followed by an UPSTART_JOB_STATUS or
	 * UPSTART_JOB_INSTANCE message for each job (see below) and a
	 * final UPSTART_JOB_LIST_END message.
	 *
	 * They may send UPSTART_JOB_QUERY to query the status of a job
	 * by name or id.  If the job is not an instance job, or is an
	 * instance, they receive an UPSTART_JOB_STATUS reply, followed by
	 * an UPSTART_JOB_PROCESS message for each running process and then
	 * an UPSTART_JOB_STATUS_END reply.  If the job is an instance job,
	 * they receive an UPSTART_JOB_INSTANCE reply, followed by
	 * UPSTART_JOB_STATUS for each active instance and finally
	 * UPSTART_JOB_INSTANCE_END.
	 *
	 * Clients send UPSTART_JOB_START or UPSTART_JOB_STOP to start or
	 * stop a job.  They will receive the UPSTART_JOB response which
	 * includes the unique id of the job actually started or stopped;
	 * for UPSTART_JOB_STOP there may be multiple of these if the job
	 * was an instance job, since all instances will be stopped.
	 *
	 * For each status change the job goes through before it reaches
	 * its goal, the client will receive a UPSTART_JOB_STATUS message.
	 * Once the job reaches the goal, it will receive an
	 * UPSTART_JOB_FINISHED event.
	 *
	 * If the job given to UPSTART_JOB_QUERY, UPSTART_JOB_START or
	 * UPSTART_JOB_STOP is not known, the client will receive an
	 * UPSTART_JOB_UNKNOWN message.
	 *
	 * If the job cannot be started or stopped, usually because it's
	 * an instance, replacement or deleted job, the client will receive
	 * UPSTART_JOB_INVALID.  If the job has already been started or
	 * stopped, the client will receive an UPSTART_JOB_UNCHANGED response.
	 */
	UPSTART_JOB_FIND          = 0x0100,
	UPSTART_JOB_QUERY         = 0x0101,
	UPSTART_JOB_START         = 0x0102,
	UPSTART_JOB_STOP          = 0x0103,
	UPSTART_JOB               = 0x0110,
	UPSTART_JOB_FINISHED      = 0x011f,
	UPSTART_JOB_LIST          = 0x0120,
	UPSTART_JOB_LIST_END      = 0x012f,
	UPSTART_JOB_INSTANCE      = 0x0130,
	UPSTART_JOB_INSTANCE_END  = 0x013f,
	UPSTART_JOB_STATUS        = 0x0180,
	UPSTART_JOB_PROCESS       = 0x0181,
	UPSTART_JOB_STATUS_END    = 0x018f,
	UPSTART_JOB_UNKNOWN       = 0x01f0,
	UPSTART_JOB_INVALID       = 0x01f1,
	UPSTART_JOB_UNCHANGED     = 0x01f2,

	/* Event requests and responses.
	 *
	 * Clients send UPSTART_EVENT_EMIT to cause a new event to be
	 * placed in the queue.  When the event is handled, they receive
	 * the UPSTART_EVENT response which will include the unique id of
	 * the event.
	 *
	 * For each effect of the event, the client will receive an
	 * UPSTART_EVENT_CAUSED which includes the unique id of the event,
	 * followed by a JOB_STATUS message.
	 *
	 * Finally once the event has been handled, the client will receive
	 * an UPSTART_EVENT_FINISHED event.
	 */
	UPSTART_EVENT_EMIT        = 0x0200,
	UPSTART_EVENT             = 0x0210,
	UPSTART_EVENT_CAUSED      = 0x0211,
	UPSTART_EVENT_FINISHED    = 0x021f,

	/* Subscription requests.
	 *
	 * Clients send UPSTART_SUBSCRIBE_JOBS or UPSTART_SUBSCRIBE_EVENTS
	 * and will receive UPSTART_JOB_STATUS or UPSTART_EVENT messages
	 * for each job status change or event emission respectively.
	 *
	 * Messages can be ceased by sending UPSTART_UNSUBSCRIBE_JOBS or
	 * UPSTART_UNSUBSCRIPE_EVENTS, or simply by closing the control
	 * socket.
	 */
	UPSTART_SUBSCRIBE_JOBS     = 0x1000,
	UPSTART_UNSUBSCRIBE_JOBS   = 0x100f,
	UPSTART_SUBSCRIBE_EVENTS   = 0x1010,
	UPSTART_UNSUBSCRIBE_EVENTS = 0x101f,
} UpstartMessageType;


/**
 * UpstartMessageHandler:
 * @data: data passed to handler,
 * @pid: origin of message,
 * @type: message type.
 *
 * A message handler function is called whenever a message of an appropriate
 * @type is received from another process @pid.  The function will be called
 * with additional arguments that vary based on @type as follows:
 *
 * UPSTART_NO_OP:
 * No arguments.
 *
 * UPSTART_VERSION_QUERY:
 * No arguments.
 *
 * UPSTART_LOG_PRIORITY:
 * @priority: new log priority (unsigned int).
 *
 * UPSTART_VERSION:
 * @version: full name and version of init daemon (char *).
 *
 *
 * UPSTART_JOB_FIND:
 * @pattern: pattern to seek or NULL (char *).
 *
 * UPSTART_JOB_QUERY:
 * @name: name of job (char *),
 * @id: unique id of job (unsigned int), if @name is NULL.
 *
 * UPSTART_JOB_START:
 * @name: name of job (char *),
 * @id: unique id of job (unsigned int), if @name is NULL.
 *
 * UPSTART_JOB_STOP:
 * @name: name of job (char *),
 * @id: unique id of job (unsigned int), if @name is NULL.
 *
 * UPSTART_JOB:
 * @id: unique id of job (unsigned int),
 * @name: name of job (char *).
 *
 * UPSTART_JOB_FINISHED:
 * @id: unique id of job (unsigned int),
 * @name: name of job (char *),
 * @failed: whether the job failed (int),
 * @failed_process: type of process that failed (unsigned int),
 * @exit_status: exit status or signal of failed process (int).
 *
 * UPSTART_JOB_LIST:
 * @pattern: pattern sought or NULL (char *).
 *
 * UPSTART_JOB_LIST_END:
 * @pattern: pattern sought or NULL (char *).
 *
 * UPSTART_JOB_INSTANCE:
 * @id: unique id of job (unsigned int),
 * @name: name of job (char *).
 *
 * UPSTART_JOB_INSTANCE_END:
 * @id: unique id of job (unsigned int),
 * @name: name of job (char *).
 *
 * UPSTART_JOB_STATUS:
 * @id: unique id of job (unsigned int),
 * @name: name of job (char *),
 * @goal: goal of job (unsigned int),
 * @state: state of job (unsigned int).
 *
 * UPSTART_JOB_PROCESS:
 * @type: type of process (unsigned int),
 * @pid: process id (int).
 *
 * UPSTART_JOB_STATUS_END:
 * @id: unique id of job (unsigned int),
 * @name: name of job (char *),
 * @goal: goal of job (unsigned int),
 * @state: state of job (unsigned int).
 *
 * UPSTART_JOB_UNKNOWN:
 * @name: name of unknown job (char *),
 * @id: unique id of job (unsigned int), if @name is NULL.
 *
 * UPSTART_JOB_INVALID:
 * @id: unique id of job (unsigned int),
 * @name: name of unknown job (char *).
 *
 * UPSTART_JOB_UNCHANGED:
 * @id: unique id of job (unsigned int),
 * @name: name of unknown job (char *).
 *
 *
 * UPSTART_EVENT_EMIT:
 * @name: name of event (char *),
 * @args: arguments to event (char **),
 * @env: environment for event (char **).
 *
 * UPSTART_EVENT:
 * @id: unique id of event (unsigned int),
 * @name: name of event (char *),
 * @args: arguments to event (char **),
 * @env: environment for event (char **).
 *
 * UPSTART_EVENT_CAUSED:
 * @id: unique id of event (unsigned int),
 *
 * UPSTART_EVENT_FINISHED:
 * @id: unique id of event (unsigned int),
 * @failed: whether the event failed (int),
 * @name: name of event (char *),
 * @args: arguments to event (char **),
 * @env: environment for event (char **).
 *
 *
 * UPSTART_SUBSCRIBE_JOBS:
 * No arguments.
 *
 * UPSTART_UNSUBSCRIBE_JOBS:
 * No arguments.
 *
 * UPSTART_SUBSCRIBE_EVENTS:
 * No arguments.
 *
 * UPSTART_UNSUBSCRIBE_EVENTS:
 * No arguments.
 *
 * Returns: zero on success, negative value on raised error.
 **/
typedef int (*UpstartMessageHandler) (void *data, pid_t pid,
				      UpstartMessageType type, ...);


/**
 * UpstartMessage:
 * @pid: process id to match,
 * @type: message type to match,
 * @handler: handler function to call.
 *
 * This structure is used to associate a message handler function with
 * a particular message @type from a particular @pid.  When a message
 * matches, @handler will be called.
 *
 * @type may be the special value -1 to match any message.  @pid may be
 * -1 to indicate any process.
 *
 * Remember that messages can be received asynchronously, so it's usually
 * a good idea to set @pid if you're expecting a message from a particular
 * source.
 **/
typedef struct upstart_message {
	pid_t                 pid;
	UpstartMessageType    type;
	UpstartMessageHandler handler;
} UpstartMessage;


/**
 * UPSTART_MESSAGE_LAST:
 *
 * This macro may be used as the last handler in the list to avoid typing
 * all those NULLs and -1s yourself.
 **/
#define UPSTART_MESSAGE_LAST { -1, -1, NULL }


NIH_BEGIN_EXTERN

int           upstart_open                 (void)
	__attribute__ ((warn_unused_result));

NihIoMessage *upstart_message_new          (const void *parent, pid_t pid,
					    UpstartMessageType type, ...)
	__attribute__ ((warn_unused_result, malloc));
NihIoMessage *upstart_message_newv         (const void *parent, pid_t pid,
					    UpstartMessageType type,
					    va_list args)
	__attribute__ ((warn_unused_result, malloc));

int           upstart_message_handle       (const void *parent,
					    NihIoMessage *message,
					    UpstartMessage *handlers,
					    void *data)
	__attribute__ ((warn_unused_result));
int           upstart_message_handle_using (const void *parent,
					    NihIoMessage *message,
					    UpstartMessageHandler handler,
					    void *data)
	__attribute__ ((warn_unused_result));

void          upstart_message_reader       (UpstartMessage *handlers,
					    NihIo *io, const char *buf,
					    size_t len);

NIH_END_EXTERN

#endif /* UPSTART_MESSAGE_H */
