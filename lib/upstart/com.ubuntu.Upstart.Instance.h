/* upstart
 *
 * Copyright (C) 2010 Scott James Remnant <scott@netsplit.com>.
 *
 * This file was automatically generated; see the source for copying
 * conditions.
 */

#ifndef UPSTART_UPSTART_COM_UBUNTU_UPSTART_INSTANCE_H
#define UPSTART_UPSTART_COM_UBUNTU_UPSTART_INSTANCE_H

#include <dbus/dbus.h>

#include <stdint.h>

#include <nih/macros.h>

#include <nih-dbus/dbus_interface.h>
#include <nih-dbus/dbus_message.h>
#include <nih-dbus/dbus_pending_data.h>
#include <nih-dbus/dbus_proxy.h>


typedef struct job_processes_element {
	char *  item0;
	int32_t item1;
} JobProcessesElement;

typedef struct job_properties {
	char *                name;
	char *                goal;
	char *                state;
	JobProcessesElement **processes;
} JobProperties;


typedef void (*JobStartReply) (void *data, NihDBusMessage *message);

typedef void (*JobStopReply) (void *data, NihDBusMessage *message);

typedef void (*JobRestartReply) (void *data, NihDBusMessage *message);

typedef void (*JobReloadReply) (void *data, NihDBusMessage *message);

typedef void (*JobGoalChangedHandler) (void *data, NihDBusMessage *message, const char *goal);

typedef void (*JobStateChangedHandler) (void *data, NihDBusMessage *message, const char *state);

typedef void (*JobFailedHandler) (void *data, NihDBusMessage *message, int32_t status);

typedef void (*JobGetNameReply) (void *data, NihDBusMessage *message, const char *value);

typedef void (*JobGetGoalReply) (void *data, NihDBusMessage *message, const char *value);

typedef void (*JobGetStateReply) (void *data, NihDBusMessage *message, const char *value);

typedef void (*JobGetProcessesReply) (void *data, NihDBusMessage *message, JobProcessesElement * const *value);

typedef void (*JobGetAllReply) (void *data, NihDBusMessage *message, const JobProperties *properties);


NIH_BEGIN_EXTERN

extern const NihDBusInterface  job_com_ubuntu_Upstart0_6_Instance;
extern const NihDBusInterface *job_interfaces[];


DBusPendingCall *job_start              (NihDBusProxy *proxy, int wait, JobStartReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              job_start_sync         (const void *parent, NihDBusProxy *proxy, int wait)
	__attribute__ ((warn_unused_result));
DBusPendingCall *job_stop               (NihDBusProxy *proxy, int wait, JobStopReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              job_stop_sync          (const void *parent, NihDBusProxy *proxy, int wait)
	__attribute__ ((warn_unused_result));
DBusPendingCall *job_restart            (NihDBusProxy *proxy, int wait, JobRestartReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              job_restart_sync       (const void *parent, NihDBusProxy *proxy, int wait)
	__attribute__ ((warn_unused_result));
DBusPendingCall *job_reload             (NihDBusProxy *proxy, JobReloadReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              job_reload_sync        (const void *parent, NihDBusProxy *proxy)
	__attribute__ ((warn_unused_result));
DBusPendingCall *job_get_name           (NihDBusProxy *proxy, JobGetNameReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              job_get_name_sync      (const void *parent, NihDBusProxy *proxy, char **value)
	__attribute__ ((warn_unused_result));
DBusPendingCall *job_get_goal           (NihDBusProxy *proxy, JobGetGoalReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              job_get_goal_sync      (const void *parent, NihDBusProxy *proxy, char **value)
	__attribute__ ((warn_unused_result));
DBusPendingCall *job_get_state          (NihDBusProxy *proxy, JobGetStateReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              job_get_state_sync     (const void *parent, NihDBusProxy *proxy, char **value)
	__attribute__ ((warn_unused_result));
DBusPendingCall *job_get_processes      (NihDBusProxy *proxy, JobGetProcessesReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              job_get_processes_sync (const void *parent, NihDBusProxy *proxy, JobProcessesElement ***value)
	__attribute__ ((warn_unused_result));
DBusPendingCall *job_get_all            (NihDBusProxy *proxy, JobGetAllReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              job_get_all_sync       (const void *parent, NihDBusProxy *proxy, JobProperties **properties)
	__attribute__ ((warn_unused_result));

NIH_END_EXTERN

#endif /* UPSTART_UPSTART_COM_UBUNTU_UPSTART_INSTANCE_H */
