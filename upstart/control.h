/* upstart
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

#ifndef UPSTART_CONTROL_H
#define UPSTART_CONTROL_H

#include <nih/macros.h>

#include <upstart/job.h>


/**
 * UpstartMsgType:
 *
 * This identifies the types of messages that can be passed between clients
 * and the init daemon over the control socket.
 *
 * Each has an associated structure which contains additional data passed
 * with the message, the structure is named by converting the type name to
 * CamelCase and appending Msg on the end; e.g. the struct for the fictional
 * UPSTART_FOO_BAR message sould be UpstartFooBarMsg.
 *
 * Most are only appropriate in one direction, see the documentation for the
 * message structures for details.
 **/
typedef enum {
	/* General messages */
	UPSTART_NO_OP,

	/* Job messages and responses */
	UPSTART_JOB_START,
	UPSTART_JOB_STOP,
	UPSTART_JOB_QUERY,
	UPSTART_JOB_STATUS,
	UPSTART_JOB_UNKNOWN
} UpstartMsgType;


/**
 * UpstartNoOpMsg:
 * @type: always UPSTART_NO_OP.
 *
 * This message has no effect, it may be used to "ping" the server or client
 * to determine whether it is still there.
 *
 * Direction: either direction.
 * Response: none.
 **/
typedef struct upstart_no_op_msg {
	UpstartMsgType type;
} UpstartNoOpMsg;


/**
 * UpstartJobStartMsg:
 * @type: always UPSTART_JOB_START,
 * @name: name of job to start.
 *
 * This message requests that the job called @name be started if it has not
 * been already, or that job be restarted if it is currently being stopped.
 *
 * Direction: client to server,
 * Response: UPSTART_JOB_STATUS or UPSTART_JOB_UNKNOWN.
 **/
typedef struct upstart_job_start_msg {
	UpstartMsgType  type;

	char           *name;
} UpstartJobStartMsg;

/**
 * UpstartStopJobMsg:
 * @type: always UPSTART_JOB_STOP,
 * @name: name of job to stop.
 *
 * This message requests that the job called @name be stopped if it is
 * currently starting or running.
 *
 * Direction: client to server,
 * Response: UPSTART_JOB_STATUS or UPSTART_JOB_UNKNOWN.
 **/
typedef struct upstart_job_stop_msg {
	UpstartMsgType  type;

	char           *name;
} UpstartJobStopMsg;

/**
 * UpstartJobQueryMsg:
 * @type: always UPSTART_JOB_QUERY,
 * @name: name of job to query.
 *
 * This message queries the current state of the job called @name.
 *
 * Direction: client to server,
 * Response: UPSTART_JOB_STATUS or UPSTART_JOB_UNKNOWN.
 **/
typedef struct upstart_job_query_msg {
	UpstartMsgType  type;

	char           *name;
} UpstartJobQueryMsg;

/**
 * UpstartJobStatusMsg:
 * @type: always UPSTART_JOB_STATUS,
 * @name: name of job,
 * @goal: whether job is being started or stopped,
 * @state: actual state of job,
 * @process_state: state of attached process.
 *
 * This message indicates the current state of a job; it is sent in response
 * to an explicit query and for commands that change the job state to
 * indicate the new state information.
 *
 * Direction: server to client,
 * Response: none.
 **/
typedef struct upstart_job_status_msg {
	UpstartMsgType  type;

	char           *name;
	JobGoal         goal;
	JobState        state;
	ProcessState    process_state;
} UpstartJobStatusMsg;

/**
 * UpstartJobUnknownMsg:
 * @type: always UPSTART_JOB_UNKNOWN,
 * @name: name of unknown job.
 *
 * This message indicates that the server does not know about a job called
 * @name, sent in response to commands that query or change a job's state
 * when the job is unknown.
 *
 * Direction: server to client,
 * Response: none.
 **/
typedef struct upstart_job_unknown_msg {
	UpstartMsgType  type;

	char           *name;
} UpstartJobUnknownMsg;


/**
 * UpstartMsg:
 * @type: type of message.
 *
 * This union combines all of the job messages into one structure; the type
 * member works for all structure types so it's possible to just check
 * msg.type and then use the appropriate member.
 **/
typedef union upstart_msg {
	UpstartMsgType       type;

	UpstartNoOpMsg       no_op;
	UpstartJobStartMsg   job_start;
	UpstartJobStopMsg    job_stop;
	UpstartJobQueryMsg   job_query;
	UpstartJobStatusMsg  job_status;
	UpstartJobUnknownMsg job_unknown;
} UpstartMsg;


NIH_BEGIN_EXTERN

int         upstart_open        (void)
	__attribute__ ((warn_unused_result));

int         upstart_send_msg    (int sock, UpstartMsg *message);
int         upstart_send_msg_to (pid_t pid, int sock, UpstartMsg *message);

UpstartMsg *upstart_recv_msg    (void *parent, int sock)
	__attribute__ ((warn_unused_result, malloc));

NIH_END_EXTERN

#endif /* UPSTART_CONTROL_H */
