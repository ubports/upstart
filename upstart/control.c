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


#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <stdio.h>
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
 * Magic data we send at the front of a packet.
 **/
#define MAGIC "upstart0.1"

/**
 * INIT_DAEMON:
 *
 * Macro used in place of a pid for the init daemon, simply to make it clear
 * what we're doing.
 **/
#define INIT_DAEMON 1


/**
 * IOVEC_ADD:
 * @iov: iovec to add to,
 * @obj: object to add,
 * @objsz: size of object,
 * @bufsz: size of buffer.
 *
 * Expands to the code to add @objsz bytes from @obj to the @iov buffer,
 * without exceeding @bufsz.
 **/
#define IOVEC_ADD(iov, obj, objsz, bufsz) \
	if ((iov).iov_len + (objsz) <= (bufsz)) { \
		memcpy ((iov).iov_base + (iov).iov_len, (obj), (objsz)); \
		(iov).iov_len += (objsz); \
	} else { \
		goto invalid; \
	}

/**
 * IOVEC_READ:
 * @iov: iovec to read from,
 * @obj: object to copy into,
 * @objsz: size of object,
 * @bufsz: size of buffer.
 *
 * Expands to the code to copy @objsz bytes from @iov into @obj without
 * reading more than @bufsz bytes.  Uses the iov_len field of @iov to
 * store the current position.
 **/
#define IOVEC_READ(iov, obj, objsz, bufsz) \
	if ((iov).iov_len + (objsz) <= (bufsz)) { \
		memcpy ((obj), (iov).iov_base + (iov).iov_len, (objsz)); \
		(iov).iov_len += (objsz); \
	} else { \
		goto invalid; \
	}


/**
 * WireHdr:
 * @magic: always "upstart0.1",
 * @type: type of message.
 *
 * This header preceeds all messages on the wire, it indicates that the
 * message is one from the upstart client library and the @type of
 * message that follows.
 **/
typedef struct wire_hdr {
	char            magic[10];
	UpstartMsgType  type;
} WireHdr;

/**
 * WireJobPayload:
 * @namelen: length of @name.
 *
 * This is the payload of a message containing just a job name, the name
 * itself follows immediately after the payload.
 **/
typedef struct wire_job_payload {
	size_t namelen;
	/* char name[namelen]; */
} WireJobPayload;

/**
 * WireJobStatusPayload:
 * @goal: job goal,
 * @state: job state,
 * @process_state: process state.
 *
 * This payload follows a job payload for the JOB_STATUS message and contains
 * the status information.
 **/
typedef struct wire_job_status_payload {
	JobGoal      goal;
	JobState     state;
	ProcessState process_state;
} WireJobStatusPayload;


/* Prototypes for static functions */
static size_t upstart_addr (struct sockaddr_un *addr, pid_t pid);


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

	addrlen = __builtin_offsetof (struct sockaddr_un, sun_path) + 1;
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
		close (sock);
		nih_return_system_error (-1);
	}

	/* Always requests credentials */
	optval = 1;
	if (setsockopt (sock, SOL_SOCKET, SO_PASSCRED, &optval,
			sizeof (optval)) < 0) {
		close (sock);
		nih_return_system_error (-1);
	}

	return sock;
}


/**
 * upstart_send_msg:
 * @sock: socket to send @message on,
 * @message: message to send.
 *
 * Send @message to the running init daemon using @sock which should have
 * been opened with #upstart_open.
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
 * with #upstart_open.
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
	WireHdr            hdr;
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
	strncpy (hdr.magic, MAGIC, sizeof (hdr.magic));
	hdr.type = message->type;
	IOVEC_ADD (iov[0], &hdr, sizeof (hdr), sizeof (buf));

	/* Message type determines actual payload */
	switch (message->type) {
	case UPSTART_NO_OP:
		/* No payload */
		break;

	case UPSTART_JOB_START:
	case UPSTART_JOB_STOP:
	case UPSTART_JOB_QUERY:
	case UPSTART_JOB_UNKNOWN: {
		/* Job name */
		WireJobPayload job;

		job.namelen = strlen (message->job_start.name);
		IOVEC_ADD (iov[0], &job, sizeof (job), sizeof (buf));
		IOVEC_ADD (iov[0], message->job_start.name, job.namelen,
			   sizeof (buf));

		break;
	}
	case UPSTART_JOB_STATUS: {
		/* Job name, followed by job status */
		WireJobPayload       job;
		WireJobStatusPayload status;

		job.namelen = strlen (message->job_status.name);
		IOVEC_ADD (iov[0], &job, sizeof (job), sizeof (buf));
		IOVEC_ADD (iov[0], message->job_status.name, job.namelen,
			   sizeof (buf));

		status.goal = message->job_status.goal;
		status.state = message->job_status.state;
		status.process_state = message->job_status.process_state;
		IOVEC_ADD (iov[0], &status, sizeof (status), sizeof (buf));

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
 * #upstart_open.  Memory is allocated for the message structure and it
 * is returned, clients should use #nih_free or #upstart_free to free
 * the message.
 *
 * If you wish to know which process sent the message, usually because
 * you might want to send a response, pass a pointer for @pid.
 *
 * Returns: newly allocated message or %NULL on raised error.
 **/
UpstartMsg *
upstart_recv_msg (void  *parent,
		  int    sock,
		  pid_t *pid)
{
	UpstartMsg     *message = NULL;
	WireHdr         hdr;
	struct msghdr   msg;
	struct iovec    iov[1];
	char            buf[MAX_PACKET_SIZE];
	char            cmsg_buf[CMSG_SPACE (sizeof (struct ucred))];
	struct cmsghdr *cmsg;
	struct ucred    cred = { 0, 0, 0 };
	size_t          len;

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


	/* Make sure we received the credentials of the sending process */
	if (cred.pid == 0)
		/* No credentials passed */
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

	/* Discard truncated messages */
	if ((msg.msg_flags & MSG_TRUNC) || (msg.msg_flags & MSG_CTRUNC))
		goto invalid;


	/* Copy the header out of the message, that'll tell us what
	 * we're actually looking at.
	 */
	iov[0].iov_len = 0;
	IOVEC_READ (iov[0], &hdr, sizeof (hdr), len);
	if (strncmp (hdr.magic, MAGIC, sizeof (hdr.magic)))
		goto invalid;


	/* Allocate the message */
	message = nih_new (parent, UpstartMsg);
	message->type = hdr.type;

	/* Message type determines actual payload */
	switch (message->type) {
	case UPSTART_NO_OP:
		/* No payload */
		break;
	case UPSTART_JOB_START:
	case UPSTART_JOB_STOP:
	case UPSTART_JOB_QUERY:
	case UPSTART_JOB_UNKNOWN: {
		/* Job name */
		WireJobPayload job;

		IOVEC_READ (iov[0], &job, sizeof (job), len);
		message->job_start.name = nih_alloc (message, job.namelen + 1);
		message->job_start.name[job.namelen] = '\0';
		IOVEC_READ (iov[0], message->job_start.name, job.namelen, len);

		break;
	}
	case UPSTART_JOB_STATUS: {
		/* Job name, followed by job status */
		WireJobPayload       job;
		WireJobStatusPayload status;

		IOVEC_READ (iov[0], &job, sizeof (job), len);
		message->job_status.name = nih_alloc (message,
						      job.namelen + 1);
		message->job_status.name[job.namelen] = '\0';
		IOVEC_READ (iov[0], message->job_status.name,
			    job.namelen, len);

		IOVEC_READ (iov[0], &status, sizeof (status), len);
		message->job_status.goal = status.goal;
		message->job_status.state = status.state;
		message->job_status.process_state = status.process_state;

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
	if (message)
		nih_free (message);

	nih_return_error (NULL, UPSTART_INVALID_MESSAGE,
			  _(UPSTART_INVALID_MESSAGE_STR));
}


/**
 * upstart_free:
 * @message: message to be freed.
 *
 * Freeds the memory used by @message, this must be used instead of the
 * ordinary free function.
 **/
void
upstart_free (UpstartMsg *message)
{
	nih_free (message);
}
