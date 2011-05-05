/* upstart
 *
 * Copyright © 2010 Canonical Ltd.
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

#ifndef INIT_JOB_CLASS_H
#define INIT_JOB_CLASS_H

#include <dbus/dbus.h>

#include <sys/types.h>
#include <sys/resource.h>

#include <time.h>

#include <nih/macros.h>
#include <nih/list.h>
#include <nih/hash.h>

#include <nih-dbus/dbus_message.h>

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
 * @instances: hash table of active instances,
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
 * @console: how to arrange processes' stdin/out/err file descriptors,
 * @umask: file mode creation mask,
 * @nice: process priority,
 * @oom_score_adj: OOM killer score adjustment,
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
	NihHash        *instances;

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

	ConsoleType     console;

	mode_t          umask;
	int             nice;
	int             oom_score_adj;
	struct rlimit  *limits[RLIMIT_NLIMITS];
	char           *chroot;
	char           *chdir;

	int             deleted;
	int             debug;
} JobClass;

#define SCORE_TO_ADJ(x) ((x * ((x < 0) ? 17 : 15)) / 1000)
#define ADJ_TO_SCORE(x) ((x * 1000) / ((x < 0) ? 17 : 15))

NIH_BEGIN_EXTERN

extern NihHash *job_classes;


void        job_class_init                 (void);

JobClass  * job_class_new                  (const void *parent,
					    const char *name)
	__attribute__ ((warn_unused_result, malloc));

int         job_class_consider             (JobClass *class);
int         job_class_reconsider           (JobClass *class);

void        job_class_register             (JobClass *class,
					    DBusConnection *conn, int signal);
void        job_class_unregister           (JobClass *class,
					    DBusConnection *conn);

char      **job_class_environment          (const void *parent,
					    JobClass *class, size_t *len)
	__attribute__ ((warn_unused_result, malloc));


int         job_class_get_instance         (JobClass *class,
					    NihDBusMessage *message,
					    char * const *env,
					    char **instance)
	__attribute__ ((warn_unused_result));
int         job_class_get_instance_by_name (JobClass *class,
					    NihDBusMessage *message,
					    const char *name,
					    char **instance)
	__attribute__ ((warn_unused_result));
int         job_class_get_all_instances    (JobClass *class,
					    NihDBusMessage *message,
					    char ***instances)
	__attribute__ ((warn_unused_result));

int         job_class_start                (JobClass *class,
					    NihDBusMessage *message,
					    char * const *env, int wait)
	__attribute__ ((warn_unused_result));
int         job_class_stop                 (JobClass *class,
					    NihDBusMessage *message,
					    char * const *env, int wait)
	__attribute__ ((warn_unused_result));
int         job_class_restart              (JobClass *class,
					    NihDBusMessage *message,
					    char * const *env, int wait)
	__attribute__ ((warn_unused_result));

int         job_class_get_name             (JobClass *class,
					    NihDBusMessage *message,
					    char **name)
	__attribute__ ((warn_unused_result));
int         job_class_get_description      (JobClass *class,
					    NihDBusMessage *message,
					    char **description)
	__attribute__ ((warn_unused_result));
int         job_class_get_author           (JobClass *class,
					    NihDBusMessage *message,
					    char **author)
	__attribute__ ((warn_unused_result));
int         job_class_get_version          (JobClass *class,
					    NihDBusMessage *message,
					    char **version)
	__attribute__ ((warn_unused_result));

int         job_class_get_start_on         (JobClass *class,
					    NihDBusMessage *message,
					    char ****start_on);
int         job_class_get_stop_on          (JobClass *class,
					    NihDBusMessage *message,
					    char ****stop_on);
int         job_class_get_emits	           (JobClass *class,
					    NihDBusMessage *message,
					    char ***emits);

NIH_END_EXTERN

#endif /* INIT_JOB_CLASS_H */
