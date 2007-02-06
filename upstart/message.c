/* upstart
 *
 * message.c - control messages and socket opening
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/io.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <upstart/message.h>
#include <upstart/wire.h>
#include <upstart/errors.h>


/* Prototypes for static functions */
static size_t                upstart_addr            (struct sockaddr_un *addr,
						      pid_t pid);
static UpstartMessageHandler upstart_message_handler (pid_t pid,
						      UpstartMessageType type,
						      UpstartMessage *handlers);


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
	if (pid == UPSTART_INIT_DAEMON) {
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
	if (bind (sock, (struct sockaddr *)&addr, addrlen) < 0)
		goto error;

	/* Always requests credentials */
	optval = 1;
	if (setsockopt (sock, SOL_SOCKET, SO_PASSCRED, &optval,
			sizeof (optval)) < 0)
		goto error;

	return sock;

error:
	nih_error_raise_system ();
	close (sock);
	return -1;
}


/**
 * upstart_message_new:
 * @parent: parent of new structure,
 * @pid: process to send message to,
 * @type: type of message.
 *
 * Allocates an NihIoMessage structure using nih_alloc() that can be
 * immediately sent down a socket with nih_io_message_send() or queued
 * for later sending with nih_io_send_message().
 *
 * The arguments after @type depend on the type of message being sent,
 * see the documentation for UpstartMessageHandler for more details.
 *
 * The destination process id is used to construct the address member of
 * the message, it is also stored in the int_data member for error handling.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * Returns: newly allocated message, or NULL if insufficient memory.
 **/
NihIoMessage *
upstart_message_new (const void         *parent,
		     pid_t               pid,
		     UpstartMessageType  type,
		     ...)
{
	NihIoMessage *message;
	va_list       args;

	nih_assert (pid > 0);

	message = nih_io_message_new (parent);
	if (! message)
		return NULL;

	message->int_data = pid;

	/* Fill in the address structure */
	message->addr = nih_new (message, struct sockaddr_un);
	if (! message->addr)
		goto error;

	message->addrlen = upstart_addr ((struct sockaddr_un *)message->addr,
					 pid);

	/* All messages begin with a header that indicates the type of the
	 * following message.
	 */
	if (upstart_push_header (message, type))
		goto error;

	/* Message type determines arguments and message payload */
	va_start (args, type);

	switch (type) {
	case UPSTART_NO_OP:
	case UPSTART_JOB_LIST:
	case UPSTART_JOB_LIST_END:
	case UPSTART_WATCH_JOBS:
	case UPSTART_UNWATCH_JOBS:
	case UPSTART_WATCH_EVENTS:
	case UPSTART_UNWATCH_EVENTS:
		break;
	case UPSTART_JOB_START:
	case UPSTART_JOB_STOP:
	case UPSTART_JOB_QUERY:
	case UPSTART_JOB_UNKNOWN:
		if (upstart_push_packv (message, "s", args))
			goto error;

		break;
	case UPSTART_JOB_STATUS:
		if (upstart_push_packv (message, "siiiis", args))
			goto error;

		break;
	case UPSTART_EVENT_QUEUE:
	case UPSTART_EVENT:
		if (upstart_push_packv (message, "saa", args))
			goto error;

		break;
	case UPSTART_SHUTDOWN:
		if (upstart_push_packv (message, "s", args))
			goto error;

		break;
	default:
		nih_assert_not_reached ();
	}

	va_end (args);

	return message;

error:
	nih_free (message);
	return NULL;
}


/**
 * upstart_message_handler:
 * @pid: origin process id,
 * @type: message type received,
 * @handlers: list of handlers.
 *
 * Looks for a handler for the message @type received from process @pid
 * in the @handlers list given, the final entry of which should have NULL
 * as the handler function pointer.
 *
 * Returns: handler function, or NULL if none found.
 **/
static UpstartMessageHandler
upstart_message_handler (pid_t               pid,
			 UpstartMessageType  type,
			 UpstartMessage     *handlers)
{
	UpstartMessage *handler;

	nih_assert (handlers != NULL);

	for (handler = handlers; handler->handler; handler++) {
		if ((handler->pid != -1) && (handler->pid != pid))
			continue;
		if ((handler->type != -1) && (handler->type != type))
			continue;

		return handler->handler;
	}

	return NULL;
}

/**
 * upstart_message_handle:
 * @parent: parent of any allocated strings,
 * @message: message to be handled,
 * @handlers: list of handlers,
 * @data: pointer to pass to handler.
 *
 * Handles an NihIoMessage received from a socket, either directly through
 * nih_io_message_recv() or taken from a queue of messages with
 * nih_io_read_message().
 *
 * The message is decoded, raising UPSTART_MESSAGE_INVALID if the message
 * was invalid or UPSTART_MESSAGE_ILLEGAL if the origin is not permitted
 * to communicate.
 *
 * Once decoded, the appropriate function from the @handlers list is called,
 * passing the origin of the message, type, and a variable number of
 * arguments that depend on the message type.
 *
 * The handler function pointer of the last entry in the @handlers list
 * should be NULL.
 *
 * If no handler function can be found, UPSTART_MESSAGE_UNKNOWN is raised.
 *
 * If you only require that one message handler function be called, which
 * examines the type before retrieving arguments, use
 * upstart_message_handle_using() instead.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for any strings allocated.  When
 * @parent is freed, those strings will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * Returns: return value from handler on success, negative value
 * on raised error.
 **/
int
upstart_message_handle (const void     *parent,
			NihIoMessage   *message,
			UpstartMessage *handlers,
			void           *data)
{
	UpstartMessageType      type;
	UpstartMessageHandler   handler;
	struct cmsghdr        **ptr;
	struct ucred            cred = { 0, 0, 0 };
	int                     ret;

	nih_assert (message != NULL);
	nih_assert (handlers != NULL);

	/* First process the control headers; we require that any message
	 * contain the credentials of the sending process.
	 */
	for (ptr = message->control; *ptr; ptr++) {
		if (((*ptr)->cmsg_level == SOL_SOCKET)
		    && ((*ptr)->cmsg_type == SCM_CREDENTIALS)) {
			/* Sender credentials */
			if ((*ptr)->cmsg_len < CMSG_LEN (sizeof (cred)))
				goto invalid;

			memcpy (&cred, CMSG_DATA (*ptr), sizeof (cred));
		}

		/* FIXME receive SCM_RIGHTS fds, close if we're not
		 * expecting them!
		 */
	}

	/* Check the origin of the message, this is a safety trap so we
	 * don't even bother parsing memory if the process shouldn't be able
	 * to talk to us.
	 *
	 * Only the init daemon accepts messages from any process, others
	 * will only accept messages from the init daemon or themselves.
	 *
	 * In addition, we only permit messages to come from a process
	 * running as root or our own user id (though this may be relaxed
	 * for the init daemon later).
	 */
	if (! upstart_disable_safeties) {
		if (cred.pid == 0)
			goto illegal;

		if ((cred.pid != UPSTART_INIT_DAEMON)
		    && (cred.pid != getpid ())
		    && (getpid () != UPSTART_INIT_DAEMON))
			goto illegal;

		if ((cred.uid != 0) && (cred.uid != getuid ()))
			goto illegal;
	}


	/* Read the header from the message, which tells us what type of
	 * message follows.
	 */
	if (upstart_pop_header (message, &type))
		goto invalid;

	/* Obtain the handler from the table given, if we don't find one,
	 * we raise an error.
	 */
	handler = upstart_message_handler (cred.pid, type, handlers);
	if (! handler) {
		nih_error_raise (UPSTART_MESSAGE_UNKNOWN,
				 _(UPSTART_MESSAGE_UNKNOWN_STR));
		return -1;
	}

	/* Message type determines message payload and thus
	 * handler arguments
	 */
	switch (type) {
	case UPSTART_NO_OP:
	case UPSTART_JOB_LIST:
	case UPSTART_JOB_LIST_END:
	case UPSTART_WATCH_JOBS:
	case UPSTART_UNWATCH_JOBS:
	case UPSTART_WATCH_EVENTS:
	case UPSTART_UNWATCH_EVENTS:
		ret = handler (data, cred.pid, type);
		break;
	case UPSTART_JOB_START:
	case UPSTART_JOB_STOP:
	case UPSTART_JOB_QUERY:
	case UPSTART_JOB_UNKNOWN: {
		char *name = NULL;

		if (upstart_pop_pack (message, parent, "s", &name)) {
			if (name)
				nih_free (name);

			goto invalid;
		}

		if (! name)
			goto invalid;

		ret = handler (data, cred.pid, type, name);
		break;
	}
	case UPSTART_JOB_STATUS: {
		char *name = NULL, *description = NULL;
		int   goal, state, process_state, pid;

		if (upstart_pop_pack (message, parent, "siiiis", &name, &goal,
				      &state, &process_state, &pid,
				      &description)) {
			if (name)
				nih_free (name);
			if (description)
				nih_free (description);

			goto invalid;
		}

		if (! name) {
			if (description)
				nih_free (description);

			goto invalid;
		}

		ret = handler (data, cred.pid, type, name, goal, state,
			       process_state, pid, description);
		break;
	}
	case UPSTART_EVENT_QUEUE:
	case UPSTART_EVENT: {
		char  *name = NULL;
		char **args = NULL, **env = NULL;

		if (upstart_pop_pack (message, parent, "saa",
				      &name, &args, &env)) {
			if (name)
				nih_free (name);
			if (args)
				nih_free (args);
			if (env)
				nih_free (env);

			goto invalid;
		}

		if (! name)
			goto invalid;

		ret = handler (data, cred.pid, type, name, args, env);
		break;
	}
	case UPSTART_SHUTDOWN: {
		char *name = NULL;

		if (upstart_pop_pack (message, parent, "s", &name)) {
			if (name)
				nih_free (name);

			goto invalid;
		}

		if (! name)
			goto invalid;

		ret = handler (data, cred.pid, type, name);
		break;
	}
	default:
		goto invalid;
	}

	return ret;

illegal:
	nih_error_raise (UPSTART_MESSAGE_ILLEGAL,
			 _(UPSTART_MESSAGE_ILLEGAL_STR));
	return -1;
invalid:
	nih_error_raise (UPSTART_MESSAGE_INVALID,
			 _(UPSTART_MESSAGE_INVALID_STR));
	return -1;
}

/**
 * upstart_message_handle_using:
 * @parent: parent of any allocated strings,
 * @message: message to be handled,
 * @handler: handler function,
 * @data: pointer to pass to @handler.
 *
 * Handles an NihIoMessage received from a socket, either directly through
 * nih_io_message_recv() or taken from a queue of messages with
 * nih_io_read_message().
 *
 * The message is decoded, raising UPSTART_MESSAGE_INVALID if the message
 * was invalid.
 *
 * Once decoded, @handler is called passing the origin of the message, type,
 * and a variable number of arguments that depend on the message type.
 * This function must examine its type argument before decoding any further
 * arguments.
 *
 * Where multiple types are accepted by a handler function, it's often more
 * elegant to use a message handler table and upstart_message_handle() to
 * dispatch each type to a specialist function,
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for any strings allocated.  When
 * @parent is freed, those strings will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * Returns: return value from handler on success, negative value
 * on raised error.
 **/
int
upstart_message_handle_using (const void            *parent,
			      NihIoMessage          *message,
			      UpstartMessageHandler  handler,
			      void                  *data)
{
	UpstartMessage handlers[2] = { { -1, -1, handler},
				       UPSTART_MESSAGE_LAST };

	nih_assert (message != NULL);
	nih_assert (handler != NULL);

	return upstart_message_handle (parent, message, handlers, data);
}


/**
 * upstart_message_reader:
 * @handlers: list of handlers,
 * @io: NihIo with message to read,
 * @buf: buffer of message data,
 * @len: bytes in @buf.
 *
 * This I/O reader may be associated with any message in message mode to
 * parse and handle incoming messages, according to the list of handlers
 * given in @handlers.
 *
 * The handler function pointer of the last entry in the @handlers list
 * should be NULL.  When associating this with an NihIo structure, pass
 * the handlers list as the data argument.
 *
 * Because these handlers are called within the main loop, they should
 * take care to handle any errors raised.  The data argument in the
 * handler call will be set from the data member of @io.
 *
 * Any strings allocated are given the message received as the parent,
 * which is automatically freed after the handler has been called.  If you
 * wish to keep the strings, reparent them with nih_alloc_reparent().
 **/
void
upstart_message_reader (UpstartMessage *handlers,
			NihIo          *io,
			const char     *buf,
			size_t          len)
{
	NihIoMessage *message;

	nih_assert (handlers != NULL);
	nih_assert (io != NULL);
	nih_assert (io->type == NIH_IO_MESSAGE);
	nih_assert (buf != NULL);
	nih_assert (len > 0);

	message = nih_io_read_message (NULL, io);
	nih_assert (message != NULL);

	if (upstart_message_handle (message, message,
				    handlers, io->data) < 0) {;
		NihError *err;

		err = nih_error_get ();
		nih_error (_("Error while handling control message: %s"),
			   err->message);
		nih_free (err);
	}

	nih_free (message);
}
