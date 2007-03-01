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
#include <nih/string.h>
#include <nih/io.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <upstart/errors.h>
#include <upstart/message.h>
#include <upstart/enum.h>


extern int upstart_disable_safeties;


void
test_open (void)
{
	struct sockaddr_un addr;
	int                sock, ret, val;
	char               name[108];
	socklen_t          len;
	NihError          *err;

	TEST_FUNCTION ("upstart_open");


	/* Check that the socket opened is a datagram socket in the
	 * AF_UNIX abstract namespace with a path that includes the PID
	 * of the current process.  The SO_PASSCRED option should be set
	 * so that we can get the credentials of any sender.
	 */
	TEST_FEATURE ("with no socket open");
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


	/* Check that attempting to open the socket again results in
	 * EADDRINUSE being raised.
	 */
	TEST_FEATURE ("with socket already open");
	ret = upstart_open ();

	TEST_LT (ret, 0);

	err = nih_error_get ();
	TEST_EQ (err->number, EADDRINUSE);
	nih_free (err);


	close (sock);
}


void
test_new (void)
{
	NihIoMessage  *msg;
	char         **args, **env;

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

		TEST_EQ (msg->data->len, 21);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\0\x01s\0\0\0\x04test", 21);

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

		TEST_EQ (msg->data->len, 21);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\0\x02s\0\0\0\x04test", 21);

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

		TEST_EQ (msg->data->len, 21);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\0\x03s\0\0\0\x04test", 21);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_STATUS message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_STATUS message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_STATUS, "test",
					   JOB_START, JOB_RUNNING);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 31);
		TEST_EQ_MEM (msg->data->buf, ("upstart\n\0\0\0\x04"
					      "s\0\0\0\x04testi\0\0\0\x01"
					      "i\0\0\0\x05"), 31);

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

		TEST_EQ (msg->data->len, 21);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\0\x05s\0\0\0\x04test", 21);

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


	/* Check that we can create an UPSTART_EVENT_EMIT message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_EVENT_EMIT message");
	args = nih_str_array_new (NULL);
	NIH_MUST (nih_str_array_add (&args, NULL, NULL, "frodo"));
	NIH_MUST (nih_str_array_add (&args, NULL, NULL, "bilbo"));

	env = nih_str_array_new (NULL);
	NIH_MUST (nih_str_array_add (&env, NULL, NULL, "FOO=BAR"));

	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_EVENT_EMIT, "test",
					   args, env);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 57);
		TEST_EQ_MEM (msg->data->buf,
			     ("upstart\n\0\0\x02\x00s\0\0\0\x04test"
			      "as\0\0\0\05frodos\0\0\0\05bilboS"
			      "as\0\0\0\07FOO=BARS"), 57);

		nih_free (msg);
	}

	nih_free (args);
	nih_free (env);


	/* Check that we can create an UPSTART_EVENT message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_EVENT message");
	args = nih_str_array_new (NULL);
	NIH_MUST (nih_str_array_add (&args, NULL, NULL, "frodo"));
	NIH_MUST (nih_str_array_add (&args, NULL, NULL, "bilbo"));

	env = nih_str_array_new (NULL);
	NIH_MUST (nih_str_array_add (&env, NULL, NULL, "FOO=BAR"));

	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_EVENT, 0xdeafbeef,
					   "test", args, env);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 62);
		TEST_EQ_MEM (msg->data->buf,
			     ("upstart\n\0\0\x02\x10u\xde\xaf\xbe\xef"
			      "s\0\0\0\x04test"
			      "as\0\0\0\05frodos\0\0\0\05bilboS"
			      "as\0\0\0\07FOO=BARS"), 62);

		nih_free (msg);
	}

	nih_free (args);
	nih_free (env);


	/* Check that we can create an UPSTART_EVENT_JOB_STATUS message
	 * and have the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_EVENT_JOB_STATUS message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_EVENT_JOB_STATUS,
					   0xdeafbeef, "test",
					   JOB_START, JOB_RUNNING);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 36);
		TEST_EQ_MEM (msg->data->buf,
			     ("upstart\n\0\0\x02\x11u\xde\xaf\xbe\xef"
			      "s\0\0\0\x04testi\0\0\0\x01i\0\0\0\x05"), 36);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_EVENT_FINISHED message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_EVENT_FINISHED message");
	args = nih_str_array_new (NULL);
	NIH_MUST (nih_str_array_add (&args, NULL, NULL, "frodo"));
	NIH_MUST (nih_str_array_add (&args, NULL, NULL, "bilbo"));

	env = nih_str_array_new (NULL);
	NIH_MUST (nih_str_array_add (&env, NULL, NULL, "FOO=BAR"));

	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_EVENT_FINISHED, 0xdeafbeef,
					   TRUE, "test", args, env);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 67);
		TEST_EQ_MEM (msg->data->buf,
			     ("upstart\n\0\0\x02\x12u\xde\xaf\xbe\xef"
			      "i\0\0\0\x01s\0\0\0\x04test"
			      "as\0\0\0\05frodos\0\0\0\05bilboS"
			      "as\0\0\0\07FOO=BARS"), 67);

		nih_free (msg);
	}

	nih_free (args);
	nih_free (env);
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
	case UPSTART_JOB_UNKNOWN: {
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
		char *name;
		int   goal, state;

		name = va_arg (args, char *);
		goal = va_arg (args, int);
		state = va_arg (args, int);

		TEST_EQ_STR (name, "test");
		TEST_EQ (goal, JOB_START);
		TEST_EQ (state, JOB_RUNNING);

		nih_free (name);

		break;
	}
	case UPSTART_EVENT_EMIT: {
		char *name, **argv, **env;

		name = va_arg (args, char *);
		argv = va_arg (args, char **);
		env = va_arg (args, char **);

		TEST_EQ_STR (name, "test");

		TEST_ALLOC_SIZE (argv, sizeof (char *) * 3);
		TEST_ALLOC_PARENT (argv[0], argv);
		TEST_ALLOC_PARENT (argv[1], argv);
		TEST_EQ_STR (argv[0], "foo");
		TEST_EQ_STR (argv[1], "bar");
		TEST_EQ_P (argv[2], NULL);

		TEST_ALLOC_SIZE (env, sizeof (char *) * 2);
		TEST_ALLOC_PARENT (env[0], env);
		TEST_EQ_STR (env[0], "FOO=BAR");
		TEST_EQ_P (env[1], NULL);

		nih_free (name);
		nih_free (argv);
		nih_free (env);

		break;
	}
	case UPSTART_EVENT: {
		uint32_t  id;
		char *    name, **argv, **env;

		id = va_arg (args, unsigned);
		name = va_arg (args, char *);
		argv = va_arg (args, char **);
		env = va_arg (args, char **);

		TEST_EQ_U (id, 0xdeafbeef);
		TEST_EQ_STR (name, "test");

		TEST_ALLOC_SIZE (argv, sizeof (char *) * 3);
		TEST_ALLOC_PARENT (argv[0], argv);
		TEST_ALLOC_PARENT (argv[1], argv);
		TEST_EQ_STR (argv[0], "foo");
		TEST_EQ_STR (argv[1], "bar");
		TEST_EQ_P (argv[2], NULL);

		TEST_ALLOC_SIZE (env, sizeof (char *) * 2);
		TEST_ALLOC_PARENT (env[0], env);
		TEST_EQ_STR (env[0], "FOO=BAR");
		TEST_EQ_P (env[1], NULL);

		nih_free (name);
		nih_free (argv);
		nih_free (env);

		break;
	}
	case UPSTART_EVENT_JOB_STATUS: {
		uint32_t  id;
		char     *name;
		JobGoal   goal;
		JobState  state;

		id = va_arg (args, unsigned);
		name = va_arg (args, char *);
		goal = va_arg (args, int);
		state = va_arg (args, int);

		TEST_EQ_U (id, 0xdeafbeef);
		TEST_EQ_STR (name, "test");
		TEST_EQ (goal, JOB_START);
		TEST_EQ (state, JOB_RUNNING);

		nih_free (name);

		break;
	}
	case UPSTART_EVENT_FINISHED: {
		uint32_t  id;
		int       failed;
		char *    name, **argv, **env;

		id = va_arg (args, unsigned);
		failed = va_arg (args, int);
		name = va_arg (args, char *);
		argv = va_arg (args, char **);
		env = va_arg (args, char **);

		TEST_EQ_U (id, 0xdeafbeef);
		TEST_FALSE (failed);
		TEST_EQ_STR (name, "test");

		TEST_ALLOC_SIZE (argv, sizeof (char *) * 3);
		TEST_ALLOC_PARENT (argv[0], argv);
		TEST_ALLOC_PARENT (argv[1], argv);
		TEST_EQ_STR (argv[0], "foo");
		TEST_EQ_STR (argv[1], "bar");
		TEST_EQ_P (argv[2], NULL);

		TEST_ALLOC_SIZE (env, sizeof (char *) * 2);
		TEST_ALLOC_PARENT (env[0], env);
		TEST_EQ_STR (env[0], "FOO=BAR");
		TEST_EQ_P (env[1], NULL);

		nih_free (name);
		nih_free (argv);
		nih_free (env);

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
	NihIoMessage *msg = NULL;
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
						      "s\0\0\0\x4test"), 21));
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
						      "s\0\0\0\x4test"), 21));
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
						      "s\0\0\0\x4test"), 21));
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
						      "s\0\0\0\04test"
						      "i\0\0\0\01i\0\0\0\05"),
						     31));
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
						      "s\0\0\0\x4test"), 21));
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


	/* Check that we call the handler function for an UPSTART_EVENT_EMIT
	 * message.
	 */
	TEST_FEATURE ("with UPSTART_EVENT_EMIT message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (
					 msg->data,
					 ("upstart\n\0\0\x02\x00s\0\0\0\x4test"
					  "as\0\0\0\03foos\0\0\0\03barS"
					  "as\0\0\0\07FOO=BARS"), 57));
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
		TEST_EQ (last_type, UPSTART_EVENT_EMIT);

		nih_free (msg);
	}


	/* Check that we call the handler function for an UPSTART_EVENT
	 * message.
	 */
	TEST_FEATURE ("with UPSTART_EVENT message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (
					 msg->data,
					 ("upstart\n\0\0\x02\x10"
					  "u\xde\xaf\xbe\xefs\0\0\0\x4test"
					  "as\0\0\0\03foos\0\0\0\03barS"
					  "as\0\0\0\07FOO=BARS"), 62));
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


	/* Check that we call the handler function for an
	 * UPSTART_EVENT_JOB_STATUS message.
	 */
	TEST_FEATURE ("with UPSTART_EVENT_JOB_STATUS message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (msg->data,
						     ("upstart\n\0\0\x02\x11"
						      "u\xde\xaf\xbe\xef"
						      "s\0\0\0\04test"
						      "i\0\0\0\01i\0\0\0\05"),
						     36));
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
		TEST_EQ (last_type, UPSTART_EVENT_JOB_STATUS);

		nih_free (msg);
	}


	/* Check that we call the handler function for an
	 * UPSTART_EVENT_FINISHED message.
	 */
	TEST_FEATURE ("with UPSTART_EVENT_FINISHED message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (
					 msg->data,
					 ("upstart\n\0\0\x02\x12"
					  "u\xde\xaf\xbe\xefi\0\0\0\0"
					  "s\0\0\0\x4test"
					  "as\0\0\0\03foos\0\0\0\03barS"
					  "as\0\0\0\07FOO=BARS"), 67));
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
		TEST_EQ (last_type, UPSTART_EVENT_FINISHED);

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


	/* Check that the UPSTART_MESSAGE_INVALID error is raised if the
	 * message type is invalid.
	 */
	TEST_FEATURE ("with unknown message type");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     "upstart\n\xff\xff\xff\xff", 12));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;

	ret = upstart_message_handle (NULL, msg, any_handler, &ret);

	TEST_LT (ret, 0);
	TEST_FALSE (handler_called);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_MESSAGE_INVALID);

	nih_free (err);

	nih_free (msg);


	/* Check that the UPSTART_MESSAGE_INVALID error is raised if the
	 * name of a job to start is missing.
	 */
	TEST_FEATURE ("with incomplete job start message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data, "upstart\n\0\0\0\x1", 12));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;

	ret = upstart_message_handle (NULL, msg, any_handler, &ret);

	TEST_LT (ret, 0);
	TEST_FALSE (handler_called);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_MESSAGE_INVALID);

	nih_free (err);

	nih_free (msg);


	/* Check that the UPSTART_MESSAGE_INVALID error is raised if the
	 * name of a job to start is NULL.
	 */
	TEST_FEATURE ("with null job to start");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data, "upstart\n\0\0\0\x1S", 13));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;

	ret = upstart_message_handle (NULL, msg, any_handler, &ret);

	TEST_LT (ret, 0);
	TEST_FALSE (handler_called);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_MESSAGE_INVALID);

	nih_free (err);

	nih_free (msg);


	/* Check that the UPSTART_MESSAGE_INVALID error is raised if the
	 * name of a job to stop is missing.
	 */
	TEST_FEATURE ("with incomplete job stop message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data, "upstart\n\0\0\0\x2", 12));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;

	ret = upstart_message_handle (NULL, msg, any_handler, &ret);

	TEST_LT (ret, 0);
	TEST_FALSE (handler_called);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_MESSAGE_INVALID);

	nih_free (err);

	nih_free (msg);


	/* Check that the UPSTART_MESSAGE_INVALID error is raised if the
	 * name of a job to stop is NULL.
	 */
	TEST_FEATURE ("with null job to stop");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data, "upstart\n\0\0\0\x2S", 13));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;

	ret = upstart_message_handle (NULL, msg, any_handler, &ret);

	TEST_LT (ret, 0);
	TEST_FALSE (handler_called);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_MESSAGE_INVALID);

	nih_free (err);

	nih_free (msg);


	/* Check that the UPSTART_MESSAGE_INVALID error is raised if the
	 * name of a job to query is missing.
	 */
	TEST_FEATURE ("with incomplete job query message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data, "upstart\n\0\0\0\x3", 12));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;

	ret = upstart_message_handle (NULL, msg, any_handler, &ret);

	TEST_LT (ret, 0);
	TEST_FALSE (handler_called);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_MESSAGE_INVALID);

	nih_free (err);

	nih_free (msg);


	/* Check that the UPSTART_MESSAGE_INVALID error is raised if the
	 * name of a job to query is NULL.
	 */
	TEST_FEATURE ("with null job to query");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data, "upstart\n\0\0\0\x3S", 13));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;

	ret = upstart_message_handle (NULL, msg, any_handler, &ret);

	TEST_LT (ret, 0);
	TEST_FALSE (handler_called);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_MESSAGE_INVALID);

	nih_free (err);

	nih_free (msg);


	/* Check that the UPSTART_MESSAGE_INVALID error is raised if some
	 * fields of a job status message are missing.
	 */
	TEST_FEATURE ("with incomplete job status message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     ("upstart\n\0\0\0\x04s\0\0\0\x04test"
				      "i\0\0\0\x01"), 26));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;

	ret = upstart_message_handle (NULL, msg, any_handler, &ret);

	TEST_LT (ret, 0);
	TEST_FALSE (handler_called);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_MESSAGE_INVALID);

	nih_free (err);

	nih_free (msg);


	/* Check that the UPSTART_MESSAGE_INVALID error is raised if the
	 * name of a job status is NULL.
	 */
	TEST_FEATURE ("with null job status");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     ("upstart\n\0\0\0\x04Si\0\0\0\x01"
				      "i\0\0\0\x05"), 23));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;

	ret = upstart_message_handle (NULL, msg, any_handler, &ret);

	TEST_LT (ret, 0);
	TEST_FALSE (handler_called);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_MESSAGE_INVALID);

	nih_free (err);

	nih_free (msg);


	/* Check that the UPSTART_MESSAGE_INVALID error is raised if the
	 * name of an unknown job is missing.
	 */
	TEST_FEATURE ("with incomplete unknown job message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data, "upstart\n\0\0\0\x5", 12));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;

	ret = upstart_message_handle (NULL, msg, any_handler, &ret);

	TEST_LT (ret, 0);
	TEST_FALSE (handler_called);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_MESSAGE_INVALID);

	nih_free (err);

	nih_free (msg);


	/* Check that the UPSTART_MESSAGE_INVALID error is raised if the
	 * name of an unknown job is NULL.
	 */
	TEST_FEATURE ("with null unknown job");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data, "upstart\n\0\0\0\x5S", 13));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;

	ret = upstart_message_handle (NULL, msg, any_handler, &ret);

	TEST_LT (ret, 0);
	TEST_FALSE (handler_called);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_MESSAGE_INVALID);

	nih_free (err);

	nih_free (msg);


	/* Check that the UPSTART_MESSAGE_INVALID error is raised if the
	 * some of the fields of an event to emit is missing.
	 */
	TEST_FEATURE ("with incomplete event emit message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     "upstart\n\0\0\x02\x00s\0\0\0\04testA",
				     22));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;

	ret = upstart_message_handle (NULL, msg, any_handler, &ret);

	TEST_LT (ret, 0);
	TEST_FALSE (handler_called);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_MESSAGE_INVALID);

	nih_free (err);

	nih_free (msg);


	/* Check that the UPSTART_MESSAGE_INVALID error is raised if the
	 * name of an event to emit is NULL.
	 */
	TEST_FEATURE ("with null event to emit");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data, "upstart\n\0\0\x02\x00SAA",
				     15));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;

	ret = upstart_message_handle (NULL, msg, any_handler, &ret);

	TEST_LT (ret, 0);
	TEST_FALSE (handler_called);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_MESSAGE_INVALID);

	nih_free (err);

	nih_free (msg);


	/* Check that the UPSTART_MESSAGE_INVALID error is raised if some of
	 * the fields of an event emitted are missing.
	 */
	TEST_FEATURE ("with incomplete event message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     ("upstart\n\0\0\x02\x10u\xde\xaf\xbe\xef"
				      "s\0\0\0\04testA"), 27));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;

	ret = upstart_message_handle (NULL, msg, any_handler, &ret);

	TEST_LT (ret, 0);
	TEST_FALSE (handler_called);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_MESSAGE_INVALID);

	nih_free (err);

	nih_free (msg);


	/* Check that the UPSTART_MESSAGE_INVALID error is raised if the
	 * name of an event emitted is NULL.
	 */
	TEST_FEATURE ("with null event emitted");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     ("upstart\n\0\0\x02\x10u\xde\xaf\xbe\xef"
				      "SAA"), 20));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;

	ret = upstart_message_handle (NULL, msg, any_handler, &ret);

	TEST_LT (ret, 0);
	TEST_FALSE (handler_called);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_MESSAGE_INVALID);

	nih_free (err);

	nih_free (msg);


	/* Check that the UPSTART_MESSAGE_INVALID error is raised if some
	 * fields of an event job status message are missing.
	 */
	TEST_FEATURE ("with incomplete event job status message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     ("upstart\n\0\0\02\x11u\xde\xaf\xbe\xef"
				      "s\0\0\0\x04testi\0\0\0\x01"), 31));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;

	ret = upstart_message_handle (NULL, msg, any_handler, &ret);

	TEST_LT (ret, 0);
	TEST_FALSE (handler_called);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_MESSAGE_INVALID);

	nih_free (err);

	nih_free (msg);


	/* Check that the UPSTART_MESSAGE_INVALID error is raised if the
	 * name of a job status is NULL.
	 */
	TEST_FEATURE ("with null job status");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     ("upstart\n\0\0\02\x11u\xde\xaf\xbe\xef"
				      "Si\0\0\0\x01i\0\0\0\x02"), 28));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;

	ret = upstart_message_handle (NULL, msg, any_handler, &ret);

	TEST_LT (ret, 0);
	TEST_FALSE (handler_called);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_MESSAGE_INVALID);

	nih_free (err);

	nih_free (msg);


	/* Check that the UPSTART_MESSAGE_INVALID error is raised if some of
	 * the fields of an event finished are missing.
	 */
	TEST_FEATURE ("with incomplete finished event message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     ("upstart\n\0\0\x02\x10u\xde\xaf\xbe\xef"
				      "i\0\0\0\0s\0\0\0\04testA"), 32));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;

	ret = upstart_message_handle (NULL, msg, any_handler, &ret);

	TEST_LT (ret, 0);
	TEST_FALSE (handler_called);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_MESSAGE_INVALID);

	nih_free (err);

	nih_free (msg);


	/* Check that the UPSTART_MESSAGE_INVALID error is raised if the
	 * name of an event finished is NULL.
	 */
	TEST_FEATURE ("with null event emitted");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     ("upstart\n\0\0\x02\x12u\xde\xaf\xbe\xef"
				      "i\0\0\0;5C\0SAA"), 20));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;

	ret = upstart_message_handle (NULL, msg, any_handler, &ret);

	TEST_LT (ret, 0);
	TEST_FALSE (handler_called);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_MESSAGE_INVALID);

	nih_free (err);

	nih_free (msg);


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
				     "upstart\n\0\0\0\1s\0\0\0\x4test", 21));
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
				     "upstart\n\0\0\0\1s\0\0\0\x4test", 21));
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
	NihIoMessage *msg = NULL;
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
						      "s\0\0\0\x4test"), 21));
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


static int logger_called = 0;

static int
my_logger (NihLogLevel  priority,
	   const char  *message)
{
	logger_called++;

	return 0;
}

void
test_reader (void)
{
	NihIo        *io = NULL;
	NihIoMessage *msg = NULL;
	struct ucred  cred = { 2000, 1000, 1000 };

	TEST_FUNCTION ("upstart_message_reader");


	/* The message reader function should take the first message from
	 * the queue, handle it, and then free the message also causing any
	 * strings to be freed.
	 */
	TEST_FEATURE ("with valid message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			io = nih_io_reopen (NULL, 0, NIH_IO_MESSAGE,
					    (NihIoReader)upstart_message_reader,
					    NULL, NULL, any_handler);

			msg = nih_io_message_new (io);
			assert0 (nih_io_buffer_push (msg->data,
						     ("upstart\n\0\0\0\1"
						      "s\0\0\0\x4test"), 21));
			assert0 (nih_io_message_add_control (msg, SOL_SOCKET,
							     SCM_CREDENTIALS,
							     sizeof (cred),
							     &cred));
		}

		destructor_called = 0;
		nih_alloc_set_destructor (msg, my_destructor);

		nih_list_add (io->recv_q, &msg->entry);

		handler_called = FALSE;
		last_data = NULL;
		last_pid = -1;
		last_type = -1;

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


	/* Check that if we fail to handle a message, a warning is emitted
	 * and it's otherwise ignored.
	 */
	TEST_FEATURE ("with invalid message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			io = nih_io_reopen (NULL, 0, NIH_IO_MESSAGE,
					    (NihIoReader)upstart_message_reader,
					    NULL, NULL, any_handler);

			msg = nih_io_message_new (io);
			assert0 (nih_io_buffer_push (msg->data,
						     "upstart\n\0\0\0\1S",
						     13));
			assert0 (nih_io_message_add_control (msg, SOL_SOCKET,
							     SCM_CREDENTIALS,
							     sizeof (cred),
							     &cred));
		}

		destructor_called = 0;
		nih_alloc_set_destructor (msg, my_destructor);

		nih_list_add (io->recv_q, &msg->entry);

		handler_called = FALSE;
		last_data = NULL;
		last_pid = -1;
		last_type = -1;

		logger_called = 0;
		nih_log_set_logger (my_logger);

		upstart_disable_safeties = TRUE;

		upstart_message_reader (any_handler, io,
					msg->data->buf, msg->data->len);

		upstart_disable_safeties = FALSE;

		nih_log_set_logger (nih_logger_printf);

		TEST_FALSE (handler_called);
		TEST_TRUE (logger_called);
		TEST_EQ (destructor_called, 1);
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
