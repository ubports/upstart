/* TODO:
 * - pre-start, post-stop, etc across a re-exec
 */

/* upstart
 *
 * test_state.c - test suite for init/state.c
 *
 * Copyright © 2012 Canonical Ltd.
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
#include <sys/prctl.h>
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
#include "environ.h"
#include "conf.h"
#include "job_class.h"
#include "job.h"
#include "log.h"
#include "blocked.h"
#include "control.h"
#include "test_util.h"


/**
 * AlreadySeen:
 *
 * Used to allow objects that directly or indirectly reference on
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
 * ALREADY_SEEN_SET to ALREADY_SEEN_JOB).
 **/
typedef enum {
	ALREADY_SEEN_SET,
	ALREADY_SEEN_EVENT,
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
	{ 1, "echo hello" },
};
int     rlimit_values[] = { 0, 1, 2, 3, 7, RLIM_INFINITY };

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

	if (obj_num_check (a, b, user))
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
 * blocking_diff
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
 * blocked_diff
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

void
test_session_serialise (void)
{
	json_object  *json;
	json_object  *json_sessions;
	Session      *session1;
	Session      *session2;
	Session      *new_session1;
	Session      *new_session2;
	int           ret;

	session_init ();

	TEST_GROUP ("Session serialisation and deserialisation");

	TEST_LIST_EMPTY (sessions);

	json = json_object_new_object ();
	TEST_NE_P (json, NULL);

	/* Create a couple of sessions */
	session1 = session_new (NULL, "/abc", getuid ());
	TEST_NE_P (session1, NULL);
	session1->conf_path = NIH_MUST (nih_strdup (session1, "/def/ghi"));
	TEST_LIST_NOT_EMPTY (sessions);

	session2 = session_new (NULL, "/foo", 0);
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

	TEST_FEATURE ("Session deserialisation");

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
	nih_local char        *json_string = NULL;
	ConfSource            *source = NULL;
	ConfFile              *file;
	JobClass              *class;
	JobClass              *new_class;
	Job                   *job;
	Job                   *new_job;
	Event                 *event;
	Event                 *new_event;
	Blocked               *blocked;
	size_t                 len;

	conf_init ();
	session_init ();
	event_init ();
	control_init ();
	job_class_init ();

	TEST_GROUP ("Blocked serialisation and deserialisation");

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

	source = conf_source_new (NULL, "/tmp/foo", CONF_JOB_DIR);
	TEST_NE_P (source, NULL);

	file = conf_file_new (source, "/tmp/foo/bar");
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

	/* XXX: We don't remove the source as these are not
	 * recreated on re-exec, so we'll re-use the existing one.
	 */
	nih_list_remove (&event->entry);
	nih_list_remove (&class->entry);

	TEST_HASH_EMPTY (job_classes);
	TEST_LIST_EMPTY (events);
	TEST_LIST_EMPTY (sessions);
	TEST_LIST_NOT_EMPTY (conf_sources);

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

	nih_free (event);

	/* free the event created "on re-exec" */
	nih_free (new_event);
	nih_free (source);
	nih_free (new_class);

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

	source = conf_source_new (NULL, "/tmp/foo", CONF_JOB_DIR);
	TEST_NE_P (source, NULL);

	file = conf_file_new (source, "/tmp/foo/bar");
	TEST_NE_P (file, NULL);
	class = file->job = job_class_new (NULL, "bar", NULL);

	TEST_NE_P (class, NULL);
	TEST_HASH_EMPTY (job_classes);
	TEST_TRUE (job_class_consider (class));
	TEST_HASH_NOT_EMPTY (job_classes);

	job = job_new (class, "");
	TEST_NE_P (job, NULL);
	TEST_HASH_NOT_EMPTY (class->instances);

	blocked = blocked_new (NULL, BLOCKED_EVENT, event);
	TEST_NE_P (blocked, NULL);

	nih_list_add (&job->blocking, &blocked->entry);
	event_block (event);
	TEST_EQ (event->blockers, 1);

	assert0 (state_to_string (&json_string, &len));
	TEST_GT (len, 0);

	nih_list_remove (&event->entry);
	nih_list_remove (&class->entry);

	TEST_HASH_EMPTY (job_classes);
	TEST_LIST_EMPTY (events);
	TEST_LIST_EMPTY (sessions);
	TEST_LIST_NOT_EMPTY (conf_sources);

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

	new_job = (Job *)nih_hash_lookup (new_class->instances, "");
	TEST_NE_P (new_job, NULL);

	assert0 (job_diff (job, new_job, ALREADY_SEEN_SET, TRUE));
	assert0 (job_class_diff (class, new_class, ALREADY_SEEN_SET, TRUE));

	nih_free (event);
	nih_free (new_event);
	nih_free (source);
	nih_free (new_class);

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

	session = session_new (NULL, "/abc", getuid ());
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
	int              fds[2] = { -1 };
	struct stat      statbuf;
	mode_t           old_perms;
	int              status;

	TEST_GROUP ("Log serialisation and deserialisation");

	/*******************************/
	/* XXX: No test for uid > 0 since user logging not currently
	 * XXX: available.
	 */
	TEST_FEATURE ("with uid 0");

	TEST_EQ (openpty (&pty_master, &pty_slave, NULL, NULL, NULL), 0);

	log = log_new (NULL, "/foo", pty_master, 0);
	TEST_NE_P (log, NULL);

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

	TEST_EQ (openpty (&pty_master, &pty_slave, NULL, NULL, NULL), 0);

	/* Provide a log file which is accessible initially */
	log = log_new (NULL, filename, pty_master, 0);
	TEST_NE_P (log, NULL);

	assert0 (pipe (fds));

	TEST_CHILD_WAIT (pid, wait_fd) {
		char   *str = "hello\n";
		char    buf[1];
		size_t  str_len;

		str_len = strlen (str);

		close (fds[1]);
		close (pty_master);

		/* Write initial data */
		ret = write (pty_slave, str, str_len);
		TEST_EQ ((size_t)ret, str_len);

		/* let parent continue */
		TEST_CHILD_RELEASE (wait_fd);

		/* now wait for parent */
		assert (read (fds[0], buf, 1) == 1);

		len = TEST_ARRAY_SIZE (test_data);
		errno = 0;

		/* Now write some data with embedded nulls */
		ret = write (pty_slave, test_data, len);
		TEST_EQ ((size_t)ret, len);

		/* keep child running until the parent is ready (to
		 * simulate a job which continues to run across
		 * a re-exec).
		 */
		pause ();
	}

	close (pty_slave);
	close (fds[0]);

	/* Slurp the childs initial output */
	TEST_FORCE_WATCH_UPDATE ();

	TEST_EQ (stat (filename, &statbuf), 0);

	/* save */
	old_perms = statbuf.st_mode;

	/* Make file inaccessible to ensure data cannot be written
	 * and will thus be added to the unflushed buffer.
	 */
	TEST_EQ (chmod (filename, 0x0), 0);

	/* Artificially stop us writing to the already open log file with
	 * perms 000.
	 */
	close (log->fd);
	log->fd = -1;

	/* release child */
	assert (write (fds[1], "\n", 1) == 1);

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
	TEST_EQ (chmod (filename, old_perms), 0);

	nih_free (log);
	nih_free (new_log);
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

	file = conf_file_new (source, "/tmp/foo/bar");
	TEST_NE_P (file, NULL);

	class = file->job = job_class_new (NULL, "bar", NULL);
	TEST_NE_P (class, NULL);
	TEST_HASH_EMPTY (job_classes);
	TEST_TRUE (job_class_consider (class));
	TEST_HASH_NOT_EMPTY (job_classes);

	/* JobClass with no associated Jobs does not need to be
	 * serialised.
	 */
	json = job_class_serialise (class);
	TEST_EQ_P (json, NULL);

	nih_free (source);

	/*******************************/
	TEST_FEATURE ("JobClass with 1 Job");

	TEST_HASH_EMPTY (job_classes);

	source = conf_source_new (NULL, "/tmp/foo", CONF_JOB_DIR);
	TEST_NE_P (source, NULL);

	file = conf_file_new (source, "/tmp/foo/bar");
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

	file = conf_file_new (source, "/tmp/foo/bar");
	TEST_NE_P (file, NULL);

	class = file->job = job_class_new (NULL, "bar", NULL);
	TEST_NE_P (class, NULL);
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

	/*******************************/
	TEST_FEATURE ("ProcessType");

	for (i = PROCESS_INVALID-1; i < PROCESS_LAST+3; i++) {

		/* convert to string value */
		string_value = process_type_enum_to_str (i);
		if ((i < 0 && i != -2) || (i+1) > PROCESS_LAST) {
			TEST_EQ_P (string_value, NULL);
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

	/*******************************/
	TEST_FEATURE ("ConsoleType");

	for (i = -3; i < CONSOLE_LOG+3; i++) {

		/* convert to string value */
		string_value = job_class_console_type_enum_to_str (i);
		if (i < 0 || i > CONSOLE_LOG) {
			TEST_EQ_P (string_value, NULL);
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

	/*******************************/
	TEST_FEATURE ("ExpectType");

	for (i = -3; i < EXPECT_FORK+3; i++) {

		/* convert to string value */
		string_value = job_class_expect_type_enum_to_str (i);
		if (i < 0 || i > EXPECT_FORK) {
			TEST_EQ_P (string_value, NULL);
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
	int32_t            int32;
	int64_t            int64;
	size_t             i;
	size_t             size32;
	size_t             size64;
	size_t             sizestr;
	char              *str;
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
test_misc (void)
{
	nih_local char **args = NULL;
	size_t           len = 0;

	TEST_GROUP ("miscellaneous");

	/*******************************/
	TEST_FUNCTION ("clean_args");

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

#if 0
	Job *job;

	job_class_init ();

	/*******************************/
	TEST_FUNCTION ("state_get_job");

	TEST_HASH_EMPTY (job_classes);

	job = state_get_job ("", "");
	TEST_EQ_P (job, NULL);

	job = state_get_job ("a", "");
	TEST_EQ_P (job, NULL);

	job = state_get_job ("", "a");
	TEST_EQ_P (job, NULL);
#endif


	/*******************************/
}

int
main (int   argc,
      char *argv[])
{
	/* run tests in legacy (pre-session support) mode */
	setenv ("UPSTART_NO_SESSIONS", "1", 1);

	/* Modify Upstarts behaviour slightly since its running under
	 * the test suite.
	 */
	setenv ("UPSTART_TESTS", "1", 1);

	test_basic_types ();
	test_misc ();
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

	return 0;
}
