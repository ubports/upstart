/* upstart
 *
 * test_log.c - test suite for init/state.c
 *
 * Copyright © 2021 Canonical Ltd.
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

/**
 * obj_string_check:
 *
 * @a: first object,
 * @b: second object,
 * @name: name of string element.
 *
 * Compare string element @name in objects @a and @b.
 *
 * Returns: 0 if strings are identical
 * (or both NULL), else 1.
 **/
#define obj_string_check(a, b, name) \
	string_check ((a)->name, (b)->name)

/**
 * obj_num_check:
 *
 * @a: first object,
 * @b: second object.
 * @name: name of numeric element.
 *
 * Compare numeric element @name in objects @a and @b.
 *
 * Returns: 0 if @a and @b are identical, else 1.
 **/
#define obj_num_check(a, b, name) \
	(a->name != b->name)

/**
 * string_check:
 *
 * @a: first string,
 * @b: second string.
 *
 * Compare @a and @b either or both of which may be NULL.
 *
 * Returns 0 if strings are identical or both NULL, else 1.
 **/
int
string_check (const char *a, const char *b)
{
	if ((a == b) && !a)
		return 0;

	if (!a || !b)
		return 1;

	if (strcmp (a, b))
		return 1;

	return 0;
}

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
 * Returns: 0 if @a and @b are identical, else 1;
 **/
int
process_diff (const Process *a, const Process *b)
{
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
 * @b: second Event.
 *
 * Compare two Event objects for equivalence.
 *
 * Returns: 0 if @a and @b are identical, else 1;
 **/
int
event_diff (const Event *a, const Event *b)
{
	nih_local char *env_a = NULL;
	nih_local char *env_b = NULL;

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

	/* FIXME: blocking */

	return 0;

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

const Process test_procs[] = {
	{ 0, "echo hello" },
	{ 1, "echo hello" },
};

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
	int  i;
	int  num;

	TEST_GROUP ("Process serialisation and deserialisation");

	num = (int)sizeof (test_procs) / sizeof (Process);

	for (i = 0; i < num; i++) {
		run_process_test (&test_procs[i]);
	}
}

void
test_event_serialise (void)
{
	json_object       *json;
	Event             *event = NULL;
	Event             *new_event = NULL;
	nih_local char   **env = NULL;
	size_t             len = 0;

	TEST_GROUP ("Event serialisation and deserialisation");

	event_init ();

	/*******************************/
	TEST_FEATURE ("without event environment");

	TEST_LIST_EMPTY (events);

	event = event_new (NULL, "foo", NULL);
	TEST_NE_P (event, NULL);

	TEST_LIST_NOT_EMPTY (events);

	json = event_serialise (event);
	TEST_NE_P (json, NULL);

	nih_list_remove (&event->entry);

	new_event = event_deserialise (json);
	TEST_NE_P (json, NULL);

	assert0 (event_diff (event, new_event));

	nih_free (event);
	nih_free (new_event);
	json_object_put (json);

	/*******************************/
	TEST_FEATURE ("with event environment");

	env = nih_str_array_new (NULL);
	TEST_NE_P (env, NULL);
	TEST_NE_P (environ_add (&env, NULL, &len, TRUE, "FOO=BAR"), NULL);
	TEST_NE_P (environ_add (&env, NULL, &len, TRUE, "a="), NULL);
	TEST_NE_P (environ_add (&env, NULL, &len, TRUE, "HELLO=world"), NULL);

	TEST_LIST_EMPTY (events);

	event = event_new (NULL, "foo", env);
	TEST_NE_P (event, NULL);

	TEST_LIST_NOT_EMPTY (events);

	json = event_serialise (event);
	TEST_NE_P (json, NULL);

	nih_list_remove (&event->entry);

	new_event = event_deserialise (json);
	TEST_NE_P (json, NULL);

	assert0 (event_diff (event, new_event));

	nih_free (event);
	nih_free (new_event);
	json_object_put (json);
}

void
test_job_class_serialise (void)
{
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

	test_session_serialise ();
	test_process_serialise ();
	test_event_serialise ();

	return 0;
}
