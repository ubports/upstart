/* upstart
 *
 * control.c - D-Bus connections, objects and methods
 *
 * Copyright © 2009 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */

#include <dbus/dbus.h>

#include <stdio.h>
#include <string.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/io.h>
#include <nih/logging.h>
#include <nih/error.h>
#include <nih/errors.h>

#include <nih-dbus/dbus_error.h>
#include <nih-dbus/dbus_connection.h>
#include <nih-dbus/dbus_message.h>
#include <nih-dbus/dbus_object.h>

#include "environ.h"
#include "job_class.h"
#include "blocked.h"
#include "conf.h"
#include "control.h"
#include "errors.h"

#include "com.ubuntu.Upstart.h"


/**
 * CONTORL_SERVER_ADDRESS:
 *
 * D-Bus address of our internal server used for private connections.
 **/
#define CONTROL_SERVER_ADDRESS "unix:abstract=/com/ubuntu/upstart"

/**
 * CONTROL_BUS_NAME:
 *
 * Well-known name that we register on the system bus so that clients may
 * contact us.
 **/
#define CONTROL_BUS_NAME "com.ubuntu.Upstart"


/* Prototypes for static functions */
static int   control_server_connect (DBusServer *server, DBusConnection *conn);
static void  control_disconnected   (DBusConnection *conn);
static void  control_register_all   (DBusConnection *conn);


/**
 * control_server:
 *
 * D-Bus server listening for new direct connections.
 **/
DBusServer *control_server = NULL;

/**
 * control_bus:
 *
 * Open connection to D-Bus system bus.  The connection may be opened with
 * control_bus_open() and if lost will become NULL.
 **/
DBusConnection *control_bus = NULL;

/**
 * control_conns:
 *
 * Open control connections, including the connection to the D-Bus system
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

	server = nih_dbus_server (CONTROL_SERVER_ADDRESS,
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
	nih_assert (control_server != NULL);

	dbus_server_disconnect (control_server);
	dbus_server_unref (control_server);

	control_server = NULL;
}


/**
 * control_bus_open:
 *
 * Open a connection to the D-Bus system bus and store it in the control_bus
 * global.  The connection is handled automatically in the main loop.
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

	/* Connect to the D-Bus System Bus and hook everything up into
	 * our own main loop automatically.
	 */
	conn = nih_dbus_bus (DBUS_BUS_SYSTEM, control_disconnected);
	if (! conn)
		return -1;

	/* Register objects on the bus. */
	control_register_all (conn);

	/* Request our well-known name.  We do this last so that once it
	 * appears on the bus, clients can assume we're ready to talk to
	 * them.
	 */
	dbus_error_init (&error);
	ret = dbus_bus_request_name (conn, CONTROL_BUS_NAME,
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
 * is about to be list.  We clear the connection from our current list
 * and drop the control_bus global if relevant.
 **/
static void
control_disconnected (DBusConnection *conn)
{
	nih_assert (conn != NULL);

	if (conn == control_bus) {
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
	NIH_MUST (nih_dbus_object_new (NULL, conn, CONTROL_ROOT,
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
	JobClass *class;

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

	/* Lookup the job and copy its path into the reply */
	class = (JobClass *)nih_hash_lookup (job_classes, name);
	if (! class) {
		nih_dbus_error_raise_printf (
			"com.ubuntu.Upstart.Error.UnknownJob",
			_("Unknown job: %s"), name);
		return -1;
	}

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
	char   **list;
	size_t   len;

	nih_assert (message != NULL);
	nih_assert (jobs != NULL);

	job_class_init ();

	len = 0;
	list = nih_str_array_new (message);
	if (! list)
		nih_return_system_error (-1);

	NIH_HASH_FOREACH (job_classes, iter) {
		JobClass *class = (JobClass *)iter;

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


/**
 * control_emit_event:
 * @data: not used,
 * @message: D-Bus connection and message received,
 * @name: name of event to emit,
 * @env: environment of environment.
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
 * Returns: zero on success, negative value on raised error.
 **/
int
control_emit_event (void            *data,
		    NihDBusMessage  *message,
		    const char      *name,
		    char * const    *env)
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
		return -1;
	}

	/* Verify that the environment is valid */
	if (! environ_all_valid (env)) {
		nih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,
					     _("Env must be KEY=VALUE pairs"));
		return -1;
	}

	/* Make the event and block the message on it */
	event = event_new (NULL, name, (char **)env);
	if (! event)
		nih_return_system_error (-1);

	blocked = blocked_new (event, BLOCKED_EMIT_METHOD, message);
	if (! blocked) {
		nih_error_raise_system ();
		nih_free (event);
		return -1;
	}

	nih_list_add (&event->blocking, &blocked->entry);

	return 0;
}
