/* upstart
 *
 * Copyright © 2013 Canonical Ltd.
 * Author: James Hunt <james.hunt@canonical.com>
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

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

/**
 * Job:
 *
 * @entry: list header,
 * @path: D-Bus path for a job.
 *
 * Representation of an Upstart Job.
 *
 **/
typedef struct job {
	NihList entry;
	char *path;
} Job;

/**
 * Socket:
 *
 * @addr/sun_addr: socket address,
 * @addrlen: length of sun_addr,
 * @sock: file descriptor of socket,
 * @watch: IO Watch used to detect client activity.
 *
 * Representation of a socket(2).
 **/
typedef struct socket {
	union {
		struct sockaddr     addr;      /* Generic type */
		struct sockaddr_un  sun_addr;  /* local/domain/unix/abstract socket */
	};
	socklen_t   addrlen;

	int         sock;
	NihIoWatch *watch;
} Socket;

/**
 * ClientConnection:
 *
 * @sock: socket client connected via,
 * @fd: file descriptor client connected on,
 * @ucred: client credentials.
 *
 * Representation of a connected client.
 **/
typedef struct client_connection {
	Socket        *sock;
	int            fd;
	struct ucred   ucred;
} ClientConnection;

static void upstart_job_added    (void *data, NihDBusMessage *message,
				  const char *job);
static void upstart_job_removed  (void *data, NihDBusMessage *message,
				  const char *job);
static void upstart_connect      (void);
static void upstart_disconnected (DBusConnection *connection);

static Socket *create_socket (void *parent);

static void socket_watcher (Socket *sock, NihIoWatch *watch,
			   NihIoEvents events);

static void socket_reader (ClientConnection *client, NihIo *io,
			   const char *buf, size_t len);

static void close_handler (ClientConnection *client, NihIo *io);

static void emit_event_error (void *data, NihDBusMessage *message);

static void signal_handler (void *data, NihSignal *signal);

static void cleanup (void);

/**
 * daemonise:
 *
 * Set to TRUE if we should become a daemon, rather than just running
 * in the foreground.
 **/
static int daemonise = FALSE;

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
 * event_name:
 *
 * Name of event this bridge emits.
 **/
static char *event_name = NULL;

/**
 * Unix (local) domain socket path.
 *
 * Abstract sockets will have '@' as first character.
 **/
static char *socket_path = NULL;

/**
 * socket_type:
 * 
 * Type of socket supported by this bridge.
 **/
static char *socket_type = "unix";

/**
 * socket_name:
 *
 * Human-readable socket name in form:
 *
 * unix:[@]/some/path
 **/
static char *socket_name = NULL;

/**
 * sock:
 *
 * Socket this bridge listens on.
 **/
static Socket *sock = NULL;

/**
 * any_user:
 *
 * If FALSE, only accept connections from the same uid as
 * user the bridge runs as.
 * If TRUE, accept connections from any user.
 **/
static int any_user = FALSE;

/**
 * options:
 *
 * Command-line options accepted by this program.
 **/
static NihOption options[] = {
	{ 0, "daemon", N_("Detach and run in the background"),
	  NULL, NULL, &daemonise, NULL },

	{ 0, "event", N_("specify name of event to emit on receipt of name=value pair"),
		NULL, "EVENT", &event_name, NULL },

	{ 0, "any-user", N_("allow any user to connect"),
		NULL, NULL, &any_user, NULL },

	{ 0, "path", N_("specify path for local/abstract socket to use"),
		NULL, "PATH", &socket_path, NULL },

	NIH_OPTION_LAST
};


/**
 * signal_handler:
 * @data: unused,
 * @signal: signal caught.
 *
 * Called when we receive the TERM/INT signal.
 **/
static void
signal_handler (void      *data,
	      NihSignal *signal)
{
	nih_assert (signal != NULL);

	cleanup ();

	nih_main_loop_exit (0);
}

/**
 * cleanup:
 *
 * Perform final operations before exit.
 **/
static void
cleanup (void)
{
	nih_assert (sock);

	close (sock->sock);

	if (sock->sun_addr.sun_path[0] != '@')
		unlink (sock->sun_addr.sun_path);
}

int
main (int   argc,
      char *argv[])
{
	char    **args;
	int       ret;

	nih_main_init (argv[0]);

	nih_option_set_synopsis (_("Local socket Upstart Bridge"));
	nih_option_set_help (
		_("By default, this bridge does not detach from the "
		  "console and remains in the foreground.  Use the --daemon "
		  "option to have it detach."));

	args = nih_option_parser (NULL, argc, argv, options, FALSE);
	if (! args)
		exit (1);

	if (! event_name) {
		nih_fatal ("%s", _("Must specify event name"));
		exit (1);
	}

	/* Allocate jobs hash table */
	jobs = NIH_MUST (nih_hash_string_new (NULL, 0));

	sock = create_socket (NULL);
	if (! sock) {
		nih_fatal ("%s %s",
			_("Failed to create socket"),
			socket_name);
		exit (1);
	}

	nih_debug ("Connected to socket '%s' on fd %d", socket_name, sock->sock);

	upstart_connect ();

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

	nih_signal_set_handler (SIGTERM, nih_signal_handler);
	NIH_MUST (nih_signal_add_handler (NULL, SIGTERM, signal_handler, NULL));

	nih_signal_set_handler (SIGINT, nih_signal_handler);
	NIH_MUST (nih_signal_add_handler (NULL, SIGINT, signal_handler, NULL));

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
upstart_job_added (void            *data,
		   NihDBusMessage  *message,
		   const char      *job_class_path)
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

	nih_debug ("Job got added %s", job_class_path);

	nih_alloc_set_destructor (job, nih_list_destroy);

	/* Add all jobs */
	nih_hash_add (jobs, &job->entry);
}

static void
upstart_job_removed (void            *data,
		     NihDBusMessage  *message,
		     const char      *job_path)
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
upstart_connect (void)
{
	DBusConnection    *connection;
	char            **job_class_paths;

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

	nih_debug ("Connected to Upstart");

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
}

static void
upstart_disconnected (DBusConnection *connection)
{
	nih_fatal (_("Disconnected from Upstart"));
	nih_main_loop_exit (1);
}

/**
 * socket_watcher:
 *
 * @sock: Socket,
 * @watch: IO watch,
 * @event: events that occurred.
 *
 * Called when activity is received for socket fd.
 **/
static void
socket_watcher (Socket *sock,
		NihIoWatch *watch,
		NihIoEvents events)
{
	struct sockaddr     client_addr;
	socklen_t           client_len;
	nih_local char     *buffer = NULL;
	ClientConnection   *client;
	size_t              len;

	nih_assert (sock);
	nih_assert (watch);

	client = NIH_MUST (nih_new (NULL, ClientConnection));
	memset (client, 0, sizeof (ClientConnection));
	client->sock = sock;

	client_len = sizeof (struct sockaddr);

	client->fd = accept (sock->sock, (struct sockaddr *)&client_addr, &client_len);

	if (client->fd < 0) {
		nih_fatal ("%s %s %s", _("Failed to accept socket"),
			  socket_name, strerror (errno));
		return;
	}

	len = sizeof (client->ucred);

	/* Establish who is connected to the other end */
	if (getsockopt (client->fd, SOL_SOCKET, SO_PEERCRED, &client->ucred, &len) < 0)
		goto error;

	if (! any_user && client->ucred.uid != geteuid ()) {
		nih_warn ("Ignoring request from uid %u (gid %u, pid %u)",
				(unsigned int)client->ucred.uid,
				(unsigned int)client->ucred.gid,
				(unsigned int)client->ucred.pid);
		close (client->fd);
		return;
	}

	nih_debug ("Client connected via local socket to %s: "
			"pid %d (uid %d, gid %d)",
			socket_name,
			client->ucred.pid,
			client->ucred.uid,
			client->ucred.gid);

	/* Wait for remote end to send data */
	NIH_MUST (nih_io_reopen (sock, client->fd,
			NIH_IO_STREAM, 
			(NihIoReader)socket_reader, 
			(NihIoCloseHandler)close_handler,
			NULL,
			client));
	return;

error:
	nih_warn ("%s %s: %s",
			_("Cannot establish peer credentials for socket"),
			socket_name, strerror (errno));
}

/**
 * socket_reader:
 *
 * @client: client connection,
 * @io: NihIo,
 * @buf: data read from client,
 * @len: length of @buf.
 *
 * NihIoReader function called when data has been read from the
 * connected client.
 **/
static void
socket_reader (ClientConnection  *client,
	       NihIo             *io,
	       const char        *buf,
	       size_t             len)
{
	DBusPendingCall    *pending_call;
	nih_local char    **env = NULL;
	nih_local char     *var = NULL;
	size_t              used_len = 0;
	int                 i;

	nih_assert (sock);
	nih_assert (client);
	nih_assert (io);
	nih_assert (buf);

	/* Ignore messages that are too short */
	if (len < 2)
		goto error;

	/* Ensure the data is a name=value pair */
	if (! strchr (buf, '=') || buf[0] == '=')
		goto error;

	/* Remove line endings */
	for (i = 0, used_len = len; i < 2; i++) {
		if (buf[used_len-1] == '\n' || buf[used_len-1] == '\r')
			used_len--;
		else
			break;
	}

	/* Second check to ensure overly short messages are ignored */
	if (used_len < 2)
		goto error;

	/* Construct the event environment.
	 *
	 * Note that although the client could conceivably specify one
	 * of the variables below _itself_, if the intent is malicious
	 * it will be thwarted since although the following example
	 * event is valid...
	 *
	 *    foo BAR=BAZ BAR=MALICIOUS
	 *
	 * ... environment variable matching only happens for the first
	 * occurence of a variable. In summary, a malicious client
	 * cannot spoof the standard variables we set.
	 */
	env = NIH_MUST (nih_str_array_new (NULL));

	/* Specify type to allow for other types to be added in the future */
	var = NIH_MUST (nih_sprintf (NULL, "SOCKET_TYPE=%s", socket_type));
	NIH_MUST (nih_str_array_addp (&env, NULL, NULL, var));

	var = NIH_MUST (nih_sprintf (NULL, "SOCKET_VARIANT=%s",
				sock->sun_addr.sun_path[0] ? "named" : "abstract"));
	NIH_MUST (nih_str_array_addp (&env, NULL, NULL, var));

	var = NIH_MUST (nih_sprintf (NULL, "CLIENT_UID=%u", (unsigned int)client->ucred.uid));
	NIH_MUST (nih_str_array_addp (&env, NULL, NULL, var));

	var = NIH_MUST (nih_sprintf (NULL, "CLIENT_GID=%u", (unsigned int)client->ucred.gid));
	NIH_MUST (nih_str_array_addp (&env, NULL, NULL, var));

	var = NIH_MUST (nih_sprintf (NULL, "CLIENT_PID=%u", (unsigned int)client->ucred.pid));
	NIH_MUST (nih_str_array_addp (&env, NULL, NULL, var));

	var = NIH_MUST (nih_sprintf (NULL, "PATH=%s", socket_path));
	NIH_MUST (nih_str_array_addp (&env, NULL, NULL, var));

	/* Add the name=value pair */
	NIH_MUST (nih_str_array_addn (&env, NULL, NULL, buf, used_len));

	pending_call = upstart_emit_event (upstart,
			event_name, env, FALSE,
			NULL, emit_event_error, NULL,
			NIH_DBUS_TIMEOUT_NEVER);

	if (! pending_call) {
		NihError *err;
		err = nih_error_get ();
		nih_warn ("%s", err->message);
		nih_free (err);
	}

	/* Consume the entire length */
	nih_io_buffer_shrink (io->recv_buf, len);

	dbus_pending_call_unref (pending_call);

	return;

error:
	nih_debug ("ignoring invalid input of length %lu",
			(unsigned long int)len);

	/* Consume the entire length */
	nih_io_buffer_shrink (io->recv_buf, len);
}

static void
close_handler (ClientConnection *client, NihIo *io)
{
	nih_assert (client);
	nih_assert (io);

	nih_debug ("Remote end closed connection");

	close (client->fd);
	nih_free (client);
	nih_free (io);
}

/**
 * create_socket:
 * @parent: Parent pointer.
 *
 * Create a Socket object, listen on it and arrange for NIH to monitor
 * it.
 *
 * Returns: Newly-allocated Socket on success, or NULL on error.
 **/
static Socket *
create_socket (void *parent)
{
	Socket   *sock = NULL;
	int       opt = 1;
	size_t    len;

	if (! socket_path) {
		nih_fatal ("%s", _("Must specify socket path"));
		exit (1);
	}

	NIH_MUST (nih_strcat_sprintf (&socket_name, NULL, "%s:%s",
				socket_type, socket_path));

	sock = NIH_MUST (nih_new (NULL, Socket));
	memset (sock, 0, sizeof (Socket));
	sock->sock = -1;

	sock->sun_addr.sun_family = AF_UNIX;

	if (! *socket_path || (socket_path[0] != '/' && socket_path[0] != '@')) {
		nih_fatal ("%s %s", _("Invalid path"), socket_path);
		goto error;
	}

	len = strlen (socket_path);

	if (len > sizeof (sock->sun_addr.sun_path)) {
		nih_fatal ("%s %s", _("Path too long"), socket_path);
		goto error;
	}

	strncpy (sock->sun_addr.sun_path, socket_path,
			sizeof (sock->sun_addr.sun_path));

	sock->addrlen = sizeof (sock->sun_addr.sun_family) + len;

	/* Handle abstract names */
	if (sock->sun_addr.sun_path[0] == '@')
		sock->sun_addr.sun_path[0] = '\0';

	sock->sock = socket (sock->addr.sa_family, SOCK_STREAM, 0);
	if (sock->sock < 0) {
		nih_fatal ("%s %s %s", _("Failed to create socket"),
				socket_name, strerror (errno));
		goto error;
	}

	if (setsockopt (sock->sock, SOL_SOCKET, SO_REUSEADDR,
				&opt, sizeof (opt)) < 0) {
		nih_fatal ("%s %s %s", _("Failed to set socket reuse"),
				socket_name, strerror (errno));
		goto error;
	}

	if (setsockopt (sock->sock, SOL_SOCKET, SO_PASSCRED,
				&opt, sizeof (opt)) < 0) {
		nih_fatal ("%s %s %s", _("Failed to set socket credential-passing"),
				socket_name, strerror (errno));
		goto error;
	}

	if (bind (sock->sock, &sock->addr, sock->addrlen) < 0) {
		nih_fatal ("%s %s %s", _("Failed to bind socket"),
				socket_name, strerror (errno));
		goto error;
	}

	if (listen (sock->sock, SOMAXCONN) < 0) {
		nih_fatal ("%s %s %s", _("Failed to listen on socket"),
				socket_name, strerror (errno));
		goto error;
	}

	sock->watch = NIH_MUST (nih_io_add_watch (sock, sock->sock,
				NIH_IO_READ,
				(NihIoWatcher)socket_watcher, sock));

	return sock;

error:
	nih_free (sock);
	return NULL;
}

static void
emit_event_error (void           *data,
		  NihDBusMessage *message)
{
	NihError *err;

	err = nih_error_get ();
	nih_warn ("%s", err->message);
	nih_free (err);
}
