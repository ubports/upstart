/* libupstart
 *
 * test_control.c - test suite for upstart/control.c
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
#include <sys/socket.h>
#include <sys/un.h>

#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/error.h>

#include <upstart/errors.h>
#include <upstart/control.h>


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
test_send_msg_to (void)
{
	UpstartMsg *msg;
	NihError   *err;
	int         sock, ret;

	TEST_FUNCTION ("upstart_send_msg_to");
	sock = upstart_open ();
	msg = nih_new (NULL, UpstartMsg);

	/* Check that sending an unknown message type results in the
	 * UPSTART_INVALID_MESSAGE error being raised.
	 */
	TEST_FEATURE ("with unknown message type");
	msg->type = 90210;
	ret = upstart_send_msg_to (getpid (), sock, msg);

	TEST_LT (ret, 0);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_INVALID_MESSAGE);

	nih_free (err);


	/* Check that sending a message that's too long also results in the
	 * UPSTART_INVALID_MESSAGE error being raised.
	 */
	TEST_FEATURE ("with overly long message");
	msg->type = UPSTART_JOB_QUERY;
	msg->name = nih_alloc (msg, 8192);
	memset (msg->name, 'a', 8191);
	msg->name[8191] = '\0';

	ret = upstart_send_msg_to (getpid (), sock, msg);

	TEST_LT (ret, 0);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_INVALID_MESSAGE);

	nih_free (err);


	nih_free (msg);
	close (sock);
}

void
test_recv_msg (void)
{
	struct sockaddr_un  addr;
	size_t              addrlen;
	UpstartMsg         *msg;
	NihError           *err;
	pid_t               pid;
	char                buf[80];
	int                 s_sock, r_sock;

	TEST_FUNCTION ("upstart_recv_msg");
	s_sock = socket (PF_UNIX, SOCK_DGRAM, 0);
	r_sock = upstart_open ();

	addr.sun_family = AF_UNIX;
	addr.sun_path[0] = '\0';

	addrlen = offsetof (struct sockaddr_un, sun_path) + 1;
	addrlen += snprintf (addr.sun_path + 1, sizeof (addr.sun_path) -1,
			     "/com/ubuntu/upstart/%d", getpid ());

	/* Check that receiving a message without the usual magic marker
	 * results in only UPSTART_INVALID_MESSAGE being raised.
	 */
	TEST_FEATURE ("without magic marker");
	memset (buf, 0, 16);
	memcpy (buf, "deadbeef", 8);
	memcpy (buf + 8, "\0\0\0\0", 4);
	memcpy (buf + 12, "\0\0\0\0", 4);
	sendto (s_sock, buf, 16, 0, (struct sockaddr *)&addr, addrlen);
	msg = upstart_recv_msg (NULL, r_sock, NULL);

	TEST_EQ_P (msg, NULL);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_INVALID_MESSAGE);

	nih_free (err);


	/* Check that receiving a message with an unknown type results in
	 * the UPSTART_INVALID_MESSAGE error being raised.
	 */
	TEST_FEATURE ("with unknown message type");
	memset (buf, 0, 16);
	memcpy (buf, "upstart\0", 8);
	memcpy (buf + 8, "\0\0\0\0", 4);
	memcpy (buf + 12, "\001\0\0\0", 4);
	sendto (s_sock, buf, 16, 0, (struct sockaddr *)&addr, addrlen);
	msg = upstart_recv_msg (NULL, r_sock, NULL);

	TEST_EQ_P (msg, NULL);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_INVALID_MESSAGE);

	nih_free (err);


	/* Check that receiving a message which has been truncated (or is
	 * just too short) results in the UPSTART_INVALID_MESSAGE error
	 * being raised.
	 */
	TEST_FEATURE ("with short message");
	memset (buf, 0, 24);
	memcpy (buf, "upstart\0", 8);
	memcpy (buf + 8, "\0\0\0\0", 4);
	memcpy (buf + 12, "\0\0\0\001", 4);
	memcpy (buf + 16, "\0\0\0\040\0\0\0\0", 8);
	sendto (s_sock, buf, 24, 0, (struct sockaddr *)&addr, addrlen);
	msg = upstart_recv_msg (NULL, r_sock, NULL);

	TEST_EQ_P (msg, NULL);

	err = nih_error_get ();

	TEST_EQ (err->number, UPSTART_INVALID_MESSAGE);

	nih_free (err);


	/* Check that a valid message being received returns that message,
	 * allocated with nih_alloc and with the values filled in.  Also
	 * check that the pid of the sender is stored in the argument.
	 */
	TEST_FEATURE ("with valid message");
	memset (buf, 0, 16);
	memcpy (buf, "upstart\0", 8);
	memcpy (buf + 8, "\0\0\0\0", 4);
	memcpy (buf + 12, "\0\0\0\0", 4);
	sendto (s_sock, buf, 16, 0, (struct sockaddr *)&addr, addrlen);
	msg = upstart_recv_msg (NULL, r_sock, &pid);

	TEST_ALLOC_SIZE (msg, sizeof (UpstartMsg));
	TEST_EQ (msg->type, UPSTART_NO_OP);
	TEST_EQ (pid, getpid ());

	nih_free (msg);


	close (s_sock);
	close (r_sock);
}

void
test_messages (void)
{
	UpstartMsg *s_msg, *r_msg;
	int         s_sock, r_sock;

	/* Rather than test the sending and receiving separately,
	 * check whether messages poked in one end come out the other
	 * the same way
	 */

	TEST_FUNCTION ("upstart_send/recv_msg");
	s_sock = socket (PF_UNIX, SOCK_DGRAM, 0);
	r_sock = upstart_open ();
	s_msg = nih_new (NULL, UpstartMsg);


	/* Check that an UPSTART_NO_OP message can be sent and received
	 * correctly, with all fields transmitted properly.
	 */
	TEST_FEATURE ("with UPSTART_NO_OP");
	s_msg->type = UPSTART_NO_OP;

	upstart_send_msg_to (getpid (), s_sock, s_msg);
	r_msg = upstart_recv_msg (NULL, r_sock, NULL);

	TEST_ALLOC_SIZE (r_msg, sizeof (UpstartMsg));
	TEST_EQ (r_msg->type, UPSTART_NO_OP);

	nih_free (r_msg);


	/* Check that an UPSTART_JOB_START message can be sent and received
	 * correctly, with all fields transmitted properly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_START");
	s_msg->type = UPSTART_JOB_START;
	s_msg->name = "wibble";

	upstart_send_msg_to (getpid (), s_sock, s_msg);
	r_msg = upstart_recv_msg (NULL, r_sock, NULL);

	TEST_ALLOC_SIZE (r_msg, sizeof (UpstartMsg));
	TEST_EQ (r_msg->type, UPSTART_JOB_START);
	TEST_EQ_STR (r_msg->name, "wibble");
	TEST_ALLOC_PARENT (r_msg->name, r_msg);

	nih_free (r_msg);


	/* Check that an UPSTART_JOB_STOP message can be sent and received
	 * correctly, with all fields transmitted properly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_STOP");
	s_msg->type = UPSTART_JOB_STOP;
	s_msg->name = "wibble";

	upstart_send_msg_to (getpid (), s_sock, s_msg);
	r_msg = upstart_recv_msg (NULL, r_sock, NULL);

	TEST_ALLOC_SIZE (r_msg, sizeof (UpstartMsg));
	TEST_EQ (r_msg->type, UPSTART_JOB_STOP);
	TEST_EQ_STR (r_msg->name, "wibble");
	TEST_ALLOC_PARENT (r_msg->name, r_msg);

	nih_free (r_msg);


	/* Check that an UPSTART_JOB_QUERY message can be sent and received
	 * correctly, with all fields transmitted properly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_QUERY");
	s_msg->type = UPSTART_JOB_QUERY;
	s_msg->name = "wibble";

	upstart_send_msg_to (getpid (), s_sock, s_msg);
	r_msg = upstart_recv_msg (NULL, r_sock, NULL);

	TEST_ALLOC_SIZE (r_msg, sizeof (UpstartMsg));
	TEST_EQ (r_msg->type, UPSTART_JOB_QUERY);
	TEST_EQ_STR (r_msg->name, "wibble");
	TEST_ALLOC_PARENT (r_msg->name, r_msg);

	nih_free (r_msg);


	/* Check that an UPSTART_JOB_STATUS message can be sent and received
	 * correctly, with all fields transmitted properly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_STATUS");
	s_msg->type = UPSTART_JOB_STATUS;
	s_msg->name = "wibble";
	s_msg->description = "foo bar";
	s_msg->goal = JOB_START;
	s_msg->state = JOB_STARTING;
	s_msg->process_state = PROCESS_ACTIVE;
	s_msg->pid = 123;

	upstart_send_msg_to (getpid (), s_sock, s_msg);
	r_msg = upstart_recv_msg (NULL, r_sock, NULL);

	TEST_ALLOC_SIZE (r_msg, sizeof (UpstartMsg));
	TEST_EQ (r_msg->type, UPSTART_JOB_STATUS);
	TEST_EQ_STR (r_msg->name, "wibble");
	TEST_ALLOC_PARENT (r_msg->name, r_msg);
	TEST_EQ_STR (r_msg->description, "foo bar");
	TEST_ALLOC_PARENT (r_msg->description, r_msg);
	TEST_EQ (r_msg->goal, JOB_START);
	TEST_EQ (r_msg->state, JOB_STARTING);
	TEST_EQ (r_msg->process_state, PROCESS_ACTIVE);
	TEST_EQ (r_msg->pid, 123);

	nih_free (r_msg);


	/* Check that an UPSTART_JOB_STATUS message without a job
	 * description can be sent and received correctly, with all fields
	 * transmitted properly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_STATUS without description");
	s_msg->description = NULL;

	upstart_send_msg_to (getpid (), s_sock, s_msg);
	r_msg = upstart_recv_msg (NULL, r_sock, NULL);

	TEST_ALLOC_SIZE (r_msg, sizeof (UpstartMsg));
	TEST_EQ (r_msg->type, UPSTART_JOB_STATUS);
	TEST_EQ_STR (r_msg->name, "wibble");
	TEST_ALLOC_PARENT (r_msg->name, r_msg);
	TEST_EQ_P (r_msg->description, NULL);
	TEST_EQ (r_msg->goal, JOB_START);
	TEST_EQ (r_msg->state, JOB_STARTING);
	TEST_EQ (r_msg->process_state, PROCESS_ACTIVE);
	TEST_EQ (r_msg->pid, 123);

	nih_free (r_msg);


	/* Check that an UPSTART_JOB_UNKNOWN message can be sent and received
	 * correctly, with all fields transmitted properly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_UNKNOWN");
	s_msg->type = UPSTART_JOB_UNKNOWN;
	s_msg->name = "wibble";

	upstart_send_msg_to (getpid (), s_sock, s_msg);
	r_msg = upstart_recv_msg (NULL, r_sock, NULL);

	TEST_ALLOC_SIZE (r_msg, sizeof (UpstartMsg));
	TEST_EQ (r_msg->type, UPSTART_JOB_UNKNOWN);
	TEST_EQ_STR (r_msg->name, "wibble");
	TEST_ALLOC_PARENT (r_msg->name, r_msg);

	nih_free (r_msg);


	/* Check that an UPSTART_EVENT_QUEUE message can be sent and received
	 * correctly, with all fields transmitted properly.
	 */
	TEST_FEATURE ("with UPSTART_EVENT_QUEUE");
	s_msg->type = UPSTART_EVENT_QUEUE;
	s_msg->name = "frodo";

	upstart_send_msg_to (getpid (), s_sock, s_msg);
	r_msg = upstart_recv_msg (NULL, r_sock, NULL);

	TEST_ALLOC_SIZE (r_msg, sizeof (UpstartMsg));
	TEST_EQ (r_msg->type, UPSTART_EVENT_QUEUE);
	TEST_EQ_STR (r_msg->name, "frodo");
	TEST_ALLOC_PARENT (r_msg->name, r_msg);

	nih_free (r_msg);


	/* Check that an UPSTART_EVENT message can be sent and received
	 * correctly, with all fields transmitted properly.
	 */
	TEST_FEATURE ("with UPSTART_EVENT");
	s_msg->type = UPSTART_EVENT;
	s_msg->name = "foo";

	upstart_send_msg_to (getpid (), s_sock, s_msg);
	r_msg = upstart_recv_msg (NULL, r_sock, NULL);

	TEST_ALLOC_SIZE (r_msg, sizeof (UpstartMsg));
	TEST_EQ (r_msg->type, UPSTART_EVENT);
	TEST_EQ_STR (r_msg->name, "foo");
	TEST_ALLOC_PARENT (r_msg->name, r_msg);

	nih_free (r_msg);


	/* Check that an UPSTART_JOB_LIST message can be sent and received
	 * correctly, with all fields transmitted properly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_LIST");
	s_msg->type = UPSTART_JOB_LIST;

	upstart_send_msg_to (getpid (), s_sock, s_msg);
	r_msg = upstart_recv_msg (NULL, r_sock, NULL);

	TEST_ALLOC_SIZE (r_msg, sizeof (UpstartMsg));
	TEST_EQ (r_msg->type, UPSTART_JOB_LIST);

	nih_free (r_msg);


	/* Check that an UPSTART_JOB_LIST_END message can be sent and received
	 * correctly, with all fields transmitted properly.
	 */
	TEST_FEATURE ("with UPSTART_JOB_LIST_END");
	s_msg->type = UPSTART_JOB_LIST_END;

	upstart_send_msg_to (getpid (), s_sock, s_msg);
	r_msg = upstart_recv_msg (NULL, r_sock, NULL);

	TEST_ALLOC_SIZE (r_msg, sizeof (UpstartMsg));
	TEST_EQ (r_msg->type, UPSTART_JOB_LIST_END);

	nih_free (r_msg);


	/* Check that an UPSTART_WATCH_JOBS message can be sent and received
	 * correctly, with all fields transmitted properly.
	 */
	TEST_FEATURE ("with UPSTART_WATCH_JOBS");
	s_msg->type = UPSTART_WATCH_JOBS;

	upstart_send_msg_to (getpid (), s_sock, s_msg);
	r_msg = upstart_recv_msg (NULL, r_sock, NULL);

	TEST_ALLOC_SIZE (r_msg, sizeof (UpstartMsg));
	TEST_EQ (r_msg->type, UPSTART_WATCH_JOBS);

	nih_free (r_msg);


	/* Check that an UPSTART_UNWATCH_JOBS message can be sent and received
	 * correctly, with all fields transmitted properly.
	 */
	TEST_FEATURE ("with UPSTART_UNWATCH_JOBS");
	s_msg->type = UPSTART_UNWATCH_JOBS;

	upstart_send_msg_to (getpid (), s_sock, s_msg);
	r_msg = upstart_recv_msg (NULL, r_sock, NULL);

	TEST_ALLOC_SIZE (r_msg, sizeof (UpstartMsg));
	TEST_EQ (r_msg->type, UPSTART_UNWATCH_JOBS);

	nih_free (r_msg);


	/* Check that an UPSTART_WATCH_EVENTS message can be sent and received
	 * correctly, with all fields transmitted properly.
	 */
	TEST_FEATURE ("with UPSTART_WATCH_EVENTS");
	s_msg->type = UPSTART_WATCH_EVENTS;

	upstart_send_msg_to (getpid (), s_sock, s_msg);
	r_msg = upstart_recv_msg (NULL, r_sock, NULL);

	TEST_ALLOC_SIZE (r_msg, sizeof (UpstartMsg));
	TEST_EQ (r_msg->type, UPSTART_WATCH_EVENTS);

	nih_free (r_msg);


	/* Check that an UPSTART_UNWATCH_EVENTS message can be sent and
	 * received correctly, with all fields transmitted properly.
	 */
	TEST_FEATURE ("with UPSTART_UNWATCH_EVENTS");
	s_msg->type = UPSTART_UNWATCH_EVENTS;

	upstart_send_msg_to (getpid (), s_sock, s_msg);
	r_msg = upstart_recv_msg (NULL, r_sock, NULL);

	TEST_ALLOC_SIZE (r_msg, sizeof (UpstartMsg));
	TEST_EQ (r_msg->type, UPSTART_UNWATCH_EVENTS);

	nih_free (r_msg);


	/* Check that an UPSTART_SHUTDOWN message can be sent and received
	 * correctly, with all fields transmitted properly.
	 */
	TEST_FEATURE ("with UPSTART_SHUTDOWN");
	s_msg->type = UPSTART_SHUTDOWN;
	s_msg->name = "reboot";

	upstart_send_msg_to (getpid (), s_sock, s_msg);
	r_msg = upstart_recv_msg (NULL, r_sock, NULL);

	TEST_ALLOC_SIZE (r_msg, sizeof (UpstartMsg));
	TEST_EQ (r_msg->type, UPSTART_SHUTDOWN);
	TEST_EQ_STR (r_msg->name, "reboot");
	TEST_ALLOC_PARENT (r_msg->name, r_msg);

	nih_free (r_msg);


	nih_free (s_msg);

	close (r_sock);
	close (s_sock);
}


int
main (int   argc,
      char *argv[])
{
	test_open ();
	test_send_msg_to ();
	test_recv_msg ();
	test_messages ();

	return 0;
}
