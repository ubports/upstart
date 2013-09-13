/* upstart
 *
 * Copyright (C) 2010 Scott James Remnant <scott@netsplit.com>.
 *
 * This file was automatically generated; see the source for copying
 * conditions.
 */

#ifndef UPSTART_UPSTART_COM_UBUNTU_UPSTART_JOB_H
#define UPSTART_UPSTART_COM_UBUNTU_UPSTART_JOB_H

#include <dbus/dbus.h>

#include <stdint.h>

#include <nih/macros.h>

#include <nih-dbus/dbus_interface.h>
#include <nih-dbus/dbus_message.h>
#include <nih-dbus/dbus_pending_data.h>
#include <nih-dbus/dbus_proxy.h>


typedef struct job_class_properties {
	char *  name;
	char *  description;
	char *  author;
	char *  version;
	char *  usage;
	char ***start_on;
	char ***stop_on;
	char ** emits;
} JobClassProperties;


typedef void (*JobClassGetInstanceReply) (void *data, NihDBusMessage *message, const char *instance);

typedef void (*JobClassGetInstanceByNameReply) (void *data, NihDBusMessage *message, const char *instance);

typedef void (*JobClassGetAllInstancesReply) (void *data, NihDBusMessage *message, char * const *instances);

typedef void (*JobClassStartReply) (void *data, NihDBusMessage *message, const char *instance);

typedef void (*JobClassStopReply) (void *data, NihDBusMessage *message);

typedef void (*JobClassRestartReply) (void *data, NihDBusMessage *message, const char *instance);

typedef void (*JobClassInstanceAddedHandler) (void *data, NihDBusMessage *message, const char *instance);

typedef void (*JobClassInstanceRemovedHandler) (void *data, NihDBusMessage *message, const char *instance);

typedef void (*JobClassGetNameReply) (void *data, NihDBusMessage *message, const char *value);

typedef void (*JobClassGetDescriptionReply) (void *data, NihDBusMessage *message, const char *value);

typedef void (*JobClassGetAuthorReply) (void *data, NihDBusMessage *message, const char *value);

typedef void (*JobClassGetVersionReply) (void *data, NihDBusMessage *message, const char *value);

typedef void (*JobClassGetUsageReply) (void *data, NihDBusMessage *message, const char *value);

typedef void (*JobClassGetStartOnReply) (void *data, NihDBusMessage *message, char ** const *value);

typedef void (*JobClassGetStopOnReply) (void *data, NihDBusMessage *message, char ** const *value);

typedef void (*JobClassGetEmitsReply) (void *data, NihDBusMessage *message, char * const *value);

typedef void (*JobClassGetAllReply) (void *data, NihDBusMessage *message, const JobClassProperties *properties);


NIH_BEGIN_EXTERN

extern const NihDBusInterface  job_class_com_ubuntu_Upstart0_6_Job;
extern const NihDBusInterface *job_class_interfaces[];


DBusPendingCall *job_class_get_instance              (NihDBusProxy *proxy, char * const *env, JobClassGetInstanceReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              job_class_get_instance_sync         (const void *parent, NihDBusProxy *proxy, char * const *env, char **instance)
	__attribute__ ((warn_unused_result));
DBusPendingCall *job_class_get_instance_by_name      (NihDBusProxy *proxy, const char *name, JobClassGetInstanceByNameReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              job_class_get_instance_by_name_sync (const void *parent, NihDBusProxy *proxy, const char *name, char **instance)
	__attribute__ ((warn_unused_result));
DBusPendingCall *job_class_get_all_instances         (NihDBusProxy *proxy, JobClassGetAllInstancesReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              job_class_get_all_instances_sync    (const void *parent, NihDBusProxy *proxy, char ***instances)
	__attribute__ ((warn_unused_result));
DBusPendingCall *job_class_start                     (NihDBusProxy *proxy, char * const *env, int wait, JobClassStartReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              job_class_start_sync                (const void *parent, NihDBusProxy *proxy, char * const *env, int wait, char **instance)
	__attribute__ ((warn_unused_result));
DBusPendingCall *job_class_stop                      (NihDBusProxy *proxy, char * const *env, int wait, JobClassStopReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              job_class_stop_sync                 (const void *parent, NihDBusProxy *proxy, char * const *env, int wait)
	__attribute__ ((warn_unused_result));
DBusPendingCall *job_class_restart                   (NihDBusProxy *proxy, char * const *env, int wait, JobClassRestartReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              job_class_restart_sync              (const void *parent, NihDBusProxy *proxy, char * const *env, int wait, char **instance)
	__attribute__ ((warn_unused_result));
DBusPendingCall *job_class_get_name                  (NihDBusProxy *proxy, JobClassGetNameReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              job_class_get_name_sync             (const void *parent, NihDBusProxy *proxy, char **value)
	__attribute__ ((warn_unused_result));
DBusPendingCall *job_class_get_description           (NihDBusProxy *proxy, JobClassGetDescriptionReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              job_class_get_description_sync      (const void *parent, NihDBusProxy *proxy, char **value)
	__attribute__ ((warn_unused_result));
DBusPendingCall *job_class_get_author                (NihDBusProxy *proxy, JobClassGetAuthorReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              job_class_get_author_sync           (const void *parent, NihDBusProxy *proxy, char **value)
	__attribute__ ((warn_unused_result));
DBusPendingCall *job_class_get_version               (NihDBusProxy *proxy, JobClassGetVersionReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              job_class_get_version_sync          (const void *parent, NihDBusProxy *proxy, char **value)
	__attribute__ ((warn_unused_result));
DBusPendingCall *job_class_get_usage                 (NihDBusProxy *proxy, JobClassGetUsageReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              job_class_get_usage_sync            (const void *parent, NihDBusProxy *proxy, char **value)
	__attribute__ ((warn_unused_result));
DBusPendingCall *job_class_get_start_on              (NihDBusProxy *proxy, JobClassGetStartOnReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              job_class_get_start_on_sync         (const void *parent, NihDBusProxy *proxy, char ****value)
	__attribute__ ((warn_unused_result));
DBusPendingCall *job_class_get_stop_on               (NihDBusProxy *proxy, JobClassGetStopOnReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              job_class_get_stop_on_sync          (const void *parent, NihDBusProxy *proxy, char ****value)
	__attribute__ ((warn_unused_result));
DBusPendingCall *job_class_get_emits                 (NihDBusProxy *proxy, JobClassGetEmitsReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              job_class_get_emits_sync            (const void *parent, NihDBusProxy *proxy, char ***value)
	__attribute__ ((warn_unused_result));
DBusPendingCall *job_class_get_all                   (NihDBusProxy *proxy, JobClassGetAllReply handler, NihDBusErrorHandler error_handler, void *data, int timeout)
	__attribute__ ((warn_unused_result));
int              job_class_get_all_sync              (const void *parent, NihDBusProxy *proxy, JobClassProperties **properties)
	__attribute__ ((warn_unused_result));

NIH_END_EXTERN

#endif /* UPSTART_UPSTART_COM_UBUNTU_UPSTART_JOB_H */
