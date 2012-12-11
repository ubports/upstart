/* upstart
 *
 * control.c - D-Bus connections, objects and methods
 *
 * Copyright © 2009-2011 Canonical Ltd.
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

#include <dbus/dbus.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/io.h>
#include <nih/main.h>
#include <nih/logging.h>
#include <nih/error.h>
#include <nih/errors.h>

#include <nih-dbus/dbus_error.h>
#include <nih-dbus/dbus_connection.h>
#include <nih-dbus/dbus_message.h>
#include <nih-dbus/dbus_object.h>

#include "dbus/upstart.h"

#include "environ.h"
#include "session.h"
#include "job_class.h"
#include "blocked.h"
#include "conf.h"
#include "control.h"
#include "errors.h"
#include "state.h"

#include "com.ubuntu.Upstart.h"

/* Prototypes for static functions */
static int   control_server_connect (DBusServer *server, DBusConnection *conn);
static void  control_disconnected   (DBusConnection *conn);
static void  control_register_all   (DBusConnection *conn);

static void  control_bus_flush      (void);

/**
 * use_session_bus:
 *
 * If TRUE, connect to the D-Bus session bus rather than the system bus.
 *
 * Used for testing.
 **/
int use_session_bus = FALSE;

/**
 * control_server_address:
 *
 * Address on which the control server may be reached.
 **/
const char *control_server_address = DBUS_ADDRESS_UPSTART;

/**
 * control_server:
 *
 * D-Bus server listening for new direct connections.
 **/
DBusServer *control_server = NULL;

/**
 * control_bus:
 *
 * Open connection to a D-Bus bus.  The connection may be opened with
 * control_bus_open() and if lost will become NULL.
 **/
DBusConnection *control_bus = NULL;

/**
 * control_conns:
 *
 * Open control connections, including the connection to a D-Bus
 * bus and any private client connections.
 **/
NihList *control_conns = NULL;


/**
 * control_init:
 *
 * Initialise the control connections list.
 **/
void
control_init (void)
{
	if (! control_conns)
		control_conns = NIH_MUST (nih_list_new (NULL));
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
	DBusServer *server;

	nih_assert (control_server == NULL);

	control_init ();

	server = nih_dbus_server (control_server_address,
				  control_server_connect,
				  control_disconnected);
	if (! server)
		return -1;

	control_server = server;

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
	NihListEntry *entry;

	nih_assert (server != NULL);
	nih_assert (server == control_server);
	nih_assert (conn != NULL);

	nih_info (_("Connection from private client"));

	/* Register objects on the connection. */
	control_register_all (conn);

	/* Add the connection to the list */
	entry = NIH_MUST (nih_list_entry_new (NULL));

	entry->data = conn;

	nih_list_add (control_conns, &entry->entry);

	return TRUE;
}

/**
 * control_server_close:
 *
 * Close the connection to the D-Bus system bus.  Since the connection is
 * shared inside libdbus, this really only drops our reference to it so
 * it's possible to have method and signal handlers called even after calling
 * this (normally to dispatch what's in the queue).
 **/
void
control_server_close (void)
{
	if (! control_server)
		return;

	dbus_server_disconnect (control_server);
	dbus_server_unref (control_server);

	control_server = NULL;
}


/**
 * control_bus_open:
 *
 * Open a connection to the appropriate D-Bus bus and store it in the
 * control_bus global. The connection is handled automatically
 * in the main loop.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
control_bus_open (void)
{
	DBusConnection *conn;
	DBusError       error;
	NihListEntry   *entry;
	int             ret;

	nih_assert (control_bus == NULL);

	control_init ();

	control_handle_bus_type ();

	/* Connect to the D-Bus System Bus and hook everything up into
	 * our own main loop automatically.
	 */
	conn = nih_dbus_bus (use_session_bus ? DBUS_BUS_SESSION : DBUS_BUS_SYSTEM,
			     control_disconnected);
	if (! conn)
		return -1;

	/* Register objects on the bus. */
	control_register_all (conn);

	/* Request our well-known name.  We do this last so that once it
	 * appears on the bus, clients can assume we're ready to talk to
	 * them.
	 */
	dbus_error_init (&error);
	ret = dbus_bus_request_name (conn, DBUS_SERVICE_UPSTART,
				     DBUS_NAME_FLAG_DO_NOT_QUEUE, &error);
	if (ret < 0) {
		/* Error while requesting the name */
		nih_dbus_error_raise (error.name, error.message);
		dbus_error_free (&error);

		dbus_connection_unref (conn);
		return -1;
	} else if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		/* Failed to obtain the name (already taken usually) */
		nih_error_raise (CONTROL_NAME_TAKEN,
				 _(CONTROL_NAME_TAKEN_STR));

		dbus_connection_unref (conn);
		return -1;
	}


	/* Add the connection to the list */
	entry = NIH_MUST (nih_list_entry_new (NULL));

	entry->data = conn;

	nih_list_add (control_conns, &entry->entry);


	control_bus = conn;

	return 0;
}

/**
 * control_bus_close:
 *
 * Close the connection to the D-Bus system bus.  Since the connection is
 * shared inside libdbus, this really only drops our reference to it so
 * it's possible to have method and signal handlers called even after calling
 * this (normally to dispatch what's in the queue).
 **/
void
control_bus_close (void)
{
	nih_assert (control_bus != NULL);

	dbus_connection_unref (control_bus);

	control_disconnected (control_bus);
}


/**
 * control_disconnected:
 *
 * This function is called when the connection to the D-Bus system bus,
 * or a client connection to our D-Bus server, is dropped and our reference
 * is about to be lost.  We clear the connection from our current list
 * and drop the control_bus global if relevant.
 **/
static void
control_disconnected (DBusConnection *conn)
{
	nih_assert (conn != NULL);

	if (conn == control_bus) {
		DBusError  error;

		dbus_error_init (&error);

		nih_warn (_("Disconnected from system bus"));

		control_bus = NULL;
	}

	/* Remove from the connections list */
	NIH_LIST_FOREACH_SAFE (control_conns, iter) {
		NihListEntry *entry = (NihListEntry *)iter;

		if (entry->data == conn)
			nih_free (entry);
	}
}


/**
 * control_register_all:
 * @conn: connection to register objects for.
 *
 * Registers the manager object and objects for all jobs and instances on
 * the given connection.
 **/
static void
control_register_all (DBusConnection *conn)
{
	nih_assert (conn != NULL);

	job_class_init ();

	/* Register the manager object, this is the primary point of contact
	 * for clients.  We only check for success, otherwise we're happy
	 * to let this object be tied to the lifetime of the connection.
	 */
	NIH_MUST (nih_dbus_object_new (NULL, conn, DBUS_PATH_UPSTART,
				       control_interfaces, NULL));

	/* Register objects for each currently registered job and its
	 * instances.
	 */
	NIH_HASH_FOREACH (job_classes, iter) {
		JobClass *class = (JobClass *)iter;

		job_class_register (class, conn, FALSE);
	}
}


/**
 * control_reload_configuration:
 * @data: not used,
 * @message: D-Bus connection and message received.
 *
 * Implements the ReloadConfiguration method of the com.ubuntu.Upstart
 * interface.
 *
 * Called to request that Upstart reloads its configuration from disk,
 * useful when inotify is not available or the user is generally paranoid.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
control_reload_configuration (void           *data,
			      NihDBusMessage *message)
{
	nih_assert (message != NULL);

	nih_info (_("Reloading configuration"));

	/* This can only be called after deserialisation */
	conf_reload ();

	return 0;
}


/**
 * control_get_job_by_name:
 * @data: not used,
 * @message: D-Bus connection and message received,
 * @name: name of job to get,
 * @job: pointer for object path reply.
 *
 * Implements the GetJobByName method of the com.ubuntu.Upstart
 * interface.
 *
 * Called to obtain the path to a D-Bus object for the job named @name,
 * which will be stored in @job.  If no job class with that name exists,
 * the com.ubuntu.Upstart.Error.UnknownJob D-Bus error will be raised.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
control_get_job_by_name (void            *data,
			 NihDBusMessage  *message,
			 const char      *name,
			 char           **job)
{
	Session  *session;
	JobClass *class = NULL;
	JobClass *global_class = NULL;

	nih_assert (message != NULL);
	nih_assert (name != NULL);
	nih_assert (job != NULL);

	job_class_init ();

	/* Verify that the name is valid */
	if (! strlen (name)) {
		nih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,
					     _("Name may not be empty string"));
		return -1;
	}

	/* Get the relevant session */
	session = session_from_dbus (NULL, message);

	/* Lookup the job */
	class = (JobClass *)nih_hash_search (job_classes, name, NULL);

	while (class && (class->session != session)) {

		/* Found a match in the global session which may be used
		 * later if no matching user session job exists.
		 */
		if ((! class->session) && (session && ! session->chroot))
			global_class = class;

		class = (JobClass *)nih_hash_search (job_classes, name,
				&class->entry);
	}

	/* If no job with the given name exists in the appropriate
	 * session, look in the global namespace (aka the NULL session).
	 */ 
	if (! class)
		class = global_class;

	if (! class) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.UnknownJob",
			_("Unknown job: %s"), name);
		return -1;
	}

	/* Copy the path */
	*job = nih_strdup (message, class->path);
	if (! *job)
		nih_return_system_error (-1);

	return 0;
}

/**
 * control_get_all_jobs:
 * @data: not used,
 * @message: D-Bus connection and message received,
 * @jobs: pointer for array of object paths reply.
 *
 * Implements the GetAllJobs method of the com.ubuntu.Upstart
 * interface.
 *
 * Called to obtain the paths of all known jobs, which will be stored in
 * @jobs.  If no jobs are registered, @jobs will point to an empty array.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
control_get_all_jobs (void             *data,
		      NihDBusMessage   *message,
		      char           ***jobs)
{
	Session *session;
	char   **list;
	size_t   len;

	nih_assert (message != NULL);
	nih_assert (jobs != NULL);

	job_class_init ();

	len = 0;
	list = nih_str_array_new (message);
	if (! list)
		nih_return_system_error (-1);

	/* Get the relevant session */
	session = session_from_dbus (NULL, message);

	NIH_HASH_FOREACH (job_classes, iter) {
		JobClass *class = (JobClass *)iter;

		if ((class->session || (session && session->chroot))
		    && (class->session != session))
			continue;

		if (! nih_str_array_add (&list, message, &len,
					 class->path)) {
			nih_error_raise_system ();
			nih_free (list);
			return -1;
		}
	}

	*jobs = list;

	return 0;
}


int
control_emit_event (void            *data,
		    NihDBusMessage  *message,
		    const char      *name,
		    char * const    *env,
		    int              wait)
{
	return control_emit_event_with_file (data, message, name, env, wait, -1);
}

/**
 * control_emit_event_with_file:
 * @data: not used,
 * @message: D-Bus connection and message received,
 * @name: name of event to emit,
 * @env: environment of environment,
 * @wait: whether to wait for event completion before returning,
 * @file: file descriptor.
 *
 * Implements the top half of the EmitEvent method of the com.ubuntu.Upstart
 * interface, the bottom half may be found in event_finished().
 *
 * Called to emit an event with a given @name and @env, which will be
 * added to the event queue and processed asynchronously.  If @name or
 * @env are not valid, the org.freedesktop.DBus.Error.InvalidArgs D-Bus
 * error will be returned immediately.  If the event fails, the
 * com.ubuntu.Upstart.Error.EventFailed D-Bus error will be returned when
 * the event finishes.
 *
 * When @wait is TRUE the method call will not return until the event
 * has completed, which means that all jobs affected by the event have
 * finished starting (running for tasks) or stopping; when @wait is FALSE,
 * the method call returns once the event has been queued.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
control_emit_event_with_file (void            *data,
			      NihDBusMessage  *message,
			      const char      *name,
			      char * const    *env,
			      int              wait,
			      int              file)
{
	Event   *event;
	Blocked *blocked;

	nih_assert (message != NULL);
	nih_assert (name != NULL);
	nih_assert (env != NULL);

	/* Verify that the name is valid */
	if (! strlen (name)) {
		nih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,
					     _("Name may not be empty string"));
		close (file);
		return -1;
	}

	/* Verify that the environment is valid */
	if (! environ_all_valid (env)) {
		nih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,
					     _("Env must be KEY=VALUE pairs"));
		close (file);
		return -1;
	}

	/* Make the event and block the message on it */
	event = event_new (NULL, name, (char **)env);
	if (! event) {
		nih_error_raise_system ();
		close (file);
		return -1;
	}

	event->fd = file;
	if (event->fd >= 0) {
		long flags;

		flags = fcntl (event->fd, F_GETFD);
		flags &= ~FD_CLOEXEC;
		fcntl (event->fd, F_SETFD, flags);
	}

	/* Obtain the session */
	event->session = session_from_dbus (NULL, message);

	if (wait) {
		blocked = blocked_new (event, BLOCKED_EMIT_METHOD, message);
		if (! blocked) {
			nih_error_raise_system ();
			nih_free (event);
			close (file);
			return -1;
		}

		nih_list_add (&event->blocking, &blocked->entry);
	} else {
		NIH_ZERO (control_emit_event_reply (message));
	}

	return 0;
}


/**
 * control_get_version:
 * @data: not used,
 * @message: D-Bus connection and message received,
 * @version: pointer for reply string.
 *
 * Implements the get method for the version property of the
 * com.ubuntu.Upstart interface.
 *
 * Called to obtain the version of the init daemon, which will be stored
 * as a string in @version.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
control_get_version (void *          data,
		     NihDBusMessage *message,
		     char **         version)
{
	nih_assert (message != NULL);
	nih_assert (version != NULL);

	*version = nih_strdup (message, package_string);
	if (! *version)
		nih_return_no_memory_error (-1);

	return 0;
}

/**
 * control_get_log_priority:
 * @data: not used,
 * @message: D-Bus connection and message received,
 * @log_priority: pointer for reply string.
 *
 * Implements the get method for the log_priority property of the
 * com.ubuntu.Upstart interface.
 *
 * Called to obtain the init daemon's current logging level, which will
 * be stored as a string in @log_priority.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
control_get_log_priority (void *          data,
			  NihDBusMessage *message,
			  char **         log_priority)
{
	const char *priority;

	nih_assert (message != NULL);
	nih_assert (log_priority != NULL);

	switch (nih_log_priority) {
	case NIH_LOG_DEBUG:
		priority = "debug";
		break;
	case NIH_LOG_INFO:
		priority = "info";
		break;
	case NIH_LOG_MESSAGE:
		priority = "message";
		break;
	case NIH_LOG_WARN:
		priority = "warn";
		break;
	case NIH_LOG_ERROR:
		priority = "error";
		break;
	case NIH_LOG_FATAL:
		priority = "fatal";
		break;
	default:
		nih_assert_not_reached ();
	}

	*log_priority = nih_strdup (message, priority);
	if (! *log_priority)
		nih_return_no_memory_error (-1);

	return 0;
}

/**
 * control_set_log_priority:
 * @data: not used,
 * @message: D-Bus connection and message received,
 * @log_priority: string log priority to be set.
 *
 * Implements the get method for the log_priority property of the
 * com.ubuntu.Upstart interface.
 *
 * Called to change the init daemon's current logging level to that given
 * as a string in @log_priority.  If the string is not recognised, the
 * com.ubuntu.Upstart.Error.InvalidLogPriority error will be returned.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
control_set_log_priority (void *          data,
			  NihDBusMessage *message,
			  const char *    log_priority)
{
	nih_assert (message != NULL);
	nih_assert (log_priority != NULL);

	if (! strcmp (log_priority, "debug")) {
		nih_log_set_priority (NIH_LOG_DEBUG);

	} else if (! strcmp (log_priority, "info")) {
		nih_log_set_priority (NIH_LOG_INFO);

	} else if (! strcmp (log_priority, "message")) {
		nih_log_set_priority (NIH_LOG_MESSAGE);

	} else if (! strcmp (log_priority, "warn")) {
		nih_log_set_priority (NIH_LOG_WARN);

	} else if (! strcmp (log_priority, "error")) {
		nih_log_set_priority (NIH_LOG_ERROR);

	} else if (! strcmp (log_priority, "fatal")) {
		nih_log_set_priority (NIH_LOG_FATAL);

	} else {
		nih_dbus_error_raise (DBUS_ERROR_INVALID_ARGS,
				      _("The log priority given was not recognised"));
		return -1;
	}

	return 0;
}

/**
 * control_handle_bus_type:
 *
 * Determine D-Bus bus type to connect to.
 **/
void
control_handle_bus_type (void)
{
	if (getenv (USE_SESSION_BUS_ENV))
		use_session_bus = TRUE;

	if (use_session_bus)
		nih_debug ("Using session bus");
}
/**
 * control_notify_disk_writeable:
 * @data: not used,
 * @message: D-Bus connection and message received,
 *
 * Implements the NotifyDiskWriteable method of the
 * com.ubuntu.Upstart interface.
 *
 * Called to flush the job logs for all jobs that ended before the log
 * disk became writeable.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
control_notify_disk_writeable (void   *data,
		     NihDBusMessage *message)
{
	int       ret;
	Session  *session;

	nih_assert (message != NULL);

	/* Get the relevant session */
	session = session_from_dbus (NULL, message);

	if (session && session->user) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.PermissionDenied",
			_("You do not have permission to notify disk is writeable"));
		return -1;
	}

	/* "nop" when run from a chroot */
	if (session && session->chroot)
		return 0;

	ret = log_clear_unflushed ();

	if (ret < 0) {
		nih_error_raise_system ();
		return -1;
	}

	return 0;
}

/**
 * control_bus_flush:
 *
 * Drain any remaining messages in the D-Bus queue.
 **/
static void
control_bus_flush (void)
{
	control_init ();

	if (! control_bus)
		return;

	while (dbus_connection_dispatch (control_bus) == DBUS_DISPATCH_DATA_REMAINS)
		;
}

/**
 * control_prepare_reexec:
 *
 * Prepare for a re-exec by allowing the bus connection to be retained
 * over re-exec and clearing all queued messages.
 **/
void
control_prepare_reexec (void)
{
	control_init ();

	/* Necessary to disallow further commands but also to allow the
	 * new instance to open the control server.
	 */
	if (control_server)
		control_server_close ();

	control_bus_flush ();

}


/**
 * control_conn_to_index:
 *
 * @event: event.
 *
 * Convert a control (DBusConnection) connection to an index number
 * the list of control connections.
 *
 * Returns: connection index, or -1 on error.
 **/
int
control_conn_to_index (const DBusConnection *connection)
{
	int conn_index = 0;
	int found = FALSE;

	nih_assert (connection);

	NIH_LIST_FOREACH (control_conns, iter) {
		NihListEntry    *entry = (NihListEntry *)iter;
		DBusConnection  *conn = (DBusConnection *)entry->data;

		if (connection == conn) {
			found = TRUE;
			break;
		}

		conn_index++;
	}
	if (! found)
		return -1;

	return conn_index;
}

/**
 * control_conn_from_index:
 *
 * @conn_index: control connection index number.
 *
 * Lookup control connection based on index number.
 *
 * Returns: existing connection on success, or NULL if connection
 * not found.
 **/
DBusConnection *
control_conn_from_index (int conn_index)
{
	int i = 0;

	nih_assert (conn_index >= 0);
	nih_assert (control_conns);

	NIH_LIST_FOREACH (control_conns, iter) {
		NihListEntry    *entry = (NihListEntry *)iter;
		DBusConnection  *conn = (DBusConnection *)entry->data;

		if (i == conn_index)
			return conn;
		i++;
	}

	return NULL;
}

/**
 * control_bus_release_name:
 *
 * Unregister well-known D-Bus name.
 *
 * Returns: 0 on success, -1 on raised error.
 **/
int
control_bus_release_name (void)
{
	DBusError  error;
	int        ret;

	if (! control_bus)
		return 0;

	dbus_error_init (&error);
	ret = dbus_bus_release_name (control_bus,
				     DBUS_SERVICE_UPSTART,
				     &error);
	if (ret < 0) {
		nih_dbus_error_raise (error.name, error.message);
		dbus_error_free (&error);
		return -1;
	}

	return 0;
}

/**
 * control_get_state:
 *
 * @data: not used,
 * @message: D-Bus connection and message received,
 * @state: output string returned to client.
 *
 * Convert internal state to JSON string.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
control_get_state (void           *data,
		   NihDBusMessage  *message,
		   char           **state)
{
	Session  *session;
	uid_t     uid;
	size_t    len;

	nih_assert (message);
	nih_assert (state);

	uid = getuid ();

	/* Get the relevant session */
	session = session_from_dbus (NULL, message);

	/* We don't want chroot sessions snooping outside their domain.
	 *
	 * Ideally, we'd allow them to query their own session, but the
	 * current implementation doesn't lend itself to that.
	 */
	if (session && session->chroot) {
		nih_warn (_("Ignoring state query from chroot session"));
		return 0;
	}

	/* Disallow users from obtaining state details, unless they
	 * happen to own this process (which they may do in the test
	 * scenario and when running Upstart as a non-privileged user).
	 */
	if (session && session->user != uid) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.PermissionDenied",
			_("You do not have permission to request state"));
		return -1;
	}

	if (state_to_string (state, &len) < 0)
		goto error;

	nih_ref (*state, message);

	return 0;

error:
	nih_dbus_error_raise_printf (DBUS_ERROR_NO_MEMORY,
			_("Out of Memory"));
	return -1;
}

/**
 * control_restart:
 *
 * @data: not used,
 * @message: D-Bus connection and message received.
 *
 * Implements the Restart method of the com.ubuntu.Upstart
 * interface.
 *
 * Called to request that Upstart performs a stateful re-exec.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
control_restart (void           *data,
		 NihDBusMessage *message)
{
	Session  *session;
	uid_t     uid;

	nih_assert (message != NULL);

	uid = getuid ();

	/* Get the relevant session */
	session = session_from_dbus (NULL, message);

	/* Chroot sessions must not be able to influence
	 * the outside system.
	 *
	 * Making this a NOP is safe since it is the Upstart outside the
	 * chroot which manages all chroot jobs.
	 */
	if (session && session->chroot) {
		nih_warn (_("Ignoring restart request from chroot session"));
		return 0;
	}

	/* Disallow users from restarting Upstart, unless they happen to
	 * own this process (which they may do in the test scenario and
	 * when running Upstart as a non-privileged user).
	 */
	if (session && session->user != uid) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.PermissionDenied",
			_("You do not have permission to request restart"));
		return -1;
	}

	nih_info (_("Restarting"));

	stateful_reexec ();

	return 0;
}

/**
 * control_set_env:
 *
 * @data: not used,
 * @message: D-Bus connection and message received,
 * @var: name[/value] pair of environment variable to set,
 * @replace: TRUE if @name should be overwritten if already set, else
 *  FALSE.
 *
 * Implements the SetEnv method of the com.ubuntu.Upstart
 * interface.
 *
 * Called to request Upstart store a particular name/value pair that
 * will be exported to all jobs' environments.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
control_set_env (void           *data,
		 NihDBusMessage *message,
		 const char     *var,
		 int             replace)
{
	Session   *session;
	char     **ret;
	uid_t      uid;

	nih_assert (message != NULL);
	nih_assert (var);

	uid = getuid ();

	/* Get the relevant session */
	session = session_from_dbus (NULL, message);

	/* Chroot sessions must not be able to influence
	 * the outside system.
	 */
	if (session && session->chroot) {
		nih_warn (_("Ignoring set env request from chroot session"));
		return 0;
	}

	/* Disallow users from changing Upstarts environment, unless they happen to
	 * own this process (which they may do in the test scenario and
	 * when running Upstart as a non-privileged user).
	 */
	if (session && session->user != uid) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.PermissionDenied",
			_("You do not have permission to modify the init environment"));
		return -1;
	}

	job_class_environment_init ();

	ret = environ_add (&job_environ, NULL, NULL, replace, var);

	if (! ret)
		nih_return_no_memory_error (-1);

	return 0;
}

/**
 * control_get_env:
 *
 * @data: not used,
 * @message: D-Bus connection and message received,
 * @name: name of environment variable to retrieve,
 * @value: value of @name.
 *
 * Implements the SetEnv method of the com.ubuntu.Upstart
 * interface.
 *
 * Called to request Upstart store a particular name/value pair that
 * will be exported to all jobs' environments.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
control_get_env (void             *data,
		 NihDBusMessage   *message,
		 char             *name,
		 char            **value)
{
	Session     *session;
	const char  *tmp;
	uid_t        uid;

	nih_assert (message != NULL);
	nih_assert (name);
	nih_assert (value);

	uid = getuid ();

	/* Get the relevant session */
	session = session_from_dbus (NULL, message);

	/* Chroot sessions must not be able to influence
	 * the outside system.
	 */
	if (session && session->chroot) {
		nih_warn (_("Ignoring get env request from chroot session"));
		return 0;
	}

	/* Disallow users from changing Upstarts environment, unless they happen to
	 * own this process (which they may do in the test scenario and
	 * when running Upstart as a non-privileged user).
	 */
	if (session && session->user != uid) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.PermissionDenied",
			_("You do not have permission to modify the init environment"));
		return -1;
	}

	job_class_environment_init ();

	tmp = environ_get (job_environ, name);
	if (! tmp)
		goto error;

	*value = nih_strdup (message, tmp);
	if (! *value)
		nih_return_no_memory_error (-1);

	return 0;

error:
	nih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,
			"%s: %s",
			_("No such variable"), name);
	return -1;
}
