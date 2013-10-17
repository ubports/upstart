/* upstart
 *
 * quiesce.c - shutdown handling.
 *
 * Copyright © 2013 Canonical Ltd.
 * Author: James Hunt <james.hunt@canonical.com>
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

#include "quiesce.h"
#include "events.h"
#include "environ.h"
#include "conf.h"
#include "job_process.h"
#include "control.h"

#include <nih/main.h>

/**
 * quiesce_requester:
 *
 * Where the quiesce request originated. This determines
 * the shutdown behaviour.
 **/
static QuiesceRequester quiesce_requester = QUIESCE_REQUESTER_INVALID;

/**
 * quiesce_phase:
 *
 * Current phase of shutdown.
 **/
static QuiescePhase quiesce_phase = QUIESCE_PHASE_NOT_QUIESCED;

/**
 * quiesce_reason:
 *
 * Human-readable string denoting what triggered the quiesce.
 **/
static char *quiesce_reason = NULL;

/**
 * max_kill_timeout:
 *
 * Maxiumum kill_timeout value calculated from all running jobs used to
 * determine how long to wait before exiting.
 **/
static time_t max_kill_timeout = 0;

/**
 * quiesce_phase_time:
 *
 * Time that a particular phase started.
 **/
static time_t quiesce_phase_time = 0;

/**
 * quiesce_start_time:
 *
 * Time quiesce commenced.
 **/
static time_t quiesce_start_time = 0;

/**
 * session_end_jobs:
 *
 * TRUE if any job specifies a 'start on' including SESSION_END_EVENT.
 *
 **/
static int session_end_jobs = FALSE;

static int quiesce_event_match (Event *event)
	__attribute__ ((warn_unused_result));

/* External definitions */
extern int disable_respawn;

/**
 * quiesce:
 *
 * @requester: where the quiesce request originated.
 *
 * Commence Session Init shutdown.
 **/
void
quiesce (QuiesceRequester requester)
{
	nih_local char  **env = NULL;
	Event            *event;

	job_class_init ();

	/* Quiesce already in progress */
	if (quiesce_phase != QUIESCE_PHASE_NOT_QUIESCED)
		return;

	quiesce_requester = requester;

	/* System shutdown skips the wait phase to ensure all running
	 * jobs get signalled.
	 *
	 * Note that jobs which choose to start on SESSION_END_EVENT may
	 * not complete (or even start), but no guarantee is possible in
	 * the system shutdown scenario since Session Inits must not
	 * hold up the system.
	 */
	quiesce_phase = (requester == QUIESCE_REQUESTER_SYSTEM)
		? QUIESCE_PHASE_KILL
		: QUIESCE_PHASE_WAIT;

	quiesce_reason = (requester == QUIESCE_REQUESTER_SESSION)
		? _("logout") : _("shutdown");

	nih_info (_("Quiescing due to %s request"), quiesce_reason);

	quiesce_start_time = quiesce_phase_time = time (NULL);

	/* Stop existing jobs from respawning */
	disable_respawn = TRUE;

	/* Signal that the session is ending. This may start new jobs.
	 *
	 * Note that the event doesn't actually get emitted until the
	 * next time the main loop gets a chance to run.
	 */
	env = NIH_MUST (nih_str_array_new (NULL));

	NIH_MUST (environ_set (&env, NULL, NULL, TRUE,
				"TYPE=%s", quiesce_reason));

	event = NIH_MUST (event_new (NULL, SESSION_END_EVENT, env));

	/* Check if any jobs care about the session end event. If not,
	 * the wait phase can be avoided entirely resulting in a much
	 * faster shutdown.
	 *
	 * Note that simply checking if running instances exist is not
	 * sufficient since if a job cares about the session end event,
	 * it won't yet have started but needs to be given a chance to
	 * run.
	 */
	if (quiesce_phase == QUIESCE_PHASE_WAIT) {

		session_end_jobs = quiesce_event_match (event);

		if (session_end_jobs) {
			/* Some as-yet unscheduled jobs care about the
			 * session end event. They will be started the
			 * next time through the main loop and will be
			 * waited for (hence the quiesce phase is not
			 * changed).
			 *
			 * However, already-running jobs *can* be stopped
			 * at this time since by definition they do not
			 * care about the session end event and may just
			 * as well die now to avoid slowing the shutdown.
			 */
			job_process_stop_all ();
		} else {
			nih_debug ("Skipping wait phase");
			quiesce_phase = QUIESCE_PHASE_KILL;
		}
	}

	if (quiesce_phase == QUIESCE_PHASE_KILL) {
		/* We'll attempt to wait for this long, but system
		 * policy may prevent it such that we just get killed
		 * and job processes reparented to PID 1.
		 */
		max_kill_timeout = job_class_max_kill_timeout ();

		job_process_stop_all ();
	}

	/* Check every second to see if all jobs have finished. If so,
	 * we can exit early.
	 */
	NIH_MUST (nih_timer_add_periodic (NULL, 1,
				(NihTimerCb)quiesce_wait_callback, NULL));
}

/**
 * quiesce_wait_callback:
 *
 * @data: not used,
 * @timer: timer that caused us to be called.
 *
 * Callback used to check if all jobs have finished and if so
 * finalise Session Init shutdown.
 **/
void
quiesce_wait_callback (void *data, NihTimer *timer)
{
	time_t now;

	nih_assert (timer);
	nih_assert (quiesce_phase_time);
	nih_assert (quiesce_requester != QUIESCE_REQUESTER_INVALID);

	now = time (NULL);

	if (quiesce_phase == QUIESCE_PHASE_KILL) {
		nih_assert (max_kill_timeout);

		if ((now - quiesce_phase_time) > max_kill_timeout)
			goto timed_out;

	} else if (quiesce_phase == QUIESCE_PHASE_WAIT) {
		int  timed_out = 0;

		timed_out = ((now - quiesce_phase_time) >= QUIESCE_DEFAULT_JOB_RUNTIME);

		if (timed_out
			|| (session_end_jobs && ! job_process_jobs_running ())
			|| ! job_process_jobs_running ()) {

			quiesce_phase = QUIESCE_PHASE_KILL;

			/* reset for new phase */
			quiesce_phase_time = time (NULL);

			max_kill_timeout = job_class_max_kill_timeout ();

			job_process_stop_all ();
		}
	} else {
		nih_assert_not_reached ();
	}

	if (! job_process_jobs_running ())
		goto out;

	return;

timed_out:
	quiesce_show_slow_jobs ();

out:
	/* Note that we might skip the kill phase for the session
	 * requestor if no jobs are actually running at this point.
	 */
	quiesce_phase = QUIESCE_PHASE_CLEANUP;
	quiesce_finalise ();

	/* Deregister */
	nih_free (timer);
}

/**
 * quiesce_show_slow_jobs:
 *
 * List jobs that are still running after their expected end time.
 **/
void
quiesce_show_slow_jobs (void)
{
	job_class_init ();

	NIH_HASH_FOREACH (job_classes, iter) {
		JobClass *class = (JobClass *)iter;

		/* Note that instances get killed in a random order */
		NIH_HASH_FOREACH (class->instances, job_iter) {
			const char  *name;
			Job         *job;
		       
			job = (Job *)job_iter;

			name = job_name (job);

			nih_warn ("job %s failed to stop", name);
		}
	}
}


/**
 * quiesce_finalise:
 *
 * Request shutdown.
 **/
void
quiesce_finalise (void)
{
	static int  finalising = FALSE;
	time_t      diff;

	nih_assert (quiesce_start_time);
	nih_assert (quiesce_phase == QUIESCE_PHASE_CLEANUP);

	if (finalising)
		return;

	finalising = TRUE;

	diff = time (NULL) - quiesce_start_time;

	nih_info (_("Quiesce %s sequence took %s%d second%s"),
			quiesce_reason,
			! (int)diff ? "<" : "",
			(int)diff ? (int)diff : 1,
			diff <= 1 ? "" : "s");

	nih_main_loop_exit (0);

}

/**
 * quiesce_complete:
 *
 * Force quiesce phase to finish.
 **/
void
quiesce_complete (void)
{
	quiesce_phase = QUIESCE_PHASE_CLEANUP;

	quiesce_finalise ();
}

/**
 * quiesce_event_match:
 * @event: event.
 *
 * Identify if any jobs _may_ start when the session ends.
 *
 * A simple heuristic is used such that there is no guarantee that the
 * jobs entire start condition will be satisfied at session-end.
 *
 * Returns: TRUE if any class specifies @event in its start
 * condition, else FALSE.
 **/
static int
quiesce_event_match (Event *event)
{
	nih_assert (event);

	job_class_init ();

	NIH_HASH_FOREACH (job_classes, iter) {
		JobClass *class = (JobClass *)iter;

		if (! class->start_on)
			continue;

		/* Note that only the jobs start on condition is
		 * relevant.
		 */
		NIH_TREE_FOREACH_POST (&class->start_on->node, iter) {
			EventOperator *oper = (EventOperator *)iter;

			switch (oper->type) {
			case EVENT_OR:
			case EVENT_AND:
				break;
			case EVENT_MATCH:
				/* Job may attempt to start as the session ends */
				if (event_operator_match (oper, event, NULL))
					return TRUE;
				break;
			default:
				nih_assert_not_reached ();
			}
		}
	}

	return FALSE;
}

/**
 * quiesce_in_progress:
 *
 * Determine if shutdown is in progress.
 *
 * Returns: TRUE if quiesce is in progress, else FALSE.
 **/
int
quiesce_in_progress (void)
{
	return quiesce_phase != QUIESCE_PHASE_NOT_QUIESCED;
}
