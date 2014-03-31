/* TODO:FIXME:XXX:
 * 
 * - XXX: cgroup_manager_connect() may block!
 *
 * - XXX: block job from starting unless cgroup_support_enabled() and
 *   cgmanager_connected().
 *
 *   - XXX: need to avoid events being destroyed if a job needs them but
 *     it is blocked on the cgmanager starting!!
 *
 * - test scenarios:
 *   - setuid + cgroup
 *   - setgid + cgroup
 *   - ensure *ALL* job pids are put into the cgroup.
 *
 * - test_state.c: test re-exec with cgroups.
 * - test_jobclass.c: test class->cgroups is an empty list.
 * - cgroup path for a job: "upstart/$UPSTART_JOB/$UPSTART_INSTANCE/$requested"
 */

/* upstart
 *
 * cgroup.c - cgroup support.
 *
 * Copyright © 2013 Canonical Ltd.
 * Author: James Hunt <james.hunt@canonical.com>.
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

#ifndef INIT_CGROUP_H
#define INIT_CGROUP_H

#include <nih/hash.h>
#include <json.h>

/* FIXME: document */
#define DBUS_ADDRESS_CGMANAGER "unix:path=/sys/fs/cgroup/cgmanager/sock"
#define DBUS_SERVICE_CGMANAGER "org.linuxcontainers.cgmanager"
#define DBUS_INTERFACE_CGMANAGER "org.linuxcontainers.cgmanager0_0"
#define DBUS_PATH_CGMANAGER "/org/linuxcontainers/cgmanager"

/* FIXME: remove - make spawning async!! */
/**
 * CGROUP_MANAGER_TIMEOUT:
 *
 * Time in seconds that the CGroup manager must respond within
 * before it is considered to have stalled/died. Required to avoid
 * blocking PID 1.
 **/
#define CGROUP_MANAGER_TIMEOUT 3

/**
 * CGroupSetting:
 *
 * Representation of a control group setting.
 *
 * Control groups are implemented as directories created under a
 * special sysfs sub-directory mount. These directories contain files
 * created by the kernel. Some of these files represent tunables
 * such that values written into them modify the behaviour of the parent
 * cgroup. A CGroupSetting represents the meta-data to be written to
 * such a tunable cgroup file.
 *
 * @entry: list header,
 * @key: setting to change,
 * @value: value of @key.
 **/
typedef struct cgroup_setting {
	NihList         entry;
	char           *key;
	char           *value;
} CGroupSetting;

/**
 * CGroupName:
 *
 * @entry: list header,
 * @name: name of cgroup,
 * @expanded: value of @name where all variables have been expanded
 *  (or NULL if expanded value is the same as @name),
 * @settings: list of CGroupSettings.
 *
 * Representation of a control group name.
 *
 * Note: @name is in fact a relative path fragment which can optionally
 * contain embedded variables which will be expanded and which will be created
 * below the appropriate cgroup controller.
 **/
typedef struct cgroup_name {
	NihList         entry;
	char           *name;
	char           *expanded;
	NihList         settings;
} CGroupName;

/**
 * CGroup:
 *
 * @entry: list header,
 * @controller: cgroup controller name,
 * @names: list of CGroupName objects.
 *
 * Representation of a control group.
 *
 * Note: @cgroups must contain atleast one entry since a control group
 * is represented by its controlling subsystem and a name.
 **/
typedef struct cgroup {
	NihList         entry;
	char           *controller;
	NihList         names;
} CGroup;

NIH_BEGIN_EXTERN

void cgroup_init (void);

int cgroup_support_enabled (void)
	__attribute__ ((warn_unused_result));

CGroupName *cgroup_name_new (void *parent, const char *name)
	__attribute__ ((warn_unused_result));

json_object *cgroup_name_serialise (CGroupName *name)
	__attribute__ ((warn_unused_result));

json_object *cgroup_name_serialise_all (NihList *names)
	__attribute__ ((warn_unused_result));

CGroupName *cgroup_name_deserialise (void *parent, json_object *json)
	__attribute__ ((warn_unused_result));

int cgroup_name_deserialise_all (void *parent,
			     NihList *list,
			     json_object *json)
	__attribute__ ((warn_unused_result));

json_object *cgroup_serialise (CGroup *group)
	__attribute__ ((warn_unused_result));

json_object *cgroup_serialise_all (NihList *cgroups)
	__attribute__ ((warn_unused_result));

CGroup *cgroup_deserialise (void *parent, json_object *json)
	__attribute__ ((warn_unused_result));

int cgroup_deserialise_all (void *parent,
			NihList *list,
			json_object *json)
	__attribute__ ((warn_unused_result));

CGroupSetting *cgroup_setting_new (void *parent,
		const char *key, const char *value)
	__attribute__ ((warn_unused_result));

CGroup *cgroup_new (void *parent, const char *controller)
	__attribute__ ((warn_unused_result));

int cgroup_manager_connect (const char *address)
	__attribute__ ((warn_unused_result));

int cgroup_manager_connected (void)
	__attribute__ ((warn_unused_result));

int cgroup_setup (NihList *cgroups, char * const *env,
		uid_t uid, gid_t gid)
	__attribute__ ((warn_unused_result));

int cgroup_chown (const char *controller,
		  const char *path,
		  uid_t uid, gid_t gid)
	__attribute__ ((warn_unused_result));

/* FIXME */
#if 0
int cgroup_cleanup (NihList *paths)
	__attribute__ ((warn_unused_result));
#endif

json_object *cgroup_manager_serialise (void)
	__attribute__ ((warn_unused_result));

int cgroup_manager_deserialise (json_object *json)
	__attribute__ ((warn_unused_result));

json_object *cgroup_serialise_all (NihList *cgroups)
	__attribute__ ((warn_unused_result));

json_object *cgroup_setting_serialise (const CGroupSetting *setting)
	__attribute__ ((warn_unused_result));

json_object *cgroup_setting_serialise_all (NihList *settings)
	__attribute__ ((warn_unused_result));

CGroupSetting *cgroup_setting_deserialise (void *parent,
		json_object *json)
	__attribute__ ((warn_unused_result));

int cgroup_setting_deserialise_all (void *parent, NihList *list,
		json_object *json)
	__attribute__ ((warn_unused_result));

int cgroup_add (void *parent, NihList *list, const char *controller,
		const char *name, const char *key, const char *value)
	__attribute__ ((warn_unused_result));

int cgroup_cleanup (NihList *cgroups)
	__attribute__ ((warn_unused_result));

int cgroup_create (const char *controller, const char *path)
	__attribute__ ((warn_unused_result));

int cgroup_enter (const char *controller, const char *path, pid_t pid)
	__attribute__ ((warn_unused_result));

int cgroup_enter_groups (NihList  *cgroups)
	__attribute__ ((warn_unused_result));

int cgroup_settings_apply (const char *controller,
		const char *path, NihList *settings)
	__attribute__ ((warn_unused_result));

#if 0
int cgroup_expand_paths (void *parent, NihList *cgroups,
		char * const  *env)
	__attribute__ ((warn_unused_result));

int cgroup_apply_paths (void)
	__attribute__ ((warn_unused_result));
#endif

NIH_END_EXTERN

#endif /* INIT_CGROUP_H */
