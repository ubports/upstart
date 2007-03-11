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
#include <stdarg.h>
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
	NihIo              *io;
	NihError           *err;
	struct sockaddr_un  addr;
	int                 val, sock;
	char                name[26];
	socklen_t           len;

	TEST_FUNCTION ("control_open");
	io = control_open ();
	control_close ();


	/* Check that we can open the control socket, the returned structure
	 * should be an NihIo on a non-blocking, close-on-exec socket that
	 * matches the parameters of the upstart communication socket.
	 */
	TEST_FEATURE ("with no open socket");
	TEST_ALLOC_FAIL {
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
		getsockopt (io->watch->fd, SOL_SOCKET, SO_TYPE,
			    &val, &len);

		TEST_EQ (val, SOCK_DGRAM);

		val = 0;
		len = sizeof (val);
		getsockopt (io->watch->fd, SOL_SOCKET, SO_PASSCRED,
			    &val, &len);
		TEST_NE (val, 0);

		TEST_TRUE (fcntl (io->watch->fd, F_GETFL) & O_NONBLOCK);
		TEST_TRUE (fcntl (io->watch->fd, F_GETFD) & FD_CLOEXEC);

		control_close ();
	}


	/* Check that if we call control_open() while something else has
	 * already got a socket open, we get EADDRINUSE.
	 */
	TEST_FEATURE ("with already bound socket");
	sock = upstart_open ();
	io = control_open ();

	TEST_EQ_P (io, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, EADDRINUSE);
	nih_free (err);

	close (sock);
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
test_error_handler (void)
{
	NihIo              *io;
	NihIoMessage       *message;
	pid_t               pid;
	int                 wait_fd, status;
	NotifySubscription *sub;

	TEST_FUNCTION ("control_error_handler");

	/* Check that we handle an error on the socket by emitting a warning
	 * message.
	 */
	TEST_FEATURE ("with error on socket");
	io = control_open ();

	logger_called = 0;
	nih_log_set_logger (my_logger);

	nih_error_raise (EBADF, strerror (EBADF));
	io->error_handler (io->data, io);

	TEST_TRUE (logger_called);

	nih_log_set_logger (nih_logger_printf);

	control_close ();


	/* Check that the error handler can handle receiving ECONNREFUSED
	 * from a subscribed process that has gone away; the message should
	 * be removed from the send queue, and the job's subscription
	 * cancelled.
	 */
	TEST_FEATURE ("with subscribed process going away");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		int sock;

		sock = upstart_open ();

		TEST_CHILD_RELEASE (wait_fd);

		exit (0);
	}

	sub = notify_subscribe_job (NULL, pid, NULL);
	TEST_NE_P (sub, NULL);

	destructor_called = 0;
	nih_alloc_set_destructor (sub, my_destructor);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	NIH_MUST (message = upstart_message_new (control_io, pid,
						 UPSTART_NO_OP));
	nih_io_send_message (control_io, message);

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	TEST_TRUE (destructor_called);

	TEST_LIST_EMPTY (io->send_q);


	control_close ();
	upstart_disable_safeties = FALSE;
}


static int
check_version (void               *data,
	       pid_t               pid,
	       UpstartMessageType  type,
	       const char         *version)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_VERSION);

	TEST_EQ_STR (version, nih_main_package_string ());

	return 0;
}


static int
check_job (void               *data,
	   pid_t               pid,
	   UpstartMessageType  type,
	   uint32_t            id,
	   const char         *name)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_JOB);

	TEST_EQ (id, 0xdeafbeef);
	TEST_EQ_STR (name, "test");

	return 0;
}


static int
check_job_instance (void               *data,
		    pid_t               pid,
		    UpstartMessageType  type,
		    uint32_t            id,
		    const char         *name)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_JOB_INSTANCE);

	TEST_EQ (id, 0xdeadbabe);
	TEST_EQ_STR (name, "test");

	return 0;
}

static int
check_job_instance_end (void               *data,
			pid_t               pid,
			UpstartMessageType  type,
			uint32_t            id,
			const char         *name)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_JOB_INSTANCE_END);

	TEST_EQ (id, 0xdeadbabe);
	TEST_EQ_STR (name, "test");

	return 0;
}


static int
check_job_status__waiting (void               *data,
			   pid_t               pid,
			   UpstartMessageType  type,
			   uint32_t            id,
			   const char         *name,
			   JobGoal             goal,
			   JobState            state)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_JOB_STATUS);

	TEST_EQ (id, 0xdeafbeef);
	TEST_EQ_STR (name, "test");
	TEST_EQ (goal, JOB_START);
	TEST_EQ (state, JOB_WAITING);

	return 0;
}

static int
check_job_status_end__waiting (void               *data,
			       pid_t               pid,
			       UpstartMessageType  type,
			       uint32_t            id,
			       const char         *name,
			       JobGoal             goal,
			       JobState            state)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_JOB_STATUS_END);

	TEST_EQ (id, 0xdeafbeef);
	TEST_EQ_STR (name, "test");
	TEST_EQ (goal, JOB_START);
	TEST_EQ (state, JOB_WAITING);

	return 0;
}


static int
check_job_status__starting (void               *data,
			    pid_t               pid,
			    UpstartMessageType  type,
			    uint32_t            id,
			    const char         *name,
			    JobGoal             goal,
			    JobState            state)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_JOB_STATUS);

	TEST_EQ (id, 0xdeafbeef);
	TEST_EQ_STR (name, "test");
	TEST_EQ (goal, JOB_START);
	TEST_EQ (state, JOB_STARTING);

	return 0;
}

static int
check_job_status_end__starting (void               *data,
				pid_t               pid,
				UpstartMessageType  type,
				uint32_t            id,
				const char         *name,
				JobGoal             goal,
				JobState            state)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_JOB_STATUS_END);

	TEST_EQ (id, 0xdeafbeef);
	TEST_EQ_STR (name, "test");
	TEST_EQ (goal, JOB_START);
	TEST_EQ (state, JOB_STARTING);

	return 0;
}


static int
check_job_status__running (void               *data,
			   pid_t               pid,
			   UpstartMessageType  type,
			   uint32_t            id,
			   const char         *name,
			   JobGoal             goal,
			   JobState            state)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_JOB_STATUS);

	TEST_EQ (id, 0xdeafbeef);
	TEST_EQ_STR (name, "test");
	TEST_EQ (goal, JOB_STOP);
	TEST_EQ (state, JOB_RUNNING);

	return 0;
}

static int
check_job_status_end__running (void               *data,
			       pid_t               pid,
			       UpstartMessageType  type,
			       uint32_t            id,
			       const char         *name,
			       JobGoal             goal,
			       JobState            state)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_JOB_STATUS_END);

	TEST_EQ (id, 0xdeafbeef);
	TEST_EQ_STR (name, "test");
	TEST_EQ (goal, JOB_STOP);
	TEST_EQ (state, JOB_RUNNING);

	return 0;
}


static int
check_job_status__pre_stop (void               *data,
			    pid_t               pid,
			    UpstartMessageType  type,
			    uint32_t            id,
			    const char         *name,
			    JobGoal             goal,
			    JobState            state)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_JOB_STATUS);

	TEST_EQ (id, 0xdeafbeef);
	TEST_EQ_STR (name, "test");
	TEST_EQ (goal, JOB_STOP);
	TEST_EQ (state, JOB_PRE_STOP);

	return 0;
}

static int
check_job_status_end__pre_stop (void               *data,
				pid_t               pid,
				UpstartMessageType  type,
				uint32_t            id,
				const char         *name,
				JobGoal             goal,
				JobState            state)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_JOB_STATUS_END);

	TEST_EQ (id, 0xdeafbeef);
	TEST_EQ_STR (name, "test");
	TEST_EQ (goal, JOB_STOP);
	TEST_EQ (state, JOB_PRE_STOP);

	return 0;
}


static int
check_job_status__stopping (void               *data,
			    pid_t               pid,
			    UpstartMessageType  type,
			    uint32_t            id,
			    const char         *name,
			    JobGoal             goal,
			    JobState            state)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_JOB_STATUS);

	TEST_EQ (id, 0xdeafbeef);
	TEST_EQ_STR (name, "test");
	TEST_EQ (goal, JOB_STOP);
	TEST_EQ (state, JOB_STOPPING);

	return 0;
}

static int
check_job_status_end__stopping (void               *data,
				pid_t               pid,
				UpstartMessageType  type,
				uint32_t            id,
				const char         *name,
				JobGoal             goal,
				JobState            state)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_JOB_STATUS_END);

	TEST_EQ (id, 0xdeafbeef);
	TEST_EQ_STR (name, "test");
	TEST_EQ (goal, JOB_STOP);
	TEST_EQ (state, JOB_STOPPING);

	return 0;
}


static int
check_job_status__deleted (void               *data,
			   pid_t               pid,
			   UpstartMessageType  type,
			   uint32_t            id,
			   const char         *name,
			   JobGoal             goal,
			   JobState            state)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_JOB_STATUS);

	TEST_EQ (id, 0xdeafbeef);
	TEST_EQ_STR (name, "test");
	TEST_EQ (goal, JOB_STOP);
	TEST_EQ (state, JOB_DELETED);

	return 0;
}

static int
check_job_status_end__deleted (void               *data,
			       pid_t               pid,
			       UpstartMessageType  type,
			       uint32_t            id,
			       const char         *name,
			       JobGoal             goal,
			       JobState            state)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_JOB_STATUS_END);

	TEST_EQ (id, 0xdeafbeef);
	TEST_EQ_STR (name, "test");
	TEST_EQ (goal, JOB_STOP);
	TEST_EQ (state, JOB_DELETED);

	return 0;
}


static int
check_job_process (void               *data,
		   pid_t               pid,
		   UpstartMessageType  type,
		   ProcessType         process,
		   pid_t               process_pid)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_JOB_PROCESS);

	TEST_EQ (process, PROCESS_MAIN);
	TEST_EQ (process_pid, 1000);

	return 0;
}


static int
check_job_unknown (void               *data,
		   pid_t               pid,
		   UpstartMessageType  type,
		   const char         *name,
		   uint32_t            id)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_JOB_UNKNOWN);

	TEST_EQ_STR (name, "test");
	TEST_EQ (id, 0);

	return 0;
}

static int
check_job_invalid (void               *data,
		   pid_t               pid,
		   UpstartMessageType  type,
		   uint32_t            id,
		   const char         *name)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_JOB_INVALID);

	TEST_EQ (id, 0xdeafbeef);
	TEST_EQ_STR (name, "test");

	return 0;
}

static int
check_job_unchanged (void               *data,
		     pid_t               pid,
		     UpstartMessageType  type,
		     uint32_t            id,
		     const char         *name)
{
	TEST_EQ (pid, getppid ());
	TEST_EQ (type, UPSTART_JOB_UNCHANGED);

	TEST_EQ (id, 0xdeafbeef);
	TEST_EQ_STR (name, "test");

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


static int                check_list_all = FALSE;
static int                check_list_responses = 0;
static UpstartMessageType last_type = -1;
static uint32_t           last_job_id = 0, last_instance_id = 0;

static int
check_list (void               *data,
	    pid_t               pid,
	    UpstartMessageType  type,
	    ...)
{
	va_list args;
	int     ret = 0;

	TEST_EQ (pid, getppid ());

	va_start (args, type);

	switch (type) {
	case UPSTART_JOB_INSTANCE: {
		uint32_t  id;
		char     *name;

		if ((last_type != UPSTART_JOB_LIST)
		    && (last_type != UPSTART_JOB_STATUS_END)
		    && (last_type != UPSTART_JOB_INSTANCE_END))
			TEST_FAILED ("incorrect message order (%04x before %04x)",
				     last_type, type);

		id = va_arg (args, unsigned);
		name = va_arg (args, char *);

		if (id == 0xbeeb1003) {
			TEST_EQ_STR (name, "baz");
		} else {
			TEST_FAILED ("unexpected job #%08x", id);
		}

		check_list_responses++;
		last_instance_id = id;
		break;
	}
	case UPSTART_JOB_INSTANCE_END: {
		uint32_t  id;
		char     *name;

		if ((last_type != UPSTART_JOB_INSTANCE)
		    && (last_type != UPSTART_JOB_STATUS_END))
			TEST_FAILED ("incorrect message order (%04x before %04x)",
				     last_type, type);

		id = va_arg (args, unsigned);
		name = va_arg (args, char *);

		TEST_EQ (id, last_instance_id);

		if (id == 0xbeeb1003) {
			TEST_EQ_STR (name, "baz");
		} else {
			TEST_FAILED ("unexpected job #%08x", id);
		}

		last_instance_id = -1;
		break;
	}
	case UPSTART_JOB_STATUS: {
		uint32_t  id;
		char     *name;

		if ((last_type != UPSTART_JOB_LIST)
		    && (last_type != UPSTART_JOB_STATUS_END)
		    && (last_type != UPSTART_JOB_INSTANCE))
			TEST_FAILED ("incorrect message order (%04x before %04x)",
				     last_type, type);

		id = va_arg (args, unsigned);
		name = va_arg (args, char *);

		if (id == 0xbeeb1001) {
			TEST_TRUE (check_list_all);
			TEST_EQ_STR (name, "foo");
			check_list_responses++;
		} else if (id == 0xbeeb1002) {
			TEST_EQ_STR (name, "bar");
			check_list_responses++;
		} else if (id == 0xbeeb1004) {
			TEST_EQ (last_type, UPSTART_JOB_INSTANCE);
			TEST_EQ (last_instance_id, 0xbeeb1003);
			TEST_EQ_STR (name, "baz");
		} else {
			TEST_FAILED ("unexpected job #%08x", id);
		}

		last_job_id = id;
		break;
	}
	case UPSTART_JOB_STATUS_END: {
		uint32_t  id;
		char     *name;

		if ((last_type != UPSTART_JOB_STATUS)
		    && (last_type != UPSTART_JOB_PROCESS))
			TEST_FAILED ("incorrect message order (%04x before %04x)",
				     last_type, type);

		id = va_arg (args, unsigned);
		name = va_arg (args, char *);

		TEST_EQ (id, last_job_id);

		if (id == 0xbeeb1001) {
			TEST_TRUE (check_list_all);
			TEST_EQ_STR (name, "foo");
		} else if (id == 0xbeeb1002) {
			TEST_EQ_STR (name, "bar");
		} else if (id == 0xbeeb1004) {
			TEST_EQ (last_instance_id, 0xbeeb1003);
			TEST_EQ_STR (name, "baz");
		} else {
			TEST_FAILED ("unexpected job #%08x", id);
		}

		last_job_id = -1;
		break;
	}
	case UPSTART_JOB_PROCESS: {
		ProcessType process;
		pid_t       process_pid;

		if ((last_type != UPSTART_JOB_STATUS)
		    && (last_type != UPSTART_JOB_PROCESS))
			TEST_FAILED ("incorrect message order (%04x before %04x)",
				     last_type, type);

		process = va_arg (args, unsigned);
		process_pid = va_arg (args, int);

		if (last_job_id == 0xbeeb1001) {
			TEST_EQ (process, PROCESS_MAIN);
			TEST_EQ (process_pid, 1000);
		} else if (last_job_id == 0xbeeb1002) {
			if (process == PROCESS_MAIN) {
				TEST_EQ (process_pid, 1000);
			} else if (process == PROCESS_PRE_STOP) {
				TEST_EQ (process_pid, 1001);
			} else {
				TEST_FAILED ("unexpected process %d",
					     process);
			}
		} else {
			TEST_FAILED ("process wasn't expected for job #%08x",
				     last_job_id);
		}

		break;
	}
	case UPSTART_JOB_LIST: {
		char *pattern;

		if (last_type != -1)
			TEST_FAILED ("incorrect message order (%04x before %04x)",
				     last_type, type);

		pattern = va_arg (args, char *);
		if (check_list_all) {
			TEST_EQ_P (pattern, NULL);
		} else {
			TEST_EQ_STR (pattern, "b*");
		}

		break;
	}
	case UPSTART_JOB_LIST_END: {
		char *pattern;

		if ((last_type != UPSTART_JOB_LIST)
		    && (last_type != UPSTART_JOB_INSTANCE_END)
		    && (last_type != UPSTART_JOB_STATUS_END))
			TEST_FAILED ("incorrect message order (%04x before %04x)",
				     last_type, type);

		pattern = va_arg (args, char *);
		if (check_list_all) {
			TEST_EQ_P (pattern, NULL);
		} else {
			TEST_EQ_STR (pattern, "b*");
		}

		ret = 1;
		break;
	}
	default:
		TEST_FAILED ("unexpected message type %04x received", type);
		break;
	}

	va_end (args);

	last_type = type;

	return ret;
}



void
test_send_job_status (void)
{
	NihIo *io;
	pid_t  pid;
	int    wait_fd, status;
	Job   *job;

	/* Check that we can send the status of a job to a child, it should
	 * receive the start message, a message for the running process and
	 * an end message.
	 */
	TEST_FUNCTION ("control_send_job_status");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	job = job_new (NULL, "test");
	job->id = 0xdeafbeef;
	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;
	job->process[PROCESS_PRE_START] = job_process_new (job->process);
	job->process[PROCESS_MAIN] = job_process_new (job->process);
	job->process[PROCESS_MAIN]->pid = 1000;
	job->process[PROCESS_POST_STOP] = job_process_new (job->process);

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_JOB_STATUS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status__stopping,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_PROCESS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_process,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS_END */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status_end__stopping,
						       NULL));
		nih_free (message);

		exit (0);
	}

	control_send_job_status (pid, job);

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	nih_list_free (&job->entry);


	control_close ();
	upstart_disable_safeties = FALSE;
}

void
test_send_instance (void)
{
	NihIo *io;
	pid_t  pid;
	int    wait_fd, status;
	Job   *job, *instance;

	/* Check that we can send the status of an instance job to a child,
	 * with the status of each instance inside it.
	 */
	TEST_FUNCTION ("control_send_instance");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	job = job_new (NULL, "test");
	job->id = 0xdeadbabe;
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->instance = TRUE;

	instance = job_new (NULL, "test");
	instance->id = 0xdeafbeef;
	instance->goal = JOB_STOP;
	instance->state = JOB_STOPPING;
	instance->process[PROCESS_PRE_START] = job_process_new (instance);
	instance->process[PROCESS_MAIN] = job_process_new (instance);
	instance->process[PROCESS_MAIN]->pid = 1000;
	instance->process[PROCESS_POST_STOP] = job_process_new (instance);
	instance->instance = TRUE;
	instance->instance_of = job;

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_JOB_INSTANCE */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_instance,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status__stopping,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_PROCESS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_process,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS_END */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status_end__stopping,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_INSTANCE_END */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_instance_end,
						       NULL));
		nih_free (message);

		exit (0);
	}

	control_send_instance (pid, job);

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	nih_list_free (&instance->entry);
	nih_list_free (&job->entry);


	control_close ();
	upstart_disable_safeties = FALSE;
}


void
test_version_query (void)
{
	NihIo *io;
	pid_t  pid;
	int    wait_fd, status;

	/* Check that we can handle a message from a child process asking us
	 * for our version.
	 */
	TEST_FUNCTION ("control_version_query");
	nih_main_init_full ("test", "upstart", "0.5.0", NULL, NULL);

	io = control_open ();
	upstart_disable_safeties = TRUE;

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_VERSION_QUERY);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_VERSION */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_version,
						       NULL));
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);


	control_close ();
	upstart_disable_safeties = FALSE;
}

void
test_log_priority (void)
{
	NihIo *io;
	pid_t  pid;
	int    status;

	/* Check that we can handle a message from a child process changing
	 * our logging priority.
	 */
	TEST_FUNCTION ("control_log_priority");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	nih_log_set_priority (NIH_LOG_MESSAGE);

	fflush (stdout);
	TEST_CHILD (pid) {
		NihIoMessage *message;
		int           sock;

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_LOG_PRIORITY,
					       NIH_LOG_DEBUG);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	TEST_EQ (nih_log_priority, NIH_LOG_DEBUG);


	nih_log_set_priority (NIH_LOG_MESSAGE);

	control_close ();
	upstart_disable_safeties = FALSE;
}


void
test_job_find (void)
{
	NihIo *io;
	pid_t  pid;
	int    wait_fd, status;
	Job   *job1, *job2, *job3, *job4, *job5, *job6;

	TEST_FUNCTION ("control_job_find");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	job1 = job_new (NULL, "foo");
	job1->id = 0xbeeb1001;
	job1->goal = JOB_START;
	job1->state = JOB_RUNNING;
	job1->process[PROCESS_MAIN] = job_process_new (job1->process);
	job1->process[PROCESS_MAIN]->command = "echo";
	job1->process[PROCESS_MAIN]->pid = 1000;

	job2 = job_new (NULL, "bar");
	job2->id = 0xbeeb1002;
	job2->goal = JOB_STOP;
	job2->state = JOB_PRE_STOP;
	job2->process[PROCESS_MAIN] = job_process_new (job2->process);
	job2->process[PROCESS_MAIN]->command = "echo";
	job2->process[PROCESS_MAIN]->pid = 1000;
	job2->process[PROCESS_PRE_STOP] = job_process_new (job2->process);
	job2->process[PROCESS_PRE_STOP]->command = "echo";
	job2->process[PROCESS_PRE_STOP]->pid = 1001;

	job3 = job_new (NULL, "baz");
	job3->id = 0xbeeb1003;
	job3->instance = TRUE;
	job3->goal = JOB_STOP;
	job3->state = JOB_WAITING;

	job4 = job_new (NULL, "baz");
	job4->id = 0xbeeb1004;
	job4->instance = TRUE;
	job4->instance_of = job3;
	job4->goal = JOB_START;
	job4->state = JOB_STARTING;

	job5 = job_new (NULL, "test");
	job5->id = 0xbeeb1005;
	job5->goal = JOB_STOP;
	job5->state = JOB_DELETED;

	job6 = job_new (NULL, "foo");
	job6->id = 0xbeeb1006;
	job6->goal = JOB_STOP;
	job6->state = JOB_WAITING;
	job6->replacement_for = job1;
	job1->replacement = job6;


	/* Check that we can obtain a list of all jobs by passing a NULL
	 * pattern, which should return the list excluding both the deleted
	 * job and that which is a replacement for another.
	 */
	TEST_FEATURE ("with no pattern");
	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		/* Send the job query message */
		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_FIND, NULL);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		check_list_all = TRUE;
		check_list_responses = 0;
		last_type = -1;

		/* Handle messages until we receive UPSTART_JOB_LIST_END */
		for (;;) {
			int ret;

			message = nih_io_message_recv (NULL, sock, &len);
			ret = upstart_message_handle_using (message, message,
							    (UpstartMessageHandler)
							    check_list,
							    NULL);
			nih_free (message);

			assert (ret >= 0);
			if (ret > 0)
				break;
		}

		TEST_EQ (check_list_responses, 3);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	event_poll ();


	/* Check that we can provide a pattern, and have only jobs matching
	 * that returned.
	 */
	TEST_FEATURE ("with pattern");
	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		/* Send the job query message */
		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_FIND, "b*");
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		check_list_all = FALSE;
		check_list_responses = 0;
		last_type = -1;

		/* Handle messages until we receive UPSTART_JOB_LIST_END */
		for (;;) {
			int ret;

			message = nih_io_message_recv (NULL, sock, &len);
			ret = upstart_message_handle_using (message, message,
							    (UpstartMessageHandler)
							    check_list,
							    NULL);
			nih_free (message);

			assert (ret >= 0);
			if (ret > 0)
				break;
		}

		TEST_EQ (check_list_responses, 2);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	event_poll ();


	nih_list_free (&job6->entry);
	nih_list_free (&job5->entry);
	nih_list_free (&job4->entry);
	nih_list_free (&job3->entry);
	nih_list_free (&job2->entry);
	nih_list_free (&job1->entry);


	control_close ();
	upstart_disable_safeties = FALSE;
}

void
test_job_query (void)
{
	NihIo *io;
	pid_t  pid;
	int    wait_fd, status;
	Job   *job, *instance;

	TEST_FUNCTION ("control_job_query");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	/* Check that we can handle a message from a child process asking us
	 * for the state of an ordinary job.  The child should receive the
	 * status in reply.
	 */
	TEST_FEATURE ("with known job");
	job = job_new (NULL, "test");
	job->id = 0xdeafbeef;
	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;
	job->process[PROCESS_MAIN] = job_process_new (job->process);
	job->process[PROCESS_MAIN]->command = "echo";
	job->process[PROCESS_MAIN]->pid = 1000;

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		/* Send the job query message */
		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_QUERY, "test", 0);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_JOB_STATUS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status__stopping,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_PROCESS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_process,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS_END */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status_end__stopping,
						       NULL));
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	nih_list_free (&job->entry);
	event_poll ();


	/* Check that a job can be queries by its id, instead of its name. */
	TEST_FEATURE ("with known job by id");
	job = job_new (NULL, "test");
	job->id = 0xdeafbeef;
	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;
	job->process[PROCESS_MAIN] = job_process_new (job->process);
	job->process[PROCESS_MAIN]->command = "echo";
	job->process[PROCESS_MAIN]->pid = 1000;

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		/* Send the job query message */
		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_QUERY, NULL,
					       0xdeafbeef);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_JOB_STATUS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status__stopping,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_PROCESS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_process,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS_END */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status_end__stopping,
						       NULL));
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	nih_list_free (&job->entry);
	event_poll ();


	/* Check that querying the state of an instance master returns
	 * a list of all of its instances.
	 */
	TEST_FEATURE ("with instance job");
	job = job_new (NULL, "test");
	job->id = 0xdeadbabe;
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->instance = TRUE;

	instance = job_new (NULL, "test");
	instance->id = 0xdeafbeef;
	instance->goal = JOB_STOP;
	instance->state = JOB_STOPPING;
	instance->process[PROCESS_MAIN] = job_process_new (job->process);
	instance->process[PROCESS_MAIN]->command = "echo";
	instance->process[PROCESS_MAIN]->pid = 1000;
	instance->instance = TRUE;
	instance->instance_of = job;

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		/* Send the job query message */
		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_QUERY, "test", 0);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_JOB_INSTANCE */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_instance,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status__stopping,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_PROCESS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_process,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS_END */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status_end__stopping,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_INSTANCE_END */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_instance_end,
						       NULL));
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	nih_list_free (&instance->entry);
	nih_list_free (&job->entry);
	event_poll ();


	/* Check that if we ask for the state of an instance of a job (by its
	 * id) that we just get the status of the instance, not a list of
	 * them.
	 */
	TEST_FEATURE ("with instance of a job");
	job = job_new (NULL, "test");
	job->id = 0xdeadbabe;
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->instance = TRUE;

	instance = job_new (NULL, "test");
	instance->id = 0xdeafbeef;
	instance->goal = JOB_STOP;
	instance->state = JOB_STOPPING;
	instance->process[PROCESS_MAIN] = job_process_new (job->process);
	instance->process[PROCESS_MAIN]->command = "echo";
	instance->process[PROCESS_MAIN]->pid = 1000;
	instance->instance = TRUE;
	instance->instance_of = job;

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		/* Send the job query message */
		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_QUERY,
					       NULL, 0xdeafbeef);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_JOB_STATUS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status__stopping,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_PROCESS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_process,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS_END */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status_end__stopping,
						       NULL));
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	nih_list_free (&instance->entry);
	nih_list_free (&job->entry);
	event_poll ();


	/* Check that if we ask for the status of an unknown job, we get an
	 * error in reply.
	 */
	TEST_FEATURE ("with unknown job");
	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_QUERY, "test", 0);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_JOB_UNKNOWN */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_unknown,
						       NULL));
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	event_poll ();


	/* Check that if we ask for the state of a deleted job we actually
	 * do get the job in reply, with the deleted state.
	 */
	TEST_FEATURE ("with deleted job");
	job = job_new (NULL, "test");
	job->id = 0xdeafbeef;
	job->goal = JOB_STOP;
	job->state = JOB_DELETED;
	job->process[PROCESS_MAIN] = job_process_new (job->process);
	job->process[PROCESS_MAIN]->command = "echo";

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_QUERY, NULL,
					       0xdeafbeef);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_JOB_STATUS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status__deleted,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS_END */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status_end__deleted,
						       NULL));
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_DELETED);

	nih_list_free (&job->entry);
	event_poll ();


	control_close ();
	upstart_disable_safeties = FALSE;
}

void
test_job_start (void)
{
	NihIo              *io;
	pid_t               pid;
	int                 wait_fd, status;
	Job                *job, *instance;
	NotifySubscription *sub;

	TEST_FUNCTION ("control_job_start");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	/* Check that we can handle a message from a child process asking us
	 * to start up a job.  The child should be subscribed to the job,
	 * and therefore receive replies containing status updates as the
	 * job heads towards being running.
	 */
	TEST_FEATURE ("with known job");
	job = job_new (NULL, "test");
	job->id = 0xdeafbeef;
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->process[PROCESS_MAIN] = job_process_new (job->process);
	job->process[PROCESS_MAIN]->command = "echo";

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		/* Send the job start message */
		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_START, "test", 0);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it and see
		 * the subscription.
		 */
		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_JOB */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status__waiting,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS_END */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status_end__waiting,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status__starting,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS_END */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status_end__starting,
						       NULL));
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	sub = notify_subscription_find (pid, NOTIFY_JOB, job);
	TEST_NE_P (sub, NULL);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_STARTING);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	nih_list_free (&sub->entry);
	nih_list_free (&job->entry);
	event_poll ();


	/* Check that a job can be started by its id, instead of its name. */
	TEST_FEATURE ("with known job by id");
	job = job_new (NULL, "test");
	job->id = 0xdeafbeef;
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->process[PROCESS_MAIN] = job_process_new (job->process);
	job->process[PROCESS_MAIN]->command = "echo";

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		/* Send the job start message */
		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_START, NULL,
					       0xdeafbeef);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it and see
		 * the subscription.
		 */
		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_JOB */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status__waiting,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS_END */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status_end__waiting,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status__starting,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS_END */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status_end__starting,
						       NULL));
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	sub = notify_subscription_find (pid, NOTIFY_JOB, job);
	TEST_NE_P (sub, NULL);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_STARTING);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	nih_list_free (&sub->entry);
	nih_list_free (&job->entry);
	event_poll ();


	/* Check that we can handle the starting of a new instance; the job
	 * the child should receive status message for, and to which it should
	 * be subscribed, should be the instance rather than the master which
	 * should be untouched.
	 */
	TEST_FEATURE ("with instance job");
	job_id = 0xdeafbeee;

	job = job_new (NULL, "test");
	job->instance = TRUE;
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->process[PROCESS_MAIN] = job_process_new (job->process);
	job->process[PROCESS_MAIN]->command = "echo";

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		/* Send the job start message */
		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_START, "test", 0);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it and see
		 * the subscription.
		 */
		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_JOB */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status__waiting,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS_END */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status_end__waiting,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status__starting,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS_END */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status_end__starting,
						       NULL));
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	instance = job_find_by_id (0xdeafbeef);

	TEST_NE_P (instance, NULL);
	TEST_EQ (instance->instance, TRUE);
	TEST_EQ_P (instance->instance_of, job);

	sub = notify_subscription_find (pid, NOTIFY_JOB, instance);
	TEST_NE_P (sub, NULL);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_WAITING);

	TEST_EQ (instance->goal, JOB_START);
	TEST_EQ (instance->state, JOB_STARTING);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	nih_list_free (&sub->entry);
	nih_list_free (&instance->entry);
	nih_list_free (&job->entry);
	event_poll ();


	/* Check that if we ask to start an unknown job, we get an error
	 * in reply.
	 */
	TEST_FEATURE ("with unknown job");
	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_START, "test", 0);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_JOB_UNKNOWN */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_unknown,
						       NULL));
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	event_poll ();


	/* Check that if we ask to start a deleted job, which we have to do
	 * by its id since it won't be found otherwise, we get an error
	 * in reply.
	 */
	TEST_FEATURE ("with deleted job");
	job = job_new (NULL, "test");
	job->id = 0xdeafbeef;
	job->goal = JOB_STOP;
	job->state = JOB_DELETED;
	job->process[PROCESS_MAIN] = job_process_new (job->process);
	job->process[PROCESS_MAIN]->command = "echo";

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_START, NULL,
					       0xdeafbeef);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_JOB_INVALID */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_invalid,
						       NULL));
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_DELETED);

	nih_list_free (&job->entry);
	event_poll ();


	/* Check that if we ask to start a instance job, which we have to do
	 * by its id since it won't be found otherwise, we get an error
	 * in reply.
	 */
	TEST_FEATURE ("with instance job");
	job = job_new (NULL, "test");
	job->id = 0xdeafbeef;
	job->instance_of = (void *)-1;
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->process[PROCESS_MAIN] = job_process_new (job->process);
	job->process[PROCESS_MAIN]->command = "echo";

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_START, NULL,
					       0xdeafbeef);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_JOB_INVALID */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_invalid,
						       NULL));
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_WAITING);

	nih_list_free (&job->entry);
	event_poll ();


	/* Check that if we ask to start a replacement job, which we have to
	 * do by its id since it won't be found otherwise, we get an error
	 * in reply.
	 */
	TEST_FEATURE ("with replacement job");
	job = job_new (NULL, "test");
	job->id = 0xdeafbeef;
	job->replacement_for = (void *)-1;
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->process[PROCESS_MAIN] = job_process_new (job->process);
	job->process[PROCESS_MAIN]->command = "echo";

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_START, NULL,
					       0xdeafbeef);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_JOB_INVALID */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_invalid,
						       NULL));
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_WAITING);

	nih_list_free (&job->entry);
	event_poll ();


	/* Check that if we ask to start a job that is already running,
	 * we get an unchanged message in reply.
	 */
	TEST_FEATURE ("with already started job");
	job = job_new (NULL, "test");
	job->id = 0xdeafbeef;
	job->goal = JOB_START;
	job->state = JOB_STARTING;
	job->process[PROCESS_MAIN] = job_process_new (job->process);
	job->process[PROCESS_MAIN]->command = "echo";

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_START, "test", 0);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_JOB_UNCHANGED */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_unchanged,
						       NULL));
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_STARTING);

	nih_list_free (&job->entry);
	event_poll ();


	control_close ();
	upstart_disable_safeties = FALSE;
}

void
test_job_stop (void)
{
	NihIo              *io;
	pid_t               pid;
	int                 wait_fd, status;
	Job                *job, *instance;
	NotifySubscription *sub;

	TEST_FUNCTION ("control_job_stop");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	/* Check that we can handle a message from a child process asking us
	 * to stop a running job.  The child should be subscribed to the job,
	 * and therefore receive replies containing status updates as the
	 * job heads towards being waiting.
	 */
	TEST_FEATURE ("with known job");
	job = job_new (NULL, "test");
	job->id = 0xdeafbeef;
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process[PROCESS_MAIN] = job_process_new (job->process);
	job->process[PROCESS_MAIN]->command = "echo";
	job->process[PROCESS_MAIN]->pid = 1000;

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		/* Send the job stop message */
		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_STOP, "test", 0);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it and see
		 * the subscription.
		 */
		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_JOB */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job,
						       NULL));
		nih_free (message);
		/* Should receive UPSTART_JOB_STATUS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status__running,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_PROCESS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_process,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS_END */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status_end__running,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status__pre_stop,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_PROCESS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_process,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS_END */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status_end__pre_stop,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status__stopping,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_PROCESS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_process,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS_END */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status_end__stopping,
						       NULL));
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	sub = notify_subscription_find (pid, NOTIFY_JOB, job);
	TEST_NE_P (sub, NULL);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_STOPPING);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	nih_list_free (&sub->entry);
	nih_list_free (&job->entry);
	event_poll ();


	/* Check that a job can be stopped by its id, instead of its name. */
	TEST_FEATURE ("with known job by id");
	job = job_new (NULL, "test");
	job->id = 0xdeafbeef;
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process[PROCESS_MAIN] = job_process_new (job->process);
	job->process[PROCESS_MAIN]->command = "echo";
	job->process[PROCESS_MAIN]->pid = 1000;

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		/* Send the job stop message */
		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_STOP, NULL,
					       0xdeafbeef);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it and see
		 * the subscription.
		 */
		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_JOB */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status__running,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_PROCESS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_process,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS_END */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status_end__running,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status__pre_stop,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_PROCESS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_process,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS_END */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status_end__pre_stop,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status__stopping,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_PROCESS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_process,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS_END */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status_end__stopping,
						       NULL));
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	sub = notify_subscription_find (pid, NOTIFY_JOB, job);
	TEST_NE_P (sub, NULL);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_STOPPING);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	nih_list_free (&sub->entry);
	nih_list_free (&job->entry);
	event_poll ();


	/* Check that attempting to stop an instance master actually stops
	 * all of its instances, returning UPSTART_JOB for each one.
	 */
	TEST_FEATURE ("with instance job");
	job = job_new (NULL, "test");
	job->instance = TRUE;
	job->id = 0xdeafbeee;
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;

	instance = job_new (NULL, "test");
	instance->instance = TRUE;
	instance->id = 0xdeafbeef;
	instance->instance_of = job;
	instance->goal = JOB_START;
	instance->state = JOB_RUNNING;
	instance->process[PROCESS_MAIN] = job_process_new (job->process);
	instance->process[PROCESS_MAIN]->command = "echo";
	instance->process[PROCESS_MAIN]->pid = 1000;

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		/* Send the job stop message */
		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_STOP, "test", 0);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it and see
		 * the subscription.
		 */
		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_JOB */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job,
						       NULL));
		nih_free (message);
		/* Should receive UPSTART_JOB_STATUS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status__running,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_PROCESS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_process,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS_END */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status_end__running,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status__pre_stop,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_PROCESS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_process,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS_END */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status_end__pre_stop,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status__stopping,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_PROCESS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_process,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS_END */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status_end__stopping,
						       NULL));
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	sub = notify_subscription_find (pid, NOTIFY_JOB, job);
	TEST_EQ_P (sub, NULL);

	sub = notify_subscription_find (pid, NOTIFY_JOB, instance);
	TEST_NE_P (sub, NULL);

	TEST_EQ (instance->goal, JOB_STOP);
	TEST_EQ (instance->state, JOB_STOPPING);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	nih_list_free (&sub->entry);
	nih_list_free (&instance->entry);
	nih_list_free (&job->entry);
	event_poll ();


	/* Check that if we ask to stop an unknown job, we get an error
	 * in reply.
	 */
	TEST_FEATURE ("with unknown job");
	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_STOP, "test", 0);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_JOB_UNKNOWN */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_unknown,
						       NULL));
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	event_poll ();


	/* Check that if we ask to stop a deleted job, which we have to do
	 * by its id since it won't be found otherwise, we get an error
	 * in reply.
	 */
	TEST_FEATURE ("with deleted job");
	job = job_new (NULL, "test");
	job->id = 0xdeafbeef;
	job->goal = JOB_STOP;
	job->state = JOB_DELETED;
	job->process[PROCESS_MAIN] = job_process_new (job->process);
	job->process[PROCESS_MAIN]->command = "echo";

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_STOP, NULL,
					       0xdeafbeef);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_JOB_INVALID */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_invalid,
						       NULL));
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_DELETED);

	nih_list_free (&job->entry);
	event_poll ();


	/* Check that if we ask to stop a replacement job, which we have to do
	 * by its id since it won't be found otherwise, we get an error
	 * in reply.
	 */
	TEST_FEATURE ("with replacement job");
	job = job_new (NULL, "test");
	job->id = 0xdeafbeef;
	job->replacement_for = (void *)-1;
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process[PROCESS_MAIN] = job_process_new (job->process);
	job->process[PROCESS_MAIN]->command = "echo";
	job->process[PROCESS_MAIN]->pid = 1000;

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_STOP, NULL,
					       0xdeafbeef);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_JOB_INVALID */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_invalid,
						       NULL));
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);

	nih_list_free (&job->entry);
	event_poll ();


	/* Check that if we ask to stop a job that is already stopped,
	 * we get an unchanged message in reply.
	 */
	TEST_FEATURE ("with already stopped job");
	job = job_new (NULL, "test");
	job->id = 0xdeafbeef;
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->process[PROCESS_MAIN] = job_process_new (job->process);
	job->process[PROCESS_MAIN]->command = "echo";

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_JOB_STOP, "test", 0);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_JOB_UNCHANGED */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_unchanged,
						       NULL));
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_WAITING);

	nih_list_free (&job->entry);
	event_poll ();


	control_close ();
	upstart_disable_safeties = FALSE;
}


void
test_event_emit (void)
{
	NihIo              *io;
	pid_t               pid;
	int                 wait_fd, status;
	EventEmission      *em;
	NotifySubscription *sub;

	/* Check that we can handle a message from a child process requesting
	 * that an event be emitted.  We don't send an immediate reply,
	 * however we should be able to find the event in the queue and see
	 * that there's a subscription on it.
	 */
	TEST_FUNCTION ("control_event_emit");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	event_init ();

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage  *message;
		char         **args, **env;
		int            sock;

		sock = upstart_open ();

		args = nih_str_array_new (NULL);
		NIH_MUST (nih_str_array_add (&args, NULL, NULL, "foo"));
		NIH_MUST (nih_str_array_add (&args, NULL, NULL, "bar"));

		env = nih_str_array_new (NULL);
		NIH_MUST (nih_str_array_add (&env, NULL, NULL, "FOO=BAR"));

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_EVENT_EMIT, "wibble",
					       args, env);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		nih_free (args);
		nih_free (env);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	em = (EventEmission *)events->prev;
	TEST_EQ_STR (em->event.name, "wibble");
	TEST_EQ_STR (em->event.args[0], "foo");
	TEST_EQ_STR (em->event.args[1], "bar");
	TEST_EQ_P (em->event.args[2], NULL);
	TEST_EQ_STR (em->event.env[0], "FOO=BAR");
	TEST_EQ_P (em->event.env[1], NULL);

	sub = notify_subscription_find (pid, NOTIFY_EVENT, em);
	TEST_NE_P (sub, NULL);

	nih_list_free (&em->event.entry);
	event_poll ();


	control_close ();
	upstart_disable_safeties = FALSE;
}


void
test_subscribe_jobs (void)
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
	TEST_FUNCTION ("control_subscribe_jobs");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	job = job_new (NULL, "test");
	job->id = 0xdeafbeef;
	job->goal = JOB_START;
	job->state = JOB_STARTING;
	job->process[PROCESS_PRE_START] = job_process_new (job->process);
	job->process[PROCESS_MAIN] = job_process_new (job->process);
	job->process[PROCESS_POST_STOP] = job_process_new (job->process);

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_SUBSCRIBE_JOBS);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_JOB_STATUS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status__starting,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS_END */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status_end__starting,
						       NULL));
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	sub = notify_subscription_find (pid, NOTIFY_JOB, NULL);
	TEST_NE_P (sub, NULL);

	notify_job (job);

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
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
test_unsubscribe_jobs (void)
{
	NihIo              *io;
	pid_t               pid;
	int                 wait_fd, status;
	Job                *job;
	NotifySubscription *sub;

	/* Check that we can handle a message from a child process asking us
	 * to unsubscribe them from job status notifications.
	 */
	TEST_FUNCTION ("control_unsubscribe_jobs");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	job = job_new (NULL, "test");
	job->id = 0xdeafbeef;
	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;
	job->process[PROCESS_PRE_START] = job_process_new (job->process);
	job->process[PROCESS_MAIN] = job_process_new (job->process);
	job->process[PROCESS_MAIN]->pid = 1000;
	job->process[PROCESS_POST_STOP] = job_process_new (job->process);

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_SUBSCRIBE_JOBS);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_JOB_STATUS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status__stopping,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_PROCESS */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_process,
						       NULL));
		nih_free (message);

		/* Should receive UPSTART_JOB_STATUS_END */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_job_status_end__stopping,
						       NULL));
		nih_free (message);

		/* Having received a status update, we know the parent has
		 * found our subscription, so now we unsubscribe.
		 */
		message = upstart_message_new (NULL, getppid (),
					       UPSTART_UNSUBSCRIBE_JOBS);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	sub = notify_subscription_find (pid, NOTIFY_JOB, NULL);
	TEST_NE_P (sub, NULL);

	destructor_called = 0;
	nih_alloc_set_destructor (sub, my_destructor);

	notify_job (job);

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	TEST_TRUE (destructor_called);

	nih_list_free (&job->entry);


	control_close ();
	upstart_disable_safeties = FALSE;
}

void
test_subscribe_events (void)
{
	NihIo              *io;
	pid_t               pid;
	int                 wait_fd, status;
	EventEmission      *emission;
	NotifySubscription *sub;

	/* Check that we can handle a message from a child process asking us
	 * to subscribe them to event notifications.  We then emit an event
	 * so that the child gets a notification.
	 */
	TEST_FUNCTION ("control_subscribe_events");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_SUBSCRIBE_EVENTS);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_EVENT */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_event,
						       NULL));
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	sub = notify_subscription_find (pid, NOTIFY_EVENT, NULL);
	TEST_NE_P (sub, NULL);

	emission = event_emit ("test", NULL, NULL);
	emission->id = 0xdeafbeef;
	notify_event (emission);

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	nih_list_free (&emission->event.entry);
	nih_list_free (&sub->entry);


	control_close ();
	upstart_disable_safeties = FALSE;
}

void
test_unsubscribe_events (void)
{
	NihIo              *io;
	pid_t               pid;
	int                 wait_fd, status;
	EventEmission      *emission;
	NotifySubscription *sub;

	/* Check that we can handle a message from a child process asking us
	 * to unsubscribe them from event notifications.
	 */
	TEST_FUNCTION ("control_unsubscribe_events");
	io = control_open ();
	upstart_disable_safeties = TRUE;

	fflush (stdout);
	TEST_CHILD_WAIT (pid, wait_fd) {
		NihIoMessage *message;
		int           sock;
		size_t        len;

		sock = upstart_open ();

		message = upstart_message_new (NULL, getppid (),
					       UPSTART_SUBSCRIBE_EVENTS);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		/* Allow the parent to continue so it can receive it */
		TEST_CHILD_RELEASE (wait_fd);

		/* Should receive UPSTART_EVENT */
		message = nih_io_message_recv (NULL, sock, &len);
		assert0 (upstart_message_handle_using (message, message,
						       (UpstartMessageHandler)
						       check_event,
						       NULL));
		nih_free (message);

		/* Having received an event, we know the parent has
		 * found our subscription, so now we unsubscribe.
		 */
		message = upstart_message_new (NULL, getppid (),
					       UPSTART_UNSUBSCRIBE_EVENTS);
		assert (nih_io_message_send (message, sock) > 0);
		nih_free (message);

		exit (0);
	}

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	sub = notify_subscription_find (pid, NOTIFY_EVENT, NULL);
	TEST_NE_P (sub, NULL);

	destructor_called = 0;
	nih_alloc_set_destructor (sub, my_destructor);

	emission = event_emit ("test", NULL, NULL);
	emission->id = 0xdeafbeef;
	notify_event (emission);

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		exit (1);

	io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);
	while (! NIH_LIST_EMPTY (io->send_q))
		io->watch->watcher (io, io->watch, NIH_IO_READ | NIH_IO_WRITE);

	TEST_TRUE (destructor_called);

	nih_list_free (&emission->event.entry);


	control_close ();
	upstart_disable_safeties = FALSE;
}


int
main (int   argc,
      char *argv[])
{
	test_open ();
	test_close ();
	test_error_handler ();
	test_send_job_status ();
	test_send_instance ();
	test_version_query ();
	test_log_priority ();
	test_job_find ();
	test_job_query ();
	test_job_start ();
	test_job_stop ();
	test_event_emit ();
	test_subscribe_jobs ();
	test_unsubscribe_jobs ();
	test_subscribe_events ();
	test_unsubscribe_events ();

	return 0;
}
