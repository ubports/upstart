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

#ifndef UPSTART_ENUM_H
#define UPSTART_ENUM_H

#include <nih/macros.h>


/**
 * JobGoal:
 *
 * There are two ultimate goals for any job, either it should be stopped
 * or it should be started.  In order to achieve these goals, we may need
 * to go through a number of different states (defined by JobState).
 **/
typedef enum job_goal {
	JOB_STOP,
	JOB_START
} JobGoal;

/**
 * JobState:
 *
 * This is used to identify the current actual state of a job, suggesting
 * which process (pre-start, post-start, pre-stop, post-stop or the binary
 * itself) is running, or which interim state we are in.
 *
 * This is combined with the job's goal decide what to do with the
 * processes and which states to move into when changes in process state
 * (pid obtained or death) occur.
 **/
typedef enum job_state {
	JOB_WAITING,
	JOB_STARTING,
	JOB_PRE_START,
	JOB_SPAWNED,
	JOB_POST_START,
	JOB_RUNNING,
	JOB_PRE_STOP,
	JOB_STOPPING,
	JOB_KILLED,
	JOB_POST_STOP,
	JOB_DELETED
} JobState;

/**
 * JobAction:
 *
 * Each job has a list of associated actions, the first set of which are
 * built-in to Upstart and indexed by this enumeration.
 **/
typedef enum job_action {
	JOB_MAIN_ACTION,
	JOB_PRE_START_ACTION,
	JOB_POST_START_ACTION,
	JOB_PRE_STOP_ACTION,
	JOB_POST_STOP_ACTION,
	JOB_LAST_ACTION
} JobAction;

/**
 * ConsoleType:
 *
 * This is used to identify how a job would like its standard input, output
 * and error file descriptors arranged.  The options are to have these
 * mapped to /dev/null, the console device (without being or being the owning
 * process) or to the logging daemon.
 **/
typedef enum console_type {
	CONSOLE_LOGGED,
	CONSOLE_OUTPUT,
	CONSOLE_OWNER,
	CONSOLE_NONE
} ConsoleType;


NIH_BEGIN_EXTERN

const char * job_goal_name           (JobGoal goal)
	__attribute__ ((const));
JobGoal      job_goal_from_name      (const char *goal);

const char * job_state_name          (JobState state)
	__attribute__ ((const));
JobState     job_state_from_name     (const char *state);

const char * job_action_name         (JobAction action)
	__attribute__ ((const));
JobAction    job_action_from_name    (const char *action);

NIH_END_EXTERN

#endif /* UPSTART_ENUM_H */
