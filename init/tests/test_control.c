/* upstart
 *
 * test_control.c - test suite for init/control.c
 *
 * Copyright © 2006 Canonical Ltd.
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
#include <sys/socket.h>
#include <sys/un.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/io.h>

#include <upstart/control.h>

#include "event.h"
#include "job.h"
#include "control.h"


extern int upstart_disable_safeties;


void
test_open (void)
{
	NihIoWatch         *watch;
	UpstartMsg         *message;
	ControlMsg         *msg;
	struct sockaddr_un  addr;
	int                 val;
	char                name[26];
	socklen_t           len;

	TEST_FUNCTION ("control_open");

	/* Check that we can open the control socket when there's an empty
	 * send queue.  The returned structure should be an NihIoWatch on
	 * a non-blocking, close-on-exec socket that matches the parameters
	 * of the upstart communication socket.  Because the send queue is
	 * empty, this should only be watching for read.
	 */
	TEST_FEATURE ("with empty send queue");
	watch = control_open ();

	TEST_ALLOC_SIZE (watch, sizeof (NihIoWatch));
	TEST_EQ (watch->events, NIH_IO_READ);

	len = sizeof (addr);
	getsockname (watch->fd, (struct sockaddr *)&addr, &len);

	TEST_EQ (addr.sun_family, AF_UNIX);
	TEST_EQ (addr.sun_path[0], '\0');

	sprintf (name, "/com/ubuntu/upstart/%d", getpid ());
	TEST_EQ_STRN (addr.sun_path + 1, name);

	val = 0;
	len = sizeof (val);
	getsockopt (watch->fd, SOL_SOCKET, SO_TYPE, &val, &len);

	TEST_EQ (val, SOCK_DGRAM);

	val = 0;
	len = sizeof (val);
	getsockopt (watch->fd, SOL_SOCKET, SO_PASSCRED, &val, &len);
	TEST_NE (val, 0);

	TEST_TRUE (fcntl (watch->fd, F_GETFL) & O_NONBLOCK);
	TEST_TRUE (fcntl (watch->fd, F_GETFD) & FD_CLOEXEC);

	control_close ();


	/* Check that we can open the control socket when there's data in
	 * the send queue, which means we should also be watching for write.
	 */
	TEST_FEATURE ("with non-empty send queue");
	message = nih_new (NULL, UpstartMsg);
	message->type = UPSTART_NO_OP;
	msg = control_send (123, message);

	watch = control_open ();

	TEST_EQ (watch->events, NIH_IO_READ | NIH_IO_WRITE);

	control_close ();

	nih_list_free (&msg->entry);
	nih_free (message);
}


static int was_called = 0;

static int
my_destructor (void *ptr)
{
	was_called++;

	return 0;
}

void
test_close (void)
{
	NihIoWatch *watch;
	int         fd;

	/* Check that when we close the control socket, the watch structure
	 * is freed and the socket itself closed.
	 */
	TEST_FUNCTION ("control_close");
	watch = control_open ();
	fd = watch->fd;

	was_called = 0;
	nih_alloc_set_destructor (watch, my_destructor);

	control_close ();

	TEST_TRUE (was_called);
	TEST_LT (fcntl (fd, F_GETFD), 0);
	TEST_EQ (errno, EBADF);
}


void
test_subscribe (void)
{
	ControlSub *sub1, *sub2;

	TEST_FUNCTION ("control_subscribe");

	/* Check that we can add a new subscription, the structure returned
	 * should be allocated with nih_alloc, placed in the subscriptions
	 * list and be filled out correctly.
	 */
	TEST_FEATURE ("with new subscription");
	sub1 = control_subscribe (123, NOTIFY_JOBS, TRUE);

	TEST_ALLOC_SIZE (sub1, sizeof (ControlSub));
	TEST_LIST_NOT_EMPTY (&sub1->entry);
	TEST_EQ (sub1->pid, 123);
	TEST_EQ (sub1->notify, NOTIFY_JOBS);


	/* Check that we can amend an existing subscription to include
	 * more notification.  The object returned should be the same one.
	 */
	TEST_FEATURE ("with addition to existing subscription");
	sub2 = control_subscribe (123, NOTIFY_EVENTS, TRUE);

	TEST_EQ_P (sub2, sub1);
	TEST_EQ (sub1->pid, 123);
	TEST_EQ (sub1->notify, NOTIFY_JOBS | NOTIFY_EVENTS);


	/* Check that we can amend an existing subscription to remove
	 * some notifications.  The object returned should still be the
	 * same one.
	 */
	TEST_FEATURE ("with removal from existing subscription");
	sub2 = control_subscribe (123, NOTIFY_JOBS, FALSE);

	TEST_EQ_P (sub2, sub1);
	TEST_EQ (sub1->pid, 123);
	TEST_EQ (sub1->notify, NOTIFY_EVENTS);


	/* Check that we can add a subscription for a different process,
	 * the object returned should be a different one.
	 */
	TEST_FEATURE ("with second new subscription");
	sub2 = control_subscribe (456, NOTIFY_JOBS, TRUE);

	TEST_NE_P (sub2, sub1);
	TEST_ALLOC_SIZE (sub2, sizeof (ControlSub));
	TEST_LIST_NOT_EMPTY (&sub2->entry);
	TEST_EQ (sub2->pid, 456);
	TEST_EQ (sub2->notify, NOTIFY_JOBS);

	nih_list_free (&sub2->entry);


	/* Check that a subscription is removed from the list and freed
	 * if we remove all notifications from it.  This should return NULL.
	 */
	TEST_FEATURE ("with removal");
	was_called = 0;
	nih_alloc_set_destructor (sub1, my_destructor);

	sub2 = control_subscribe (123, NOTIFY_EVENTS, FALSE);

	TEST_EQ_P (sub2, NULL);
	TEST_TRUE (was_called);
}

void
test_send (void)
{
	ControlMsg *msg;
	UpstartMsg *message;
	NihIoWatch *watch;

	TEST_FUNCTION ("control_send");
	message = nih_new (NULL, UpstartMsg);
	watch = control_open ();


	/* Check that sending a no-op message results in a ControlMsg
	 * structure being allocated with nih_alloc, placed in the send
	 * queue, and the contents of the UpstartMsg we give copied into
	 * it.
	 *
	 * In addition, the control socket watch should now be watching
	 * for writability.
	 */
	TEST_FEATURE ("with no-op message");
	message->type = UPSTART_NO_OP;
	msg = control_send (123, message);

	TEST_ALLOC_SIZE (msg, sizeof (ControlMsg));
	TEST_LIST_NOT_EMPTY (&msg->entry);
	TEST_EQ (msg->pid, 123);
	TEST_EQ (msg->message.type, UPSTART_NO_OP);

	TEST_TRUE (watch->events & NIH_IO_WRITE);

	nih_list_free (&msg->entry);


	/* Check that a job-start message is copied correctly. */
	TEST_FEATURE ("with job start message");
	message->type = UPSTART_JOB_START;
	message->name = "wibble";
	msg = control_send (123, message);

	TEST_ALLOC_SIZE (msg, sizeof (ControlMsg));
	TEST_LIST_NOT_EMPTY (&msg->entry);
	TEST_EQ (msg->pid, 123);
	TEST_EQ (msg->message.type, UPSTART_JOB_START);

	TEST_EQ_STR (msg->message.name, "wibble");
	TEST_ALLOC_PARENT (msg->message.name, msg);

	nih_list_free (&msg->entry);


	/* Check that a job-status message is copied correctly. */
	TEST_FEATURE ("with job status message");
	message->type = UPSTART_JOB_STATUS;
	message->name = "wibble";
	message->description = "frodo";
	msg = control_send (123, message);

	TEST_ALLOC_SIZE (msg, sizeof (ControlMsg));
	TEST_LIST_NOT_EMPTY (&msg->entry);
	TEST_EQ (msg->pid, 123);
	TEST_EQ (msg->message.type, UPSTART_JOB_STATUS);

	TEST_EQ_STR (msg->message.name, "wibble");
	TEST_ALLOC_PARENT (msg->message.name, msg);
	TEST_EQ_STR (msg->message.description, "frodo");
	TEST_ALLOC_PARENT (msg->message.description, msg);

	nih_list_free (&msg->entry);


	/* Check that a queue-event message is copied correctly. */
	TEST_FEATURE ("with queue event message");
	message->type = UPSTART_EVENT_QUEUE;
	message->name = "wibble";
	msg = control_send (123, message);

	TEST_ALLOC_SIZE (msg, sizeof (ControlMsg));
	TEST_LIST_NOT_EMPTY (&msg->entry);
	TEST_EQ (msg->pid, 123);
	TEST_EQ (msg->message.type, UPSTART_EVENT_QUEUE);

	TEST_EQ_STR (msg->message.name, "wibble");
	TEST_ALLOC_PARENT (msg->message.name, msg);

	nih_list_free (&msg->entry);


	/* Check that an event message is copied correctly */
	TEST_FEATURE ("with event message");
	message->type = UPSTART_EVENT;
	message->name = "foo";
	msg = control_send (123, message);

	TEST_ALLOC_SIZE (msg, sizeof (ControlMsg));
	TEST_LIST_NOT_EMPTY (&msg->entry);
	TEST_EQ (msg->pid, 123);
	TEST_EQ (msg->message.type, UPSTART_EVENT);

	TEST_EQ_STR (msg->message.name, "foo");
	TEST_ALLOC_PARENT (msg->message.name, msg);

	nih_list_free (&msg->entry);


	nih_free (message);
	control_close ();
}


enum {
	TEST_SILLY,
	TEST_NO_OP,
	TEST_JOB_UNKNOWN,
	TEST_JOB_START,
	TEST_JOB_STOP,
	TEST_JOB_QUERY,
	TEST_JOB_STATUS,
	TEST_JOB_LIST,
	TEST_EVENT,
	TEST_EVENT_TRIGGERED,
	TEST_JOB_WATCH,
	TEST_EVENT_WATCH,
	TEST_SHUTDOWN
};

static void
watcher_child (int test,
	       int fd)
{
	UpstartMsg *s_msg, *r_msg;
	int         sock;

	/* This function behaves as the child in a communication with the
	 * parent which runs the control_watcher() function.  Some of
	 * the tests only send something to the watcher, and the checks are
	 * done in the calling function -- other involve using the child
	 * to manipulate the parent's state, with checks done at either this
	 * or both ends.
	 *
	 * It's in the foreground, so it can output messages; the parent
	 * will need to catch a non-zero exit code.
	 */

	sock = upstart_open ();
	s_msg = nih_new (NULL, UpstartMsg);

	switch (test) {
	case TEST_SILLY:
		/* Send an odd message; this should just get ignored. */
		s_msg->type = UPSTART_JOB_UNKNOWN;
		s_msg->name = "eh";
		upstart_send_msg_to (getppid (), sock, s_msg);
		break;
	case TEST_NO_OP:
		/* Send a no-op message; this should just get ignored. */
		s_msg->type = UPSTART_NO_OP;
		upstart_send_msg_to (getppid (), sock, s_msg);
		break;
	case TEST_JOB_UNKNOWN:
		/* Send a job-start message with an unknown job. */
		s_msg->type = UPSTART_JOB_START;
		s_msg->name = "wibble";
		upstart_send_msg_to (getppid (), sock, s_msg);

		TEST_CHILD_RELEASE (fd);

		/* Check that we receive an unknown-job response containing
		 * the name of the job we tried.
		 */
		r_msg = upstart_recv_msg (NULL, sock, NULL);

		TEST_EQ (r_msg->type, UPSTART_JOB_UNKNOWN);
		TEST_EQ_STR (r_msg->name, "wibble");
		break;
	case TEST_JOB_START:
		/* Send a job-start message with a known job. */
		s_msg->type = UPSTART_JOB_START;
		s_msg->name = "test";
		upstart_send_msg_to (getppid (), sock, s_msg);

		TEST_CHILD_RELEASE (fd);

		/* Check that we receive a job-status response that indicates
		 * the job is now running.
		 */
		r_msg = upstart_recv_msg (NULL, sock, NULL);

		TEST_EQ (r_msg->type, UPSTART_JOB_STATUS);
		TEST_EQ_STR (r_msg->name, "test");
		TEST_EQ (r_msg->goal, JOB_START);
		TEST_EQ (r_msg->state, JOB_RUNNING);
		TEST_EQ (r_msg->process_state, PROCESS_ACTIVE);
		break;
	case TEST_JOB_STOP:
		/* Send a job-stop message. */
		s_msg->type = UPSTART_JOB_STOP;
		s_msg->name = "test";
		upstart_send_msg_to (getppid (), sock, s_msg);

		TEST_CHILD_RELEASE (fd);

		/* Check that we receive a job-status response that indicates
		 * that the job has been asked to stop and killed
		 */
		r_msg = upstart_recv_msg (NULL, sock, NULL);

		TEST_EQ (r_msg->type, UPSTART_JOB_STATUS);
		TEST_EQ_STR (r_msg->name, "test");
		TEST_EQ (r_msg->goal, JOB_STOP);
		TEST_EQ (r_msg->state, JOB_RUNNING);
		TEST_EQ (r_msg->process_state, PROCESS_KILLED);
		break;
	case TEST_JOB_QUERY:
		/* Send a job-query message. */
		s_msg->type = UPSTART_JOB_QUERY;
		s_msg->name = "test";
		upstart_send_msg_to (getppid (), sock, s_msg);

		TEST_CHILD_RELEASE (fd);

		/* Check that we receive a job-status response with the
		 * full information about the job in it.
		 */
		r_msg = upstart_recv_msg (NULL, sock, NULL);

		TEST_EQ (r_msg->type, UPSTART_JOB_STATUS);
		TEST_EQ_STR (r_msg->name, "test");
		TEST_EQ_STR (r_msg->description, "a test job");
		TEST_EQ (r_msg->goal, JOB_START);
		TEST_EQ (r_msg->state, JOB_STOPPING);
		TEST_EQ (r_msg->process_state, PROCESS_ACTIVE);
		break;
	case TEST_JOB_LIST:
		/* Send a job-list message. */
		s_msg->type = UPSTART_JOB_LIST;
		upstart_send_msg_to (getppid (), sock, s_msg);

		TEST_CHILD_RELEASE (fd);

		/* Check that we receive a job-status response with the
		 * full information about the job in it.
		 */
		r_msg = upstart_recv_msg (NULL, sock, NULL);

		TEST_EQ (r_msg->type, UPSTART_JOB_STATUS);
		TEST_EQ_STR (r_msg->name, "test");
		TEST_EQ_STR (r_msg->description, "a test job");
		TEST_EQ (r_msg->goal, JOB_START);
		TEST_EQ (r_msg->state, JOB_STOPPING);
		TEST_EQ (r_msg->process_state, PROCESS_ACTIVE);

		/* Check that we also receive a job-list-end response. */
		r_msg = upstart_recv_msg (NULL, sock, NULL);

		TEST_EQ (r_msg->type, UPSTART_JOB_LIST_END);
		break;
	case TEST_JOB_STATUS:
		/* Check that we receive a job-status response with the
		 * full information about the job in it.
		 */
		TEST_CHILD_RELEASE (fd);

		r_msg = upstart_recv_msg (NULL, sock, NULL);

		TEST_EQ (r_msg->type, UPSTART_JOB_STATUS);
		TEST_EQ_STR (r_msg->name, "test");
		TEST_EQ_STR (r_msg->description, "a test job");
		TEST_EQ (r_msg->goal, JOB_START);
		TEST_EQ (r_msg->state, JOB_STOPPING);
		TEST_EQ (r_msg->process_state, PROCESS_ACTIVE);
		break;
	case TEST_EVENT:
		/* Send an event-queue message */
		s_msg->type = UPSTART_EVENT_QUEUE;
		s_msg->name = "snarf";
		upstart_send_msg_to (getppid (), sock, s_msg);
		break;
	case TEST_EVENT_TRIGGERED:
		/* Check that we receive an event message. */
		TEST_CHILD_RELEASE (fd);

		r_msg = upstart_recv_msg (NULL, sock, NULL);

		TEST_EQ (r_msg->type, UPSTART_EVENT);
		TEST_EQ_STR (r_msg->name, "snarf");
		break;
	case TEST_JOB_WATCH:
		/* Send a watch-jobs message. */
		s_msg->type = UPSTART_WATCH_JOBS;
		upstart_send_msg_to (getppid (), sock, s_msg);

		TEST_CHILD_RELEASE (fd);

		/* Check that we receive a job-status response with the
		 * full information about the job in it.
		 */
		r_msg = upstart_recv_msg (NULL, sock, NULL);

		TEST_EQ (r_msg->type, UPSTART_JOB_STATUS);
		TEST_EQ_STR (r_msg->name, "test");
		TEST_EQ_STR (r_msg->description, "a test job");
		TEST_EQ (r_msg->goal, JOB_START);
		TEST_EQ (r_msg->state, JOB_STOPPING);
		TEST_EQ (r_msg->process_state, PROCESS_ACTIVE);

		/* Send an unwatch-jobs message. */
		s_msg->type = UPSTART_UNWATCH_JOBS;
		upstart_send_msg_to (getppid (), sock, s_msg);
		break;
	case TEST_EVENT_WATCH:
		/* Send a watch-events message. */
		s_msg->type = UPSTART_WATCH_EVENTS;
		upstart_send_msg_to (getppid (), sock, s_msg);

		TEST_CHILD_RELEASE (fd);

		/* Check that we receive an event message. */
		r_msg = upstart_recv_msg (NULL, sock, NULL);

		TEST_EQ (r_msg->type, UPSTART_EVENT);
		TEST_EQ_STR (r_msg->name, "snarf");

		/* Send an unwatch-events message. */
		s_msg->type = UPSTART_UNWATCH_EVENTS;
		upstart_send_msg_to (getppid (), sock, s_msg);
		break;
	case TEST_SHUTDOWN:
		/* First send a watch-events message, so we can see what
		 * shutdown does.
		 */
		s_msg->type = UPSTART_WATCH_EVENTS;
		upstart_send_msg_to (getppid (), sock, s_msg);

		TEST_CHILD_RELEASE (fd);

		/* Next send the shutdown message itself, with halt as the
		 * second event.
		 */
		s_msg->type = UPSTART_SHUTDOWN;
		s_msg->name = "halt";
		upstart_send_msg_to (getppid (), sock, s_msg);

		/* Check that we receive an event message for the shutdown
		 * event.
		 */
		r_msg = upstart_recv_msg (NULL, sock, NULL);

		TEST_EQ (r_msg->type, UPSTART_EVENT);
		TEST_EQ_STR (r_msg->name, "shutdown");

		/* Check that we receive a second event message for the
		 * halt event.
		 */
		r_msg = upstart_recv_msg (NULL, sock, NULL);

		TEST_EQ (r_msg->type, UPSTART_EVENT);
		TEST_EQ_STR (r_msg->name, "halt");

		/* Send the unwatch-events message. */
		s_msg->type = UPSTART_UNWATCH_EVENTS;
		upstart_send_msg_to (getppid (), sock, s_msg);
		break;
	}
}

static pid_t
test_watcher_child (int test)
{
	pid_t pid;
	int   fd;

	fflush (stdout);

	TEST_CHILD_WAIT (pid, fd) {
		watcher_child (test, fd);
		exit (0);
	}

	return pid;
}

static void
wait_watcher_child (pid_t pid)
{
	int status;

	waitpid (pid, &status, 0);
	if (! WIFEXITED (status))
		exit (1);
	if (WEXITSTATUS (status) != 0)
		exit (1);
}

void
test_watcher (void)
{
	NihIoWatch *watch;
	Job        *job;
	Event      *event;
	pid_t       pid;

	TEST_FUNCTION ("control_watcher");
	watch = control_open ();
	upstart_disable_safeties = TRUE;

	/* Check that an inappropriate command is ignored. */
	TEST_FEATURE ("with inappropriate command");
	pid = test_watcher_child (TEST_SILLY);
	watch->watcher (watch->data, watch, NIH_IO_READ | NIH_IO_WRITE);
	wait_watcher_child (pid);


	/* Check that a no-op command is ignored. */
	TEST_FEATURE ("with no-op command");
	pid = test_watcher_child (TEST_NO_OP);
	watch->watcher (watch->data, watch, NIH_IO_READ | NIH_IO_WRITE);
	wait_watcher_child (pid);


	/* Check that a job-start message with an unknown job results in
	 * the appropriate response (checked by child).
	 */
	TEST_FEATURE ("with unknown job");
	pid = test_watcher_child (TEST_JOB_UNKNOWN);
	watch->watcher (watch->data, watch, NIH_IO_READ | NIH_IO_WRITE);
	wait_watcher_child (pid);


	/* Check that a job-start message with a known job results in a
	 * status response (checked by child) and the goal changing here
	 * too.
	 */
	TEST_FEATURE ("with start job command");
	job = job_new (NULL, "test");
	job->description = nih_strdup (job, "a test job");
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->process_state = PROCESS_NONE;
	job->command = "echo";

	pid = test_watcher_child (TEST_JOB_START);
	watch->watcher (watch->data, watch, NIH_IO_READ | NIH_IO_WRITE);
	wait_watcher_child (pid);

	TEST_EQ (job->goal, JOB_START);


	/* Check that a job-stop message with a known job results in
	 * a status response (checked by child), the goal being changed
	 * here; and the actual action taken too (killing the process).
	 */
	TEST_FEATURE ("with stop job command");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;
	TEST_CHILD (job->pid) {
		pause ();
	}

	pid = test_watcher_child (TEST_JOB_STOP);
	watch->watcher (watch->data, watch, NIH_IO_READ | NIH_IO_WRITE);
	wait_watcher_child (pid);

	TEST_EQ (job->goal, JOB_STOP);

	waitpid (job->pid, NULL, 0);


	/* Check that a query message with a known job results in a status
	 * response (checked by child).
	 */
	TEST_FEATURE ("with query job command");
	job->goal = JOB_START;
	job->state = JOB_STOPPING;
	job->process_state = PROCESS_ACTIVE;

	pid = test_watcher_child (TEST_JOB_QUERY);
	watch->watcher (watch->data, watch, NIH_IO_READ | NIH_IO_WRITE);
	wait_watcher_child (pid);


	/* Check that a list jobs command results in the list being sent
	 * (checked by child).
	 */
	TEST_FEATURE ("with list jobs command");
	job->goal = JOB_START;
	job->state = JOB_STOPPING;
	job->process_state = PROCESS_ACTIVE;

	pid = test_watcher_child (TEST_JOB_LIST);
	watch->watcher (watch->data, watch, NIH_IO_READ | NIH_IO_WRITE);
	wait_watcher_child (pid);


	/* Check that we can queue an event in the child, which results in
	 * a job here being started.
	 */
	TEST_FEATURE ("with queue event command");
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->process_state = PROCESS_NONE;

	event = event_new (job, "snarf");
	nih_list_add (&job->start_events, &event->entry);

	pid = test_watcher_child (TEST_EVENT);
	watch->watcher (watch->data, watch, NIH_IO_READ | NIH_IO_WRITE);
	wait_watcher_child (pid);

	event_queue_run ();

	TEST_EQ (job->goal, JOB_START);


	/* Check that a child can watch for job changes, and receive status
	 * responses when they do (checked in child).
	 */
	TEST_FEATURE ("with job watch");
	pid = test_watcher_child (TEST_JOB_WATCH);
	watch->watcher (watch->data, watch, NIH_IO_READ | NIH_IO_WRITE);

	job->goal = JOB_START;
	job->state = JOB_STOPPING;
	job->process_state = PROCESS_ACTIVE;
	control_handle_job (job);

	watch->watcher (watch->data, watch, NIH_IO_READ | NIH_IO_WRITE);
	wait_watcher_child (pid);


	/* Check that a child can watch for events, and receive notifications
	 * when they occur (checked in child).
	 */
	TEST_FEATURE ("with event watch");
	pid = test_watcher_child (TEST_EVENT_WATCH);
	watch->watcher (watch->data, watch, NIH_IO_READ | NIH_IO_WRITE);

	control_handle_event (event);

	watch->watcher (watch->data, watch, NIH_IO_READ | NIH_IO_WRITE);
	wait_watcher_child (pid);

	nih_list_free (&job->entry);

	event_queue_run ();


	/* Check that a shutdown event results in two events (checked by
	 * child)
	 */
	TEST_FEATURE ("with shutdown event");
	pid = test_watcher_child (TEST_SHUTDOWN);
	watch->watcher (watch->data, watch, NIH_IO_READ | NIH_IO_WRITE);

	job_detect_idle ();
	event_queue_run ();

	watch->watcher (watch->data, watch, NIH_IO_READ | NIH_IO_WRITE);
	wait_watcher_child (pid);

	event_queue_run ();


	upstart_disable_safeties = FALSE;
	control_close ();
}


void
test_handle_job (void)
{
	NihIoWatch *watch;
	ControlSub *sub;
	Job        *job;
	pid_t       pid;

	/* Check that control_handle_job results in a status message
	 * being sent to any subscriptions (checked in child).
	 */
	TEST_FUNCTION ("control_handle_job");
	watch = control_open ();
	upstart_disable_safeties = TRUE;

	pid = test_watcher_child (TEST_JOB_STATUS);
	sub = control_subscribe (pid, NOTIFY_JOBS, TRUE);

	job = job_new (NULL, "test");
	job->description = nih_strdup (job, "a test job");
	job->goal = JOB_START;
	job->state = JOB_STOPPING;
	job->process_state = PROCESS_ACTIVE;
	control_handle_job (job);

	watch->watcher (watch->data, watch, NIH_IO_READ | NIH_IO_WRITE);
	wait_watcher_child (pid);

	nih_list_free (&job->entry);
	nih_list_free (&sub->entry);

	upstart_disable_safeties = FALSE;
	control_close ();
}

void
test_handle_event (void)
{
	NihIoWatch *watch;
	ControlSub *sub;
	Event      *event;
	pid_t       pid;

	/* Check that control_handle_event results in a notification
	 * message being sent to any subscriptions (checked in child).
	 */
	TEST_FUNCTION ("control_handle_event");
	watch = control_open ();
	upstart_disable_safeties = TRUE;

	pid = test_watcher_child (TEST_EVENT_TRIGGERED);
	sub = control_subscribe (pid, NOTIFY_EVENTS, TRUE);

	event = event_new (NULL, "snarf");
	control_handle_event (event);

	watch->watcher (watch->data, watch, NIH_IO_READ | NIH_IO_WRITE);
	wait_watcher_child (pid);

	nih_free (event);
	nih_list_free (&sub->entry);

	upstart_disable_safeties = FALSE;
	control_close ();
}

int
main (int   argc,
      char *argv[])
{
	test_open ();
	test_close ();
	test_subscribe ();
	test_send ();
	test_watcher ();
	test_handle_job ();
	test_handle_event ();

	return 0;
}
