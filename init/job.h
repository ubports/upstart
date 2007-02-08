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

#ifndef INIT_JOB_H
#define INIT_JOB_H

#include <sys/types.h>
#include <sys/resource.h>

#include <time.h>
#include <stdio.h>

#include <nih/macros.h>
#include <nih/list.h>
#include <nih/timer.h>
#include <nih/main.h>

#include <upstart/enum.h>

#include "event.h"


/**
 * JOB_DEFAULT_PID_TIMEOUT:
 *
 * The default length of time to wait after spawning a daemon process for
 * the pid to be obtained before giving up and assuming the job did not
 * start.
 **/
#define JOB_DEFAULT_PID_TIMEOUT 10

/**
 * JOB_DEFAULT_KILL_TIMEOUT:
 *
 * The default length of time to wait after sending a process the TERM
 * signal before sending the KILL signal if it hasn't terminated.
 **/
#define JOB_DEFAULT_KILL_TIMEOUT 5

/**
 * JOB_DEFAULT_RESPAWN_LIMIT:
 *
 * The default number of times in JOB_DEFAULT_RESPAWN_INTERVAL seconds that
 * we permit a process to respawn before stoping it
 **/
#define JOB_DEFAULT_RESPAWN_LIMIT 10

/**
 * JOB_DEFAULT_RESPAWN_INTERVAL:
 *
 * The default number of seconds before resetting the respawn timer.
 **/
#define JOB_DEFAULT_RESPAWN_INTERVAL 5

/**
 * JOB_DEFAULT_UMASK:
 *
 * The default file creation mark for processes.
 **/
#define JOB_DEFAULT_UMASK 022




/**
 * Job:
 * @entry: list header,
 * @name: string name of the job; namespace shared with events,
 * @description: description of the job; intended for humans,
 * @author: author of the job; intended for humans,
 * @version: version of the job; intended for humans,
 * @goal: whether the job is to be stopped or started,
 * @state: actual state of the job,
 * @pid: current process id,
 * @cause: cause of last goal change,
 * @failed: whether the last process ran failed,
 * @failed_state: state the job was in for the last failed process,
 * @exit_status: exit status of the last failed process,
 * @start_events: list of events that can start this job,
 * @stop_events; list of events that can stop this job.
 * @emits: list of additional events that this job can emit,
 * @kill_timeout: time to wait between sending TERM and KILL signals,
 * @kill_timer: timer to kill process,
 * @spawns_instance: job is always waiting and spawns instances,
 * @is_instance: job should be cleaned up instead of waiting,
 * @service: job has reached its goal when running,
 * @respawn: process should be restarted if it fails,
 * @respawn_limit: number of respawns in @respawn_interval that we permit,
 * @respawn_interval: barrier for @respawn_limit,
 * @respawn_count: number of respawns since @respawn_time,
 * @respawn_time: time service was first respawned,
 * @normalexit: array of exit codes that prevent a respawn,
 * @normalexit_len: length of @normalexit array,
 * @daemon: process forks into background; pid needs to be obtained,
 * @pid_file: obtain pid by reading this file,
 * @pid_binary: obtain pid by locating this binary,
 * @pid_timeout: time to wait before giving up obtaining pid,
 * @pid_timer: timer for pid location,
 * @command: command to be run as the primary process,
 * @script: script to run instead of @command,
 * @start_script: script to run before @command is started,
 * @stop_script: script to run after @command is stopped,
 * @respawn_script: script to run between @command respawns,
 * @console: how to arrange the job's stdin/out/err file descriptors,
 * @env: NULL-terminated list of environment strings to set,
 * @umask: file mode creation mask,
 * @nice: process priority,
 * @limits: resource limits indexed by resource,
 * @chroot: root directory of process (implies @chdir if not set),
 * @chdir: working directory of process,
 *
 * This structure represents a known task or service that should be tracked
 * by the init daemon; as tasks and services are fundamentally identical,
 * except for the handling when the main process terminates, they are both
 * collated together in this structure and only differ in the value of the
 * @respawn member.
 **/
typedef struct job {
	NihList        entry;

	char          *name;
	char          *description;
	char          *author;
	char          *version;

	JobGoal        goal;
	JobState       state;
	pid_t          pid;

	EventEmission *cause;

	int            failed;
	JobState       failed_state;
	int            exit_status;

	NihList        start_events;
	NihList        stop_events;
	NihList        emits;

	int           *normalexit;
	size_t         normalexit_len;

	time_t         kill_timeout;
	NihTimer      *kill_timer;

	int            spawns_instance;
	int            is_instance;

	int            service;
	int            respawn;
	int            respawn_limit;
	time_t         respawn_interval;
	int            respawn_count;
	time_t         respawn_time;

	int            daemon;
	char          *pid_file;
	char          *pid_binary;
	time_t         pid_timeout;
	NihTimer      *pid_timer;

	char          *command;
	char          *script;
	char          *start_script;
	char          *stop_script;

	ConsoleType    console;
	char         **env;

	mode_t         umask;
	int            nice;
	struct rlimit *limits[RLIMIT_NLIMITS];
	char          *chroot;
	char          *chdir;
} Job;

/**
 * JobName:
 * @entry: list header,
 * @name: name of job.
 *
 * This structure is used to form lists of job names, for example in the
 * depends list of an ordinary Job.
 **/
typedef struct job_name {
	NihList  entry;
	char    *name;
} JobName;


NIH_BEGIN_EXTERN

NihList *   job_list            (void);

Job *       job_new             (const void *parent, const char *name)
	__attribute__ ((warn_unused_result, malloc));

Job *       job_find_by_name    (const char *name);
Job *       job_find_by_pid     (pid_t pid);

void        job_change_goal     (Job *job, JobGoal goal,
				 EventEmission *emission);

void        job_change_state    (Job *job, JobState state);
JobState    job_next_state      (Job *job);

void        job_run_command     (Job *job, const char *command);
void        job_run_script      (Job *job, const char *script);

void        job_kill_process    (Job *job);

void        job_child_reaper    (void *ptr, pid_t pid, int killed, int status);

void        job_handle_event    (EventEmission *emission);

void        job_detect_stalled  (void);

NIH_END_EXTERN

#endif /* INIT_JOB_H */
