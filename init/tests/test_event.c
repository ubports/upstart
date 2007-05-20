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
test_info_new (void)
{
	EventInfo *event;

	/* Check that we can create a new Event structure, and have the
	 * details filled in and returned.  It should not be placed in
	 * any kind of list.
	 */
	TEST_FUNCTION ("event_info_new");
	event_poll ();
	TEST_ALLOC_FAIL {
		event = event_info_new (NULL, "test", NULL, NULL);

		if (test_alloc_failed) {
			TEST_EQ_P (event, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (event, sizeof (EventInfo));
		TEST_LIST_EMPTY (&event->entry);
		TEST_EQ_STR (event->name, "test");
		TEST_ALLOC_PARENT (event->name, event);

		TEST_EQ_P (event->args, NULL);
		TEST_EQ_P (event->env, NULL);

		nih_list_free (&event->entry);
	}
}

void
test_info_copy (void)
{
	EventInfo *event, *copy;

	TEST_FUNCTION ("event_copy");
	event = event_info_new (NULL, "test", NULL, NULL);

	/* Check that we can copy an event which does not have any arguments
	 * or environment variables.
	 */
	TEST_FEATURE ("without arguments or environment");
	TEST_ALLOC_FAIL {
		copy = event_info_copy (NULL, event);

		if (test_alloc_failed) {
			TEST_EQ_P (copy, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (copy, sizeof (EventInfo));
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
		copy = event_info_copy (NULL, event);

		if (test_alloc_failed) {
			TEST_EQ_P (copy, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (copy, sizeof (EventInfo));
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
	EventInfo *event1, *event2;
	char      *args1[] = { "foo", "bar", "baz", NULL };
	char      *args2[] = { "foo", "bar", "baz", NULL };

	TEST_FUNCTION ("event_match");

	/* Check that two events with different names do not match. */
	TEST_FEATURE ("with different name events");
	event1 = event_info_new (NULL, "foo", NULL, NULL);
	event2 = event_info_new (NULL, "bar", NULL, NULL);

	TEST_FALSE (event_match (event1, event2));


	/* Check that two events with the same names match. */
	TEST_FEATURE ("with same name events");
	nih_free (event2);
	event2 = event_info_new (NULL, "foo", NULL, NULL);

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
test_new (void)
{
	Event         *event;
	char         **args, **env;
	unsigned int   last_id = -1;

	/* Check that we can create a new event; the structure should
	 * be allocated with nih_alloc(), placed in a list and all of the
	 * details filled in.
	 */
	TEST_FUNCTION ("event_new");
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

		event = event_new (NULL, "test", args, env);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_NE (event->id, last_id);
		last_id = event->id;

		TEST_EQ (event->progress, EVENT_PENDING);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);

		TEST_EQ_STR (event->info.name, "test");
		TEST_ALLOC_PARENT (event->info.name, event);

		TEST_EQ_P (event->info.args, args);
		TEST_ALLOC_PARENT (event->info.args, event);

		TEST_EQ_P (event->info.env, env);
		TEST_ALLOC_PARENT (event->info.env, event);

		nih_list_free (&event->entry);
	}
}

void
test_find_by_id (void)
{
	Event        *event, *ret;
	unsigned int  id;

	TEST_FUNCTION ("event_find_by_id");

	/* Check that we can locate an event in the queue by its id, and
	 * have it returned.
	 */
	TEST_FEATURE ("with id in pending queue");
	event = event_new (NULL, "test", NULL, NULL);

	ret = event_find_by_id (event->id);

	TEST_EQ_P (ret, event);

	id = event->id;
	nih_list_free (&event->entry);


	/* Check that we get NULL if the id isn't in either queue. */
	TEST_FEATURE ("with id not in either queue");
	ret = event_find_by_id (id);

	TEST_EQ_P (ret, NULL);
}


void
test_ref (void)
{
	Event *event;

	/* Check that calling event_ref increments the number of references
	 * that the event has, while leaving the blockers at zero.
	 */
	TEST_FUNCTION ("event_ref");
	event = event_new (NULL, "test", NULL, NULL);
	event->refs = 4;

	event_ref (event);

	TEST_EQ (event->refs, 5);
	TEST_EQ (event->blockers, 0);

	nih_list_free (&event->entry);
}

void
test_unref (void)
{
	Event *event;

	/* Check that calling event_unref decrements the number of references
	 * that the event has, while leaving the blockers at zero.
	 */
	TEST_FUNCTION ("event_unref");
	event = event_new (NULL, "test", NULL, NULL);
	event->refs = 4;

	event_unref (event);

	TEST_EQ (event->refs, 3);
	TEST_EQ (event->blockers, 0);

	nih_list_free (&event->entry);
}

void
test_block (void)
{
	Event *event;

	/* Check that calling event_block increments the number of blockers
	 * that the event has, while leaving the references at zero.
	 */
	TEST_FUNCTION ("event_block");
	event = event_new (NULL, "test", NULL, NULL);
	event->blockers = 4;

	event_block (event);

	TEST_EQ (event->blockers, 5);
	TEST_EQ (event->refs, 0);

	nih_list_free (&event->entry);
}

void
test_unblock (void)
{
	Event *event;

	/* Check that calling event_unblock increments the number of blockers
	 * that the event has, while leaving the references at zero.
	 */
	TEST_FUNCTION ("event_unblock");
	event = event_new (NULL, "test", NULL, NULL);
	event->blockers = 4;

	event_unblock (event);

	TEST_EQ (event->blockers, 3);
	TEST_EQ (event->refs, 0);

	nih_list_free (&event->entry);
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
	     unsigned int        id,
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
		      unsigned int        id,
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
	Event     *event;
	Job       *job;
	EventInfo *event_info;
	NihIo     *io;
	int        wait_fd, status;
	pid_t      pid;

	TEST_FUNCTION ("event_poll");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	job_init ();
	event_init ();


	/* Check that a pending event in the queue is handled, with any
	 * subscribed processes being notified of the event and any
	 * jobs started or stopped as a result.  The event should remain
	 * in the handling state while the job is blocking it.
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
	job->process[PROCESS_MAIN] = job_process_new (job->process);
	job->process[PROCESS_MAIN]->command = "echo";

	event_info = event_info_new (job, "test", NULL, NULL);
	nih_list_add (&job->start_events, &event_info->entry);

	event = event_new (NULL, "test", NULL, NULL);
	event->id = 0xdeafbeef;

	notify_subscribe_event (NULL, pid, event);

	event_poll ();

	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_EQ (event->progress, EVENT_HANDLING);
	TEST_EQ (event->refs, 1);
	TEST_EQ (event->blockers, 1);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_GT (job->process[PROCESS_MAIN]->pid, 0);

	waitpid (job->process[PROCESS_MAIN]->pid, NULL, 0);

	TEST_LIST_EMPTY (subscriptions);

	nih_list_free (&job->entry);
	nih_list_free (&event->entry);


	/* Check that having a handling event in the queue which has blockers
	 * doesn't cause any problem.
	 */
	TEST_FEATURE ("with blocked handling event");
	TEST_ALLOC_FAIL {
		event = event_new (NULL, "test", NULL, NULL);
		event->progress = EVENT_HANDLING;
		event->blockers = 1;

		event_poll ();

		TEST_LIST_NOT_EMPTY (&event->entry);
		nih_list_free (&event->entry);
	}


	/* Check that we finish unblocked handling events, leaving the list
	 * empty.  Subscribed processes should be notified, blocked jobs
	 * should be releaed and the event should be freed.
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

	event = event_new (NULL, "test", NULL, NULL);
	event->id = 0xdeafbeef;
	event->progress = EVENT_HANDLING;

	job = job_new (NULL, "test");
	job->goal = JOB_START;
	job->state = JOB_STARTING;
	job->blocked = event;
	job->process[PROCESS_MAIN] = job_process_new (job->process);
	job->process[PROCESS_MAIN]->command = "echo";

	event_ref (job->blocked);

	destructor_called = 0;
	nih_alloc_set_destructor (event, my_destructor);

	notify_subscribe_event (NULL, pid, event);

	event_poll ();

	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_GT (job->process[PROCESS_MAIN]->pid, 0);

	waitpid (job->process[PROCESS_MAIN]->pid, &status, 0);
	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_EQ_P (job->blocked, NULL);

	TEST_TRUE (destructor_called);

	TEST_LIST_EMPTY (subscriptions);


	/* Check that a finished event with remaining references is held
	 * in the done state, and not freed immediately.
	 */
	TEST_FEATURE ("with referenced event");
	TEST_ALLOC_FAIL {
		event = event_new (NULL, "test", NULL, NULL);
		event->progress = EVENT_DONE;
		event->refs = 1;

		event_poll ();

		TEST_LIST_NOT_EMPTY (&event->entry);
		nih_list_free (&event->entry);
	}


	/* Check that a pending event which doesn't cause any jobs to be
	 * changed goes straight into the done state, thus getting
	 * destroyed.
	 */
	TEST_FEATURE ("with no-op pending event");
	TEST_ALLOC_FAIL {
		event = event_new (NULL, "test", NULL, NULL);

		destructor_called = 0;
		nih_alloc_set_destructor (event, my_destructor);

		event_poll ();

		TEST_TRUE (destructor_called);
	}


	/* Check that a failed event causes another event to be emitted
	 * that has "/failed" appended on the end.  We can obtain the
	 * failed event by hooking a job on it, and using the
	 * cause.
	 */
	TEST_FEATURE ("with failed event");
	event = event_new (NULL, "test", NULL, NULL);
	event->failed = TRUE;
	event->progress = EVENT_FINISHED;

	job = job_new (NULL, "test");
	job->process[PROCESS_MAIN] = job_process_new (job->process);
	job->process[PROCESS_MAIN]->command = "echo";

	event_info = event_info_new (job, "test/failed", NULL, NULL);
	nih_list_add (&job->start_events, &event_info->entry);

	destructor_called = 0;
	nih_alloc_set_destructor (event, my_destructor);

	event_poll ();

	TEST_TRUE (destructor_called);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_GT (job->process[PROCESS_MAIN]->pid, 0);

	waitpid (job->process[PROCESS_MAIN]->pid, NULL, 0);

	TEST_EQ_STR (job->cause->info.name, "test/failed");

	event_poll ();

	nih_list_free (&job->entry);


	/* Check that failed events do not, themselves, emit new failed
	 * events (otherwise we could be there all night :p)
	 */
	TEST_FEATURE ("with failed failed event");
	event = event_new (NULL, "test/failed", NULL, NULL);
	event->failed = TRUE;
	event->progress = EVENT_FINISHED;

	job = job_new (NULL, "test");
	job->process[PROCESS_MAIN] = job_process_new (job->process);
	job->process[PROCESS_MAIN]->command = "echo";

	event_info = event_info_new (job, "test/failed", NULL, NULL);
	nih_list_add (&job->start_events, &event_info->entry);

	event_info = event_info_new (job, "test/failed/failed", NULL, NULL);
	nih_list_add (&job->start_events, &event_info->entry);

	destructor_called = 0;
	nih_alloc_set_destructor (event, my_destructor);

	event_poll ();

	TEST_TRUE (destructor_called);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_WAITING);
	TEST_EQ (job->process[PROCESS_MAIN]->pid, 0);

	nih_list_free (&job->entry);


	control_close ();
	upstart_disable_safeties = FALSE;
}


int
main (int   argc,
      char *argv[])
{
	test_info_new ();
	test_info_copy ();
	test_match ();
	test_new ();
	test_find_by_id ();
	test_ref ();
	test_unref ();
	test_block ();
	test_unblock ();
	test_poll ();

	return 0;
}
