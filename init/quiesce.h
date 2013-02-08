/* upstart
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

#ifndef INIT_QUIESCE_H
#define INIT_QUIESCE_H

#include <nih/timer.h>

/**
 * QUIESCE_DEFAULT_JOB_RUNTIME:
 *
 * The default maximum length of time to wait after emitting the
 * SESSION_END_EVENT event before stopping all jobs.
 **/
#define QUIESCE_DEFAULT_JOB_RUNTIME 5

/**
 * QuiesceRequester:
 *
 * Reason for Session Init wishing to shutdown; either the Session Init
 * has been notified the system is being shutdown, or the session has
 * requested it be ended (for example due to a user logout request).
 **/
typedef enum quiesce_requester {
	QUIESCE_REQUESTER_INVALID = -1,
	QUIESCE_REQUESTER_SYSTEM = 0,
	QUIESCE_REQUESTER_SESSION,
} QuiesceRequester;

/**
 * QuiescePhase:
 *
 * Phase 0: No quiesce operation in progress.
 *
 * Phase 1: Wait: Period between SESSION_END_EVENT being emitted and
 *          QUIESCE_DEFAULT_JOB_RUNTIME being reached.
 *
 * Phase 2: Kill: Period between QUIESCE_DEFAULT_JOB_RUNTIME being
 *          reached and kill signal being sent to all jobs.
 *
 * Phase 3: Cleanup: Period between all jobs having ended
 *          (either naturally or by induction) and final exit.
 **/
typedef enum quiesce_phase {
	QUIESCE_PHASE_NOT_QUIESCED,
	QUIESCE_PHASE_WAIT,
	QUIESCE_PHASE_KILL,
	QUIESCE_PHASE_CLEANUP,
} QuiescePhase;

NIH_BEGIN_EXTERN

void    quiesce                (QuiesceRequester requester);
void    quiesce_wait_callback  (void *data, NihTimer *timer);
void    quiesce_show_slow_jobs (void);
void    quiesce_finalise       (void);

NIH_END_EXTERN

#endif /* INIT_QUIESCE_H */
