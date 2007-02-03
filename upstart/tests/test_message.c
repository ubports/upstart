/* libupstart
 *
 * test_message.c - test suite for upstart/message.c
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
#include <sys/socket.h>
#include <sys/un.h>

#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/io.h>
#include <nih/error.h>

#include <upstart/errors.h>
#include <upstart/message.h>
#include <upstart/enum.h>


extern int upstart_disable_safeties;


void
test_open (void)
{
	struct sockaddr_un addr;
	int                sock, val;
	char               name[108];
	socklen_t          len;

	/* Check that the socket opened is a datagram socket in the
	 * AF_UNIX abstract namespace with a path that includes the PID
	 * of the current process.  The SO_PASSCRED option should be set
	 * so that we can get the credentials of any sender.
	 */
	TEST_FUNCTION ("upstart_open");
	sock = upstart_open ();

	len = sizeof (addr);
	getsockname (sock, (struct sockaddr *)&addr, &len);

	TEST_EQ (addr.sun_family, AF_UNIX);
	TEST_EQ (addr.sun_path[0], '\0');

	sprintf (name, "/com/ubuntu/upstart/%d", getpid ());
	TEST_EQ_STRN (addr.sun_path + 1, name);

	val = 0;
	len = sizeof (val);
	getsockopt (sock, SOL_SOCKET, SO_TYPE, &val, &len);

	TEST_EQ (val, SOCK_DGRAM);

	val = 0;
	len = sizeof (val);
	getsockopt (sock, SOL_SOCKET, SO_PASSCRED, &val, &len);

	TEST_NE (val, 0);


	close (sock);
}


void
test_new (void)
{
	NihIoMessage *msg;

	TEST_FUNCTION ("upstart_message_new");


	/* Check that we can create a UPSTART_NO_OP message, and have
	 * the returned structure look right; with the address filled in
	 * properly and everything.
	 */
	TEST_FEATURE ("with UPSTART_NO_OP message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_NO_OP);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->int_data, UPSTART_INIT_DAEMON);
		TEST_EQ (msg->addrlen,
			 offsetof (struct sockaddr_un, sun_path) + 20);
		TEST_ALLOC_SIZE (msg->addr, sizeof (struct sockaddr_un));
		TEST_ALLOC_PARENT (msg->addr, msg);
		TEST_EQ (((struct sockaddr_un *)msg->addr)->sun_family,
			 AF_UNIX);
		TEST_EQ (((struct sockaddr_un *)msg->addr)->sun_path[0], '\0');
		TEST_EQ_MEM (((struct sockaddr_un *)msg->addr)->sun_path + 1,
			     "/com/ubuntu/upstart", 19);

		TEST_EQ (msg->data->len, 12);
		TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\0", 12);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_START message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_START message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_START, "test");

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 20);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\0\x01\0\0\0\x04test", 20);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_STOP message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_STOP message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_STOP, "test");

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 20);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\0\x02\0\0\0\x04test", 20);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_QUERY message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_QUERY message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_QUERY, "test");

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 20);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\0\x03\0\0\0\x04test", 20);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_STATUS message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_STATUS message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_STATUS, "test",
					   JOB_START, JOB_RUNNING,
					   PROCESS_ACTIVE, 1000, "foo bar");

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 47);
		TEST_EQ_MEM (msg->data->buf, ("upstart\n\0\0\0\x04"
					      "\0\0\0\x04test\0\0\0\x01"
					      "\0\0\0\x02\0\0\0\x02"
					      "\0\0\x03\xe8\0\0\0\afoo bar"),
			     47);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_UNKNOWN message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_UNKNOWN message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_UNKNOWN, "test");

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 20);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\0\x05\0\0\0\x04test", 20);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_LIST message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_LIST message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_LIST);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 12);
		TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\x06", 12);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_LIST_END message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_LIST_END message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_LIST_END);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 12);
		TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\x07", 12);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_EVENT_QUEUE message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_EVENT_QUEUE message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_EVENT_QUEUE, "test");

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 20);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\0\x08\0\0\0\x04test", 20);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_EVENT message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_EVENT message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_EVENT, "test");

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 20);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\0\x09\0\0\0\x04test", 20);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_WATCH_JOBS message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_WATCH_JOBS message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_WATCH_JOBS);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 12);
		TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\x0a", 12);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_UNWATCH_JOBS message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_UNWATCH_JOBS message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_UNWATCH_JOBS);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 12);
		TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\x0b", 12);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_WATCH_EVENTS message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_WATCH_EVENTS message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_WATCH_EVENTS);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 12);
		TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\x0c", 12);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_UNWATCH_EVENTS message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_UNWATCH_EVENTS message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_UNWATCH_EVENTS);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 12);
		TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\x0d", 12);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_SHUTDOWN message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_SHUTDOWN message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_SHUTDOWN, "test");

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 20);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\0\x0e\0\0\0\x04test", 20);

		nih_free (msg);
	}
}


static int destructor_called = 0;

int
my_destructor (void *ptr)
{
	destructor_called++;

	return 0;
}

static int handler_called = 0;
static void *last_data = NULL;
static pid_t last_pid = 0;
static UpstartMessageType last_type = 0;

int
my_handler (void                *data,
	    pid_t               pid,
	    UpstartMessageType  type,
	    ...)
{
	va_list args;

	handler_called++;
	last_data = data;
	last_pid = pid;
	last_type = type;

	if (pid == 999)
		return 100;

	va_start (args, type);

	switch (type) {
	case UPSTART_JOB_START:
	case UPSTART_JOB_STOP:
	case UPSTART_JOB_QUERY:
	case UPSTART_JOB_UNKNOWN:
	case UPSTART_EVENT_QUEUE:
	case UPSTART_EVENT:
	case UPSTART_SHUTDOWN: {
		char *name;

		name = va_arg (args, char *);

		TEST_EQ_STR (name, "test");

		if (pid == 2000) {
			nih_alloc_set_destructor (name, my_destructor);
		} else {
			nih_free (name);
		}

		break;
	}
	case UPSTART_JOB_STATUS: {
		char *name, *description;
		int   goal, state, process_state, pid;

		name = va_arg (args, char *);
		goal = va_arg (args, int);
		state = va_arg (args, int);
		process_state = va_arg (args, int);
		pid = va_arg (args, int);
		description = va_arg (args, char *);

		TEST_EQ_STR (name, "test");
		TEST_EQ (goal, JOB_START);
		TEST_EQ (state, JOB_RUNNING);
		TEST_EQ (process_state, PROCESS_ACTIVE);
		TEST_EQ (pid, 1000);
		TEST_EQ_STR (description, "foo bar");

		nih_free (name);
		nih_free (description);

		break;
	}
	default:
		break;
	}

	va_end (args);

	return 0;
}

static UpstartMessage no_op_handler[] = {
	{ 1000, UPSTART_NO_OP, my_handler },
	UPSTART_MESSAGE_LAST
};

static UpstartMessage any_handler[] = {
	{ -1, -1, my_handler },
	UPSTART_MESSAGE_LAST
};

void
test_handle (void)
{
	NihIoMessage *msg;
	NihError     *err;
	struct ucred  cred = { 1000, 1000, 1000 };
	int           ret;

	TEST_FUNCTION ("upstart_message_handle");
	upstart_disable_safeties = TRUE;


	/* Check that we call the handler function for an UPSTART_NO_OP
	 * message from a particular process id.
	 */
	TEST_FEATURE ("with UPSTART_NO_OP message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (msg->data,
						     "upstart\n\0\0\0\0", 12));
			assert0 (nih_io_message_add_control (msg, SOL_SOCKET,
							     SCM_CREDENTIALS,
							     sizeof (cred),
							     &cred));
		}

		handler_called = FALSE;
		last_data = NULL;
		last_pid = -1;
		last_type = -1;

		ret = upstart_message_handle (NULL, msg, no_op_handler, &ret);

		TEST_EQ (ret, 0);
		TEST_TRUE (handler_called);
		TEST_EQ_P (last_data, &ret);
		TEST_EQ (last_pid, 1000);
		TEST_EQ (last_type, UPSTART_NO_OP);

		nih_free (msg);
	}


	/* Check that we call the handler function for an UPSTART_JOB_START
	 * message.
	 */
	TEST_FEATURE ("with UPSTART_JOB_START message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (msg->data,
						     ("upstart\n\0\0\0\x1"
						      "\0\0\0\x4test"), 20));
			assert0 (nih_io_message_add_control (msg, SOL_SOCKET,
							     SCM_CREDENTIALS,
							     sizeof (cred),
							     &cred));
		}

		handler_called = FALSE;
		last_data = NULL;
		last_pid = -1;
		last_type = -1;

		ret = upstart_message_handle (NULL, msg, any_handler, &ret);

		TEST_EQ (ret, 0);
		TEST_TRUE (handler_called);
		TEST_EQ_P (last_data, &ret);
		TEST_EQ (last_pid, 1000);
		TEST_EQ (last_type, UPSTART_JOB_START);

		nih_free (msg);
	}


	/* Check that we call the handler function for an UPSTART_JOB_STOP
	 * message.
	 */
	TEST_FEATURE ("with UPSTART_JOB_STOP message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (msg->data,
						     ("upstart\n\0\0\0\x2"
						      "\0\0\0\x4test"), 20));
			assert0 (nih_io_message_add_control (msg, SOL_SOCKET,
							     SCM_CREDENTIALS,
							     sizeof (cred),
							     &cred));
		}

		handler_called = FALSE;
		last_data = NULL;
		last_pid = -1;
		last_type = -1;

		ret = upstart_message_handle (NULL, msg, any_handler, &ret);

		TEST_EQ (ret, 0);
		TEST_TRUE (handler_called);
		TEST_EQ_P (last_data, &ret);
		TEST_EQ (last_pid, 1000);
		TEST_EQ (last_type, UPSTART_JOB_STOP);

		nih_free (msg);
	}


	/* Check that we call the handler function for an UPSTART_JOB_QUERY
	 * message.
	 */
	TEST_FEATURE ("with UPSTART_JOB_QUERY message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (msg->data,
						     ("upstart\n\0\0\0\x3"
						      "\0\0\0\x4test"), 20));
			assert0 (nih_io_message_add_control (msg, SOL_SOCKET,
							     SCM_CREDENTIALS,
							     sizeof (cred),
							     &cred));
		}

		handler_called = FALSE;
		last_data = NULL;
		last_pid = -1;
		last_type = -1;

		ret = upstart_message_handle (NULL, msg, any_handler, &ret);

		TEST_EQ (ret, 0);
		TEST_TRUE (handler_called);
		TEST_EQ_P (last_data, &ret);
		TEST_EQ (last_pid, 1000);
		TEST_EQ (last_type, UPSTART_JOB_QUERY);

		nih_free (msg);
	}


	/* Check that we call the handler function for an UPSTART_JOB_STATUS
	 * message.
	 */
	TEST_FEATURE ("with UPSTART_JOB_STATUS message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (msg->data,
						     ("upstart\n\0\0\0\x04"
						      "\0\0\0\x04test"
						      "\0\0\0\x01\0\0\0\x02"
						      "\0\0\0\x02\0\0\x03"
						      "\xe8\0\0\0\afoo bar"),
						     47));
			assert0 (nih_io_message_add_control (msg, SOL_SOCKET,
							     SCM_CREDENTIALS,
							     sizeof (cred),
							     &cred));
		}

		handler_called = FALSE;
		last_data = NULL;
		last_pid = -1;
		last_type = -1;

		ret = upstart_message_handle (NULL, msg, any_handler, &ret);

		TEST_EQ (ret, 0);
		TEST_TRUE (handler_called);
		TEST_EQ_P (last_data, &ret);
		TEST_EQ (last_pid, 1000);
		TEST_EQ (last_type, UPSTART_JOB_STATUS);

		nih_free (msg);
	}


	/* Check that we call the handler function for an UPSTART_JOB_UNKNOWN
	 * message.
	 */
	TEST_FEATURE ("with UPSTART_JOB_UNKNOWN message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (msg->data,
						     ("upstart\n\0\0\0\x5"
						      "\0\0\0\x4test"), 20));
			assert0 (nih_io_message_add_control (msg, SOL_SOCKET,
							     SCM_CREDENTIALS,
							     sizeof (cred),
							     &cred));
		}

		handler_called = FALSE;
		last_data = NULL;
		last_pid = -1;
		last_type = -1;

		ret = upstart_message_handle (NULL, msg, any_handler, &ret);

		TEST_EQ (ret, 0);
		TEST_TRUE (handler_called);
		TEST_EQ_P (last_data, &ret);
		TEST_EQ (last_pid, 1000);
		TEST_EQ (last_type, UPSTART_JOB_UNKNOWN);

		nih_free (msg);
	}


	/* Check that we call the handler function for an UPSTART_JOB_LIST
	 * message.
	 */
	TEST_FEATURE ("with UPSTART_JOB_LIST message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (msg->data,
						     "upstart\n\0\0\0\x6",
						     12));
			assert0 (nih_io_message_add_control (msg, SOL_SOCKET,
							     SCM_CREDENTIALS,
							     sizeof (cred),
							     &cred));
		}

		handler_called = FALSE;
		last_data = NULL;
		last_pid = -1;
		last_type = -1;

		ret = upstart_message_handle (NULL, msg, any_handler, &ret);

		TEST_EQ (ret, 0);
		TEST_TRUE (handler_called);
		TEST_EQ_P (last_data, &ret);
		TEST_EQ (last_pid, 1000);
		TEST_EQ (last_type, UPSTART_JOB_LIST);

		nih_free (msg);
	}


	/* Check that we call the handler function for an UPSTART_JOB_LIST_END
	 * message.
	 */
	TEST_FEATURE ("with UPSTART_JOB_LIST_END message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (msg->data,
						     "upstart\n\0\0\0\x7",
						     12));
			assert0 (nih_io_message_add_control (msg, SOL_SOCKET,
							     SCM_CREDENTIALS,
							     sizeof (cred),
							     &cred));
		}

		handler_called = FALSE;
		last_data = NULL;
		last_pid = -1;
		last_type = -1;

		ret = upstart_message_handle (NULL, msg, any_handler, &ret);

		TEST_EQ (ret, 0);
		TEST_TRUE (handler_called);
		TEST_EQ_P (last_data, &ret);
		TEST_EQ (last_pid, 1000);
		TEST_EQ (last_type, UPSTART_JOB_LIST_END);

		nih_free (msg);
	}


	/* Check that we call the handler function for an UPSTART_EVENT_QUEUE
	 * message.
	 */
	TEST_FEATURE ("with UPSTART_EVENT_QUEUE message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (msg->data,
						     ("upstart\n\0\0\0\x8"
						      "\0\0\0\x4test"), 20));
			assert0 (nih_io_message_add_control (msg, SOL_SOCKET,
							     SCM_CREDENTIALS,
							     sizeof (cred),
							     &cred));
		}

		handler_called = FALSE;
		last_data = NULL;
		last_pid = -1;
		last_type = -1;

		ret = upstart_message_handle (NULL, msg, any_handler, &ret);

		TEST_EQ (ret, 0);
		TEST_TRUE (handler_called);
		TEST_EQ_P (last_data, &ret);
		TEST_EQ (last_pid, 1000);
		TEST_EQ (last_type, UPSTART_EVENT_QUEUE);

		nih_free (msg);
	}


	/* Check that we call the handler function for an UPSTART_EVENT
	 * message.
	 */
	TEST_FEATURE ("with UPSTART_EVENT message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (msg->data,
						     ("upstart\n\0\0\0\x9"
						      "\0\0\0\x4test"), 20));
			assert0 (nih_io_message_add_control (msg, SOL_SOCKET,
							     SCM_CREDENTIALS,
							     sizeof (cred),
							     &cred));
		}

		handler_called = FALSE;
		last_data = NULL;
		last_pid = -1;
		last_type = -1;

		ret = upstart_message_handle (NULL, msg, any_handler, &ret);

		TEST_EQ (ret, 0);
		TEST_TRUE (handler_called);
		TEST_EQ_P (last_data, &ret);
		TEST_EQ (last_pid, 1000);
		TEST_EQ (last_type, UPSTART_EVENT);

		nih_free (msg);
	}


	/* Check that we call the handler function for an UPSTART_WATCH_JOBS
	 * message.
	 */
	TEST_FEATURE ("with UPSTART_WATCH_JOBS message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (msg->data,
						     "upstart\n\0\0\0\xa",
						     12));
			assert0 (nih_io_message_add_control (msg, SOL_SOCKET,
							     SCM_CREDENTIALS,
							     sizeof (cred),
							     &cred));
		}

		handler_called = FALSE;
		last_data = NULL;
		last_pid = -1;
		last_type = -1;

		ret = upstart_message_handle (NULL, msg, any_handler, &ret);

		TEST_EQ (ret, 0);
		TEST_TRUE (handler_called);
		TEST_EQ_P (last_data, &ret);
		TEST_EQ (last_pid, 1000);
		TEST_EQ (last_type, UPSTART_WATCH_JOBS);

		nih_free (msg);
	}


	/* Check that we call the handler function for an UPSTART_UNWATCH_JOBS
	 * message.
	 */
	TEST_FEATURE ("with UPSTART_UNWATCH_JOBS message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (msg->data,
						     "upstart\n\0\0\0\xb",
						     12));
			assert0 (nih_io_message_add_control (msg, SOL_SOCKET,
							     SCM_CREDENTIALS,
							     sizeof (cred),
							     &cred));
		}

		handler_called = FALSE;
		last_data = NULL;
		last_pid = -1;
		last_type = -1;

		ret = upstart_message_handle (NULL, msg, any_handler, &ret);

		TEST_EQ (ret, 0);
		TEST_TRUE (handler_called);
		TEST_EQ_P (last_data, &ret);
		TEST_EQ (last_pid, 1000);
		TEST_EQ (last_type, UPSTART_UNWATCH_JOBS);

		nih_free (msg);
	}


	/* Check that we call the handler function for an UPSTART_WATCH_EVENTS
	 * message.
	 */
	TEST_FEATURE ("with UPSTART_WATCH_EVENTS message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (msg->data,
						     "upstart\n\0\0\0\xc",
						     12));
			assert0 (nih_io_message_add_control (msg, SOL_SOCKET,
							     SCM_CREDENTIALS,
							     sizeof (cred),
							     &cred));
		}

		handler_called = FALSE;
		last_data = NULL;
		last_pid = -1;
		last_type = -1;

		ret = upstart_message_handle (NULL, msg, any_handler, &ret);

		TEST_EQ (ret, 0);
		TEST_TRUE (handler_called);
		TEST_EQ (last_data, &ret);
		TEST_EQ (last_pid, 1000);
		TEST_EQ (last_type, UPSTART_WATCH_EVENTS);

		nih_free (msg);
	}


	/* Check that we call the handler function for an
	 * UPSTART_UNWATCH_EVENTS message.
	 */
	TEST_FEATURE ("with UPSTART_UNWATCH_EVENTS message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (msg->data,
						     "upstart\n\0\0\0\xd",
						     12));
			assert0 (nih_io_message_add_control (msg, SOL_SOCKET,
							     SCM_CREDENTIALS,
							     sizeof (cred),
							     &cred));
		}

		handler_called = FALSE;
		last_data = NULL;
		last_pid = -1;
		last_type = -1;

		ret = upstart_message_handle (NULL, msg, any_handler, &ret);

		TEST_EQ (ret, 0);
		TEST_TRUE (handler_called);
		TEST_EQ_P (last_data, &ret);
		TEST_EQ (last_pid, 1000);
		TEST_EQ (last_type, UPSTART_UNWATCH_EVENTS);

		nih_free (msg);
	}


	/* Check that we call the handler function for an UPSTART_SHUTDOWN
	 * message.
	 */
	TEST_FEATURE ("with UPSTART_SHUTDOWN message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (msg->data,
						     ("upstart\n\0\0\0\xe"
						      "\0\0\0\x4test"), 20));
			assert0 (nih_io_message_add_control (msg, SOL_SOCKET,
							     SCM_CREDENTIALS,
							     sizeof (cred),
							     &cred));
		}

		handler_called = FALSE;
		last_data = NULL;
		last_pid = -1;
		last_type = -1;

		ret = upstart_message_handle (NULL, msg, any_handler, &ret);

		TEST_EQ (ret, 0);
		TEST_TRUE (handler_called);
		TEST_EQ_P (last_data, &ret);
		TEST_EQ (last_pid, 1000);
		TEST_EQ (last_type, UPSTART_SHUTDOWN);

		nih_free (msg);
	}


	/* Check that the UPSTART_MESSAGE_INVALID error is raised if the
	 * message is invalid.
	 */
	TEST_FEATURE ("with invalid message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data, "snarf", 5));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;

	ret = upstart_message_handle (NULL, msg, no_op_handler, &ret);

	TEST_LT (ret, 0);
	TEST_FALSE (handler_called);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_MESSAGE_INVALID);

	nih_free (err);

	nih_free (msg);

	cred.pid = 1000;


	/* Check that no handler is called if the message doesn't come from
	 * the right pid and the unknown message error is raised instead.
	 */
	TEST_FEATURE ("with message from unexpected pid");
	cred.pid = 1234;

	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data, "upstart\n\0\0\0\0", 12));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;

	ret = upstart_message_handle (NULL, msg, no_op_handler, &ret);

	TEST_LT (ret, 0);
	TEST_FALSE (handler_called);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_MESSAGE_UNKNOWN);

	nih_free (err);

	nih_free (msg);

	cred.pid = 1000;


	/* Check that no handler is called if the message type isn't right. */
	TEST_FEATURE ("with message of unexpected type");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     "upstart\n\0\0\0\1\0\0\0\x4test", 20));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;

	ret = upstart_message_handle (NULL, msg, no_op_handler, &ret);

	TEST_LT (ret, 0);
	TEST_FALSE (handler_called);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_MESSAGE_UNKNOWN);

	nih_free (err);

	nih_free (msg);


	/* Check that a handler is called if the pid is a wildcard. */
	TEST_FEATURE ("with wildcard pid");
	cred.pid = 1234;

	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data, "upstart\n\0\0\0\0", 12));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;
	last_data = NULL;
	last_pid = -1;
	last_type = -1;

	ret = upstart_message_handle (NULL, msg, any_handler, &ret);

	TEST_EQ (ret, 0);
	TEST_TRUE (handler_called);
	TEST_EQ_P (last_data, &ret);
	TEST_EQ (last_pid, 1234);
	TEST_EQ (last_type, UPSTART_NO_OP);

	nih_free (msg);

	cred.pid = 1000;


	/* Check that a handler is called if the type is a wildcard. */
	TEST_FEATURE ("with wildcard message type");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     "upstart\n\0\0\0\1\0\0\0\x4test", 20));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;
	last_data = NULL;
	last_pid = -1;
	last_type = -1;

	ret = upstart_message_handle (NULL, msg, any_handler, &ret);

	TEST_EQ (ret, 0);
	TEST_TRUE (handler_called);
	TEST_EQ_P (last_data, &ret);
	TEST_EQ (last_pid, 1000);
	TEST_EQ (last_type, UPSTART_JOB_START);

	nih_free (msg);


	/* Check that the return value from a handler is what gets
	 * returned.
	 */
	TEST_FEATURE ("with return value from handler");
	cred.pid = 999;

	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data, "upstart\n\0\0\0\0", 12));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;
	last_data = NULL;
	last_pid = -1;
	last_type = -1;

	ret = upstart_message_handle (NULL, msg, any_handler, &ret);

	TEST_EQ (ret, 100);
	TEST_TRUE (handler_called);
	TEST_EQ_P (last_data, &ret);
	TEST_EQ (last_pid, 999);
	TEST_EQ (last_type, UPSTART_NO_OP);

	nih_free (msg);

	cred.pid = 1000;


	upstart_disable_safeties = FALSE;


	/* Check that the illegal source error is raised if the message
	 * comes from a bad source.
	 */
	TEST_FEATURE ("with illegal source");
	cred.pid = 1234;
	cred.uid = 999;
	cred.gid = 876;

	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data, "upstart\n\0\0\0\0", 12));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;

	ret = upstart_message_handle (NULL, msg, no_op_handler, &ret);

	TEST_LT (ret, 0);
	TEST_FALSE (handler_called);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_MESSAGE_ILLEGAL);

	nih_free (err);

	nih_free (msg);
}

void
test_handle_using (void)
{
	NihIoMessage *msg;
	struct ucred  cred = { 1000, 1000, 1000 };
	int           ret;

	/* Check that the handler function is called for the message we
	 * pass.
	 */
	TEST_FUNCTION ("upstart_message_handle_using");
	upstart_disable_safeties = TRUE;
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (msg->data,
						     ("upstart\n\0\0\0\x1"
						      "\0\0\0\x4test"), 20));
			assert0 (nih_io_message_add_control (msg, SOL_SOCKET,
							     SCM_CREDENTIALS,
							     sizeof (cred),
							     &cred));
		}

		handler_called = FALSE;
		last_data = NULL;
		last_pid = -1;
		last_type = -1;

		ret = upstart_message_handle_using (NULL, msg, my_handler, &ret);

		TEST_EQ (ret, 0);
		TEST_TRUE (handler_called);
		TEST_EQ_P (last_data, &ret);
		TEST_EQ (last_pid, 1000);
		TEST_EQ (last_type, UPSTART_JOB_START);

		nih_free (msg);
	}

	upstart_disable_safeties = FALSE;
}


void
test_reader (void)
{
	NihIo        *io;
	NihIoMessage *msg;
	struct ucred  cred = { 2000, 1000, 1000 };

	/* The message reader function should take the first message from
	 * the queue, handle it, and then free the message also causing any
	 * strings to be freed.
	 */
	TEST_FUNCTION ("upstart_message_reader");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			io = nih_io_reopen (NULL, 0, NIH_IO_MESSAGE,
					    (NihIoReader)upstart_message_reader,
					    NULL, NULL, any_handler);

			msg = nih_io_message_new (io);
			assert0 (nih_io_buffer_push (msg->data,
						     ("upstart\n\0\0\0\1"
						      "\0\0\0\x4test"), 20));
			assert0 (nih_io_message_add_control (msg, SOL_SOCKET,
							     SCM_CREDENTIALS,
							     sizeof (cred),
							     &cred));
		}

		nih_alloc_set_destructor (msg, my_destructor);

		nih_list_add (io->recv_q, &msg->entry);

		handler_called = FALSE;
		last_data = NULL;
		last_pid = -1;
		last_type = -1;

		destructor_called = 0;

		upstart_disable_safeties = TRUE;

		upstart_message_reader (any_handler, io,
					msg->data->buf, msg->data->len);

		upstart_disable_safeties = FALSE;

		TEST_TRUE (handler_called);
		TEST_EQ_P (last_data, any_handler);
		TEST_EQ (last_pid, 2000);
		TEST_EQ (last_type, UPSTART_JOB_START);
		TEST_EQ (destructor_called, 2);
		TEST_LIST_EMPTY (io->recv_q);

		nih_free (io);
	}
}


int
main (int   argc,
      char *argv[])
{
	test_open ();
	test_new ();
	test_handle ();
	test_handle_using ();
	test_reader ();

	return 0;
}
