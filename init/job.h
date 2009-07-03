/* upstart
 *
 * Copyright © 2009 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef INIT_JOB_H
#define INIT_JOB_H

#include <dbus/dbus.h>

#include <sys/types.h>

#include <time.h>

#include <nih/macros.h>
#include <nih/list.h>
#include <nih/timer.h>

#include <nih-dbus/dbus_message.h>

#include "job_class.h"
#include "event_operator.h"

#include "com.ubuntu.Upstart.Instance.h"


/**
 * JobGoal:
 *
 * There are two ultimate goals for any job, either it should be stopped
 * or it should be started.  In order to achieve these goals, we may need
 * to go through a number of different states (defined by JobState).
 **/
typedef enum job_goal {
	JOB_STOP,
	JOB_START,
	JOB_RESPAWN
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
 * Job:
 * @entry: list header,
 * @name: unique instance name,
 * @class: pointer to job class,
 * @path: D-Bus path of instance,
 * @goal: whether the job is to be stopped or started,
 * @state: actual state of the job,
 * @env: NULL-terminated list of environment variables,
 * @start_env: environment to use next time the job is started,
 * @stop_env: environment to add for the next pre-stop script,
 * @stop_on: event operator expression that can stop this job.
 * @pid: current process ids,
 * @blocker: emitted event we're waiting to finish,
 * @blocking: list of events we're blocking from finishing,
 * @kill_timer: timer to kill process,
 * @kill_process: process @kill_timer will kill,
 * @failed: whether the last process ran failed,
 * @failed_process: the last process that failed,
 * @exit_status: exit status of the last failed process,
 * @respawn_time: time job was first respawned,
 * @respawn_count: number of respawns since @respawn_time,
 * @trace_forks: number of forks traced,
 * @trace_state: state of trace.
 *
 * This structure holds the state of an active job instance being tracked
 * by the init daemon, the configuration details of the job are available
 * from the @class member.
 **/
typedef struct job {
	NihList         entry;

	char           *name;
	JobClass       *class;
	char           *path;

	JobGoal         goal;
	JobState        state;
	char          **env;

	char          **start_env;
	char          **stop_env;
	EventOperator  *stop_on;

	pid_t          *pid;
	Event          *blocker;
	NihList         blocking;

	NihTimer       *kill_timer;
	ProcessType     kill_process;

	int             failed;
	ProcessType     failed_process;
	int             exit_status;

	time_t          respawn_time;
	int             respawn_count;

	int             trace_forks;
	TraceState      trace_state;
} Job;


NIH_BEGIN_EXTERN

Job *       job_new             (JobClass *class, const char *name)
	__attribute__ ((warn_unused_result, malloc));
void        job_register        (Job *job, DBusConnection *conn, int signal);

void        job_change_goal     (Job *job, JobGoal goal);

void        job_change_state    (Job *job, JobState state);
JobState    job_next_state      (Job *job);

void        job_failed          (Job *job, ProcessType process, int status);
void        job_finished        (Job *job, int failed);

Event      *job_emit_event      (Job *job)
	__attribute__ ((malloc));


const char *job_name            (Job *job);

const char *job_goal_name       (JobGoal goal)
	__attribute__ ((const));
JobGoal     job_goal_from_name  (const char *goal);

const char *job_state_name      (JobState state)
	__attribute__ ((const));
JobState    job_state_from_name (const char *state);

int         job_start           (Job *job, NihDBusMessage *message)
	__attribute__ ((warn_unused_result));
int         job_stop            (Job *job, NihDBusMessage *message)
	__attribute__ ((warn_unused_result));
int         job_restart         (Job *job, NihDBusMessage *message)
	__attribute__ ((warn_unused_result));

int         job_get_name        (Job *job, NihDBusMessage *message,
				 char **name)
	__attribute__ ((warn_unused_result));
int         job_get_goal        (Job *job, NihDBusMessage *message,
				 char **goal)
	__attribute__ ((warn_unused_result));
int         job_get_state       (Job *job, NihDBusMessage *message,
				 char **state)
	__attribute__ ((warn_unused_result));

int         job_get_processes   (Job *job, NihDBusMessage *message,
				 JobProcessesElement ***processes)
	__attribute__ ((warn_unused_result));

NIH_END_EXTERN

#endif /* INIT_JOB_H */
