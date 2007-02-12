/* upstart
 *
 * test_event.c - test suite for init/event.c
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

#include <nih/test.h>

#include <sys/types.h>
#include <sys/wait.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/io.h>

#include <upstart/message.h>

#include "job.h"
#include "event.h"
#include "control.h"
#include "notify.h"


extern int upstart_disable_safeties;


void
test_new (void)
{
	Event *event;

	/* Check that we can create a new Event structure, and have the
	 * details filled in and returned.  It should not be placed in
	 * any kind of list.
	 */
	TEST_FUNCTION ("event_new");
	event_poll ();
	TEST_ALLOC_FAIL {
		event = event_new (NULL, "test");

		if (test_alloc_failed) {
			TEST_EQ_P (event, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_EMPTY (&event->entry);
		TEST_EQ_STR (event->name, "test");
		TEST_ALLOC_PARENT (event->name, event);

		TEST_EQ_P (event->args, NULL);
		TEST_EQ_P (event->env, NULL);

		nih_list_free (&event->entry);
	}
}

void
test_copy (void)
{
	Event *event, *copy;

	TEST_FUNCTION ("event_copy");
	event = event_new (NULL, "test");

	/* Check that we can copy an event which does not have any arguments
	 * or environment variables.
	 */
	TEST_FEATURE ("without arguments or environment");
	TEST_ALLOC_FAIL {
		copy = event_copy (NULL, event);

		if (test_alloc_failed) {
			TEST_EQ_P (copy, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (copy, sizeof (Event));
		TEST_LIST_EMPTY (&copy->entry);
		TEST_ALLOC_PARENT (copy->name, copy);
		TEST_EQ_STR (copy->name, "test");
		TEST_EQ_P (copy->args, NULL);
		TEST_EQ_P (copy->env, NULL);

		nih_list_free (&copy->entry);
	}


	/* Check that we can copy an event which does have arguments and
	 * environment; and that the arrays are copies not references.
	 */
	TEST_FEATURE ("with arguments and environment");
	NIH_MUST (nih_str_array_add (&event->args, event, NULL, "foo"));
	NIH_MUST (nih_str_array_add (&event->args, event, NULL, "bar"));

	NIH_MUST (nih_str_array_add (&event->env, event, NULL, "FOO=BAR"));

	TEST_ALLOC_FAIL {
		copy = event_copy (NULL, event);

		if (test_alloc_failed) {
			TEST_EQ_P (copy, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (copy, sizeof (Event));
		TEST_LIST_EMPTY (&copy->entry);
		TEST_ALLOC_PARENT (copy->name, copy);
		TEST_EQ_STR (copy->name, "test");

		TEST_ALLOC_PARENT (copy->args, copy);
		TEST_ALLOC_SIZE (copy->args, sizeof (char *) * 3);
		TEST_ALLOC_PARENT (copy->args[0], copy->args);
		TEST_ALLOC_PARENT (copy->args[1], copy->args);
		TEST_EQ_STR (copy->args[0], "foo");
		TEST_EQ_STR (copy->args[1], "bar");
		TEST_EQ_P (copy->args[2], NULL);

		TEST_ALLOC_PARENT (copy->env, copy);
		TEST_ALLOC_SIZE (copy->env, sizeof (char *) * 2);
		TEST_ALLOC_PARENT (copy->env[0], copy->env);
		TEST_EQ_STR (copy->env[0], "FOO=BAR");
		TEST_EQ_P (copy->env[1], NULL);

		nih_list_free (&copy->entry);
	}

	nih_list_free (&event->entry);
}

void
test_match (void)
{
	Event *event1, *event2;
	char  *args1[] = { "foo", "bar", "baz", NULL };
	char  *args2[] = { "foo", "bar", "baz", NULL };

	TEST_FUNCTION ("event_match");

	/* Check that two events with different names do not match. */
	TEST_FEATURE ("with different name events");
	event1 = event_new (NULL, "foo");
	event2 = event_new (NULL, "bar");

	TEST_FALSE (event_match (event1, event2));


	/* Check that two events with the same names match. */
	TEST_FEATURE ("with same name events");
	nih_free (event2);
	event2 = event_new (NULL, "foo");

	TEST_TRUE (event_match (event1, event2));


	/* Check that two events with the same arguments lists match. */
	TEST_FEATURE ("with same argument lists");
	event1->args = args1;
	event2->args = args2;

	TEST_TRUE (event_match (event1, event2));


	/* Check that the argument list in event2 may be shorter. */
	TEST_FEATURE ("with shorter list in event2");
	args2[2] = NULL;

	TEST_TRUE (event_match (event1, event2));


	/* Check that the argument list in event1 may not be shorter. */
	TEST_FEATURE ("with shorter list in event1");
	args2[2] = args1[2];
	args1[2] = NULL;

	TEST_FALSE (event_match (event1, event2));


	/* Check that the second events argument lists may be globs. */
	TEST_FEATURE ("with globs in arguments");
	args1[2] = args2[2];
	args2[2] = "b?z*";

	TEST_TRUE (event_match (event1, event2));


	nih_free (event2);
	nih_free (event1);
}


void
test_emit (void)
{
	EventEmission  *emission;
	char          **args, **env;
	uint32_t        last_id = -1;

	/* Check that we can request an event emission; the structure should
	 * be allocated with nih_alloc(), placed in a list and all of the
	 * details filled in.
	 */
	TEST_FUNCTION ("event_emit");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			args = nih_str_array_new (NULL);
			NIH_MUST (nih_str_array_add (&args, NULL, NULL,
						     "foo"));
			NIH_MUST (nih_str_array_add (&args, NULL, NULL,
						     "bar"));

			env = nih_str_array_new (NULL);
			NIH_MUST (nih_str_array_add (&env, NULL, NULL,
						     "FOO=BAR"));
		}

		emission = event_emit ("test", args, env);

		TEST_ALLOC_SIZE (emission, sizeof (EventEmission));
		TEST_LIST_NOT_EMPTY (&emission->event.entry);

		TEST_NE (emission->id, last_id);
		last_id = emission->id;

		TEST_EQ (emission->progress, EVENT_PENDING);
		TEST_EQ (emission->jobs, 0);
		TEST_EQ (emission->failed, FALSE);

		TEST_EQ_STR (emission->event.name, "test");
		TEST_ALLOC_PARENT (emission->event.name, emission);

		TEST_EQ_P (emission->event.args, args);
		TEST_ALLOC_PARENT (emission->event.args, emission);

		TEST_EQ_P (emission->event.env, env);
		TEST_ALLOC_PARENT (emission->event.env, emission);

		nih_list_free (&emission->event.entry);
	}
}

void
test_emit_find_by_id (void)
{
	EventEmission *emission, *ret;
	uint32_t       id;

	TEST_FUNCTION ("event_emit_find_by_id");

	/* Check that we can locate an emission in the pending queue by
	 * its id, and have it returned.
	 */
	TEST_FEATURE ("with id in pending queue");
	emission = event_emit ("test", NULL, NULL);

	ret = event_emit_find_by_id (emission->id);

	TEST_EQ_P (ret, emission);

	id = emission->id;
	nih_list_free (&emission->event.entry);


	/* Check that we get NULL if the id isn't in either queue. */
	TEST_FEATURE ("with id not in either queue");
	ret = event_emit_find_by_id (id);

	TEST_EQ_P (ret, NULL);
}

void
test_emit_finished (void)
{
	EventEmission *emission;

	TEST_FUNCTION ("event_emit_finished");
	emission = event_emit ("test", NULL, NULL);
	emission->progress = EVENT_HANDLING;

	/* Check that if an event has jobs remaining, the progress isn't
	 * changed.
	 */
	TEST_FEATURE ("with remaining jobs");
	emission->jobs = 1;
	event_emit_finished (emission);

	TEST_EQ (emission->progress, EVENT_HANDLING);


	/* Check that if an event has no jobs remaining, the progress is
	 * changed to finished.
	 */
	TEST_FEATURE ("with no remaining jobs");
	emission->jobs = 0;
	event_emit_finished (emission);

	TEST_EQ (emission->progress, EVENT_FINISHED);


	nih_list_free (&emission->event.entry);
}


static int destructor_called = 0;

static int
my_destructor (void *ptr)
{
	destructor_called++;

	return 0;
}

static int
check_event (void               *data,
	     pid_t               pid,
	     UpstartMessageType  type,
	     uint32_t            id,
	     const char         *name,
	     char * const       *args,
	     char * const       *env)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_EVENT);
	TEST_EQ_U (id, 0xdeafbeef);
	TEST_EQ_STR (name, "test");
	TEST_EQ_P (args, NULL);
	TEST_EQ_P (env, NULL);

	return 0;
}

static int
check_event_finished (void               *data,
		      pid_t               pid,
		      UpstartMessageType  type,
		      uint32_t            id,
		      int                 failed,
		      const char         *name,
		      char * const       *args,
		      char * const       *env)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_EVENT_FINISHED);
	TEST_EQ_U (id, 0xdeafbeef);
	TEST_EQ (failed, FALSE);
	TEST_EQ_STR (name, "test");
	TEST_EQ_P (args, NULL);
	TEST_EQ_P (env, NULL);

	return 0;
}

void
test_poll (void)
{
	EventEmission      *em1;
	Job                *job;
	Event              *event;
	NihIo              *io;
	NotifySubscription *sub;
	int                 wait_fd, status;
	pid_t               pid;

	TEST_FUNCTION ("event_poll");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	job_init ();
	event_init ();


	/* Check that a pending event in the queue is handled, with any
	 * subscribed processes being notified of the event and any
	 * jobs started or stopped as a result.
	 */
	TEST_FEATURE ("with pending event");
	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		TEST_CHILD_RELEASE (wait_fd);

		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_event,
						       NULL));

		nih_free (message);

		exit (0);
	}

	job = job_new (NULL, "test");
	job->process = nih_new (job, JobProcess);
	job->process->script = FALSE;
	job->process->command = "echo";

	event = event_new (job, "test");
	nih_list_add (&job->start_events, &event->entry);

	em1 = event_emit ("test", NULL, NULL);
	em1->id = 0xdeafbeef;

	sub = notify_subscribe_event (NULL, pid, em1);

	event_poll ();

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_EQ (em1->progress, EVENT_HANDLING);
	TEST_EQ (em1->jobs, 1);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_GT (job->pid, 0);

	waitpid (job->pid, NULL, 0);

	nih_list_free (&sub->entry);
	nih_list_free (&job->entry);
	nih_list_free (&em1->event.entry);


	/* Check that having a handling event in the queue doesn't cause
	 * any problem.
	 */
	TEST_FEATURE ("with handling event");
	TEST_ALLOC_FAIL {
		em1 = event_emit ("test", NULL, NULL);
		em1->progress = EVENT_HANDLING;

		event_poll ();

		TEST_LIST_NOT_EMPTY (&em1->event.entry);
		nih_list_free (&em1->event.entry);
	}


	/* Check that events in the finished state are consumed, leaving
	 * the list empty.  Subscribed processes should be notified, blocked
	 * jobs should be releaed and the event should be freed.
	 */
	TEST_FEATURE ("with finished event");
	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		TEST_CHILD_RELEASE (wait_fd);

		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_event_finished,
						       NULL));

		nih_free (message);

		exit (0);
	}

	em1 = event_emit ("test", NULL, NULL);
	em1->id = 0xdeafbeef;

	job = job_new (NULL, "test");
	job->goal = JOB_START;
	job->state = JOB_STARTING;
	job->pid = 0;
	job->blocked = em1;
	job->process = nih_new (job, JobProcess);
	job->process->script = FALSE;
	job->process->command = "echo";

	event_emit_finished (em1);

	destructor_called = 0;
	nih_alloc_set_destructor (em1, my_destructor);

	sub = notify_subscribe_event (NULL, pid, em1);

	event_poll ();

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_GT (job->pid, 0);

	waitpid (job->pid, &status, 0);
	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_EQ_P (job->blocked, NULL);

	TEST_TRUE (destructor_called);

	nih_list_free (&sub->entry);


	/* Check that a pending event which doesn't cause any jobs to be
	 * changed goes straight into the finished state, thus getting
	 * destroyed.
	 */
	TEST_FEATURE ("with no-op pending event");
	TEST_ALLOC_FAIL {
		em1 = event_emit ("test", NULL, NULL);

		destructor_called = 0;
		nih_alloc_set_destructor (em1, my_destructor);

		event_poll ();

		TEST_TRUE (destructor_called);
	}


	/* Check that a failed event causes another event to be emitted
	 * that has "/failed" appended on the end.  We can obtain the
	 * failed event emission by hooking a job on it, and using the
	 * cause.
	 */
	TEST_FEATURE ("with failed event");
	em1 = event_emit ("test", NULL, NULL);
	em1->failed = TRUE;
	em1->progress = EVENT_FINISHED;

	job = job_new (NULL, "test");
	job->process = nih_new (job, JobProcess);
	job->process->script = FALSE;
	job->process->command = "echo";

	event = event_new (job, "test/failed");
	nih_list_add (&job->start_events, &event->entry);

	destructor_called = 0;
	nih_alloc_set_destructor (em1, my_destructor);

	event_poll ();

	TEST_TRUE (destructor_called);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_GT (job->pid, 0);

	waitpid (job->pid, NULL, 0);

	TEST_EQ_STR (job->cause->event.name, "test/failed");

	event_emit_finished (job->cause);
	event_poll ();

	nih_list_free (&job->entry);


	/* Check that failed events do not, themselves, emit new failed
	 * events (otherwise we could be there all night :p)
	 */
	TEST_FEATURE ("with failed failed event");
	em1 = event_emit ("test/failed", NULL, NULL);
	em1->failed = TRUE;
	em1->progress = EVENT_FINISHED;

	job = job_new (NULL, "test");
	job->process = nih_new (job, JobProcess);
	job->process->script = FALSE;
	job->process->command = "echo";

	event = event_new (job, "test/failed");
	nih_list_add (&job->start_events, &event->entry);

	event = event_new (job, "test/failed/failed");
	nih_list_add (&job->start_events, &event->entry);

	destructor_called = 0;
	nih_alloc_set_destructor (em1, my_destructor);

	event_poll ();

	TEST_TRUE (destructor_called);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_WAITING);
	TEST_EQ (job->pid, 0);

	nih_list_free (&job->entry);


	control_close ();
	upstart_disable_safeties = FALSE;
}


int
main (int   argc,
      char *argv[])
{
	test_new ();
	test_copy ();
	test_match ();
	test_emit ();
	test_emit_find_by_id ();
	test_emit_finished ();
	test_poll ();

	return 0;
}
