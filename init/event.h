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

#ifndef INIT_EVENT_H
#define INIT_EVENT_H

#include <stdio.h>

#include <nih/macros.h>
#include <nih/list.h>
#include <nih/main.h>


/**
 * EventEmissionCb:
 * @data: pointer given when registered,
 * @emission: EventEmission structure.
 *
 * The event emission callback is given when an event is registered for
 * emission, and is called once the event has deemed to be completed.
 * Once the callback returns, @emission is automatically freed; and
 * a failed event generated if necessary.
 **/
typedef struct event_emission EventEmission;
typedef void (*EventEmissionCb) (void *data, EventEmission *emission);


/**
 * EventProgress:
 *
 * This is used to record the progress of an event emission, starting at
 * being pending, then being handled and finally waiting for the callback
 * to be called and any cleanup performed.
 **/
typedef enum {
	EVENT_PENDING,
	EVENT_HANDLING,
	EVENT_FINISHED
} EventProgress;


/**
 * Event:
 * @entry: list header,
 * @name: string name of the event,
 * @args: NULL-terminated list of arguments,
 * @env: NULL-terminated list of environment variables.
 *
 * Events are one of the core concepts of upstart; they occur whenever
 * something, somewhere changes state.  They are idenitied by a unique
 * @name string, and can carry further information in the form of @args
 * and @env; both of which are passed to any jobs whose goal is changed
 * by this event.
 *
 * This structure represents an event, and is used both for the events
 * themselves and to match events.
 **/
typedef struct event {
	NihList   entry;

	char     *name;
	char    **args;
	char    **env;
} Event;

/**
 * EventEmission:
 * @event: event being emitted,
 * @id: unique id assigned to each emission,
 * @progress: progress of emission,
 * @jobs: number of jobs holding this event,
 * @failed: whether this event has failed,
 * @callback: callback once emission has completed,
 * @data: data to pass to @callback.
 *
 * Events aren't useful on their own; in order to change the state of jobs
 * they need to be first placed in the event queue, then emitted and only
 * freed and forgotten once all jobs changed have reached their goal state.
 *
 * This process is known as emission, and this structure holds all the
 * information on the emission of a single event; including the event
 * itself.
 **/
struct event_emission {
	Event            event;
	uint32_t         id;
	EventProgress    progress;

	int              jobs;
	int              failed;

	EventEmissionCb  callback;
	void            *data;
};


/**
 * STARTUP_EVENT:
 *
 * Name of the event that we generate when init is first executed.
 **/
#define STARTUP_EVENT "startup"

/**
 * SHUTDOWN_EVENT:
 *
 * Name of the event that we generate to begin the shutdown process.
 **/
#define SHUTDOWN_EVENT "shutdown"

/**
 * STALLED_EVENT:
 *
 * Name of the event that we generate if the system stalls (all jobs are
 * stopped/waiting)
 **/
#define STALLED_EVENT "stalled"

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
#define KBDREQUEST_EVENT "kbdrequest"


/**
 * JOB_START_EVENT:
 *
 * Name of the event we generate when a job begins to be started.
 **/
#define JOB_START_EVENT "start"

/**
 * JOB_STARTED_EVENT:
 *
 * Name of the event we generate once a job has been started and is now
 * running.
 **/
#define JOB_STARTED_EVENT "started"

/**
 * JOB_STOP_EVENT:
 *
 * Name of the event we generate when a job begins to be stopped.
 **/
#define JOB_STOP_EVENT "stop"

/**
 * JOB_STOPPED_EVENT:
 *
 * Name of the event we generate once a job has been stopped and is now
 * waiting.
 **/
#define JOB_STOPPED_EVENT "stopped"


#define event_queue(_n) (event_emit ((_n), NULL, NULL, NULL, NULL))
#define event_queue_run event_poll

NIH_BEGIN_EXTERN

int paused;


Event *        event_new             (const void *parent, const char *name)
	__attribute__ ((warn_unused_result, malloc));

int            event_match           (Event *event1, Event *event2);

EventEmission *event_emit            (const char *name, char **args,
				      char **env, EventEmissionCb callback,
				      void *data)
	__attribute__ ((malloc));
EventEmission *event_emit_find_by_id (uint32_t id);
void           event_emit_finished   (EventEmission *emission);

void           event_poll            (void);

#if 0
EventEmission *event_read_state      (EventEmission *emission, char *buf)
	__attribute__ ((warn_unused_result, malloc));
void           event_write_state     (FILE *state);
#endif

NIH_END_EXTERN

#endif /* INIT_EVENT_H */
