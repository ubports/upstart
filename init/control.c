/* upstart
 *
 * control.c - D-Bus connections, objects and methods
 *
 * Copyright  2009-2011 Canonical Ltd.
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
#include "job.h"
#include "blocked.h"
#include "conf.h"
#include "control.h"
#include "errors.h"
#include "state.h"
#include "event.h"
#include "events.h"
#include "paths.h"
#include "xdg.h"

#include "com.ubuntu.Upstart.h"

/* Prototypes for static functions */
static int   control_server_connect      (DBusServer *server, DBusConnection *conn);
static void  control_disconnected        (DBusConnection *conn);
static void  control_register_all        (DBusConnection *conn);

static void  control_bus_flush           (void);
static int   control_get_origin_uid      (NihDBusMessage *message, uid_t *uid)
	__attribute__ ((warn_unused_result));
static int   control_check_permission    (NihDBusMessage *message)
	__attribute__ ((warn_unused_result));
static void  control_session_file_create (void);
static void  control_session_file_remove (void);

/**
 * use_session_bus:
 *
 * If TRUE, connect to the D-Bus session bus rather than the system bus.
 *
 * Used for testing to simulate (as far as possible) a system-like init
 * when running as a non-priv user (but not as a Session Init).
 **/
int use_session_bus = FALSE;

/**
 * dbus_bus_type:
 *
 * Type of D-Bus bus to connect to.
 **/
DBusBusType dbus_bus_type;

/**
 * control_server_address:
 *
 * Address on which the control server may be reached.
 **/
char *control_server_address = NULL;

/**
 * control_server:
 *
 * D-Bus server listening for new direct connections.
 **/
DBusServer *control_server = NULL;

/**
 * control_bus_address:
 *
 * Address on which the control bus may be reached.
 **/
char *control_bus_address = NULL;

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

/* External definitions */
extern int      user_mode;
extern int      disable_respawn;
extern char    *session_file;

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

	if (! control_server_address) {
		if (user_mode) {
			NIH_MUST (nih_strcat_sprintf (&control_server_address, NULL,
					    "%s-session/%d/%d", DBUS_ADDRESS_UPSTART, getuid (), getpid ()));

			control_session_file_create ();
		} else {
			control_server_address = NIH_MUST (nih_strdup (NULL, DBUS_ADDRESS_UPSTART));
		}
	}
}

/**
 * control_cleanup:
 *
 * Perform cleanup operations.
 **/
void
control_cleanup (void)
{
	control_session_file_remove ();
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

	dbus_error_init (&error);

	control_init ();

	dbus_bus_type = control_get_bus_type ();

	/* Connect to the appropriate D-Bus bus and hook everything up into
	 * our own main loop automatically.
	 */
	if (user_mode && control_bus_address) {
		conn = nih_dbus_connect (control_bus_address, control_disconnected);
		if (! conn)
			return -1;

		if (! dbus_bus_register (conn, &error)) {
			nih_dbus_error_raise (error.name, error.message);
			dbus_error_free (&error);
			return -1;
		}

		nih_debug ("Connected to notified D-Bus bus");
	} else {
		conn = nih_dbus_bus (use_session_bus ? DBUS_BUS_SESSION : DBUS_BUS_SYSTEM,
				control_disconnected);
		if (! conn)
			return -1;

		nih_debug ("Connected to D-Bus %s bus",
				dbus_bus_type == DBUS_BUS_SESSION
				? "session" : "system");
	}

	/* Register objects on the bus. */
	control_register_all (conn);

	/* Request our well-known name.  We do this last so that once it
	 * appears on the bus, clients can assume we're ready to talk to
	 * them.
	 */
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

		if (user_mode && control_bus_address) {
			nih_warn (_("Disconnected from notified D-Bus bus"));
		} else {
			nih_warn (_("Disconnected from D-Bus %s bus"),
					dbus_bus_type == DBUS_BUS_SESSION
					? "session" : "system");
		}

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
 * Notes: chroot sessions are permitted to make this call.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
control_reload_configuration (void           *data,
			      NihDBusMessage *message)
{
	nih_assert (message != NULL);

	if (! control_check_permission (message)) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.PermissionDenied",
			_("You do not have permission to reload configuration"));
		return -1;
	}

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
	Event    *event;
	Blocked  *blocked;

	nih_assert (message != NULL);
	nih_assert (name != NULL);
	nih_assert (env != NULL);

	if (! control_check_permission (message)) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.PermissionDenied",
			_("You do not have permission to emit an event"));
		return -1;
	}

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

	if (! control_check_permission (message)) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.PermissionDenied",
			_("You do not have permission to set log priority"));
		return -1;
	}

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
 * control_get_bus_type:
 *
 * Determine D-Bus bus type to connect to.
 *
 * Returns: Type of D-Bus bus to connect to.
 **/
DBusBusType
control_get_bus_type (void)
{
	return (use_session_bus || user_mode) 
		? DBUS_BUS_SESSION
		: DBUS_BUS_SYSTEM;
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
 * Notes: Session Inits are permitted to make this call. In the common
 * case of starting a Session Init as a child of a Display Manager this
 * is somewhat meaningless, but it does mean that if a Session Init were
 * started from a system job, behaviour would be as expected.
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

	if (! control_check_permission (message)) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.PermissionDenied",
			_("You do not have permission to notify disk is writeable"));
		return -1;
	}

	/* Get the relevant session */
	session = session_from_dbus (NULL, message);

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
 * control_notify_dbus_address:
 * @data: not used,
 * @message: D-Bus connection and message received,
 * @address: Address of D-Bus to connect to.
 *
 * Implements the NotifyDBusAddress method of the
 * com.ubuntu.Upstart interface.
 *
 * Called to allow the Session Init to connect to the D-Bus
 * Session Bus when available.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
control_notify_dbus_address (void            *data,
			     NihDBusMessage  *message,
			     const char      *address)
{
	nih_assert (message);
	nih_assert (address);

	if (getpid () == 1) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.PermissionDenied",
			_("Not permissible to notify D-Bus address for PID 1"));
		return -1;
	}

	if (! control_check_permission (message)) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.PermissionDenied",
			_("You do not have permission to notify D-Bus address"));
		return -1;
	}

	/* Ignore as already connected */
	if (control_bus)
		return 0;

	control_bus_address = nih_strdup (NULL, address);
	if (! control_bus_address) {
		nih_dbus_error_raise_printf (DBUS_ERROR_NO_MEMORY,
				_("Out of Memory"));
		return -1;
	}

	if (control_bus_open () < 0)
		return -1;

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
 * @connection: D-Bus connection.
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
	size_t    len;

	nih_assert (message);
	nih_assert (state);

	if (! control_check_permission (message)) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.PermissionDenied",
			_("You do not have permission to request state"));
		return -1;
	}

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

	nih_assert (message != NULL);

	if (! control_check_permission (message)) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.PermissionDenied",
			_("You do not have permission to request restart"));
		return -1;
	}

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

	nih_info (_("Restarting"));

	stateful_reexec ();

	return 0;
}

/**
 * control_notify_event_emitted
 *
 * @event: Event.
 *
 * Re-emits an event over DBUS using the EventEmitted signal
 **/
void
control_notify_event_emitted (Event *event)
{
	nih_assert (event != NULL);

	control_init ();

	NIH_LIST_FOREACH (control_conns, iter) {
		NihListEntry   *entry = (NihListEntry *)iter;
		DBusConnection *conn = (DBusConnection *)entry->data;

		NIH_ZERO (control_emit_event_emitted (conn, DBUS_PATH_UPSTART,
							    event->name, event->env));
	}
}

/**
 * control_notify_restarted
 *
 * DBUS signal sent when upstart has re-executed itself.
 **/
void
control_notify_restarted (void)
{
	control_init ();

	NIH_LIST_FOREACH (control_conns, iter) {
		NihListEntry   *entry = (NihListEntry *)iter;
		DBusConnection *conn = (DBusConnection *)entry->data;

		NIH_ZERO (control_emit_restarted (conn, DBUS_PATH_UPSTART));
	}
}

/**
 * control_set_env_list:
 *
 * @data: not used,
 * @message: D-Bus connection and message received,
 * @job_details: name and instance of job to apply operation to,
 * @vars: array of name[/value] pairs of environment variables to set,
 * @replace: TRUE if @name should be overwritten if already set, else
 *  FALSE.
 *
 * Implements the SetEnvList method of the com.ubuntu.Upstart
 * interface.
 *
 * Called to request Upstart store one or more name/value pairs.
 *
 * If @job_details is empty, change will be applied to all job
 * environments, else only apply changes to specific job environment
 * encoded within @job_details.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
control_set_env_list (void            *data,
		      NihDBusMessage  *message,
		      char * const    *job_details,
		      char * const    *vars,
		      int              replace)
{
	Session         *session;
	Job             *job = NULL;
	char            *job_name = NULL;
	char            *instance = NULL;
	char * const    *var;

	nih_assert (message);
	nih_assert (job_details);
	nih_assert (vars);

	if (! control_check_permission (message)) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.PermissionDenied",
			_("You do not have permission to modify job environment"));
		return -1;
	}

	if (job_details[0]) {
		job_name = job_details[0];

		/* this can be a null value */
		instance = job_details[1];
	} else if (getpid () == 1) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.PermissionDenied",
			_("Not permissible to modify PID 1 job environment"));
		return -1;
	}

	/* Verify that job name is valid */
	if (job_name && ! strlen (job_name)) {
		nih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,
					     _("Job may not be empty string"));
		return -1;
	}

	/* Get the relevant session */
	session = session_from_dbus (NULL, message);

	/* Chroot sessions must not be able to influence
	 * the outside system.
	 */
	if (session && session->chroot) {
		nih_warn (_("Ignoring set env request from chroot session"));
		return 0;
	}

	/* Lookup the job */
	control_get_job (session, job, job_name, instance);

	for (var = vars; var && *var; var++) {
		nih_local char *envvar = NULL;

		if (! *var) {
			nih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,
					_("Variable may not be empty string"));
			return -1;
		}

		/* If variable does not contain a delimiter, add one to ensure
		 * it gets entered into the job environment table. Without the
		 * delimiter, the variable will be silently ignored unless it's
		 * already set in inits environment. But in that case there is
		 * no point in setting such a variable to its already existing
		 * value.
		 */
		if (! strchr (*var, '=')) {
			envvar = NIH_MUST (nih_sprintf (NULL, "%s=", *var));
		} else {
			envvar = NIH_MUST (nih_strdup (NULL, *var));
		}

		if (job) {
			/* Modify job-specific environment */
			nih_assert (job->env);

			NIH_MUST (environ_add (&job->env, job, NULL, replace, envvar));
		} else if (job_class_environment_set (envvar, replace) < 0) {
			nih_return_no_memory_error (-1);
		}
	}

	return 0;
}

/**
 * control_set_env:
 *
 * @data: not used,
 * @message: D-Bus connection and message received,
 * @job_details: name and instance of job to apply operation to,
 * @var: name[/value] pair of environment variable to set,
 * @replace: TRUE if @name should be overwritten if already set, else
 *  FALSE.
 *
 * Implements the SetEnv method of the com.ubuntu.Upstart
 * interface.
 *
 * Called to request Upstart store a particular name/value pair.
 *
 * If @job_details is empty, change will be applied to all job
 * environments, else only apply changes to specific job environment
 * encoded within @job_details.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
control_set_env (void            *data,
		 NihDBusMessage  *message,
		 char * const    *job_details,
		 const char      *var,
		 int              replace)
{
	if (! var) {
		nih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,
					_("Variable may not be empty string"));
		return -1;
	}
	nih_local char **vars = NULL;
	
	vars = NIH_MUST (nih_str_array_new (NULL));

	NIH_MUST (nih_str_array_add (&vars, NULL, NULL, var));

	return control_set_env_list (data, message, job_details, vars, replace);
}

/**
 * control_unset_env_list:
 *
 * @data: not used,
 * @message: D-Bus connection and message received,
 * @job_details: name and instance of job to apply operation to,
 * @names: array of variables to clear from the job environment array.
 *
 * Implements the UnsetEnvList method of the com.ubuntu.Upstart
 * interface.
 *
 * Called to request Upstart remove one or more variables from the job
 * environment array.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
control_unset_env_list (void            *data,
			NihDBusMessage  *message,
			char * const    *job_details,
			char * const    *names)
{
	Session         *session;
	Job             *job = NULL;
	char            *job_name = NULL;
	char            *instance = NULL;
	char * const    *name;

	nih_assert (message);
	nih_assert (job_details);
	nih_assert (names);

	if (! control_check_permission (message)) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.PermissionDenied",
			_("You do not have permission to modify job environment"));
		return -1;
	}

	if (job_details[0]) {
		job_name = job_details[0];

		/* this can be a null value */
		instance = job_details[1];
	} else if (getpid () == 1) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.PermissionDenied",
			_("Not permissible to modify PID 1 job environment"));
		return -1;
	}

	/* Verify that job name is valid */
	if (job_name && ! strlen (job_name)) {
		nih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,
					     _("Job may not be empty string"));
		return -1;
	}

	/* Get the relevant session */
	session = session_from_dbus (NULL, message);

	/* Chroot sessions must not be able to influence
	 * the outside system.
	 */
	if (session && session->chroot) {
		nih_warn (_("Ignoring unset env request from chroot session"));
		return 0;
	}

	/* Lookup the job */
	control_get_job (session, job, job_name, instance);

	for (name = names; name && *name; name++) {
		if (! *name) {
			nih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,
					_("Variable may not be empty string"));
			return -1;
		}

		if (job) {
			/* Modify job-specific environment */
			nih_assert (job->env);

			if (! environ_remove (&job->env, job, NULL, *name))
				return -1;
		} else if (job_class_environment_unset (*name) < 0) {
			goto error;
		}
	}

	return 0;

error:
	nih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,
			"%s: %s",
			_("No such variable"), *name);
	return -1;
}

/**
 * control_unset_env:
 *
 * @data: not used,
 * @message: D-Bus connection and message received,
 * @job_details: name and instance of job to apply operation to,
 * @name: variable to clear from the job environment array.
 *
 * Implements the UnsetEnv method of the com.ubuntu.Upstart
 * interface.
 *
 * Called to request Upstart remove a particular variable from the job
 * environment array.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
control_unset_env (void            *data,
		   NihDBusMessage  *message,
		   char * const    *job_details,
		   const char      *name)
{
	if (! name) {
		nih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,
					_("Variable may not be empty string"));
		return -1;
	}
	nih_local char **names = NULL;
	
	names = NIH_MUST (nih_str_array_new (NULL));

	NIH_MUST (nih_str_array_add (&names, NULL, NULL, name));

	return control_unset_env_list (data, message, job_details, names);
}

/**
 * control_get_env:
 *
 * @data: not used,
 * @message: D-Bus connection and message received,
 * @job_details: name and instance of job to apply operation to,
 * @name: name of environment variable to retrieve,
 * @value: value of @name.
 *
 * Implements the GetEnv method of the com.ubuntu.Upstart
 * interface.
 *
 * Called to obtain the value of a specified job environment variable.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
control_get_env (void             *data,
		 NihDBusMessage   *message,
		 char * const     *job_details,
		 const char       *name,
		 char            **value)
{
	Session     *session;
	const char  *tmp;
	Job         *job = NULL;
	char        *job_name = NULL;
	char        *instance = NULL;

	nih_assert (message != NULL);
	nih_assert (job_details);

	if (! control_check_permission (message)) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.PermissionDenied",
			_("You do not have permission to query job environment"));
		return -1;
	}

	if (! name || ! *name) {
		nih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,
				_("Variable may not be empty string"));
		return -1;
	}

	if (job_details[0]) {
		job_name = job_details[0];

		/* this can be a null value */
		instance = job_details[1];
	}

	/* Verify that job name is valid */
	if (job_name && ! strlen (job_name)) {
		nih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,
					     _("Job may not be empty string"));
		return -1;
	}

	/* Get the relevant session */
	session = session_from_dbus (NULL, message);

	/* Chroot sessions must not be able to influence
	 * the outside system.
	 */
	if (session && session->chroot) {
		nih_warn (_("Ignoring get env request from chroot session"));
		return 0;
	}

	/* Lookup the job */
	control_get_job (session, job, job_name, instance);

	if (job) {
		tmp = environ_get (job->env, name);
		if (! tmp)
			goto error;

		*value = nih_strdup (message, tmp);
		if (! *value)
			nih_return_no_memory_error (-1);

		return 0;
	}

	tmp = job_class_environment_get (name);

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

/**
 * control_list_env:
 *
 * @data: not used,
 * @message: D-Bus connection and message received,
 * @job_details: name and instance of job to apply operation to,
 * @env: pointer to array of all job environment variables.
 *
 * Implements the ListEnv method of the com.ubuntu.Upstart
 * interface.
 *
 * Called to obtain an unsorted array of all environment variables
 * that will be set in a jobs environment.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
control_list_env (void             *data,
		 NihDBusMessage    *message,
		 char * const      *job_details,
		 char            ***env)
{
	Session   *session;
	Job       *job = NULL;
	char      *job_name = NULL;
	char      *instance = NULL;

	nih_assert (message);
	nih_assert (job_details);
	nih_assert (env);

	if (! control_check_permission (message)) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.PermissionDenied",
			_("You do not have permission to query job environment"));
		return -1;
	}

	if (job_details[0]) {
		job_name = job_details[0];

		/* this can be a null value */
		instance = job_details[1];
	}

	/* Verify that job name is valid */
	if (job_name && ! strlen (job_name)) {
		nih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,
					     _("Job may not be empty string"));
		return -1;
	}

	/* Get the relevant session */
	session = session_from_dbus (NULL, message);

	/* Lookup the job */
	control_get_job (session, job, job_name, instance);

	if (job) {
		*env = nih_str_array_copy (job, NULL, job->env);
		if (! *env)
			nih_return_no_memory_error (-1);

		return 0;
	}

	*env = job_class_environment_get_all (message);
	if (! *env)
		nih_return_no_memory_error (-1);

	return 0;
}

/**
 * control_reset_env:
 *
 * @data: not used,
 * @message: D-Bus connection and message received,
 * @job_details: name and instance of job to apply operation to.
 *
 * Implements the ResetEnv method of the com.ubuntu.Upstart
 * interface.
 *
 * Called to reset the environment all subsequent jobs will run in to
 * the default minimal environment.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
control_reset_env (void           *data,
		 NihDBusMessage   *message,
		 char * const    *job_details)
{
	Session    *session;
	Job        *job = NULL;
	char       *job_name = NULL;
	char       *instance = NULL;

	nih_assert (message);
	nih_assert (job_details);

	if (! control_check_permission (message)) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.PermissionDenied",
			_("You do not have permission to modify job environment"));
		return -1;
	}

	if (job_details[0]) {
		job_name = job_details[0];

		/* this can be a null value */
		instance = job_details[1];
	} else if (getpid () == 1) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.PermissionDenied",
			_("Not permissible to modify PID 1 job environment"));
		return -1;
	}

	/* Verify that job name is valid */
	if (job_name && ! strlen (job_name)) {
		nih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,
					     _("Job may not be empty string"));
		return -1;
	}

	/* Get the relevant session */
	session = session_from_dbus (NULL, message);

	/* Chroot sessions must not be able to influence
	 * the outside system.
	 */
	if (session && session->chroot) {
		nih_warn (_("Ignoring reset env request from chroot session"));
		return 0;
	}

	/* Lookup the job */
	control_get_job (session, job, job_name, instance);


	if (job) {
		size_t len;
		if (job->env) {
			nih_free (job->env);
			job->env = NULL;
		}

		job->env = job_class_environment (job, job->class, &len);
		if (! job->env)
			nih_return_system_error (-1);

		return 0;
	}

	job_class_environment_reset ();

	return 0;
}

/**
 * control_get_origin_uid:
 * @message: D-Bus connection and message received,
 * @uid: returned uid value.
 *
 * Returns TRUE: if @uid now contains uid corresponding to @message,
 * else FALSE.
 **/
static int
control_get_origin_uid (NihDBusMessage *message, uid_t *uid)
{
	DBusError       dbus_error;
	unsigned long   unix_user = 0;
	const char     *sender;

	nih_assert (message);
	nih_assert (uid);

	dbus_error_init (&dbus_error);

	if (! message->message || ! message->connection)
		return FALSE;

	sender = dbus_message_get_sender (message->message);
	if (sender) {
		unix_user = dbus_bus_get_unix_user (message->connection, sender,
						    &dbus_error);
		if (unix_user == (unsigned long)-1) {
			dbus_error_free (&dbus_error);
			return FALSE;
		}
	} else {
		if (! dbus_connection_get_unix_user (message->connection,
						     &unix_user)) {
			return FALSE;
		}
	}

	*uid = (uid_t)unix_user;

	return TRUE;
}

/**
 * control_check_permission:
 *
 * @message: D-Bus connection and message received.
 *
 * Determine if caller should be allowed to make a control request.
 *
 * Note that these permission checks rely on D-Bus to limit
 * session bus access to the same user.
 *
 * Returns: TRUE if permission is granted, else FALSE.
 **/
static int
control_check_permission (NihDBusMessage *message)
{
	int    ret;
	uid_t  uid;
	pid_t  pid;
	uid_t  origin_uid = 0;

	nih_assert (message);

	uid = getuid ();
	pid = getpid ();

	ret = control_get_origin_uid (message, &origin_uid);

	/* Its possible that D-Bus might be unable to determine the user
	 * making the request. In this case, deny the request unless
	 * we're running as a Session Init or via the test harness.
	 */
	if ((ret && origin_uid == uid) || user_mode || (uid && pid != 1))
		return TRUE;

	return FALSE;
}

/**
 * control_session_file_create:
 *
 * Create session file if possible.
 *
 * Errors are not fatal - the file is just not created.
 **/
static void
control_session_file_create (void)
{
	nih_local char *session_dir = NULL;
	FILE           *f;
	int             ret;

	nih_assert (control_server_address);

	session_dir = get_session_dir ();

	if (! session_dir)
		return;

	NIH_MUST (nih_strcat_sprintf (&session_file, NULL, "%s/%d%s",
				session_dir, (int)getpid (), SESSION_EXT));

	f = fopen (session_file, "w");
	if (! f) {
		nih_error ("%s: %s", _("unable to create session file"), session_file);
		return;
	}

	ret = fprintf (f, SESSION_ENV "=%s\n", control_server_address);

	if (ret < 0)
		nih_error ("%s: %s", _("unable to write session file"), session_file);

	fclose (f);
}

/**
 * control_session_file_remove:
 *
 * Delete session file.
 *
 * Errors are not fatal.
 **/
static void
control_session_file_remove (void)
{
	if (session_file)
		(void)unlink (session_file);
}

/**
 * control_session_end:
 *
 * @data: not used,
 * @message: D-Bus connection and message received.
 *
 * Implements the EndSession method of the com.ubuntu.Upstart
 * interface.
 *
 * Called to request that Upstart stop all jobs and exit. Only
 * appropriate when running as a Session Init and user wishes to
 * 'logout'.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
control_end_session (void             *data,
		     NihDBusMessage   *message)
{
	Session  *session;

	nih_assert (message);

	/* Not supported at the system level */
	if (getpid () == 1)
		return 0;

	if (! control_check_permission (message)) {
		nih_dbus_error_raise_printf (
			DBUS_INTERFACE_UPSTART ".Error.PermissionDenied",
			_("You do not have permission to end session"));
		return -1;
	}

	/* Get the relevant session */
	session = session_from_dbus (NULL, message);

	if (session && session->chroot) {
		nih_warn (_("Ignoring session end request from chroot session"));
		return 0;
	}

	quiesce (QUIESCE_REQUESTER_SESSION);

	return 0;
}

/**
 * control_serialise_bus_address:
 *
 * Convert control_bus_address into JSON representation.
 *
 * Returns: JSON string representing control_bus_address or NULL if
 * control_bus_address not set or on error.
 *
 * Note: If NULL is returned, check the value of control_bus_address
 * itself to determine if the error is real.
 **/
json_object *
control_serialise_bus_address (void)
{
	control_init ();

	/* A NULL return represents a JSON null */
	return control_bus_address
		? json_object_new_string (control_bus_address)
		: NULL;
}

/**
 * control_deserialise_bus_address:
 *
 * @json: root of JSON-serialised state.
 *
 * Convert JSON representation of control_bus_address back into a native
 * string.
 *
 * Returns: 0 on success, -1 on error.
 **/
int
control_deserialise_bus_address (json_object *json)
{
	const char  *address;

	nih_assert (json);
	nih_assert (! control_bus_address);

	control_init ();

	/* control_bus_address was never set */
	if (state_check_json_type (json, null))
		return 0;

	if (! state_check_json_type (json, string))
		goto error;

	address = json_object_get_string (json);
	if (! address)
		goto error;

	control_bus_address = nih_strdup (NULL, address);
	if (! control_bus_address)
		goto error;

	return 0;

error:
	return -1;
}
