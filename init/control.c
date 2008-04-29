/* upstart
 *
 * control.c - D-Bus connections, objects and methods
 *
 * Copyright © 2008 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
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

#include <dbus/dbus.h>

#include <stdio.h>
#include <string.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/io.h>
#include <nih/logging.h>
#include <nih/error.h>
#include <nih/errors.h>

#include <nih/dbus.h>

#include "control.h"
#include "errors.h"


/* Prototypes for static functions */
static void  control_bus_disconnected (DBusConnection *conn);
static int   control_register_all     (DBusConnection *conn);


/**
 * control_bus:
 *
 * Open connection to D-Bus system bus.  The connection may be opened with
 * control_bus_open() and if lost will become NULL.
 **/
DBusConnection *control_bus = NULL;

/**
 * control_manager:
 *
 * Interfaces exported by the control manager object.
 **/
const static NihDBusInterface *control_manager[] = {
	NULL
};


/**
 * control_bus_open:
 *
 * Open a connection to the D-Bus system bus and store it in the control_bus
 * global.  The connection is handled automatically in the main loop and
 * will be closed should we exec() a different process.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
control_bus_open (void)
{
	DBusConnection *conn;
	DBusError       error;
	int             fd, ret;

	nih_assert (control_bus == NULL);

	/* Connect to the D-Bus System Bus and hook everything up into
	 * our own main loop automatically.
	 */
	conn = nih_dbus_bus (DBUS_BUS_SYSTEM, control_bus_disconnected);
	if (! conn)
		return -1;

	/* In theory all D-Bus file descriptors are set to be closed on exec
	 * anyway, but there's no harm in making damned sure since that's
	 * not actually documented anywhere that I can tell.
	 */
	if (dbus_connection_get_unix_fd (conn, &fd))
		nih_io_set_cloexec (fd);

	/* Register objects on the bus. */
	if (control_register_all (conn) < 0) {
		errno = ENOMEM;
		nih_error_raise_system ();

		dbus_connection_unref (conn);
		return -1;
	}

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

	control_bus = conn;

	return 0;
}

/**
 * control_bus_disconnected:
 *
 * This function is called when the connection to the D-Bus system bus is
 * dropped and our reference is about to be lost.  We simply clear the
 * control_bus global.
 **/
static void
control_bus_disconnected (DBusConnection *conn)
{
	nih_assert (conn != NULL);

	if (control_bus)
		nih_warn (_("Disconnected from system bus"));

	control_bus = NULL;
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

	control_bus = NULL;
}


/**
 * control_register_all:
 * @conn: connection to register objects for.
 *
 * Registers the manager object and objects for all jobs and instances on
 * the given connection.
 **/
static int
control_register_all (DBusConnection *conn)
{

	/* Register the manager object, this is the primary point of contact
	 * for clients.  We only check for success, otherwise we're happy
	 * to let this object be tied to the lifetime of the connection.
	 */
	if (! nih_dbus_object_new (NULL, conn, CONTROL_ROOT, control_manager,
				   NULL))
		return -1;

	/* FIXME register objects for jobs and their instances */

	return 0;
}
