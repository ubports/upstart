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
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <ctype.h>

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
#include <nih-dbus/dbus_object.h>

#include "dbus/upstart.h"
#include "com.ubuntu.Upstart.h"
#include "org.freedesktop.systemd1.h"
#include "control_com.ubuntu.Upstart.h"

#define DBUS_ADDRESS_SYSTEMD "unix:path=/run/systemd/private"
#define DBUS_PATH_SYSTEMD "/org/freedesktop/systemd1"
#define DBUS_SERVICE_SYSTEMD "org.freedesktop.systemd1"
#define DBUS_ADDRESS_LOCAL "unix:abstract=/com/ubuntu/upstart/local/bridge"

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

static void upstart_connect      (void);
static void systemd_connect      (void);
static int  systemd_booted       (void);
static int  control_server_open  (void);
static int  control_server_connect (DBusServer *server,
				    DBusConnection *conn);
static void control_disconnected   (DBusConnection *conn);
static void init_disconnected (DBusConnection *connection);

static Socket *create_socket (void *parent);

static void socket_watcher (Socket *sock, NihIoWatch *watch,
			   NihIoEvents events);

static void socket_reader (ClientConnection *client, NihIo *io,
			   const char *buf, size_t len);

static void close_handler (ClientConnection *client, NihIo *io);

static void emit_event_error (void *data, NihDBusMessage *message);

static void emit_event (ClientConnection *client, const char *pair, size_t len);

static void process_event (ClientConnection *client, const char *pair, size_t len);

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
 * upstart:
 *
 * Proxy to Upstart daemon.
 **/
static NihDBusProxy *upstart = NULL;

/**
 * systemd:
 *
 * Proxy to systemd daemon.
 **/
static NihDBusProxy *systemd = NULL;

/**
 * control_server
 * 
 * D-Bus server listening for new direct connections.
 **/
DBusServer *control_server = NULL;

/**
 * control_conns:
 *
 * Open control connections, including the connection to a D-Bus
 * bus and any private client connections.
 **/
NihList *control_conns = NULL;

/**
 * event_name:
 *
 * upstart: Name of event this bridge emits.
 * systmed: Name of target this generator creates.
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

	{ 0, "event", N_("specify name of event to emit / target to generate on receipt of name=value pair"),
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

	nih_option_set_synopsis (_("Local socket Upstart Bridge & systemd generator"));
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

	sock = create_socket (NULL);
	if (! sock) {
		nih_fatal ("%s %s",
			_("Failed to create socket"),
			socket_name);
		exit (1);
	}

	nih_debug ("Connected to socket '%s' on fd %d", socket_name, sock->sock);

	if (systemd_booted() == TRUE) {
		systemd_connect ();
	} else {
		upstart_connect ();
	}

	control_conns = NIH_MUST (nih_list_new (NULL));

        while ((ret = control_server_open ()) < 0) {
		NihError *err;

                err = nih_error_get ();
                if (err->number != ENOMEM) {
                        nih_warn ("%s: %s", _("Unable to listen for private"
					      "connections"), err->message);
                        nih_free (err);
			break;
                }
                nih_free (err);
        }

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
upstart_connect (void)
{
	DBusConnection    *connection;

	/* Initialise the connection to Upstart */
	connection = NIH_SHOULD (nih_dbus_connect (DBUS_ADDRESS_UPSTART, init_disconnected));
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
}

static void
systemd_connect (void)
{
	DBusConnection    *connection;

	/* Initialise the connection to systemd */
	/* /run/systemd/private is supposedly "private" end-point
	 * which systemctl & libsystemd use */
	connection = NIH_SHOULD (nih_dbus_connect (DBUS_ADDRESS_SYSTEMD, init_disconnected));
	if (! connection) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not connect to systemd"),
			   err->message);
		nih_free (err);

		exit (1);
	}

	systemd = NIH_SHOULD (nih_dbus_proxy_new (NULL, connection,
						  NULL, DBUS_PATH_SYSTEMD,
						  NULL, NULL));
	if (! systemd) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not create systemd proxy"),
			   err->message);
		nih_free (err);

		exit (1);
	}

	FILE *fp = NULL;
	nih_local char *template_name = NULL;
	
	template_name = NIH_MUST (nih_sprintf (NULL, "/run/systemd/system/%s@.target", event_name));

	fp = NIH_SHOULD (fopen(template_name, "we"));
	if (!fp) {
		nih_fatal ("%s %s", _("Failed to create target template"),
			  strerror (errno));
		exit (1);
	}
	fprintf (fp,
		"# Automatically generated by %s\n\n"
		"[Unit]\n"
		"Description=Local bridge key value pairs\n"
		"Documentation=man:%s\n",
		program_name, program_name);
	fflush (fp);
	if (ferror (fp)) {
		nih_fatal ("%s %s", _("Failed to write target template"),
			   strerror (errno));
		exit (1);
	}
	fclose (fp);

	nih_debug ("Connected to systemd");
}

static int
systemd_booted (void)
{
	struct stat st;

	if (lstat ("/run/systemd/system/", &st) == 0) {
		if (S_ISDIR(st.st_mode)) {
			return TRUE;
		}
	}

	return FALSE;
}

static void
init_disconnected (DBusConnection *connection)
{
	nih_fatal (_("Disconnected from init"));
	nih_main_loop_exit (1);
}

/**
 * control_server_open:
 *
 * Open a listening D-Bus server and store it in the control_server global.
 * New connections are permitted from the root user, and handled
 * automatically in the main loop.
 *
 * Returns: zero on success, negative value on raised error.
 **/

int
control_server_open (void)
{
	nih_assert (control_server == NULL);

	control_server = nih_dbus_server (DBUS_ADDRESS_LOCAL,
				  control_server_connect,
				  control_disconnected);
	if (! control_server)
		return -1;

	nih_debug("D-Bus server started at address: %s", DBUS_ADDRESS_LOCAL);

	return 0;
}

/**
 * control_server_connect:
 *
 * Called when a new client connects to our server and is used to register
 * objects on the new connection.
 *
 * Returns: always TRUE.
 **/
static int
control_server_connect (DBusServer     *server,
			DBusConnection *conn)
{
	nih_assert (server != NULL);
	nih_assert (server == control_server);
	nih_assert (conn != NULL);
	NihListEntry *entry = NULL;

	/* Register objects on the connection. */
	NIH_MUST (nih_dbus_object_new (NULL, conn, DBUS_PATH_UPSTART,
				       control_interfaces, NULL));


	entry = NIH_MUST (nih_list_entry_new (NULL));
	entry->data = conn;
	nih_list_add (control_conns, &entry->entry);
	nih_debug("Connection from private client");

	return TRUE;
}

/**
 * control_disconnected:
 *
 * This function is called when the connection to the D-Bus system bus,
 * or a client connection to our D-Bus server, is dropped and our reference
 * is about to be list.  We clear the connection from our current list
 * and drop the control_bus global if relevant.
 **/
static void
control_disconnected (DBusConnection *conn)
{
	nih_assert (conn != NULL);
        /* Remove from the connections list */
        NIH_LIST_FOREACH_SAFE (control_conns, iter) {
                NihListEntry *entry = (NihListEntry *)iter;

                if (entry->data == conn)
                        nih_free (entry);
        }
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
	socklen_t           len;

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
	nih_local char     *pairs = NULL;
	char               *pair;
	size_t              used_len = 0;
	size_t              i;

	/* Ignore messages that are too short.
	 * (minimum message is of form "a=").
	 */
	size_t              min_len = 2;

	nih_assert (sock);
	nih_assert (client);
	nih_assert (io);
	nih_assert (buf);

	if (len < min_len)
		goto error;

	pairs = nih_strdup (NULL, buf);
	if (! pairs)
		return;

	for (pair = strsep (&pairs, "\n");
	     pair;
	     pair = strsep (&pairs, "\n")) {

		used_len = strlen (pair);

		if (used_len < min_len)
			continue;

		/* Ensure the data is a 'name=value' pair */
		if (! strchr (pair, '=') || pair[0] == '=')
			continue;

		/* Remove extraneous line ending */
		if (pair[used_len-1] == '\r') {
			pair[used_len-1] = '\0';
			used_len--;
		}

		/* Ignore invalid input */
		for (i = 0; i < used_len; i++) {
			if (! isprint (pair[i]) && ! isspace (pair[i]))
				continue;
		}

		/* Yet another check to ensure overly short messages are ignored
		 * (required since we may have adjusted used_len
		 */
		if (used_len < min_len)
			continue;

		process_event (client, pair, used_len);
	}

	/* Consume the entire length */
	nih_io_buffer_shrink (io->recv_buf, len);

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
	else
		(void) unlink(sock->sun_addr.sun_path);

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

static void
emit_event (ClientConnection  *client,
	    const char        *pair,
	    size_t             len)
{
	DBusPendingCall    *pending_call;
	nih_local char    **env = NULL;
	nih_local char     *var = NULL;

	nih_assert  (client);
	nih_assert  (pair);
	nih_assert  (len);
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

	var = NIH_MUST (nih_sprintf (NULL, "SOCKET_PATH=%s", socket_path));
	NIH_MUST (nih_str_array_addp (&env, NULL, NULL, var));

	/* Add the name=value pair */
	NIH_MUST (nih_str_array_addn (&env, NULL, NULL, pair, len));

	if (upstart) {
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
		
		dbus_pending_call_unref (pending_call);
	}

	NIH_LIST_FOREACH (control_conns, iter) {
		NihListEntry   *entry = (NihListEntry *)iter;
		DBusConnection *conn = (DBusConnection *)entry->data;

		NIH_ZERO (control_emit_event_emitted (conn, DBUS_PATH_UPSTART,
							    event_name, env));
	}

}

static void
systemd_launch_instance (ClientConnection  *client,
	    const char        *pair,
	    size_t             len)
{
	nih_local char     *safe_pair = NULL;
	nih_local char    **key_value = NULL;
	nih_local char     *group_name = NULL;
	nih_local char     *unit_name = NULL;
	nih_local char     *job_name = NULL;

	nih_assert  (client);
	nih_assert  (pair);
	nih_assert  (len);

	/* Why is pair not null-terminated?! */
	safe_pair = NIH_MUST (nih_strndup (NULL, pair, len));

	/* Get key val from the key=val pair */
	key_value = NIH_MUST (nih_str_split (NULL, safe_pair, "=", TRUE));

	/* Construct systemd event@key=*.target group name */
	group_name = NIH_MUST (nih_sprintf (NULL, "%s@%s=*.target",
					    event_name, key_value[0]));

	/* Construct systemd event@key=value.target unit name */
	unit_name = NIH_MUST (nih_sprintf (NULL, "%s@%s\\x3d%s.target",
					   event_name, key_value[0],
					   key_value[1]));

	/* Stop group */
	int pid = -1;
	siginfo_t info;
	do {
		pid = fork ();
	} while (pid < 0);
	
	if (pid) {
		info.si_code = 0;
		info.si_status = 0;
		if (waitid (P_PID, pid, &info, WEXITED)) {
			nih_fatal ("%s %s", _("Failed to wait for systemctl"),
				   strerror (errno));
		}
                if (info.si_code != CLD_EXITED || info.si_status) {
                        nih_fatal ("Bad systemctl exit code %i and status %i\n", info.si_code, info.si_status);
                }
	} else {
		/* Create and submit stop state transition, do not wait to complete */
		execlp ("systemctl", "systemctl", "--no-block", "stop", group_name, (char *)NULL);
	}
			       
	/* Create and submit start state transition, do not wait to complete */
	if (systemd_start_unit_sync (NULL, systemd, unit_name, "replace", &job_name)) {
		NihError *err;
		err = nih_error_get ();
		nih_warn ("%s", err->message);
		nih_free (err);
	}
}


static void
process_event (ClientConnection  *client,
	    const char        *pair,
	    size_t             len)
{
	emit_event (client, pair, len);

	if (systemd) {
		systemd_launch_instance (client, pair, len);
	}
}
