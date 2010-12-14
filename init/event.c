/* upstart
 *
 * event.c - event queue and handling
 *
 * Copyright © 2010 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <string.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/hash.h>
#include <nih/main.h>
#include <nih/logging.h>
#include <nih/error.h>

#include "dbus/upstart.h"

#include "environ.h"
#include "event.h"
#include "job.h"
#include "blocked.h"
#include "errors.h"

#include "com.ubuntu.Upstart.h"


/* Prototypes for static functions */
static void event_pending              (Event *event);
static void event_pending_handle_jobs  (Event *event);
static void event_finished             (Event *event);


/**
 * events:
 *
 * This list holds the list of events in the process of pending, being
 * handled or awaiting cleanup; each item is an Event structure.
 **/
NihList *events = NULL;


/**
 * event_init:
 *
 * Initialise the event list.
 **/
void
event_init (void)
{
	if (! events)
		events = NIH_MUST (nih_list_new (NULL));
}


/**
 * event_new:
 * @parent: parent object for new event,
 * @name: name of event to emit,
 * @env: NULL-terminated array of environment variables for event.
 *
 * Allocates an Event structure for the event details given and
 * appends it to the queue of events.
 *
 * @env is optional, and may be NULL; if given it should be a NULL-terminated
 * array of environment variables in KEY=VALUE form.  @env will be referenced
 * by the new event.  After calling this function, you should never use
 * nih_free() to free @env and instead use nih_unref() or nih_discard() if
 * you no longer need to use it.
 *
 * When the event reaches the top of the queue, it is taken off and placed
 * into the handling queue.  It is not removed from that queue until there
 * are no remaining references to it.
 *
 * The event is created with nothing blocking it.  Be sure to call
 * event_block() otherwise it will be automatically freed next time
 * through the main loop.
 *
 * If @parent is not NULL, it should be a pointer to another object which
 * will be used as a parent for the returned event.  When all parents
 * of the returned event are freed, the returned event will also be
 * freed.
 *
 * Returns: new Event structure pending in the queue or NULL if insufficent
 * memory.
 **/
Event *
event_new (const void  *parent,
	   const char  *name,
	   char      **env)
{
	Event *event;

	nih_assert (name != NULL);
	nih_assert (strlen (name) > 0);

	event_init ();

	event = nih_new (parent, Event);
	if (! event)
		return NULL;

	nih_list_init (&event->entry);

	event->progress = EVENT_PENDING;
	event->failed = FALSE;

	event->blockers = 0;
	nih_list_init (&event->blocking);

	nih_alloc_set_destructor (event, nih_list_destroy);


	/* Fill in the event details */
	event->name = nih_strdup (event, name);
	if (! event->name) {
		nih_free (event);
		return NULL;
	}

	event->env = env;
	if (event->env)
		nih_ref (event->env, event);


	/* Place it in the pending list */
	nih_debug ("Pending %s event", name);
	nih_list_add (events, &event->entry);

	nih_main_loop_interrupt ();

	return event;
}


/**
 * event_block:
 * @event: event to block.
 *
 * This function should be called by jobs that wish to hold a reference on
 * the event and block it from finishing.
 *
 * Once the reference is no longer needed, you must call event_unblock()
 * to allow the event to be finished, and potentially freed.
 **/
void
event_block (Event *event)
{
	nih_assert (event != NULL);

	event->blockers++;
}

/**
 * event_unblock:
 * @event: event to unblock.
 *
 * This function should be called by jobs that are holding a reference on the
 * event which blocks it from finishing, and wish to discard that reference.
 *
 * It must match a previous call to event_block().
 **/
void
event_unblock (Event *event)
{
	nih_assert (event != NULL);
	nih_assert (event->blockers > 0);

	event->blockers--;
}


/**
 * event_poll:
 *
 * This function is used to process the list of events; any in the pending
 * state are moved into the handling state and job states changed.  Any
 * in the finished state will have subscribers and jobs notified that the
 * event has completed.
 *
 * Events remain in the handling state while they have blocking jobs.
 *
 * This function will only return once the events list is empty, or all
 * events are in the handling state; so any time an event queues another,
 * it will be processed immediately.
 *
 * Normally this function is used as a main loop callback.
 **/
void
event_poll (void)
{
	int poll_again;

	event_init ();

	do {
		poll_again = FALSE;

		NIH_LIST_FOREACH_SAFE (events, iter) {
			Event *event = (Event *)iter;

			/* Ignore events that we're handling and are
			 * blocked, there's nothing we can do to hurry them.
			 *
			 * Decide whether to poll again based on the state
			 * before handling the event; that way we always loop
			 * at least once more after finding a pending or
			 * finished event, in case they added new events as
			 * a side effect that we missed.
			 */
			switch (event->progress) {
			case EVENT_PENDING:
				event_pending (event);
				poll_again = TRUE;

				/* fall through */
			case EVENT_HANDLING:
				if (event->blockers)
					break;

				event->progress = EVENT_FINISHED;
				/* fall through */
			case EVENT_FINISHED:
				event_finished (event);
				poll_again = TRUE;
				break;
			default:
				nih_assert_not_reached ();
			}
		}
	} while (poll_again);
}


/**
 * event_pending:
 * @event: pending event.
 *
 * This function is called for each event in the list that is in the pending
 * state.  Subscribers to emitted events are notified, and the event is
 * passed to the job system to start or stop any.
 *
 * The event is marked as handling; if no jobs took it, then it is
 * immediately finished.
 **/
static void
event_pending (Event *event)
{
	nih_assert (event != NULL);
	nih_assert (event->progress == EVENT_PENDING);

	nih_info (_("Handling %s event"), event->name);
	event->progress = EVENT_HANDLING;

	event_pending_handle_jobs (event);
}

/**
 * event_pending_handle_jobs:
 * @event: event to be handled.
 *
 * This function is called whenever an event reaches the handling state.
 * It iterates the list of jobs and stops or starts any necessary.
 **/
static void
event_pending_handle_jobs (Event *event)
{
	nih_assert (event != NULL);

	job_class_init ();

	NIH_HASH_FOREACH_SAFE (job_classes, iter) {
		JobClass *class = (JobClass *)iter;

		/* We stop first so that if an event is listed both as a
		 * stop and start event, it causes an active running process
		 * to be killed, the stop script then the start script to be
		 * run.  In any other state, it has no special effect.
		 *
		 * (The other way around would be just strange, it'd cause
		 * a process's start and stop scripts to be run without the
		 * actual process).
		 */
		NIH_HASH_FOREACH_SAFE (class->instances, job_iter) {
			Job *job = (Job *)job_iter;

			if (job->stop_on
			    && event_operator_handle (job->stop_on, event,
						      job->env)
			    && job->stop_on->value) {
				if (job->goal != JOB_STOP) {
					size_t len = 0;

					if (job->stop_env)
						nih_unref (job->stop_env, job);
					job->stop_env = NULL;

					/* Collect environment that stopped
					 * the job for the pre-stop script;
					 * it can make a more informed
					 * decision whether the stop is valid.
					 * We don't add class environment
					 * since this is appended to the
					 * existing job environment.
					 */
					NIH_MUST (event_operator_environment (
						job->stop_on, &job->stop_env,
						job, &len, "UPSTART_STOP_EVENTS"));

					job_finished (job, FALSE);

					event_operator_events (
						job->stop_on,
						job, &job->blocking);

					job_change_goal (job, JOB_STOP);
				}

				event_operator_reset (job->stop_on);
			}

		}

		/* Now we match the start events for the class to see
		 * whether we need a new instance.
		 */
		if (class->start_on
		    && event_operator_handle (class->start_on, event, NULL)
		    && class->start_on->value) {
			nih_local char **env = NULL;
			nih_local char  *name = NULL;
			size_t           len;
			Job             *job;

			/* Construct the environment for the new instance
			 * from the class and the start events.
			 */
			env = NIH_MUST (job_class_environment (
					  NULL, class, &len));
			NIH_MUST (event_operator_environment (class->start_on,
							      &env, NULL, &len,
							      "UPSTART_EVENTS"));

			/* Expand the instance name against the environment */
			name = NIH_SHOULD (environ_expand (NULL,
							   class->instance,
							   env));
			if (! name) {
				NihError *err;

				err = nih_error_get ();
				nih_warn (_("Failed to obtain %s instance: %s"),
					  class->name, err->message);
				nih_free (err);

				event_operator_reset (class->start_on);
				continue;
			}

			/* Locate the current instance or create a new one */
			job = (Job *)nih_hash_lookup (class->instances, name);
			if (! job)
				job = NIH_MUST (job_new (class, name));

			nih_debug ("New instance %s", job_name (job));

			/* Start the job with the environment we want */
			if (job->goal != JOB_START) {
				if (job->start_env)
					nih_unref (job->start_env, job);

				job->start_env = env;
				nih_ref (job->start_env, job);

				job_finished (job, FALSE);

				event_operator_events (job->class->start_on,
						       job, &job->blocking);

				job_change_goal (job, JOB_START);
			}

			event_operator_reset (class->start_on);
		}
	}
}


/**
 * event_finished:
 * @event: finished event.
 *
 * This function is called for each event in the list that is in the finished
 * state.  Subscribers and jobs are notified, then, if the event failed, a
 * new pending failed event is queued.  Finally the event is freed and
 * removed from the list.
 **/
static void
event_finished (Event *event)
{
	nih_assert (event != NULL);
	nih_assert (event->progress == EVENT_FINISHED);

	nih_debug ("Finished %s event", event->name);

	NIH_LIST_FOREACH_SAFE (&event->blocking, iter) {
		Blocked *blocked = (Blocked *)iter;

		switch (blocked->type) {
		case BLOCKED_JOB:
			/* Event was blocking a job, let it enter the
			 * next state.
			 */
			blocked->job->blocker = NULL;
			job_change_state (blocked->job,
					  job_next_state (blocked->job));

			break;
		case BLOCKED_EMIT_METHOD:
			/* Event was blocking an emit method call, send
			 * the reply, or an error if the event failed.
			 */
			if (event->failed) {
				NIH_ZERO (nih_dbus_message_error (
						  blocked->message,
						  DBUS_INTERFACE_UPSTART ".Error.EventFailed",
						  "%s", _("Event failed")));
			} else {
				NIH_ZERO (control_emit_event_reply (
						  blocked->message));
			}

			break;
		default:
			nih_assert_not_reached ();
		}

		nih_free  (blocked);
	}

	if (event->failed) {
		char *name;

		name = strrchr (event->name, '/');
		if ((! name) || strcmp (name, "/failed")) {
			nih_local char *failed = NULL;
			Event          *new_event;

			failed = NIH_MUST (nih_sprintf (NULL, "%s/failed",
							event->name));
			new_event = NIH_MUST (event_new (NULL, failed, NULL));

			if (event->env)
				new_event->env = NIH_MUST (nih_str_array_copy (
						  new_event, NULL, event->env));
		}
	}

	nih_free (event);
}
