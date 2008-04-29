/* upstart
 *
 * Copyright © 2008 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
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

#ifndef INIT_EVENTS_H
#define INIT_EVENTS_H

/**
 * STARTUP_EVENT:
 *
 * Name of the event that we generate when init is first executed.
 **/
#ifndef DEBUG
#define STARTUP_EVENT "startup"
#else /* DEBUG */
#define STARTUP_EVENT "debug"
#endif


/**
 * CTRLALTDEL_EVENT:
 *
 * Name of the event that we generate when the Control-Alt-Delete key
 * combination is pressed.
 **/
#define CTRLALTDEL_EVENT "control-alt-delete"

/**
 * KBDREQUEST_EVENT:
 *
 * Name of the event that we generate when the Alt-UpArrow key combination
 * is pressed.
 **/
#define KBDREQUEST_EVENT "keyboard-request"

/**
 * PWRSTATUS_EVENT:
 *
 * Name of the event that we generate when we receive SIGPWR, indicating
 * that the power status has changed.
 **/
#define PWRSTATUS_EVENT "power-status-changed"


/**
 * JOB_STARTING_EVENT:
 *
 * Name of the event we generate when we're ready to start a job; the job
 * is not actually started until the handling of this event finishes.
 **/
#define JOB_STARTING_EVENT "starting"

/**
 * JOB_STARTED_EVENT:
 *
 * Name of the event we generate once a job has been started and is now
 * running.  This is not generated until the spawned pid is located (if
 * appropriate) and the post-start script has finished.
 **/
#define JOB_STARTED_EVENT "started"

/**
 * JOB_STOPPING_EVENT:
 *
 * Name of the event we generate when we're ready to stop a job, which
 * includes arguments and environment indicating whether the job failed.
 * This is run after the pre-stop script has finished without setting the
 * goal back to start.  The job is not actually stopped until the handling
 * of this event finishes.
 **/
#define JOB_STOPPING_EVENT "stopping"

/**
 * JOB_STOPPED_EVENT:
 *
 * Name of the event we generate once a job has been stopped and is now
 * waiting.
 **/
#define JOB_STOPPED_EVENT "stopped"


#endif /* INIT_EVENTS_H */
