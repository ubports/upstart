/* upstart
 *
 * control.c - control socket communication
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <arpa/inet.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <stdio.h>
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
#include <upstart/control.h>


/**
 * MAX_PACKET_SIZE:
 *
 * Maximum size of a packet, including all names, environment, etc.  This
 * is completely arbitrary and just needs to be agreed by both ends.
 **/
#define MAX_PACKET_SIZE 4096

/**
 * MAGIC:
 *
 * Magic string that is placed on the front of all messages.
 **/
#define MAGIC "upstart\0"

/**
 * INIT_DAEMON:
 *
 * Macro used in place of a pid for the init daemon, simply to make it clear
 * what we're doing.
 **/
#define INIT_DAEMON 1


/* Prototypes for static functions */
static size_t upstart_addr         (struct sockaddr_un *addr, pid_t pid);
static int    upstart_read_int     (struct iovec *iovec, size_t *pos,
				    int *value);
static int    upstart_write_int    (struct iovec *iovec, size_t size,
				    int value);
static int    upstart_read_ints    (struct iovec *iovec, size_t *pos,
				    int number, ...);
static int    upstart_write_ints   (struct iovec *iovec, size_t size,
				    int number, ...);
static int    upstart_read_str     (struct iovec *iovec, size_t *pos,
				    const void *parent, char **value);
static int    upstart_write_str    (struct iovec *iovec, size_t size,
				    const char *value);
static int    upstart_read_header  (struct iovec *iovec, size_t *pos,
				    int *version, UpstartMsgType *type);
static int    upstart_write_header (struct iovec *iovec, size_t size,
				    int version, UpstartMsgType type);


/**
 * upstart_disable_safeties:
 *
 * If this variable is set to a TRUE value then safety checks on the
 * control socket are disabled.  This is highly unrecommended (which is
 * why there is no other prototype for it), but necessary for the test
 * suite *sigh*
 **/
int upstart_disable_safeties = FALSE;


/**
 * upstart_addr:
 * @addr: address structure to fill,
 * @pid: process id.
 *
 * Fills the given address structure with the address that a process of
 * @pid should be listening for responses on.
 *
 * The AF_UNIX abstract namespace is used with the init daemon (process #1)
 * bound to /com/ubuntu/upstart and clients bound to /com/ubuntu/upstart/$PID
 *
 * Returns: size of address.
 **/
static size_t
upstart_addr (struct sockaddr_un *addr,
	      pid_t               pid)
{
	size_t addrlen;

	nih_assert (addr != NULL);
	nih_assert (pid > 0);

	addr->sun_family = AF_UNIX;
	addr->sun_path[0] = '\0';

	addrlen = offsetof (struct sockaddr_un, sun_path) + 1;
	if (pid == INIT_DAEMON) {
		addrlen += snprintf (addr->sun_path + 1,
				     sizeof (addr->sun_path) - 1,
				     "/com/ubuntu/upstart");
	} else {
		addrlen += snprintf (addr->sun_path + 1,
				     sizeof (addr->sun_path) - 1,
				     "/com/ubuntu/upstart/%d", pid);
	}

	return addrlen;
}


/**
 * upstart_open:
 *
 * Open a connection to the running init daemon's control socket.  The
 * returned socket is used both to send messages to the daemon and receive
 * responses.
 *
 * Only one connection is permitted per process, a second call to this
 * function without closing the socket from the first will result in an
 * EADDRINUSE error.
 *
 * If the init daemon calls this function then the socket returned will
 * receive messages from all clients.
 *
 * Returns: open socket or negative value on raised error.
 **/
int
upstart_open (void)
{
	struct sockaddr_un addr;
	size_t             addrlen;
	int                sock, optval;

	/* Communication is performed using a unix datagram socket */
	sock = socket (PF_UNIX, SOCK_DGRAM, 0);
	if (sock < 0)
		nih_return_system_error (-1);

	/* Bind the socket so we can receive responses */
	addrlen = upstart_addr (&addr, getpid ());
	if (bind (sock, (struct sockaddr *)&addr, addrlen) < 0) {
		nih_error_raise_system ();
		close (sock);
		return -1;
	}

	/* Always requests credentials */
	optval = 1;
	if (setsockopt (sock, SOL_SOCKET, SO_PASSCRED, &optval,
			sizeof (optval)) < 0) {
		nih_error_raise_system ();
		close (sock);
		return -1;
	}

	return sock;
}


/**
 * upstart_send_msg:
 * @sock: socket to send @message on,
 * @message: message to send.
 *
 * Send @message to the running init daemon using @sock which should have
 * been opened with upstart_open().
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
upstart_send_msg (int         sock,
		  UpstartMsg *message)
{
	nih_assert (sock >= 0);
	nih_assert (message != NULL);

	return upstart_send_msg_to (INIT_DAEMON, sock, message);
}

/**
 * upstart_send_msg_to:
 * @pid: process to send message to,
 * @sock: socket to send @message on,
 * @message: message to send.
 *
 * Send @message to process @pid using @sock which should have been opened
 * with upstart_open().
 *
 * Clients will normally discard messages that do not come from process #1
 * (the init daemon), so this is only useful from the init daemon itself.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
upstart_send_msg_to (pid_t       pid,
		     int         sock,
		     UpstartMsg *message)
{
	struct sockaddr_un addr;
	struct msghdr      msg;
	struct iovec       iov[1];
	char               buf[MAX_PACKET_SIZE];

	nih_assert (pid > 0);
	nih_assert (sock >= 0);
	nih_assert (message != NULL);

	/* Recipient address */
	msg.msg_name = &addr;
	msg.msg_namelen = upstart_addr (&addr, pid);

	/* Send whatever we put in the iovec */
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;

	/* Start off with no control information */
	msg.msg_control = NULL;
	msg.msg_controllen = 0;

	/* Unused, so set to zero */
	msg.msg_flags = 0;

	/* Use the buffer */
	iov[0].iov_base = buf;
	iov[0].iov_len = 0;

	/* Place a header at the start */
	if (upstart_write_header (&iov[0], sizeof (buf),
				  UPSTART_API_VERSION, message->type))
		goto invalid;

	/* Message type determines actual payload */
	switch (message->type) {
	case UPSTART_NO_OP:
	case UPSTART_JOB_LIST:
	case UPSTART_JOB_LIST_END:
	case UPSTART_WATCH_JOBS:
	case UPSTART_UNWATCH_JOBS:
	case UPSTART_WATCH_EVENTS:
	case UPSTART_UNWATCH_EVENTS:
		/* No payload */
		break;

	case UPSTART_JOB_START:
	case UPSTART_JOB_STOP:
	case UPSTART_JOB_QUERY:
	case UPSTART_JOB_UNKNOWN: {
		/* Job name */
		if (upstart_write_str (&iov[0], sizeof (buf),
				       message->job_query.name))
			goto invalid;

		break;
	}
	case UPSTART_JOB_STATUS: {
		/* Job name, followed by job status */
		if (upstart_write_str (&iov[0], sizeof (buf),
				       message->job_status.name))
			goto invalid;

		if (upstart_write_ints (&iov[0], sizeof (buf), 4,
					message->job_status.goal,
					message->job_status.state,
					message->job_status.process_state,
					message->job_status.pid))
			goto invalid;

		if (upstart_write_str (&iov[0], sizeof (buf),
				       message->job_status.description))
			goto invalid;

		break;
	}
	case UPSTART_EVENT_QUEUE:
	case UPSTART_EVENT:
	case UPSTART_SHUTDOWN: {
		/* Event name */
		if (upstart_write_str (&iov[0], sizeof (buf),
				       message->event.name))
			goto invalid;
		break;
	}
	default:
		goto invalid;
	}

	/* Send it! */
	if (sendmsg (sock, &msg, 0) < 0)
		nih_return_system_error (-1);

	return 0;

invalid:
	nih_return_error (-1, UPSTART_INVALID_MESSAGE,
			  _(UPSTART_INVALID_MESSAGE_STR));
}


/**
 * upstart_recv_msg:
 * @parent: parent of new structure,
 * @sock: socket to receive from,
 * @pid: place to store pid of sender.
 *
 * Receives a single message from @sock, which should have been opened with
 * upstart_open().  Memory is allocated for the message structure and it
 * is returned, clients should use nih_free() to free the message.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * If you wish to know which process sent the message, usually because
 * you might want to send a response, pass a pointer for @pid.
 *
 * Returns: newly allocated message or NULL on raised error.
 **/
UpstartMsg *
upstart_recv_msg (const void *parent,
		  int         sock,
		  pid_t      *pid)
{
	UpstartMsg     *message = NULL;
	struct msghdr   msg;
	struct iovec    iov[1];
	char            buf[MAX_PACKET_SIZE];
	char            cmsg_buf[CMSG_SPACE (sizeof (struct ucred))];
	struct cmsghdr *cmsg;
	struct ucred    cred = { 0, 0, 0 };
	size_t          pos = 0;
	ssize_t         len;
	int             version;

	nih_assert (sock >= 0);

	/* We don't use the sender address, but rely on credentials instead */
	msg.msg_name = NULL;
	msg.msg_namelen = 0;

	/* Put the message in the iovec */
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;

	/* Space to receive control information */
	msg.msg_control = cmsg_buf;
	msg.msg_controllen = sizeof (cmsg_buf);

	/* Clear flags */
	msg.msg_flags = 0;

	/* Use the buffer */
	iov[0].iov_base = buf;
	iov[0].iov_len = sizeof (buf);

	/* Receive a single message into the buffers */
	len = recvmsg (sock, &msg, 0);
	if (len < 0)
		nih_return_system_error (NULL);

	iov[0].iov_len = len;

	/* Process the ancillary control information */
	for (cmsg = CMSG_FIRSTHDR (&msg); cmsg != NULL;
	     cmsg = CMSG_NXTHDR (&msg, cmsg)) {
		if ((cmsg->cmsg_level == SOL_SOCKET)
		    && (cmsg->cmsg_type == SCM_CREDENTIALS)) {
			/* Sender credentials */
			if (cmsg->cmsg_len < CMSG_LEN (sizeof (struct ucred)))
				goto invalid;

			memcpy (&cred, CMSG_DATA (cmsg),
				sizeof (struct ucred));
		}

		/* FIXME receive SCM_RIGHTS fds, close if we're not
		 * expecting them!
		 */
	}


	if (! upstart_disable_safeties) {
		/* Make sure we received the credentials of the
		 * sending process */
		if (cred.pid == 0)
			goto invalid;

		/* Can only receive messages from root, or our own uid
		 * FIXME init may want to receive more in future
		 */
		if ((cred.uid != 0) && (cred.uid != getuid ()))
			goto invalid;

		/* Only the init daemon may accept messages from any process */
		if ((cred.pid != INIT_DAEMON) && (cred.pid != getpid ())
		    && (getpid () != INIT_DAEMON))
			goto invalid;
	}

	/* Discard truncated messages */
	if ((msg.msg_flags & MSG_TRUNC) || (msg.msg_flags & MSG_CTRUNC))
		goto invalid;


	/* Allocate the message */
	NIH_MUST (message = nih_new (parent, UpstartMsg));

	/* Copy the header out of the message, that'll tell us what
	 * we're actually looking at.
	 */
	if (upstart_read_header (&iov[0], &pos, &version, &(message->type)))
		goto invalid;

	/* Message type determines actual payload */
	switch (message->type) {
	case UPSTART_NO_OP:
	case UPSTART_JOB_LIST:
	case UPSTART_JOB_LIST_END:
	case UPSTART_WATCH_JOBS:
	case UPSTART_UNWATCH_JOBS:
	case UPSTART_WATCH_EVENTS:
	case UPSTART_UNWATCH_EVENTS:
		/* No payload */
		break;
	case UPSTART_JOB_START:
	case UPSTART_JOB_STOP:
	case UPSTART_JOB_QUERY:
	case UPSTART_JOB_UNKNOWN: {
		/* Job name */
		if (upstart_read_str (&iov[0], &pos,
				      message, &(message->job_query.name)))
			goto invalid;

		break;
	}
	case UPSTART_JOB_STATUS: {
		/* Job name, followed by job status */
		if (upstart_read_str (&iov[0], &pos,
				      message, &(message->job_status.name)))
			goto invalid;

		if (upstart_read_ints (&iov[0], &pos, 4,
				       &(message->job_status.goal),
				       &(message->job_status.state),
				       &(message->job_status.process_state),
				       &(message->job_status.pid)))
			goto invalid;

		if (upstart_read_str (&iov[0], &pos,
				      message, &(message->job_status.description)))
			goto invalid;

		break;
	}
	case UPSTART_EVENT_QUEUE:
	case UPSTART_EVENT:
	case UPSTART_SHUTDOWN: {
		/* Event name */
		if (upstart_read_str (&iov[0], &pos,
				      message, &(message->event.name)))
			goto invalid;

		break;
	}
	default:
		goto invalid;
	}

	/* Save the pid */
	if (pid)
		*pid = cred.pid;

	return message;

invalid:
	nih_free (message);

	nih_return_error (NULL, UPSTART_INVALID_MESSAGE,
			  _(UPSTART_INVALID_MESSAGE_STR));
}


/**
 * upstart_read_int:
 * @iovec: iovec to read from,
 * @pos: position within iovec,
 * @value: pointer to write to.
 *
 * Read an integer value from @pos bytes into the @iovec given and store
 * it in the pointer @value.  @pos is incremented by the number of bytes
 * the integer used.
 *
 * Returns: zero on success, negative value if insufficient space.
 **/
static int
upstart_read_int (struct iovec *iovec,
		  size_t       *pos,
		  int          *value)
{
	size_t start;
	int    n_value;

	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);
	nih_assert (pos != NULL);
	nih_assert (value != NULL);

	start = *pos;
	*pos += sizeof (int);

	if (*pos > iovec->iov_len)
		return -1;

	memcpy (&n_value, iovec->iov_base + start, sizeof (int));
	*value = ntohl (n_value);

	return 0;
}

/**
 * upstart_write_int:
 * @iovec: iovec to write to,
 * @size: size of iovec buffer,
 * @value: value to write.
 *
 * Write an integer @value to the end of the @iovec given, which has a
 * buffer of @size bytes.  The length of the @iovec is incremented by
 * the number of bytes the integer used.
 *
 * Returns: zero on success, negative value if insufficient space.
 **/
static int
upstart_write_int (struct iovec *iovec,
		   size_t        size,
		   int           value)
{
	size_t start;
	int    n_value;

	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);

	start = iovec->iov_len;
	iovec->iov_len += sizeof (int);

	if (iovec->iov_len > size)
		return -1;

	n_value = htonl (value);
	memcpy (iovec->iov_base + start, &n_value, sizeof (int));

	return 0;
}

/**
 * upstart_read_ints:
 * @iovec: iovec to read from,
 * @pos: position within iovec,
 * @number: number of integers to read.
 *
 * Read @number integer values from @pos bytes into the @iovec given and
 * store them in the pointers given as additional arguments to the function.
 * @pos is incremented by the number of bytes the integers used.
 *
 * Returns: zero on success, negative value if insufficient space.
 **/
static int
upstart_read_ints (struct iovec *iovec,
		   size_t       *pos,
		   int           number,
		   ...)
{
	va_list args;

	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);
	nih_assert (pos != NULL);

	va_start (args, number);

	while (number--) {
		int *value;

		value = va_arg (args, int *);

		if (upstart_read_int (iovec, pos, value))
			return -1;
	}

	va_end (args);

	return 0;
}

/**
 * upstart_write_ints:
 * @iovec: iovec to write to,
 * @size: size of iovec buffer,
 * @number: number of integers to read.
 *
 * Write @number integer values to the end of the @iovec given, which has a
 * buffer of @size bytes.  The values are taken from additional arguments
 * to the function.  The length of the @iovec is incremented by the number
 * of bytes the integers used.
 *
 * Returns: zero on success, negative value if insufficient space.
 **/
static int
upstart_write_ints (struct iovec *iovec,
		    size_t        size,
		    int           number,
		    ...)
{
	va_list args;

	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);

	va_start (args, number);

	while (number--) {
		int value;

		value = va_arg (args, int);

		if (upstart_write_int (iovec, size, value))
			return -1;
	}

	va_end (args);

	return 0;
}

/**
 * upstart_read_str:
 * @iovec: iovec to read from,
 * @pos: position within iovec.
 * @parent: parent of new string,
 * @value: pointer to store string.
 *
 * Read a string value from @pos bytes into the @iovec given,
 * allocates a new string to contain it and stores it in the pointer
 * @value.
 *
 * The string will be NULL terminated.  If a zero-length string is
 * read, @value will be set to NULL.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned string will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * Returns: zero on success, negative value if insufficient space
 * or memory.
 **/
static int
upstart_read_str (struct iovec  *iovec,
		  size_t        *pos,
		  const void    *parent,
		  char         **value)
{
	size_t start, length;

	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);
	nih_assert (pos != NULL);
	nih_assert (value != NULL);

	if (upstart_read_int (iovec, pos, (int *)&length))
		return -1;

	if (! length) {
		*value = NULL;
		return 0;
	}


	start = *pos;
	*pos += length;

	if (*pos > iovec->iov_len)
		return -1;

	*value = nih_alloc (parent, length + 1);
	if (! *value)
		return -1;

	memcpy (*value, iovec->iov_base + start, length);
	(*value)[length] = '\0';

	return 0;
}

/**
 * upstart_write_str:
 * @iovec: iovec to write to,
 * @size: size of iovec buffer,
 * @value: value to write.
 *
 * Write a string @value to the end of the @iovec given, which has a
 * buffer of @size bytes.  The length of the @iovec is incremented by
 * the number of bytes the string used.
 *
 * If @value is NULL, a zero-length string is written.
 *
 * Returns: zero on success, negative value if insufficient space.
 **/
static int
upstart_write_str (struct iovec *iovec,
		   size_t        size,
		   const char   *value)
{
	size_t start, length;

	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);

	length = value ? strlen (value) : 0;
	if (upstart_write_int (iovec, size, length))
		return -1;

	if (! length)
		return 0;


	start = iovec->iov_len;
	iovec->iov_len += length;

	if (iovec->iov_len > size)
		return -1;

	memcpy (iovec->iov_base + start, value, length);

	return 0;
}

/**
 * upstart_read_header:
 * @iovec: iovec to read from,
 * @pos: position within iovec,
 * @version: pointer to write version to,
 * @type: pointer to write message type to.
 *
 * Read a message header from @pos bytes into the @iovec given, storing
 * the message version number in @version and message type in @type.  @pos
 * is incremented by the number of bytes the header.
 *
 * Returns: zero on success, negative value if insufficient space or invalid
 * format.
 **/
static int
upstart_read_header (struct iovec   *iovec,
		     size_t         *pos,
		     int            *version,
		     UpstartMsgType *type)
{
	size_t start;

	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);
	nih_assert (pos != NULL);
	nih_assert (version != NULL);
	nih_assert (type != NULL);

	start = *pos;
	*pos += sizeof (MAGIC) - 1;

	if (*pos > iovec->iov_len)
		return -1;

	if (memcmp (iovec->iov_base + start, MAGIC, sizeof (MAGIC) - 1))
		return -1;

	if (upstart_read_int (iovec, pos, version))
		return -1;

	if (upstart_read_int (iovec, pos, (int *)type))
		return -1;

	return 0;
}

/**
 * upstart_write_header:
 * @iovec: iovec to read from,
 * @size: size of iovec buffer,
 * @version: version to write,
 * @type: message type to write.
 *
 * Write a message header to the end of the @iovec given, which has a
 * buffer of @size bytes.  The header declares a message version number of
 * @version and a message type of @type.  The length of the @iovec is
 * incremented by the number of bytes the header used.
 *
 * Returns: zero on success, negative value if insufficient space or invalid
 * format.
 **/
static int
upstart_write_header (struct iovec   *iovec,
		      size_t          size,
		      int             version,
		      UpstartMsgType  type)
{
	size_t start;

	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);

	start = iovec->iov_len;
	iovec->iov_len += sizeof (MAGIC) - 1;

	if (iovec->iov_len > size)
		return -1;

	memcpy (iovec->iov_base + start, MAGIC, sizeof (MAGIC) - 1);

	if (upstart_write_int (iovec, size, version))
		return -1;

	if (upstart_write_int (iovec, size, type))
		return -1;

	return 0;
}
