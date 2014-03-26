/* TODO:FIXME:XXX:
 *
 * - XXX: block job from starting unless cgroup_support_enabled() and
 *   cgmanager_connected().
 *
 *   - XXX: need to avoid events being destroyed if a job needs them but
 *     it is blocked on the cgmanager starting!!
 *
 * - XXX: BUG: XXX : cgroup_setup() calls cgroup_create_path() which stores the
 *   expanded cgroup path in the NihHash, but it's being called from the
 *   child side of the fork so is lost! Problem would be solved when we
 *   can perform async process start since cgroup_setup can be called
 *   from the parent side.
 *
 * - test cgmanager stall/timeouts carefully!
 * - call cgmanager_rmdir() when job ends if CGPath blockers is 1.
 *
 * - test_state.c: test re-exec with cgroups.
 * - test_jobclass.c: test class->cgroups is an empty list.
 * - cgroup path for a job: "upstart/$UPSTART_JOB/$UPSTART_INSTANCE/$requested"
 * - FIXME: Who should own each path element?
 * - FIXME: check in global CGPath hash before creating a path!
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
 * @settings: list of CGroupSettings.
 *
 * Representation of a control group name.
 *
 * Note: @name is in fact a relative path fragment which may contain
 * slashes and where embedded variables will be expanded.
 **/
typedef struct cgroup_name {
	NihList         entry;
	char           *name;
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

/**
 * CGroupPath:
 * @entry: list header,
 * @path: Fully-expanded relative path of cgroup,
 * @blockers: Number of jobs that require @path.
 *
 * Used to track paths the CGroup manager has created on behalf of one
 * or more jobs.
 *
 * Entries are stored in cgroup_paths to allow fast lookup of paths that
 * can be deleted once a job ends (a path will be deleted once @blockers
 * falls to zero). Note that extracting the paths via each JobClass is
 * insufficient since those paths are not fully expanded (they may
 * contain variables).
 **/
typedef struct cgroup_path {
	NihList         entry;
	char           *path;
	int             blockers;
} CGroupPath;

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

int cgroup_setup (NihList *paths, char * const *env)
	__attribute__ ((warn_unused_result));

int cgroup_cleanup (NihList *paths)
	__attribute__ ((warn_unused_result));

CGroupPath *cgroup_path_new (void *parent, const char *path)
	__attribute__ ((warn_unused_result));

json_object *cgroup_manager_serialise (void)
	__attribute__ ((warn_unused_result));

int cgroup_manager_deserialise (json_object *json)
	__attribute__ ((warn_unused_result));

json_object *cgroup_serialise_all (NihList *cgroups)
	__attribute__ ((warn_unused_result));

json_object *cgroup_path_serialise (const CGroupPath *cgpath)
	__attribute__ ((warn_unused_result));

CGroupPath *cgroup_path_deserialise (json_object *json)
	__attribute__ ((warn_unused_result));

json_object *cgroup_path_serialise_all (void)
	__attribute__ ((warn_unused_result));

int cgroup_path_deserialise_all (json_object *json)
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

int cgroup_create_path (const char *controller, const char *path)
	__attribute__ ((warn_unused_result));

int cgroup_enter (const char *controller, const char *path, pid_t pid)
	__attribute__ ((warn_unused_result));

int cgroup_settings_apply (const char *controller,
		const char *path, NihList *settings)
	__attribute__ ((warn_unused_result));

int cgroup_setup_paths (void *parent, char ***paths, NihList *cgroups, char * const  *env)
	__attribute__ ((warn_unused_result));

int cgroup_apply_paths (void)
	__attribute__ ((warn_unused_result));

NIH_END_EXTERN

#endif /* INIT_CGROUP_H */
