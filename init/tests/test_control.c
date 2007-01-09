/* upstart
 *
 * test_control.c - test suite for init/control.c
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
#include <nih/logging.h>
#include <nih/error.h>

#include <upstart/message.h>

#include "event.h"
#include "job.h"
#include "control.h"
#include "notify.h"


extern int upstart_disable_safeties;


void
test_open (void)
{
	NihIo              *io, *ptr;
	struct sockaddr_un  addr;
	int                 val;
	char                name[26];
	socklen_t           len;

	TEST_FUNCTION ("control_open");

	/* Check that we can open the control socket, the returned structure
	 * should be an NihIo on a non-blocking, close-on-exec socket that
	 * matches the parameters of the upstart communication socket.
	 */
	TEST_FEATURE ("with no open socket");
	io = control_open ();

	TEST_ALLOC_SIZE (io, sizeof (NihIo));
	TEST_EQ (io->type, NIH_IO_MESSAGE);
	TEST_EQ (io->watch->events, NIH_IO_READ);

	len = sizeof (addr);
	getsockname (io->watch->fd, (struct sockaddr *)&addr, &len);

	TEST_EQ (addr.sun_family, AF_UNIX);
	TEST_EQ (addr.sun_path[0], '\0');

	sprintf (name, "/com/ubuntu/upstart/%d", getpid ());
	TEST_EQ_STRN (addr.sun_path + 1, name);

	val = 0;
	len = sizeof (val);
	getsockopt (io->watch->fd, SOL_SOCKET, SO_TYPE, &val, &len);

	TEST_EQ (val, SOCK_DGRAM);

	val = 0;
	len = sizeof (val);
	getsockopt (io->watch->fd, SOL_SOCKET, SO_PASSCRED, &val, &len);
	TEST_NE (val, 0);

	TEST_TRUE (fcntl (io->watch->fd, F_GETFL) & O_NONBLOCK);
	TEST_TRUE (fcntl (io->watch->fd, F_GETFD) & FD_CLOEXEC);


	/* Check that if we call control_open() again, we get the same
	 * structure as before.
	 */
	TEST_FEATURE ("with already open socket");
	ptr = control_open ();

	TEST_EQ_P (ptr, io);

	control_close ();
}


static int destructor_called = 0;

static int
my_destructor (void *ptr)
{
	destructor_called++;

	return 0;
}

void
test_close (void)
{
	NihIo *io;
	int    fd;

	/* Check that when we close the control socket, the NihIo structure
	 * is freed and the socket itself closed.
	 */
	TEST_FUNCTION ("control_close");
	io = control_open ();
	fd = io->watch->fd;

	destructor_called = 0;
	nih_alloc_set_destructor (io, my_destructor);

	control_close ();

	TEST_TRUE (destructor_called);
	TEST_LT (fcntl (fd, F_GETFD), 0);
	TEST_EQ (errno, EBADF);
}


static int logger_called = 0;

static int
my_logger (NihLogLevel  priority,
	   const char  *message)
{
	logger_called++;

	return 0;
}

void
test_close_handler (void)
{
	NihIo *io;
	int    fd, tmp_fd;

	TEST_FUNCTION ("control_close_handler");

	/* Check that we handle the closing of the socket by opening a new
	 * descriptor and not clearing the queue.  A warning message should
	 * be emitted.
	 */
	TEST_FEATURE ("with no problem reopening");
	tmp_fd = socket (PF_UNIX, SOCK_DGRAM, 0);

	io = control_open ();
	fd = io->watch->fd;

	close (tmp_fd);

	logger_called = 0;
	nih_log_set_logger (my_logger);

	io->close_handler (io->data, io);

	TEST_TRUE (logger_called);

	TEST_NE (io->watch->fd, fd);
	TEST_GE (fcntl (io->watch->fd, F_GETFD), 0);

	TEST_LT (fcntl (fd, F_GETFD), 0);
	TEST_EQ (errno, EBADF);


	/* Check that an error is emitted if it's not possible to open a new
	 * descriptor, and the control structure closed and freed.
	 */
	TEST_FEATURE ("with inability to reopen");
	close (io->watch->fd);
	fd = io->watch->fd = socket (PF_UNIX, SOCK_DGRAM, 0);
	tmp_fd = upstart_open ();

	logger_called = 0;

	destructor_called = 0;
	nih_alloc_set_destructor (io, my_destructor);

	io->close_handler (io->data, io);

	TEST_TRUE (destructor_called);
	TEST_EQ (logger_called, 2);

	TEST_LT (fcntl (fd, F_GETFD), 0);
	TEST_EQ (errno, EBADF);

	close (tmp_fd);

	nih_log_set_logger (nih_logger_printf);
}

void
test_error_handler (void)
{
	NihIo              *io;
	pid_t               pid;
	int                 fd, tmp_fd, wait_fd, status;
	Job                *job;
	NotifySubscription *sub;

	TEST_FUNCTION ("control_error_handler");

	/* Check that we handle an error on the socket by opening a new
	 * descriptor and not clearing the queue.  A warning message should
	 * be emitted.
	 */
	TEST_FEATURE ("with no problem reopening");
	tmp_fd = socket (PF_UNIX, SOCK_DGRAM, 0);

	io = control_open ();
	fd = io->watch->fd;

	close (tmp_fd);

	logger_called = 0;
	nih_log_set_logger (my_logger);

	nih_error_raise (EBADF, strerror (EBADF));
	io->error_handler (io->data, io);

	TEST_TRUE (logger_called);

	TEST_NE (io->watch->fd, fd);
	TEST_GE (fcntl (io->watch->fd, F_GETFD), 0);

	TEST_LT (fcntl (fd, F_GETFD), 0);
	TEST_EQ (errno, EBADF);


	/* Check that an error is emitted if it's not possible to open a new
	 * descriptor, and the control structure closed and freed.
	 */
	TEST_FEATURE ("with inability to reopen");
	close (io->watch->fd);
	fd = io->watch->fd = socket (PF_UNIX, SOCK_DGRAM, 0);
	tmp_fd = upstart_open ();

	logger_called = 0;

	destructor_called = 0;
	nih_alloc_set_destructor (io, my_destructor);

	nih_error_raise (EBADF, strerror (EBADF));
	io->error_handler (io->data, io);

	TEST_TRUE (destructor_called);
	TEST_EQ (logger_called, 2);

	TEST_LT (fcntl (fd, F_GETFD), 0);
	TEST_EQ (errno, EBADF);

	close (tmp_fd);

	nih_log_set_logger (nih_logger_printf);


	/* Check that the error handler can handle receiving ECONNREFUSED
	 * from a subscribed process that has gone away; the message should
	 * be removed from the send queue, and the job's subscription
	 * cancelled.
	 */
	TEST_FEATURE ("with subscribed process going away");
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

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_WATCH_JOBS);
		nih_io_message_send (message, sock);
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	sub = notify_subscribe (pid, NOTIFY_NONE, FALSE);

	TEST_NE_P (sub, NULL);

	TEST_EQ (sub->pid, pid);
	TEST_EQ (sub->notify, NOTIFY_JOBS);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	notify_job (job);

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	sub = notify_subscribe (pid, NOTIFY_NONE, FALSE);

	TEST_EQ_P (sub, NULL);

	TEST_LIST_EMPTY (io->send_q);

	nih_list_free (&job->entry);


	control_close ();
	upstart_disable_safeties = FALSE;
}


static int
check_job_started (void               *data,
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
	TEST_EQ (state, JOB_RUNNING);
	TEST_EQ (process_state, PROCESS_ACTIVE);
	TEST_GT (process, 0);
	TEST_EQ_STR (description, "a test job");

	return 0;
}

static int
check_job_unknown (void               *data,
		   pid_t               pid,
		   UpstartMessageType  type,
		   const char         *name)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_JOB_UNKNOWN);
	TEST_EQ_STR (name, "wibble");

	return 0;
}

void
test_job_start (void)
{
	NihIo *io;
	pid_t  pid;
	int    wait_fd, status;
	Job   *job;

	TEST_FUNCTION ("control_job_start");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	/* Check that we can handle a message from a child process asking us
	 * to start up a job.  The child should get a reply containing the
	 * status of the job, while the job goal should change in the parent
	 * as well as being started.
	 */
	TEST_FEATURE ("with known job");
	job = job_new (NULL, "test");
	job->description = nih_strdup (job, "a test job");
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->process_state = PROCESS_NONE;
	job->command = "echo";

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		/* Send the job start message */
		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_START, "test");
		nih_io_message_send (message, sock);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Wait for a reply */
		message = nih_io_message_recv (NULL, sock, &len);
		upstart_message_handle_using (message, message,
					      (UpstartMessageHandler)check_job_started,
					      NULL);
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);
	TEST_GT (job->pid, 0);

	waitpid (job->pid, NULL, 0);

	nih_list_free (&job->entry);


	/* Check that if we ask to start an unknown job, we get an unknown
	 * job reply.
	 */
	TEST_FEATURE ("with unknown job");
	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_START, "wibble");
		nih_io_message_send (message, sock);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Wait for a reply */
		message = nih_io_message_recv (NULL, sock, &len);
		upstart_message_handle_using (message, message,
					      (UpstartMessageHandler)check_job_unknown,
					      NULL);
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);


	control_close ();
	upstart_disable_safeties = FALSE;
}

static int
check_job_stopped (void               *data,
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
	TEST_EQ (goal, JOB_STOP);
	TEST_EQ (state, JOB_RUNNING);
	TEST_EQ (process_state, PROCESS_KILLED);
	TEST_GT (process, 0);
	TEST_EQ_STR (description, "a test job");

	return 0;
}

void
test_job_stop (void)
{
	NihIo *io;
	pid_t  pid;
	int    wait_fd, status;
	Job   *job;

	TEST_FUNCTION ("control_job_stop");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	/* Check that we can handle a message from a child process asking us
	 * to stop a job.  The child should get a reply containing the
	 * status of the job, while the job goal should change in the parent,
	 * along with the running process being killed.
	 */
	TEST_FEATURE ("with known job");
	job = job_new (NULL, "test");
	job->description = nih_strdup (job, "a test job");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;
	TEST_CHILD (job->pid) {
		pause ();
	}

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_STOP, "test");
		nih_io_message_send (message, sock);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Wait for a reply */
		message = nih_io_message_recv (NULL, sock, &len);
		upstart_message_handle_using (message, message,
					      (UpstartMessageHandler)check_job_stopped,
					      NULL);
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_EQ (job->process_state, PROCESS_KILLED);
	TEST_GT (job->pid, 0);

	waitpid (job->pid, &status, 0);

	TEST_TRUE (WIFSIGNALED (status));
	TEST_EQ (WTERMSIG (status), SIGTERM);

	nih_list_free (&job->entry);


	/* Check that if we ask to start an unknown job, we get an unknown
	 * job reply.
	 */
	TEST_FEATURE ("with unknown job");
	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_STOP, "wibble");
		nih_io_message_send (message, sock);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Wait for a reply */
		message = nih_io_message_recv (NULL, sock, &len);
		upstart_message_handle_using (message, message,
					      (UpstartMessageHandler)check_job_unknown,
					      NULL);
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);


	control_close ();
	upstart_disable_safeties = FALSE;
}

static int
check_job_stopping (void               *data,
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
test_job_query (void)
{
	NihIo *io;
	pid_t  pid;
	int    wait_fd, status;
	Job   *job;

	TEST_FUNCTION ("control_job_query");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	/* Check that we can handle a message from a child process asking us
	 * for the status of a job.  The child should get a reply containing
	 * the status without changing it.
	 */
	TEST_FEATURE ("with known job");
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

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_QUERY, "test");
		nih_io_message_send (message, sock);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Wait for a reply */
		message = nih_io_message_recv (NULL, sock, &len);
		upstart_message_handle_using (message, message,
					      (UpstartMessageHandler)check_job_stopping,
					      NULL);
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_STOPPING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);
	TEST_EQ (job->pid, 1000);

	nih_list_free (&job->entry);


	/* Check that if we ask to start an unknown job, we get an unknown
	 * job reply.
	 */
	TEST_FEATURE ("with unknown job");
	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_QUERY, "wibble");
		nih_io_message_send (message, sock);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Wait for a reply */
		message = nih_io_message_recv (NULL, sock, &len);
		upstart_message_handle_using (message, message,
					      (UpstartMessageHandler)check_job_unknown,
					      NULL);
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);


	control_close ();
	upstart_disable_safeties = FALSE;
}

static int
check_job_starting (void               *data,
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
	TEST_EQ_STR (name, "frodo");
	TEST_EQ (goal, JOB_STOP);
	TEST_EQ (state, JOB_STARTING);
	TEST_EQ (process_state, PROCESS_ACTIVE);
	TEST_EQ (process, 1000);
	TEST_EQ_STR (description, "baggins");

	return 0;
}

static int
check_job_list_end (void               *data,
		    pid_t               pid,
		    UpstartMessageType  type)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_JOB_LIST_END);

	return 0;
}

void
test_job_list (void)
{
	NihIo *io;
	pid_t  pid;
	int    wait_fd, status;
	Job   *job1, *job2;

	/* Check that we can handle a message from a child process asking us
	 * for the list of jobs.  The child should get a reply containing
	 * the status of each job, followed by without changing it.
	 */
	TEST_FUNCTION ("control_job_list");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	job1 = job_new (NULL, "test");
	job1->description = nih_strdup (job1, "a test job");
	job1->goal = JOB_START;
	job1->state = JOB_STOPPING;
	job1->process_state = PROCESS_ACTIVE;
	job1->pid = 1000;

	job2 = job_new (NULL, "frodo");
	job2->description = nih_strdup (job2, "baggins");
	job2->goal = JOB_STOP;
	job2->state = JOB_STARTING;
	job2->process_state = PROCESS_ACTIVE;
	job2->pid = 1000;

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_LIST);
		nih_io_message_send (message, sock);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Wait for a reply for the first job */
		message = nih_io_message_recv (NULL, sock, &len);
		upstart_message_handle_using (message, message,
					      (UpstartMessageHandler)check_job_stopping,
					      NULL);
		nih_free (message);

		/* Wait for a reply for the second job */
		message = nih_io_message_recv (NULL, sock, &len);
		upstart_message_handle_using (message, message,
					      (UpstartMessageHandler)check_job_starting,
					      NULL);
		nih_free (message);

		/* Wait for a reply for the end of the list */
		message = nih_io_message_recv (NULL, sock, &len);
		upstart_message_handle_using (message, message,
					      (UpstartMessageHandler)check_job_list_end,
					      NULL);
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	nih_list_free (&job1->entry);
	nih_list_free (&job2->entry);

	control_close ();
	upstart_disable_safeties = FALSE;
}

void
test_event_queue (void)
{
	NihIo   *io;
	pid_t    pid;
	int      wait_fd, status;
	Event   *event;
	NihList *list;

	/* Check that we can handle a message from a child process requesting
	 * that an event be queued.  The child won't get a reply, but we
	 * should be able to see the event in the queue in the parent.
	 */
	TEST_FUNCTION ("control_event_queue");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	/* This is a naughty way of getting a pointer to the event queue
	 * list head...
	 */
	event_queue_run ();
	event = event_queue ("wibble");
	list = event->entry.prev;
	nih_list_free (&event->entry);

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_EVENT_QUEUE, "snarf");
		nih_io_message_send (message, sock);
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	event = (Event *)list->prev;
	TEST_EQ_STR (event->name, "snarf");
	nih_list_free (&event->entry);

	control_close ();
	upstart_disable_safeties = FALSE;
}

void
test_watch_jobs (void)
{
	NihIo              *io;
	pid_t               pid;
	int                 wait_fd, status;
	Job                *job;
	NotifySubscription *sub;

	/* Check that we can handle a message from a child process asking us
	 * to subscribe them to job status notifications.  We then tickle
	 * a job so that the child gets a status notification.
	 */
	TEST_FUNCTION ("control_watch_jobs");
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

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_WATCH_JOBS);
		nih_io_message_send (message, sock);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Wait for job notification */
		message = nih_io_message_recv (NULL, sock, &len);
		upstart_message_handle_using (message, message,
					      (UpstartMessageHandler)check_job_stopping,
					      NULL);
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	sub = notify_subscribe (pid, NOTIFY_NONE, FALSE);

	TEST_NE_P (sub, NULL);

	TEST_EQ (sub->pid, pid);
	TEST_EQ (sub->notify, NOTIFY_JOBS);

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
test_unwatch_jobs (void)
{
	NihIo              *io;
	pid_t               pid;
	int                 wait_fd, status;
	Job                *job;
	NotifySubscription *sub;

	/* Check that we can handle a message from a child process asking us
	 * to unsubscribe them from job status notifications.
	 */
	TEST_FUNCTION ("control_unwatch_jobs");
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

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_WATCH_JOBS);
		nih_io_message_send (message, sock);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Wait for job notification - this ensures that the parent
		 * knows we're subscribed before we unsubscripe.
		 */
		message = nih_io_message_recv (NULL, sock, &len);
		upstart_message_handle_using (message, message,
					      (UpstartMessageHandler)check_job_stopping,
					      NULL);
		nih_free (message);

		/* Now unsubscribe */
		message = upstart_message_new (NULL, getppid (),
					       UPSTART_UNWATCH_JOBS);
		nih_io_message_send (message, sock);
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	sub = notify_subscribe (pid, NOTIFY_NONE, FALSE);

	TEST_NE_P (sub, NULL);

	TEST_EQ (sub->pid, pid);
	TEST_EQ (sub->notify, NOTIFY_JOBS);

	notify_job (job);

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	sub = notify_subscribe (pid, NOTIFY_NONE, FALSE);

	TEST_EQ_P (sub, NULL);

	nih_list_free (&job->entry);


	control_close ();
	upstart_disable_safeties = FALSE;
}

static int
check_event (void               *data,
	     pid_t               pid,
	     UpstartMessageType  type,
	     const char         *name)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_EVENT);
	TEST_EQ_STR (name, "snarf");

	return 0;
}

void
test_watch_events (void)
{
	NihIo              *io;
	pid_t               pid;
	int                 wait_fd, status;
	Event              *event;
	NotifySubscription *sub;

	/* Check that we can handle a message from a child process asking us
	 * to subscribe them to event notifications.  We then emit an event
	 * so that the child gets a notification.
	 */
	TEST_FUNCTION ("control_watch_events");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_WATCH_EVENTS);
		nih_io_message_send (message, sock);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Wait for event notification */
		message = nih_io_message_recv (NULL, sock, &len);
		upstart_message_handle_using (message, message,
					      (UpstartMessageHandler)check_event,
					      NULL);
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	sub = notify_subscribe (pid, NOTIFY_NONE, FALSE);

	TEST_NE_P (sub, NULL);

	TEST_EQ (sub->pid, pid);
	TEST_EQ (sub->notify, NOTIFY_EVENTS);

	event = event_new (NULL, "snarf");
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

void
test_unwatch_events (void)
{
	NihIo              *io;
	pid_t               pid;
	int                 wait_fd, status;
	Event              *event;
	NotifySubscription *sub;

	/* Check that we can handle a message from a child process asking us
	 * to unsubscribe them from event notifications.
	 */
	TEST_FUNCTION ("control_unwatch_events");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_WATCH_EVENTS);
		nih_io_message_send (message, sock);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Wait for event notification - this ensures that the parent
		 * knows we're subscribed before we unsubscripe.
		 */
		message = nih_io_message_recv (NULL, sock, &len);
		upstart_message_handle_using (message, message,
					      (UpstartMessageHandler)check_event,
					      NULL);
		nih_free (message);

		/* Now unsubscribe */
		message = upstart_message_new (NULL, getppid (),
					       UPSTART_UNWATCH_EVENTS);
		nih_io_message_send (message, sock);
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	sub = notify_subscribe (pid, NOTIFY_NONE, FALSE);

	TEST_NE_P (sub, NULL);

	TEST_EQ (sub->pid, pid);
	TEST_EQ (sub->notify, NOTIFY_EVENTS);

	event = event_new (NULL, "snarf");
	notify_event (event);

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	sub = notify_subscribe (pid, NOTIFY_NONE, FALSE);

	TEST_EQ_P (sub, NULL);

	nih_list_free (&event->entry);


	control_close ();
	upstart_disable_safeties = FALSE;
}

void
test_shutdown (void)
{
	NihIo   *io;
	pid_t    pid;
	int      wait_fd, status;
	Event   *event;
	NihList *list;

	/* Check that we can handle a message from a child process requesting
	 * that the computer be shutdown.  The child won't get a reply, but we
	 * should be able to see the shutdown event in the queue in the parent
	 * and run the idle function to get the second event.
	 */
	TEST_FUNCTION ("control_shutdown");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	/* This is a naughty way of getting a pointer to the event queue
	 * list head...
	 */
	event_queue_run ();
	event = event_queue ("wibble");
	list = event->entry.prev;
	nih_list_free (&event->entry);

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_SHUTDOWN, "kaboom");
		nih_io_message_send (message, sock);
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	event = (Event *)list->prev;
	TEST_EQ_STR (event->name, "shutdown");
	nih_list_free (&event->entry);

	job_detect_idle ();

	event = (Event *)list->prev;
	TEST_EQ_STR (event->name, "kaboom");
	nih_list_free (&event->entry);

	control_close ();
	upstart_disable_safeties = FALSE;
}


#if 0
{
	switch (test) {
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
	}
}

{
	/* Check that a child can watch for job changes, and receive status
	 * responses when they do (checked in child).
	 */
	TEST_FEATURE ("with job watch");
	pid = test_watcher_child (TEST_JOB_WATCH);
	watch->watcher (watch->data, watch, NIH_IO_READ | NIH_IO_WRITE);

	job->goal = JOB_START;
	job->state = JOB_STOPPING;
	job->process_state = PROCESS_ACTIVE;
	notify_job (job);

	watch->watcher (watch->data, watch, NIH_IO_READ | NIH_IO_WRITE);
	wait_watcher_child (pid);


	/* Check that a child can watch for events, and receive notifications
	 * when they occur (checked in child).
	 */
	TEST_FEATURE ("with event watch");
	pid = test_watcher_child (TEST_EVENT_WATCH);
	watch->watcher (watch->data, watch, NIH_IO_READ | NIH_IO_WRITE);

	notify_event (event);

	watch->watcher (watch->data, watch, NIH_IO_READ | NIH_IO_WRITE);
	wait_watcher_child (pid);

	nih_list_free (&job->entry);

	event_queue_run ();
}
#endif


int
main (int   argc,
      char *argv[])
{
	test_open ();
	test_close ();
	test_close_handler ();
	test_error_handler ();
	test_job_start ();
	test_job_stop ();
	test_job_query ();
	test_job_list ();
	test_event_queue ();
	test_watch_jobs ();
	test_unwatch_jobs ();
	test_watch_events ();
	test_unwatch_events ();
	test_shutdown ();

	return 0;
}
