/* upstart
 *
 * test_state.c - test suite for init/state.c and other
 * associated serialisation and deserialisation routines.
 *
 * Copyright © 2012-2013 Canonical Ltd.
 * Author: James Hunt <james.hunt@canonical.com>
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

#include <unistd.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <pty.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <nih/test.h>
#include <nih/timer.h>
#include <nih/child.h>
#include <nih/signal.h>
#include <nih/main.h>
#include <nih/string.h>
#include <nih/logging.h>

#include "state.h"
#include "session.h"
#include "process.h"
#include "event.h"
#include "event_operator.h"
#include "environ.h"
#include "conf.h"
#include "job_class.h"
#include "job.h"
#include "log.h"
#include "blocked.h"
#include "control.h"
#include "test_util_common.h"
#include "test_util.h"

#ifdef ENABLE_CGROUPS
#include "cgroup.h"
#endif /* ENABLE_CGROUPS */

#ifndef TEST_DATA_DIR
#error ERROR: TEST_DATA_DIR not defined
#endif

/* These functions are 'protected'.
 *
 * The test code needs access, but they cannot be public due to
 * header-file complications.
 */
json_object *
state_serialise_blocked (const Blocked *blocked)
	__attribute__ ((warn_unused_result));

Blocked *
state_deserialise_blocked (void *parent, json_object *json, NihList *list)
	__attribute__ ((warn_unused_result));

/**
 * AlreadySeen:
 *
 * Used to allow objects that directly or indirectly reference
 * another to be inspected and compared without causing infinite
 * recursion.
 *
 * For example, an Event can reference a Job via its event->blocking list.
 * But the Job referenced by the Blocked object will have its job->blocker
 * set to the original event. If inspecting the original Event, we can
 * pass ALREADY_SEEN_EVENT such that we can detect that no further
 * operations are required at the point we consider job->blocker.
 *
 * If ALREADY_SEEN_SET is specified, the first function that understands
 * this type will _change_ the value to one of the other values based on
 * the action the function performs (for example, job_diff() changes
 * ALREADY_SEEN_SET to ALREADY_SEEN_JOB and conf_source_diff() changes
 * ALREADY_SEEN_SET to ALREADY_SEEN_SOURCE).
 **/
typedef enum {
	ALREADY_SEEN_SET,
	ALREADY_SEEN_EVENT,
	ALREADY_SEEN_SOURCE,
	ALREADY_SEEN_FILE,
	ALREADY_SEEN_BLOCKED,
	ALREADY_SEEN_JOB,
	ALREADY_SEEN_LAST
} AlreadySeen;

/**
 * Foo:
 *
 * Test structure containing simple, opaque and
 * known aggregate types.
 **/
typedef struct foo {
	int32_t         int32;
	int64_t         int64;
	char           *str;
	pid_t           pid;
	struct rlimit   limit;
	struct rlimit  *limits[RLIMIT_NLIMITS];
	char          **env;
	char          **array;
	Process       **process;
} Foo;

int session_diff (const Session *a, const Session *b)
	__attribute__ ((warn_unused_result));

int process_diff (const Process *a, const Process *b)
	__attribute__ ((warn_unused_result));

int event_diff (const Event *a, const Event *b, AlreadySeen seen)
	__attribute__ ((warn_unused_result));

int nih_timer_diff (const NihTimer *a, const NihTimer *b)
	__attribute__ ((warn_unused_result));

int log_diff (const Log *a, const Log *b)
	__attribute__ ((warn_unused_result));

int rlimit_diff (const struct rlimit *a, const struct rlimit *b)
	__attribute__ ((warn_unused_result));

int job_class_diff (const JobClass *a, const JobClass *b,
		    AlreadySeen seen, int check_jobs)
	__attribute__ ((warn_unused_result));

int job_diff (const Job *a, const Job *b,
	      AlreadySeen seen, int check_class)
	__attribute__ ((warn_unused_result));

int blocking_diff (const NihList *a, const NihList *b, AlreadySeen seen)
	__attribute__ ((warn_unused_result));

int blocked_diff (const Blocked *a, const Blocked *b, AlreadySeen seen)
	__attribute__ ((warn_unused_result));

void test_upstart1_6_upgrade (const char *path);
void test_session_upgrade_midflight (const char *path);
void test_session_upgrade_exists (const char *original_path);
void test_session_upgrade_stale (const char *path);
void test_upstart1_8_upgrade (const char *path);
void test_upstart_pre_security_upgrade (const char *path);
void test_upstart_with_apparmor_upgrade (const char *path);
void test_upstart_full_serialise_without_apparmor_upgrade (const char *path);
void test_upstart_full_serialise_with_apparmor_upgrade (const char *path);
void test_reload_signal_state (const char *path);
void test_job_environ_upgrade (const char *path);

ConfSource * conf_source_from_path (const char *path,
				    ConfSourceType type,
				    const Session *session)
	__attribute__ ((warn_unused_result));

int conf_source_diff (const ConfSource *a, const ConfSource *b, AlreadySeen seen)
	__attribute__ ((warn_unused_result));

int conf_file_diff (const ConfFile *a, const ConfFile *b, AlreadySeen seen)
	__attribute__ ((warn_unused_result));

int cgroup_path_diff (const CGroupPath *a, const CGroupPath *b)
	__attribute__ ((warn_unused_result));

/**
 * TestDataFile:
 *
 * @filename: basename of data file,
 * @func: function to run to test @filename.
 *
 * Representation of a JSON data file used for ensuring that the current
 * version of Upstart is able to deserialise all previous JSON data file
 * format versions.
 *
 * @func returns nothing so is expected to assert on any error.
 **/
typedef struct test_data_file {
	char    *filename;
	void   (*func) (const char *path);
} TestDataFile;

/**
 * test_data_files:
 *
 * Array of data files to test.
 **/
TestDataFile test_data_files[] = {
	{ "upstart-1.6.json", test_upstart1_6_upgrade },
	{ "upstart-1.8.json", test_upstart1_8_upgrade },
	{ "upstart-pre-security.json", test_upstart_pre_security_upgrade },
	{ "upstart-1.8+apparmor.json", test_upstart_with_apparmor_upgrade },
	{ "upstart-1.8+full_serialisation-apparmor.json", test_upstart_full_serialise_without_apparmor_upgrade },
	{ "upstart-1.8+full_serialisation+apparmor.json", test_upstart_full_serialise_with_apparmor_upgrade },
	{ "upstart-session.json", test_session_upgrade_midflight },
	{ "upstart-session2.json", test_session_upgrade_exists },
	{ "upstart-session-infinity.json", test_session_upgrade_stale },
	{ "upstart-1.9.json", test_reload_signal_state },
	{ "upstart-1.11.json", test_job_environ_upgrade },

	{ NULL, NULL }
};

/* Data with some embedded nulls */
const    char test_data[] = {
	'h', 'e', 'l', 'l', 'o', 0x0, 0x0, 0x0, ' ', 'w', 'o', 'r', 'l', 'd', '\n', '\r', '\0'
};
char    *strings[] = {
	"", NULL, "a", "123", "FOO=BAR", "hello\n\t\aworld",
	"foo bar", "\\\a\b\f\n\r\t\v", "\"'$*&()[]{}-_=+/?@':;>.<,~#"
};
int32_t  values32[] = {INT32_MIN, -1, 0, 1, INT32_MAX};
int64_t  values64[] = {INT64_MIN, -1, 0, 1, INT64_MAX};
const Process test_procs[] = {
	{ 0, "echo hello" },
	{ 1, "echo hello" }
};
rlim_t rlimit_values[] = { 0, 1, 2, 3, 7, RLIM_INFINITY };

/**
 * session_diff:
 * @a: first Session,
 * @b: second Session.
 *
 * Compare two Session objects for equivalence.
 *
 * Returns: 0 if @a and @b are identical (may be NULL),
 * else 1.
 **/
int
session_diff (const Session *a, const Session *b)
{
	if ((a == b) && !a)
		return 0;

	if (!a || !b)
		goto fail;

	if (obj_string_check (a, b, chroot))
		goto fail;

	if (obj_string_check (a, b, conf_path))
		goto fail;

	return 0;

fail:
	return 1;
}

/**
 * process_diff:
 * @a: first Process,
 * @b: second Process.
 *
 * Compare two Process objects for equivalence.
 *
 * Returns: 0 if @a and @b are identical, else 1.
 **/
int
process_diff (const Process *a, const Process *b)
{
	if ((a == b) && !a)
		return 0;

	if (!a || !b)
		goto fail;

	if (obj_num_check (a, b, script))
		goto fail;

	if (obj_string_check (a, b, command))
		goto fail;

	return 0;

fail:
	return 1;
}

/**
 * event_diff:
 * @a: first Event,
 * @b: second Event,
 * @seen: object type that has already been seen.
 *
 * Compare two Event objects for equivalence.
 *
 * Returns: 0 if @a and @b are identical, else 1.
 **/
int
event_diff (const Event *a, const Event *b, AlreadySeen seen)
{
	nih_local char *env_a = NULL;
	nih_local char *env_b = NULL;

	if (seen == ALREADY_SEEN_EVENT)
		return 0;

	if (seen == ALREADY_SEEN_SET)
		seen = ALREADY_SEEN_EVENT;

	if ((a == b) && !a)
		return 0;

	if (!a || !b)
		goto fail;

	if (session_diff (a->session, b->session))
		goto fail;

	if (obj_string_check (a, b, name))
		goto fail;

	env_a = state_collapse_env ((const char **)a->env);
	env_b = state_collapse_env ((const char **)b->env);

	if (string_check (env_a, env_b))
		goto fail;

	if (obj_num_check (a, b, fd))
		goto fail;

	if (obj_num_check (a, b, progress))
		goto fail;

	if (obj_num_check (a, b, failed))
		goto fail;

	if (obj_num_check (a, b, blockers))
		goto fail;

	if (blocking_diff (&a->blocking, &b->blocking, seen))
		goto fail;

	return 0;

fail:
	return 1;
}

/**
 * nih_timer_diff:
 * @a: first NihTimer,
 * @b: second NihTimer.
 *
 * Compare two NihTimer objects for equivalence.
 *
 * Returns: 0 if @a and @b are identical, else 1.
 **/
int
nih_timer_diff (const NihTimer *a, const NihTimer *b)
{
	if ((a == b) && !a)
		return 0;

	if (!a || !b)
		goto fail;

	if (obj_num_check (a, b, timeout))
		goto fail;

	if (obj_num_check (a, b, due))
		goto fail;

	return 0;

fail:
	return 1;
}

/**
 * log_diff:
 * @a: first Log,
 * @b: second Log.
 *
 * Compare two Log objects for equivalence.
 *
 * Returns: 0 if @a and @b are identical, else 1.
 **/
int
log_diff (const Log *a, const Log *b)
{
	if (!a || !b)
		goto fail;

	if (obj_num_check (a, b, fd))
		goto fail;

	if (obj_string_check (a, b, path))
		goto fail;


	if (a->io && b->io) {
		if (a->io->watch && b->io->watch) {
			if (obj_num_check ((a->io->watch), (b->io->watch), fd))
				goto fail;
		} else if (a->io->watch || b->io->watch)
			goto fail;
	} else if (a->io || b->io)
		goto fail;

	if (a->unflushed && b->unflushed) {
		if (obj_num_check (a->unflushed, b->unflushed, len))
			goto fail;

		if (obj_string_check (a->unflushed, b->unflushed, buf))
			goto fail;
	} else if (a->unflushed || b->unflushed)
		goto fail;

	if (obj_num_check (a, b, uid))
		goto fail;

	if (obj_num_check (a, b, detached))
		goto fail;

	if (obj_num_check (a, b, remote_closed))
		goto fail;

	if (obj_num_check (a, b, open_errno))
		goto fail;

	return 0;

fail:
	return 1;
}

/**
 * rlimit_diff:
 * @a: first rlimit,
 * @b: second rlimit.
 *
 * Compare two rlimit structs for equivalence.
 *
 * Returns: 0 if @a and @b are identical (may be NULL),
 * else 1.
 **/
int
rlimit_diff (const struct rlimit *a, const struct rlimit *b)
{
	if ((a == b) && !a)
		return 0;

	if (!a || !b)
		goto fail;

	if (obj_num_check (a, b, rlim_cur))
		goto fail;

	if (obj_num_check (a, b, rlim_max))
		goto fail;

	return 0;

fail:
	return 1;
}

/**
 * job_class_diff:
 * @a: first Job,
 * @b: second Job,
 * @seen: object type that has already been seen,
 * @check_jobs: if TRUE, consider job instances.
 *
 * Compare two JobClass objects for equivalence.
 *
 * Returns: 0 if @a and @b are identical, else 1.
 **/
int
job_class_diff (const JobClass *a, const JobClass *b,
		AlreadySeen seen, int check_jobs)
{
	size_t          i;
	nih_local char *env_a = NULL;
	nih_local char *env_b = NULL;
	nih_local char *export_a = NULL;
	nih_local char *export_b = NULL;
	nih_local char *condition_a = NULL;
	nih_local char *condition_b = NULL;
	nih_local char *emits_a = NULL;
	nih_local char *emits_b = NULL;

	if ((a == b) && !a)
		return 0;

	if (!a || !b)
		goto fail;

	if (obj_string_check (a, b, name))
		goto fail;

	if (obj_string_check (a, b, path))
		goto fail;

	if (session_diff (a->session, b->session))
		goto fail;

	if (obj_string_check (a, b, instance))
		goto fail;

	if (obj_num_check (a->instances, b->instances, size))
		goto fail;

	if (check_jobs) {
		TEST_TWO_HASHES_FOREACH (a->instances, b->instances, iter1, iter2) {
			Job *job1 = (Job *)iter1;
			Job *job2 = (Job *)iter2;

			if (job_diff (job1, job2, seen, FALSE))
				goto fail;
		}
	}

	if (obj_string_check (a, b, description))
		goto fail;

	if (obj_string_check (a, b, author))
		goto fail;

	if (obj_string_check (a, b, version))
		goto fail;

	env_a = state_collapse_env ((const char **)a->env);
	env_b = state_collapse_env ((const char **)b->env);

	if (string_check (env_a, env_b))
		goto fail;

	export_a = state_collapse_env ((const char **)a->export);
	export_b = state_collapse_env ((const char **)b->export);

	if (string_check (export_a, export_b))
		goto fail;

	if (event_operator_diff (a->start_on, b->start_on))
		goto fail;

	/* Check string values too for complete overkill :) */
	if (a->start_on)
		condition_a = event_operator_collapse (a->start_on);

	if (b->start_on)
		condition_b = event_operator_collapse (b->start_on);

	if (string_check (condition_a, condition_b))
		goto fail;

	if (a->stop_on)
		condition_a = event_operator_collapse (a->stop_on);

	if (b->stop_on)
		condition_b = event_operator_collapse (b->stop_on);

	if (string_check (condition_a, condition_b))
		goto fail;

	emits_a = state_collapse_env ((const char **)a->emits);
	emits_b = state_collapse_env ((const char **)b->emits);

	if (string_check (emits_a, emits_b))
		goto fail;

	for (i = 0; i < PROCESS_LAST; i++) {
		if (a->process[i] && b->process[i])
			assert0 (process_diff (a->process[i], b->process[i]));
		else if (a->process[i] || b->process[i])
			goto fail;
	}

	if (obj_num_check (a, b, expect))
		goto fail;

	if (obj_num_check (a, b, task))
		goto fail;

	if (obj_num_check (a, b, kill_timeout))
		goto fail;

	if (obj_num_check (a, b, kill_signal))
		goto fail;

	if (obj_num_check (a, b, reload_signal))
		goto fail;

	if (obj_num_check (a, b, respawn))
		goto fail;

	if (obj_num_check (a, b, respawn_limit))
		goto fail;

	if (obj_num_check (a, b, respawn_interval))
		goto fail;

	if (obj_num_check (a, b, normalexit_len))
		goto fail;

	if (a->normalexit_len) {
		for (i = 0; i < a->normalexit_len; i++) {
			if (a->normalexit[i] != b->normalexit[i])
				goto fail;
		}
	}

	if (obj_num_check (a, b, console))
		goto fail;

	if (obj_num_check (a, b, umask))
		goto fail;

	if (obj_num_check (a, b, nice))
		goto fail;

	if (obj_num_check (a, b, oom_score_adj))
		goto fail;

	for (i = 0; i < RLIMIT_NLIMITS; i++) {
		if (! a->limits[i] && ! b->limits[i])
			continue;
		if (rlimit_diff (a->limits[i], b->limits[i]))
			goto fail;
	}

	if (obj_string_check (a, b, chroot))
		goto fail;

	if (obj_string_check (a, b, chdir))
		goto fail;

	if (obj_string_check (a, b, setuid))
		goto fail;

	if (obj_string_check (a, b, setgid))
		goto fail;

	if (obj_num_check (a, b, deleted))
		goto fail;

	if (obj_num_check (a, b, debug))
		goto fail;

	if (obj_string_check (a, b, usage))
		goto fail;

	if (obj_string_check (a, b, apparmor_switch))
		goto fail;

	return 0;

fail:
	return 1;
}

/**
 * job_diff:
 * @a: first Job,
 * @b: second Job,
 * @seen: object type that has already been seen,
 * @check_class: if TRUE, consider parent_class.
 *
 * Compare two Job objects for equivalence.
 *
 * Returns: 0 if @a and @b are identical, else 1.
 **/
int
job_diff (const Job *a, const Job *b, AlreadySeen seen, int check_class)
{
	size_t          i;
	nih_local char *env_a = NULL;
	nih_local char *env_b = NULL;

	nih_local char *condition_a = NULL;
	nih_local char *condition_b = NULL;

	if (seen == ALREADY_SEEN_JOB)
		return 0;
	if (seen == ALREADY_SEEN_SET)
		seen = ALREADY_SEEN_JOB;

	if ((a == b) && !a)
		return 0;

	if (!a || !b)
		goto fail;

	if (obj_string_check (a, b, name))
		goto fail;

	if (check_class) {
		if (job_class_diff (a->class, b->class, seen, FALSE))
			goto fail;
	}

	if (obj_string_check (a, b, path))
		goto fail;

	if (obj_num_check (a, b, goal))
		goto fail;

	if (obj_num_check (a, b, state))
		goto fail;

	env_a = state_collapse_env ((const char **)a->env);
	env_b = state_collapse_env ((const char **)b->env);

	if (string_check (env_a, env_b))
		goto fail;

	env_a = state_collapse_env ((const char **)a->start_env);
	env_b = state_collapse_env ((const char **)b->start_env);

	if (string_check (env_a, env_b))
		goto fail;

	env_a = state_collapse_env ((const char **)a->stop_env);
	env_b = state_collapse_env ((const char **)b->stop_env);

	if (string_check (env_a, env_b))
		goto fail;

	if (a->stop_on)
		condition_a = event_operator_collapse (a->stop_on);

	if (b->stop_on)
		condition_b = event_operator_collapse (b->stop_on);

	if (string_check (condition_a, condition_b))
		goto fail;

	if (obj_num_check (a, b, num_fds))
		goto fail;

	for (i = 0; i < a->num_fds; i++) {
		if (a->fds[i] != b->fds[i])
			goto fail;
	}

	for (i = 0; i < PROCESS_LAST; i++) {
		if (a->pid[i] != b->pid[i])
			goto fail;
	}

	assert0 (event_diff (a->blocker, b->blocker, TRUE));

	if (blocking_diff (&a->blocking, &b->blocking, seen))
		goto fail;

	if (nih_timer_diff (a->kill_timer, b->kill_timer))
		goto fail;

	if (obj_num_check (a, b, kill_process))
		goto fail;

	if (obj_num_check (a, b, failed))
		goto fail;

	if (obj_num_check (a, b, failed_process))
		goto fail;

	if (obj_num_check (a, b, exit_status))
		goto fail;

	if (obj_num_check (a, b, respawn_time))
		goto fail;

	if (obj_num_check (a, b, respawn_count))
		goto fail;

	if (obj_num_check (a, b, trace_forks))
		goto fail;

	if (obj_num_check (a, b, trace_state))
		goto fail;

	for (i = 0; i < PROCESS_LAST; i++) {
		if (! a->log[i] && ! b->log[i])
			continue;
		if (log_diff (a->log[i], b->log[i]))
			goto fail;
	}

	return 0;

fail:
	return 1;
}

/**
 * blocking_diff:
 * @a: first list of Blocked objects,
 * @b: second list of Blocked objects,
 * @seen: object type that has already been seen.
 *
 * Compare two lists of Blocked objects.
 *
 * Returns: 0 if @a and @b are identical, else 1.
 **/
int
blocking_diff (const NihList *a, const NihList *b, AlreadySeen seen)
{
	if ((a == b) && !a)
		return 0;

	if (!a || !b)
		goto fail;

	/* walk both lists together */
	TEST_TWO_LISTS_FOREACH (a, b, iter_a, iter_b) {
		Blocked *blocked_a = (Blocked *)iter_a;
		Blocked *blocked_b = (Blocked *)iter_b;

		if (blocked_diff (blocked_a, blocked_b, seen))
			goto fail;
	}

	return 0;

fail:
	return 1;
}

/**
 * blocked_diff:
 * @a: first Blocked,
 * @b: second Blocked,
 * @seen: object type that has already been seen.
 *
 * Compare two Blocked objects for equivalence.
 *
 * Returns: 0 if @a and @b are identical, else 1.
 **/
int
blocked_diff (const Blocked *a, const Blocked *b, AlreadySeen seen)
{
	const char  *enum_str_a;
	const char  *enum_str_b;
	int          ret = 0;

	if (seen == ALREADY_SEEN_BLOCKED)
		return 0;
	if (seen == ALREADY_SEEN_SET)
		seen = ALREADY_SEEN_BLOCKED;

	if ((a == b) && !a)
		return 0;

	if (!a || !b)
		goto fail;

	if (obj_num_check (a, b, type))
		goto fail;

	enum_str_a = blocked_type_enum_to_str (a->type);
	enum_str_b = blocked_type_enum_to_str (b->type);

	if (string_check (enum_str_a, enum_str_b))
		goto fail;

	switch (a->type) {
	case BLOCKED_JOB:
		ret = job_diff (a->job, b->job, seen, TRUE);
		break;

	case BLOCKED_EVENT:
		ret = event_diff (a->event, b->event, seen);
		break;

	default:
		/* FIXME: cannot handle D-Bus types yet */
		nih_assert_not_reached ();
	}

	return ret;

fail:
	return 1;
}

/**
 * conf_source_diff:
 * @a: first ConfSource,
 * @b: second ConfSource,
 * @seen: object type that has already been seen.
 *
 * Compare two ConfSource objects for equivalence.
 *
 * Returns: 0 if @a and @b are identical, else 1.
 **/
int
conf_source_diff (const ConfSource *a, const ConfSource *b, AlreadySeen seen)
{
	if (seen == ALREADY_SEEN_SOURCE)
		return 0;

	if (seen == ALREADY_SEEN_SET)
		seen = ALREADY_SEEN_SOURCE;

	if (! a && ! b)
		return 0;

	if ((! a && b) || (a && ! b))
		goto fail;

	if (session_diff (a->session, b->session))
		goto fail;

	if (obj_string_check (a, b, path))
		goto fail;

	if (obj_num_check (a, b, type))
		goto fail;

	if (obj_num_check (a, b, flag))
		goto fail;

	TEST_TWO_HASHES_FOREACH (a->files, b->files, iter1, iter2) {
		ConfFile *file1 = (ConfFile *)iter1;
		ConfFile *file2 = (ConfFile *)iter2;

		if (conf_file_diff (file1, file2, seen))
			goto fail;
	}

	return 0;

fail:
	return 1;
}

/**
 * conf_file_diff:
 * @a: first ConfFile,
 * @b: second ConfFile,
 * @seen: object type that has already been seen.
 *
 * Compare two ConfFile objects for equivalence.
 *
 * Returns: 0 if @a and @b are identical, else 1.
 **/
int
conf_file_diff (const ConfFile *a, const ConfFile *b, AlreadySeen seen)
{
	if (seen == ALREADY_SEEN_FILE)
		return 0;

	if (seen == ALREADY_SEEN_SET)
		seen = ALREADY_SEEN_FILE;

	if (! a && ! b)
		return 0;

	if ((! a && b) || (a && ! b))
		goto fail;

	if (obj_string_check (a, b, path))
		goto fail;

	if (conf_source_diff (a->source, b->source, seen))
		goto fail;

	if (obj_num_check (a, b, flag))
		goto fail;

	if (job_class_diff (a->job, b->job, seen, TRUE))
		goto fail;

	return 0;

fail:
	return 1;
}

/**
 * cgroup_path_diff:
 * @a: first CGroupPath,
 * @b: second CGroupPath.
 *
 * Compare two CGroupPath objects for equivalence.
 *
 * Returns: 0 if @a and @b are identical, else 1.
 **/
int
cgroup_path_diff (const CGroupPath *a, const CGroupPath *b)
{
	if ((a == b) && !a)
		return 0;

	if (!a || !b)
		fail;

	if (obj_string_check (a, b, path))
		goto fail;

	if (obj_num_check (a, b, blockers))
		goto fail;

	return 0;

fail:
	return 1;
}

void
test_session_serialise (void)
{
	json_object     *json;
	json_object     *json_sessions;
	Session         *session1;
	Session         *session2;
	Session         *new_session1;
	Session         *new_session2;
	int              ret;
	char             chroot_path[PATH_MAX];
	mode_t           old_perms;
	nih_local char  *path = NULL;

	session_init ();

	TEST_GROUP ("Session serialisation and deserialisation");

	/*******************************/
	TEST_FEATURE ("Session deserialisation");

	TEST_LIST_EMPTY (sessions);

	json = json_object_new_object ();
	TEST_NE_P (json, NULL);

	/* Create a couple of sessions */
	session1 = session_new (NULL, "/abc");
	TEST_NE_P (session1, NULL);
	session1->conf_path = NIH_MUST (nih_strdup (session1, "/def/ghi"));
	TEST_LIST_NOT_EMPTY (sessions);

	session2 = session_new (NULL, "/foo");
	TEST_NE_P (session2, NULL);
	session2->conf_path = NIH_MUST (nih_strdup (session2, "/bar/baz"));

	TEST_FEATURE ("Session serialisation");
	/* Convert them to JSON */
	json_sessions = session_serialise_all ();
	TEST_NE_P (json_sessions, NULL);

	json_object_object_add (json, "sessions", json_sessions);

	/* Remove the original sessions from the master list (but don't
	 * free them)
	 */
	nih_list_remove (&session1->entry);
	nih_list_remove (&session2->entry);

	TEST_LIST_EMPTY (sessions);

	/* Convert the JSON back into Session objects */
	ret = session_deserialise_all (json);
	assert0 (ret);

	/* free the JSON */
	json_object_put (json);

	TEST_LIST_NOT_EMPTY (sessions);

	/* Remove the newly-de-serialised Session objects from the
	 * master list.
	 */
	new_session1 = (Session *)nih_list_remove (sessions->next);
	TEST_NE_P (new_session1, NULL);

	new_session2 = (Session *)nih_list_remove (sessions->next);
	TEST_NE_P (new_session2, NULL);

	TEST_LIST_EMPTY (sessions);

	/* Compare original and new session objects for equivalence */
	assert0 (session_diff (session1, new_session1));
	assert0 (session_diff (session2, new_session2));

	/* Clean up */
	nih_free (session1);
	nih_free (session2);
	nih_free (new_session1);
	nih_free (new_session2);

	/*******************************/
	TEST_FEATURE ("Ensure session deserialisation does not create JobClasses");

	clean_env ();

	TEST_LIST_EMPTY (sessions);
	TEST_HASH_EMPTY (job_classes);
	TEST_LIST_EMPTY (conf_sources);

	old_perms = umask (0);

	TEST_FILENAME (chroot_path);
	assert0 (mkdir (chroot_path, 0755));

	path = NIH_MUST (nih_sprintf (NULL, "%s/etc", chroot_path));
	assert0 (mkdir (path, 0755));
	path = NIH_MUST (nih_sprintf (NULL, "%s/etc/init", chroot_path));
	assert0 (mkdir (path, 0755));

	CREATE_FILE (path, "foo.conf", "manual");

	session1 = session_new (NULL, chroot_path);
	TEST_NE_P (session1, NULL);
	session1->conf_path = NIH_MUST (nih_sprintf (session1, "%s//etc/init", chroot_path));
	TEST_LIST_NOT_EMPTY (sessions);

	TEST_HASH_EMPTY (job_classes);
	TEST_LIST_EMPTY (conf_sources);

	json = json_object_new_object ();
	TEST_NE_P (json, NULL);

	json_sessions = session_serialise_all ();
	TEST_NE_P (json_sessions, NULL);

	json_object_object_add (json, "sessions", json_sessions);

	/* Remove the session from the master list (but don't free it)
	 */
	nih_list_remove (&session1->entry);
	TEST_LIST_EMPTY (sessions);

	clean_env ();

	/* Convert the JSON back into Session objects */
	ret = session_deserialise_all (json);
	assert0 (ret);

	TEST_LIST_NOT_EMPTY (sessions);

	/* Ensure no ConfSources, ConfFiles or JobClasses were created
	 * as part of the session deserialisation.
	 */
	TEST_HASH_EMPTY (job_classes);
	TEST_LIST_EMPTY (conf_sources);

	session2 = (Session *)nih_list_remove (sessions->next);
	assert0 (session_diff (session1, session2));

	/* Clean up */

	/* free the JSON */
	json_object_put (json);

	DELETE_FILE (path, "foo.conf");

	path = NIH_MUST (nih_sprintf (NULL, "%s/etc/init", chroot_path));
	assert0 (rmdir (path));

	path = NIH_MUST (nih_sprintf (NULL, "%s/etc", chroot_path));
	assert0 (rmdir (path));

	assert0 (rmdir (chroot_path));

	/* Restore */
	umask (old_perms);
}

void
run_process_test (const Process *proc)
{
	json_object         *json;
	nih_local Process   *process = NULL;
	nih_local Process   *new_process = NULL;
	nih_local char      *feature = NULL;

	nih_assert (proc);

	process = process_new (NULL);
	TEST_NE_P (process, NULL);
	process->script = proc->script;
	process->command = NIH_MUST (nih_strdup (process, proc->command));

	feature = nih_sprintf (NULL, "Process serialisation with %sscript and %scommand",
			proc->script ? "" : "no ",
			proc->command ? "" : "no ");
	TEST_FEATURE (feature);

	json = process_serialise (process);
	TEST_NE_P (json, NULL);

	feature = nih_sprintf (NULL, "Process deserialisation with %sscript and %scommand",
			proc->script ? "" : "no ",
			proc->command ? "" : "no ");
	TEST_FEATURE (feature);

	new_process = process_deserialise (json, NULL);
	TEST_NE_P (new_process, NULL);

	/* Compare original and new objects */
	assert0 (process_diff (process, new_process));

	/* free the JSON */
	json_object_put (json);
}

void
test_process_serialise (void)
{
	int            i;
	int            num;
	nih_local Foo *foo = NULL;
	nih_local Foo *new_foo = NULL;
	json_object   *json;
	json_object   *json_processes;

	TEST_GROUP ("Process serialisation and deserialisation");

	/*******************************/
	TEST_FEATURE ("single Process serialisation and deserialisation");

	num = TEST_ARRAY_SIZE (test_procs);

	for (i = 0; i < num; i++)
		run_process_test (&test_procs[i]);

	/*******************************/
	TEST_FEATURE ("array of Processes serialisation and deserialisation");

	json = json_object_new_object ();
	TEST_NE_P (json, NULL);

	foo = nih_new (NULL, Foo);
	TEST_NE_P (foo, NULL);

	new_foo = nih_new (NULL, Foo);
	TEST_NE_P (new_foo, NULL);

	foo->process = nih_alloc (foo, sizeof (Process *) * PROCESS_LAST);
	TEST_NE_P (foo->process, NULL);

	new_foo->process = nih_alloc (new_foo, sizeof (Process *) * PROCESS_LAST);
	TEST_NE_P (new_foo->process, NULL);

	for (i = 0; i < PROCESS_LAST; i++) {
		foo->process[i] = NULL;
		new_foo->process[i] = NULL;
	}

	foo->process[PROCESS_MAIN] = process_new (foo->process);
	TEST_NE_P (foo->process[PROCESS_MAIN], NULL);

	foo->process[PROCESS_MAIN]->script = 1;
	foo->process[PROCESS_MAIN]->command = NIH_MUST (nih_strdup (foo->process[PROCESS_MAIN],
				"echo hello !£$%^&*()_+-={}:@~;'#<>?,./"));

	foo->process[PROCESS_SECURITY] = process_new (foo->process);
	TEST_NE_P (foo->process[PROCESS_SECURITY], NULL);
	foo->process[PROCESS_SECURITY]->script = 0;
	foo->process[PROCESS_SECURITY]->command = NIH_MUST (nih_strdup (foo->process[PROCESS_SECURITY],
			"/bin/true"));

	foo->process[PROCESS_PRE_START] = process_new (foo->process);
	TEST_NE_P (foo->process[PROCESS_PRE_START], NULL);
	foo->process[PROCESS_PRE_START]->script = 0;
	foo->process[PROCESS_PRE_START]->command = NIH_MUST (nih_strdup (foo->process[PROCESS_PRE_START],
			"/bin/echo \"\\\a\b''''''\f\n\r\t\v\""));

	foo->process[PROCESS_POST_STOP] = process_new (foo->process);
	TEST_NE_P (foo->process[PROCESS_POST_STOP], NULL);
	foo->process[PROCESS_POST_STOP]->script = 0;
	foo->process[PROCESS_POST_STOP]->command = NIH_MUST (nih_strdup (foo->process[PROCESS_POST_STOP],
			"/bin/true"));

	json_processes = process_serialise_all ((const Process const * const * const)foo->process);
	TEST_NE_P (json_processes, NULL);
	json_object_object_add (json, "process", json_processes);

	assert0 (process_deserialise_all (json, new_foo->process, new_foo->process));

	for (i = 0; i < PROCESS_LAST; i++)
		assert0 (process_diff (foo->process[i], new_foo->process[i]));

	json_object_put (json);

	/*******************************/
}

void
test_blocking (void)
{
	nih_local char          *json_string = NULL;
	nih_local char          *parent_str = NULL;
	ConfSource              *source = NULL;
	ConfSource              *new_source = NULL;
	ConfFile                *file;
	ConfFile                *new_file;
	JobClass                *class;
	JobClass                *new_class;
	Job                     *job;
	Job                     *new_job;
	Event                   *event;
	Event                   *new_event;
	Blocked                 *blocked;
	Blocked                 *new_blocked;
	NihList                  blocked_list;
	size_t                   len;
	json_object             *json_blocked;
	Session                 *session;
	Session                 *new_session;

	conf_init ();
	session_init ();
	event_init ();
	control_init ();
	job_class_init ();

	TEST_GROUP ("Blocked serialisation and deserialisation");

	/*******************************/
	TEST_FEATURE ("BLOCKED_JOB serialisation and deserialisation");

	nih_list_init (&blocked_list);
	TEST_LIST_EMPTY (&blocked_list);

	TEST_LIST_EMPTY (conf_sources);
	source = conf_source_new (NULL, "/tmp/foo", CONF_JOB_DIR);
	TEST_NE_P (source, NULL);
	TEST_LIST_NOT_EMPTY (conf_sources);

	file = conf_file_new (source, "/tmp/foo/bar.conf");
	TEST_NE_P (file, NULL);
	class = file->job = job_class_new (file, "bar", NULL);

	TEST_NE_P (class, NULL);
	TEST_HASH_EMPTY (job_classes);
	TEST_TRUE (job_class_consider (class));
	TEST_HASH_NOT_EMPTY (job_classes);

	job = job_new (class, "");
	TEST_NE_P (job, NULL);

	parent_str = nih_strdup (NULL, "parent");
	TEST_NE_P (parent_str, NULL);

	blocked = blocked_new (NULL, BLOCKED_JOB, job);
	TEST_NE_P (blocked, NULL);

	json_blocked = state_serialise_blocked (blocked);
	TEST_NE_P (json_blocked, NULL);

	new_blocked = state_deserialise_blocked (parent_str,
			json_blocked, &blocked_list);
	TEST_NE_P (new_blocked, NULL);
	TEST_LIST_NOT_EMPTY (&blocked_list);

	assert0 (blocked_diff (blocked, new_blocked, ALREADY_SEEN_SET));

	json_object_put (json_blocked);
	nih_free (source);

	/*******************************/
	TEST_FEATURE ("BLOCKED_EVENT serialisation and deserialisation");

	event = event_new (NULL, "event", NULL);
	TEST_NE_P (event, NULL);

	nih_list_init (&blocked_list);

	TEST_LIST_EMPTY (conf_sources);
	source = conf_source_new (NULL, "/tmp/foo", CONF_JOB_DIR);
	TEST_NE_P (source, NULL);
	TEST_LIST_NOT_EMPTY (conf_sources);

	file = conf_file_new (source, "/tmp/foo/bar.conf");
	TEST_NE_P (file, NULL);
	class = file->job = job_class_new (file, "bar", NULL);

	TEST_NE_P (class, NULL);
	TEST_HASH_EMPTY (job_classes);
	TEST_TRUE (job_class_consider (class));
	TEST_HASH_NOT_EMPTY (job_classes);

	job = job_new (class, "");
	TEST_NE_P (job, NULL);

	TEST_LIST_EMPTY (&job->blocking);

	blocked = blocked_new (NULL, BLOCKED_EVENT, event);
	TEST_NE_P (blocked, NULL);

	nih_list_add (&job->blocking, &blocked->entry);
	TEST_LIST_NOT_EMPTY (&job->blocking);

	event->blockers = 1;

	parent_str = nih_strdup (NULL, "parent");
	TEST_NE_P (parent_str, NULL);

	json_blocked = state_serialise_blocked (blocked);
	TEST_NE_P (json_blocked, NULL);

	TEST_LIST_EMPTY (&blocked_list);
	new_blocked = state_deserialise_blocked (parent_str,
			json_blocked, &blocked_list);
	TEST_NE_P (new_blocked, NULL);
	TEST_LIST_NOT_EMPTY (&blocked_list);

	assert0 (blocked_diff (blocked, new_blocked, ALREADY_SEEN_SET));

	json_object_put (json_blocked);
	nih_free (source);
	nih_free (event);

	/*******************************/
	/* Test Upstart 1.6+ behaviour
	 *
	 * The data serialisation format for this version now includes
	 * a "session" entry in the JSON for the blocked job.
	 *
	 * Note that this test is NOT testing whether a JobClass with an
	 * associated Upstart session is handled correctly, it is merely
	 * testing that a JobClass with the NULL session is handled
	 * correctly!
	 */
	TEST_FEATURE ("BLOCKED_JOB with JSON session object");

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_EMPTY (events);
	TEST_LIST_EMPTY (conf_sources);
	TEST_HASH_EMPTY (job_classes);

	event = event_new (NULL, "Christmas", NULL);
	TEST_NE_P (event, NULL);
	TEST_LIST_EMPTY (&event->blocking);

	TEST_LIST_EMPTY (conf_sources);
	source = conf_source_new (NULL, "/tmp/foo", CONF_JOB_DIR);
	TEST_NE_P (source, NULL);
	TEST_LIST_NOT_EMPTY (conf_sources);

	file = conf_file_new (source, "/tmp/foo/bar.conf");
	TEST_NE_P (file, NULL);
	/* Create class with NULL session */
	class = file->job = job_class_new (NULL, "bar", NULL);

	TEST_NE_P (class, NULL);
	TEST_HASH_EMPTY (job_classes);
	TEST_TRUE (job_class_consider (class));
	TEST_HASH_NOT_EMPTY (job_classes);

	job = job_new (class, "");
	TEST_NE_P (job, NULL);
	TEST_HASH_NOT_EMPTY (class->instances);

	blocked = blocked_new (event, BLOCKED_JOB, job);
	TEST_NE_P (blocked, NULL);

	nih_list_add (&event->blocking, &blocked->entry);
	job->blocker = event;

	TEST_LIST_NOT_EMPTY (&event->blocking);

	assert0 (state_to_string (&json_string, &len));
	TEST_GT (len, 0);

	/* ConfSources are now recreated on re-exec, so remove the
	 * original.
	 */
	nih_free (source);
	TEST_LIST_EMPTY (conf_sources);

	nih_list_remove (&class->entry);
	nih_list_remove (&event->entry);

	/* destroying the ConfSource will mark the JobClass as deleted,
	 * so undo that to allow comparison.
	 */
	nih_assert (class->deleted);
	class->deleted = FALSE;

	TEST_LIST_EMPTY (events);
	TEST_HASH_EMPTY (job_classes);

	job_class_environment_clear ();

	assert0 (state_from_string (json_string));

	TEST_LIST_NOT_EMPTY (conf_sources);
	TEST_LIST_NOT_EMPTY (events);
	TEST_HASH_NOT_EMPTY (job_classes);
	TEST_LIST_EMPTY (sessions);

	new_class = (JobClass *)nih_hash_lookup (job_classes, "bar");
	TEST_NE_P (new_class, NULL);
	nih_list_remove (&new_class->entry);

	/* Upstart 1.6 can only deserialise the NULL session */
	TEST_EQ_P (class->session, NULL);

	new_event = (Event *)nih_list_remove (events->next);
	TEST_LIST_EMPTY (events);
	TEST_LIST_NOT_EMPTY (&new_event->blocking);
	assert0 (event_diff (event, new_event, ALREADY_SEEN_SET));

	nih_free (event);
	nih_free (new_event);

	/* Check ConfSource got recreated, and then destroy it */
	source = NULL;
	NIH_LIST_FOREACH (conf_sources, iter) {
		source = (ConfSource *)iter;
		if (! strcmp (source->path, "/tmp/foo") && source->type == CONF_JOB_DIR)
			break;
	}
	TEST_NE_P (source, NULL);

	TEST_FREE_TAG (new_class);
	/* This will remove new_class too */
	nih_free (source);

	TEST_FREE (new_class);

	TEST_HASH_EMPTY (job_classes);

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_EMPTY (events);
	TEST_LIST_EMPTY (conf_sources);
	TEST_HASH_EMPTY (job_classes);

	/*******************************/
	TEST_FEATURE ("ensure BLOCKED_JOB with non-NULL session is ignored");

	TEST_LIST_EMPTY (sessions);
	session = session_new (NULL, "/my/session");
	TEST_NE_P (session, NULL);
	session->conf_path = NIH_MUST (nih_strdup (session, "/lives/here"));
	TEST_LIST_NOT_EMPTY (sessions);

	/* We simulate a user job being blocked by a system event, hence
	 * the session is not associated with the event.
	 */
	TEST_LIST_EMPTY (events);
	event = event_new (NULL, "Christmas", NULL);
	TEST_NE_P (event, NULL);
	TEST_LIST_EMPTY (&event->blocking);

	TEST_LIST_EMPTY (conf_sources);
	source = conf_source_new (NULL, "/tmp/foo", CONF_JOB_DIR);
	source->session = session;
	TEST_NE_P (source, NULL);
	TEST_LIST_NOT_EMPTY (conf_sources);

	file = conf_file_new (source, "/tmp/foo/bar.conf");
	TEST_NE_P (file, NULL);

	/* Create class with non-NULL session, simulating a user job */
	class = file->job = job_class_new (NULL, "bar", session);
	TEST_NE_P (class, NULL);

	TEST_HASH_EMPTY (job_classes);
	TEST_TRUE (job_class_consider (class));
	TEST_HASH_NOT_EMPTY (job_classes);

	job = job_new (class, "");
	TEST_NE_P (job, NULL);
	TEST_HASH_NOT_EMPTY (class->instances);

	blocked = blocked_new (event, BLOCKED_JOB, job);
	TEST_NE_P (blocked, NULL);

	nih_list_add (&event->blocking, &blocked->entry);
	job->blocker = event;

	TEST_LIST_NOT_EMPTY (&event->blocking);

	assert0 (state_to_string (&json_string, &len));
	TEST_GT (len, 0);

	/* ConfSources are now recreated on re-exec, so remove the
	 * original from the list.
	 */
	nih_list_remove (&source->entry);
	TEST_LIST_EMPTY (conf_sources);

	nih_list_remove (&class->entry);
	nih_free (event);

	nih_list_remove (&session->entry);
	TEST_LIST_EMPTY (sessions);

	TEST_LIST_EMPTY (events);
	TEST_HASH_EMPTY (job_classes);

	job_class_environment_clear ();

	assert0 (state_from_string (json_string));

	TEST_LIST_NOT_EMPTY (conf_sources);
	TEST_LIST_NOT_EMPTY (events);

	TEST_LIST_NOT_EMPTY (sessions);
	new_session = session_from_chroot ("/my/session");
	TEST_NE_P (new_session, NULL);
	assert0 (session_diff (session, new_session));

	/* We don't expect any job_classes since the serialised one
	 * related to a user session.
	 */
	TEST_HASH_EMPTY (job_classes);

	new_source = conf_source_from_path ("/tmp/foo", CONF_JOB_DIR, new_session);
	TEST_NE_P (new_source, NULL);

	new_file = (ConfFile *)nih_hash_lookup (new_source->files, "/tmp/foo/bar.conf");
	TEST_NE_P (new_file, NULL);

	/* We don't expect the original JobClass to have been
	 * deserialised since it has a non-NULL session.
	 */
	TEST_EQ_P (new_file->job, NULL);

	/* Disassociate the old JobClass from its ConfFile to allow a
	 * diff.
	 */
	file->job = NULL;

	assert0 (conf_source_diff (source, new_source, ALREADY_SEEN_SET));

	new_session = (Session *)nih_list_remove (sessions->next);

	nih_free (session);
	nih_free (new_session);
	nih_free (source);
	nih_free (new_source);
	nih_free (class);

	event = (Event *)nih_list_remove (events->next);
	nih_free (event);

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_EMPTY (events);
	TEST_LIST_EMPTY (conf_sources);
	TEST_HASH_EMPTY (job_classes);

	/*******************************/
	TEST_FEATURE ("event blocking a job");

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_EMPTY (events);
	TEST_LIST_EMPTY (conf_sources);
	TEST_HASH_EMPTY (job_classes);

	event = event_new (NULL, "Christmas", NULL);
	TEST_NE_P (event, NULL);
	TEST_LIST_EMPTY (&event->blocking);

	TEST_LIST_NOT_EMPTY (events);

	TEST_LIST_EMPTY (conf_sources);
	source = conf_source_new (NULL, "/tmp/foo", CONF_JOB_DIR);
	TEST_NE_P (source, NULL);
	TEST_LIST_NOT_EMPTY (conf_sources);

	file = conf_file_new (source, "/tmp/foo/bar.conf");
	TEST_NE_P (file, NULL);
	class = file->job = job_class_new (NULL, "bar", NULL);

	TEST_NE_P (class, NULL);
	TEST_HASH_EMPTY (job_classes);
	TEST_TRUE (job_class_consider (class));
	TEST_HASH_NOT_EMPTY (job_classes);

	job = job_new (class, "");
	TEST_NE_P (job, NULL);
	TEST_HASH_NOT_EMPTY (class->instances);

	blocked = blocked_new (NULL, BLOCKED_JOB, job);
	TEST_NE_P (blocked, NULL);

	nih_list_add (&event->blocking, &blocked->entry);
	job->blocker = event;

	assert0 (state_to_string (&json_string, &len));
	TEST_GT (len, 0);

	/* remove the source as these are now recreated on re-exec */
	nih_list_remove (&source->entry);
	nih_list_remove (&event->entry);
	nih_list_remove (&class->entry);

	TEST_HASH_EMPTY (job_classes);
	TEST_LIST_EMPTY (events);
	TEST_LIST_EMPTY (sessions);
	TEST_LIST_EMPTY (conf_sources);

	job_class_environment_clear ();

	assert0 (state_from_string (json_string));

	TEST_LIST_NOT_EMPTY (conf_sources);
	TEST_LIST_NOT_EMPTY (events);
	TEST_HASH_NOT_EMPTY (job_classes);
	TEST_LIST_EMPTY (sessions);

	new_class = (JobClass *)nih_hash_lookup (job_classes, "bar");
	TEST_NE_P (new_class, NULL);
	nih_list_remove (&new_class->entry);

	new_event = (Event *)nih_list_remove (events->next);
	TEST_LIST_EMPTY (events);
	TEST_LIST_NOT_EMPTY (&new_event->blocking);

	assert0 (event_diff (event, new_event, ALREADY_SEEN_SET));

	new_source = conf_source_from_path ("/tmp/foo", CONF_JOB_DIR, NULL);
	TEST_NE_P (new_source, NULL);

	assert0 (conf_source_diff (source, new_source, ALREADY_SEEN_SET));

	nih_free (event);
	nih_free (new_event);
	nih_free (source);
	nih_free (new_source);

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_EMPTY (events);
	TEST_LIST_EMPTY (conf_sources);
	TEST_HASH_EMPTY (job_classes);

	/*******************************/
	TEST_FEATURE ("job blocking an event");

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_EMPTY (events);
	TEST_LIST_EMPTY (conf_sources);
	TEST_HASH_EMPTY (job_classes);

	event = event_new (NULL, "bingo", NULL);
	TEST_NE_P (event, NULL);
	TEST_LIST_EMPTY (&event->blocking);

	TEST_LIST_EMPTY (conf_sources);
	source = conf_source_new (NULL, "/tmp/foo", CONF_JOB_DIR);
	TEST_NE_P (source, NULL);
	TEST_LIST_NOT_EMPTY (conf_sources);

	file = conf_file_new (source, "/tmp/foo/bar.conf");
	TEST_NE_P (file, NULL);
	class = file->job = job_class_new (NULL, "bar", NULL);

	TEST_NE_P (class, NULL);
	TEST_HASH_EMPTY (job_classes);
	TEST_TRUE (job_class_consider (class));
	TEST_HASH_NOT_EMPTY (job_classes);

	job = job_new (class, "");
	TEST_NE_P (job, NULL);
	TEST_HASH_NOT_EMPTY (class->instances);

	/* Simulate event_operator_events() */
	blocked = blocked_new (NULL, BLOCKED_EVENT, event);
	TEST_NE_P (blocked, NULL);

	nih_list_add (&job->blocking, &blocked->entry);
	event_block (event);
	TEST_EQ (event->blockers, 1);

	assert0 (state_to_string (&json_string, &len));
	TEST_GT (len, 0);

	nih_list_remove (&event->entry);
	nih_list_remove (&class->entry);
	nih_list_remove (&source->entry);

	TEST_HASH_EMPTY (job_classes);
	TEST_LIST_EMPTY (events);
	TEST_LIST_EMPTY (sessions);
	TEST_LIST_EMPTY (conf_sources);

	job_class_environment_clear ();

	assert0 (state_from_string (json_string));

	TEST_LIST_NOT_EMPTY (conf_sources);
	TEST_LIST_NOT_EMPTY (events);
	TEST_HASH_NOT_EMPTY (job_classes);
	TEST_LIST_EMPTY (sessions);

	new_source = conf_source_from_path ("/tmp/foo", CONF_JOB_DIR, NULL);
	TEST_NE_P (new_source, NULL);

	new_class = (JobClass *)nih_hash_lookup (job_classes, "bar");
	TEST_NE_P (new_class, NULL);
	nih_list_remove (&new_class->entry);

	new_event = (Event *)nih_list_remove (events->next);
	TEST_LIST_EMPTY (events);

	new_job = (Job *)nih_hash_lookup (new_class->instances, "");
	TEST_NE_P (new_job, NULL);

	TEST_EQ (event->blockers, 1);
	TEST_EQ (new_event->blockers, 1);

	assert0 (job_diff (job, new_job, ALREADY_SEEN_SET, TRUE));
	assert0 (job_class_diff (class, new_class, ALREADY_SEEN_SET, TRUE));

	nih_free (event);
	nih_free (new_event);
	nih_free (source);
	nih_free (new_source);

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_EMPTY (events);
	TEST_LIST_EMPTY (conf_sources);
	TEST_HASH_EMPTY (job_classes);

	/*******************************/
}

void
test_event_serialise (void)
{
	json_object         *json;
	Event               *event = NULL;
	Event               *new_event = NULL;
	nih_local char     **env = NULL;
	Session             *session;
	Session             *new_session;
	size_t               len = 0;
	nih_local char      *json_string = NULL;

	event_init ();
	session_init ();
	job_class_init ();

	TEST_GROUP ("Event serialisation and deserialisation");

	/*******************************/
	TEST_FEATURE ("without event environment");

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_EMPTY (events);

	event = event_new (NULL, "foo", NULL);
	TEST_NE_P (event, NULL);

	TEST_LIST_NOT_EMPTY (events);

	json = event_serialise (event);
	TEST_NE_P (json, NULL);

	nih_list_remove (&event->entry);
	TEST_LIST_EMPTY (events);

	new_event = event_deserialise (json);
	TEST_NE_P (json, NULL);
	TEST_LIST_NOT_EMPTY (events);

	assert0 (event_diff (event, new_event, ALREADY_SEEN_SET));

	nih_free (event);
	nih_free (new_event);
	json_object_put (json);

	/*******************************/
	TEST_FEATURE ("with event environment");

	TEST_LIST_EMPTY (events);
	TEST_LIST_EMPTY (sessions);

	env = nih_str_array_new (NULL);
	TEST_NE_P (env, NULL);
	TEST_NE_P (environ_add (&env, NULL, &len, TRUE, "FOO=BAR"), NULL);
	TEST_NE_P (environ_add (&env, NULL, &len, TRUE, "a="), NULL);
	TEST_NE_P (environ_add (&env, NULL, &len, TRUE, "HELLO=world"), NULL);

	event = event_new (NULL, "foo", env);
	TEST_NE_P (event, NULL);

	TEST_LIST_NOT_EMPTY (events);

	json = event_serialise (event);
	TEST_NE_P (json, NULL);

	nih_list_remove (&event->entry);

	new_event = event_deserialise (json);
	TEST_NE_P (json, NULL);

	assert0 (event_diff (event, new_event, ALREADY_SEEN_SET));

	nih_free (event);
	nih_free (new_event);
	json_object_put (json);

	TEST_LIST_EMPTY (events);
	TEST_LIST_EMPTY (sessions);

	/*******************************/
	TEST_FEATURE ("with progress values");

	TEST_LIST_EMPTY (events);
	TEST_LIST_EMPTY (sessions);

	/* Advance beyond last legitimate value to test failure behaviour */
	for (int progress = 0; progress <= EVENT_FINISHED+1; progress++) {
		TEST_LIST_EMPTY (events);
		TEST_LIST_EMPTY (sessions);

		event = event_new (NULL, "foo", NULL);
		TEST_NE_P (event, NULL);
		event->progress = progress;

		TEST_LIST_NOT_EMPTY (events);

		json = event_serialise (event);
		if (progress > EVENT_FINISHED) {
			TEST_EQ_P (json, NULL);
			nih_free (event);
			continue;
		}

		TEST_NE_P (json, NULL);

		nih_list_remove (&event->entry);

		new_event = event_deserialise (json);
		TEST_NE_P (json, NULL);

		assert0 (event_diff (event, new_event, ALREADY_SEEN_SET));

		nih_free (event);
		nih_free (new_event);
		json_object_put (json);
	}

	/*******************************/
	TEST_FEATURE ("with various fd values");

	TEST_LIST_EMPTY (events);
	TEST_LIST_EMPTY (sessions);

	for (int fd = -1; fd < 4; fd++) {
		TEST_LIST_EMPTY (events);
		TEST_LIST_EMPTY (sessions);

		event = event_new (NULL, "foo", NULL);
		TEST_NE_P (event, NULL);
		event->fd = fd;

		TEST_LIST_NOT_EMPTY (events);

		json = event_serialise (event);
		TEST_NE_P (json, NULL);

		nih_list_remove (&event->entry);

		new_event = event_deserialise (json);
		TEST_NE_P (json, NULL);

		assert0 (event_diff (event, new_event, ALREADY_SEEN_SET));

		nih_free (event);
		nih_free (new_event);
		json_object_put (json);
	}

	/*******************************/
	TEST_FEATURE ("with env+session");

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_EMPTY (events);
	TEST_HASH_EMPTY (job_classes);

	env = nih_str_array_new (NULL);
	TEST_NE_P (env, NULL);
	TEST_NE_P (environ_add (&env, NULL, &len, TRUE, "FOO=BAR"), NULL);

	session = session_new (NULL, "/abc");
	TEST_NE_P (session, NULL);
	session->conf_path = NIH_MUST (nih_strdup (session, "/def/ghi"));
	TEST_LIST_NOT_EMPTY (sessions);

	event = event_new (NULL, "foo", env);
	TEST_NE_P (event, NULL);
	TEST_LIST_NOT_EMPTY (events);
	event->session = session;

	assert0 (state_to_string (&json_string, &len));
	TEST_GT (len, 0);

	nih_list_remove (&event->entry);
	nih_list_remove (&session->entry);

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_EMPTY (events);

	job_class_environment_clear ();

	assert0 (state_from_string (json_string));

	TEST_LIST_NOT_EMPTY (sessions);
	TEST_LIST_NOT_EMPTY (events);

	new_event = (Event *)nih_list_remove (events->next);
	assert0 (event_diff (event, new_event, ALREADY_SEEN_SET));

	nih_free (event);
	nih_free (session);

	new_session = (Session *)nih_list_remove (sessions->next);
	TEST_NE_P (new_session, NULL);

	nih_free (new_event);
	nih_free (new_session);

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_EMPTY (events);

	/*******************************/

	TEST_FEATURE ("with failed");

	TEST_LIST_EMPTY (events);
	TEST_HASH_EMPTY (job_classes);
	TEST_LIST_EMPTY (sessions);

	event = event_new (NULL, "foo", NULL);
	TEST_NE_P (event, NULL);
	TEST_LIST_NOT_EMPTY (events);

	/* Force failed */
	event->failed = TRUE;

	json = event_serialise (event);
	TEST_NE_P (json, NULL);

	nih_list_remove (&event->entry);

	new_event = event_deserialise (json);
	TEST_NE_P (json, NULL);

	assert0 (event_diff (event, new_event, ALREADY_SEEN_SET));
	TEST_EQ (new_event->failed, TRUE);

	nih_free (event);
	nih_free (new_event);

	/*******************************/

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_EMPTY (events);
	TEST_HASH_EMPTY (job_classes);
	TEST_LIST_EMPTY (sessions);

	/*******************************/
}

void
test_log_serialise (void)
{
	json_object     *json;
	json_object     *json_unflushed = NULL;
	Log             *log;
	Log             *new_log;
	int              pty_master;
	int              pty_slave;
	size_t           len;
	ssize_t          ret;
	char             filename[PATH_MAX];
	pid_t            pid;
	int              wait_fd;
	int              fd;
	int              status;

	conf_init ();
	nih_io_init ();
	log_unflushed_init ();

	TEST_TRUE (NIH_LIST_EMPTY (conf_sources));
	TEST_TRUE (NIH_LIST_EMPTY (log_unflushed_files));
	TEST_TRUE (NIH_LIST_EMPTY (nih_io_watches));

	TEST_GROUP ("Log serialisation and deserialisation");

	/*******************************/
	/* XXX: No test for uid > 0 since user logging not currently
	 * XXX: available.
	 */
	TEST_FEATURE ("with uid 0");

	TEST_EQ (openpty (&pty_master, &pty_slave, NULL, NULL, NULL), 0);

	log = log_new (NULL, "/foo", pty_master, 0);
	TEST_NE_P (log, NULL);
	TEST_FALSE (NIH_LIST_EMPTY (nih_io_watches));

	json = log_serialise (log);
	TEST_NE_P (json, NULL);

	new_log = log_deserialise (NULL, json);
	TEST_NE_P (new_log, NULL);

	assert0 (log_diff (log, new_log));

	close (pty_master);
	close (pty_slave);
	nih_free (log);
	nih_free (new_log);

	/*******************************/
	TEST_FEATURE ("with unflushed data");

	TEST_FILENAME (filename);

	TEST_TRUE (NIH_LIST_EMPTY (nih_io_watches));

	TEST_EQ (openpty (&pty_master, &pty_slave, NULL, NULL, NULL), 0);

	/* Make file inaccessible to ensure data cannot be written
	 * and will thus be added to the unflushed buffer.
	 */
	fd = open (filename, O_CREAT | O_EXCL, 0);
	TEST_NE (fd, -1);
	close (fd);

	/* Set up logging that we know won't go anywhere yet */
	log = log_new (NULL, filename, pty_master, 0);
	TEST_NE_P (log, NULL);
	TEST_FALSE (NIH_LIST_EMPTY (nih_io_watches));

	TEST_CHILD_WAIT (pid, wait_fd) {

		close (pty_master);

		len = TEST_ARRAY_SIZE (test_data);
		errno = 0;

		/* Now write some data with embedded nulls */
		ret = write (pty_slave, test_data, len);
		TEST_EQ ((size_t)ret, len);

		/* let parent continue */
		TEST_CHILD_RELEASE (wait_fd);

		/* keep child running until the parent is ready (to
		 * simulate a job which continues to run across
		 * a re-exec).
		 */
		pause ();
	}

	close (pty_slave);

	/* Ensure that unflushed buffer contains data */
	TEST_WATCH_UPDATE ();

	TEST_GT (log->unflushed->len, 0);

	/* Serialise the log which will now contain the unflushed data */
	json = log_serialise (log);
	TEST_NE_P (json, NULL);

	/* Sanity check */
	ret = json_object_object_get_ex (json, "unflushed", &json_unflushed);
	TEST_EQ (ret, TRUE);
	TEST_NE_P (json_unflushed, NULL);

	new_log = log_deserialise (NULL, json);
	TEST_NE_P (new_log, NULL);

	assert0 (log_diff (log, new_log));

	/* Wait for child to finish */
	assert0 (kill (pid, SIGTERM));
	TEST_EQ (waitpid (pid, &status, 0), pid);

	/* Restore access to allow log to be written on destruction */
	TEST_EQ (chmod (filename, 0644), 0);

	nih_free (log);
	nih_free (new_log);
	TEST_TRUE (NIH_LIST_EMPTY (nih_io_watches));
	TEST_EQ (unlink (filename), 0);

	/*******************************/
}

void
test_job_class_serialise (void)
{
	ConfSource     *source;
	ConfFile       *file;
	JobClass       *class;
	JobClass       *new_class;
	Job            *job1;
	Job            *job2;
	Job            *job3;
	json_object    *json;

	TEST_GROUP ("JobClass serialisation and deserialisation");


	/*******************************/
	TEST_FEATURE ("JobClass with no Jobs");

	TEST_HASH_EMPTY (job_classes);

	source = conf_source_new (NULL, "/tmp/foo", CONF_JOB_DIR);
	TEST_NE_P (source, NULL);

	file = conf_file_new (source, "/tmp/foo/bar.conf");
	TEST_NE_P (file, NULL);

	class = file->job = job_class_new (NULL, "bar", NULL);
	TEST_NE_P (class, NULL);
	TEST_HASH_EMPTY (job_classes);
	TEST_TRUE (job_class_consider (class));
	TEST_HASH_NOT_EMPTY (job_classes);

	/* BEHAVIOURAL CHANGE:
	 *
	 * Previously, a JobClass with no associated Jobs would
	 * not be serialised (since, as there were no "running" jobs
	 * associated with it, it was considered unnecessary).
	 *
	 * However, we now serialise *all* JobClasses regardless since
	 * in the case of a stateful re-exec, we need as much state as
	 * possible, particularly since Events have always been fully
	 * serialised, and if Event->blockers is non-zero, it is
	 * necessary to manipulate the 'start on' EventOperator tree for
	 * non-running jobs post-reexec to correspond to the
	 * Event->blockers value.
	 */
	json = job_class_serialise (class);
	TEST_NE_P (json, NULL);

	nih_free (source);

	/*******************************/
	TEST_FEATURE ("JobClass with 1 Job");

	TEST_HASH_EMPTY (job_classes);

	source = conf_source_new (NULL, "/tmp/foo", CONF_JOB_DIR);
	TEST_NE_P (source, NULL);

	file = conf_file_new (source, "/tmp/foo/bar.conf");
	TEST_NE_P (file, NULL);

	class = file->job = job_class_new (NULL, "bar", NULL);
	TEST_NE_P (class, NULL);
	TEST_HASH_EMPTY (job_classes);
	TEST_TRUE (job_class_consider (class));
	TEST_HASH_NOT_EMPTY (job_classes);

	job1 = job_new (class, "");
	TEST_NE_P (job1, NULL);
	TEST_HASH_NOT_EMPTY (class->instances);

	class->process[PROCESS_MAIN] = process_new (class);
	TEST_NE_P (class->process[PROCESS_MAIN], NULL);
	class->process[PROCESS_MAIN]->command = "echo";

	class->process[PROCESS_PRE_STOP] = process_new (class);
	TEST_NE_P (class->process[PROCESS_PRE_STOP], NULL);
	class->process[PROCESS_PRE_STOP]->command = "echo";

	job1->goal = JOB_START;
	job1->state = JOB_PRE_STOP;
	job1->pid[PROCESS_MAIN] = 1234;
	job1->pid[PROCESS_PRE_STOP] = 5678;

	json = job_class_serialise (class);
	TEST_NE_P (json, NULL);

	nih_list_remove (&class->entry);
	TEST_HASH_EMPTY (job_classes);

	new_class = job_class_deserialise (json);
	TEST_NE_P (new_class, NULL);

	assert0 (job_class_diff (class, new_class, ALREADY_SEEN_SET, TRUE));

	nih_free (source);
	nih_free (new_class);
	json_object_put (json);

	/*******************************/
	TEST_FEATURE ("JobClass with >1 Jobs");

	TEST_HASH_EMPTY (job_classes);

	source = conf_source_new (NULL, "/tmp/foo", CONF_JOB_DIR);
	TEST_NE_P (source, NULL);

	file = conf_file_new (source, "/tmp/foo/bar.conf");
	TEST_NE_P (file, NULL);

	class = file->job = job_class_new (NULL, "bar", NULL);
	TEST_NE_P (class, NULL);
	class->reload_signal = SIGUSR1;
	TEST_HASH_EMPTY (job_classes);
	TEST_TRUE (job_class_consider (class));
	TEST_HASH_NOT_EMPTY (job_classes);

	job1 = job_new (class, "a");
	TEST_NE_P (job1, NULL);

	job2 = job_new (class, "b");
	TEST_NE_P (job1, NULL);

	job3 = job_new (class, "c");
	TEST_NE_P (job1, NULL);

	TEST_HASH_NOT_EMPTY (class->instances);

	class->process[PROCESS_MAIN] = process_new (class);
	TEST_NE_P (class->process[PROCESS_MAIN], NULL);
	class->process[PROCESS_MAIN]->command = "echo";

	class->process[PROCESS_PRE_STOP] = process_new (class);
	TEST_NE_P (class->process[PROCESS_PRE_STOP], NULL);
	class->process[PROCESS_PRE_STOP]->command = "echo";

	job1->goal = JOB_START;
	job1->state = JOB_PRE_STOP;
	job1->pid[PROCESS_MAIN] = 1234;
	job1->pid[PROCESS_PRE_STOP] = 5678;

	job2->goal = JOB_STOP;
	job2->state = JOB_WAITING;

	job3->goal = JOB_START;
	job3->state = JOB_RUNNING;
	job3->pid[PROCESS_MAIN] = 1;

	json = job_class_serialise (class);
	TEST_NE_P (json, NULL);

	nih_list_remove (&class->entry);
	TEST_HASH_EMPTY (job_classes);

	new_class = job_class_deserialise (json);
	TEST_NE_P (new_class, NULL);

	assert0 (job_class_diff (class, new_class, ALREADY_SEEN_SET, TRUE));

	nih_free (source);
	nih_free (new_class);
	json_object_put (json);

	/*******************************/
}

void
test_job_serialise (void)
{
	nih_local JobClass   *class = NULL;
	Job                  *job;
	Job                  *new_job;
	json_object          *json;


	TEST_GROUP ("Job serialisation and deserialisation");

	TEST_HASH_EMPTY (job_classes);

	class = job_class_new (NULL, "class", NULL);
	TEST_NE_P (class, NULL);
	TEST_HASH_EMPTY (class->instances);

	/*******************************/
	TEST_FEATURE ("basic job");

	job = job_new (class, "");
	TEST_NE_P (job, NULL);
	TEST_HASH_NOT_EMPTY (class->instances);

	json = json_object_new_object ();
	TEST_NE_P (json, NULL);

	json = job_serialise (job);
	TEST_NE_P (json, NULL);

	nih_list_remove (&job->entry);
	TEST_HASH_EMPTY (class->instances);

	new_job = job_deserialise (class, json);
	TEST_NE_P (new_job, NULL);
	TEST_HASH_NOT_EMPTY (class->instances);

	assert0 (job_diff (job, new_job, ALREADY_SEEN_SET, TRUE));

	nih_free (job);
	json_object_put (json);

	/*******************************/
}

void
test_enums (void)
{
	int           blocked_value;
	const char   *string_value;
	int           i;

	TEST_GROUP ("stateful re-exec enums");

	/*******************************/
	TEST_FEATURE ("BlockedType");

	for (i = -3; i < BLOCKED_INSTANCE_RESTART_METHOD+3; i++) {

		/* convert to string value */
		string_value = blocked_type_enum_to_str (i);
		if (i < 0 || i > BLOCKED_INSTANCE_RESTART_METHOD) {
			TEST_EQ_P (string_value, NULL);
			continue;
		} else {
			TEST_NE_P (string_value, NULL);
		}

		/* convert back to enum */
		blocked_value = blocked_type_str_to_enum (string_value);
		if (i < 0 || i > BLOCKED_INSTANCE_RESTART_METHOD) {
			TEST_EQ (blocked_value, -1);
		} else {
			TEST_NE (blocked_value, -1);
			TEST_EQ (blocked_value, i);
		}
	}

	TEST_EQ ((int)blocked_type_str_to_enum (""), -1);
	TEST_EQ ((int)blocked_type_str_to_enum ("foo"), -1);

	/*******************************/
	TEST_FEATURE ("ProcessType");

	for (i = PROCESS_INVALID-1; i < PROCESS_LAST+3; i++) {

		/* convert to string value */
		string_value = process_type_enum_to_str (i);
		if ((i < 0 && i != -2) || (i+1) > PROCESS_LAST) {
			TEST_EQ_P (string_value, NULL);
			continue;
		} else {
			TEST_NE_P (string_value, NULL);
		}

		/* convert back to enum */
		blocked_value = process_type_str_to_enum (string_value);
		if ((i < 0 && i != -2) || (i+1) > PROCESS_LAST) {
			TEST_EQ (blocked_value, -1);
		} else {
			TEST_NE (blocked_value, -1);
			TEST_EQ (blocked_value, i);
		}
	}

	TEST_EQ ((int)process_type_str_to_enum (""), -1);
	TEST_EQ ((int)process_type_str_to_enum ("foo"), -1);

	/*******************************/
	TEST_FEATURE ("ConsoleType");

	for (i = -3; i < CONSOLE_LOG+3; i++) {

		/* convert to string value */
		string_value = job_class_console_type_enum_to_str (i);
		if (i < 0 || i > CONSOLE_LOG) {
			TEST_EQ_P (string_value, NULL);
			continue;
		} else {
			TEST_NE_P (string_value, NULL);
		}

		/* convert back to enum */
		blocked_value = job_class_console_type_str_to_enum (string_value);
		if (i < 0 || i > CONSOLE_LOG) {
			TEST_EQ (blocked_value, -1);
		} else {
			TEST_NE (blocked_value, -1);
			TEST_EQ (blocked_value, i);
		}
	}

	TEST_EQ ((int)job_class_console_type_str_to_enum (""), -1);
	TEST_EQ ((int)job_class_console_type_str_to_enum ("foo"), -1);

	/*******************************/
	TEST_FEATURE ("ExpectType");

	for (i = -3; i < EXPECT_FORK+3; i++) {

		/* convert to string value */
		string_value = job_class_expect_type_enum_to_str (i);
		if (i < 0 || i > EXPECT_FORK) {
			TEST_EQ_P (string_value, NULL);
			continue;
		} else {
			TEST_NE_P (string_value, NULL);
		}

		/* convert back to enum */
		blocked_value = job_class_expect_type_str_to_enum (string_value);
		if (i < 0 || i > EXPECT_FORK) {
			TEST_EQ (blocked_value, -1);
		} else {
			TEST_NE (blocked_value, -1);
			TEST_EQ (blocked_value, i);
		}
	}

	TEST_EQ ((int)job_class_expect_type_str_to_enum (""), -1);
	TEST_EQ ((int)job_class_expect_type_str_to_enum ("foo"), -1);

	/*******************************/

}

void
test_int_arrays (void)
{
	int                 ret;
	size_t              size32 = 7;
	size_t              size64 = 9;
	size_t              i;
	size_t              new_size;
	char               *parent = NULL;
	json_object        *json;
	nih_local int32_t  *array32 = NULL;
	nih_local int64_t  *array64 = NULL;
	int32_t	           *new_array32 = NULL;
	int64_t	           *new_array64 = NULL;

	array32 = nih_alloc (NULL, size32 * sizeof (int32_t));
	TEST_NE_P (array32, NULL);

	i = 0;
	array32[i++] = INT32_MIN;
	array32[i++] = -254;
	array32[i++] = -1;
	array32[i++] = 0;
	array32[i++] = 1;
	array32[i++] = 123456;
	array32[i++] = INT32_MAX;

	assert (size32 == i);

	array64 = nih_alloc (NULL, size64 * sizeof (int64_t));
	TEST_NE_P (array64, NULL);

	i = 0;
	array64[i++] = INT64_MIN;
	array64[i++] = -255;
	array64[i++] = -1;
	array64[i++] = 0;
	array64[i++] = 1;
	array64[i++] = 7;
	array64[i++] = 99;
	array64[i++] = 123456;
	array64[i++] = INT64_MAX;

	assert (size64 == i);

	TEST_GROUP ("integer array serialisation and deserialisation");

	parent = nih_strdup (NULL, "");
	TEST_NE_P (parent, NULL);

	/*******************************/
	TEST_FEATURE ("explicit 32-bit integer array");

	json = state_serialise_int32_array (array32, size32);
	TEST_NE_P (json, NULL);

	ret = state_deserialise_int32_array (parent, json,
			&new_array32, &new_size);
	TEST_EQ (ret, 0);

	ret = TEST_CMP_INT_ARRAYS (array32, new_array32, size32, new_size);
	TEST_EQ (ret, 0);

	json_object_put (json);

	/*******************************/
	TEST_FEATURE ("implicit 32-bit integer array");

	json = state_serialise_int_array (int32_t, array32, size32);
	TEST_NE_P (json, NULL);

	ret = state_deserialise_int_array (parent, json,
			int32_t, &new_array32, &new_size);
	TEST_EQ (ret, 0);

	ret = TEST_CMP_INT_ARRAYS (array32, new_array32, size32, new_size);
	TEST_EQ (ret, 0);

	json_object_put (json);

	/*******************************/
	TEST_FEATURE ("explicit 64-bit integer array");

	json = state_serialise_int64_array (array64, size64);
	TEST_NE_P (json, NULL);

	ret = state_deserialise_int64_array (parent, json,
			&new_array64, &new_size);
	TEST_EQ (ret, 0);

	ret = TEST_CMP_INT_ARRAYS (array64, new_array64, size64, new_size);
	TEST_EQ (ret, 0);

	json_object_put (json);

	/*******************************/
	TEST_FEATURE ("implicit 64-bit integer array");

	json = state_serialise_int_array (int64_t, array64, size64);
	TEST_NE_P (json, NULL);

	ret = state_deserialise_int_array (parent, json,
			int64_t, &new_array64, &new_size);
	TEST_EQ (ret, 0);

	ret = TEST_CMP_INT_ARRAYS (array64, new_array64, size64, new_size);
	TEST_EQ (ret, 0);

	json_object_put (json);

	/*******************************/
	TEST_FEATURE ("implicit native integer array");

	if (sizeof (int) == 4) {
		json = state_serialise_int_array (int, array32, size32);
		TEST_NE_P (json, NULL);

		ret = state_deserialise_int_array (parent, json,
				int32_t, &new_array32, &new_size);
		TEST_EQ (ret, 0);

		ret = TEST_CMP_INT_ARRAYS (array32, new_array32, size32, new_size);
		TEST_EQ (ret, 0);
	} else if (sizeof (int) == 8) {
		json = state_serialise_int_array (int, array64, size64);
		TEST_NE_P (json, NULL);

		ret = state_deserialise_int_array (parent, json,
				int64_t, &new_array64, &new_size);
		TEST_EQ (ret, 0);

		ret = TEST_CMP_INT_ARRAYS (array64, new_array64, size64, new_size);
		TEST_EQ (ret, 0);
	} else {
		/* How long before this fires? ;-) */
		nih_assert_not_reached ();
	}

	json_object_put (json);
	/*******************************/

	/* parent frees the new arrays */
	nih_free (parent);
}

void
test_string_arrays (void)
{
	json_object      *json;
	nih_local char  **array = NULL;
	char            **new_array;
	int               ret;
	size_t            len = 0;
	size_t            new_len = 0;

	TEST_GROUP ("string array serialisation and deserialisation");

	/*******************************/
	TEST_FEATURE ("serialisation of empty array");

	len = 0;
	array = nih_str_array_new (NULL);
	TEST_NE_P (array, NULL);

	json = state_serialise_str_array (array);
	TEST_NE_P (json, NULL);

	/*******************************/
	TEST_FEATURE ("deserialisation of empty array");

	ret = state_deserialise_str_array (NULL, json, &new_array);
	TEST_TRUE (ret);

	/* count elements */
	new_len = 0;
	for (char **p = new_array; p && *p; p++, new_len++)
		;

	ret = TEST_CMP_STR_ARRAYS (array, new_array, len, new_len);
	TEST_EQ (ret, 0);
	TEST_EQ_P (new_array, NULL);

	json_object_put (json);

	/*******************************/
	TEST_FEATURE ("serialisation of array with single nul string");

	len = 0;
	array = nih_str_array_new (NULL);
	TEST_NE_P (array, NULL);

	NIH_MUST (nih_str_array_add (&array, NULL, &len, ""));

	json = state_serialise_str_array (array);
	TEST_NE_P (json, NULL);

	/*******************************/
	TEST_FEATURE ("deserialisation of array with single nul string");

	ret = state_deserialise_str_array (NULL, json, &new_array);
	TEST_TRUE (ret);

	/* count elements */
	new_len = 0;
	for (char **p = new_array; p && *p; p++, new_len++)
		;

	ret = TEST_CMP_STR_ARRAYS (array, new_array, len, new_len);
	TEST_EQ (ret, 0);

	TEST_NE_P (new_array, NULL);
	nih_free (new_array);

	json_object_put (json);

	/*******************************/
	TEST_FEATURE ("serialisation of non-empty array");

	len = 0;
	array = nih_str_array_new (NULL);
	TEST_NE_P (array, NULL);

	NIH_MUST (nih_str_array_add (&array, NULL, &len, ""));
	NIH_MUST (nih_str_array_add (&array, NULL, &len, ""));
	NIH_MUST (nih_str_array_add (&array, NULL, &len, "hello="));
	NIH_MUST (nih_str_array_add (&array, NULL, &len, "FOO=BAR"));
	NIH_MUST (nih_str_array_add (&array, NULL, &len, "wibble"));
	NIH_MUST (nih_str_array_add (&array, NULL, &len, "\n"));
	NIH_MUST (nih_str_array_add (&array, NULL, &len, "\t \n"));
	NIH_MUST (nih_str_array_add (&array, NULL, &len, "\"'$*&()[]{}-_=+/?@':;>.<,~#"));
	NIH_MUST (nih_str_array_add (&array, NULL, &len, ""));

	json = state_serialise_str_array (array);
	TEST_NE_P (json, NULL);

	/*******************************/
	TEST_FEATURE ("deserialisation of non-empty array");

	ret = state_deserialise_str_array (NULL, json, &new_array);
	TEST_TRUE (ret);

	/* count elements */
	new_len = 0;
	for (char **p = new_array; p && *p; p++, new_len++)
		;

	ret = TEST_CMP_STR_ARRAYS (array, new_array, len, new_len);
	TEST_EQ (ret, 0);

	TEST_NE_P (new_array, NULL);
	nih_free (new_array);

	json_object_put (json);

	/*******************************/
	/* XXX: No point in checking an empty environment array as
         * its the same as a string array.
	 */
	TEST_FEATURE ("serialisation of non-empty environment array");

	len = 0;
	array = nih_str_array_new (NULL);
	TEST_NE_P (array, NULL);

	TEST_NE_P (environ_add (&array, NULL, &len, TRUE, "foo=bar"), NULL);
	TEST_NE_P (environ_add (&array, NULL, &len, TRUE, "hello="), NULL);
	TEST_NE_P (environ_add (&array, NULL, &len, TRUE, "\"'$*&()[]{}-_=+/?@':;>.<,~#"), NULL);

	json = state_serialise_str_array (array);
	TEST_NE_P (json, NULL);

	/*******************************/
	TEST_FEATURE ("deserialisation of non-empty environment array");

	ret = state_deserialise_env_array (NULL, json, &new_array);
	TEST_TRUE (ret);

	/* count elements */
	new_len = 0;
	for (char **p = new_array; p && *p; p++, new_len++)
		;

	ret = TEST_CMP_STR_ARRAYS (array, new_array, len, new_len);
	TEST_EQ (ret, 0);

	TEST_NE_P (new_array, NULL);
	nih_free (new_array);

	json_object_put (json);
	/*******************************/
}

void
test_hex_encoding (void)
{
	int                ret;
	nih_local char    *hex_data = NULL;
	size_t             hex_data_len;
	size_t             test_data_len;
	nih_local char    *new_data = NULL;
	size_t             new_data_len;

	test_data_len = TEST_ARRAY_SIZE (test_data);

	TEST_GROUP ("hex data encoding");

	/*******************************/
	TEST_FEATURE ("serialisation");

	hex_data = state_data_to_hex (NULL, test_data, test_data_len);
	TEST_NE_P (hex_data, NULL);
	hex_data_len = strlen (hex_data);

	/*******************************/
	TEST_FEATURE ("deserialisation");

	ret = state_hex_to_data (NULL, hex_data, hex_data_len,
			&new_data, &new_data_len);
	TEST_EQ (ret, 0);


	ret = TEST_CMP_INT_ARRAYS (test_data, new_data, test_data_len, new_data_len);
	TEST_EQ (ret, 0);
}

void
test_rlimit_encoding (void)
{
	int                       ret;
	nih_local Foo            *foo = NULL;
	nih_local Foo            *new_foo = NULL;
	json_object              *json;
	json_object              *json_limits;
	struct rlimit             limit;
	nih_local struct rlimit  *new_limit = NULL;
	int                       len;
	int                       i;

	TEST_GROUP ("rlimit encoding");

	/*******************************/
	TEST_FEATURE ("single rlimit serialisation and deserialisation");

	len = TEST_ARRAY_SIZE (rlimit_values);

	for (int i = 0; i < len; i++) {

		limit.rlim_cur = rlimit_values[i];
		limit.rlim_max = RLIM_INFINITY - rlimit_values[i];
		
		json = state_rlimit_serialise (&limit);
		TEST_NE_P (json, NULL);

		new_limit = state_rlimit_deserialise (json);
		TEST_NE_P (new_limit, NULL);

		TEST_EQ (limit.rlim_cur, new_limit->rlim_cur);
		TEST_EQ (limit.rlim_max, new_limit->rlim_max);
	}

	json_object_put (json);

	/*******************************/
	TEST_FEATURE ("rlimits array serialisation and deserialisation");

	len = TEST_ARRAY_SIZE (rlimit_values);

	foo = nih_new (NULL, Foo);
	TEST_NE_P (foo, NULL);

	new_foo = nih_new (NULL, Foo);
	TEST_NE_P (new_foo, NULL);

	for (i = 0; i < RLIMIT_NLIMITS; i++)
		foo->limits[i] = new_foo->limits[i] = NULL;

	for (i = 0; i < len; i++) {
		json = json_object_new_object ();
		TEST_NE_P (json, NULL);

		foo->limits[i] = nih_new (foo, struct rlimit);
		TEST_NE_P (foo->limits[i], NULL);

		foo->limits[i]->rlim_cur = RLIM_INFINITY - rlimit_values[i];
		foo->limits[i]->rlim_max = rlimit_values[i];

		json_limits = state_rlimit_serialise_all (foo->limits);
		TEST_NE_P (json_limits, NULL);

		json_object_object_add (json, "limits", json_limits);

		ret = state_rlimit_deserialise_all (json, new_foo, &new_foo->limits);
		TEST_EQ (ret, 0);

		json_object_put (json);
	}
}

void
test_basic_types (void)
{
	int                ret;
	int32_t            int32 = -1;
	int64_t            int64 = -1;
	size_t             i;
	size_t             size32;
	size_t             size64;
	size_t             sizestr;
	char              *str = NULL;
	size_t             len;
	size_t             new_len;
	json_object       *json;
	nih_local Foo     *foo = NULL;
	nih_local Foo     *new_foo = NULL;
	nih_local char  **array = NULL;
	nih_local char  **new_array = NULL;

	size32 = TEST_ARRAY_SIZE (values32);
	size64 = TEST_ARRAY_SIZE (values64);
	sizestr = TEST_ARRAY_SIZE (strings);

	TEST_GROUP ("basic types");

	json = json_object_new_object ();
	TEST_NE_P (json, NULL);

	foo = nih_new (NULL, Foo);
	TEST_NE_P (foo, NULL);

	new_foo = nih_new (NULL, Foo);
	TEST_NE_P (new_foo, NULL);

	memset (foo, '\0', sizeof (Foo));
	memset (new_foo, '\0', sizeof (Foo));

	/*******************************/
	TEST_FEATURE ("32-bit integer serialisation and deserialisation");

	for (i = 0; i < size32; i++) {
		TEST_TRUE (state_set_json_int_var (json, "foo", values32[i]));
		TEST_TRUE (state_get_json_int_var (json, "foo", int32));
		TEST_EQ (int32, values32[i]);
	}

	/*******************************/
	TEST_FEATURE ("64-bit integer serialisation and deserialisation");

	for (i = 0; i < size64; i++) {
		TEST_TRUE (state_set_json_int_var (json, "foo", values64[i]));
		TEST_TRUE (state_get_json_int_var (json, "foo", int64));
		TEST_EQ (int64, values64[i]);
	}

	/*******************************/
	TEST_FEATURE ("string serialisation and deserialisation");

	for (i = 0; i < sizestr; i++) {
		TEST_TRUE (state_set_json_string_var (json, "s", strings[i]));
		TEST_TRUE (state_get_json_string_var (json, "s", NULL, str));
		if (strings[i] == NULL) {
			TEST_EQ (strings[i], str);
		} else {
			TEST_EQ_STR (strings[i], str);
		}
	}

	/*******************************/
	TEST_FEATURE ("strict string serialisation and deserialisation");

	for (i = 0; i < sizestr; i++) {
		TEST_TRUE (state_set_json_string_var (json, "s", strings[i]));

		ret = state_get_json_string_var_strict (json, "s", NULL, str);

		if (strings[i] == NULL) {
			TEST_FALSE (ret);
			TEST_EQ (strings[i], str);
		} else {
			TEST_TRUE (ret);
			TEST_EQ_STR (strings[i], str);
		}
	}

	/*******************************/
	TEST_FEATURE ("32-bit object integer serialisation and deserialisation");

	for (i = 0; i < size32; i++) {
		foo->int32 = values32[i];
		TEST_TRUE (state_set_json_int_var_from_obj (json, foo, int32));
		TEST_TRUE (state_get_json_int_var_to_obj (json, new_foo, int32));
		TEST_EQ (new_foo->int32, foo->int32);
	}

	/*******************************/
	TEST_FEATURE ("64-bit object integer serialisation and deserialisation");

	for (i = 0; i < size64; i++) {
		foo->int64 = values64[i];
		TEST_TRUE (state_set_json_int_var_from_obj (json, foo, int64));
		TEST_TRUE (state_get_json_int_var_to_obj (json, new_foo, int64));
		TEST_EQ (new_foo->int64, foo->int64);
	}
	json_object_put (json);

	/*******************************/
	TEST_FEATURE ("empty object string array serialisation and deserialisation");

	json = json_object_new_object ();
	TEST_NE_P (json, NULL);

	foo->array = nih_str_array_new (NULL);
	TEST_NE_P (foo->array, NULL);

	len = new_len = 0;

	TEST_TRUE (state_set_json_str_array_from_obj (json, foo, array));
	TEST_TRUE (state_get_json_str_array_to_obj (json, new_foo, array));

	/* count elements */
	new_len = 0;
	for (char **p = new_foo->array; p && *p; p++, new_len++)
		;

	ret = TEST_CMP_STR_ARRAYS (foo->array, new_foo->array, len, new_len);
	TEST_EQ (ret, 0);

	/* An empty array should be deserialised to "no array"
	 * (since an empty JSON array is the encoding for "no array").
	 */
	TEST_EQ_P (new_foo->array, NULL);

	json_object_put (json);

	/*******************************/
	TEST_FEATURE ("object string array serialisation and deserialisation");

	json = json_object_new_object ();
	TEST_NE_P (json, NULL);

	foo = nih_new (NULL, Foo);
	TEST_NE_P (foo, NULL);
	memset (foo, '\0', sizeof (Foo));

	new_foo = nih_new (NULL, Foo);
	TEST_NE_P (new_foo, NULL);
	memset (new_foo, '\0', sizeof (Foo));

	foo->array = nih_str_array_new (NULL);
	TEST_NE_P (foo->array, NULL);

	len = new_len = 0;
	NIH_MUST (nih_str_array_add (&foo->array, NULL, &len, ""));
	NIH_MUST (nih_str_array_add (&foo->array, NULL, &len, ""));
	NIH_MUST (nih_str_array_add (&foo->array, NULL, &len, "hello="));
	NIH_MUST (nih_str_array_add (&foo->array, NULL, &len, "FOO=BAR"));
	NIH_MUST (nih_str_array_add (&foo->array, NULL, &len, "wibble"));
	NIH_MUST (nih_str_array_add (&foo->array, NULL, &len, "\n"));
	NIH_MUST (nih_str_array_add (&foo->array, NULL, &len, "\t \n"));
	NIH_MUST (nih_str_array_add (&foo->array, NULL, &len, "\"'$*&()[]{}-_=+/?@':;>.<,~#"));
	NIH_MUST (nih_str_array_add (&foo->array, NULL, &len, ""));

	TEST_TRUE (state_set_json_str_array_from_obj (json, foo, array));
	TEST_TRUE (state_get_json_str_array_to_obj (json, new_foo, array));

	/* count elements */
	new_len = 0;
	for (char **p = new_foo->array; p && *p; p++, new_len++)
		;

	ret = TEST_CMP_STR_ARRAYS (foo->array, new_foo->array, len, new_len);
	TEST_EQ (ret, 0);

	json_object_put (json);

	/*******************************/
	TEST_FEATURE ("empty object env array serialisation and deserialisation");

	json = json_object_new_object ();
	TEST_NE_P (json, NULL);

	foo = nih_new (NULL, Foo);
	TEST_NE_P (foo, NULL);
	memset (foo, '\0', sizeof (Foo));

	new_foo = nih_new (NULL, Foo);
	TEST_NE_P (new_foo, NULL);
	memset (new_foo, '\0', sizeof (Foo));

	foo->env = nih_str_array_new (foo);
	TEST_NE_P (foo->env, NULL);

	len = 0;

	TEST_TRUE (state_set_json_str_array_from_obj (json, foo, env));
	TEST_TRUE (state_get_json_env_array_to_obj (json, new_foo, env));

	/* count elements */
	new_len = 0;
	for (char **p = new_foo->env; p && *p; p++, new_len++)
		;

	ret = TEST_CMP_STR_ARRAYS (foo->env, new_foo->env, len, new_len);
	TEST_EQ (ret, 0);
	TEST_EQ_P (new_foo->env, NULL);

	json_object_put (json);

	/*******************************/
	TEST_FEATURE ("object env array serialisation and deserialisation");

	json = json_object_new_object ();
	TEST_NE_P (json, NULL);

	foo = nih_new (NULL, Foo);
	TEST_NE_P (foo, NULL);
	memset (foo, '\0', sizeof (Foo));

	new_foo = nih_new (NULL, Foo);
	TEST_NE_P (new_foo, NULL);
	memset (new_foo, '\0', sizeof (Foo));

	foo->env = nih_str_array_new (foo);
	TEST_NE_P (foo->env, NULL);

	new_foo->env = NULL;

	len = 0;
	TEST_TRUE (environ_add (&foo->env, NULL, &len, TRUE, "hello=world"));
	TEST_TRUE (environ_add (&foo->env, NULL, &len, TRUE, "foo="));
	TEST_TRUE (environ_add (&foo->env, NULL, &len, TRUE, "bar=123"));
	TEST_TRUE (environ_add (&foo->env, NULL, &len, TRUE, "baz=\'two words\'"));

	TEST_TRUE (state_set_json_str_array_from_obj (json, foo, env));
	TEST_TRUE (state_get_json_env_array_to_obj (json, new_foo, env));

	/* count elements */
	new_len = 0;
	for (char **p = new_foo->env; p && *p; p++, new_len++)
		;

	ret = TEST_CMP_STR_ARRAYS (foo->env, new_foo->env, len, new_len);
	TEST_EQ (ret, 0);
	json_object_put (json);

	/*******************************/
}

void
test_clean_args (void)
{
	nih_local char **args = NULL;
	size_t           len = 0;

	TEST_FUNCTION ("clean_args");

	/*******************************/
	TEST_FEATURE ("no arguments");

	len = 0;
	args = nih_str_array_new (NULL);
	TEST_NE_P (args, NULL);
	TEST_EQ_P (args[0], NULL);

	clean_args (&args);

	TEST_EQ_P (args[0], NULL);

	/*******************************/
	TEST_FEATURE ("1 argument");

	len = 0;
	args = nih_str_array_new (NULL);
	TEST_NE_P (args, NULL);

	NIH_MUST (nih_str_array_add (&args, NULL, &len, "/sbin/init"));

	clean_args (&args);

	TEST_EQ_STR (args[0], "/sbin/init");
	TEST_EQ_P (args[1], NULL);

	/*******************************/
	TEST_FEATURE ("non-cleanable arguments");

	len = 0;
	args = nih_str_array_new (NULL);
	TEST_NE_P (args, NULL);

	NIH_MUST (nih_str_array_add (&args, NULL, &len, "/sbin/init"));
	NIH_MUST (nih_str_array_add (&args, NULL, &len, "--no-startup-event"));

	clean_args (&args);

	TEST_EQ_STR (args[0], "/sbin/init");
	TEST_EQ_STR (args[1], "--no-startup-event");
	TEST_EQ_P (args[2], NULL);

	/*******************************/
	TEST_FEATURE ("mostly cleanable arguments");

	len = 0;
	args = nih_str_array_new (NULL);
	TEST_NE_P (args, NULL);

	NIH_MUST (nih_str_array_add (&args, NULL, &len, "/sbin/init"));
	NIH_MUST (nih_str_array_add (&args, NULL, &len, "--debug"));

	clean_args (&args);

	TEST_EQ_STR (args[0], "/sbin/init");
	TEST_EQ_P (args[1], NULL);

	/*******************************/
	TEST_FEATURE ("only cleanable arguments");

	len = 0;
	args = nih_str_array_new (NULL);
	TEST_NE_P (args, NULL);

	NIH_MUST (nih_str_array_add (&args, NULL, &len, "--verbose"));
	NIH_MUST (nih_str_array_add (&args, NULL, &len, "--state-fd"));
	NIH_MUST (nih_str_array_add (&args, NULL, &len, "9999"));
	NIH_MUST (nih_str_array_add (&args, NULL, &len, "--debug"));

	clean_args (&args);

	TEST_EQ_P (args[0], NULL);

	/*******************************/
	TEST_FEATURE ("lots of arguments");

	len = 0;
	args = nih_str_array_new (NULL);
	TEST_NE_P (args, NULL);

	NIH_MUST (nih_str_array_add (&args, NULL, &len, "/sbin/init"));
	NIH_MUST (nih_str_array_add (&args, NULL, &len, "--debug"));
	NIH_MUST (nih_str_array_add (&args, NULL, &len, "--logdir"));
	NIH_MUST (nih_str_array_add (&args, NULL, &len, "/var/log/upstart"));
	NIH_MUST (nih_str_array_add (&args, NULL, &len, "--state-fd"));
	NIH_MUST (nih_str_array_add (&args, NULL, &len, "7"));
	NIH_MUST (nih_str_array_add (&args, NULL, &len, "--state-fd"));
	NIH_MUST (nih_str_array_add (&args, NULL, &len, "3"));
	NIH_MUST (nih_str_array_add (&args, NULL, &len, "--state-fd"));
	NIH_MUST (nih_str_array_add (&args, NULL, &len, "123"));
	NIH_MUST (nih_str_array_add (&args, NULL, &len, "--verbose"));
	NIH_MUST (nih_str_array_add (&args, NULL, &len, "--confdir"));
	NIH_MUST (nih_str_array_add (&args, NULL, &len, "/etc/init"));
	NIH_MUST (nih_str_array_add (&args, NULL, &len, "--debug"));
	NIH_MUST (nih_str_array_add (&args, NULL, &len, "--debug"));
	NIH_MUST (nih_str_array_add (&args, NULL, &len, "--debug"));
	NIH_MUST (nih_str_array_add (&args, NULL, &len, "--state-fd"));
	NIH_MUST (nih_str_array_add (&args, NULL, &len, "123"));

	clean_args (&args);

	TEST_EQ_STR (args[0], "/sbin/init");
	TEST_EQ_STR (args[1], "--logdir");
	TEST_EQ_STR (args[2], "/var/log/upstart");
	TEST_EQ_STR (args[3], "--confdir");
	TEST_EQ_STR (args[4], "/etc/init");
	TEST_EQ_P (args[5], NULL);

	/*******************************/
}

/**
 * test_upgrade:
 * 
 * Run tests that simulate an upgrade by attempting to deserialise an
 * older version of the JSON data format than is currently used.
 **/
void
test_upgrade (void)
{
	TestDataFile  *datafile;

	TEST_GROUP ("upgrade tests");

	for (datafile = test_data_files; datafile && datafile->filename; datafile++) {
		nih_local char  *path = NULL;
		nih_local char  *name = NULL;

		nih_assert (datafile->func != NULL);

		name = NIH_MUST (nih_sprintf (NULL, "with data file '%s'",
					datafile->filename));
		TEST_FEATURE (name);

		/* Potentially recreate the lists and hashes here which
		 * allows individual tests to free them and assign to
		 * NULL.
		 */
		conf_init ();
		session_init ();
		event_init ();
		control_init ();
		job_class_init ();
		job_class_environment_clear ();

		path = NIH_MUST (nih_sprintf (NULL, "%s/%s",
					TEST_DATA_DIR, datafile->filename));

		/* Ensure environment is clean before test is run */
		ensure_env_clean ();

		datafile->func (path);
	}

	/* Call again to make sure last test left environment sane */
	ensure_env_clean ();
}

/**
 * test_upstart1_6_upgrade:
 *
 * @path: full path to JSON data file to deserialise.
 *
 * Test for original Upstart 1.6 serialisation data format containing
 * a blocked object that does not contain a 'session' element.
 *
 * Note that this test is NOT testing whether a JobClass with an
 * associated Upstart session is handled correctly, it is merely
 * testing that a JobClass with the NULL session encoded in the JSON
 * is handled correctly.
 **/
void
test_upstart1_6_upgrade (const char *path)
{
	nih_local char  *json_string = NULL;
	Event           *event;
	struct stat      statbuf;
	size_t           len;

	nih_assert (path);

	conf_init ();
	session_init ();
	event_init ();
	control_init ();
	job_class_init ();

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_EMPTY (events);
	TEST_LIST_EMPTY (conf_sources);
	TEST_HASH_EMPTY (job_classes);

	/* Check data file exists */
	TEST_EQ (stat (path, &statbuf), 0);

	json_string = nih_file_read (NULL, path, &len);
	TEST_NE_P (json_string, NULL);

	/* Recreate state from JSON data file */
	assert0 (state_from_string (json_string));

	/* ConfSource and ConfFile objects not serialised in this
	 * version.
	 */
	TEST_LIST_EMPTY (conf_sources);

	TEST_LIST_NOT_EMPTY (events);
	TEST_HASH_NOT_EMPTY (job_classes);
	TEST_LIST_EMPTY (sessions);

	event = (Event *)nih_list_remove (events->next);
	TEST_NE_P (event, NULL);
	TEST_EQ_STR (event->name, "Christmas");

	NIH_HASH_FOREACH (job_classes, iter) {
		JobClass *class = (JobClass *)iter;

		TEST_EQ_STR (class->name, "bar");
		TEST_EQ_STR (class->path, "/com/ubuntu/Upstart/jobs/bar");
		TEST_HASH_NOT_EMPTY (class->instances);

		NIH_HASH_FOREACH (class->instances, iter2) {
			Blocked        *blocked;
			Job            *job = (Job *)iter2;
			nih_local char *instance_path = NULL;

			/* instance name */
			TEST_EQ_STR (job->name, "");

			instance_path = NIH_MUST (nih_sprintf (NULL, "%s/_", class->path));
			TEST_EQ_STR (job->path, instance_path);

			/* job is blocked by the event */
			TEST_EQ (job->blocker, event);

			/* First entry in list should be a Blocked
			 * object pointing to the job.
			 */
			TEST_LIST_NOT_EMPTY (&event->blocking);
			blocked = (Blocked *)(&event->blocking)->next;
			TEST_EQ (blocked->type, BLOCKED_JOB);
			TEST_EQ (blocked->job, job);
		}
	}

	nih_free (event);
	nih_free (conf_sources);
	nih_free (job_classes);

	events = NULL;
	conf_sources = NULL;
	job_classes = NULL;

	event_init ();
	conf_init ();
	job_class_init ();
}

/**
 * test_session_upgrade_midflight
 *
 * @path: full path to JSON data file to deserialise.
 *
 * Test for re-exec with an active and existing chroot session.
 *
 **/
void
test_session_upgrade_midflight (const char *path)
{
	nih_local char  *json_string = NULL;
	struct stat      statbuf;
	size_t           len;
	int              got_tty1 = FALSE;

	nih_assert (path);

	conf_init ();
	session_init ();
	event_init ();
	control_init ();
	job_class_init ();

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_EMPTY (events);
	TEST_LIST_EMPTY (conf_sources);
	TEST_HASH_EMPTY (job_classes);

	/* Check data file exists */
	TEST_EQ (stat (path, &statbuf), 0);

	json_string = nih_file_read (NULL, path, &len);
	TEST_NE_P (json_string, NULL);

	/* Recreate state from JSON data file */
	assert0 (state_from_string (json_string));

	TEST_LIST_NOT_EMPTY (conf_sources);
	TEST_LIST_NOT_EMPTY (events);
	TEST_HASH_NOT_EMPTY (job_classes);
	TEST_LIST_NOT_EMPTY (sessions);

	int session_count = 0;
	NIH_LIST_FOREACH (sessions, iter) {
		Session *session = (Session *)iter;
		TEST_EQ_STR (session->chroot, "/mnt");
		TEST_EQ_STR (session->conf_path, "/mnt/etc/init");
		session_count++;
	}
	TEST_EQ (session_count, 1);

	int source_types[3] = {0, 0, 0};

	NIH_LIST_FOREACH (conf_sources, iter) {
		ConfSource *source = (ConfSource *) iter;
		if (! source->session) {
			switch (source->type) {
			case CONF_FILE:
				source_types[0]++;
				break;
			case CONF_JOB_DIR:
				source_types[1]++;
				break;
			default:
				nih_assert_not_reached ();
			}
		} else {
			switch (source->type) {
			case CONF_JOB_DIR:
				source_types[2]++;
				break;
			default:
				nih_assert_not_reached ();
			}
		}
	}
	TEST_EQ (source_types[0], 1);
	TEST_EQ (source_types[1], 1);
	TEST_EQ (source_types[2], 1);

	NIH_HASH_FOREACH (job_classes, iter) {
		JobClass *class = (JobClass *)iter;
		TEST_EQ_P (class->session, NULL);
		if (! strcmp (class->name, "tty1"))
			got_tty1 = TRUE;
	}

	/* XXX: The json contains 2 tty1 jobs: one in the NULL session,
	 * and the other in the chroot session.
	 *
	 * Make sure that duplicate job names (albeit in different sessions)
	 * do not stop the NULL session job from being recreated.
	 */
	TEST_EQ (got_tty1, TRUE);

	nih_free (conf_sources);
	nih_free (job_classes);
	nih_free (events);
	nih_free (sessions);

	conf_sources = NULL;
	job_classes = NULL;
	events = NULL;
	sessions = NULL;

	conf_init ();
	job_class_init ();
	event_init ();
	session_init ();
}


/**
 * test_session_upgrade_exists
 *
 * @path: full path to JSON data file that needs pre-processing.
 *
 * XXX: @path contains multiple occurences of @CHROOT_PATH@ which must
 * be replaced by a valid temporary directory path before attempting
 * deserialisation.
 *
 * Test to ensure Upstart can deserialise a state file containing a
 * chroot session where that chroot path actually exists with jobs on disk.
 * This was added since that exact scenario caught a bug in an early fix
 * for LP:#1199778 (resulting in stateless re-exec).
 **/
void
test_session_upgrade_exists (const char *original_path)
{
	nih_local char  *json_string = NULL;
	struct stat      statbuf;
	size_t           len;
	int              got_tty1 = FALSE;
	char             chroot_path[PATH_MAX];
	nih_local char  *path = NULL;
	nih_local char  *file = NULL;
	nih_local char  *processed_json = NULL;
	mode_t           old_perms;

	nih_assert (original_path);

	conf_init ();
	session_init ();
	event_init ();
	control_init ();
	job_class_init ();

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_EMPTY (events);
	TEST_LIST_EMPTY (conf_sources);
	TEST_HASH_EMPTY (job_classes);

	/* Check data file exists */
	TEST_EQ (stat (original_path, &statbuf), 0);

	/* Read the original file */
	file = nih_file_read (NULL, original_path, &len);
	TEST_NE_P (file, NULL);

	old_perms = umask (0);

	TEST_FILENAME (chroot_path);
	assert0 (mkdir (chroot_path, 0755));

	path = NIH_MUST (nih_sprintf (NULL, "%s/etc", chroot_path));
	assert0 (mkdir (path, 0755));
	path = NIH_MUST (nih_sprintf (NULL, "%s/etc/init", chroot_path));
	assert0 (mkdir (path, 0755));

	/* Replace @CHROOT_PATH@ with our temporary path */
	processed_json = search_and_replace (NULL, file, "@CHROOT_PATH@", chroot_path);
	TEST_NE_P (processed_json, NULL);

	/* Create some jobs which are also specified in the original
	 * JSON state data.
	 */
	CREATE_FILE (path, "tty1.conf", "manual");
	CREATE_FILE (path, "tty2.conf", "manual");

	/* Recreate state from JSON data file */
	assert0 (state_from_string (processed_json));

	TEST_LIST_NOT_EMPTY (conf_sources);
	TEST_LIST_NOT_EMPTY (events);
	TEST_HASH_NOT_EMPTY (job_classes);
	TEST_LIST_NOT_EMPTY (sessions);

	int session_count = 0;
	NIH_LIST_FOREACH (sessions, iter) {
		Session *session = (Session *)iter;
		nih_local char *new_path = NULL;

		/* yes, there is a double-slash in this path */
		new_path = nih_sprintf (NULL, "%s/%s", chroot_path,
				"/etc/init");

		TEST_EQ_STR (session->chroot, chroot_path);
		TEST_EQ_STR (session->conf_path, new_path);
		session_count++;
	}
	TEST_EQ (session_count, 1);

	int source_types[3] = {0, 0, 0};

	NIH_LIST_FOREACH (conf_sources, iter) {
		ConfSource *source = (ConfSource *) iter;

		if (! source->session) {
			switch (source->type) {
			case CONF_FILE:
				source_types[0]++;
				break;
			case CONF_JOB_DIR:
				source_types[1]++;
				break;
			default:
				nih_assert_not_reached ();
			}
		} else {
			switch (source->type) {
			case CONF_JOB_DIR:
				source_types[2]++;
				break;
			default:
				nih_assert_not_reached ();
			}
		}
	}
	TEST_EQ (source_types[0], 1);
	TEST_EQ (source_types[1], 1);
	TEST_EQ (source_types[2], 1);

	NIH_HASH_FOREACH (job_classes, iter) {
		JobClass *class = (JobClass *)iter;

		TEST_EQ_P (class->session, NULL);
		if (! strcmp (class->name, "tty1"))
			got_tty1 = TRUE;
	}

	/* XXX: The json contains 2 tty1 jobs: one in the NULL session,
	 * and the other in the chroot session.
	 *
	 * Make sure that duplicate job names (albeit in different sessions)
	 * do not stop the NULL session job from being recreated.
	 */
	TEST_EQ (got_tty1, TRUE);

	nih_free (conf_sources);
	nih_free (job_classes);
	nih_free (events);
	nih_free (sessions);

	conf_sources = NULL;
	job_classes = NULL;
	events = NULL;
	sessions = NULL;

	conf_init ();
	job_class_init ();
	event_init ();
	session_init ();

	DELETE_FILE (path, "tty1.conf");
	DELETE_FILE (path, "tty2.conf");

	path = NIH_MUST (nih_sprintf (NULL, "%s/etc/init", chroot_path));
	assert0 (rmdir (path));

	path = NIH_MUST (nih_sprintf (NULL, "%s/etc", chroot_path));
	assert0 (rmdir (path));

	assert0 (rmdir (chroot_path));

	/* Restore */
	umask (old_perms);
}

/**
 * test_session_upgrade_stale
 *
 * @path: full path to JSON data file that needs pre-processing.
 *
 * Test to ensure Upstart can deserialise a state file containing a
 * stale chroot session which is long gone.  This was added since
 * updated scenario caught a bug in a bug fix to an early fix for
 * LP:#1199778 (resulting in stateless re-exec).
 *
 **/
void
test_session_upgrade_stale (const char *path)
{
	nih_local char  *json_string = NULL;
	struct stat      statbuf;
	size_t           len;
	int              got_tty1 = FALSE;

	nih_assert (path);

	conf_init ();
	session_init ();
	event_init ();
	control_init ();
	job_class_init ();

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_EMPTY (events);
	TEST_LIST_EMPTY (conf_sources);
	TEST_HASH_EMPTY (job_classes);

	/* Check data file exists */
	TEST_EQ (stat (path, &statbuf), 0);

	json_string = nih_file_read (NULL, path, &len);
	TEST_NE_P (json_string, NULL);

	/* Recreate state from JSON data file */
	assert0 (state_from_string (json_string));

	TEST_LIST_NOT_EMPTY (conf_sources);
	TEST_LIST_NOT_EMPTY (events);
	TEST_HASH_NOT_EMPTY (job_classes);
	TEST_LIST_NOT_EMPTY (sessions);

	int session_count = 0;
	NIH_LIST_FOREACH (sessions, iter) {
		Session *session = (Session *)iter;
		if (session_count == 0) {
			TEST_EQ_STR (session->chroot, "/var/lib/schroot/mount/raring-amd64-9684b2e0-c5c2-49f0-9baf-7eddd72f2482");
			TEST_EQ_STR (session->conf_path, "/var/lib/schroot/mount/raring-amd64-9684b2e0-c5c2-49f0-9baf-7eddd72f2482/etc/init");
		} else {
			TEST_EQ_STR (session->chroot, "/var/lib/schroot/mount/saucy-amd64-4c0015a8-7e99-4d1b-8453-557a82aff76f");
			TEST_EQ_STR (session->conf_path, "/var/lib/schroot/mount/saucy-amd64-4c0015a8-7e99-4d1b-8453-557a82aff76f/etc/init");
		}
		session_count++;
	}
	TEST_EQ (session_count, 2);

	int source_types[3] = {0, 0, 0};

	NIH_LIST_FOREACH (conf_sources, iter) {
		ConfSource *source = (ConfSource *) iter;
		if (! source->session) {
			switch (source->type) {
			case CONF_FILE:
				source_types[0]++;
				break;
			case CONF_JOB_DIR:
				source_types[1]++;
				break;
			default:
				nih_assert_not_reached ();
			}
		} else {
			switch (source->type) {
			case CONF_JOB_DIR:
				source_types[2]++;
				break;
			default:
				nih_assert_not_reached ();
			}
		}
	}
	TEST_EQ (source_types[0], 1);
	TEST_EQ (source_types[1], 1);
	TEST_EQ (source_types[2], 2);

	NIH_HASH_FOREACH (job_classes, iter) {
		JobClass *class = (JobClass *)iter;
		TEST_EQ_P (class->session, NULL);
		if (! strcmp (class->name, "tty1"))
			got_tty1 = TRUE;
	}

	/* XXX: The json contains 2 tty1 jobs: one in the NULL session,
	 * and the other in the chroot session.
	 *
	 * Make sure that duplicate job names (albeit in different sessions)
	 * do not stop the NULL session job from being recreated.
	 */
	TEST_EQ (got_tty1, TRUE);

	nih_free (conf_sources);
	nih_free (job_classes);
	nih_free (events);
	nih_free (sessions);

	conf_sources = NULL;
	job_classes = NULL;
	events = NULL;
	sessions = NULL;

	conf_init ();
	job_class_init ();
	event_init ();
	session_init ();
}


/**
 * test_upstart1_8_upgrade:
 *
 * @path: full path to JSON data file to deserialise.
 *
 * Test for Upstart 1.8 serialisation data format where start_on and
 * stop_on conditions were encoded as strings, not EventOperators.
 **/
void
test_upstart1_8_upgrade (const char *path)
{
	nih_local char  *json_string = NULL;
	struct stat      statbuf;
	size_t           len;

	nih_assert (path);

	conf_init ();
	session_init ();
	event_init ();
	control_init ();
	job_class_init ();

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_EMPTY (events);
	TEST_LIST_EMPTY (conf_sources);
	TEST_HASH_EMPTY (job_classes);

	/* Check data file exists */
	TEST_EQ (stat (path, &statbuf), 0);

	json_string = nih_file_read (NULL, path, &len);
	TEST_NE_P (json_string, NULL);

	/* Recreate state from JSON data file */
	assert0 (state_from_string (json_string));

	/* ConfSource and ConfFile objects not serialised in this
	 * version.
	 */
	TEST_LIST_EMPTY (conf_sources);

	TEST_LIST_NOT_EMPTY (events);
	TEST_HASH_NOT_EMPTY (job_classes);
	TEST_LIST_EMPTY (sessions);

	nih_free (events);
	nih_free (conf_sources);
	nih_free (job_classes);

	events = NULL;
	conf_sources = NULL;
	job_classes = NULL;

	event_init ();
	conf_init ();
	job_class_init ();
}

/**
 * test_upstart_pre_security_upgrade:
 *
 * @path: full path to JSON data file to deserialise.
 *
 * Test for Upstart pre-security serialisation data format that doesn't
 * contain apparmor_switch element, and PROCESS_SECURITY.
 *
 **/
void
test_upstart_pre_security_upgrade (const char *path)
{
	nih_local char  *json_string = NULL;
	Event           *event;
	ConfSource      *source;
	ConfFile        *file;
	nih_local char  *conf_file_path = NULL;
	struct stat      statbuf;
	size_t           len;

	nih_assert (path);

	conf_init ();
	session_init ();
	event_init ();
	control_init ();
	job_class_init ();

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_EMPTY (events);
	TEST_LIST_EMPTY (conf_sources);
	TEST_HASH_EMPTY (job_classes);

	/* Check data file exists */
	TEST_EQ (stat (path, &statbuf), 0);

	json_string = nih_file_read (NULL, path, &len);
	TEST_NE_P (json_string, NULL);

	/* Recreate state from JSON data file */
	assert0 (state_from_string (json_string));

	TEST_LIST_NOT_EMPTY (events);
	TEST_HASH_NOT_EMPTY (job_classes);
	TEST_LIST_EMPTY (sessions);

	TEST_LIST_EMPTY (conf_sources);

	/* Create the ConfSource and ConfFile objects to simulate
	 * Upstart reading /etc/init on startup. Required since we
	 * don't currently serialise these objects.
	 */
	source = conf_source_new (NULL, "/tmp/security", CONF_JOB_DIR);
	TEST_NE_P (source, NULL);

	conf_file_path = NIH_MUST (nih_sprintf (NULL, "%s/%s",
				"/tmp/security", "security"));

	file = conf_file_new (source, conf_file_path);
	TEST_NE_P (file, NULL);

	TEST_LIST_NOT_EMPTY (conf_sources);

	event = (Event *)nih_list_remove (events->next);
	TEST_NE_P (event, NULL);
	TEST_EQ_STR (event->name, "Christmas");

	NIH_HASH_FOREACH (job_classes, iter) {
		JobClass *class = (JobClass *)iter;

		TEST_EQ_STR (class->name, "security");
		TEST_EQ_STR (class->path, "/com/ubuntu/Upstart/jobs/security");
		TEST_EQ_P (class->apparmor_switch, NULL);
		TEST_HASH_NOT_EMPTY (class->instances);

		TEST_EQ_P (class->process[PROCESS_SECURITY], NULL);

		TEST_FALSE (class->process[PROCESS_MAIN]->script);
		TEST_FALSE (class->process[PROCESS_PRE_START]->script);
		TEST_FALSE (class->process[PROCESS_POST_START]->script);
		TEST_FALSE (class->process[PROCESS_PRE_STOP]->script);
		TEST_FALSE (class->process[PROCESS_POST_STOP]->script);

		TEST_EQ_STR (class->process[PROCESS_MAIN]->command, "a");
		TEST_EQ_STR (class->process[PROCESS_PRE_START]->command, "b");
		TEST_EQ_STR (class->process[PROCESS_POST_START]->command, "c");
		TEST_EQ_STR (class->process[PROCESS_PRE_STOP]->command, "d");
		TEST_EQ_STR (class->process[PROCESS_POST_STOP]->command, "e");

		NIH_HASH_FOREACH (class->instances, iter2) {
			Job            *job = (Job *)iter2;
			nih_local char *instance_path = NULL;

			/* instance name */
			TEST_EQ_STR (job->name, "");

			instance_path = NIH_MUST (nih_sprintf (NULL, "%s/_", class->path));
			TEST_EQ_STR (job->path, instance_path);

			TEST_EQ (job->pid[PROCESS_MAIN], 10);
			TEST_EQ (job->pid[PROCESS_PRE_START], 11);
			TEST_EQ (job->pid[PROCESS_POST_START], 12);
			TEST_EQ (job->pid[PROCESS_PRE_STOP], 13);
			TEST_EQ (job->pid[PROCESS_POST_STOP], 14);
			TEST_EQ (job->pid[PROCESS_SECURITY], 0);

			TEST_EQ_P (job->log[PROCESS_MAIN], NULL);
			TEST_EQ_P (job->log[PROCESS_PRE_START], NULL);
			TEST_EQ_P (job->log[PROCESS_POST_START], NULL);
			TEST_EQ_P (job->log[PROCESS_PRE_STOP], NULL);
			TEST_EQ_P (job->log[PROCESS_POST_STOP], NULL);
			TEST_EQ_P (job->log[PROCESS_SECURITY], NULL);

		}
	}

	nih_free (event);
	nih_free (conf_sources);
	conf_sources = NULL;
	nih_free (job_classes);
	job_classes = NULL;
}

/**
 * test_upstart_with_apparmor_upgrade:
 *
 * @path: full path to JSON data file to deserialise.
 *
 * Test that Upstart is able to deserialise the 1.8-format JSON with the
 * addition of the apparmor meta-data but _without_ the full serialisation
 * data (including complete EventOperator data for
 * JobClass->[start|stop]_on objects).
 **/
void
test_upstart_with_apparmor_upgrade (const char *path)
{
	nih_local char   *json_string = NULL;
	struct stat       statbuf;
	size_t            len;
	json_object      *json = NULL;
	json_object      *json_job_classes = NULL;
	json_object      *json_job_class = NULL;
	json_object      *json_job_class_start_on = NULL;
	json_object      *json_job_class_process = NULL;
	json_object      *json_jobs = NULL;
	json_object      *json_job = NULL;
	json_object      *json_job_pids = NULL;
	json_object      *json_job_logs = NULL;
	size_t            count;

	/*
	PROCESS_MAIN,
	PROCESS_PRE_START,
	PROCESS_POST_START,
	PROCESS_PRE_STOP,
	PROCESS_POST_STOP,
	PROCESS_SECURITY
	*/
	size_t            expected_count = 6;

	nih_assert (path);

	conf_init ();
	session_init ();
	event_init ();
	control_init ();
	job_class_init ();

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_EMPTY (events);
	TEST_LIST_EMPTY (conf_sources);
	TEST_HASH_EMPTY (job_classes);

	/* Check data file exists */
	TEST_EQ (stat (path, &statbuf), 0);

	json_string = nih_file_read (NULL, path, &len);
	TEST_NE_P (json_string, NULL);

	/* Recreate state from JSON data file */
	assert0 (state_from_string (json_string));

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_NOT_EMPTY (events);
	TEST_HASH_NOT_EMPTY (job_classes);
	TEST_LIST_EMPTY (conf_sources);

	/* Re-read the json, checking for expected content */
	json = json_object_from_file (path);
	TEST_NE_P (json, NULL);

	TEST_TRUE (json_object_object_get_ex (json, "job_classes", &json_job_classes));
	TEST_NE_P (json_job_classes, NULL);

	TEST_TRUE (state_check_json_type (json_job_classes, array));

	/* Look at first JobClass */
	json_job_class = json_object_array_get_idx (json_job_classes, 0);
	TEST_NE_P (json_job_class, NULL);
	TEST_TRUE (state_check_json_type (json_job_class, object));

	/* Check to ensure we are dealing with the old serialisation
	 * format where the 'start on' condition was encoded as a
	 * string.
	 */
	TEST_TRUE (json_object_object_get_ex (json_job_class, "start_on", &json_job_class_start_on));
	TEST_NE_P (json_job_class_start_on, NULL);
	TEST_TRUE (state_check_json_type (json_job_class_start_on, string));

	/* Check to ensure the JobClass contains the expected apparmor field */
	TEST_TRUE (json_object_object_get_ex (json_job_class, "apparmor_switch", NULL));

	TEST_TRUE (json_object_object_get_ex (json_job_class, "process", &json_job_class_process));
	TEST_NE_P (json_job_class_process, NULL);
	TEST_TRUE (state_check_json_type (json_job_class_process, array));

	count = json_object_array_length (json_job_class_process);
	TEST_EQ (count, expected_count);

	TEST_TRUE (json_object_object_get_ex (json_job_class, "jobs", &json_jobs));
	TEST_NE_P (json_jobs, NULL);

	TEST_TRUE (state_check_json_type (json_jobs, array));

	/* Look at first Job */
	json_job = json_object_array_get_idx (json_jobs, 0);
	TEST_NE_P (json_job, NULL);
	TEST_TRUE (state_check_json_type (json_job, object));

	/* Check size of Job->pid array is as expected */
	TEST_TRUE (json_object_object_get_ex (json_job, "pid", &json_job_pids));
	TEST_NE_P (json_job_pids, NULL);
	TEST_TRUE (state_check_json_type (json_job_pids, array));

	count = json_object_array_length (json_job_pids);
	TEST_EQ (count, expected_count);

	/* Check size of Job->log array is as expected */
	TEST_TRUE (json_object_object_get_ex (json_job, "log", &json_job_logs));
	TEST_NE_P (json_job_logs, NULL);
	TEST_TRUE (state_check_json_type (json_job_logs, array));

	count = json_object_array_length (json_job_logs);
	TEST_EQ (count, expected_count);

	nih_free (job_classes);
	job_classes = NULL;

	nih_free (events);
	events = NULL;
}

/**
 * test_upstart_full_serialise_without_apparmor_upgrade:
 *
 * @path: full path to JSON data file to deserialise.
 *
 * Test that Upstart is able to deserialise the 1.8-format JSON with the
 * addition of the full serialisation (including complete EventOperator
 * data for JobClass->[start|stop]_on objects) but _without_ the apparmor
 * meta-data.
 **/
void
test_upstart_full_serialise_without_apparmor_upgrade (const char *path)
{
	nih_local char   *json_string = NULL;
	struct stat       statbuf;
	size_t            len;
	json_object      *json = NULL;
	json_object      *json_job_classes = NULL;
	json_object      *json_job_class = NULL;
	json_object      *json_job_class_stop_on = NULL;
	json_object      *json_job_class_process = NULL;
	json_object      *json_job_class_stop_on_node = NULL;
	json_object      *json_jobs = NULL;
	json_object      *json_job = NULL;
	json_object      *json_job_pids = NULL;
	json_object      *json_job_logs = NULL;
	size_t            count;

	/*
	PROCESS_MAIN,
	PROCESS_PRE_START,
	PROCESS_POST_START,
	PROCESS_PRE_STOP,
	PROCESS_POST_STOP,
	*/
	size_t            expected_count = 5;

	nih_assert (path);

	conf_init ();
	session_init ();
	event_init ();
	control_init ();
	job_class_init ();

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_EMPTY (events);
	TEST_LIST_EMPTY (conf_sources);
	TEST_HASH_EMPTY (job_classes);

	/* Check data file exists */
	TEST_EQ (stat (path, &statbuf), 0);

	json_string = nih_file_read (NULL, path, &len);
	TEST_NE_P (json_string, NULL);

	/* Recreate state from JSON data file */
	assert0 (state_from_string (json_string));

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_NOT_EMPTY (events);
	TEST_HASH_NOT_EMPTY (job_classes);

	/* Full serialisation, so we expect some ConfSource objects */
	TEST_LIST_NOT_EMPTY (conf_sources);

	/* Re-read the json, checking for expected content */
	json = json_object_from_file (path);
	TEST_NE_P (json, NULL);

	TEST_TRUE (json_object_object_get_ex (json, "job_classes", &json_job_classes));
	TEST_NE_P (json_job_classes, NULL);

	TEST_TRUE (state_check_json_type (json_job_classes, array));

	/* Look at 2nd JobClass */
	TEST_GE (json_object_array_length (json_job_classes), 2);
	json_job_class = json_object_array_get_idx (json_job_classes, 1);
	TEST_NE_P (json_job_class, NULL);
	TEST_TRUE (state_check_json_type (json_job_class, object));

	/* Check to ensure we are dealing with the new serialisation
	 * format where the 'stop on' condition is encoded as a
	 * full EventOperator object.
	 */
	TEST_TRUE (json_object_object_get_ex (json_job_class, "stop_on", &json_job_class_stop_on));
	TEST_NE_P (json_job_class_stop_on, NULL);
	/* EventOperators are serialised as an array of objects
	 * representing the tree.
	 */
	TEST_TRUE (state_check_json_type (json_job_class_stop_on, array));

	/* Look at the first element */
	json_job_class_stop_on_node = json_object_array_get_idx (json_job_class_stop_on, 0);
	TEST_NE_P (json_job_class_stop_on_node, NULL);
	TEST_TRUE (state_check_json_type (json_job_class_stop_on_node, object));

	/* Look for expected EventOperator elements */
	TEST_TRUE (json_object_object_get_ex (json_job_class_stop_on_node, "name", NULL));
	TEST_TRUE (json_object_object_get_ex (json_job_class_stop_on_node, "value", NULL));
	TEST_TRUE (json_object_object_get_ex (json_job_class_stop_on_node, "type", NULL));

	/* Check to ensure the JobClass does NOT contain the apparmor field */
	TEST_FALSE (json_object_object_get_ex (json_job_class, "apparmor_switch", NULL));

	TEST_TRUE (json_object_object_get_ex (json_job_class, "process", &json_job_class_process));
	TEST_NE_P (json_job_class_process, NULL);
	TEST_TRUE (state_check_json_type (json_job_class_process, array));

	count = json_object_array_length (json_job_class_process);
	TEST_EQ (count, expected_count);

	TEST_TRUE (json_object_object_get_ex (json_job_class, "jobs", &json_jobs));
	TEST_NE_P (json_jobs, NULL);

	TEST_TRUE (state_check_json_type (json_jobs, array));

	/* Look at first Job */
	json_job = json_object_array_get_idx (json_jobs, 0);
	TEST_NE_P (json_job, NULL);
	TEST_TRUE (state_check_json_type (json_job, object));

	/* Check size of Job->pid array is as expected */
	TEST_TRUE (json_object_object_get_ex (json_job, "pid", &json_job_pids));
	TEST_NE_P (json_job_pids, NULL);
	TEST_TRUE (state_check_json_type (json_job_pids, array));

	count = json_object_array_length (json_job_pids);
	TEST_EQ (count, expected_count);

	/* Check size of Job->log array is as expected */
	TEST_TRUE (json_object_object_get_ex (json_job, "log", &json_job_logs));
	TEST_NE_P (json_job_logs, NULL);
	TEST_TRUE (state_check_json_type (json_job_logs, array));

	count = json_object_array_length (json_job_logs);
	TEST_EQ (count, expected_count);

	nih_free (conf_sources);
	conf_sources = NULL;

	nih_free (job_classes);
	job_classes = NULL;

	nih_free (events);
	events = NULL;
}

/**
 * test_upstart_full_serialise_with_apparmor_upgrade:
 *
 * @path: full path to JSON data file to deserialise.
 *
 * Test that Upstart is able to deserialise the 1.8-format JSON with the
 * addition of the full serialisation (including complete EventOperator
 * data for JobClass->[start|stop]_on objects) and the apparmor
 * meta-data.
 **/
void
test_upstart_full_serialise_with_apparmor_upgrade (const char *path)
{
	nih_local char   *json_string = NULL;
	struct stat       statbuf;
	size_t            len;
	json_object      *json = NULL;
	json_object      *json_job_classes = NULL;
	json_object      *json_job_class = NULL;
	json_object      *json_job_class_stop_on = NULL;
	json_object      *json_job_class_process = NULL;
	json_object      *json_job_class_stop_on_node = NULL;
	json_object      *json_jobs = NULL;
	json_object      *json_job = NULL;
	json_object      *json_job_pids = NULL;
	json_object      *json_job_logs = NULL;
	size_t            count;

	/*
	PROCESS_MAIN,
	PROCESS_PRE_START,
	PROCESS_POST_START,
	PROCESS_PRE_STOP,
	PROCESS_POST_STOP,
	PROCESS_SECURITY
	*/
	size_t            expected_count = 6;

	nih_assert (path);

	conf_init ();
	session_init ();
	event_init ();
	control_init ();
	job_class_init ();

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_EMPTY (events);
	TEST_LIST_EMPTY (conf_sources);
	TEST_HASH_EMPTY (job_classes);

	/* Check data file exists */
	TEST_EQ (stat (path, &statbuf), 0);

	json_string = nih_file_read (NULL, path, &len);
	TEST_NE_P (json_string, NULL);

	/* Recreate state from JSON data file */
	assert0 (state_from_string (json_string));

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_NOT_EMPTY (events);
	TEST_HASH_NOT_EMPTY (job_classes);

	/* Full serialisation, so we expect some ConfSource objects */
	TEST_LIST_NOT_EMPTY (conf_sources);

	/* Re-read the json, checking for expected content */
	json = json_object_from_file (path);
	TEST_NE_P (json, NULL);

	TEST_TRUE (json_object_object_get_ex (json, "job_classes", &json_job_classes));
	TEST_NE_P (json_job_classes, NULL);

	TEST_TRUE (state_check_json_type (json_job_classes, array));

	/* Look at 2nd JobClass */
	TEST_GE (json_object_array_length (json_job_classes), 2);
	json_job_class = json_object_array_get_idx (json_job_classes, 1);
	TEST_NE_P (json_job_class, NULL);
	TEST_TRUE (state_check_json_type (json_job_class, object));

	/* Check to ensure we are dealing with the new serialisation
	 * format where the 'stop on' condition is encoded as a
	 * full EventOperator object.
	 */
	TEST_TRUE (json_object_object_get_ex (json_job_class, "stop_on", &json_job_class_stop_on));
	TEST_NE_P (json_job_class_stop_on, NULL);
	/* EventOperators are serialised as an array of objects
	 * representing the tree.
	 */
	TEST_TRUE (state_check_json_type (json_job_class_stop_on, array));

	/* Look at the first element */
	json_job_class_stop_on_node = json_object_array_get_idx (json_job_class_stop_on, 0);
	TEST_NE_P (json_job_class_stop_on_node, NULL);
	TEST_TRUE (state_check_json_type (json_job_class_stop_on_node, object));

	/* Look for expected EventOperator elements */
	TEST_TRUE (json_object_object_get_ex (json_job_class_stop_on_node, "name", NULL));
	TEST_TRUE (json_object_object_get_ex (json_job_class_stop_on_node, "value", NULL));
	TEST_TRUE (json_object_object_get_ex (json_job_class_stop_on_node, "type", NULL));

	/* Check to ensure the JobClass contains the expected apparmor field */
	TEST_TRUE (json_object_object_get_ex (json_job_class, "apparmor_switch", NULL));

	TEST_TRUE (json_object_object_get_ex (json_job_class, "process", &json_job_class_process));
	TEST_NE_P (json_job_class_process, NULL);
	TEST_TRUE (state_check_json_type (json_job_class_process, array));

	count = json_object_array_length (json_job_class_process);
	TEST_EQ (count, expected_count);

	TEST_TRUE (json_object_object_get_ex (json_job_class, "jobs", &json_jobs));
	TEST_NE_P (json_jobs, NULL);

	TEST_TRUE (state_check_json_type (json_jobs, array));

	/* Look at first Job */
	json_job = json_object_array_get_idx (json_jobs, 0);
	TEST_NE_P (json_job, NULL);
	TEST_TRUE (state_check_json_type (json_job, object));

	/* Check size of Job->pid array is as expected */
	TEST_TRUE (json_object_object_get_ex (json_job, "pid", &json_job_pids));
	TEST_NE_P (json_job_pids, NULL);
	TEST_TRUE (state_check_json_type (json_job_pids, array));

	count = json_object_array_length (json_job_pids);
	TEST_EQ (count, expected_count);

	/* Check size of Job->log array is as expected */
	TEST_TRUE (json_object_object_get_ex (json_job, "log", &json_job_logs));
	TEST_NE_P (json_job_logs, NULL);
	TEST_TRUE (state_check_json_type (json_job_logs, array));

	count = json_object_array_length (json_job_logs);
	TEST_EQ (count, expected_count);

	nih_free (conf_sources);
	conf_sources = NULL;

	nih_free (job_classes);
	job_classes = NULL;

	nih_free (events);
	events = NULL;
}


/**
 * test_reload_signal_state:
 *
 * @path: full path to JSON data file to deserialise.
 *
 * Test that Upstart is able to deserialise the 1.9-format JSON with the
 * addition of the reload signal stanza.
 **/
void
test_reload_signal_state (const char *path)
{
	nih_local char   *json_string = NULL;
	struct stat       statbuf;
	size_t            len;

	nih_assert (path);

	conf_init ();
	session_init ();
	event_init ();
	control_init ();
	job_class_init ();

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_EMPTY (events);
	TEST_LIST_EMPTY (conf_sources);
	TEST_HASH_EMPTY (job_classes);

	/* Check data file exists */
	TEST_EQ (stat (path, &statbuf), 0);

	json_string = nih_file_read (NULL, path, &len);
	TEST_NE_P (json_string, NULL);

	/* Recreate state from JSON data file */
	assert0 (state_from_string (json_string));

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_NOT_EMPTY (events);
	TEST_HASH_NOT_EMPTY (job_classes);
	TEST_LIST_NOT_EMPTY (conf_sources);

	NIH_HASH_FOREACH (job_classes, iter) {
		JobClass *class = (JobClass *)iter;
		if (strcmp (class->name, "whoopsie") == 0) {
			TEST_EQ (class->reload_signal, 10);
		} else
			TEST_EQ (class->reload_signal, SIGHUP);
	}

	nih_free (conf_sources);
	nih_free (job_classes);
	nih_free (events);
	nih_free (sessions);

	conf_sources = NULL;
	job_classes = NULL;
	events = NULL;
	sessions = NULL;

	conf_init ();
	job_class_init ();
	event_init ();
	session_init ();

}

/**
 * test_job_environ_upgrade:
 *
 * @path: full path to JSON data file to deserialise.
 *
 * Test that Upstart is able to deserialise 1.10-format JSON with
 * the job_environ data.
 **/
void
test_job_environ_upgrade (const char *path)
{
	nih_local char   *json_string = NULL;
	json_object      *json = NULL;
	json_object      *json_value = NULL;
	struct stat       statbuf;
	size_t            len;

	nih_assert (path);

	conf_init ();
	session_init ();
	event_init ();
	control_init ();
	job_class_init ();

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_EMPTY (events);
	TEST_LIST_EMPTY (conf_sources);
	TEST_HASH_EMPTY (job_classes);

	/* Check data file exists */
	TEST_EQ (stat (path, &statbuf), 0);

	json_string = nih_file_read (NULL, path, &len);
	TEST_NE_P (json_string, NULL);

	/* Read the json, checking for expected content */
	json = json_object_from_file (path);
	TEST_NE_P (json, NULL);

	/* Ensure it's there */
	TEST_TRUE (json_object_object_get_ex (json, "job_environment", &json_value));

	/* free the JSON */
	json_object_put (json);

	/* Recreate state from JSON data file */
	assert0 (state_from_string (json_string));

	TEST_LIST_EMPTY (sessions);
	TEST_LIST_NOT_EMPTY (events);
	TEST_HASH_NOT_EMPTY (job_classes);
	TEST_LIST_NOT_EMPTY (conf_sources);

	nih_free (conf_sources);
	nih_free (job_classes);
	nih_free (events);
	nih_free (sessions);

	conf_sources = NULL;
	job_classes = NULL;
	events = NULL;
	sessions = NULL;

	conf_init ();
	job_class_init ();
	event_init ();
	session_init ();
}

/**
 * conf_source_from_path:
 *
 * @path: path to consider,
 * @type: tyoe of ConfSource to check for,
 * @session: session.
 *
 * Look for a ConfSource with path @path, type @type and session
 * @session.
 *
 * Returns: Matching ConfSource or NULL if not found.
 */
ConfSource *
conf_source_from_path (const char *path, ConfSourceType type, const Session *session)
{
	nih_assert (path);

	NIH_LIST_FOREACH (conf_sources, iter) {
		ConfSource *source = (ConfSource *)iter;
		if (! strcmp (source->path, path)
				&& source->type == type && source->session == session)
			return source;
	}

	return NULL;
}

int
main (int   argc,
      char *argv[])
{
	/* run tests in legacy (pre-session support) mode */
	setenv ("UPSTART_NO_SESSIONS", "1", 1);

	/* Modify Upstart's behaviour slightly since it's running under
	 * the test suite.
	 */
	test_basic_types ();
	test_clean_args ();
	test_enums ();
	test_int_arrays ();
	test_string_arrays ();
	test_hex_encoding ();
	test_rlimit_encoding ();
	test_session_serialise ();
	test_process_serialise ();
	test_blocking ();
	test_event_serialise ();
	test_log_serialise ();
	test_job_serialise ();
	test_job_class_serialise ();
	test_upgrade ();

	return 0;
}
