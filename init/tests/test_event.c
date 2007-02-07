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
	event_queue_run ();
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


static int emission_called = 0;

static void
my_emission_cb (void          *data,
		EventEmission *emission)
{
	emission_called++;
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

		emission = event_emit ("test", args, env,
				       my_emission_cb, &emission);

		TEST_ALLOC_SIZE (emission, sizeof (EventEmission));
		TEST_LIST_NOT_EMPTY (&emission->event.entry);

		TEST_NE (emission->id, last_id);
		last_id = emission->id;

		TEST_EQ (emission->progress, EVENT_PENDING);
		TEST_EQ (emission->jobs, 0);
		TEST_EQ (emission->failed, FALSE);
		TEST_EQ_P (emission->callback, my_emission_cb);
		TEST_EQ_P (emission->data, &emission);

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
	emission = event_emit ("test", NULL, NULL, my_emission_cb, &emission);

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
	emission = event_emit ("test", NULL, NULL, NULL, NULL);
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
	     const char         *name,
	     char * const       *args,
	     char * const       *env)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_EVENT);
	TEST_EQ_STR (name, "test");
	TEST_EQ_P (args, NULL);
	TEST_EQ_P (env, NULL);

	return 0;
}

void
test_poll (void)
{
	EventEmission      *em1;
	NihList            *events;
	Job                *job;
	Event              *event;
	NihIo              *io;
	NotifySubscription *sub;
	int                 wait_fd, status;
	pid_t               pid;

	TEST_FUNCTION ("event_poll");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	job_list ();

	/* Naughty way of getting the list */
	event_poll ();
	em1 = event_emit ("test", NULL, NULL, NULL, NULL);
	events = em1->event.entry.next;
	nih_list_free (&em1->event.entry);


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

	sub = notify_subscribe (pid, NOTIFY_EVENTS, TRUE);

	job = job_new (NULL, "test");
	job->command = nih_strdup (job, "echo");

	event = event_new (job, "test");
	nih_list_add (&job->start_events, &event->entry);

	em1 = event_emit ("test", NULL, NULL, my_emission_cb, &em1);
	em1->jobs++; /* FIXME hack until we fix goal_event stuff */

	event_poll ();

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_EQ (em1->progress, EVENT_HANDLING);
	TEST_EQ (em1->jobs, 1);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);
	TEST_NE (job->pid, -1);

	waitpid (job->pid, NULL, 0);

	nih_list_free (&sub->entry);
	nih_list_free (&job->entry);
	nih_list_free (&em1->event.entry);

	control_close ();


	/* Check that having a handling event in the queue doesn't cause
	 * any problem.
	 */
	TEST_FEATURE ("with handling event");
	TEST_ALLOC_FAIL {
		em1 = event_emit ("test", NULL, NULL, my_emission_cb, &em1);
		em1->progress = EVENT_HANDLING;

		event_poll ();

		TEST_LIST_NOT_EMPTY (&em1->event.entry);
		nih_list_free (&em1->event.entry);
	}


	/* Check that events in the finished state are consumed, leaving
	 * the list empty.  The callback for the event should be run and
	 * the event should be freed.
	 */
	TEST_FEATURE ("with finished event");
	TEST_ALLOC_FAIL {
		em1 = event_emit ("test", NULL, NULL, my_emission_cb, &em1);
		event_emit_finished (em1);

		destructor_called = 0;
		nih_alloc_set_destructor (em1, my_destructor);

		emission_called = 0;

		event_poll ();

		TEST_TRUE (emission_called);
		TEST_TRUE (destructor_called);
	}


	/* Check that a pending event which doesn't cause any jobs to be
	 * changed goes straight into the finished state, thus getting
	 * the callback called and destroyed.
	 */
	TEST_FEATURE ("with no-op pending event");
	TEST_ALLOC_FAIL {
		em1 = event_emit ("test", NULL, NULL, my_emission_cb, &em1);

		destructor_called = 0;
		nih_alloc_set_destructor (em1, my_destructor);

		emission_called = 0;

		event_poll ();

		TEST_TRUE (emission_called);
		TEST_TRUE (destructor_called);
	}


	/* Check that a failed event causes another event to be emitted
	 * that has "/failed" appended on the end.  We can obtain the
	 * failed event emission by hooking a job on it, and using the
	 * goal event.
	 */
	TEST_FEATURE ("with failed event");
	em1 = event_emit ("test", NULL, NULL, my_emission_cb, &em1);
	em1->failed = TRUE;
	em1->progress = EVENT_FINISHED;

	job = job_new (NULL, "test");
	job->command = nih_strdup (job, "echo");

	event = event_new (job, "test/failed");
	nih_list_add (&job->start_events, &event->entry);

	destructor_called = 0;
	nih_alloc_set_destructor (em1, my_destructor);

	emission_called = 0;

	event_poll ();

	TEST_TRUE (emission_called);
	TEST_TRUE (destructor_called);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);
	TEST_NE (job->pid, -1);

	waitpid (job->pid, NULL, 0);

	TEST_EQ_STR (job->goal_event->name, "test/failed");

	/* FIXME will need to finish that emission and call poll again */

	nih_list_free (&job->entry);


	/* Check that failed events do not, themselves, emit new failed
	 * events (otherwise we could be there all night :p)
	 */
	TEST_FEATURE ("with failed failed event");
	em1 = event_emit ("test/failed", NULL, NULL, my_emission_cb, &em1);
	em1->failed = TRUE;
	em1->progress = EVENT_FINISHED;

	job = job_new (NULL, "test");
	job->command = nih_strdup (job, "echo");

	event = event_new (job, "test/failed");
	nih_list_add (&job->start_events, &event->entry);

	event = event_new (job, "test/failed/failed");
	nih_list_add (&job->start_events, &event->entry);

	destructor_called = 0;
	nih_alloc_set_destructor (em1, my_destructor);

	emission_called = 0;

	event_poll ();

	TEST_TRUE (emission_called);
	TEST_TRUE (destructor_called);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_WAITING);
	TEST_EQ (job->process_state, PROCESS_NONE);

	nih_list_free (&job->entry);


	upstart_disable_safeties = FALSE;
}


void
test_read_state (void)
{
	EventEmission *em, *ptr;
	char           buf[80];

	TEST_FUNCTION ("event_read_state");

	/* Check that an event can be created from a text state that contains
	 * the name, and queued automatically.
	 */
	TEST_FEATURE ("with event name");
	TEST_ALLOC_FAIL {
		sprintf (buf, "Event bang");
		em = event_read_state (NULL, buf);

		TEST_ALLOC_SIZE (em, sizeof (EventEmission));
		TEST_LIST_NOT_EMPTY (&em->event.entry);
		TEST_EQ_STR (em->event.name, "bang");
		TEST_ALLOC_PARENT (em->event.name, em);

		nih_list_free (&em->event.entry);
	}


	/* Check that an event in the buffer can contain arguments, which
	 * are appended to the event.
	 */
	TEST_FEATURE ("with argument to event");
	em = event_emit ("foo", NULL, NULL, NULL, NULL);
	TEST_ALLOC_FAIL {
		sprintf (buf, ".arg frodo");
		ptr = event_read_state (em, buf);

		TEST_EQ_P (ptr, em);
		TEST_ALLOC_PARENT (em->event.args, em);
		TEST_ALLOC_SIZE (em->event.args, sizeof (char *) * 2);
		TEST_ALLOC_PARENT (em->event.args[0], em->event.args);
		TEST_EQ_STR (em->event.args[0], "frodo");
		TEST_EQ_P (em->event.args[1], NULL);

		nih_free (em->event.args);
		em->event.args = NULL;
	}


	/* Check that an event in the buffer can contain environment, which
	 * are appended to the event.
	 */
	TEST_FEATURE ("with environment for event");
	TEST_ALLOC_FAIL {
		sprintf (buf, ".env FOO=BAR");
		ptr = event_read_state (em, buf);

		TEST_EQ_P (ptr, em);
		TEST_ALLOC_PARENT (em->event.env, em);
		TEST_ALLOC_SIZE (em->event.env, sizeof (char *) * 2);
		TEST_ALLOC_PARENT (em->event.env[0], em->event.env);
		TEST_EQ_STR (em->event.env[0], "FOO=BAR");
		TEST_EQ_P (em->event.env[1], NULL);

		nih_free (em->event.env);
		em->event.env = NULL;
	}


	/* Check that the id of the emission can be received. */
	TEST_FEATURE ("with emission id");
	TEST_ALLOC_FAIL {
		sprintf (buf, ".id 42");
		ptr = event_read_state (em, buf);

		TEST_EQ_P (ptr, em);
		TEST_EQ (em->id, 42);
	}


	/* Check that the emission progress can be received. */
	TEST_FEATURE ("with emission progress");
	TEST_ALLOC_FAIL {
		sprintf (buf, ".progress 2");
		ptr = event_read_state (em, buf);

		TEST_EQ_P (ptr, em);
		TEST_EQ (em->progress, EVENT_FINISHED);
	}


	/* Check that the failed status of the event can be received. */
	TEST_FEATURE ("with failed emission");
	TEST_ALLOC_FAIL {
		sprintf (buf, ".failed TRUE");
		ptr = event_read_state (em, buf);

		TEST_EQ_P (ptr, em);
		TEST_EQ (em->failed, TRUE);
	}


	/* Check that a negative failed status of the event can be received. */
	TEST_FEATURE ("with non-failed emission");
	TEST_ALLOC_FAIL {
		sprintf (buf, ".failed FALSE");
		ptr = event_read_state (em, buf);

		TEST_EQ_P (ptr, em);
		TEST_EQ (em->failed, FALSE);
	}

	nih_list_free (&em->event.entry);


	/* Check that next emission id can be reiceved, and that is used
	 * for the next event.
	 */
	TEST_FEATURE ("with next emission id");
	TEST_ALLOC_FAIL {
		sprintf (buf, "Emission 809120");
		em = event_read_state (NULL, buf);

		TEST_EQ_P (em, NULL);

		TEST_ALLOC_SAFE {
			em = event_emit ("test", NULL, NULL, NULL, NULL);
		}

		TEST_EQ (em->id, 809120);
		nih_list_free (&em->event.entry);
	}
}

void
test_write_state (void)
{
	FILE           *output;
	EventEmission  *em1, *em2, *em3;
	char          **args, **env;

	/* Check that the state of the event queue can be written out to
	 * a file descriptor.
	 */
	TEST_FUNCTION ("event_write_state");
	em1 = event_emit ("frodo", NULL, NULL, NULL, NULL);
	em1->id = 100;
	em1->progress = EVENT_HANDLING;
	em1->failed = FALSE;

	args = nih_str_array_new (NULL);
	NIH_MUST (nih_str_array_add (&args, NULL, NULL, "foo"));
	NIH_MUST (nih_str_array_add (&args, NULL, NULL, "bar"));
	em2 = event_emit ("bilbo", args, NULL, NULL, NULL);
	em2->id = 101;
	em2->progress = EVENT_PENDING;
	em2->failed = TRUE;

	args = nih_str_array_new (NULL);
	NIH_MUST (nih_str_array_add (&args, NULL, NULL, "baggins"));

	env = nih_str_array_new (NULL);
	NIH_MUST (nih_str_array_add (&env, NULL, NULL, "FOO=BAR"));
	NIH_MUST (nih_str_array_add (&env, NULL, NULL, "TEA=YES"));
	em3 = event_emit ("drogo", args, env, NULL, NULL);
	em3->id = 102;
	em3->progress = EVENT_FINISHED;
	em3->failed = FALSE;

	output = tmpfile ();
	event_write_state (output);
	rewind (output);

	TEST_FILE_EQ (output, "Event frodo\n");
	TEST_FILE_EQ (output, ".id 100\n");
	TEST_FILE_EQ (output, ".progress 1\n");
	TEST_FILE_EQ (output, ".failed FALSE\n");
	TEST_FILE_EQ (output, "Event bilbo\n");
	TEST_FILE_EQ (output, ".arg foo\n");
	TEST_FILE_EQ (output, ".arg bar\n");
	TEST_FILE_EQ (output, ".id 101\n");
	TEST_FILE_EQ (output, ".progress 0\n");
	TEST_FILE_EQ (output, ".failed TRUE\n");
	TEST_FILE_EQ (output, "Event drogo\n");
	TEST_FILE_EQ (output, ".arg baggins\n");
	TEST_FILE_EQ (output, ".env FOO=BAR\n");
	TEST_FILE_EQ (output, ".env TEA=YES\n");
	TEST_FILE_EQ (output, ".id 102\n");
	TEST_FILE_EQ (output, ".progress 2\n");
	TEST_FILE_EQ (output, ".failed FALSE\n");
	TEST_FILE_EQ_N (output, "Emission ");
	TEST_FILE_END (output);

	fclose (output);

	nih_list_free (&em1->event.entry);
	nih_list_free (&em2->event.entry);
	nih_list_free (&em3->event.entry);
}


int
main (int   argc,
      char *argv[])
{
	test_new ();
	test_match ();
	test_emit ();
	test_emit_find_by_id ();
	test_emit_finished ();
	test_poll ();
	test_read_state ();
	test_write_state ();

	return 0;
}
