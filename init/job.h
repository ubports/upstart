/* upstart
 *
 * Copyright © 2008 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
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

#include "event.h"


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
 * JOB_DEFAULT_ENVIRONMENT:
 *
 * Environment variables to always copy from our own environment, these
 * can be overriden in the job definition or by events since they have the
 * lowest priority.
 **/
#define JOB_DEFAULT_ENVIRONMENT \
	"PATH",			\
	"TERM"


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
 * JobExpect:
 *
 * This is used to determine whether to leave the job in the spawned state
 * until a particular event occurs.
 **/
typedef enum job_expect {
	JOB_EXPECT_NONE,
	JOB_EXPECT_STOP,
	JOB_EXPECT_DAEMON,
	JOB_EXPECT_FORK
} JobExpect;

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
 * @emits: array of additional events that this job can emit,
 * @process: processes to be run,
 * @expect: what to expect before entering the next state after spawned,
 * @kill_timeout: time to wait between sending TERM and KILL signals,
 * @task: job does not reach its goal until stopped again,
 * @instance: job may have multiple instances,
 * @instance_name: pattern to identify instances,
 * @respawn: process should be restarted if it fails,
 * @respawn_limit: number of respawns in @respawn_interval that we permit,
 * @respawn_interval: barrier for @respawn_limit,
 * @normalexit: array of exit codes that prevent a respawn,
 * @normalexit_len: length of @normalexit array,
 * @leader: TRUE if job should be a session leader,
 * @console: how to arrange the job's stdin/out/err file descriptors,
 * @env: NULL-terminated array of default environment variables,
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
 * collated together and only differ in the value of @task.
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

	JobExpect       expect;

	time_t          kill_timeout;

	int             task;

	int             instance;
	char           *instance_name;

	int             respawn;
	int             respawn_limit;
	time_t          respawn_interval;

	int            *normalexit;
	size_t          normalexit_len;

	int             leader;
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
 * @config: pointer to JobConfig structure,
 * @name: unique instance name,
 * @stop_on: event operator expression that can stop this job.
 * @goal: whether the job is to be stopped or started,
 * @state: actual state of the job,
 * @pid: current process ids,
 * @env: NULL-terminated list of environment variables,
 * @start_env: environment to use next time the job is started,
 * @stop_env: environment to add for the next pre-stop script,
 * @blocked: emitted event we're waiting to finish,
 * @blocking: list of events we're blocking from finishing,
 * @failed: whether the last process ran failed,
 * @failed_process: the last process that failed,
 * @exit_status: exit status of the last failed process,
 * @kill_timer: timer to kill process,
 * @respawn_time: time job was first respawned,
 * @respawn_count: number of respawns since @respawn_time,
 * @trace_forks: number of forks traced,
 * @trace_state: state of trace.
 *
 * This structure represents a known task or service that should be tracked
 * by the init daemon; as tasks and services are fundamentally identical,
 * except for the handling when the main process terminates, they are both
 * collated together in this structure and only differ in the value of the
 * @task member.
 **/
typedef struct job {
	NihList         entry;

	JobConfig      *config;
	char           *name;

	EventOperator  *stop_on;

	JobGoal         goal;
	JobState        state;

	pid_t          *pid;

	char          **env;
	char          **start_env;
	char          **stop_env;

	Event          *blocked;
	NihList        *blocking;

	int             failed;
	ProcessType     failed_process;
	int             exit_status;

	NihTimer       *kill_timer;

	time_t          respawn_time;
	int             respawn_count;

	int             trace_forks;
	TraceState      trace_state;
} Job;


NIH_BEGIN_EXTERN

NihHash *jobs;


void        job_init                  (void);

const char * job_goal_name            (JobGoal goal)
	__attribute__ ((const));
JobGoal     job_goal_from_name        (const char *goal);

const char *job_state_name            (JobState state)
	__attribute__ ((const));
JobState    job_state_from_name       (const char *state);

const char *process_name              (ProcessType process)
	__attribute__ ((const));
ProcessType process_from_name         (const char *process);

JobProcess *job_process_new           (const void *parent)
	__attribute__ ((warn_unused_result, malloc));


JobConfig * job_config_new            (const void *parent, const char *name)
	__attribute__ ((warn_unused_result, malloc));

JobConfig * job_config_replace        (JobConfig *config);

char      **job_config_environment    (const void *parent, JobConfig *config,
				       size_t *len)
	__attribute__ ((warn_unused_result, malloc));


Job *       job_new                   (JobConfig *config, char *name)
	__attribute__ ((warn_unused_result, malloc));

Job *       job_find_by_pid           (pid_t pid, ProcessType *process);

Job *       job_instance              (JobConfig *config, const char *name);

void        job_change_goal           (Job *job, JobGoal goal);

void        job_change_state          (Job *job, JobState state);
JobState    job_next_state            (Job *job);

int         job_run_process           (Job *job, ProcessType process);
void        job_kill_process          (Job *job, ProcessType process);


void        job_child_handler         (void *ptr, pid_t pid,
				       NihChildEvents event, int status);

void        job_handle_event          (Event *event);
void        job_handle_event_finished (Event *event);

NIH_END_EXTERN

#endif /* INIT_JOB_H */
