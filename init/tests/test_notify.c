/* upstart
 *
 * test_notify.c - test suite for init/notify.c
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

#include <stdlib.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/io.h>

#include <upstart/message.h>

#include "event.h"
#include "job.h"
#include "control.h"
#include "notify.h"


extern int upstart_disable_safeties;


void
test_subscribe_job (void)
{
	NotifySubscription *sub;
	Job                *job;

	TEST_FUNCTION ("notify_subscribe_job");

	/* Check that we can add a new subscription on a specific job;
	 * the structure returned should b e allocated with nih_alloc(),
	 * placed in the subscriptions list and have the details filled
	 * out correctly.
	 */
	TEST_FEATURE ("with subscription to job");
	job = job_new (NULL, "test");

	TEST_ALLOC_FAIL {
		sub = notify_subscribe_job (NULL, 1000, job);

		TEST_ALLOC_SIZE (sub, sizeof (NotifySubscription));
		TEST_LIST_NOT_EMPTY (&sub->entry);
		TEST_EQ (sub->pid, 1000);
		TEST_EQ (sub->type, NOTIFY_JOB);
		TEST_EQ_P (sub->job, job);

		nih_list_free (&sub->entry);
	}

	nih_list_free (&job->entry);


	/* Check that we can subscribe to any job by passing in NULL. */
	TEST_FEATURE ("with subscription to all jobs");
	TEST_ALLOC_FAIL {
		sub = notify_subscribe_job (NULL, 1000, NULL);

		TEST_ALLOC_SIZE (sub, sizeof (NotifySubscription));
		TEST_LIST_NOT_EMPTY (&sub->entry);
		TEST_EQ (sub->pid, 1000);
		TEST_EQ (sub->type, NOTIFY_JOB);
		TEST_EQ_P (sub->job, NULL);

		nih_list_free (&sub->entry);
	}
}

void
test_subscribe_event (void)
{
	NotifySubscription *sub;
	EventEmission      *emission;

	TEST_FUNCTION ("notify_subscribe_event");

	/* Check that we can add a new subscription on a specific event
	 * emission; the structure returned should be allocated with
	 * nih_alloc(), placed in the subscriptions list and have the
	 * details filled out correctly.
	 */
	TEST_FEATURE ("with subscription to emission");
	emission = event_emit ("test", NULL, NULL);

	TEST_ALLOC_FAIL {
		sub = notify_subscribe_event (NULL, 1000, emission);

		TEST_ALLOC_SIZE (sub, sizeof (NotifySubscription));
		TEST_LIST_NOT_EMPTY (&sub->entry);
		TEST_EQ (sub->pid, 1000);
		TEST_EQ (sub->type, NOTIFY_EVENT);
		TEST_EQ_P (sub->emission, emission);

		nih_list_free (&sub->entry);
	}

	nih_list_free (&emission->event.entry);


	/* Check that we can subscribe to any event by passing in NULL. */
	TEST_FEATURE ("with subscription to all events");
	TEST_ALLOC_FAIL {
		sub = notify_subscribe_event (NULL, 1000, NULL);

		TEST_ALLOC_SIZE (sub, sizeof (NotifySubscription));
		TEST_LIST_NOT_EMPTY (&sub->entry);
		TEST_EQ (sub->pid, 1000);
		TEST_EQ (sub->type, NOTIFY_EVENT);
		TEST_EQ_P (sub->emission, NULL);

		nih_list_free (&sub->entry);
	}
}


void
test_subscription_find (void)
{
	NotifySubscription *sub1, *sub2, *sub3, *sub4, *ret;

	TEST_FUNCTION ("notify_subscription_find");
	sub1 = notify_subscribe_job (NULL, 1000, (void *)&sub1);
	sub2 = notify_subscribe_event (NULL, 1001, (void *)&sub2);
	sub3 = notify_subscribe_job (NULL, 1000, NULL);
	sub4 = notify_subscribe_event (NULL, 1000, NULL);


	/* Check that we can find a job subscription with the right pid
	 * and Job record.
	 */
	TEST_FEATURE ("with subscription to job");
	ret = notify_subscription_find (1000, NOTIFY_JOB, &sub1);

	TEST_EQ_P (ret, sub1);


	/* Check that we can find an event subscription with the right pid
	 * and EventEmission record.
	 */
	TEST_FEATURE ("with subscription to job");
	ret = notify_subscription_find (1001, NOTIFY_EVENT, &sub2);

	TEST_EQ_P (ret, sub2);


	/* Check that we can find a subscription to all jobs
	 * with the right pid.
	 */
	TEST_FEATURE ("with subscription to all jobs");
	ret = notify_subscription_find (1000, NOTIFY_JOB, NULL);

	TEST_EQ_P (ret, sub3);


	/* Check that we can find a subscription to all events
	 * with the right pid.
	 */
	TEST_FEATURE ("with subscription to all events");
	ret = notify_subscription_find (1000, NOTIFY_EVENT, NULL);

	TEST_EQ_P (ret, sub4);


	/* Check that no match returns NULL */
	TEST_FEATURE ("with no matching subscription");
	ret = notify_subscription_find (1001, NOTIFY_JOB, &sub2);

	TEST_EQ_P (ret, NULL);

	nih_list_free (&sub1->entry);
	nih_list_free (&sub2->entry);
	nih_list_free (&sub3->entry);
	nih_list_free (&sub4->entry);
}


static int destructor_called = 0;

static int
my_destructor (void *ptr)
{
	destructor_called++;

	return 0;
}

void
test_unsubscribe (void)
{
	NotifySubscription *sub1, *sub2, *sub3;

	/* Check that unsubscribe removes and frees all subscriptions for
	 * the given process id from the list.
	 */
	TEST_FUNCTION ("notify_unsubscribe");
	notify_init ();
	sub1 = notify_subscribe_job (NULL, 1000, NULL);
	sub2 = notify_subscribe_event (NULL, 1001, NULL);
	sub3 = notify_subscribe_event (NULL, 1000, NULL);

	destructor_called = 0;
	nih_alloc_set_destructor (sub1, my_destructor);
	nih_alloc_set_destructor (sub2, my_destructor);
	nih_alloc_set_destructor (sub3, my_destructor);

	notify_unsubscribe (1000);

	TEST_EQ (destructor_called, 2);

	nih_list_free (&sub2->entry);

	TEST_LIST_EMPTY (subscriptions);
}


static int
check_job_status (void               *data,
		  pid_t               pid,
		  UpstartMessageType  type,
		  const char         *name,
		  JobGoal             goal,
		  JobState            state,
		  pid_t               process)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_JOB_STATUS);
	TEST_EQ_STR (name, "test");
	TEST_EQ (goal, JOB_START);
	TEST_EQ (state, JOB_SPAWNED);
	TEST_EQ (process, 1000);

	return 0;
}

static int
check_event_job_status (void               *data,
			pid_t               pid,
			UpstartMessageType  type,
			uint32_t            id,
			const char         *name,
			JobGoal             goal,
			JobState            state,
			pid_t               process)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_EVENT_JOB_STATUS);
	TEST_EQ_U (id, 0xdeafbeef);
	TEST_EQ_STR (name, "test");
	TEST_EQ (goal, JOB_START);
	TEST_EQ (state, JOB_SPAWNED);
	TEST_EQ (process, 1000);

	return 0;
}

void
test_job (void)
{
	NihIo              *io;
	pid_t               pid;
	int                 wait_fd, status;
	Job                *job;
	EventEmission      *emission;
	NotifySubscription *sub;

	TEST_FUNCTION ("notify_job");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	/* Check that subscribed processes receive a job status message when
	 * a job changes state.
	 */
	TEST_FEATURE ("with subscription to job");
	job = job_new (NULL, "test");
	job->description = nih_strdup (job, "a test job");
	job->goal = JOB_START;
	job->state = JOB_SPAWNED;
	job->pid = 1000;

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		/* Release the parent so we can receive the job notification */
		TEST_CHILD_RELEASE (wait_fd);

		/* Wait for a reply */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status,
						       NULL));
		nih_free (message);

		exit (0);
	}

	sub = notify_subscribe_job (NULL, pid, job);

	notify_job (job);

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	nih_list_free (&job->entry);
	nih_list_free (&sub->entry);


	/* Check that a job status change also notifies any processes
	 * subscribed to its cause event, with the slightly different
	 * event job status message that includes the event id.
	 */
	TEST_FEATURE ("with subscription to cause event");
	job = job_new (NULL, "test");
	job->description = nih_strdup (job, "a test job");
	job->goal = JOB_START;
	job->state = JOB_SPAWNED;
	job->pid = 1000;

	emission = event_emit ("test", NULL, NULL);
	emission->id = 0xdeafbeef;

	job->cause = emission;

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		/* Release the parent so we can receive the job notification */
		TEST_CHILD_RELEASE (wait_fd);

		/* Wait for a reply */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_event_job_status,
						       NULL));
		nih_free (message);

		exit (0);
	}

	sub = notify_subscribe_event (NULL, pid, emission);

	notify_job (job);

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	nih_list_free (&job->entry);
	nih_list_free (&sub->entry);


	control_close ();
	upstart_disable_safeties = FALSE;
}

void
test_job_event (void)
{
	NihIo              *io;
	pid_t               pid;
	int                 wait_fd, status;
	Job                *job;
	EventEmission      *emission;
	NotifySubscription *sub;

	/* Check that processes subscribed to the job's cause event
	 * receive an event job status message that includes the event id.
	 */
	TEST_FUNCTION ("notify_job_event");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	job = job_new (NULL, "test");
	job->description = nih_strdup (job, "a test job");
	job->goal = JOB_START;
	job->state = JOB_SPAWNED;
	job->pid = 1000;

	emission = event_emit ("test", NULL, NULL);
	emission->id = 0xdeafbeef;

	job->cause = emission;

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		/* Release the parent so we can receive the job notification */
		TEST_CHILD_RELEASE (wait_fd);

		/* Wait for a reply */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_event_job_status,
						       NULL));
		nih_free (message);

		exit (0);
	}

	sub = notify_subscribe_event (NULL, pid, emission);

	notify_job (job);

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	nih_list_free (&job->entry);
	nih_list_free (&sub->entry);


	control_close ();
	upstart_disable_safeties = FALSE;
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
	TEST_EQ_STR (name, "snarf");

	TEST_ALLOC_SIZE (args, sizeof (char *) * 3);
	TEST_ALLOC_PARENT (args[0], args);
	TEST_ALLOC_PARENT (args[1], args);
	TEST_EQ_STR (args[0], "foo");
	TEST_EQ_STR (args[1], "bar");
	TEST_EQ_P (args[2], NULL);

	TEST_ALLOC_SIZE (env, sizeof (char *) * 2);
	TEST_ALLOC_PARENT (env[0], env);
	TEST_EQ_STR (env[0], "FOO=BAR");
	TEST_EQ_P (env[1], NULL);

	return 0;
}

void
test_event (void)
{
	NihIo               *io;
	pid_t                pid;
	int                  wait_fd, status;
	EventEmission       *emission;
	char               **args, **env;
	NotifySubscription  *sub;

	/* Check that subscribed processes receive an event message when
	 * an event is emitted.
	 */
	TEST_FUNCTION ("notify_event");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		/* Release the parent so we can receive the
		 * event notification */
		TEST_CHILD_RELEASE (wait_fd);

		/* Wait for a reply */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_event,
						       NULL));
		nih_free (message);

		exit (0);
	}

	args = nih_str_array_new (NULL);
	NIH_MUST (nih_str_array_add (&args, NULL, NULL, "foo"));
	NIH_MUST (nih_str_array_add (&args, NULL, NULL, "bar"));

	env = nih_str_array_new (NULL);
	NIH_MUST (nih_str_array_add (&env, NULL, NULL, "FOO=BAR"));

	emission = event_emit ("snarf", args, env);
	emission->id = 0xdeafbeef;

	sub = notify_subscribe_event (NULL, pid, emission);

	notify_event (emission);

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	nih_list_free (&emission->event.entry);
	nih_list_free (&sub->entry);

	control_close ();
	upstart_disable_safeties = FALSE;
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
	TEST_EQ_STR (name, "snarf");

	TEST_ALLOC_SIZE (args, sizeof (char *) * 3);
	TEST_ALLOC_PARENT (args[0], args);
	TEST_ALLOC_PARENT (args[1], args);
	TEST_EQ_STR (args[0], "foo");
	TEST_EQ_STR (args[1], "bar");
	TEST_EQ_P (args[2], NULL);

	TEST_ALLOC_SIZE (env, sizeof (char *) * 2);
	TEST_ALLOC_PARENT (env[0], env);
	TEST_EQ_STR (env[0], "FOO=BAR");
	TEST_EQ_P (env[1], NULL);

	return 0;
}

void
test_event_finished (void)
{
	NihIo               *io;
	pid_t                pid;
	int                  wait_fd, status;
	EventEmission       *emission;
	char               **args, **env;
	NotifySubscription  *sub;

	/* Check that subscribed processes receive an event message when
	 * handling of an event is finished.
	 */
	TEST_FUNCTION ("notify_event_finished");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		/* Release the parent so we can receive the
		 * event notification */
		TEST_CHILD_RELEASE (wait_fd);

		/* Wait for a reply */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_event_finished,
						       NULL));
		nih_free (message);

		exit (0);
	}

	args = nih_str_array_new (NULL);
	NIH_MUST (nih_str_array_add (&args, NULL, NULL, "foo"));
	NIH_MUST (nih_str_array_add (&args, NULL, NULL, "bar"));

	env = nih_str_array_new (NULL);
	NIH_MUST (nih_str_array_add (&env, NULL, NULL, "FOO=BAR"));

	emission = event_emit ("snarf", args, env);
	emission->id = 0xdeafbeef;
	emission->failed = FALSE;

	sub = notify_subscribe_event (NULL, pid, emission);

	notify_event_finished (emission);

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	nih_list_free (&emission->event.entry);
	nih_list_free (&sub->entry);

	control_close ();
	upstart_disable_safeties = FALSE;
}


int
main (int   argc,
      char *argv[])
{
	test_subscribe_job ();
	test_subscribe_event ();
	test_subscription_find ();
	test_unsubscribe ();
	test_job ();
	test_job_event ();
	test_event ();
	test_event_finished ();

	return 0;
}
