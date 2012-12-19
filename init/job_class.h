/* upstart
 *
 * Copyright © 2011 Canonical Ltd.
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
#include "session.h"


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
 * and error file descriptors arranged.  The options are:
 * - CONSOLE_NONE: to have these all mapped to /dev/null,
 * - CONSOLE_OUTPUT: the console device (non-owning process),
 * - CONSOLE_OWNER: the console device (owning process),
 * - CONSOLE_LOG: stdin is mapped to /dev/null and standard output and error
 *   are redirected to the built-in logger (this is the default).
 **/
typedef enum console_type {
	CONSOLE_NONE,
	CONSOLE_OUTPUT,
	CONSOLE_OWNER,
	CONSOLE_LOG
} ConsoleType;


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
 * JOB_NICE_INVALID:
 *
 * The nice level for processes when no nice level is set.
 **/
#define JOB_NICE_INVALID -21

/**
 * JOB_DEFAULT_OOM_SCORE_ADJ:
 *
 * The default OOM score adjustment for processes.
 **/
#define JOB_DEFAULT_OOM_SCORE_ADJ 0

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
 * JobClass:
 * @entry: list header,
 * @name: unique name,
 * @path: path of D-Bus object,
 * @session: attached session,
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
 * @kill_signal: first signal to send (usually SIGTERM),
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
 * @setuid: user name to drop to before starting process,
 * @setgid: group name to drop to before starting process,
 * @deleted: whether job should be deleted when finished.
 * @usage: usage text - how to control job
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
	Session *       session;

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
	int             kill_signal;

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
	char           *setuid;
	char           *setgid;

	int             deleted;
	int             debug;

	char           *usage;
} JobClass;


NIH_BEGIN_EXTERN

extern NihHash  *job_classes;
extern char    **job_environ;


void        job_class_init                 (void);

void        job_class_environment_init     (void);

JobClass  * job_class_new                  (const void *parent,
					    const char *name,
					    Session *session)
	__attribute__ ((warn_unused_result, malloc));

int         job_class_consider             (JobClass *class);
int         job_class_reconsider           (JobClass *class);

void        job_class_add_safe             (JobClass *class);

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
int         job_class_get_usage	           (JobClass *class,
					    NihDBusMessage *message,
					    char **usage);

const char *
job_class_console_type_enum_to_str (ConsoleType console)
	__attribute__ ((warn_unused_result));

ConsoleType
job_class_console_type_str_to_enum (const char *name)
	__attribute__ ((warn_unused_result));

const char *
job_class_expect_type_enum_to_str (ExpectType expect)
	__attribute__ ((warn_unused_result));

ExpectType
job_class_expect_type_str_to_enum (const char *name)
	__attribute__ ((warn_unused_result));

ConsoleType job_class_console_type         (const char *console)
	__attribute__ ((warn_unused_result));

json_object *job_class_serialise (const JobClass *class)
	__attribute__ ((warn_unused_result, malloc));

JobClass *job_class_deserialise (json_object *json)
	__attribute__ ((malloc, warn_unused_result));

json_object * job_class_serialise_all (void)
	__attribute__ ((warn_unused_result, malloc));

int job_class_deserialise_all (json_object *json)
	__attribute__ ((warn_unused_result));

JobClass * job_class_get (const char *name, Session *session)
	__attribute__ ((warn_unused_result));

void job_class_prepare_reexec (void);

NIH_END_EXTERN

#endif /* INIT_JOB_CLASS_H */
