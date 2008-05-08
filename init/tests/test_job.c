/* upstart
 *
 * test_job.c - test suite for init/job.c
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

#include <nih/test.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <stdio.h>
#include <limits.h>
#include <signal.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/hash.h>
#include <nih/main.h>

#include <nih/dbus.h>

#include "process.h"
#include "job_class.h"
#include "job.h"
#include "event.h"
#include "event_operator.h"
#include "conf.h"
#include "control.h"


void
test_new (void)
{
	JobClass       *class;
	Job            *job;
	EventOperator  *oper;
	DBusConnection *conn;
	NihListEntry   *entry;
	NihDBusObject  *object;
	int             i;

	TEST_FUNCTION ("job_new");
	control_init ();

	/* Check that we can create a new job structure; the structure
	 * should be allocated with nih_alloc, placed in the instances
	 * list of the class and have sensible defaults.
	 */
	TEST_FEATURE ("with no name");
	class = job_class_new (NULL, "test");
	class->stop_on = event_operator_new (class, EVENT_MATCH, "baz", NULL);

	TEST_ALLOC_FAIL {
		job = job_new (class, NULL);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);
			continue;
		}

		TEST_ALLOC_PARENT (job, class);
		TEST_ALLOC_SIZE (job, sizeof (Job));
		TEST_LIST_NOT_EMPTY (&job->entry);

		TEST_EQ_P (job->class, class);
		TEST_EQ_P (job->name, NULL);

		TEST_ALLOC_PARENT (job->path, job);
		TEST_EQ_STR (job->path,
			     "/com/ubuntu/Upstart/jobs/test/active");

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ_P (job->env, NULL);

		TEST_EQ_P (job->start_env, NULL);
		TEST_EQ_P (job->stop_env, NULL);

		oper = (EventOperator *)job->stop_on;
		TEST_ALLOC_PARENT (oper, job);
		TEST_ALLOC_SIZE (oper, sizeof (EventOperator));
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "baz");
		TEST_EQ_P (oper->env, NULL);
		TEST_EQ (oper->value, FALSE);

		TEST_NE_P (job->pid, NULL);
		TEST_ALLOC_PARENT (job->pid, job);
		TEST_ALLOC_SIZE (job->pid, sizeof (pid_t) * PROCESS_LAST);

		for (i = 0; i < PROCESS_LAST; i++)
			TEST_EQ (job->pid[i], 0);

		TEST_EQ_P (job->blocked, NULL);
		TEST_EQ_P (job->blocking, NULL);

		TEST_EQ_P (job->kill_timer, NULL);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_EQ (job->respawn_count, 0);
		TEST_EQ (job->respawn_time, 0);

		TEST_EQ (job->trace_forks, 0);
		TEST_EQ (job->trace_state, TRACE_NONE);

		event_operator_reset (job->stop_on);

		nih_free (job);
	}


	/* Check that if a name is passed, it is stored in the job.
	 */
	TEST_FEATURE ("with name given");
	TEST_ALLOC_FAIL {
		job = job_new (class, "fred");

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);
			continue;
		}

		TEST_ALLOC_PARENT (job, class);
		TEST_ALLOC_SIZE (job, sizeof (Job));
		TEST_LIST_NOT_EMPTY (&job->entry);

		TEST_ALLOC_PARENT (job->name, job);
		TEST_EQ_STR (job->name, "fred");

		TEST_ALLOC_PARENT (job->path, job);
		TEST_EQ_STR (job->path, "/com/ubuntu/Upstart/jobs/test/fred");

		event_operator_reset (job->stop_on);

		nih_free (job);
	}


	/* Check that when a D-Bus connection is open, the new instance
	 * is registered on that connection as an object.
	 */
	TEST_FEATURE ("with D-Bus connection");
	assert (conn = nih_dbus_bus (DBUS_BUS_SESSION, NULL));

	control_init ();

	entry = nih_list_entry_new (NULL);
	entry->data = conn;
	nih_list_add (control_conns, &entry->entry);

	job = job_new (class, "fred");

	TEST_ALLOC_PARENT (job, class);
	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_LIST_NOT_EMPTY (&job->entry);

	TEST_ALLOC_PARENT (job->name, job);
	TEST_EQ_STR (job->name, "fred");

	TEST_ALLOC_PARENT (job->path, job);
	TEST_EQ_STR (job->path, "/com/ubuntu/Upstart/jobs/test/fred");

	TEST_TRUE (dbus_connection_get_object_path_data (conn,
							 job->path,
							 (void **)&object));

	TEST_ALLOC_SIZE (object, sizeof (NihDBusObject));
	TEST_EQ_STR (object->path, job->path);
	TEST_EQ_P (object->data, job);

	event_operator_reset (job->stop_on);

	nih_free (job);

	nih_free (entry);

	dbus_connection_unref (conn);

	dbus_shutdown ();


	event_operator_reset (class->stop_on);

	nih_free (class);
}

void
test_register (void)
{
	JobClass       *class;
	Job            *job;
	DBusConnection *conn;
	NihListEntry   *entry;
	NihDBusObject  *object;

	/* Check that we can register an existing job instance on the bus
	 * using its path, create the job first to ensure that it's not
	 * registered and don't place it in the hash table to ensure that
	 * it doesn't _get_ registered.
	 */
	TEST_FUNCTION ("job_register");
	class = job_class_new (NULL, "test");

	job = job_new (class, "fred");

	assert (conn = nih_dbus_bus (DBUS_BUS_SESSION, NULL));

	assert (dbus_connection_get_object_path_data (conn, job->path,
						      (void **)&object));
	assert (object == NULL);

	entry = nih_list_entry_new (NULL);
	entry->data = conn;
	nih_list_add (control_conns, &entry->entry);

	job_register (job, conn);

	TEST_TRUE (dbus_connection_get_object_path_data (conn,
							 job->path,
							 (void **)&object));

	TEST_ALLOC_SIZE (object, sizeof (NihDBusObject));
	TEST_EQ_STR (object->path, job->path);
	TEST_EQ_P (object->data, job);

	nih_free (entry);

	dbus_connection_unref (conn);

	dbus_shutdown ();

	nih_free (class);
}


void
test_instance (void)
{
	JobClass *class;
	Job      *job, *ptr;

	TEST_FUNCTION ("job_instance");

	class = job_class_new (NULL, "test");
	class->process[PROCESS_MAIN] = process_new (class);
	class->process[PROCESS_MAIN]->command = "echo";


	/* Check that NULL is returned for an inactive singleton job,
	 * which should indicate a new instance should be created.
	 */
	TEST_FEATURE ("with inactive singleton job");
	TEST_ALLOC_FAIL {
		job = job_instance (class, NULL);

		TEST_EQ_P (job, NULL);
	}


	/* Check that the active instance of a singleton job is
	 * returned.
	 */
	TEST_FEATURE ("with active singleton job");
	job = job_new (class, NULL);

	TEST_ALLOC_FAIL {
		ptr = job_instance (class, NULL);

		TEST_EQ_P (ptr, job);
	}

	nih_free (job);


	/* Check that NULL is returned for an inactive instance job,
	 * indicating that a new instance should be created.
	 */
	TEST_FEATURE ("with inactive instance job");
	class->instance = "$FOO";

	TEST_ALLOC_FAIL {
		job = job_instance (class, NULL);

		TEST_EQ_P (job, NULL);
	}

	class->instance = NULL;


	/* Check that NULL is still returned for an active instance
	 * job where the name does not match, since a new one may be
	 * created.
	 */
	TEST_FEATURE ("with active instance job of different name");
	class->instance = "$FOO";

	job = job_new (class, "bar");

	TEST_ALLOC_FAIL {
		ptr = job_instance (class, "foo");

		TEST_EQ_P (ptr, NULL);
	}

	class->instance = NULL;

	nih_free (job);


	/* Check that the instance with the matching name is returned for
	 * an active instance job since a new one may not be created.
	 */
	TEST_FEATURE ("with active instance job");
	class->instance = "$FOO";

	job = job_new (class, "foo");

	TEST_ALLOC_FAIL {
		ptr = job_instance (class, "foo");

		TEST_EQ_P (ptr, job);
	}

	class->instance = NULL;

	nih_free (job);


	nih_free (class);
}


void
test_change_goal (void)
{
	JobClass *class;
	Job      *job = NULL;

	TEST_FUNCTION ("job_change_goal");
	event_init ();

	program_name = "test";

	class = job_class_new (NULL, "test");
	class->leader = TRUE;
	class->process[PROCESS_MAIN] = process_new (class);
	class->process[PROCESS_MAIN]->command = "echo";
	class->process[PROCESS_PRE_START] = process_new (class);
	class->process[PROCESS_PRE_START]->command = "echo";
	class->process[PROCESS_POST_STOP] = process_new (class);
	class->process[PROCESS_POST_STOP]->command = "echo";


	/* Check that an attempt to start a waiting job results in the
	 * goal being changed to start, and the state transitioned to
	 * starting.
	 */
	TEST_FEATURE ("with waiting job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);
		}

		job->goal = JOB_STOP;
		job->state = JOB_WAITING;
		job->blocked = NULL;

		job_change_goal (job, JOB_START);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STARTING);

		TEST_NE_P (job->blocked, NULL);

		nih_free (job);
	}


	/* Check that an attempt to start a job that's in the process of
	 * stopping changes only the goal, and leaves the rest of the
	 * state transition up to the normal process.
	 */
	TEST_FEATURE ("with stopping job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);
		}

		job->goal = JOB_STOP;
		job->state = JOB_KILLED;
		job->pid[PROCESS_MAIN] = 1;
		job->blocked = NULL;

		job_change_goal (job, JOB_START);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_KILLED);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);

		TEST_EQ_P (job->blocked, NULL);

		nih_free (job);
	}


	/* Check that an attempt to start a job that's running and still
	 * with a start goal does nothing.
	 */
	TEST_FEATURE ("with running job and start");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);
		}

		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;
		job->blocked = NULL;

		job_change_goal (job, JOB_START);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);

		TEST_EQ_P (job->blocked, NULL);

		nih_free (job);
	}


	/* Check that an attempt to stop a running job results in the goal
	 * and the state being changed.
	 */
	TEST_FEATURE ("with running job and stop");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);
		}

		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;
		job->blocked = NULL;

		job_change_goal (job, JOB_STOP);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);

		TEST_NE_P (job->blocked, NULL);

		nih_free (job);
	}


	/* Check that an attempt to stop a running job without any process
	 * also results in the state being changed.
	 */
	TEST_FEATURE ("with running job and no process");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);
		}

		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->blocked = NULL;

		job_change_goal (job, JOB_STOP);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);

		TEST_NE_P (job->blocked, NULL);

		nih_free (job);
	}


	/* Check that an attempt to stop a starting job only results in the
	 * goal being changed, the state should not be changed.
	 */
	TEST_FEATURE ("with starting job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);
		}

		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->pid[PROCESS_PRE_START] = 1;
		job->blocked = NULL;

		job_change_goal (job, JOB_STOP);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_PRE_START);
		TEST_EQ (job->pid[PROCESS_PRE_START], 1);

		TEST_EQ_P (job->blocked, NULL);

		nih_free (job);
	}


	/* Check that an attempt to stop a waiting job does nothing. */
	TEST_FEATURE ("with waiting job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);
		}

		job->goal = JOB_STOP;
		job->state = JOB_WAITING;
		job->blocked = NULL;

		job_change_goal (job, JOB_STOP);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);

		TEST_EQ_P (job->blocked, NULL);

		nih_free (job);
	}


	nih_free (class);
	event_poll ();
}


void
test_change_state (void)
{
	FILE          *output;
	ConfSource    *source = NULL;
	ConfFile      *file = NULL;
	JobClass      *class, *replacement = NULL, *ptr;
	Job           *job = NULL, *instance = NULL;
	NihList       *list = NULL;
	NihListEntry  *entry = NULL;
	Event         *cause, *event;
	struct stat    statbuf;
	char           dirname[PATH_MAX], filename[PATH_MAX];
	char         **env1, **env2, **env3;
	Process       *tmp, *fail;
	pid_t          pid;
	int            status;

	TEST_FUNCTION ("job_change_state");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (dirname);
	mkdir (dirname, 0700);

	class = job_class_new (NULL, "test");
	class->leader = TRUE;
	class->process[PROCESS_MAIN] = process_new (class);
	class->process[PROCESS_MAIN]->command = nih_sprintf (
		class->process[PROCESS_MAIN], "touch %s/run", dirname);
	class->process[PROCESS_PRE_START] = process_new (class);
	class->process[PROCESS_PRE_START]->command = nih_sprintf (
		class->process[PROCESS_PRE_START], "touch %s/start", dirname);
	class->process[PROCESS_POST_STOP] = process_new (class);
	class->process[PROCESS_POST_STOP]->command = nih_sprintf (
		class->process[PROCESS_POST_STOP], "touch %s/stop", dirname);

	class->start_on = event_operator_new (class, EVENT_MATCH,
					      "wibble", NULL);
	class->stop_on = event_operator_new (class, EVENT_MATCH,
					     "wibble", NULL);

	fail = process_new (class);
	fail->command = nih_sprintf (fail, "%s/no/such/file", dirname);

	cause = event_new (NULL, "wibble", NULL);
	nih_list_remove (&cause->entry);


	/* Check that a job can move from waiting to starting.  This
	 * should emit the starting event and block on it and copy the
	 * environment from start_env.
	 */
	TEST_FEATURE ("waiting to starting");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			assert (nih_str_array_add (&(job->start_env), job,
						   NULL, "FOO=BAR"));
			assert (nih_str_array_add (&(job->start_env), job,
						   NULL, "BAZ=BAZ"));

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_WAITING;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		env1 = job->start_env;

		job->failed = TRUE;
		job->failed_process = PROCESS_POST_STOP;
		job->exit_status = 1;

		job_change_state (job, JOB_STARTING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STARTING);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->env, env1);
		TEST_EQ_P (job->start_env, NULL);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "starting");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_P (event->env[1], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that a named instance of a job can move from waiting to
	 * starting, and that the instance name is included in the event
	 * environment.
	 */
	TEST_FEATURE ("waiting to starting for named instance");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "foo");

			assert (nih_str_array_add (&(job->start_env), job,
						   NULL, "FOO=BAR"));
			assert (nih_str_array_add (&(job->start_env), job,
						   NULL, "BAZ=BAZ"));

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_WAITING;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		env1 = job->start_env;

		job->failed = TRUE;
		job->failed_process = PROCESS_POST_STOP;
		job->exit_status = 1;

		job_change_state (job, JOB_STARTING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STARTING);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->env, env1);
		TEST_EQ_P (job->start_env, NULL);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "starting");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "INSTANCE=foo");
		TEST_EQ_P (event->env[2], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that any exported variable is added to the starting event
	 * from the job environment; it should not be possible to overwrite
	 * built-in variables, and any unknown variables should be ignored.
	 */
	TEST_FEATURE ("waiting to starting with export");
	assert (nih_str_array_add (&(class->export), class, NULL, "FOO"));
	assert (nih_str_array_add (&(class->export), class, NULL, "JOB"));
	assert (nih_str_array_add (&(class->export), class, NULL, "BEEP"));

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "foo");

			assert (nih_str_array_add (&(job->start_env), job,
						   NULL, "FOO=BAR"));
			assert (nih_str_array_add (&(job->start_env), job,
						   NULL, "BAZ=BAZ"));
			assert (nih_str_array_add (&(job->start_env), job,
						   NULL, "JOB=wibble"));

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_WAITING;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		env1 = job->start_env;

		job->failed = TRUE;
		job->failed_process = PROCESS_POST_STOP;
		job->exit_status = 1;

		job_change_state (job, JOB_STARTING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STARTING);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->env, env1);
		TEST_EQ_P (job->start_env, NULL);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "starting");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "INSTANCE=foo");
		TEST_EQ_STR (event->env[2], "FOO=BAR");
		TEST_EQ_P (event->env[3], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	nih_free (class->export);
	class->export = NULL;


	/* Check that a job with a start process can move from starting
	 * to pre-start, and have the process run.
	 */
	TEST_FEATURE ("starting to pre-start");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_STARTING;
		job->pid[PROCESS_PRE_START] = 0;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_PRE_START);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_PRE_START);
		TEST_NE (job->pid[PROCESS_PRE_START], 0);

		waitpid (job->pid[PROCESS_PRE_START], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/start");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that a job without a start process can move from starting
	 * to pre-start, skipping over that state, and instead going all
	 * the way through to the running state.  Because we get there,
	 * we should get a started event emitted.
	 */
	TEST_FEATURE ("starting to pre-start without process");
	tmp = class->process[PROCESS_PRE_START];
	class->process[PROCESS_PRE_START] = NULL;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_STARTING;
		job->pid[PROCESS_MAIN] = 0;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_PRE_START);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_NE (job->pid[PROCESS_MAIN], 0);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/run");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_FREE (list);
		TEST_EQ_P (job->blocking, NULL);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_P (event->env[1], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	class->process[PROCESS_PRE_START] = tmp;


	/* Check that a job with a start process that fails to run moves
	 * from starting to pre-start, the goal gets changed to stop, the
	 * status to stopping and the failed information set correctly.
	 */
	TEST_FEATURE ("starting to pre-start for failed process");
	tmp = class->process[PROCESS_PRE_START];
	class->process[PROCESS_PRE_START] = fail;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_STARTING;
		job->pid[PROCESS_PRE_START] = 0;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_change_state (job, JOB_PRE_START);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_PRE_START], 0);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, TRUE);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_FREE (list);
		TEST_EQ_P (job->blocking, NULL);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopping");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=pre-start");
		TEST_EQ_P (event->env[3], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_PRE_START);
		TEST_EQ (job->exit_status, -1);

		TEST_FILE_EQ (output, ("test: Failed to spawn test "
				       "pre-start process: unable to execute: "
				       "No such file or directory\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	class->process[PROCESS_PRE_START] = tmp;


	/* Check that a job with a main process can move from pre-start to
	 * spawned and have the process run, and as it's not going to wait,
	 * the state will be skipped forwards to running and the started
	 * event emitted.
	 */
	TEST_FEATURE ("pre-start to spawned");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->pid[PROCESS_MAIN] = 0;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_SPAWNED);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_NE (job->pid[PROCESS_MAIN], 0);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/run");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_FREE (list);
		TEST_EQ_P (job->blocking, NULL);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_P (event->env[1], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that a job with a named instance includes the instance
	 * name in the started event.
	 */
	TEST_FEATURE ("pre-start to spawned for named instance");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "foo");

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->pid[PROCESS_MAIN] = 0;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_SPAWNED);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_NE (job->pid[PROCESS_MAIN], 0);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/run");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_FREE (list);
		TEST_EQ_P (job->blocking, NULL);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "INSTANCE=foo");
		TEST_EQ_P (event->env[2], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that any exported variable is added to the started event
	 * from the job environment; it should not be possible to overwrite
	 * built-in variables, and any unknown variables should be ignored.
	 */
	TEST_FEATURE ("pre-start to spawned with export");
	assert (nih_str_array_add (&(class->export), class, NULL, "FOO"));
	assert (nih_str_array_add (&(class->export), class, NULL, "JOB"));
	assert (nih_str_array_add (&(class->export), class, NULL, "BEEP"));

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "foo");

			assert (nih_str_array_add (&(job->env), job,
						   NULL, "FOO=BAR"));
			assert (nih_str_array_add (&(job->env), job,
						   NULL, "BAZ=BAZ"));
			assert (nih_str_array_add (&(job->env), job,
						   NULL, "JOB=wibble"));

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->pid[PROCESS_MAIN] = 0;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_SPAWNED);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_NE (job->pid[PROCESS_MAIN], 0);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/run");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_FREE (list);
		TEST_EQ_P (job->blocking, NULL);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "INSTANCE=foo");
		TEST_EQ_STR (event->env[2], "FOO=BAR");
		TEST_EQ_P (event->env[3], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	nih_free (class->export);
	class->export = NULL;


	/* Check that a job without a main process can move from pre-start
	 * straight to running skipping the interim steps, and has the
	 * started event emitted.
	 */
	TEST_FEATURE ("pre-start to spawned without process");
	tmp = class->process[PROCESS_MAIN];
	class->process[PROCESS_MAIN] = NULL;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_PRE_START;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_SPAWNED);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_FREE (list);
		TEST_EQ_P (job->blocking, NULL);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_P (event->env[1], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	class->process[PROCESS_MAIN] = tmp;


	/* Check that a job with a main process that fails has its goal
	 * changed to stop, the state changed to stopping and failed
	 * information filled in.
	 */
	TEST_FEATURE ("pre-start to spawned for failed process");
	tmp = class->process[PROCESS_MAIN];
	class->process[PROCESS_MAIN] = fail;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->pid[PROCESS_MAIN] = 0;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_change_state (job, JOB_SPAWNED);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, TRUE);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_FREE (list);
		TEST_EQ_P (job->blocking, NULL);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopping");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=main");
		TEST_EQ_P (event->env[3], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, -1);

		TEST_FILE_EQ (output, ("test: Failed to spawn test "
				       "main process: unable to execute: "
				       "No such file or directory\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	class->process[PROCESS_MAIN] = tmp;


	/* Check that a job which has a main process that needs to wait for
	 * an event can move from pre-start to spawned and have the process
	 * run.  The state will remain in spawned until whatever we're
	 * waiting for happens.
	 */
	TEST_FEATURE ("pre-start to spawned for waiting job");
	class->expect = EXPECT_STOP;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->pid[PROCESS_MAIN] = 0;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_SPAWNED);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_SPAWNED);
		TEST_NE (job->pid[PROCESS_MAIN], 0);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/run");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	class->expect = EXPECT_NONE;


	/* Check that a job with a post-start process can move from spawned
	 * to post-start, and have the process run.
	 */
	TEST_FEATURE ("spawned to post-start");
	class->process[PROCESS_POST_START] = process_new (class);
	class->process[PROCESS_POST_START]->command = nih_sprintf (
		class->process[PROCESS_POST_START],
		"touch %s/post-start", dirname);

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = 1;
		job->pid[PROCESS_POST_START] = 0;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_POST_START);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_POST_START);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);
		TEST_NE (job->pid[PROCESS_POST_START], 0);

		waitpid (job->pid[PROCESS_POST_START], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/post-start");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	nih_free (class->process[PROCESS_POST_START]);
	class->process[PROCESS_POST_START] = NULL;


	/* Check that a job without a post-start process can move from
	 * spawned to post-start, skipping over that state, and instead
	 * going to the running state.  Because we get there, we should
	 * get a started event emitted.
	 */
	TEST_FEATURE ("spawned to post-start without process");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = 1;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_POST_START);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_FREE (list);
		TEST_EQ_P (job->blocking, NULL);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_P (event->env[1], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that a job with a post-start process ignores the failure
	 * of that process and can move from spawned to post-start, skipping
	 * over that state, and instead going to the running state.  Because
	 * we get there, we should get a started event emitted.
	 */
	TEST_FEATURE ("spawned to post-start for failed process");
	class->process[PROCESS_POST_START] = fail;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = 1;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_change_state (job, JOB_POST_START);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_FREE (list);
		TEST_EQ_P (job->blocking, NULL);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_P (event->env[1], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: Failed to spawn test "
				       "post-start process: unable to execute: "
				       "No such file or directory\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	class->process[PROCESS_POST_START] = NULL;


	/* Check that a service can move from post-start to running, which
	 * will emit the started event and unblock the events that caused
	 * us to start since the job has reached the desired state.
	 */
	TEST_FEATURE ("post-start to running for service");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_POST_START;
		job->pid[PROCESS_MAIN] = 1;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_RUNNING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_FREE (list);
		TEST_EQ_P (job->blocking, NULL);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_P (event->env[1], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that a task can move from post-start to running, which will
	 * emit the started event but leave events blocked and referenced.
	 */
	TEST_FEATURE ("post-start to running for task");
	class->task = TRUE;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_POST_START;
		job->pid[PROCESS_MAIN] = 1;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_RUNNING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_P (event->env[1], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	class->task = FALSE;


	/* Check that a job with a pre-stop process can move from running
	 * to pre-stop, and have the process run.
	 */
	TEST_FEATURE ("running to pre-stop");
	class->process[PROCESS_PRE_STOP] = process_new (class);
	class->process[PROCESS_PRE_STOP]->command = nih_sprintf (
		class->process[PROCESS_PRE_STOP],
		"touch %s/pre-stop", dirname);

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;
		job->pid[PROCESS_PRE_STOP] = 0;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_PRE_STOP);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_PRE_STOP);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);
		TEST_NE (job->pid[PROCESS_PRE_STOP], 0);

		waitpid (job->pid[PROCESS_PRE_STOP], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/pre-stop");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	nih_free (class->process[PROCESS_PRE_STOP]);
	class->process[PROCESS_PRE_STOP] = NULL;


	/* Check that a job without a pre-stop process can move from
	 * running to pre-stop, skipping over that state, and instead
	 * going to the stopping state.  Because we get there, we should
	 * get a stopping event emitted.
	 */
	TEST_FEATURE ("running to pre-stop without process");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_PRE_STOP);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopping");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=ok");
		TEST_EQ_P (event->env[2], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that a job with a named instance and without a pre-stop
	 * process includes the instance name in the stopping event.
	 */
	TEST_FEATURE ("running to pre-stop for named instance");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "foo");

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_PRE_STOP);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopping");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "INSTANCE=foo");
		TEST_EQ_STR (event->env[2], "RESULT=ok");
		TEST_EQ_P (event->env[3], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that any exported variable is added to the stopping event
	 * from the job environment; it should not be possible to overwrite
	 * built-in variables, and any unknown variables should be ignored.
	 */
	TEST_FEATURE ("running to pre-stop with export");
	assert (nih_str_array_add (&(class->export), class, NULL, "FOO"));
	assert (nih_str_array_add (&(class->export), class, NULL, "JOB"));
	assert (nih_str_array_add (&(class->export), class, NULL, "BEEP"));

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "foo");

			assert (nih_str_array_add (&(job->env), job,
						   NULL, "FOO=BAR"));
			assert (nih_str_array_add (&(job->env), job,
						   NULL, "BAZ=BAZ"));
			assert (nih_str_array_add (&(job->env), job,
						   NULL, "JOB=wibble"));

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_PRE_STOP);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopping");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "INSTANCE=foo");
		TEST_EQ_STR (event->env[2], "RESULT=ok");
		TEST_EQ_STR (event->env[3], "FOO=BAR");
		TEST_EQ_P (event->env[4], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	nih_free (class->export);
	class->export = NULL;


	/* Check that a job with a pre-stop process ignores any failure and
	 * moves from running to pre-stop, and then straight into the stopping
	 * state, emitting that event.
	 */
	TEST_FEATURE ("running to pre-stop for failed process");
	class->process[PROCESS_PRE_STOP] = fail;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_change_state (job, JOB_PRE_STOP);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopping");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=ok");
		TEST_EQ_P (event->env[2], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: Failed to spawn test "
				       "pre-stop process: unable to execute: "
				       "No such file or directory\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	class->process[PROCESS_PRE_STOP] = NULL;


	/* Check that a job can move from running to stopping, by-passing
	 * pre-stop.  This should emit the stopping event, containing the
	 * failed information including the exit status, and block on it.
	 */
	TEST_FEATURE ("running to stopping");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 1;

		job_change_state (job, JOB_STOPPING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopping");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=main");
		TEST_EQ_STR (event->env[3], "EXIT_STATUS=1");
		TEST_EQ_P (event->env[4], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, 1);

		nih_free (job);
	}


	/* Check that a job with a named instance that fails includes the
	 * instance name in the stopping event after the failed information.
	 */
	TEST_FEATURE ("running to stopping for named instance");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "foo");

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 1;

		job_change_state (job, JOB_STOPPING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopping");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "INSTANCE=foo");
		TEST_EQ_STR (event->env[2], "RESULT=failed");
		TEST_EQ_STR (event->env[3], "PROCESS=main");
		TEST_EQ_STR (event->env[4], "EXIT_STATUS=1");
		TEST_EQ_P (event->env[5], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, 1);

		nih_free (job);
	}


	/* Check that any exported variable is added to the stopping event
	 * from the job environment after any failed information; it should
	 * not be possible to overwrite built-in variables, and any unknown
	 * variables should be ignored.
	 */
	TEST_FEATURE ("running to stopping with export");
	assert (nih_str_array_add (&(class->export), class, NULL, "FOO"));
	assert (nih_str_array_add (&(class->export), class, NULL, "JOB"));
	assert (nih_str_array_add (&(class->export), class, NULL, "BEEP"));

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "foo");

			assert (nih_str_array_add (&(job->env), job,
						   NULL, "FOO=BAR"));
			assert (nih_str_array_add (&(job->env), job,
						   NULL, "BAZ=BAZ"));
			assert (nih_str_array_add (&(job->env), job,
						   NULL, "JOB=wibble"));

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 1;

		job_change_state (job, JOB_STOPPING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopping");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "INSTANCE=foo");
		TEST_EQ_STR (event->env[2], "RESULT=failed");
		TEST_EQ_STR (event->env[3], "PROCESS=main");
		TEST_EQ_STR (event->env[4], "EXIT_STATUS=1");
		TEST_EQ_STR (event->env[5], "FOO=BAR");
		TEST_EQ_P (event->env[6], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, 1);

		nih_free (job);
	}

	nih_free (class->export);
	class->export = NULL;


	/* Check that a job killed by a signal can move from running to
	 * stopping, by-passing pre-stop.  This should emit the stopping
	 * event, containing the failed information including the exit
	 * signal, and block on it.
	 */
	TEST_FEATURE ("running to stopping for killed process");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = SIGSEGV << 8;

		job_change_state (job, JOB_STOPPING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopping");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=main");
		TEST_EQ_STR (event->env[3], "EXIT_SIGNAL=SEGV");
		TEST_EQ_P (event->env[4], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, SIGSEGV << 8);

		nih_free (job);
	}


	/* Check that a job killed by an unknown signal can move from
	 * running to stopping, by-passing pre-stop.  This should emit
	 * the stopping event, containing the failed information
	 * including the exit signal number, and block on it.
	 */
	TEST_FEATURE ("running to stopping for unknown signal");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 33 << 8;

		job_change_state (job, JOB_STOPPING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopping");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=main");
		TEST_EQ_STR (event->env[3], "EXIT_SIGNAL=33");
		TEST_EQ_P (event->env[4], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, 33 << 8);

		nih_free (job);
	}


	/* Check that a job can move from pre-stop back to running again;
	 * clearing the block and reference on the events that stopped it
	 * including their environment.
	 */
	TEST_FEATURE ("pre-stop to running");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			assert (nih_str_array_add (&(job->stop_env), job,
						   NULL, "FOO=BAR"));
			assert (nih_str_array_add (&(job->stop_env), job,
						   NULL, "BAZ=BAZ"));

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_PRE_STOP;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		env1 = job->stop_env;
		TEST_FREE_TAG (env1);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_goal (job, JOB_START);
		job_change_state (job, JOB_RUNNING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_FREE (env1);
		TEST_EQ_P (job->stop_env, NULL);

		TEST_FREE (list);
		TEST_EQ_P (job->blocking, NULL);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that a job can move from pre-stop to stopping.  This
	 * should emit the stopping event, containing the failed information,
	 * and block on it.
	 */
	TEST_FEATURE ("pre-stop to stopping");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_PRE_STOP;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_STOPPING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopping");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=ok");
		TEST_EQ_P (event->env[2], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that a job with an active process can move from stopping
	 * to killed, the process should be sent the TERM signal and a
	 * kill timer put in place to check up on it.
	 */
	TEST_FEATURE ("stopping to killed");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_STOPPING;
		TEST_CHILD (job->pid[PROCESS_MAIN]) {
			pause ();
		}
		pid = job->pid[PROCESS_MAIN];
		setpgid (pid, pid);

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_KILLED);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_KILLED);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFSIGNALED (status));
		TEST_EQ (WTERMSIG (status), SIGTERM);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_NE_P (job->kill_timer, NULL);

		nih_free (job->kill_timer);
		job->kill_timer = NULL;

		nih_free (job);
	}


	/* Check that a job with no running process can move from stopping
	 * to killed, skipping over that state and ending up in post-stop
	 * instead.
	 */
	TEST_FEATURE ("stopping to killed without process");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_STOPPING;
		job->pid[PROCESS_POST_STOP] = 0;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_KILLED);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_POST_STOP);
		TEST_NE (job->pid[PROCESS_POST_STOP], 0);

		waitpid (job->pid[PROCESS_POST_STOP], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/stop");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_EQ_P (job->kill_timer, NULL);

		nih_free (job);
	}


	/* Check that a job with a stop process can move from killed
	 * to post-stop, and have the process run.
	 */
	TEST_FEATURE ("killed to post-stop");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_KILLED;
		job->pid[PROCESS_POST_STOP] = 0;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_POST_STOP);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_POST_STOP);
		TEST_NE (job->pid[PROCESS_POST_STOP], 0);

		waitpid (job->pid[PROCESS_POST_STOP], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/stop");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that a job without a stop process can move from killed
	 * to post-stop, skipping over that state, and instead going all
	 * the way through to being deleted.  Because we get there,
	 * we should get a stopped event emitted, and both the events
	 * that started and stopped the job forgotten.
	 */
	TEST_FEATURE ("killed to post-stop without process");
	tmp = class->process[PROCESS_POST_STOP];
	class->process[PROCESS_POST_STOP] = NULL;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_KILLED;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 1;

		TEST_FREE_TAG (job);

		job_change_state (job, JOB_POST_STOP);

		TEST_FREE (job);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, FALSE);

		TEST_FREE (list);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopped");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=main");
		TEST_EQ_STR (event->env[3], "EXIT_STATUS=1");
		TEST_EQ_P (event->env[4], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);
	}

	class->process[PROCESS_POST_STOP] = tmp;


	/* Check that a job with a stop process that fails to run moves
	 * from killed to post-start, the goal gets changed to stop, the
	 * status to stopped (and thus through to being deleted) and the
	 * failed information set correctly.
	 */
	TEST_FEATURE ("killed to post-stop for failed process");
	tmp = class->process[PROCESS_POST_STOP];
	class->process[PROCESS_POST_STOP] = fail;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_KILLED;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_FREE_TAG (job);

		TEST_DIVERT_STDERR (output) {
			job_change_state (job, JOB_POST_STOP);
		}
		rewind (output);

		TEST_FREE (job);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, TRUE);

		TEST_FREE (list);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopped");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=post-stop");
		TEST_EQ_P (event->env[3], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_FILE_EQ (output, ("test: Failed to spawn test "
				       "post-stop process: unable to execute: "
				       "No such file or directory\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	class->process[PROCESS_POST_STOP] = tmp;


	/* Check that a job can move from post-stop to being deleted.  This
	 * should emit the stopped event and clear the cause.
	 */
	TEST_FEATURE ("post-stop to waiting");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 1;

		TEST_FREE_TAG (job);

		job_change_state (job, JOB_WAITING);

		TEST_FREE (job);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, FALSE);

		TEST_FREE (list);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopped");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=main");
		TEST_EQ_STR (event->env[3], "EXIT_STATUS=1");
		TEST_EQ_P (event->env[4], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);
	}


	/* Check that a job with a named instance includes the instance
	 * name in the stopped event.
	 */
	TEST_FEATURE ("post-stop to waiting for named instance");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "foo");

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 1;

		TEST_FREE_TAG (job);

		job_change_state (job, JOB_WAITING);

		TEST_FREE (job);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, FALSE);

		TEST_FREE (list);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopped");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "INSTANCE=foo");
		TEST_EQ_STR (event->env[2], "RESULT=failed");
		TEST_EQ_STR (event->env[3], "PROCESS=main");
		TEST_EQ_STR (event->env[4], "EXIT_STATUS=1");
		TEST_EQ_P (event->env[5], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);
	}


	/* Check that any exported variable is added to the stopped event
	 * from the job environment; it should not be possible to overwrite
	 * built-in variables, and any unknown variables should be ignored.
	 */
	TEST_FEATURE ("post-stop to waiting with export");
	assert (nih_str_array_add (&(class->export), class, NULL, "FOO"));
	assert (nih_str_array_add (&(class->export), class, NULL, "JOB"));
	assert (nih_str_array_add (&(class->export), class, NULL, "BEEP"));

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, "foo");

			assert (nih_str_array_add (&(job->env), job,
						   NULL, "FOO=BAR"));
			assert (nih_str_array_add (&(job->env), job,
						   NULL, "BAZ=BAZ"));
			assert (nih_str_array_add (&(job->env), job,
						   NULL, "JOB=wibble"));

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 1;

		TEST_FREE_TAG (job);

		job_change_state (job, JOB_WAITING);

		TEST_FREE (job);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, FALSE);

		TEST_FREE (list);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopped");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "INSTANCE=foo");
		TEST_EQ_STR (event->env[2], "RESULT=failed");
		TEST_EQ_STR (event->env[3], "PROCESS=main");
		TEST_EQ_STR (event->env[4], "EXIT_STATUS=1");
		TEST_EQ_STR (event->env[5], "FOO=BAR");
		TEST_EQ_P (event->env[6], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);
	}

	nih_free (class->export);
	class->export = NULL;


	/* Check that a job can move from post-stop to starting.  This
	 * should emit the starting event and block on it, as well as clear
	 * any failed state information; but only unblock and unreference the
	 * stop events, the start events should remain referenced while the
	 * environment should be replaced with the new one.
	 */
	TEST_FEATURE ("post-stop to starting");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			assert (nih_str_array_add (&(job->env), job,
						   NULL, "FOO=TEA"));
			assert (nih_str_array_add (&(job->env), job,
						   NULL, "BAZ=COFFEE"));

			assert (nih_str_array_add (&(job->start_env), job,
						   NULL, "FOO=BAR"));
			assert (nih_str_array_add (&(job->start_env), job,
						   NULL, "BAZ=BAZ"));

			assert (nih_str_array_add (&(job->stop_env), job,
						   NULL, "FOO=BAR"));
			assert (nih_str_array_add (&(job->stop_env), job,
						   NULL, "BAZ=BAZ"));

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_POST_STOP;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		env1 = job->env;
		env2 = job->start_env;
		env3 = job->stop_env;

		TEST_FREE_TAG (env1);
		TEST_FREE_TAG (env2);
		TEST_FREE_TAG (env3);

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 1;

		job_change_state (job, JOB_STARTING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STARTING);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_FREE (env1);

		TEST_NOT_FREE (env2);
		TEST_EQ_P (job->env, env2);
		TEST_EQ_P (job->start_env, NULL);

		TEST_FREE (env3);
		TEST_EQ_P (job->stop_env, NULL);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "starting");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_P (event->env[1], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that when there is no new environment, the old one is left
	 * intact when the job moves from post-stop to starting.
	 */
	TEST_FEATURE ("post-stop to starting without new environment");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (class, NULL);

			assert (nih_str_array_add (&(job->env), job,
						   NULL, "FOO=TEA"));
			assert (nih_str_array_add (&(job->env), job,
						   NULL, "BAZ=COFFEE"));

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_POST_STOP;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		env1 = job->env;

		TEST_FREE_TAG (env1);

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 1;

		job_change_state (job, JOB_STARTING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STARTING);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_NOT_FREE (env1);
		TEST_EQ_P (job->env, env1);
		TEST_EQ_P (job->start_env, NULL);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "starting");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_P (event->env[1], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that a job which has a better replacement can move from
	 * post-stop to waiting, and be removed from the jobs hash table
	 * and replaced by the better one.
	 */
	TEST_FEATURE ("post-stop to waiting for replaced job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			source = conf_source_new (NULL, "/tmp", CONF_JOB_DIR);
			file = conf_file_new (source, "/tmp/test");
			file->job = job_class_new (NULL, "test");
			replacement = file->job;

			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		nih_hash_add (job_classes, &class->entry);

		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 1;

		TEST_FREE_TAG (job);

		job_change_state (job, JOB_WAITING);

		TEST_FREE (job);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, FALSE);

		TEST_FREE (list);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopped");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=main");
		TEST_EQ_STR (event->env[3], "EXIT_STATUS=1");
		TEST_EQ_P (event->env[4], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		ptr = (JobClass *)nih_hash_lookup (job_classes, "test");
		TEST_EQ (ptr, replacement);

		file->job = NULL;
		nih_free (replacement);
		nih_free (source);
	}


	/* Check that a job with a remaining running instance is not deleted,
	 * and is not replaced even if there is another waiting - only the
	 * instance should be deleted.
	 */
	TEST_FEATURE ("post-stop to waiting for still active job");
	class->deleted = TRUE;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			source = conf_source_new (NULL, "/tmp", CONF_JOB_DIR);
			file = conf_file_new (source, "/tmp/test");
			file->job = job_class_new (NULL, "test");
			replacement = file->job;

			job = job_new (class, NULL);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);

			instance = job_new (class, NULL);
			instance->goal = JOB_START;
			instance->state = JOB_RUNNING;
		}

		nih_hash_add (job_classes, &class->entry);

		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 1;

		TEST_FREE_TAG (class);
		TEST_FREE_TAG (job);
		TEST_FREE_TAG (instance);

		job_change_state (job, JOB_WAITING);

		TEST_NOT_FREE (class);
		TEST_FREE (job);
		TEST_NOT_FREE (instance);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, FALSE);

		TEST_FREE (list);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopped");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=main");
		TEST_EQ_STR (event->env[3], "EXIT_STATUS=1");
		TEST_EQ_P (event->env[4], NULL);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		ptr = (JobClass *)nih_hash_lookup (job_classes, "test");
		TEST_EQ_P (ptr, class);

		file->job = NULL;
		nih_free (replacement);
		nih_free (source);

		nih_free (instance);
	}

	class->deleted = FALSE;


	/* Check that a job with a deleted source can move from post-stop
	 * to waiting, be removed from the jobs hash table, replaced by
	 * a better one, then freed.
	 */
	TEST_FEATURE ("post-stop to waiting for deleted job");
	source = conf_source_new (NULL, "/tmp", CONF_JOB_DIR);
	file = conf_file_new (source, "/tmp/test");
	file->job = job_class_new (NULL, "test");
	replacement = file->job;

	class->deleted = TRUE;
	job = job_new (class, NULL);

	nih_hash_add (job_classes, &class->entry);

	job->blocking = nih_list_new (job);
	list = job->blocking;

	entry = nih_list_entry_new (job->blocking);
	entry->data = cause;
	event_block (cause);
	nih_list_add (job->blocking, &entry->entry);

	job->goal = JOB_STOP;
	job->state = JOB_POST_STOP;

	job->blocked = NULL;
	cause->failed = FALSE;

	TEST_FREE_TAG (list);

	job->failed = TRUE;
	job->failed_process = PROCESS_MAIN;
	job->exit_status = 1;

	TEST_FREE_TAG (class);
	TEST_FREE_TAG (job);

	job_change_state (job, JOB_WAITING);

	TEST_FREE (class);
	TEST_FREE (job);

	TEST_EQ (cause->blockers, 0);
	TEST_EQ (cause->failed, FALSE);

	TEST_FREE (list);

	event = (Event *)events->next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "stopped");
	TEST_EQ_STR (event->env[0], "JOB=test");
	TEST_EQ_STR (event->env[1], "RESULT=failed");
	TEST_EQ_STR (event->env[2], "PROCESS=main");
	TEST_EQ_STR (event->env[3], "EXIT_STATUS=1");
	TEST_EQ_P (event->env[4], NULL);
	nih_free (event);

	TEST_LIST_EMPTY (events);

	ptr = (JobClass *)nih_hash_lookup (job_classes, "test");
	TEST_EQ (ptr, replacement);

	file->job = NULL;
	nih_free (replacement);
	nih_free (source);


	fclose (output);
	rmdir (dirname);

	nih_free (cause);
	event_poll ();
}

void
test_next_state (void)
{
	JobClass *class;
	Job       *job;

	TEST_FUNCTION ("job_next_state");
	class = job_class_new (NULL, "test");
	class->process[PROCESS_MAIN] = process_new (class);
	class->process[PROCESS_MAIN]->command = "echo";

	job = job_new (class, NULL);

	/* Check that the next state if we're starting a waiting job is
	 * starting.
	 */
	TEST_FEATURE ("with waiting job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_WAITING;

	TEST_EQ (job_next_state (job), JOB_STARTING);


	/* Check that the next state if we're stopping a starting job is
	 * stpoping.
	 */
	TEST_FEATURE ("with starting job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_STARTING;

	TEST_EQ (job_next_state (job), JOB_STOPPING);


	/* Check that the next state if we're starting a starting job is
	 * pre-start.
	 */
	TEST_FEATURE ("with starting job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_STARTING;

	TEST_EQ (job_next_state (job), JOB_PRE_START);


	/* Check that the next state if we're stopping a pre-start job is
	 * stopping.
	 */
	TEST_FEATURE ("with pre-start job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_PRE_START;

	TEST_EQ (job_next_state (job), JOB_STOPPING);


	/* Check that the next state if we're starting a pre-start job is
	 * spawned.
	 */
	TEST_FEATURE ("with pre-start job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_PRE_START;

	TEST_EQ (job_next_state (job), JOB_SPAWNED);


	/* Check that the next state if we're stopping a spawned job is
	 * stopping.
	 */
	TEST_FEATURE ("with spawned job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_SPAWNED;

	TEST_EQ (job_next_state (job), JOB_STOPPING);


	/* Check that the next state if we're starting a spawned job is
	 * post-start.
	 */
	TEST_FEATURE ("with spawned job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_SPAWNED;

	TEST_EQ (job_next_state (job), JOB_POST_START);


	/* Check that the next state if we're stopping a post-start job is
	 * stopping.
	 */
	TEST_FEATURE ("with post-start job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_POST_START;

	TEST_EQ (job_next_state (job), JOB_STOPPING);


	/* Check that the next state if we're starting a post-start job is
	 * running.
	 */
	TEST_FEATURE ("with post-start job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_POST_START;

	TEST_EQ (job_next_state (job), JOB_RUNNING);


	/* Check that the next state if we're stopping a running job is
	 * pre-stop.  This is the "normal" stop process, as called from the
	 * goal change event.
	 */
	TEST_FEATURE ("with running job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_RUNNING;
	job->pid[PROCESS_MAIN] = 1;

	TEST_EQ (job_next_state (job), JOB_PRE_STOP);


	/* Check that the next state if we're stopping a running job that
	 * has no process is stopping.  This is the stop process if the
	 * process goes away on its own, as called from the child reaper.
	 */
	TEST_FEATURE ("with dead running job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_RUNNING;
	job->pid[PROCESS_MAIN] = 0;

	TEST_EQ (job_next_state (job), JOB_STOPPING);


	/* Check that the next state if we're starting a running job is
	 * stopping.  This assumes that the job has exited, but we didn't
	 * change the goal, so it should be respawned.
	 */
	TEST_FEATURE ("with running job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;

	TEST_EQ (job_next_state (job), JOB_STOPPING);


	/* Check that the next state if we're starting a pre-stop job is
	 * running.  This assumes that the pre-stop job decided that the
	 * job should not stop.
	 */
	TEST_FEATURE ("with pre-stop job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_PRE_STOP;

	TEST_EQ (job_next_state (job), JOB_RUNNING);


	/* Check that the next state if we're stopping a pre-stop job is
	 * stopping.
	 */
	TEST_FEATURE ("with pre-stop job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_PRE_STOP;

	TEST_EQ (job_next_state (job), JOB_STOPPING);


	/* Check that the next state if we're starting a stopping job is
	 * killed.  This is because we need to clean up before we can start
	 * again.
	 */
	TEST_FEATURE ("with stopping job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_STOPPING;

	TEST_EQ (job_next_state (job), JOB_KILLED);


	/* Check that the next state if we're stopping a stopping job is
	 * killed.
	 */
	TEST_FEATURE ("with stopping job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;

	TEST_EQ (job_next_state (job), JOB_KILLED);


	/* Check that the next state if we're starting a killed job is
	 * post-stop.  This is because we need to clean up before we can
	 * start again.
	 */
	TEST_FEATURE ("with killed job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_KILLED;

	TEST_EQ (job_next_state (job), JOB_POST_STOP);


	/* Check that the next state if we're stopping a killed job is
	 * post-stop.
	 */
	TEST_FEATURE ("with killed job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_KILLED;

	TEST_EQ (job_next_state (job), JOB_POST_STOP);


	/* Check that the next state if we're starting a post-stop job is
	 * starting.
	 */
	TEST_FEATURE ("with post-stop job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_POST_STOP;

	TEST_EQ (job_next_state (job), JOB_STARTING);


	/* Check that the next state if we're stopping a post-stop job is
	 * waiting.
	 */
	TEST_FEATURE ("with post-stop job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_POST_STOP;

	TEST_EQ (job_next_state (job), JOB_WAITING);


	nih_free (class);
}


void
test_failed (void)
{
	JobClass     *class;
	Job          *job;
	NihList      *list;
	NihListEntry *entry1, *entry2;
	Event        *event1, *event2;

	TEST_FUNCTION ("job_failed");

	/* Check that when the job has failed, the process and status are
	 * stored in the job and any events are unblocked and also marked
	 * as failed before freeing the blocking list.
	 */
	TEST_FEATURE ("with no current failure");
	class = job_class_new (NULL, "test");
	job = job_new (class, NULL);

	job->blocking = nih_list_new (job);
	list = job->blocking;

	TEST_FREE_TAG (list);

	entry1 = nih_list_entry_new (list);
	event1 = entry1->data = event_new (NULL, "foo", NULL);
	event_block (event1);
	nih_list_add (list, &entry1->entry);

	entry2 = nih_list_entry_new (list);
	event2 = entry2->data = event_new (NULL, "bar", NULL);
	event_block (event2);
	nih_list_add (list, &entry2->entry);

	job_failed (job, PROCESS_MAIN, 1);

	TEST_TRUE (job->failed);
	TEST_EQ (job->failed_process, PROCESS_MAIN);
	TEST_EQ (job->exit_status, 1);

	TEST_EQ_P (job->blocking, NULL);
	TEST_FREE (list);

	TEST_EQ (event1->blockers, 0);
	TEST_TRUE (event1->failed);

	TEST_EQ (event2->blockers, 0);
	TEST_TRUE (event2->failed);

	nih_free (class);


	/* Check that if the job has already failed, the new failure
	 * information does not override it and thus the events remain blocked
	 * since these were added after the first failure.
	 */
	TEST_FEATURE ("with previous failure");
	class = job_class_new (NULL, "test");
	job = job_new (class, NULL);
	job->failed = TRUE;
	job->failed_process = PROCESS_PRE_START;
	job->exit_status = 1;

	job->blocking = nih_list_new (job);
	list = job->blocking;

	TEST_FREE_TAG (list);

	entry1 = nih_list_entry_new (list);
	event1 = entry1->data = event_new (NULL, "foo", NULL);
	event_block (event1);
	nih_list_add (list, &entry1->entry);

	entry2 = nih_list_entry_new (list);
	event2 = entry2->data = event_new (NULL, "bar", NULL);
	event_block (event2);
	nih_list_add (list, &entry2->entry);

	job_failed (job, PROCESS_POST_STOP, 10);

	TEST_TRUE (job->failed);
	TEST_EQ (job->failed_process, PROCESS_PRE_START);
	TEST_EQ (job->exit_status, 1);

	TEST_EQ_P (job->blocking, list);
	TEST_NOT_FREE (list);

	TEST_EQ (event1->blockers, 1);
	TEST_FALSE (event1->failed);

	TEST_EQ (event2->blockers, 1);
	TEST_FALSE (event2->failed);

	nih_free (class);


	event_poll ();
}

void
test_unblock (void)
{
	JobClass     *class;
	Job          *job;
	NihList      *list;
	NihListEntry *entry1, *entry2;
	Event        *event1, *event2;

	TEST_FUNCTION ("job_unblock");

	/* Check that when the job has a list of blocked events, each event
	 * is unblocked and the list itself is then freed.
	 */
	TEST_FEATURE ("with blocked events");
	class = job_class_new (NULL, "test");
	job = job_new (class, NULL);

	job->blocking = nih_list_new (job);
	list = job->blocking;

	TEST_FREE_TAG (list);

	entry1 = nih_list_entry_new (list);
	event1 = entry1->data = event_new (NULL, "foo", NULL);
	event_block (event1);
	nih_list_add (list, &entry1->entry);

	entry2 = nih_list_entry_new (list);
	event2 = entry2->data = event_new (NULL, "bar", NULL);
	event_block (event2);
	nih_list_add (list, &entry2->entry);

	job_unblock (job, FALSE);

	TEST_EQ_P (job->blocking, NULL);
	TEST_FREE (list);

	TEST_EQ (event1->blockers, 0);
	TEST_FALSE (event1->failed);

	TEST_EQ (event2->blockers, 0);
	TEST_FALSE (event2->failed);

	nih_free (class);


	/* Check that when the job failed, each event in the list is
	 * unblocked and marked as failed.
	 */
	TEST_FEATURE ("with blocked events and failure");
	class = job_class_new (NULL, "test");
	job = job_new (class, NULL);

	job->blocking = nih_list_new (job);
	list = job->blocking;

	TEST_FREE_TAG (list);

	entry1 = nih_list_entry_new (list);
	event1 = entry1->data = event_new (NULL, "foo", NULL);
	event_block (event1);
	nih_list_add (list, &entry1->entry);

	entry2 = nih_list_entry_new (list);
	event2 = entry2->data = event_new (NULL, "bar", NULL);
	event_block (event2);
	nih_list_add (list, &entry2->entry);

	job_unblock (job, TRUE);

	TEST_EQ_P (job->blocking, NULL);
	TEST_FREE (list);

	TEST_EQ (event1->blockers, 0);
	TEST_TRUE (event1->failed);

	TEST_EQ (event2->blockers, 0);
	TEST_TRUE (event2->failed);

	nih_free (class);


	/* Check that when the job has no blocked events, the function
	 * still works (and just does nothing).
	 */
	TEST_FEATURE ("without blocked events");
	class = job_class_new (NULL, "test");
	job = job_new (class, NULL);

	job_unblock (job, TRUE);

	TEST_EQ_P (job->blocking, NULL);

	nih_free (class);


	event_poll ();
}


void
test_emit_event (void)
{
	JobClass *class;
	Job      *job;
	Event    *event;

	TEST_FUNCTION ("job_emit_event");

	/* Check that a nameless job in the starting state has the starting
	 * event event emitted with the job name set.
	 */
	TEST_FEATURE ("with singleton in starting state");
	class = job_class_new (NULL, "test");

	job = job_new (class, NULL);
	job->goal = JOB_START;
	job->state = JOB_STARTING;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "starting");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 2);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_P (event->env[1], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that an instance job in the starting state also has the
	 * instance name set.
	 */
	TEST_FEATURE ("with instance in starting state");
	class = job_class_new (NULL, "test");
	class->instance = "$FOO";

	job = job_new (class, "foo");
	job->goal = JOB_START;
	job->state = JOB_STARTING;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "starting");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 3);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "INSTANCE=foo");
		TEST_EQ_P (event->env[2], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that a nameless job in the running state has the started
	 * event emitted with the job name set.
	 */
	TEST_FEATURE ("with singleton in running state");
	class = job_class_new (NULL, "test");

	job = job_new (class, NULL);
	job->goal = JOB_START;
	job->state = JOB_RUNNING;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "started");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 2);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_P (event->env[1], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that an instance job in the running state also has the
	 * instance name set.
	 */
	TEST_FEATURE ("with instance in running state");
	class = job_class_new (NULL, "test");
	class->instance = "$FOO";

	job = job_new (class, "foo");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "started");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 3);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "INSTANCE=foo");
		TEST_EQ_P (event->env[2], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that a nameless job in the stopping state, with no failure,
	 * has the stopping event emitted with the job name set and the
	 * result as "ok"
	 */
	TEST_FEATURE ("with non-failed singleton in stopping state");
	class = job_class_new (NULL, "test");

	job = job_new (class, NULL);
	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "stopping");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 3);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=ok");
		TEST_EQ_P (event->env[2], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that a instance job in the stopping state, with no failure,
	 * also has the instance name set before the result information.
	 */
	TEST_FEATURE ("with non-failed instance in stopping state");
	class = job_class_new (NULL, "test");
	class->instance = "$FOO";

	job = job_new (class, "foo");
	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "stopping");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 4);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "INSTANCE=foo");
		TEST_EQ_STR (event->env[2], "RESULT=ok");
		TEST_EQ_P (event->env[3], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that a nameless job in the stopping state, with a process
	 * terminated by an abnormal exit code, has the stopping event emitted
	 * with the job name set, the result as failed, and the process and
	 * exit status members set.
	 */
	TEST_FEATURE ("with failed singleton in stopping state");
	class = job_class_new (NULL, "test");

	job = job_new (class, NULL);
	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;
	job->failed = TRUE;
	job->failed_process = PROCESS_MAIN;
	job->exit_status = 1;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "stopping");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 5);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=main");
		TEST_EQ_STR (event->env[3], "EXIT_STATUS=1");
		TEST_EQ_P (event->env[4], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that a instance job in the stopping state, with a process
	 * terminated by an abnormal exit code, also has the instance name
	 * set before the result information.
	 */
	TEST_FEATURE ("with failed instance in stopping state");
	class = job_class_new (NULL, "test");
	class->instance = "$FOO";

	job = job_new (class, "foo");
	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;
	job->failed = TRUE;
	job->failed_process = PROCESS_MAIN;
	job->exit_status = 1;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "stopping");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 6);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "INSTANCE=foo");
		TEST_EQ_STR (event->env[2], "RESULT=failed");
		TEST_EQ_STR (event->env[3], "PROCESS=main");
		TEST_EQ_STR (event->env[4], "EXIT_STATUS=1");
		TEST_EQ_P (event->env[5], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that a nameless job in the stopping state, with a process
	 * killed by a signal, has the stopping event emitted with the job
	 * name set, the result as failed, and the process and exit signal
	 * members set.
	 */
	TEST_FEATURE ("with killed singleton in stopping state");
	class = job_class_new (NULL, "test");

	job = job_new (class, NULL);
	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;
	job->failed = TRUE;
	job->failed_process = PROCESS_PRE_START;
	job->exit_status = SIGSEGV << 8;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "stopping");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 5);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=pre-start");
		TEST_EQ_STR (event->env[3], "EXIT_SIGNAL=SEGV");
		TEST_EQ_P (event->env[4], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that a instance job in the stopping state, with a process
	 * killed by a signal, also has the instance name set before the
	 * result information.
	 */
	TEST_FEATURE ("with killed instance in stopping state");
	class = job_class_new (NULL, "test");
	class->instance = "$FOO";

	job = job_new (class, "foo");
	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;
	job->failed = TRUE;
	job->failed_process = PROCESS_PRE_START;
	job->exit_status = SIGSEGV << 8;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "stopping");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 6);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "INSTANCE=foo");
		TEST_EQ_STR (event->env[2], "RESULT=failed");
		TEST_EQ_STR (event->env[3], "PROCESS=pre-start");
		TEST_EQ_STR (event->env[4], "EXIT_SIGNAL=SEGV");
		TEST_EQ_P (event->env[5], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that a nameless job in the stopping state, with a process
	 * killed by an unknown signal, has the stopping event emitted with
	 * the job name set, the result as failed, and the process and exit
	 * signal members set.
	 */
	TEST_FEATURE ("with unknown killed singleton in stopping state");
	class = job_class_new (NULL, "test");

	job = job_new (class, NULL);
	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;
	job->failed = TRUE;
	job->failed_process = PROCESS_PRE_START;
	job->exit_status = 47 << 8;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "stopping");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 5);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=pre-start");
		TEST_EQ_STR (event->env[3], "EXIT_SIGNAL=47");
		TEST_EQ_P (event->env[4], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that a instance job in the stopping state, with a process
	 * killed by an unknown signal, also has the instance name set before
	 * the result information.
	 */
	TEST_FEATURE ("with unknown killed instance in stopping state");
	class = job_class_new (NULL, "test");
	class->instance = "$FOO";

	job = job_new (class, "foo");
	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;
	job->failed = TRUE;
	job->failed_process = PROCESS_PRE_START;
	job->exit_status = 47 << 8;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "stopping");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 6);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "INSTANCE=foo");
		TEST_EQ_STR (event->env[2], "RESULT=failed");
		TEST_EQ_STR (event->env[3], "PROCESS=pre-start");
		TEST_EQ_STR (event->env[4], "EXIT_SIGNAL=47");
		TEST_EQ_P (event->env[5], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that a nameless job in the stopping state, with a process
	 * that failed to start at all,has the stopping event emitted with
	 * the job name set, the result as failed, and just the process set.
	 */
	TEST_FEATURE ("with unstarted singleton in stopping state");
	class = job_class_new (NULL, "test");

	job = job_new (class, NULL);
	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;
	job->failed = TRUE;
	job->failed_process = PROCESS_MAIN;
	job->exit_status = -1;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "stopping");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 4);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=main");
		TEST_EQ_P (event->env[3], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that a instance job in the stopping state, with a process
	 * that failed to start at all, also has the instance name set before
	 * the result information.
	 */
	TEST_FEATURE ("with unstarted instance in stopping state");
	class = job_class_new (NULL, "test");
	class->instance = "$FOO";

	job = job_new (class, "foo");
	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;
	job->failed = TRUE;
	job->failed_process = PROCESS_MAIN;
	job->exit_status = -1;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "stopping");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 5);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "INSTANCE=foo");
		TEST_EQ_STR (event->env[2], "RESULT=failed");
		TEST_EQ_STR (event->env[3], "PROCESS=main");
		TEST_EQ_P (event->env[4], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that a nameless job in the stopping state that failed to
	 * respawn has the stopping event emitted with the job name set,
	 * the result as failed and the process set to respawn.
	 */
	TEST_FEATURE ("with failed respawn singleton in stopping state");
	class = job_class_new (NULL, "test");

	job = job_new (class, NULL);
	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;
	job->failed = TRUE;
	job->failed_process = -1;
	job->exit_status = -1;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "stopping");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 4);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=respawn");
		TEST_EQ_P (event->env[3], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that a instance job in the stopping state that failed to
	 * respawn also has the instance name set before the result
	 * information.
	 */
	TEST_FEATURE ("with failed respawn instance in stopping state");
	class = job_class_new (NULL, "test");
	class->instance = "$FOO";

	job = job_new (class, "foo");
	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;
	job->failed = TRUE;
	job->failed_process = -1;
	job->exit_status = -1;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "stopping");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 5);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "INSTANCE=foo");
		TEST_EQ_STR (event->env[2], "RESULT=failed");
		TEST_EQ_STR (event->env[3], "PROCESS=respawn");
		TEST_EQ_P (event->env[4], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that a nameless job in the stopped state, with no failure,
	 * has the stopped event emitted with the job name set and the
	 * result as "ok"
	 */
	TEST_FEATURE ("with non-failed singleton in stopped state");
	class = job_class_new (NULL, "test");

	job = job_new (class, NULL);
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "stopped");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 3);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=ok");
		TEST_EQ_P (event->env[2], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that a instance job in the waiting state, with no failure,
	 * also has the instance name set before the result information.
	 */
	TEST_FEATURE ("with non-failed instance in waiting state");
	class = job_class_new (NULL, "test");
	class->instance = "$FOO";

	job = job_new (class, "foo");
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "stopped");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 4);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "INSTANCE=foo");
		TEST_EQ_STR (event->env[2], "RESULT=ok");
		TEST_EQ_P (event->env[3], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that a nameless job in the waiting state, with a process
	 * terminated by an abnormal exit code, has the waiting event emitted
	 * with the job name set, the result as failed, and the process and
	 * exit status members set.
	 */
	TEST_FEATURE ("with failed singleton in waiting state");
	class = job_class_new (NULL, "test");

	job = job_new (class, NULL);
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->failed = TRUE;
	job->failed_process = PROCESS_MAIN;
	job->exit_status = 1;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "stopped");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 5);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=main");
		TEST_EQ_STR (event->env[3], "EXIT_STATUS=1");
		TEST_EQ_P (event->env[4], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that a instance job in the waiting state, with a process
	 * terminated by an abnormal exit code, also has the instance name
	 * set before the result information.
	 */
	TEST_FEATURE ("with failed instance in waiting state");
	class = job_class_new (NULL, "test");
	class->instance = "$FOO";

	job = job_new (class, "foo");
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->failed = TRUE;
	job->failed_process = PROCESS_MAIN;
	job->exit_status = 1;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "stopped");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 6);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "INSTANCE=foo");
		TEST_EQ_STR (event->env[2], "RESULT=failed");
		TEST_EQ_STR (event->env[3], "PROCESS=main");
		TEST_EQ_STR (event->env[4], "EXIT_STATUS=1");
		TEST_EQ_P (event->env[5], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that a nameless job in the waiting state, with a process
	 * killed by a signal, has the waiting event emitted with the job
	 * name set, the result as failed, and the process and exit signal
	 * members set.
	 */
	TEST_FEATURE ("with killed singleton in waiting state");
	class = job_class_new (NULL, "test");

	job = job_new (class, NULL);
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->failed = TRUE;
	job->failed_process = PROCESS_PRE_START;
	job->exit_status = SIGSEGV << 8;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "stopped");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 5);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=pre-start");
		TEST_EQ_STR (event->env[3], "EXIT_SIGNAL=SEGV");
		TEST_EQ_P (event->env[4], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that a instance job in the waiting state, with a process
	 * killed by a signal, also has the instance name set before the
	 * result information.
	 */
	TEST_FEATURE ("with killed instance in waiting state");
	class = job_class_new (NULL, "test");
	class->instance = "$FOO";

	job = job_new (class, "foo");
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->failed = TRUE;
	job->failed_process = PROCESS_PRE_START;
	job->exit_status = SIGSEGV << 8;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "stopped");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 6);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "INSTANCE=foo");
		TEST_EQ_STR (event->env[2], "RESULT=failed");
		TEST_EQ_STR (event->env[3], "PROCESS=pre-start");
		TEST_EQ_STR (event->env[4], "EXIT_SIGNAL=SEGV");
		TEST_EQ_P (event->env[5], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that a nameless job in the waiting state, with a process
	 * killed by an unknown signal, has the waiting event emitted with
	 * the job name set, the result as failed, and the process and exit
	 * signal members set.
	 */
	TEST_FEATURE ("with unknown killed singleton in waiting state");
	class = job_class_new (NULL, "test");

	job = job_new (class, NULL);
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->failed = TRUE;
	job->failed_process = PROCESS_PRE_START;
	job->exit_status = 47 << 8;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "stopped");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 5);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=pre-start");
		TEST_EQ_STR (event->env[3], "EXIT_SIGNAL=47");
		TEST_EQ_P (event->env[4], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that a instance job in the waiting state, with a process
	 * killed by an unknown signal, also has the instance name set before
	 * the result information.
	 */
	TEST_FEATURE ("with unknown killed instance in waiting state");
	class = job_class_new (NULL, "test");
	class->instance = "$FOO";

	job = job_new (class, "foo");
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->failed = TRUE;
	job->failed_process = PROCESS_PRE_START;
	job->exit_status = 47 << 8;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "stopped");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 6);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "INSTANCE=foo");
		TEST_EQ_STR (event->env[2], "RESULT=failed");
		TEST_EQ_STR (event->env[3], "PROCESS=pre-start");
		TEST_EQ_STR (event->env[4], "EXIT_SIGNAL=47");
		TEST_EQ_P (event->env[5], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that a nameless job in the waiting state, with a process
	 * that failed to start at all,has the waiting event emitted with
	 * the job name set, the result as failed, and just the process set.
	 */
	TEST_FEATURE ("with unstarted singleton in waiting state");
	class = job_class_new (NULL, "test");

	job = job_new (class, NULL);
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->failed = TRUE;
	job->failed_process = PROCESS_MAIN;
	job->exit_status = -1;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "stopped");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 4);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=main");
		TEST_EQ_P (event->env[3], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that a instance job in the waiting state, with a process
	 * that failed to start at all, also has the instance name set before
	 * the result information.
	 */
	TEST_FEATURE ("with unstarted instance in waiting state");
	class = job_class_new (NULL, "test");
	class->instance = "$FOO";

	job = job_new (class, "foo");
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->failed = TRUE;
	job->failed_process = PROCESS_MAIN;
	job->exit_status = -1;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "stopped");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 5);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "INSTANCE=foo");
		TEST_EQ_STR (event->env[2], "RESULT=failed");
		TEST_EQ_STR (event->env[3], "PROCESS=main");
		TEST_EQ_P (event->env[4], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that a nameless job in the waiting state that failed to
	 * respawn has the waiting event emitted with the job name set,
	 * the result as failed and the process set to respawn.
	 */
	TEST_FEATURE ("with failed respawn singleton in waiting state");
	class = job_class_new (NULL, "test");

	job = job_new (class, NULL);
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->failed = TRUE;
	job->failed_process = -1;
	job->exit_status = -1;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "stopped");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 4);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=respawn");
		TEST_EQ_P (event->env[3], NULL);

		nih_free (event);
	}

	nih_free (class);


	/* Check that a instance job in the waiting state that failed to
	 * respawn also has the instance name set before the result
	 * information.
	 */
	TEST_FEATURE ("with failed respawn instance in waiting state");
	class = job_class_new (NULL, "test");
	class->instance = "$FOO";

	job = job_new (class, "foo");
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->failed = TRUE;
	job->failed_process = -1;
	job->exit_status = -1;

	TEST_ALLOC_FAIL {
		event = job_emit_event (job);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ_STR (event->name, "stopped");
		TEST_ALLOC_SIZE (event->env, sizeof (char *) * 5);
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "INSTANCE=foo");
		TEST_EQ_STR (event->env[2], "RESULT=failed");
		TEST_EQ_STR (event->env[3], "PROCESS=respawn");
		TEST_EQ_P (event->env[4], NULL);

		nih_free (event);
	}

	nih_free (class);
}


void
test_name (void)
{
	JobClass   *class;
	Job        *job;
	const char *ret;
	char       *name;

	TEST_FUNCTION ("job_name");

	/* Check that the name of a non-instance job is returned. */
	TEST_FEATURE ("with non-instance job");
	class = job_class_new (NULL, "test");
	job = job_new (class, NULL);

	TEST_ALLOC_FAIL {
		ret = job_name (job);

		TEST_EQ_STR (ret, "test");
	}

	nih_free (class);


	/* Check that the name of an instance job is returned. */
	TEST_FEATURE ("with instance job");
	class = job_class_new (NULL, "test");
	class->instance = "$FOO";
	job = job_new (class, "foo");

	TEST_ALLOC_FAIL {
		ret = job_name (job);

		TEST_EQ_STR (ret, "test (foo)");
	}

	nih_free (class);


	/* Check that a second call to the function frees the first returned
	 * value.
	 */
	TEST_FEATURE ("with multiple calls");
	class = job_class_new (NULL, "test");
	job = job_new (class, NULL);

	ret = job_name (job);
	name = (char *)ret;

	TEST_FREE_TAG (name);

	job_name (job);

	TEST_FREE (name);

	nih_free (class);
}


void
test_goal_name (void)
{
	const char *name;

	TEST_FUNCTION ("job_goal_name");

	/* Check that the JOB_STOP goal returns the right string. */
	TEST_FEATURE ("with stop goal");
	name = job_goal_name (JOB_STOP);

	TEST_EQ_STR (name, "stop");


	/* Check that the JOB_START goal returns the right string. */
	TEST_FEATURE ("with start goal");
	name = job_goal_name (JOB_START);

	TEST_EQ_STR (name, "start");


	/* Check that an invalid goal returns NULL. */
	TEST_FEATURE ("with invalid goal");
	name = job_goal_name (1234);

	TEST_EQ_P (name, NULL);
}

void
test_goal_from_name (void)
{
	JobGoal goal;

	TEST_FUNCTION ("job_goal_from_name");

	/* Check that the JOB_STOP goal is returned for the right string. */
	TEST_FEATURE ("with stop goal");
	goal = job_goal_from_name ("stop");

	TEST_EQ (goal, JOB_STOP);


	/* Check that the JOB_START goal is returned for the right string. */
	TEST_FEATURE ("with start goal");
	goal = job_goal_from_name ("start");

	TEST_EQ (goal, JOB_START);


	/* Check that -1 is returned for an invalid string. */
	TEST_FEATURE ("with invalid goal");
	goal = job_goal_from_name ("wibble");

	TEST_EQ (goal, -1);
}


void
test_state_name (void)
{
	const char *name;

	TEST_FUNCTION ("job_state_name");

	/* Check that the JOB_WAITING state returns the right string. */
	TEST_FEATURE ("with waiting state");
	name = job_state_name (JOB_WAITING);

	TEST_EQ_STR (name, "waiting");


	/* Check that the JOB_STARTING state returns the right string. */
	TEST_FEATURE ("with starting state");
	name = job_state_name (JOB_STARTING);

	TEST_EQ_STR (name, "starting");


	/* Check that the JOB_PRE_START state returns the right string. */
	TEST_FEATURE ("with pre-start state");
	name = job_state_name (JOB_PRE_START);

	TEST_EQ_STR (name, "pre-start");


	/* Check that the JOB_SPAWNED state returns the right string. */
	TEST_FEATURE ("with spawned state");
	name = job_state_name (JOB_SPAWNED);

	TEST_EQ_STR (name, "spawned");


	/* Check that the JOB_POST_START state returns the right string. */
	TEST_FEATURE ("with post-start state");
	name = job_state_name (JOB_POST_START);

	TEST_EQ_STR (name, "post-start");


	/* Check that the JOB_RUNNING state returns the right string. */
	TEST_FEATURE ("with running state");
	name = job_state_name (JOB_RUNNING);

	TEST_EQ_STR (name, "running");


	/* Check that the JOB_PRE_STOP state returns the right string. */
	TEST_FEATURE ("with pre-stop state");
	name = job_state_name (JOB_PRE_STOP);

	TEST_EQ_STR (name, "pre-stop");


	/* Check that the JOB_STOPPING state returns the right string. */
	TEST_FEATURE ("with stopping state");
	name = job_state_name (JOB_STOPPING);

	TEST_EQ_STR (name, "stopping");


	/* Check that the JOB_KILLED state returns the right string. */
	TEST_FEATURE ("with killed state");
	name = job_state_name (JOB_KILLED);

	TEST_EQ_STR (name, "killed");


	/* Check that the JOB_POST_STOP state returns the right string. */
	TEST_FEATURE ("with post-stop state");
	name = job_state_name (JOB_POST_STOP);

	TEST_EQ_STR (name, "post-stop");


	/* Check that an invalid state returns NULL. */
	TEST_FEATURE ("with invalid state");
	name = job_state_name (1234);

	TEST_EQ_P (name, NULL);
}

void
test_state_from_name (void)
{
	JobState state;

	TEST_FUNCTION ("job_state_from_name");

	/* Check that JOB_WAITING is returned for the right string. */
	TEST_FEATURE ("with waiting state");
	state = job_state_from_name ("waiting");

	TEST_EQ (state, JOB_WAITING);


	/* Check that JOB_STARTING is returned for the right string. */
	TEST_FEATURE ("with starting state");
	state = job_state_from_name ("starting");

	TEST_EQ (state, JOB_STARTING);


	/* Check that JOB_PRE_START is returned for the right string. */
	TEST_FEATURE ("with pre-start state");
	state = job_state_from_name ("pre-start");

	TEST_EQ (state, JOB_PRE_START);


	/* Check that JOB_SPAWNED is returned for the right string. */
	TEST_FEATURE ("with spawned state");
	state = job_state_from_name ("spawned");

	TEST_EQ (state, JOB_SPAWNED);


	/* Check that JOB_POST_START is returned for the right string. */
	TEST_FEATURE ("with post-start state");
	state = job_state_from_name ("post-start");

	TEST_EQ (state, JOB_POST_START);


	/* Check that JOB_RUNNING is returned for the right string. */
	TEST_FEATURE ("with running state");
	state = job_state_from_name ("running");

	TEST_EQ (state, JOB_RUNNING);


	/* Check that JOB_PRE_STOP is returned for the right string. */
	TEST_FEATURE ("with pre-stop state");
	state = job_state_from_name ("pre-stop");

	TEST_EQ (state, JOB_PRE_STOP);


	/* Check that JOB_STOPPING is returned for the right string. */
	TEST_FEATURE ("with stopping state");
	state = job_state_from_name ("stopping");

	TEST_EQ (state, JOB_STOPPING);


	/* Check that JOB_KILLED is returned for the right string. */
	TEST_FEATURE ("with killed state");
	state = job_state_from_name ("killed");

	TEST_EQ (state, JOB_KILLED);


	/* Check that JOB_POST_STOP is returned for the right string. */
	TEST_FEATURE ("with post-stop state");
	state = job_state_from_name ("post-stop");

	TEST_EQ (state, JOB_POST_STOP);


	/* Check that -1 is returned for an invalid string. */
	TEST_FEATURE ("with invalid state");
	state = job_state_from_name ("wibble");

	TEST_EQ (state, -1);
}


int
main (int   argc,
      char *argv[])
{
	test_new ();
	test_register ();
	test_instance ();
	test_change_goal ();
	test_change_state ();
	test_next_state ();
	test_failed ();
	test_unblock ();
	test_emit_event ();

	test_name ();
	test_goal_name ();
	test_goal_from_name ();
	test_state_name ();
	test_state_from_name ();

	return 0;
}
