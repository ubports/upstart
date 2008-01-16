/* upstart
 *
 * Copyright © 2008 Canonical Ltd.
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
	JOB_POST_STOP
} JobState;

/**
 * JobWaitType:
 *
 * This is used to determine whether to leave the job in the spawned state
 * until a particular event occurs.
 **/
typedef enum job_wait_type {
	JOB_WAIT_NONE,
	JOB_WAIT_STOP,
	JOB_WAIT_DAEMON,
	JOB_WAIT_FORK
} JobWaitType;

/**
 * ProcessType:
 *
 * Each job has a list of associated processes, the first of which are
 * built-in to upstart and indexed by this enumeration.  PROCESS_LAST
 * is (slightly oddly) the first non-built-in process, and is normally
 * added or subtracted from the index to find the name.
 **/
typedef enum process_type {
	PROCESS_MAIN,
	PROCESS_PRE_START,
	PROCESS_POST_START,
	PROCESS_PRE_STOP,
	PROCESS_POST_STOP,
	PROCESS_LAST
} ProcessType;

/**
 * TraceState:
 *
 * We trace jobs to follow forks and detect execs in order to be able to
 * supervise daemon processes.  Unfortunately due to the "unique and arcane"
 * nature of ptrace(), we need to track some state.
 **/
typedef enum trace_state {
	TRACE_NONE,
	TRACE_NEW,
	TRACE_NEW_CHILD,
	TRACE_NORMAL
} TraceState;

/**
 * ConsoleType:
 *
 * This is used to identify how a job would like its standard input, output
 * and error file descriptors arranged.  The options are to have these
 * mapped to /dev/null, the console device (without being or being the owning
 * process) or to the logging daemon.
 **/
typedef enum console_type {
	CONSOLE_NONE,
	CONSOLE_OUTPUT,
	CONSOLE_OWNER
} ConsoleType;


NIH_BEGIN_EXTERN

const char * job_goal_name           (JobGoal goal)
	__attribute__ ((const));
JobGoal      job_goal_from_name      (const char *goal);

const char * job_state_name          (JobState state)
	__attribute__ ((const));
JobState     job_state_from_name     (const char *state);

const char * process_name            (ProcessType process)
	__attribute__ ((const));
ProcessType  process_from_name       (const char *process);

NIH_END_EXTERN

#endif /* UPSTART_ENUM_H */
