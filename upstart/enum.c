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
 * Returns: static string or NULL if state not known.
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
	case JOB_RUNNING:
		return N_("running");
	case JOB_STOPPING:
		return N_("stopping");
	case JOB_RESPAWNING:
		return N_("respawning");
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
	} else if (! strcmp (state, "running")) {
		return JOB_RUNNING;
	} else if (! strcmp (state, "stopping")) {
		return JOB_STOPPING;
	} else if (! strcmp (state, "respawning")) {
		return JOB_RESPAWNING;
	} else {
		return -1;
	}
}


/**
 * process_state_name:
 * @state: state to convert.
 *
 * Converts an enumerated process state into the string used for the status
 * and for logging purposes.
 *
 * Returns: static string or NULL if state not known.
 **/
const char *
process_state_name (ProcessState state)
{
	switch (state) {
	case PROCESS_NONE:
		return N_("none");
	case PROCESS_SPAWNED:
		return N_("spawned");
	case PROCESS_ACTIVE:
		return N_("active");
	case PROCESS_KILLED:
		return N_("killed");
	default:
		return NULL;
	}
}

/**
 * process_state_from_name:
 * @state: state to convert.
 *
 * Converts a process state string into the enumeration.
 *
 * Returns: enumerated process state or -1 if not known.
 **/
ProcessState
process_state_from_name (const char *state)
{
	nih_assert (state != NULL);

	if (! strcmp (state, "none")) {
		return PROCESS_NONE;
	} else if (! strcmp (state, "spawned")) {
		return PROCESS_SPAWNED;
	} else if (! strcmp (state, "active")) {
		return PROCESS_ACTIVE;
	} else if (! strcmp (state, "killed")) {
		return PROCESS_KILLED;
	} else {
		return -1;
	}
}
