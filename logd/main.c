/* upstart
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

#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/signal.h>
#include <nih/io.h>
#include <nih/main.h>
#include <nih/option.h>
#include <nih/error.h>
#include <nih/logging.h>


/**
 * LOG_FILE:
 *
 * File we write log messages to; we keep trying to open this until it
 * succeeds.
 **/
#define LOG_FILE "/var/log/boot"


/* Prototypes for static functions */
static NihIoWatch *open_logging    (void);
static void        logging_watcher (void *data, NihIoWatch *watch,
				    NihIoEvents events);
static void        logging_reader  (void *data, NihIo *io,
				    const char *buf, size_t len);
static void        line_reader     (const char *name, NihIo *io,
				    const char *buf, size_t len);


/**
 * log_buffer:
 *
 * Buffer we use to hold log file contents until we can write them.
 **/
static NihIoBuffer *log_buffer = NULL;

/**
 * log_file:
 *
 * Open log file, this remains NULL until it's opened successfully.
 **/
static FILE *log_file = NULL;


/**
 * daemonise:
 *
 * This is set to TRUE if we should become a daemon, rather than just
 * running in the foreground.
 **/
static int daemonise = FALSE;

/**
 * options:
 *
 * Command-line options accepted for all arguments.
 **/
static NihOption options[] = {
	{ 0, "daemon", N_("Detach and run in the background"),
	  NULL, NULL, &daemonise, NULL },

	NIH_OPTION_LAST
};


int
main (int   argc,
      char *argv[])
{
	char **args;
	int    ret;

	nih_main_init (argv[0]);

	args = nih_option_parser (NULL, argc, argv, options, FALSE);
	if (! args)
		exit (1);

	if (args[0] != NULL) {
		fprintf (stderr, _("%s: unexpected argument\n"), program_name);
		nih_main_suggest_help ();
		exit (1);
	}

	/* Become daemon */
	if (daemonise)
		nih_main_daemonise ();


	/* Send all logging output to syslog */
	openlog (program_name, LOG_CONS | LOG_PID, LOG_DAEMON);
	nih_log_set_logger (nih_logger_syslog);

	/* Handle TERM signal gracefully */
	nih_signal_set_handler (SIGTERM, nih_signal_handler);
	nih_signal_add_handler (NULL, SIGTERM, nih_main_term_signal, NULL);

	/* Open the logging socket */
	if (! open_logging ()) {
		NihError *err;

		err = nih_error_get ();
		nih_error (_("Unable to open listening socket: %s"),
			   err->message);
		nih_free (err);

		exit (1);
	}

	/* Signify that we're ready to receive events */
	raise (SIGSTOP);

	ret = nih_main_loop ();

	return ret;
}


/**
 * open_logging:
 *
 * Open a socket to listen for logging requests from the init daemon,
 * we accept connections on this socket and expect to read the name of
 * the daemon we are logging before reading the lines.
 *
 * Returns: NihIoWatch structure or NULL on raised error.
 **/
static NihIoWatch *
open_logging (void)
{
	struct sockaddr_un  addr;
	size_t              addrlen;
	int                 sock;
	NihIoWatch         *watch;

	/* Need a unix stream socket */
	sock = socket (PF_UNIX, SOCK_STREAM, 0);
	if (sock < 0)
		nih_return_system_error (NULL);

	/* Use the abstract namespace */
	addr.sun_family = AF_UNIX;
	addr.sun_path[0] = '\0';

	addrlen = offsetof (struct sockaddr_un, sun_path) + 1;
	addrlen += snprintf (addr.sun_path + 1, sizeof (addr.sun_path) - 1,
			     "/com/ubuntu/upstart/logd");

	/* Bind to the address */
	if (bind (sock, (struct sockaddr *)&addr, addrlen) < 0) {
		nih_error_raise_system ();
		close (sock);
		return NULL;
	}

	/* Listen for connections */
	if (listen (sock, SOMAXCONN) < 0) {
		nih_error_raise_system ();
		close (sock);
		return NULL;
	}

	/* Watch for connections in the main loop */
	watch = nih_io_add_watch (NULL, sock, NIH_IO_READ,
				  logging_watcher, NULL);
	if (! watch) {
		errno = ENOMEM;
		nih_error_raise_system ();
		close (sock);
		return NULL;
	}

	return watch;
}

/**
 * logging_watcher:
 * @data: not used,
 * @watch: NihIoWatch for which event occurred,
 * @events: events that occurred.
 *
 * This function is called whenever we can accept new connections on the
 * logging socket, or whenever there's an error of some kind.
 **/
static void
logging_watcher (void        *data,
		 NihIoWatch  *watch,
		 NihIoEvents  events)
{
	NihIo *io;
	int    sock;

	nih_assert (watch != NULL);
	nih_assert (events == NIH_IO_READ);

	/* Accept the connection */
	sock = accept (watch->fd, NULL, NULL);
	if (sock < 0) {
		nih_error (_("Unable to accept connection: %s"),
			   strerror (errno));
		return;
	}

	/* Create an NihIo structure for the child */
	io = nih_io_reopen (NULL, sock, logging_reader, NULL, NULL, NULL);
	if (! io) {
		nih_error (_("Insufficient memory to accept child"));
		close (sock);
		return;
	}
}

/**
 * logging_reader:
 * @data: not used,
 * @io: NihIo with data to be read,
 * @buf: buffer data is available in,
 * @len: bytes in @buf.
 *
 * This function is called when there is data available to be read from
 * a logging connection, this only takes care of reading the child name
 * from the socket and then adjusts the watch to call line_reader()
 * instead.
 **/
static void
logging_reader (void       *data,
		NihIo      *io,
		const char *buf,
		size_t      len)
{
	size_t  namelen;
	char   *name;

	nih_assert (io != NULL);
	nih_assert (buf != NULL);
	nih_assert (len > 0);

	if (len < sizeof (size_t))
		return;

	/* Read a size_t from the front of the buffer which is the length
	 * of the name.  Don't read the name until it's all there.
	 */
	memcpy (&namelen, buf, sizeof (size_t));
	if (len < (sizeof (size_t) + namelen))
		return;

	/* Read the size and then the name from the buffer */
	NIH_MUST (name = nih_io_read (NULL, io, sizeof (size_t)));
	nih_free (name);
	NIH_MUST (name = nih_io_read (io, io, namelen));

	/* Change the functions called and pass the name */
	io->reader = (NihIoReader)line_reader;
	io->data = name;

	/* If there's still data, call the line reader */
	if (io->recv_buf->size)
		line_reader (name, io, io->recv_buf->buf, io->recv_buf->size);
}

/**
 * line_reader:
 * @name: name of daemon,
 * @io: NihIo with data to be read,
 * @buf: buffer data is available in,
 * @len: bytes in @buf.
 *
 * This function is called when there is data available to be read from
 * a connection to a daemon being logged.  We read lines at a time and
 * handle them appropriately.
 **/
static void
line_reader (const char *name,
	     NihIo      *io,
	     const char *buf,
	     size_t      len)
{
	char *line;

	nih_assert (name != NULL);
	nih_assert (io != NULL);
	nih_assert (buf != NULL);
	nih_assert (len > 0);

	/* Read lines from the buffer */
	while ((line = nih_io_get (NULL, io, "\n")) != NULL) {
		time_t     now;
		struct tm *tm;
		char       stamp[32];

		/* Format a time stamp for the log */
		now = time (NULL);
		tm = localtime (&now);
		strftime (stamp, sizeof (stamp), "%b %e %H:%M:%S", tm);

		/* Have a go at opening the log file again */
		if (! log_file)
			log_file = fopen (LOG_FILE, "w");

		/* Can we flush the buffer into the file? */
		if (log_file && log_buffer) {
			if ((fwrite (log_buffer->buf, 1, log_buffer->len,
				     log_file) < 0)
			    || (fflush (log_file) < 0)) {
				nih_error (_("Error occurred while writing "
					     "to log file: %s"),
					   strerror (errno));

				fclose (log_file);
				log_file = NULL;
			} else {
				nih_free (log_buffer);
				log_buffer = NULL;
			}
		}

		/* Write the line to the file if it's open and flush */
		if (log_file) {
			if ((fprintf (log_file, "%s %s: %s\n",
				      stamp, name, line) < 0)
			    || (fflush (log_file) < 0)) {
				nih_error (_("Error occurred while writing "
					     "to log file: %s"),
					   strerror (errno));

				fclose (log_file);
				log_file = NULL;
			}
		}

		/* Write the line to memory if we don't have the log file
		 * open at this point (failed to open or failed to write)
		 */
		if (! log_file) {
			if (! log_buffer)
				NIH_MUST (log_buffer
					  = nih_io_buffer_new (NULL));

			NIH_MUST (nih_io_buffer_push (log_buffer, stamp,
						      strlen (stamp)) == 0);
			NIH_MUST (nih_io_buffer_push (log_buffer, " ",
						      1) == 0);
			NIH_MUST (nih_io_buffer_push (log_buffer, name,
						      strlen (name)) == 0);
			NIH_MUST (nih_io_buffer_push (log_buffer, ": ",
						      2) == 0);
			NIH_MUST (nih_io_buffer_push (log_buffer, line,
						      strlen (line)) == 0);
			NIH_MUST (nih_io_buffer_push (log_buffer, "\n",
						      1) == 0);
		}

		nih_free (line);
	}
}

