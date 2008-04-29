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

#ifndef INIT_JOB_CLASS_H
#define INIT_JOB_CLASS_H

#include <sys/types.h>
#include <sys/resource.h>

#include <time.h>

#include <nih/macros.h>
#include <nih/list.h>
#include <nih/hash.h>

#include "process.h"
#include "event_operator.h"


/**
 * ExpectType:
 *
 * This is used to determine what to expect to happen before moving the job
 * from the spawned state.  EXPECT_NONE means that we don't expect anything
 * so the job will move directly out of the spawned state without waiting.
 **/
typedef enum expect_type {
	EXPECT_NONE,
	EXPECT_STOP,
	EXPECT_DAEMON,
	EXPECT_FORK
} ExpectType;

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
 * JobClass:
 * @entry: list header,
 * @name: unique name,
 * @path: path of D-Bus object,
 * @instance: pattern to uniquely identify multiple instances,
 * @instances: active instances,
 * @description: description; intended for humans,
 * @author: author; intended for humans,
 * @version: version; intended for humans,
 * @env: NULL-terminated array of default environment variables,
 * @export: NULL-terminated array of environment exported to events,
 * @start_on: event operator expression that can start an instance,
 * @stop_on: event operator expression that stops instances,
 * @emits: NULL-terminated array of events that may be emitted by instances,
 * @process: processes to be run,
 * @expect: what to expect before entering the next state after spawned,
 * @task: start requests are not unblocked until instances have finished,
 * @kill_timeout: time to wait between sending TERM and KILL signals,
 * @respawn: instances should be restarted if main process fails,
 * @respawn_limit: number of respawns in @respawn_interval that we permit,
 * @respawn_interval: barrier for @respawn_limit,
 * @normalexit: array of exit codes that prevent a respawn,
 * @normalexit_len: length of @normalexit array,
 * @leader: TRUE if processes should be session leaders,
 * @console: how to arrange processes' stdin/out/err file descriptors,
 * @umask: file mode creation mask,
 * @nice: process priority,
 * @oom_adj: OOM killer adjustment,
 * @limits: resource limits indexed by resource,
 * @chroot: root directory of process (implies @chdir if not set),
 * @chdir: working directory of process,
 * @deleted: whether job should be deleted when finished.
 *
 * This structure holds the configuration of a known task or service that
 * should be tracked by the init daemon; as tasks and services are
 * fundamentally identical except for when they "finish", they are both
 * collated together and only differ in the value of @task.
 **/
typedef struct job_class {
	NihList         entry;

	char           *name;
	char           *path;

	char           *instance;
	NihList         instances;

	char           *description;
	char           *author;
	char           *version;

	char          **env;
	char          **export;

	EventOperator  *start_on;
	EventOperator  *stop_on;
	char          **emits;

	Process       **process;
	ExpectType      expect;
	int             task;

	time_t          kill_timeout;

	int             respawn;
	int             respawn_limit;
	time_t          respawn_interval;

	int            *normalexit;
	size_t          normalexit_len;

	int             leader;
	ConsoleType     console;

	mode_t          umask;
	int             nice;
	int             oom_adj;
	struct rlimit  *limits[RLIMIT_NLIMITS];
	char           *chroot;
	char           *chdir;

	int             deleted;
} JobClass;


NIH_BEGIN_EXTERN

NihHash *job_classes;


void        job_class_init        (void);

JobClass  * job_class_new         (const void *parent, const char *name)
	__attribute__ ((warn_unused_result, malloc));

int         job_class_consider    (JobClass *class);
int         job_class_reconsider  (JobClass *class);

char      **job_class_environment (const void *parent, JobClass *class,
				   size_t *len)
	__attribute__ ((warn_unused_result, malloc));

NIH_END_EXTERN

#endif /* INIT_JOB_CLASS_H */
