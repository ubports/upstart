/* upstart
 *
 * Copyright (C) 2010 Scott James Remnant <scott@netsplit.com>.
 *
 * This file was automatically generated; see the source for copying
 * conditions.
 */

#ifndef UPSTART_UPSTART_COM_UBUNTU_UPSTART_H
#define UPSTART_UPSTART_COM_UBUNTU_UPSTART_H

#include <dbus/dbus.h>

#include <stdint.h>

#include <nih/macros.h>

#include <nih-dbus/dbus_interface.h>
#include <nih-dbus/dbus_message.h>
#include <nih-dbus/dbus_pending_data.h>
#include <nih-dbus/dbus_proxy.h>


typedef struct upstart_properties {
	char *version;
	char *log_priority;
} UpstartProperties;


typedef void (*UpstartReloadConfigurationReply) (void *data, NihDBusMessage *message);

typedef void (*UpstartGetJobByNameReply) (void *data, NihDBusMessage *message, const char *job);

typedef void (*UpstartGetAllJobsReply) (void *data, NihDBusMessage *message, char * const *jobs);

typedef void (*UpstartGetStateReply) (void *data, NihDBusMessage *message, const char *state);

typedef void (*UpstartRestartReply) (void *data, NihDBusMessage *message);

typedef void (*UpstartGetEnvReply) (void *data, NihDBusMessage *message, const char *value);

typedef void (*UpstartSetEnvReply) (void *data, NihDBusMessage *message);

typedef void (*UpstartSetEnvListReply) (void *data, NihDBusMessage *message);

typedef void (*UpstartUnsetEnvReply) (void *data, NihDBusMessage *message);

typedef void (*UpstartUnsetEnvListReply) (void *data, NihDBusMessage *message);

typedef void (*UpstartListEnvReply) (void *data, NihDBusMessage *message, char * const *env);

typedef void (*UpstartResetEnvReply) (void *data, NihDBusMessage *message);

typedef void (*UpstartEmitEventReply) (void *data, NihDBusMessage *message);

typedef void (*UpstartEmitEventWithFileReply) (void *data, NihDBusMessage *message);

typedef void (*UpstartNotifyDiskWriteableReply) (void *data, NihDBusMessage *message);

typedef void (*UpstartNotifyDbusAddressReply) (void *data, NihDBusMessage *message);

typedef void (*UpstartNotifyCgroupManagerAddressReply) (void *data, NihDBusMessage *message);

typedef void (*UpstartEndSessionReply) (void *data, NihDBusMessage *message);

typedef void (*UpstartJobAddedHandler) (void *data, NihDBusMessage *message, const char *job);

typedef void (*UpstartJobRemovedHandler) (void *data, NihDBusMessage *message, const char *job);

typedef void (*UpstartEventEmittedHandler) (void *data, NihDBusMessage *message, const char *name, char * const *env);

typedef void (*UpstartRestartedHandler) (void *data, NihDBusMessage *message);

typedef void (*UpstartGetVersionReply) (void *data, NihDBusMessage *message, const char *value);

typedef void (*UpstartGetLogPriorityReply) (void *data, NihDBusMessage *message, const char *value);

typedef void (*UpstartSetLogPriorityReply) (void *data, NihDBusMessage *message);

typedef void (*UpstartGetAllReply) (void *data, NihDBusMessage *message, const UpstartProperties *properties);


NIH_BEGIN_EXTERN

extern const NihDBusInterface  upstart_com_ubuntu_Upstart0_6;
extern const NihDBusInterface *upstart_interfaces[];


DBusPendingCall *upstart_reload_configuration               (NihDBusProxy *proxy, UpstartReloadConfigurationReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              upstart_reload_configuration_sync          (const void *parent, NihDBusProxy *proxy)
	__attribute__ ((warn_unused_result));
DBusPendingCall *upstart_get_job_by_name                    (NihDBusProxy *proxy, const char *name, UpstartGetJobByNameReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              upstart_get_job_by_name_sync               (const void *parent, NihDBusProxy *proxy, const char *name, char **job)
	__attribute__ ((warn_unused_result));
DBusPendingCall *upstart_get_all_jobs                       (NihDBusProxy *proxy, UpstartGetAllJobsReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              upstart_get_all_jobs_sync                  (const void *parent, NihDBusProxy *proxy, char ***jobs)
	__attribute__ ((warn_unused_result));
DBusPendingCall *upstart_get_state                          (NihDBusProxy *proxy, UpstartGetStateReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              upstart_get_state_sync                     (const void *parent, NihDBusProxy *proxy, char **state)
	__attribute__ ((warn_unused_result));
DBusPendingCall *upstart_restart                            (NihDBusProxy *proxy, UpstartRestartReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              upstart_restart_sync                       (const void *parent, NihDBusProxy *proxy)
	__attribute__ ((warn_unused_result));
DBusPendingCall *upstart_get_env                            (NihDBusProxy *proxy, char * const *job_details, const char *name, UpstartGetEnvReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              upstart_get_env_sync                       (const void *parent, NihDBusProxy *proxy, char * const *job_details, const char *name, char **value)
	__attribute__ ((warn_unused_result));
DBusPendingCall *upstart_set_env                            (NihDBusProxy *proxy, char * const *job_details, const char *var, int replace, UpstartSetEnvReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              upstart_set_env_sync                       (const void *parent, NihDBusProxy *proxy, char * const *job_details, const char *var, int replace)
	__attribute__ ((warn_unused_result));
DBusPendingCall *upstart_set_env_list                       (NihDBusProxy *proxy, char * const *job_details, char * const *vars, int replace, UpstartSetEnvListReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              upstart_set_env_list_sync                  (const void *parent, NihDBusProxy *proxy, char * const *job_details, char * const *vars, int replace)
	__attribute__ ((warn_unused_result));
DBusPendingCall *upstart_unset_env                          (NihDBusProxy *proxy, char * const *job_details, const char *name, UpstartUnsetEnvReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              upstart_unset_env_sync                     (const void *parent, NihDBusProxy *proxy, char * const *job_details, const char *name)
	__attribute__ ((warn_unused_result));
DBusPendingCall *upstart_unset_env_list                     (NihDBusProxy *proxy, char * const *job_details, char * const *name, UpstartUnsetEnvListReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              upstart_unset_env_list_sync                (const void *parent, NihDBusProxy *proxy, char * const *job_details, char * const *name)
	__attribute__ ((warn_unused_result));
DBusPendingCall *upstart_list_env                           (NihDBusProxy *proxy, char * const *job_details, UpstartListEnvReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              upstart_list_env_sync                      (const void *parent, NihDBusProxy *proxy, char * const *job_details, char ***env)
	__attribute__ ((warn_unused_result));
DBusPendingCall *upstart_reset_env                          (NihDBusProxy *proxy, char * const *job_details, UpstartResetEnvReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              upstart_reset_env_sync                     (const void *parent, NihDBusProxy *proxy, char * const *job_details)
	__attribute__ ((warn_unused_result));
DBusPendingCall *upstart_emit_event                         (NihDBusProxy *proxy, const char *name, char * const *env, int wait, UpstartEmitEventReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              upstart_emit_event_sync                    (const void *parent, NihDBusProxy *proxy, const char *name, char * const *env, int wait)
	__attribute__ ((warn_unused_result));
DBusPendingCall *upstart_emit_event_with_file               (NihDBusProxy *proxy, const char *name, char * const *env, int wait, int file, UpstartEmitEventWithFileReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              upstart_emit_event_with_file_sync          (const void *parent, NihDBusProxy *proxy, const char *name, char * const *env, int wait, int file)
	__attribute__ ((warn_unused_result));
DBusPendingCall *upstart_notify_disk_writeable              (NihDBusProxy *proxy, UpstartNotifyDiskWriteableReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              upstart_notify_disk_writeable_sync         (const void *parent, NihDBusProxy *proxy)
	__attribute__ ((warn_unused_result));
DBusPendingCall *upstart_notify_dbus_address                (NihDBusProxy *proxy, const char *address, UpstartNotifyDbusAddressReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              upstart_notify_dbus_address_sync           (const void *parent, NihDBusProxy *proxy, const char *address)
	__attribute__ ((warn_unused_result));
DBusPendingCall *upstart_notify_cgroup_manager_address      (NihDBusProxy *proxy, const char *address, UpstartNotifyCgroupManagerAddressReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              upstart_notify_cgroup_manager_address_sync (const void *parent, NihDBusProxy *proxy, const char *address)
	__attribute__ ((warn_unused_result));
DBusPendingCall *upstart_end_session                        (NihDBusProxy *proxy, UpstartEndSessionReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              upstart_end_session_sync                   (const void *parent, NihDBusProxy *proxy)
	__attribute__ ((warn_unused_result));
DBusPendingCall *upstart_get_version                        (NihDBusProxy *proxy, UpstartGetVersionReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              upstart_get_version_sync                   (const void *parent, NihDBusProxy *proxy, char **value)
	__attribute__ ((warn_unused_result));
DBusPendingCall *upstart_get_log_priority                   (NihDBusProxy *proxy, UpstartGetLogPriorityReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              upstart_get_log_priority_sync              (const void *parent, NihDBusProxy *proxy, char **value)
	__attribute__ ((warn_unused_result));
DBusPendingCall *upstart_set_log_priority                   (NihDBusProxy *proxy, const char *value, UpstartSetLogPriorityReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              upstart_set_log_priority_sync              (const void *parent, NihDBusProxy *proxy, const char *value)
	__attribute__ ((warn_unused_result));
DBusPendingCall *upstart_get_all                            (NihDBusProxy *proxy, UpstartGetAllReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              upstart_get_all_sync                       (const void *parent, NihDBusProxy *proxy, UpstartProperties **properties)
	__attribute__ ((warn_unused_result));

NIH_END_EXTERN

#endif /* UPSTART_UPSTART_COM_UBUNTU_UPSTART_H */
