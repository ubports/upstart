/* upstart
 *
 * job.c - handling of tasks and services
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <string.h>

#include <nih/alloc.h>
#include <nih/logging.h>
#include <nih/error.h>
#include <nih/string.h>

#include "job.h"


/**
 * jobs:
 *
 * This list holds the list of known jobs, each entry is of the Job
 * structure.  No particular order is maintained.
 **/
static NihList *jobs = NULL;


/**
 * job_init:
 *
 * Initialise the list of jobs.
 **/
static inline void
job_init (void)
{
	if (! jobs)
		NIH_MUST (jobs = nih_list_new ());
}


/**
 * job_new:
 * @parent: parent of new job,
 * @name: name of new job,
 *
 * Allocates and returns a new Job structure with the @name given, and
 * appends it to the internal list of registered jobs.  It is up to the
 * caller to ensure that @name is unique amongst the job list.
 *
 * The job can be removed using #nih_list_free.
 *
 * Returns: newly allocated job structure or %NULL if insufficient memory.
 **/
Job *
job_new (void       *parent,
	 const char *name)
{
	Job *job;
	int  i;

	nih_assert (name != NULL);
	nih_assert (strlen (name) > 0);

	job_init ();

	job = nih_new (parent, Job);
	if (! job)
		return NULL;

	nih_list_init (&job->entry);

	job->name = nih_strdup (job, name);
	if (! job->name) {
		nih_free (job);
		return NULL;
	}

	job->description = NULL;
	job->author = NULL;
	job->version = NULL;

	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->process_state = PROCESS_NONE;

	job->kill_timeout = JOB_DEFAULT_KILL_TIMEOUT;
	job->kill_timer = NULL;

	job->normalexit = NULL;
	job->normalexit_len = 0;

	job->pidfile = NULL;
	job->binary = NULL;
	job->pid_timeout = JOB_DEFAULT_PID_TIMEOUT;
	job->pid_timer = NULL;

	job->command = NULL;
	job->script = NULL;
	job->start_script = NULL;
	job->stop_script = NULL;
	job->respawn_script = NULL;

	job->console = CONSOLE_LOGGED;
	job->env = NULL;

	job->umask = JOB_DEFAULT_UMASK;
	job->nice = 0;
	job->chroot = NULL;
	job->chdir = NULL;

	for (i = 0; i < RLIMIT_NLIMITS; i++)
		job->limits[i] = NULL;

	nih_list_add (jobs, &job->entry);

	return job;
}
