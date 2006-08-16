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


#include <nih/macros.h>

#include <upstart/job.h>


/**
 * job_state_name:
 * @state: state to convert.
 *
 * Converts an enumerated job state into the string used for the event
 * and for logging purposes.
 *
 * Returns: static string or %NULL if state not known.
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
