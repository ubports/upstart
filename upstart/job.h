/* upstart
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

#ifndef UPSTART_JOB_H
#define UPSTART_JOB_H

/**
 * JobGoal:
 *
 * There are two ultimate goals for any job, either it should be stopped
 * or it should be started.  In order to achieve these goals, we may need
 * to go through a number of different states, and even the processes
 * involved may need to go through different states.
 *
 * A typical example might be changing the goal of a process that is active
 * in the running state from start to stop; the actual activities and state
 * changes for that are:
 * - send process the TERM signal, set process state to killed
 * - wait for process to die, possibly send KILL signal
 * - change state to stopping, spawn the stop script and set process state
 *   to active
 * - wait for script to terminate
 * - change state to waiting and process state to none
 **/
typedef enum {
	JOB_STOP,
	JOB_START
} JobGoal;

/**
 * JobState:
 *
 * This is used to identify the current actual state of a job, suggesting
 * which process (start, stop and respawn scripts or the binary itself)
 * is spawning, running or terminating.
 *
 * This is combined with the job's goal decide what to do with the
 * processes (spawn or kill) and which states to move into when changes in
 * process state (pid obtained or death) occur.
 **/
typedef enum {
	JOB_WAITING,
	JOB_STARTING,
	JOB_RUNNING,
	JOB_STOPPING,
	JOB_RESPAWNING
} JobState;

/**
 * ProcessState:
 *
 * This is used to identify the current state of the process associated with
 * a job, whether one exists and whether it has been spawned but the pid not
 * yet obtained, whether it is actively running or whether it is in the
 * process of being killed.
 *
 * This is used during state changes to decide what action to take against
 * the running process.
 **/
typedef enum {
	PROCESS_NONE,
	PROCESS_SPAWNED,
	PROCESS_ACTIVE,
	PROCESS_KILLED
} ProcessState;

/**
 * ConsoleType:
 *
 * This is used to identify how a job would like its standard input, output
 * and error file descriptors arranged.  The options are to have these
 * mapped to /dev/null, the console device (without being or being the owning
 * process) or to the logging daemon.
 **/
typedef enum {
	CONSOLE_LOGGED,
	CONSOLE_OUTPUT,
	CONSOLE_OWNER,
	CONSOLE_NONE
} ConsoleType;

#endif /* UPSTART_JOB_H */
