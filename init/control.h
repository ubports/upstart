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

#ifndef INIT_CONTROL_H
#define INIT_CONTROL_H

#include <sys/types.h>

#include <upstart/control.h>

#include <nih/macros.h>
#include <nih/io.h>

#include "job.h"
#include "event.h"


/**
 * NotifyEvents:
 *
 * Events we notify subscribed processes of, used as a bitmask in the
 * Subscription structure.
 **/
typedef enum {
	NOTIFY_NONE   = 00,
	NOTIFY_JOBS   = 01,
	NOTIFY_EVENTS = 02
} NotifyEvents;


/**
 * ControlMsg:
 * @entry: list header,
 * @pid: destination,
 * @message: message to send.
 *
 * This structure is used to maintain a queue of messages that are
 * waiting to be sent on the control socket.
 *
 * @message is a copy of that given to us at the time, with any pointers
 * copied by control_send().
 **/
typedef struct control_msg {
	NihList    entry;
	pid_t      pid;
	UpstartMsg message;
} ControlMsg;

/**
 * ControlSub:
 * @entry: list header,
 * @pid: subscribed process,
 * @notify: notify events subscribed to.
 *
 * This structure is used to allow processes to subscribe to notification
 * of changes in event level or job status.  @notify is a bitmask of which
 * of the two events (or both) to receive, it is never allowed to be zero
 * as that's an unsubscription.
 **/
typedef struct control_sub {
	NihList      entry;
	pid_t        pid;
	NotifyEvents notify;
} ControlSub;


NIH_BEGIN_EXTERN

NihIoWatch *control_open         (void);
void        control_close        (void);

ControlSub *control_subscribe    (pid_t pid, NotifyEvents notify, int set);

ControlMsg *control_send         (pid_t pid, UpstartMsg *message);

void        control_handle_job   (Job *job);
void        control_handle_event (Event *event);

NIH_END_EXTERN

#endif /* INIT_CONTROL_H */
