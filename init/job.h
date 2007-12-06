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
#include <nih/hash.h>
#include <nih/timer.h>
#include <nih/child.h>
#include <nih/main.h>

#include "enum.h"
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
 * JobProcess:
 * @script: whether a shell will be required,
 * @command: command or script to be run.
 *
 * This structure represents an individual action process within the job.
 * When @script is FALSE, @command is checked for shell characters; if there
 * are none, it is split on whitespace and executed directly using exec().
 * If there are shell characters, or @script is TRUE, @command is executed
 * using a shell.
 **/
typedef struct job_process {
	int    script;
	char  *command;
} JobProcess;

/**
 * JobConfig:
 * @entry: list header,
 * @name: string name of the job,
 * @description: description of the job; intended for humans,
 * @author: author of the job; intended for humans,
 * @version: version of the job; intended for humans,
 * @start_on: event operator expression that can start this job,
 * @stop_on: event operator expression that can stop this job.
 * @emits: list of additional events that this job can emit,
 * @process: processes to be run,
 * @wait_for: what to wait for before entering the next state after spawned,
 * @kill_timeout: time to wait between sending TERM and KILL signals,
 * @instance: job may have multiple instances,
 * @service: job has reached its goal when running,
 * @respawn: process should be restarted if it fails,
 * @respawn_limit: number of respawns in @respawn_interval that we permit,
 * @respawn_interval: barrier for @respawn_limit,
 * @normalexit: array of exit codes that prevent a respawn,
 * @normalexit_len: length of @normalexit array,
 * @daemon: process forks into background; pid needs to be obtained,
 * @pid_file: obtain pid by reading this file,
 * @pid_binary: obtain pid by locating this binary,
 * @pid_timeout: time to wait before giving up obtaining pid,
 * @console: how to arrange the job's stdin/out/err file descriptors,
 * @env: NULL-terminated list of environment strings to set,
 * @umask: file mode creation mask,
 * @nice: process priority,
 * @limits: resource limits indexed by resource,
 * @chroot: root directory of process (implies @chdir if not set),
 * @chdir: working directory of process,
 * @instances: instances of this job,
 * @deleted: whether job should be deleted when finished.
 *
 * This structure holds the configuration of a known task or service that
 * should be tracked by the init daemon; as tasks and services are
 * fundamentally identical except for when they "finish", they are both
 * collated together and only differ in the value of @service.
 **/
typedef struct job_config JobConfig;
struct job_config {
	NihList         entry;

	char           *name;
	char           *description;
	char           *author;
	char           *version;

	EventOperator  *start_on;
	EventOperator  *stop_on;
	NihList         emits;

	JobProcess    **process;

	JobWaitType     wait_for;

	int            *normalexit;
	size_t          normalexit_len;

	time_t          kill_timeout;

	int             instance;
	int             service;
	int             respawn;
	int             respawn_limit;
	time_t          respawn_interval;

	int             daemon;
	char           *pid_file;
	char           *pid_binary;
	time_t          pid_timeout;

	ConsoleType     console;
	char          **env;

	mode_t          umask;
	int             nice;
	struct rlimit  *limits[RLIMIT_NLIMITS];
	char           *chroot;
	char           *chdir;

	NihList         instances;
	int             deleted;
};

/**
 * Job:
 * @entry: list header,
 * @id: unique job id,
 * @config: pointer to JobConfig structure,
 * @start_on: event operator expression that started this job,
 * @stop_on: event operator expression that can stop this job.
 * @goal: whether the job is to be stopped or started,
 * @state: actual state of the job,
 * @pid: current process ids,
 * @blocked: emitted event we're waiting to finish,
 * @failed: whether the last process ran failed,
 * @failed_process: the last process that failed,
 * @exit_status: exit status of the last failed process,
 * @kill_timer: timer to kill process,
 * @respawn_time: time service was first respawned,
 * @respawn_count: number of respawns since @respawn_time,
 * @pid_timer: timer for pid location,
 *
 * This structure represents a known task or service that should be tracked
 * by the init daemon; as tasks and services are fundamentally identical,
 * except for the handling when the main process terminates, they are both
 * collated together in this structure and only differ in the value of the
 * @service member.
 **/
typedef struct job {
	NihList         entry;

	unsigned int    id;
	JobConfig      *config;

	EventOperator  *start_on;
	EventOperator  *stop_on;

	JobGoal         goal;
	JobState        state;

	pid_t          *pid;

	Event          *blocked;

	int             failed;
	ProcessType     failed_process;
	int             exit_status;

	NihTimer       *kill_timer;

	time_t          respawn_time;
	int             respawn_count;

	NihTimer       *pid_timer;

} Job;


NIH_BEGIN_EXTERN

unsigned int  job_id;
int           job_id_wrapped;
NihHash      *jobs;
unsigned int  job_instances;


void        job_init                  (void);

JobProcess *job_process_new           (const void *parent)
	__attribute__ ((warn_unused_result, malloc));

JobConfig * job_config_new            (const void *parent, const char *name)
	__attribute__ ((warn_unused_result, malloc));

JobConfig * job_config_replace        (JobConfig *job_config);

Job *       job_new                   (JobConfig *job_config)
	__attribute__ ((warn_unused_result, malloc));

Job *       job_find_by_pid           (pid_t pid, ProcessType *process);
Job *       job_find_by_id            (unsigned int id);

Job *       job_instance              (JobConfig *job_config);

void        job_change_goal           (Job *job, JobGoal goal);

void        job_change_state          (Job *job, JobState state);
JobState    job_next_state            (Job *job);

void        job_run_process           (Job *job, ProcessType process);
void        job_kill_process          (Job *job, ProcessType process);

void        job_child_handler         (void *ptr, pid_t pid,
				       NihChildEvents event, int status);

void        job_handle_event          (Event *event);
void        job_handle_event_finished (Event *event);

NIH_END_EXTERN

#endif /* INIT_JOB_H */
