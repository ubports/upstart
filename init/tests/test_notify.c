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


static int destructor_called = 0;

static int
my_destructor (void *ptr)
{
	destructor_called++;

	return 0;
}

void
test_subscribe (void)
{
	NotifySubscription *sub1, *sub2;

	TEST_FUNCTION ("notify_subscribe");
	notify_subscribe (getpid (), NOTIFY_NONE, FALSE);

	/* Check that we can add a new subscription, the structure returned
	 * should be allocated with nih_alloc, placed in the subscriptions
	 * list and be filled out correctly.
	 */
	TEST_FEATURE ("with new subscription");
	TEST_ALLOC_FAIL {
		sub1 = notify_subscribe (123, NOTIFY_JOBS, TRUE);

		TEST_ALLOC_SIZE (sub1, sizeof (NotifySubscription));
		TEST_LIST_NOT_EMPTY (&sub1->entry);
		TEST_EQ (sub1->pid, 123);
		TEST_EQ (sub1->notify, NOTIFY_JOBS);
	}


	/* Check that we can amend an existing subscription to include
	 * more notification.  The object returned should be the same one.
	 */
	TEST_FEATURE ("with addition to existing subscription");
	TEST_ALLOC_FAIL {
		sub2 = notify_subscribe (123, NOTIFY_EVENTS, TRUE);

		TEST_EQ_P (sub2, sub1);
		TEST_EQ (sub1->pid, 123);
		TEST_EQ (sub1->notify, NOTIFY_JOBS | NOTIFY_EVENTS);
	}


	/* Check that we can amend an existing subscription to remove
	 * some notifications.  The object returned should still be the
	 * same one.
	 */
	TEST_FEATURE ("with removal from existing subscription");
	TEST_ALLOC_FAIL {
		sub2 = notify_subscribe (123, NOTIFY_JOBS, FALSE);

		TEST_EQ_P (sub2, sub1);
		TEST_EQ (sub1->pid, 123);
		TEST_EQ (sub1->notify, NOTIFY_EVENTS);
	}


	/* Check that we can add a subscription for a different process,
	 * the object returned should be a different one.
	 */
	TEST_FEATURE ("with second new subscription");
	TEST_ALLOC_FAIL {
		sub2 = notify_subscribe (456, NOTIFY_JOBS, TRUE);

		TEST_NE_P (sub2, sub1);
		TEST_ALLOC_SIZE (sub2, sizeof (NotifySubscription));
		TEST_LIST_NOT_EMPTY (&sub2->entry);
		TEST_EQ (sub2->pid, 456);
		TEST_EQ (sub2->notify, NOTIFY_JOBS);

		nih_list_free (&sub2->entry);
	}


	/* Check that a subscription is removed from the list and freed
	 * if we remove all notifications from it.  This should return NULL.
	 */
	TEST_FEATURE ("with removal");
	TEST_ALLOC_FAIL {
		destructor_called = 0;
		nih_alloc_set_destructor (sub1, my_destructor);

		sub2 = notify_subscribe (123, NOTIFY_EVENTS, FALSE);

		TEST_EQ_P (sub2, NULL);
		TEST_TRUE (destructor_called);
	}
}


static int
check_job_status (void               *data,
		  pid_t               pid,
		  UpstartMessageType  type,
		  const char         *name,
		  JobGoal             goal,
		  JobState            state,
		  ProcessState        process_state,
		  pid_t               process,
		  const char         *description)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_JOB_STATUS);
	TEST_EQ_STR (name, "test");
	TEST_EQ (goal, JOB_START);
	TEST_EQ (state, JOB_STOPPING);
	TEST_EQ (process_state, PROCESS_ACTIVE);
	TEST_EQ (process, 1000);
	TEST_EQ_STR (description, "a test job");

	return 0;
}

void
test_job (void)
{
	NihIo              *io;
	pid_t               pid;
	int                 wait_fd, status;
	Job                *job;
	NotifySubscription *sub;

	/* Check that subscribed processes receive a job status message when
	 * a job changes state.
	 */
	TEST_FUNCTION ("notify_job");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	job = job_new (NULL, "test");
	job->description = nih_strdup (job, "a test job");
	job->goal = JOB_START;
	job->state = JOB_STOPPING;
	job->process_state = PROCESS_ACTIVE;
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

	sub = notify_subscribe (pid, NOTIFY_JOBS, TRUE);

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
	     const char         *name,
	     char * const       *args,
	     char * const       *env)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_EVENT);
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
	NihIo              *io;
	pid_t               pid;
	int                 wait_fd, status;
	Event              *event;
	NotifySubscription *sub;

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

	sub = notify_subscribe (pid, NOTIFY_EVENTS, TRUE);

	event = event_new (NULL, "snarf");
	event->args = nih_str_array_new (event);
	NIH_MUST (nih_str_array_add (&event->args, event, NULL, "foo"));
	NIH_MUST (nih_str_array_add (&event->args, event, NULL, "bar"));
	event->env = nih_str_array_new (event);
	NIH_MUST (nih_str_array_add (&event->env, event, NULL, "FOO=BAR"));
	notify_event (event);

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	nih_list_free (&event->entry);
	nih_list_free (&sub->entry);

	control_close ();
	upstart_disable_safeties = FALSE;
}


int
main (int   argc,
      char *argv[])
{
	test_subscribe ();
	test_job ();
	test_event ();

	return 0;
}
