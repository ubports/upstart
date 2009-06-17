/* upstart
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

#ifndef INIT_CONTROL_H
#define INIT_CONTROL_H

#include <dbus/dbus.h>

#include <nih/macros.h>

#include <nih-dbus/dbus_connection.h>
#include <nih-dbus/dbus_message.h>


/**
 * CONTROL_ROOT:
 *
 * Well-known object name that we register for the manager object, and that
 * we use as the root path for all of our other objects.
 **/
#define CONTROL_ROOT "/com/ubuntu/Upstart"


NIH_BEGIN_EXTERN

DBusServer     *control_server;
DBusConnection *control_bus;

NihList        *control_conns;


void control_init                 (void);

int  control_server_open          (void)
	__attribute__ ((warn_unused_result));
void control_server_close         (void);

int  control_bus_open             (void)
	__attribute__ ((warn_unused_result));
void control_bus_close            (void);

int  control_reload_configuration (void *data, NihDBusMessage *message)
	__attribute__ ((warn_unused_result));

int  control_get_job_by_name      (void *data, NihDBusMessage *message,
				   const char *name, char **job)
	__attribute__ ((warn_unused_result));
int  control_get_all_jobs         (void *data, NihDBusMessage *message,
				   char ***jobs)
	__attribute__ ((warn_unused_result));

int  control_emit_event           (void *data, NihDBusMessage *message,
				   const char *name, char * const *env)
	__attribute__ ((warn_unused_result));

NIH_END_EXTERN

#endif /* INIT_CONTROL_H */
