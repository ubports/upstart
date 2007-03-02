/* upstart
 *
 * enum.c - conversion of enums into strings, and vice-versa
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <string.h>

#include <nih/macros.h>
#include <nih/logging.h>

#include <upstart/enum.h>


/**
 * job_goal_name:
 * @goal: goal to convert.
 *
 * Converts an enumerated job goal into the string used for the status
 * and for logging purposes.
 *
 * Returns: static string or NULL if goal not known.
 **/
const char *
job_goal_name (JobGoal goal)
{
	switch (goal) {
	case JOB_STOP:
		return N_("stop");
	case JOB_START:
		return N_("start");
	default:
		return NULL;
	}
}

/**
 * job_goal_from_name:
 * @goal: goal to convert.
 *
 * Converts a job goal string into the enumeration.
 *
 * Returns: enumerated goal or -1 if not known.
 **/
JobGoal
job_goal_from_name (const char *goal)
{
	nih_assert (goal != NULL);

	if (! strcmp (goal, "stop")) {
		return JOB_STOP;
	} else if (! strcmp (goal, "start")) {
		return JOB_START;
	} else {
		return -1;
	}
}


/**
 * job_state_name:
 * @state: state to convert.
 *
 * Converts an enumerated job state into the string used for the status
 * and for logging purposes.
 *
 * Returns: static string or NULL if state not known.
 **/
const char *
job_state_name (JobState state)
{
	switch (state) {
	case JOB_WAITING:
		return N_("waiting");
	case JOB_STARTING:
		return N_("starting");
	case JOB_PRE_START:
		return N_("pre-start");
	case JOB_SPAWNED:
		return N_("spawned");
	case JOB_POST_START:
		return N_("post-start");
	case JOB_RUNNING:
		return N_("running");
	case JOB_PRE_STOP:
		return N_("pre-stop");
	case JOB_STOPPING:
		return N_("stopping");
	case JOB_KILLED:
		return N_("killed");
	case JOB_POST_STOP:
		return N_("post-stop");
	case JOB_DELETED:
		return N_("deleted");
	default:
		return NULL;
	}
}

/**
 * job_state_from_name:
 * @state: state to convert.
 *
 * Converts a job state string into the enumeration.
 *
 * Returns: enumerated state or -1 if not known.
 **/
JobState
job_state_from_name (const char *state)
{
	nih_assert (state != NULL);

	if (! strcmp (state, "waiting")) {
		return JOB_WAITING;
	} else if (! strcmp (state, "starting")) {
		return JOB_STARTING;
	} else if (! strcmp (state, "pre-start")) {
		return JOB_PRE_START;
	} else if (! strcmp (state, "spawned")) {
		return JOB_SPAWNED;
	} else if (! strcmp (state, "post-start")) {
		return JOB_POST_START;
	} else if (! strcmp (state, "running")) {
		return JOB_RUNNING;
	} else if (! strcmp (state, "pre-stop")) {
		return JOB_PRE_STOP;
	} else if (! strcmp (state, "stopping")) {
		return JOB_STOPPING;
	} else if (! strcmp (state, "killed")) {
		return JOB_KILLED;
	} else if (! strcmp (state, "post-stop")) {
		return JOB_POST_STOP;
	} else if (! strcmp (state, "deleted")) {
		return JOB_DELETED;
	} else {
		return -1;
	}
}


/**
 * process_name:
 * @process: process type to convert.
 *
 * Converts an enumerated process type into the string used for the status
 * and for logging purposes.
 *
 * Returns: static string or NULL if action not known.
 **/
const char *
process_name (ProcessType process)
{
	switch (process) {
	case PROCESS_MAIN:
		return N_("main");
	case PROCESS_PRE_START:
		return N_("pre-start");
	case PROCESS_POST_START:
		return N_("post-start");
	case PROCESS_PRE_STOP:
		return N_("pre-stop");
	case PROCESS_POST_STOP:
		return N_("post-stop");
	default:
		return NULL;
	}
}

/**
 * process_from_name:
 * @process: process string to convert.
 *
 * Converts a process type string into the enumeration.
 *
 * Returns: enumerated action or -1 if not known.
 **/
ProcessType
process_from_name (const char *process)
{
	nih_assert (process != NULL);

	if (! strcmp (process, "main")) {
		return PROCESS_MAIN;
	} else if (! strcmp (process, "pre-start")) {
		return PROCESS_PRE_START;
	} else if (! strcmp (process, "post-start")) {
		return PROCESS_POST_START;
	} else if (! strcmp (process, "pre-stop")) {
		return PROCESS_PRE_STOP;
	} else if (! strcmp (process, "post-stop")) {
		return PROCESS_POST_STOP;
	} else {
		return -1;
	}
}
