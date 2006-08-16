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

#include <config.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/error.h>

#include <upstart/errors.h>
#include <upstart/control.h>


int
test_open (void)
{
	struct sockaddr_un addr;
	int                ret = 0, sock, val;
	char               name[26];
	socklen_t          len;

	printf ("Testing upstart_open()\n");
	sock = upstart_open ();

	/* Socket should be in AF_UNIX space */
	len = sizeof (addr);
	assert (getsockname (sock, (struct sockaddr *)&addr, &len) == 0);
	if (addr.sun_family != AF_UNIX) {
		printf ("BAD: address family wasn't what we expected.\n");
		ret = 1;
	}

	/* Socket should be in abstract namespace */
	if (addr.sun_path[0] != '\0') {
		printf ("BAD: address type wasn't what we expected.\n");
		ret = 1;
	}

	/* Name should be /com/ubuntu/upstart/$PID */
	sprintf (name, "/com/ubuntu/upstart/%d", getpid ());
	if (strncmp (addr.sun_path + 1, name, strlen (name))) {
		printf ("BAD: address wasn't what we expected.\n");
		ret = 1;
	}

	/* Should work on datagrams */
	val = 0;
	len = sizeof (val);
	assert (getsockopt (sock, SOL_SOCKET, SO_TYPE, &val, &len) == 0);
	if (val != SOCK_DGRAM) {
		printf ("BAD: socket type wasn't what we expected.\n");
		ret = 1;
	}

	/* Credentials should be passed with any received message */
	val = 0;
	len = sizeof (val);
	assert (getsockopt (sock, SOL_SOCKET, SO_PASSCRED, &val, &len) == 0);
	if (val == 0) {
		printf ("BAD: socket will not receive credentials.\n");
		ret = 1;
	}

	close (sock);

	return ret;
}


int
test_send_msg_to (void)
{
	UpstartMsg *msg;
	NihError   *err;
	int         ret = 0, sock, retval;

	printf ("Testing upstart_send_msg_to()\n");
	sock = upstart_open ();
	msg = nih_new (NULL, UpstartMsg);

	printf ("...with unknown message type\n");
	msg->type = 90210;
	retval = upstart_send_msg_to (getpid (), sock, msg);

	/* Return value should be negative */
	if (retval >= 0) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}

	/* UPSTART_INVALID_MESSAGE should be raised */
	err = nih_error_get ();
	if (err->number != UPSTART_INVALID_MESSAGE) {
		printf ("BAD: raised error wasn't what we expected.\n");
		ret = 1;
	}
	nih_free (err);


	printf ("...with overly long message\n");
	msg->type = UPSTART_JOB_QUERY;
	msg->job_query.name = nih_alloc (msg, 8192);
	memset (msg->job_query.name, 'a', 8192);
	msg->job_query.name[8192] = '\0';

	retval = upstart_send_msg_to (getpid (), sock, msg);

	/* Return value should be negative */
	if (retval >= 0) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}

	/* UPSTART_INVALID_MESSAGE should be raised */
	err = nih_error_get ();
	if (err->number != UPSTART_INVALID_MESSAGE) {
		printf ("BAD: raised error wasn't what we expected.\n");
		ret = 1;
	}
	nih_free (err);


	nih_free (msg);
	close (sock);

	return ret;
}

int
test_recv_msg (void)
{
	struct sockaddr_un  addr;
	size_t              addrlen;
	UpstartMsg         *msg;
	NihError           *err;
	pid_t               pid;
	int                 ret = 0, s_sock, r_sock;

	printf ("Testing upstart_recv_msg()\n");
	s_sock = socket (PF_UNIX, SOCK_DGRAM, 0);
	r_sock = upstart_open ();

	addr.sun_family = AF_UNIX;
	addr.sun_path[0] = '\0';

	addrlen = __builtin_offsetof (struct sockaddr_un, sun_path) + 1;
	addrlen += snprintf (addr.sun_path + 1, sizeof (addr.sun_path) -1,
			     "/com/ubuntu/upstart/%d", getpid ());

	printf ("...without magic marker\n");
	sendto (s_sock, "wibblefart\0\0\0\0\0\0", 16,
		0, (struct sockaddr *)&addr, addrlen);
	msg = upstart_recv_msg (NULL, r_sock, NULL);

	/* Return value should be NULL */
	if (msg != NULL) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}

	/* UPSTART_INVALID_MESSAGE should be raised */
	err = nih_error_get ();
	if (err->number != UPSTART_INVALID_MESSAGE) {
		printf ("BAD: raised error wasn't what we expected.\n");
		ret = 1;
	}
	nih_free (err);


	printf ("...with unknown message type\n");
	sendto (s_sock, "upstart0.1\0\0\0\0\0\001", 16,
		0, (struct sockaddr *)&addr, addrlen);
	msg = upstart_recv_msg (NULL, r_sock, NULL);

	/* Return value should be NULL */
	if (msg != NULL) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}

	/* UPSTART_INVALID_MESSAGE should be raised */
	err = nih_error_get ();
	if (err->number != UPSTART_INVALID_MESSAGE) {
		printf ("BAD: raised error wasn't what we expected.\n");
		ret = 1;
	}
	nih_free (err);


	printf ("...with short message\n");
	sendto (s_sock, "upstart0.1\0\0\001\0\0\0\040\0\0\0\0\0\0\0", 24,
		0, (struct sockaddr *)&addr, addrlen);
	msg = upstart_recv_msg (NULL, r_sock, NULL);

	/* Return value should be NULL */
	if (msg != NULL) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}

	/* UPSTART_INVALID_MESSAGE should be raised */
	err = nih_error_get ();
	if (err->number != UPSTART_INVALID_MESSAGE) {
		printf ("BAD: raised error wasn't what we expected.\n");
		ret = 1;
	}
	nih_free (err);


	printf ("...with valid message\n");
	sendto (s_sock, "upstart0.1\0\0\0\0\0\0", 16,
		0, (struct sockaddr *)&addr, addrlen);
	msg = upstart_recv_msg (NULL, r_sock, &pid);

	/* Message type should be UPSTART_NO_OP */
	if (msg->type != UPSTART_NO_OP) {
		printf ("BAD: message type wasn't what we expected.\n");
		ret = 1;
	}

	/* Process id should be stored in pid */
	if (pid != getpid ()) {
		printf ("BAD: process id wasn't what we expected.\n");
		ret = 1;
	}

	nih_free (msg);


	close (s_sock);
	close (r_sock);

	return ret;
}

int
test_messages (void)
{
	UpstartMsg *s_msg, *r_msg;
	int         s_sock, r_sock;
	int         ret = 0;

	/* Rather than test the sending and receiving separately,
	 * check whether messages poked in one end come out the other
	 * the same way
	 */

	printf ("Testing upstart_send/recv_msg()\n");
	s_sock = socket (PF_UNIX, SOCK_DGRAM, 0);
	r_sock = upstart_open ();
	s_msg = nih_new (NULL, UpstartMsg);


	printf ("...with UPSTART_NO_OP\n");
	s_msg->type = UPSTART_NO_OP;

	upstart_send_msg_to (getpid (), s_sock, s_msg);
	r_msg = upstart_recv_msg (NULL, r_sock, NULL);

	/* Type should be UPSTART_NO_OP */
	if (r_msg->type != UPSTART_NO_OP) {
		printf ("BAD: message type wasn't what we expected.\n");
		ret = 1;
	}

	nih_free (r_msg);


	printf ("...with UPSTART_JOB_START\n");
	s_msg->type = UPSTART_JOB_START;
	s_msg->job_start.name = "wibble";

	upstart_send_msg_to (getpid (), s_sock, s_msg);
	r_msg = upstart_recv_msg (NULL, r_sock, NULL);

	/* Type should be UPSTART_JOB_START */
	if (r_msg->type != UPSTART_JOB_START) {
		printf ("BAD: message type wasn't what we expected.\n");
		ret = 1;
	}

	/* Name should be what we sent */
	if (strcmp (r_msg->job_start.name, "wibble")) {
		printf ("BAD: job name wasn't what we expected.\n");
		ret = 1;
	}

	/* Name should be nih_alloc child of message */
	if (nih_alloc_parent (r_msg->job_start.name) != r_msg) {
		printf ("BAD: name wasn't nih_alloc child of message.\n");
		ret = 1;
	}

	nih_free (r_msg);


	printf ("...with UPSTART_JOB_STOP\n");
	s_msg->type = UPSTART_JOB_STOP;
	s_msg->job_stop.name = "wibble";

	upstart_send_msg_to (getpid (), s_sock, s_msg);
	r_msg = upstart_recv_msg (NULL, r_sock, NULL);

	/* Type should be UPSTART_JOB_STOP */
	if (r_msg->type != UPSTART_JOB_STOP) {
		printf ("BAD: message type wasn't what we expected.\n");
		ret = 1;
	}

	/* Name should be what we sent */
	if (strcmp (r_msg->job_stop.name, "wibble")) {
		printf ("BAD: job name wasn't what we expected.\n");
		ret = 1;
	}

	/* Name should be nih_alloc child of message */
	if (nih_alloc_parent (r_msg->job_stop.name) != r_msg) {
		printf ("BAD: name wasn't nih_alloc child of message.\n");
		ret = 1;
	}

	nih_free (r_msg);


	printf ("...with UPSTART_JOB_QUERY\n");
	s_msg->type = UPSTART_JOB_QUERY;
	s_msg->job_query.name = "wibble";

	upstart_send_msg_to (getpid (), s_sock, s_msg);
	r_msg = upstart_recv_msg (NULL, r_sock, NULL);

	/* Type should be UPSTART_JOB_QUERY */
	if (r_msg->type != UPSTART_JOB_QUERY) {
		printf ("BAD: message type wasn't what we expected.\n");
		ret = 1;
	}

	/* Name should be what we sent */
	if (strcmp (r_msg->job_query.name, "wibble")) {
		printf ("BAD: job name wasn't what we expected.\n");
		ret = 1;
	}

	/* Name should be nih_alloc child of message */
	if (nih_alloc_parent (r_msg->job_query.name) != r_msg) {
		printf ("BAD: name wasn't nih_alloc child of message.\n");
		ret = 1;
	}

	nih_free (r_msg);


	printf ("...with UPSTART_JOB_STATUS\n");
	s_msg->type = UPSTART_JOB_STATUS;
	s_msg->job_status.name = "wibble";
	s_msg->job_status.goal = JOB_START;
	s_msg->job_status.state = JOB_STARTING;
	s_msg->job_status.process_state = PROCESS_ACTIVE;

	upstart_send_msg_to (getpid (), s_sock, s_msg);
	r_msg = upstart_recv_msg (NULL, r_sock, NULL);

	/* Type should be UPSTART_JOB_STATUS */
	if (r_msg->type != UPSTART_JOB_STATUS) {
		printf ("BAD: message type wasn't what we expected.\n");
		ret = 1;
	}

	/* Name should be what we sent */
	if (strcmp (r_msg->job_status.name, "wibble")) {
		printf ("BAD: job name wasn't what we expected.\n");
		ret = 1;
	}

	/* Name should be nih_alloc child of message */
	if (nih_alloc_parent (r_msg->job_status.name) != r_msg) {
		printf ("BAD: name wasn't nih_alloc child of message.\n");
		ret = 1;
	}

	/* Job goal should be what we sent */
	if (r_msg->job_status.goal != JOB_START) {
		printf ("BAD: job goal wasn't what we expected.\n");
		ret = 1;
	}

	/* Job state should be what we sent */
	if (r_msg->job_status.state != JOB_STARTING) {
		printf ("BAD: job state wasn't what we expected.\n");
		ret = 1;
	}

	/* Process state should be what we sent */
	if (r_msg->job_status.process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state wasn't what we expected.\n");
		ret = 1;
	}

	nih_free (r_msg);


	printf ("...with UPSTART_JOB_UNKNOWN\n");
	s_msg->type = UPSTART_JOB_UNKNOWN;
	s_msg->job_unknown.name = "wibble";

	upstart_send_msg_to (getpid (), s_sock, s_msg);
	r_msg = upstart_recv_msg (NULL, r_sock, NULL);

	/* Type should be UPSTART_JOB_UNKNOWN */
	if (r_msg->type != UPSTART_JOB_UNKNOWN) {
		printf ("BAD: message type wasn't what we expected.\n");
		ret = 1;
	}

	/* Name should be what we sent */
	if (strcmp (r_msg->job_unknown.name, "wibble")) {
		printf ("BAD: job name wasn't what we expected.\n");
		ret = 1;
	}

	/* Name should be nih_alloc child of message */
	if (nih_alloc_parent (r_msg->job_unknown.name) != r_msg) {
		printf ("BAD: name wasn't nih_alloc child of message.\n");
		ret = 1;
	}

	nih_free (r_msg);


	nih_free (s_msg);

	close (r_sock);
	close (s_sock);

	return ret;
}

int
test_free (void)
{
	void *ptr;
	int   ret = 0;

	printf ("Testing upstart_free()\n");
	ptr = nih_alloc (NULL, 1024);
	upstart_free (ptr);

	/* didn't crash, so it worked */

	return ret;
}


int
main (int   argc,
      char *argv[])
{
	int ret = 0;

	ret |= test_open ();
	ret |= test_send_msg_to ();
	ret |= test_recv_msg ();
	ret |= test_messages ();
	ret |= test_free ();

	return ret;
}
