/* upstart
 *
 * Copyright © 2010 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/hash.h>
#include <nih/string.h>
#include <nih/io.h>
#include <nih/option.h>
#include <nih/main.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <nih-dbus/dbus_connection.h>
#include <nih-dbus/dbus_proxy.h>

#include "dbus/upstart.h"
#include "com.ubuntu.Upstart.h"
#include "com.ubuntu.Upstart.Job.h"


/* Structure we use for tracking jobs */
typedef struct job {
	NihList entry;
	char *path;
	NihList sockets;
} Job;

/* Structure we use for tracking listening sockets */
typedef struct socket {
	NihList entry;

	union {
		struct sockaddr         addr;
		struct sockaddr_in  sin_addr;
		struct sockaddr_in6 sin6_addr;
		struct sockaddr_un  sun_addr;
	};
	socklen_t addrlen;

	int sock;
} Socket;


/* Prototypes for static functions */
static void epoll_watcher        (void *data, NihIoWatch *watch,
				  NihIoEvents events);
static void upstart_job_added    (void *data, NihDBusMessage *message,
				  const char *job);
static void upstart_job_removed  (void *data, NihDBusMessage *message,
				  const char *job);
static void job_add_socket       (Job *job, char **socket_info);
static void socket_destroy       (Socket *socket);
static void upstart_disconnected (DBusConnection *connection);
static void emit_event_reply     (Socket *sock, NihDBusMessage *message);
static void emit_event_error     (Socket *sock, NihDBusMessage *message);


/**
 * daemonise:
 *
 * Set to TRUE if we should become a daemon, rather than just running
 * in the foreground.
 **/
static int daemonise = FALSE;

/**
 * epoll_fd:
 *
 * Shared epoll file descriptor for listening on.
 **/
static int epoll_fd = -1;

/**
 * jobs:
 *
 * Jobs that we're monitoring.
 **/
static NihHash *jobs = NULL;

/**
 * upstart:
 *
 * Proxy to Upstart daemon.
 **/
static NihDBusProxy *upstart = NULL;


/**
 * options:
 *
 * Command-line options accepted by this program.
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
	char **         args;
	DBusConnection *connection;
	char **         job_class_paths;
	int             ret;

	nih_main_init (argv[0]);

	nih_option_set_synopsis (_("Bridge socket events into upstart"));
	nih_option_set_help (
		_("By default, upstart-socket-bridge does not detach from the "
		  "console and remains in the foreground.  Use the --daemon "
		  "option to have it detach."));

	args = nih_option_parser (NULL, argc, argv, options, FALSE);
	if (! args)
		exit (1);

	/* Create an epoll file descriptor for listening on; use this so
	 * we can do edge triggering rather than level.
	 */
	epoll_fd = epoll_create1 (0);
	if (epoll_fd < 0) {
		nih_fatal ("%s: %s", _("Could not create epoll descriptor"),
			   strerror (errno));
		exit (1);
	}

	NIH_MUST (nih_io_add_watch (NULL, epoll_fd, NIH_IO_READ,
				    epoll_watcher, NULL));

	/* Allocate jobs hash table */
	jobs = NIH_MUST (nih_hash_string_new (NULL, 0));

	/* Initialise the connection to Upstart */
	connection = NIH_SHOULD (nih_dbus_connect (DBUS_ADDRESS_UPSTART, upstart_disconnected));
	if (! connection) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not connect to Upstart"),
			   err->message);
		nih_free (err);

		exit (1);
	}

	upstart = NIH_SHOULD (nih_dbus_proxy_new (NULL, connection,
						  NULL, DBUS_PATH_UPSTART,
						  NULL, NULL));
	if (! upstart) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not create Upstart proxy"),
			   err->message);
		nih_free (err);

		exit (1);
	}

	/* Connect signals to be notified when jobs come and go */
	if (! nih_dbus_proxy_connect (upstart, &upstart_com_ubuntu_Upstart0_6, "JobAdded",
				      (NihDBusSignalHandler)upstart_job_added, NULL)) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not create JobAdded signal connection"),
			   err->message);
		nih_free (err);

		exit (1);
	}

	if (! nih_dbus_proxy_connect (upstart, &upstart_com_ubuntu_Upstart0_6, "JobRemoved",
				      (NihDBusSignalHandler)upstart_job_removed, NULL)) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not create JobRemoved signal connection"),
			   err->message);
		nih_free (err);

		exit (1);
	}

	/* Request a list of all current jobs */
	if (upstart_get_all_jobs_sync (NULL, upstart, &job_class_paths) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not obtain job list"),
			   err->message);
		nih_free (err);

		exit (1);
	}

	for (char **job_class_path = job_class_paths;
	     job_class_path && *job_class_path; job_class_path++)
		upstart_job_added (NULL, NULL, *job_class_path);

	nih_free (job_class_paths);

	/* Become daemon */
	if (daemonise) {
		if (nih_main_daemonise () < 0) {
			NihError *err;

			err = nih_error_get ();
			nih_fatal ("%s: %s", _("Unable to become daemon"),
				   err->message);
			nih_free (err);

			exit (1);
		}

		/* Send all logging output to syslog */
		openlog (program_name, LOG_PID, LOG_DAEMON);
		nih_log_set_logger (nih_logger_syslog);
	}

	/* Handle TERM and INT signals gracefully */
	nih_signal_set_handler (SIGTERM, nih_signal_handler);
	NIH_MUST (nih_signal_add_handler (NULL, SIGTERM, nih_main_term_signal, NULL));

	if (! daemonise) {
		nih_signal_set_handler (SIGINT, nih_signal_handler);
		NIH_MUST (nih_signal_add_handler (NULL, SIGINT, nih_main_term_signal, NULL));
	}

	ret = nih_main_loop ();

	return ret;
}


static void
epoll_watcher (void *      data,
	       NihIoWatch *watch,
	       NihIoEvents events)
{
	struct epoll_event event[1024];
	int                num_events;

	num_events = epoll_wait (epoll_fd, event, 1024, 0);
	if (num_events < 0) {
		nih_error ("%s: %s", _("Error from epoll"), strerror (errno));
		return;
	} else if (num_events == 0)
		return;

	for (int i = 0; i < num_events; i++) {
		Socket *sock = (Socket *)event[i].data.ptr;
		nih_local char **env = NULL;
		size_t env_len = 0;
		char *var;
		DBusPendingCall *pending_call;
		char buffer[INET6_ADDRSTRLEN];

		if (event[i].events & EPOLLIN)
			nih_debug ("%p EPOLLIN", sock);
		if (event[i].events & EPOLLERR)
			nih_debug ("%p EPOLLERR", sock);
		if (event[i].events & EPOLLHUP)
			nih_debug ("%p EPOLLHUP", sock);

		env = NIH_MUST (nih_str_array_new (NULL));

		switch (sock->addr.sa_family) {
		case AF_INET:
			NIH_MUST (nih_str_array_add (&env, NULL, &env_len,
							"PROTO=inet"));

			var = NIH_MUST (nih_sprintf (NULL, "PORT=%d",
							ntohs (sock->sin_addr.sin_port)));
			NIH_MUST (nih_str_array_addp (&env, NULL, &env_len,
							var));
			nih_discard (var);

			var = NIH_MUST (nih_sprintf (NULL, "ADDR=%s",
							inet_ntoa (sock->sin_addr.sin_addr)));
			NIH_MUST (nih_str_array_addp (&env, NULL, &env_len,
							var));
			nih_discard (var);
			break;
 		case AF_INET6:
			NIH_MUST (nih_str_array_add (&env, NULL, &env_len,
							"PROTO=inet6"));

			var = NIH_MUST (nih_sprintf (NULL, "PORT=%d",
							ntohs (sock->sin6_addr.sin6_port)));
			NIH_MUST (nih_str_array_addp (&env, NULL, &env_len,
							var));
			nih_discard (var);

			var = NIH_MUST (nih_sprintf (NULL, "ADDR=%s",
							inet_ntop(AF_INET6, &sock->sin6_addr.sin6_addr, buffer, INET6_ADDRSTRLEN)));

			NIH_MUST (nih_str_array_addp (&env, NULL, &env_len,
							var));
			nih_discard (var);
			break;
		case AF_UNIX:
			NIH_MUST (nih_str_array_add (&env, NULL, &env_len,
						     "PROTO=unix"));

			var = NIH_MUST (nih_sprintf (NULL, "SOCKET_PATH=%s",
						     sock->sun_addr.sun_path));
			NIH_MUST (nih_str_array_addp (&env, NULL, &env_len,
						      var));
			nih_discard (var);
			break;
		default:
			nih_assert_not_reached ();
		}

		pending_call = NIH_SHOULD (upstart_emit_event_with_file (
						   upstart, "socket", env, TRUE,
						   sock->sock,
						   (UpstartEmitEventWithFileReply)emit_event_reply,
						   (NihDBusErrorHandler)emit_event_error,
						   sock,
						   NIH_DBUS_TIMEOUT_NEVER));
		if (! pending_call) {
			NihError *err;

			err = nih_error_get ();
			nih_warn ("%s: %s", _("Could not send socket event"),
				  err->message);
			nih_free (err);
		}

		dbus_pending_call_unref (pending_call);

		// might be EPOLLIN
		// might be EPOLLERR
		// might be EPOLLHUP
	}
}


static void
upstart_job_added (void *          data,
		   NihDBusMessage *message,
		   const char *    job_class_path)
{
	nih_local NihDBusProxy *job_class = NULL;
	nih_local char ***start_on = NULL;
	nih_local char ***stop_on = NULL;
	Job *job;

	nih_assert (job_class_path != NULL);

	/* Obtain a proxy to the job */
	job_class = nih_dbus_proxy_new (NULL, upstart->connection,
					upstart->name, job_class_path,
					NULL, NULL);
	if (! job_class) {
		NihError *err;

		err = nih_error_get ();
		nih_error ("Could not create proxy for job %s: %s",
			   job_class_path, err->message);
		nih_free (err);

		return;
	}

	job_class->auto_start = FALSE;

	/* Obtain the start_on and stop_on properties of the job */
	if (job_class_get_start_on_sync (NULL, job_class, &start_on) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_error ("Could not obtain job start condition %s: %s",
			   job_class_path, err->message);
		nih_free (err);

		return;
	}

	if (job_class_get_stop_on_sync (NULL, job_class, &stop_on) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_error ("Could not obtain job stop condition %s: %s",
			   job_class_path, err->message);
		nih_free (err);

		return;
	}

	/* Free any existing record for the job (should never happen,
	 * but worth being safe).
	 */
	job = (Job *)nih_hash_lookup (jobs, job_class_path);
	if (job)
		nih_free (job);

	/* Create new record for the job */
	job = NIH_MUST (nih_new (NULL, Job));
	job->path = NIH_MUST (nih_strdup (job, job_class_path));

	nih_list_init (&job->entry);
	nih_list_init (&job->sockets);

	/* Find out whether this job listens for any socket events */
	for (char ***event = start_on; event && *event && **event; event++)
		if (! strcmp (**event, "socket"))
			job_add_socket (job, *event);
	for (char ***event = stop_on; event && *event && **event; event++)
		if (! strcmp (**event, "socket"))
			job_add_socket (job, *event);

	/* If we didn't end up with any sockets, free the job and move on */
	if (NIH_LIST_EMPTY (&job->sockets)) {
		nih_free (job);
		return;
	}

	nih_debug ("Job got added %s", job_class_path);

	nih_alloc_set_destructor (job, nih_list_destroy);
	nih_hash_add (jobs, &job->entry);
}

static void
upstart_job_removed (void *          data,
		     NihDBusMessage *message,
		     const char *    job_path)
{
	Job *job;

	nih_assert (job_path != NULL);

	job = (Job *)nih_hash_lookup (jobs, job_path);
	if (job) {
		nih_debug ("Job went away %s", job_path);
		nih_free (job);
	}
}


static void
job_add_socket (Job *  job,
		char **socket_info)
{
	Socket *sock;
	nih_local char *error = NULL;
	int     components = 0;
	struct epoll_event event;

	nih_assert (job != NULL);
	nih_assert (socket_info != NULL);
	nih_assert (! strcmp(socket_info[0], "socket"));

	sock = NIH_MUST (nih_new (job, Socket));
	memset (sock, 0, sizeof (Socket));
	sock->sock = -1;

	nih_list_init (&sock->entry);

	nih_debug ("Found socket");
	for (char **env = socket_info + 1; env && *env; env++) {
		char  *val;
		size_t name_len;

		val = strchr (*env, '=');
		if (! val) {
			nih_warn ("Ignored socket event without variable name in %s",
				  job->path);
			goto error;
		}

		name_len = val - *env;
		val++;

		if (! strncmp (*env, "PROTO", name_len)) {
			if (! strcmp (val, "inet")) {
				sock->addrlen = sizeof sock->sin_addr;
				sock->sin_addr.sin_family = AF_INET;
				sock->sin_addr.sin_addr.s_addr = INADDR_ANY;
				components = 1;
			} else if (! strcmp (val, "inet6")) {
				sock->addrlen = sizeof sock->sin6_addr;
				sock->sin6_addr.sin6_family = AF_INET6;
				sock->sin6_addr.sin6_addr = in6addr_any;
				components = 1;
			} else if (! strcmp (val, "unix")) {
				sock->addrlen = sizeof sock->sun_addr;
				sock->sun_addr.sun_family = AF_UNIX;
				components = 1;
			} else {
				nih_warn ("Ignored socket event with unknown PROTO=%s in %s",
					  val, job->path);
				goto error;
			}

		} else if (! strncmp (*env, "PORT", name_len)
		           && (sock->sin_addr.sin_family == AF_INET)) {
			sock->sin_addr.sin_port = htons (atoi (val));
			components--;
		} else if (! strncmp (*env, "PORT", name_len)
		           && (sock->sin6_addr.sin6_family == AF_INET6)) {
			sock->sin6_addr.sin6_port = htons (atoi (val));
			components--;
		} else if (! strncmp (*env, "ADDR", name_len)
		           && (sock->sin_addr.sin_family == AF_INET)) {
			if (inet_aton (val, &(sock->sin_addr.sin_addr)) == 0) {
				nih_warn ("Ignored socket event with invalid ADDR=%s in %s",
				          val, job->path);
				goto error;
			}
		} else if (! strncmp (*env, "ADDR", name_len)
		           && (sock->sin6_addr.sin6_family == AF_INET6)) {
			if (inet_pton (AF_INET6, val, &(sock->sin6_addr.sin6_addr)) == 0) {
				nih_warn ("Ignored socket event with invalid ADDR=%s in %s",
				          val, job->path);
				goto error;
			}
		} else if (! strncmp (*env, "PATH", name_len)
		           && (sock->sun_addr.sun_family == AF_UNIX)) {
			strncpy (sock->sun_addr.sun_path, val,
			         sizeof sock->sun_addr.sun_path);

			if (sock->sun_addr.sun_path[0] == '@')
				sock->sun_addr.sun_path[0] = '\0';

			components--;

		} else {
			nih_warn ("Ignored socket event with unknown variable %.*s in %s",
				  (int)name_len, *env, job->path);
			goto error;
		}
	}

	/* Missing any required components? */
	if (components) {
		nih_warn ("Ignored incomplete socket event in %s",
			  job->path);
		goto error;
	}

	/* Let's try and set this baby up */
	sock->sock = socket (sock->addr.sa_family, SOCK_STREAM, 0);
	if (sock->sock < 0) {
		nih_warn ("Failed to create socket in %s: %s",
			  job->path, strerror (errno));
		goto error;
	}

	int opt = 1;
	if (setsockopt (sock->sock, SOL_SOCKET, SO_REUSEADDR,
			&opt, sizeof opt) < 0) {
		nih_warn ("Failed to set socket reuse in %s: %s",
			  job->path, strerror (errno));
		goto error;
	}

	if (bind (sock->sock, &sock->addr, sock->addrlen) < 0) {
		nih_warn ("Failed to bind socket in %s: %s",
			  job->path, strerror (errno));
		goto error;
	}

	if (listen (sock->sock, SOMAXCONN) < 0) {
		nih_warn ("Failed to listen on socket in %s: %s",
			  job->path, strerror (errno));
		goto error;
	}

	/* We have a listening socket, now we want to be notified when someone
	 * connects; but we just want one notification, we don't want to get
	 * a DDoS of wake-ups while waiting for the service to start.
	 *
	 * The solution is to use epoll in edge-triggered mode, this will
	 * fire only on initial connection until a new one comes in.
	 */
	event.events = EPOLLIN | EPOLLET;
	event.data.ptr = sock;

	if (epoll_ctl (epoll_fd, EPOLL_CTL_ADD, sock->sock, &event) < 0) {
		nih_warn ("Failed to watch socket in %s: %s",
			  job->path, strerror (errno));
		goto error;
	}

	/* Okay then, add to the job */
	nih_alloc_set_destructor (sock, socket_destroy);
	nih_list_add (&job->sockets, &sock->entry);

	return;

error:
	if (sock->sock != -1)
		close (sock->sock);
	nih_free (sock);
}

static void
socket_destroy (Socket *sock)
{
	epoll_ctl (epoll_fd, EPOLL_CTL_DEL, sock->sock, NULL);
	close (sock->sock);

	nih_list_destroy (&sock->entry);
}


static void
upstart_disconnected (DBusConnection *connection)
{
	nih_fatal (_("Disconnected from Upstart"));
	nih_main_loop_exit (1);
}


static void
emit_event_reply (Socket *        sock,
		  NihDBusMessage *message)
{
	nih_debug ("Event completed");
}

static void
emit_event_error (Socket *        sock,
		  NihDBusMessage *message)
{
	NihError *err;

	err = nih_error_get ();
	nih_warn ("%s: %s", _("Error emitting socket event"), err->message);
	nih_free (err);
}
