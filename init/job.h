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

#ifndef INIT_JOB_H
#define INIT_JOB_H

#include <sys/types.h>
#include <sys/resource.h>

#include <time.h>

#include <nih/macros.h>
#include <nih/list.h>
#include <nih/timer.h>

#include <upstart/job.h>

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
 * @start_events: list of events that can start this job,
 * @stop_events; list of events that can stop this job.
 * @process_state: what we're waiting for from the process,
 * @pid: current process id,
 * @kill_timeout: time to wait between sending TERM and KILL signals,
 * @kill_timer: timer to kill process,
 * @spawns_instance: job is always waiting and spawns instances,
 * @is_instance: job should be cleaned up instead of waiting,
 * @respawn: process should be restarted if it fails,
 * @normalexit: array of exit codes that prevent a respawn,
 * @normalexit_len: length of @normalexit array,
 * @daemon: process forks into background; pid needs to be obtained,
 * @pidfile: obtain pid by reading this file,
 * @binary: obtain pid by locating this binary,
 * @pid_timeout: time to wait before giving up obtaining pid,
 * @pid_timer: timer for pid location,
 * @command: command to be run as the primary process,
 * @script: script to run instead of @command,
 * @start_script: script to run before @command is started,
 * @stop_script: script to run after @command is stopped,
 * @respawn_script: script to run between @command respawns,
 * @console: how to arrange the job's stdin/out/err file descriptors,
 * @env: %NULL-terminated list of environment strings to set,
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

	NihList        start_events;
	NihList        stop_events;

	ProcessState   process_state;
	pid_t          pid;
	time_t         kill_timeout;
	NihTimer      *kill_timer;

	int            spawns_instance;
	int            is_instance;

	int            respawn;
	int           *normalexit;
	size_t         normalexit_len;

	int            daemon;
	char          *pidfile;
	char          *binary;
	time_t         pid_timeout;
	NihTimer      *pid_timer;

	char          *command;
	char          *script;
	char          *start_script;
	char          *stop_script;
	char          *respawn_script;

	ConsoleType    console;
	char         **env;

	mode_t         umask;
	int            nice;
	struct rlimit *limits[RLIMIT_NLIMITS];
	char          *chroot;
	char          *chdir;
} Job;


NIH_BEGIN_EXTERN

Job *       job_new          (void *parent, const char *name);

Job *       job_find_by_name (const char *name);
Job *       job_find_by_pid  (pid_t pid);

void        job_change_state (Job *job, JobState state);
JobState    job_next_state   (Job *job);

void        job_run_command  (Job *job, const char *command);
void        job_run_script   (Job *job, const char *script);

void        job_kill_process (Job *job);

void        job_handle_child (void *ptr, pid_t pid, int killed, int status);

void        job_start        (Job *job);
void        job_stop         (Job *job);

void        job_start_event  (Job *job, Event *event);
void        job_stop_event   (Job *job, Event *event);
void        job_handle_event (Event *event);

NIH_END_EXTERN

#endif /* INIT_JOB_H */
