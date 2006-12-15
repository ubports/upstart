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

#include <sys/types.h>

#include <nih/macros.h>

#include <upstart/job.h>


/**
 * UPSTART_API_VERSION:
 *
 * This macro defines the current API version number; it can optionally
 * be used to make a judgement about whether it's legal for a particular
 * field to be missing or not.
 **/
#define UPSTART_API_VERSION 0

/**
 * UpstartMsgType:
 *
 * This identifies the types of messages that can be passed between clients
 * and the init daemon over the control socket.
 *
 * Each uses a different selection of the fields from the UpstartMsg
 * structure, depending on its type.
 **/
typedef enum {
	/* General messages */
	UPSTART_NO_OP,

	/* Job messages and responses */
	UPSTART_JOB_START,
	UPSTART_JOB_STOP,
	UPSTART_JOB_QUERY,
	UPSTART_JOB_STATUS,
	UPSTART_JOB_UNKNOWN,
	UPSTART_JOB_LIST,
	UPSTART_JOB_LIST_END,

	/* Event messages and responses */
	UPSTART_EVENT_QUEUE,
	UPSTART_EVENT,

	/* Watches */
	UPSTART_WATCH_JOBS,
	UPSTART_UNWATCH_JOBS,
	UPSTART_WATCH_EVENTS,
	UPSTART_UNWATCH_EVENTS,

	/* Special commands */
	UPSTART_SHUTDOWN
} UpstartMsgType;


/**
 * UpstartMsg:
 * @type: type of message,
 * @name: name of job or event.
 * @description: description of job,
 * @goal: whether job is being started or stopped,
 * @state: actual state of job,
 * @process_state: state of attached process,
 * @pid: current pid, if any.
 **/
typedef struct upstart_msg {
	UpstartMsgType  type;

	char           *name;
	char           *description;
	JobGoal         goal;
	JobState        state;
	ProcessState    process_state;

	pid_t           pid;
} UpstartMsg;


NIH_BEGIN_EXTERN

int         upstart_open        (void)
	__attribute__ ((warn_unused_result));

int         upstart_send_msg    (int sock, UpstartMsg *message);
int         upstart_send_msg_to (pid_t pid, int sock, UpstartMsg *message);

UpstartMsg *upstart_recv_msg    (const void *parent, int sock, pid_t *pid)
	__attribute__ ((warn_unused_result, malloc));

void        upstart_free        (UpstartMsg *message);

NIH_END_EXTERN

#endif /* UPSTART_CONTROL_H */
