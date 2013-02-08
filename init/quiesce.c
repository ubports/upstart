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
 * max_kill_timeout:
 *
 * Maxiumum kill_timout value calculated from all running jobs used to
 * determine how long to wait before exiting.
 **/
static time_t max_kill_timeout = 0;

/**
 * quiesce_phase_time:
 *
 * Time that a particular phase started.
 **/
static time_t quiesce_phase_time = 0;

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
	const char       *reason;

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

	reason = (requester == QUIESCE_REQUESTER_SESSION)
		? _("logout") : _("shutdown");

	nih_info (_("Quiescing due to %s request"), reason);

	quiesce_phase_time = time (NULL);

	/* Stop existing jobs from respawning */
	disable_respawn = TRUE;

	/* Signal that the session is ending. This may start new jobs.
	 *
	 * Note that the event doesn't actually get emitted until the
	 * next time the main loop gets a chance to run.
	 */
	env = NIH_MUST (nih_str_array_new (NULL));

	NIH_MUST (environ_set (&env, NULL, NULL, TRUE,
				"TYPE=%s", reason));

	NIH_MUST (event_new (NULL, SESSION_END_EVENT, env));

	if (requester == QUIESCE_REQUESTER_SYSTEM) {
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

	now = time (NULL);

	nih_assert (quiesce_requester != QUIESCE_REQUESTER_INVALID);

	if (quiesce_requester == QUIESCE_REQUESTER_SYSTEM) {
		nih_assert (quiesce_phase == QUIESCE_PHASE_KILL);

		if ((now - quiesce_phase_time) > max_kill_timeout)
			goto out;

	} else if (quiesce_phase == QUIESCE_PHASE_WAIT) {

		if ((now - quiesce_phase_time) > QUIESCE_DEFAULT_JOB_RUNTIME) {
			quiesce_phase = QUIESCE_PHASE_KILL;

			/* reset for new phase */
			quiesce_phase_time = time (NULL);

			max_kill_timeout = job_class_max_kill_timeout ();
			job_process_stop_all ();
		}
	} else if (quiesce_phase == QUIESCE_PHASE_KILL) {

		if ((now - quiesce_phase_time) > max_kill_timeout)
			goto out;
	} else {
		nih_assert_not_reached ();
	}

	if (! job_process_jobs_running ())
		goto out;

	return;

out:
	quiesce_show_slow_jobs ();

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
			nih_local const char  *name = NULL;
			Job                   *job;
		       
			job = (Job *)job_iter;

			name = job_name (job);

			nih_message ("job %s failed to stop", name);
		}
	}
}


/**
 * quiesce_finalise:
 *
 * Perform final shutdown operations.
 **/
void
quiesce_finalise (void)
{
	nih_assert (quiesce_phase == QUIESCE_PHASE_CLEANUP);

	/* Cleanup */
	conf_destroy ();
	session_destroy ();
	control_cleanup ();

	nih_main_loop_exit (0);
}
