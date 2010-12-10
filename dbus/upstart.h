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

#ifndef DBUS_UPSTART_H
#define DBUS_UPSTART_H


/**
 * DBUS_SERVICE_UPSTART:
 *
 * The well-known name used by Upstart on the system bus.
 **/
#ifndef DBUS_SERVICE_UPSTART
# ifdef DEBUG
#  define DBUS_SERVICE_UPSTART "com.ubuntu.TestUpstart"
# else
#  define DBUS_SERVICE_UPSTART "com.ubuntu.Upstart"
#endif
#endif


/**
 * DBUS_PATH_UPSTART:
 *
 * The object path used by the manager object, and used as the root path
 * for all other objects.
 **/
#define DBUS_PATH_UPSTART "/com/ubuntu/Upstart"


/**
 * DBUS_INTERFACE_UPSTART:
 *
 * The interface exported by the manager object.
 **/
#define DBUS_INTERFACE_UPSTART "com.ubuntu.Upstart0_6"

/**
 * DBUS_INTERFACE_UPSTART_JOB:
 *
 * The interface exported by job objects.
 **/
#define DBUS_INTERFACE_UPSTART_JOB "com.ubuntu.Upstart0_6.Job"

/**
 * DBUS_INTERFACE_UPSTART_INSTANCE:
 *
 * The interface exported by instance objects.
 **/
#define DBUS_INTERFACE_UPSTART_INSTANCE "com.ubuntu.Upstart0_6.Instance"


/**
 * DBUS_ADDRESS_UPSTART:
 *
 * The address where the private D-Bus server inside Upstart can be
 * found.
 **/
#ifndef DBUS_ADDRESS_UPSTART
# ifdef DEBUG
#  define DBUS_ADDRESS_UPSTART "unix:abstract=/com/ubuntu/test_upstart"
# else
#  define DBUS_ADDRESS_UPSTART "unix:abstract=/com/ubuntu/upstart"
# endif
#endif


#endif /* DBUS_UPSTART_H */
