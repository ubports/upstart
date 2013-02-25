/* upstart
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

#ifndef INIT_CONTROL_H
#define INIT_CONTROL_H

#include <dbus/dbus.h>

#include <nih/macros.h>
#include <nih/list.h>

#include <nih-dbus/dbus_connection.h>
#include <nih-dbus/dbus_message.h>

#include <json.h>

#include "event.h"
#include "quiesce.h"

/**
 * USE_SESSION_BUS_ENV:
 *
 * If this environment variable is set to any value, connect to
 * D-Bus session bus rather than the system bus.
 *
 * Used for testing.
 **/
#ifndef USE_SESSION_BUS_ENV
#define USE_SESSION_BUS_ENV "UPSTART_USE_SESSION_BUS"
#endif

/**
 * control_get_job:
 * 
 * @session: session,
 * @job: job that will be set,
 * @job_name: name of job to search for,
 * @instance: instance of @job_name to search for.
 *
 * Determine the Job associated with @job_name and @instance and set it
 * to @job.
 *
 * Returns: -1 on raised error, or nothing on success.
 **/
#define control_get_job(session, job, job_name, instance)            \
{                                                                    \
	if (job_name != NULL ) {                                     \
		JobClass *class;                                     \
                                                                     \
		class = job_class_find (session, job_name);          \
		if (! class) {                                       \
			nih_dbus_error_raise_printf (                \
				DBUS_INTERFACE_UPSTART               \
				".Error.UnknownJob",                 \
				_("Unknown job: %s"),                \
				job_name);                           \
			return -1;                                   \
		}                                                    \
                                                                     \
		job = job_find (session, class, NULL, instance);     \
		if (job == NULL) {                                   \
			nih_dbus_error_raise_printf (                \
				DBUS_INTERFACE_UPSTART               \
				".Error.UnknownJobInstance",         \
				_("Unknown instance: %s of job %s"), \
				instance, job_name);                 \
			return -1;                                   \
		}                                                    \
	}                                                            \
}

NIH_BEGIN_EXTERN

extern DBusServer     *control_server;
extern DBusConnection *control_bus;

extern NihList        *control_conns;


void control_init                 (void);
void control_cleanup              (void);

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
				   const char *name, char * const *env,
				   int wait)
	__attribute__ ((warn_unused_result));
int  control_emit_event_with_file (void *data, NihDBusMessage *message,
				   const char *name, char * const *env,
				   int wait, int file)
	__attribute__ ((warn_unused_result));

int  control_get_version          (void *data, NihDBusMessage *message,
				   char **version)
	__attribute__ ((warn_unused_result));

int  control_get_log_priority     (void *data, NihDBusMessage *message,
				   char **log_priority)
	__attribute__ ((warn_unused_result));
int  control_set_log_priority     (void *data, NihDBusMessage *message,
				   const char *log_priority)
	__attribute__ ((warn_unused_result));

void control_handle_bus_type      (void);

void control_prepare_reexec       (void);

int control_conn_to_index (const DBusConnection *connection)
	__attribute__ ((warn_unused_result));

DBusConnection *
control_conn_from_index (int conn_index)
	__attribute__ ((warn_unused_result));

int control_bus_release_name (void)
	__attribute__ ((warn_unused_result));

int control_get_state (void           *data,
		   NihDBusMessage  *message,
		   char           **state)
	__attribute__ ((warn_unused_result));

int  control_restart (void *data, NihDBusMessage *message)
	__attribute__ ((warn_unused_result));

void control_notify_event_emitted (Event *event);
void control_notify_restarted (void);

int control_set_env (void           *data,
		 NihDBusMessage *message,
		 char * const    *job_details,
		 const char     *var,
		 int             replace)
	__attribute__ ((warn_unused_result));

int control_get_env (void             *data,
		 NihDBusMessage   *message,
		 char * const     *job_details,
		 const char       *name,
		 char            **value)
	__attribute__ ((warn_unused_result));

int
control_list_env (void             *data,
		 NihDBusMessage   *message,
		 char * const     *job_details,
		 char           ***env)
	__attribute__ ((warn_unused_result));

int
control_reset_env (void           *data,
		 NihDBusMessage   *message,
		 char * const    *job_details)
	__attribute__ ((warn_unused_result));

int
control_unset_env (void            *data,
		   NihDBusMessage  *message,
		   char * const    *job_details,
		   const char      *name)
	__attribute__ ((warn_unused_result));

NIH_END_EXTERN

#endif /* INIT_CONTROL_H */
