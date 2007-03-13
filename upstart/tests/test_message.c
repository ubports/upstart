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
		TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\x00\x00", 12);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_VERSION_QUERY message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_VERSION_QUERY message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_VERSION_QUERY);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 12);
		TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\x00\x01", 12);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_LOG_PRIORITY message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_LOG_PRIORITY message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_LOG_PRIORITY,
					   NIH_LOG_DEBUG);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 17);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\x00\x02u\0\0\0\x1", 17);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_VERSION message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_VERSION message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_VERSION, "upstart 0.5.0");

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 30);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\x00\x10s\0\0\0\xdupstart 0.5.0",
			     30);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_FIND message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_FIND message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_FIND, "test*");

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 22);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\x01\x00s\0\0\0\x05test*", 22);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_FIND message with a NULL
	 * pattern and have the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_FIND message and NULL pattern");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_FIND, NULL);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 13);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\x01\x00S", 13);
		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_QUERY message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_QUERY message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_QUERY, "test", 0);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 26);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\x01\x01s\0\0\0\x04testu\0\0\0\0",
			     26);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_QUERY message with an
	 * id instead of name and have the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_QUERY message and id not name");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_QUERY,
					   NULL, 0xdeafbeef);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 18);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\x01\x01Su\xde\xaf\xbe\xef",
			     18);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_START message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_START message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_START, "test", 0);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 26);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\x01\x02s\0\0\0\x04testu\0\0\0\0",
			     26);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_START message with an
	 * id instead of name and have the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_START message and id not name");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_START,
					   NULL, 0xdeafbeef);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 18);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\x01\x02Su\xde\xaf\xbe\xef",
			     18);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_STOP message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_STOP message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_STOP, "test", 0);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 26);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\x01\x03s\0\0\0\x04testu\0\0\0\0",
			     26);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_STOP message with an
	 * id instead of name and have the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_STOP message and id not name");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_STOP, NULL,
					   0xdeafbeef);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 18);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\x01\x03Su\xde\xaf\xbe\xef", 18);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB, 0xdeafbeef, "test");

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 26);
		TEST_EQ_MEM (msg->data->buf,
			     ("upstart\n\0\0\x01\x10u\xde\xaf\xbe\xef"
			      "s\0\0\0\x04test"), 26);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_FINISHED message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_FINISHED message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_FINISHED,
					   0xdeafbeef, "test", TRUE,
					   PROCESS_MAIN, 1);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 41);
		TEST_EQ_MEM (msg->data->buf,
			     ("upstart\n\0\0\x01\x1fu\xde\xaf\xbe\xef"
			      "s\0\0\0\x04testi\0\0\0\1u\0\0\0\0i\0\0\0\1"),
			     41);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_LIST message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_LIST message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_LIST, "test*");

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 22);
		TEST_EQ_MEM (msg->data->buf,
			     ("upstart\n\0\0\x01\x20s\0\0\0\x05test*"), 22);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_LIST_END message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_LIST_END message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_LIST_END, "test*");

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 22);
		TEST_EQ_MEM (msg->data->buf,
			     ("upstart\n\0\0\x01\x2fs\0\0\0\x05test*"), 22);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_INSTANCE message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_INSTANCE message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_INSTANCE,
					   0xdeafbeef, "test");

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 26);
		TEST_EQ_MEM (msg->data->buf,
			     ("upstart\n\0\0\x01\x30u\xde\xaf\xbe\xef"
			      "s\0\0\0\x04test"), 26);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_INSTANCE_END message and
	 * have the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_INSTANCE_END message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_INSTANCE_END,
					   0xdeafbeef, "test");

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 26);
		TEST_EQ_MEM (msg->data->buf,
			     ("upstart\n\0\0\x01\x3fu\xde\xaf\xbe\xef"
			      "s\0\0\0\x04test"), 26);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_STATUS message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_STATUS message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_STATUS, 0xdeafbeef,
					   "test", JOB_START, JOB_RUNNING);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 36);
		TEST_EQ_MEM (msg->data->buf,
			     ("upstart\n\0\0\x01\x80u\xde\xaf\xbe\xef"
			      "s\0\0\0\x04testu\0\0\0\1u\0\0\0\5"), 36);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_PROCESS message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_PROCESS message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_PROCESS,
					   PROCESS_MAIN, 1000);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 22);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\x01\x81u\0\0\0\0i\0\0\x3\xe8",
			     22);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_STATUS_END message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_STATUS_END message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_STATUS_END, 0xdeafbeef,
					   "test", JOB_START, JOB_RUNNING);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 36);
		TEST_EQ_MEM (msg->data->buf,
			     ("upstart\n\0\0\x01\x8fu\xde\xaf\xbe\xef"
			      "s\0\0\0\x04testu\0\0\0\1u\0\0\0\5"), 36);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_UNKNOWN message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_UNKNOWN message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_UNKNOWN, "test", 0);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 26);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\x01\xf0s\0\0\0\x04testu\0\0\0\0",
			     26);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_INVALID message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_INVALID message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_INVALID,
					   0xdeafbeef, "test");

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 26);
		TEST_EQ_MEM (msg->data->buf,
			     ("upstart\n\0\0\x01\xf1u\xde\xaf\xbe\xef"
			      "s\0\0\0\x04test"), 26);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_JOB_UNCHANGED message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_UNCHANGED message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_JOB_UNCHANGED,
					   0xdeafbeef, "test");

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 26);
		TEST_EQ_MEM (msg->data->buf,
			     ("upstart\n\0\0\x01\xf2u\xde\xaf\xbe\xef"
			      "s\0\0\0\x04test"), 26);

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


	/* Check that we can create an UPSTART_EVENT_CAUSED message
	 * and have the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_EVENT_CAUSED message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_EVENT_CAUSED, 0xdeafbeef);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 17);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\x02\x11u\xde\xaf\xbe\xef", 17);

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
			     ("upstart\n\0\0\x02\x1fu\xde\xaf\xbe\xef"
			      "i\0\0\0\x01s\0\0\0\x04test"
			      "as\0\0\0\05frodos\0\0\0\05bilboS"
			      "as\0\0\0\07FOO=BARS"), 67);

		nih_free (msg);
	}

	nih_free (args);
	nih_free (env);


	/* Check that we can create an UPSTART_SUBSCRIBE_JOBS message and have
	 * the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_SUBSCRIBE_JOBS message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_SUBSCRIBE_JOBS);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 12);
		TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\x10\x00", 12);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_UNSUBSCRIBE_JOBS message and
	 * have the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_UNSUBSCRIBE_JOBS message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_UNSUBSCRIBE_JOBS);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 12);
		TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\x10\x0f", 12);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_SUBSCRIBE_EVENTS message and
	 * have the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_SUBSCRIBE_EVENTS message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_SUBSCRIBE_EVENTS);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 12);
		TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\x10\x10", 12);

		nih_free (msg);
	}


	/* Check that we can create an UPSTART_UNSUBSCRIBE_EVENTS message and
	 * have the message buffer filled in correctly.
	 */
	TEST_FEATURE ("with UPSTART_UNSUBSCRIBE_EVENTS message");
	TEST_ALLOC_FAIL {
		msg = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
					   UPSTART_UNSUBSCRIBE_EVENTS);

		if (test_alloc_failed) {
			TEST_EQ_P (msg, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (msg, sizeof (NihIoMessage));

		TEST_EQ (msg->data->len, 12);
		TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\x10\x1f", 12);

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
	    pid_t                src_pid,
	    UpstartMessageType   type,
	    ...)
{
	va_list args;

	handler_called++;
	last_data = data;
	last_pid = src_pid;
	last_type = type;

	if (src_pid == 999)
		return 100;

	va_start (args, type);

	switch (type) {
	case UPSTART_NO_OP:
		break;
	case UPSTART_VERSION_QUERY:
		break;
	case UPSTART_LOG_PRIORITY: {
		NihLogLevel priority;

		priority = va_arg (args, unsigned int);

		TEST_EQ (priority, NIH_LOG_DEBUG);

		break;
	}
	case UPSTART_VERSION: {
		char *version;

		version = va_arg (args, char *);

		TEST_EQ_STR (version, "upstart 0.5.0");

		nih_free (version);

		break;
	}

	case UPSTART_JOB_FIND: {
		char *pattern;

		pattern = va_arg (args, char *);

		TEST_EQ_STR (pattern, "test*");

		nih_free (pattern);

		break;
	}
	case UPSTART_JOB_QUERY: {
		char         *name;
		unsigned int  id;

		name = va_arg (args, char *);
		id = va_arg (args, unsigned int);

		TEST_EQ_STR (name, "test");
		TEST_EQ_U (id, 0);

		nih_free (name);

		break;
	}
	case UPSTART_JOB_START: {
		char         *name;
		unsigned int  id;

		name = va_arg (args, char *);
		id = va_arg (args, unsigned int);

		TEST_EQ_STR (name, "test");
		TEST_EQ_U (id, 0);

		nih_free (name);

		break;
	}
	case UPSTART_JOB_STOP: {
		char         *name;
		unsigned int  id;

		name = va_arg (args, char *);
		id = va_arg (args, unsigned int);

		TEST_EQ_P (name, NULL);
		TEST_EQ_U (id, 0xdeafbeef);

		break;
	}
	case UPSTART_JOB: {
		unsigned int  id;
		char         *name;

		id = va_arg (args, unsigned);
		name = va_arg (args, char *);

		TEST_EQ_U (id, 0xdeafbeef);
		TEST_EQ_STR (name, "test");

		nih_free (name);

		break;
	}
	case UPSTART_JOB_FINISHED: {
		unsigned int  id;
		char         *name;
		int           failed;
		ProcessType   failed_process;
		int           exit_status;

		id = va_arg (args, unsigned int);
		name = va_arg (args, char *);
		failed = va_arg (args, int);
		failed_process = va_arg (args, unsigned int);
		exit_status = va_arg (args, int);

		TEST_EQ_U (id, 0xdeafbeef);
		TEST_EQ_STR (name, "test");
		TEST_EQ (failed, TRUE);
		TEST_EQ (failed_process, PROCESS_MAIN);
		TEST_EQ (exit_status, 1);

		nih_free (name);

		break;
	}
	case UPSTART_JOB_LIST: {
		char     *pattern;

		pattern = va_arg (args, char *);

		TEST_EQ_STR (pattern, "test*");

		nih_free (pattern);

		break;
	}
	case UPSTART_JOB_LIST_END: {
		char     *pattern;

		pattern = va_arg (args, char *);

		TEST_EQ_STR (pattern, "test*");

		nih_free (pattern);

		break;
	}
	case UPSTART_JOB_INSTANCE: {
		unsigned int  id;
		char         *name;

		id = va_arg (args, unsigned int);
		name = va_arg (args, char *);

		TEST_EQ_U (id, 0xdeafbeef);
		TEST_EQ_STR (name, "test");

		nih_free (name);

		break;
	}
	case UPSTART_JOB_INSTANCE_END: {
		unsigned int  id;
		char         *name;

		id = va_arg (args, unsigned int);
		name = va_arg (args, char *);

		TEST_EQ_U (id, 0xdeafbeef);
		TEST_EQ_STR (name, "test");

		nih_free (name);

		break;
	}
	case UPSTART_JOB_STATUS: {
		unsigned int  id;
		char         *name;
		JobGoal       goal;
		JobState      state;

		id = va_arg (args, unsigned int);
		name = va_arg (args, char *);
		goal = va_arg (args, unsigned int);
		state = va_arg (args, unsigned int);

		TEST_EQ_U (id, 0xdeafbeef);
		TEST_EQ_STR (name, "test");
		TEST_EQ (goal, JOB_START);
		TEST_EQ (state, JOB_RUNNING);

		nih_free (name);

		break;
	}
	case UPSTART_JOB_PROCESS: {
		ProcessType process;
		pid_t       pid;

		process = va_arg (args, unsigned);
		pid = va_arg (args, int);

		TEST_EQ (process, PROCESS_MAIN);
		TEST_EQ (pid, 1000);

		break;
	}
	case UPSTART_JOB_STATUS_END: {
		unsigned int  id;
		char         *name;
		JobGoal       goal;
		JobState      state;

		id = va_arg (args, unsigned int);
		name = va_arg (args, char *);
		goal = va_arg (args, unsigned int);
		state = va_arg (args, unsigned int);

		TEST_EQ_U (id, 0xdeafbeef);
		TEST_EQ_STR (name, "test");
		TEST_EQ (goal, JOB_START);
		TEST_EQ (state, JOB_RUNNING);

		nih_free (name);

		break;
	}
	case UPSTART_JOB_UNKNOWN: {
		char         *name;
		unsigned int  id;

		name = va_arg (args, char *);
		id = va_arg (args, unsigned int);

		TEST_EQ_STR (name, "test");
		TEST_EQ_U (id, 0);

		nih_free (name);

		break;
	}
	case UPSTART_JOB_INVALID: {
		unsigned int  id;
		char         *name;

		id = va_arg (args, unsigned int);
		name = va_arg (args, char *);

		TEST_EQ_U (id, 0xdeafbeef);
		TEST_EQ_STR (name, "test");

		nih_free (name);

		break;
	}
	case UPSTART_JOB_UNCHANGED: {
		unsigned int  id;
		char         *name;

		id = va_arg (args, unsigned int);
		name = va_arg (args, char *);

		TEST_EQ_U (id, 0xdeafbeef);
		TEST_EQ_STR (name, "test");

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
		unsigned int  id;
		char         *name, **argv, **env;

		id = va_arg (args, unsigned int);
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
	case UPSTART_EVENT_CAUSED: {
		unsigned int id;

		id = va_arg (args, unsigned int);

		TEST_EQ_U (id, 0xdeafbeef);

		break;
	}
	case UPSTART_EVENT_FINISHED: {
		unsigned int  id;
		int           failed;
		char         *name, **argv, **env;

		id = va_arg (args, unsigned int);
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
	case UPSTART_SUBSCRIBE_JOBS:
		break;
	case UPSTART_UNSUBSCRIBE_JOBS:
		break;
	case UPSTART_SUBSCRIBE_EVENTS:
		break;
	case UPSTART_UNSUBSCRIBE_EVENTS:
		break;
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
						     "upstart\n\0\0\x00\x00",
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
		TEST_EQ (last_type, UPSTART_NO_OP);

		nih_free (msg);
	}


	/* Check that we call the handler function for an UPSTART_VERSION_QUERY
	 * message from a particular process id.
	 */
	TEST_FEATURE ("with UPSTART_VERSION_QUERY message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (msg->data,
						     "upstart\n\0\0\x00\x01",
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
		TEST_EQ (last_type, UPSTART_VERSION_QUERY);

		nih_free (msg);
	}


	/* Check that we call the handler function for an UPSTART_LOG_PRIORITY
	 * message from a particular process id.
	 */
	TEST_FEATURE ("with UPSTART_VERSION_QUERY message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (msg->data,
						     ("upstart\n\0\0\x00\x02"
						      "u\0\0\0\1"), 17));
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
		TEST_EQ (last_type, UPSTART_LOG_PRIORITY);

		nih_free (msg);
	}


	/* Check that we call the handler function for an UPSTART_VERSION
	 * message from a particular process id.
	 */
	TEST_FEATURE ("with UPSTART_VERSION message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (msg->data,
						     ("upstart\n\0\0\x00\x10"
						      "s\0\0\0\xd"
						      "upstart 0.5.0"), 30));
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
		TEST_EQ (last_type, UPSTART_VERSION);

		nih_free (msg);
	}


	/* Check that we call the handler function for an UPSTART_JOB_FIND
	 * message from a particular process id.
	 */
	TEST_FEATURE ("with UPSTART_JOB_FIND message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (
					 msg->data,
					 ("upstart\n\0\0\x01\x00"
					  "s\0\0\0\5test*"),
					 22));
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
		TEST_EQ (last_type, UPSTART_JOB_FIND);

		nih_free (msg);
	}


	/* Check that we call the handler function for an UPSTART_JOB_QUERY
	 * message from a particular process id.
	 */
	TEST_FEATURE ("with UPSTART_JOB_QUERY message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (
					 msg->data,
					 ("upstart\n\0\0\x01\x01"
					  "s\0\0\0\4testu\0\0\0\0"),
					 26));
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


	/* Check that we call the handler function for an UPSTART_JOB_START
	 * message from a particular process id.
	 */
	TEST_FEATURE ("with UPSTART_JOB_START message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (
					 msg->data,
					 ("upstart\n\0\0\x01\x02"
					  "s\0\0\0\4testu\0\0\0\0"),
					 26));
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
	 * message from a particular process id.
	 */
	TEST_FEATURE ("with UPSTART_JOB_STOP message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (
					 msg->data,
					 ("upstart\n\0\0\x01\x03"
					  "Su\xde\xaf\xbe\xef"),
					 18));
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


	/* Check that we call the handler function for an UPSTART_JOB
	 * message from a particular process id.
	 */
	TEST_FEATURE ("with UPSTART_JOB message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (
					 msg->data,
					 ("upstart\n\0\0\x01\x10"
					  "u\xde\xaf\xbe\xefs\0\0\0\4test"),
					 26));
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
		TEST_EQ (last_type, UPSTART_JOB);

		nih_free (msg);
	}


	/* Check that we call the handler function for an UPSTART_JOB_FINISHED
	 * message from a particular process id.
	 */
	TEST_FEATURE ("with UPSTART_JOB_FINISHED message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (
					 msg->data,
					 ("upstart\n\0\0\x01\x1f"
					  "u\xde\xaf\xbe\xefs\0\0\0\4test"
					  "i\0\0\0\1u\0\0\0\0i\0\0\0\1"), 41));
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
		TEST_EQ (last_type, UPSTART_JOB_FINISHED);

		nih_free (msg);
	}


	/* Check that we call the handler function for an UPSTART_JOB_LIST
	 * message from a particular process id.
	 */
	TEST_FEATURE ("with UPSTART_JOB_LIST message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (
					 msg->data,
					 ("upstart\n\0\0\x01\x20"
					  "s\0\0\0\5test*"),
					 22));
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
	 * message from a particular process id.
	 */
	TEST_FEATURE ("with UPSTART_JOB_LIST_END message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (
					 msg->data,
					 ("upstart\n\0\0\x01\x2f"
					  "s\0\0\0\5test*"),
					 22));
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


	/* Check that we call the handler function for an UPSTART_JOB_INSTANCE
	 * message from a particular process id.
	 */
	TEST_FEATURE ("with UPSTART_JOB_INSTANCE message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (
					 msg->data,
					 ("upstart\n\0\0\x01\x30"
					  "u\xde\xaf\xbe\xefs\0\0\0\4test"),
					 26));
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
		TEST_EQ (last_type, UPSTART_JOB_INSTANCE);

		nih_free (msg);
	}


	/* Check that we call the handler function for an
	 * UPSTART_JOB_INSTANCE_END message from a particular process id.
	 */
	TEST_FEATURE ("with UPSTART_JOB_INSTANCE_END message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (
					 msg->data,
					 ("upstart\n\0\0\x01\x3f"
					  "u\xde\xaf\xbe\xefs\0\0\0\4test"),
					 26));
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
		TEST_EQ (last_type, UPSTART_JOB_INSTANCE_END);

		nih_free (msg);
	}


	/* Check that we call the handler function for an UPSTART_JOB_STATUS
	 * message from a particular process id.
	 */
	TEST_FEATURE ("with UPSTART_JOB_STATUS message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (
					 msg->data,
					 ("upstart\n\0\0\x01\x80"
					  "u\xde\xaf\xbe\xefs\0\0\0\4test"
					  "u\0\0\0\1u\0\0\0\05"), 36));
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


	/* Check that we call the handler function for an UPSTART_JOB_PROCESS
	 * message from a particular process id.
	 */
	TEST_FEATURE ("with UPSTART_JOB_PROCESS message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (
					 msg->data,
					 ("upstart\n\0\0\x01\x81"
					  "u\0\0\0\0i\0\0\x3\xe8"), 22));
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
		TEST_EQ (last_type, UPSTART_JOB_PROCESS);

		nih_free (msg);
	}


	/* Check that we call the handler function for an
	 * UPSTART_JOB_STATUS_END message from a particular process id.
	 */
	TEST_FEATURE ("with UPSTART_JOB_STATUS_END message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (
					 msg->data,
					 ("upstart\n\0\0\x01\x8f"
					  "u\xde\xaf\xbe\xefs\0\0\0\4test"
					  "u\0\0\0\1u\0\0\0\05"), 36));
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
		TEST_EQ (last_type, UPSTART_JOB_STATUS_END);

		nih_free (msg);
	}


	/* Check that we call the handler function for an UPSTART_JOB_UNKNOWN
	 * message from a particular process id.
	 */
	TEST_FEATURE ("with UPSTART_JOB_UNKNOWN message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (
					 msg->data,
					 ("upstart\n\0\0\x01\xf0"
					  "s\0\0\0\4testu\0\0\0\0"),
					 26));
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


	/* Check that we call the handler function for an UPSTART_JOB_INVALID
	 * message from a particular process id.
	 */
	TEST_FEATURE ("with UPSTART_JOB_INVALID message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (
					 msg->data,
					 ("upstart\n\0\0\x01\xf1"
					  "u\xde\xaf\xbe\xefs\0\0\0\4test"),
					 26));
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
		TEST_EQ (last_type, UPSTART_JOB_INVALID);

		nih_free (msg);
	}


	/* Check that we call the handler function for an UPSTART_JOB_UNCHANGED
	 * message from a particular process id.
	 */
	TEST_FEATURE ("with UPSTART_JOB_UNCHANGED message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (
					 msg->data,
					 ("upstart\n\0\0\x01\xf2"
					  "u\xde\xaf\xbe\xefs\0\0\0\4test"),
					 26));
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
		TEST_EQ (last_type, UPSTART_JOB_UNCHANGED);

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
	 * UPSTART_EVENT_CAUSED message.
	 */
	TEST_FEATURE ("with UPSTART_EVENT_CAUSED message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (msg->data,
						     ("upstart\n\0\0\x02\x11"
						      "u\xde\xaf\xbe\xef"),
						     17));
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
		TEST_EQ (last_type, UPSTART_EVENT_CAUSED);

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
					 ("upstart\n\0\0\x02\x1f"
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


	/* Check that we call the handler function for an
	 * UPSTART_SUBSCRIBE_JOBS message.
	 */
	TEST_FEATURE ("with UPSTART_SUBSCRIBE_JOBS message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (msg->data,
						     "upstart\n\0\0\x10\x00",
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
		TEST_EQ (last_type, UPSTART_SUBSCRIBE_JOBS);

		nih_free (msg);
	}


	/* Check that we call the handler function for an
	 * UPSTART_UNSUBSCRIBE_JOBS message.
	 */
	TEST_FEATURE ("with UPSTART_UNSUBSCRIBE_JOBS message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (msg->data,
						     "upstart\n\0\0\x10\x0f",
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
		TEST_EQ (last_type, UPSTART_UNSUBSCRIBE_JOBS);

		nih_free (msg);
	}


	/* Check that we call the handler function for an
	 * UPSTART_SUBSCRIBE_EVENTS message.
	 */
	TEST_FEATURE ("with UPSTART_SUBSCRIBE_EVENTS message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (msg->data,
						     "upstart\n\0\0\x10\x10",
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
		TEST_EQ (last_type, UPSTART_SUBSCRIBE_EVENTS);

		nih_free (msg);
	}


	/* Check that we call the handler function for an
	 * UPSTART_UNSUBSCRIBE_EVENTS message.
	 */
	TEST_FEATURE ("with UPSTART_UNSUBSCRIBE_EVENTS message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			msg = nih_io_message_new (NULL);
			assert0 (nih_io_buffer_push (msg->data,
						     "upstart\n\0\0\x10\x1f",
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
		TEST_EQ (last_type, UPSTART_UNSUBSCRIBE_EVENTS);

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

	ret = upstart_message_handle (NULL, msg, any_handler, &ret);

	TEST_LT (ret, 0);
	TEST_FALSE (handler_called);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_MESSAGE_INVALID);

	nih_free (err);

	nih_free (msg);


	/* Check that the UPSTART_MESSAGE_UNKNOWN error is raised if the
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

	TEST_EQ (err->number, UPSTART_MESSAGE_UNKNOWN);

	nih_free (err);

	nih_free (msg);


	/* Check that the UPSTART_MESSAGE_INVALID error is raised on an
	 * incomplete UPSTART_VERSION message.
	 */
	TEST_FEATURE ("with incomplete UPSTART_VERSION message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data, "upstart\n\0\0\x00\x10", 12));
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
	 * name field in a UPSTART_VERSION message is NULL.
	 */
	TEST_FEATURE ("with null name in UPSTART_VERSION message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data, "upstart\n\0\0\x00\x10S", 13));
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


	/* Check that the UPSTART_MESSAGE_INVALID error is raised on an
	 * incomplete UPSTART_JOB_FIND message.
	 */
	TEST_FEATURE ("with incomplete UPSTART_JOB_FIND message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data, "upstart\n\0\0\x01\x00", 12));
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


	/* Check that the UPSTART_MESSAGE_INVALID error is raised on an
	 * incomplete UPSTART_JOB_QUERY message.
	 */
	TEST_FEATURE ("with incomplete UPSTART_JOB_QUERY message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data, "upstart\n\0\0\x01\x01S", 13));
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


	/* Check that the UPSTART_MESSAGE_INVALID error is raised on an
	 * incomplete UPSTART_JOB_START message.
	 */
	TEST_FEATURE ("with incomplete UPSTART_JOB_START message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data, "upstart\n\0\0\x01\x02S", 13));
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


	/* Check that the UPSTART_MESSAGE_INVALID error is raised on an
	 * incomplete UPSTART_JOB_STOP message.
	 */
	TEST_FEATURE ("with incomplete UPSTART_JOB_STOP message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data, "upstart\n\0\0\x01\x03S", 13));
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


	/* Check that the UPSTART_MESSAGE_INVALID error is raised on an
	 * incomplete UPSTART_JOB message.
	 */
	TEST_FEATURE ("with incomplete UPSTART_JOB message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     "upstart\n\0\0\x01\x10u\0\0\0\0", 17));
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
	 * name field in a UPSTART_JOB message is NULL.
	 */
	TEST_FEATURE ("with null name in UPSTART_JOB message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     "upstart\n\0\0\x01\x10u\0\0\0\0S", 18));
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


	/* Check that the UPSTART_MESSAGE_INVALID error is raised on an
	 * incomplete UPSTART_JOB_FINISHED message.
	 */
	TEST_FEATURE ("with incomplete UPSTART_JOB_FINISHED message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     ("upstart\n\0\0\x01\x1fu\0\0\0\0"
				      "s\0\0\04testi\0\0\0\1u\0\0\0\0"),
				     36));
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
	 * name field in a UPSTART_JOB_FINISHED message is NULL.
	 */
	TEST_FEATURE ("with null name in UPSTART_JOB_FINISHED message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     ("upstart\n\0\0\x01\x1fu\0\0\0\0S"
				      "i\0\0\0\1u\0\0\0\0i\0\0\0\1"),
				     33));
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


	/* Check that the UPSTART_MESSAGE_INVALID error is raised on an
	 * incomplete UPSTART_JOB_LIST message.
	 */
	TEST_FEATURE ("with incomplete UPSTART_JOB_LIST message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data, "upstart\n\0\0\x01\x20", 12));
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


	/* Check that the UPSTART_MESSAGE_INVALID error is raised on an
	 * incomplete UPSTART_JOB_LIST_END message.
	 */
	TEST_FEATURE ("with incomplete UPSTART_JOB_LIST_END message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data, "upstart\n\0\0\x01\x2f", 12));
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


	/* Check that the UPSTART_MESSAGE_INVALID error is raised on an
	 * incomplete UPSTART_JOB_STATUS message.
	 */
	TEST_FEATURE ("with incomplete UPSTART_JOB_STATUS message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     ("upstart\n\0\0\x01\x80u\xde\xaf\xbe\xef"
				      "s\0\0\0\4testu\0\0\0\0"), 31));
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


	/* Check that the UPSTART_MESSAGE_INVALID error is raised on an
	 * incomplete UPSTART_JOB_INSTANCE message.
	 */
	TEST_FEATURE ("with incomplete UPSTART_JOB_INSTANCE message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     "upstart\n\0\0\x01\x30u\0\0\0\0", 17));
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
	 * name field in a UPSTART_JOB_INSTANCE message is NULL.
	 */
	TEST_FEATURE ("with null name in UPSTART_JOB_INSTANCE message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     "upstart\n\0\0\x01\x30u\0\0\0\0S", 18));
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


	/* Check that the UPSTART_MESSAGE_INVALID error is raised on an
	 * incomplete UPSTART_JOB_INSTANCE_END message.
	 */
	TEST_FEATURE ("with incomplete UPSTART_JOB_INSTANCE_END message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     "upstart\n\0\0\x01\x3fu\0\0\0\0", 17));
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
	 * name field in a UPSTART_JOB_INSTANCE_END message is NULL.
	 */
	TEST_FEATURE ("with null name in UPSTART_JOB_INSTANCE_END message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     "upstart\n\0\0\x01\x3fu\0\0\0\0S", 18));
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
	 * name field in a UPSTART_JOB_STATUS message is NULL.
	 */
	TEST_FEATURE ("with null name in UPSTART_JOB_STATUS message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     ("upstart\n\0\0\x01\x80u\xde\xaf\xbe\xef"
				      "Su\0\0\0\0u\0\0\0\0"), 28));
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


	/* Check that the UPSTART_MESSAGE_INVALID error is raised on an
	 * incomplete UPSTART_JOB_PROCESS message.
	 */
	TEST_FEATURE ("with incomplete UPSTART_JOB_PROCESS message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     "upstart\n\0\0\x01\x81u\0\0\0\0", 17));
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


	/* Check that the UPSTART_MESSAGE_INVALID error is raised on an
	 * incomplete UPSTART_JOB_STATUS_END message.
	 */
	TEST_FEATURE ("with incomplete UPSTART_JOB_STATUS_END message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     ("upstart\n\0\0\x01\x8fu\xde\xaf\xbe\xef"
				      "s\0\0\0\4testu\0\0\0\0"), 31));
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
	 * name field in a UPSTART_JOB_STATUS_END message is NULL.
	 */
	TEST_FEATURE ("with null name in UPSTART_JOB_STATUS_END message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     ("upstart\n\0\0\x01\x8fu\xde\xaf\xbe\xef"
				      "Su\0\0\0\0u\0\0\0\0"), 28));
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


	/* Check that the UPSTART_MESSAGE_INVALID error is raised on an
	 * incomplete UPSTART_JOB_UNKNOWN message.
	 */
	TEST_FEATURE ("with incomplete UPSTART_JOB_UNKNOWN message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data, "upstart\n\0\0\x01\xf0S", 13));
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


	/* Check that the UPSTART_MESSAGE_INVALID error is raised on an
	 * incomplete UPSTART_JOB_INVALID message.
	 */
	TEST_FEATURE ("with incomplete UPSTART_JOB_INVALID message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     "upstart\n\0\0\x01\xf1u\0\0\0\0", 17));
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
	 * name field in a UPSTART_JOB_INVALID message is NULL.
	 */
	TEST_FEATURE ("with null name in UPSTART_JOB_INVALID message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     "upstart\n\0\0\x01\xf1u\0\0\0\0S", 18));
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


	/* Check that the UPSTART_MESSAGE_INVALID error is raised on an
	 * incomplete UPSTART_JOB_UNCHANGED message.
	 */
	TEST_FEATURE ("with incomplete UPSTART_JOB_UNCHANGED message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     "upstart\n\0\0\x01\xf2u\0\0\0\0", 17));
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
	 * name field in a UPSTART_JOB_UNCHANGED message is NULL.
	 */
	TEST_FEATURE ("with null name in UPSTART_JOB_UNCHANGED message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     "upstart\n\0\0\x01\xf2u\0\0\0\0S", 18));
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


	/* Check that the UPSTART_MESSAGE_INVALID error is raised on an
	 * incomplete UPSTART_EVENT_EMIT message.
	 */
	TEST_FEATURE ("with incomplete UPSTART_EVENT_EMIT message");
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
	 * name field in a UPSTART_EVENT_EMIT message is NULL.
	 */
	TEST_FEATURE ("with null name in UPSTART_EVENT_EMIT message");
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


	/* Check that the UPSTART_MESSAGE_INVALID error is raised on an
	 * incomplete UPSTART_EVENT message.
	 */
	TEST_FEATURE ("with incomplete UPSTART_EVENT message");
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
	 * name field in a UPSTART_EVENT message is NULL.
	 */
	TEST_FEATURE ("with null name in UPSTART_EVENT message");
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


	/* Check that the UPSTART_MESSAGE_INVALID error is raised on an
	 * incomplete UPSTART_EVENT_CAUSED message.
	 */
	TEST_FEATURE ("with incomplete UPSTART_EVENT_CAUSED message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data, "upstart\n\0\0\02\x11", 12));
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


	/* Check that the UPSTART_MESSAGE_INVALID error is raised on an
	 * incomplete UPSTART_EVENT_FINISHED message.
	 */
	TEST_FEATURE ("with incomplete UPSTART_EVENT_FINISHED message");
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
	 * name field in a UPSTART_EVENT_FINISHED message is NULL.
	 */
	TEST_FEATURE ("with null name in UPSTART_EVENT_FINISHED message");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     ("upstart\n\0\0\x02\x1fu\xde\xaf\xbe\xef"
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
	 * the right pid and the illegal message error is raised instead.
	 */
	TEST_FEATURE ("with message from unexpected pid");
	upstart_disable_safeties = FALSE;
	cred.pid = 1234;

	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data, "upstart\n\0\0\0\0", 12));
	assert0 (nih_io_message_add_control (msg, SOL_SOCKET, SCM_CREDENTIALS,
					     sizeof (cred), &cred));

	handler_called = FALSE;

	ret = upstart_message_handle (NULL, msg, any_handler, &ret);

	TEST_LT (ret, 0);
	TEST_FALSE (handler_called);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_MESSAGE_ILLEGAL);

	nih_free (err);

	nih_free (msg);

	cred.pid = 1000;
	upstart_disable_safeties = TRUE;


	/* Check that no handler is called if the message type isn't right
	 * and the UPSTART_MESSAGE_UNKNOWN error is raised instead.
	 */
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
				     "upstart\n\0\0\0\0", 17));
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
	TEST_EQ (last_type, UPSTART_NO_OP);

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


	/* Check that the illegal message error is raised if the message
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

		ret = upstart_message_handle_using (NULL, msg,
						    my_handler, &ret);

		TEST_EQ (ret, 0);
		TEST_TRUE (handler_called);
		TEST_EQ_P (last_data, &ret);
		TEST_EQ (last_pid, 1000);
		TEST_EQ (last_type, UPSTART_NO_OP);

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
						     "upstart\n\0\0\0\0", 12));
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
		TEST_EQ (last_type, UPSTART_NO_OP);
		TEST_TRUE (destructor_called);
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
						     "upstart\n\xff\xff\xff\xff",
						     12));
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
