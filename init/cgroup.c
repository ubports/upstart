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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */

#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>

#include <nih/macros.h>
#include <nih/logging.h>
#include <nih/string.h>
#include <nih/hash.h>
#include <nih/alloc.h>
#include <nih/io.h>

#include <dbus/dbus.h>

#include <nih-dbus/dbus_error.h>
#include <nih-dbus/dbus_proxy.h>
#include <nih-dbus/errors.h>
#include <nih-dbus/dbus_connection.h>
#include <nih-dbus/dbus_message.h>

#include "environ.h"
#include "errors.h"
#include "state.h"
#include "cgroup.h"

#include <cgmanager/cgmanager-client.h>

/* FIXME */
#if 1
#include "early_assert.c"
#endif

/**
 * disable_cgroups:
 *
 * If TRUE, make the cgroup stanza a NOP.
 **/
int disable_cgroups = FALSE;

/**
 * cgroup_manager_address:
 *
 * Address on which the CGroup Manager may be reached.
 **/
char *cgroup_manager_address = NULL;

/**
 * cgroup_manager:
 *
 * Proxy to the CGroup Manager.
 **/
NihDBusProxy *cgroup_manager = NULL;

#if 0
static int cgroup_path_unref (const char *controller, const char *path)
	__attribute__ ((warn_unused_result));
#endif

static void cgroup_manager_disconnected (DBusConnection *connection);

//static void cgroup_manager_lost_handler (void *data, NihDBusProxy *proxy);

#if 0
static void cgroup_manager_error_handler (void *data, NihDBusMessage  *message)
	__attribute__ ((unused)); /* FIXME */
#endif

static void cgroup_name_remap (char *str);

/**
 * cgroup_support_enabled:
 *
 * Determine if cgroup support is currently enabled.
 *
 * Returns: TRUE if enabled, else FALSE.
 **/
int
cgroup_support_enabled (void)
{
	return ! disable_cgroups;
}

/**
 * cgroup_new:
 * @parent: parent of new CGroup object,
 * @controller: cgroup controller name.
 *
 * Allocates and returns a new CGroup object.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.
 *
 * Returns: newly allocated CGroup or NULL if insufficient memory.
 **/
CGroup *
cgroup_new (void *parent, const char *controller)
{
	CGroup *cgroup;

	/* FIXME */
#if 1
	static int done = 0;
	if (! done) {
		nih_log_set_logger (nih_logger_james);
		done = 1;
	}
#endif

	nih_assert (controller);

	cgroup = nih_new (parent, CGroup);
	if (! cgroup)
		return NULL;

	nih_list_init (&cgroup->entry);

	nih_alloc_set_destructor (cgroup, nih_list_destroy);

	cgroup->controller = nih_strdup (cgroup, controller);
	if (! cgroup->controller)
		goto error;

	nih_list_init (&cgroup->names);

	return cgroup;

error:
	nih_free (cgroup);
	return NULL;
}

/**
 * cgroup_serialise:
 * @cgroup: CGroup to serialise.
 *
 * Convert @cgroup into a JSON representation for serialisation.
 * Caller must free returned value using json_object_put().
 *
 * Returns: JSON-serialised CGroup object, or NULL on error.
 **/
json_object *
cgroup_serialise (CGroup *cgroup)
{
	json_object  *json;
	json_object  *json_names;

	nih_assert (cgroup);

	json = json_object_new_object ();
	if (! json)
		return NULL;

	if (! state_set_json_string_var_from_obj (json, cgroup, controller))
		goto error;

	json_names = cgroup_name_serialise_all (&cgroup->names);
	if (! json_names)
		goto error;

	json_object_object_add (json, "names", json_names);

	return json;

error:
	json_object_put (json);
	return NULL;
}

/**
 * cgroup_serialise_all:
 *
 * @cgroups: list of CGroup objects.
 *
 * Convert @cgroups to JSON representation.
 *
 * Returns: JSON object containing array of CGroup object in JSON form,
 * or NULL on error.
 **/
json_object *
cgroup_serialise_all (NihList *cgroups)
{
	json_object  *json;
	json_object  *json_cgroup;

	nih_assert (cgroups);

	json = json_object_new_array ();
	if (! json)
		return NULL;

	NIH_LIST_FOREACH (cgroups, iter) {
		CGroup *cgroup = (CGroup *)iter;

		json_cgroup = cgroup_serialise (cgroup);
		if (! json_cgroup)
			goto error;

		json_object_array_add (json, json_cgroup);
	}

	return json;

error:
	json_object_put (json);
	return NULL;
}

/**
 * cgroup_deserialise:
 * @parent: parent of new CGroup object,
 * @json: JSON-serialised CGroup object to deserialise.
 *
 * Convert @json into a CGroup object.
 *
 * Returns: CGroup object, or NULL on error.
 **/
CGroup *
cgroup_deserialise (void *parent, json_object *json)
{
	nih_local char  *controller = NULL;
	CGroup          *cgroup;

	nih_assert (json);

	if (! state_check_json_type (json, object))
		return NULL;

	if (! state_get_json_string_var (json, "controller", NULL, controller))
		return NULL;

	cgroup = cgroup_new (parent, controller);
	if (! cgroup)
		return NULL;

	if (cgroup_name_deserialise_all (cgroup, &cgroup->names, json) < 0)
		goto error;

	return cgroup;

error:
	nih_free (cgroup);
	return NULL;
}

/**
 * cgroup_deserialise_all:
 *
 * @parent: parent of new CGroup objects,
 * @list: list to add new CGroup objects to,
 * @json: root of JSON-serialised state.
 *
 * Convert JSON representation of CGroup objects back into
 * CGroup objects.
 *
 * Returns: 0 on success, -1 on error.
 **/
int
cgroup_deserialise_all (void         *parent,
			NihList      *list,
			json_object  *json)
{
	json_object  *json_cgroups;

	nih_assert (list);
	nih_assert (json);

	json_cgroups = json_object_object_get (json, "cgroups");

	if (! json_cgroups)
		goto error;

	for (int i = 0; i < json_object_array_length (json_cgroups); i++) {
		json_object  *json_cgroup;
		CGroup       *cgroup;

		json_cgroup = json_object_array_get_idx (json_cgroups, i);
		if (! json_cgroup)
			goto error;

		if (! state_check_json_type (json_cgroup, object))
			goto error;

		cgroup = cgroup_deserialise (parent, json_cgroup);
		if (! cgroup)
			goto error;

		nih_list_add (list, &cgroup->entry);
	}

	return 0;

error:
	return -1;
}

/**
 * cgroup_setup:
 *
 * @cgroups: list of CGroup objects,
 * @env: environment table,
 * @uid: user id that should own the created cgroup,
 * @gid: group id that should own the created cgroup.
 *
 * Use @env to expand all variables in the cgroup names specified
 * in @cgroups, create the resulting cgroup paths, placing the caller
 * into each group and applying requested cgroup settings.
 *
 * Returns: TRUE on success, FALSE on raised error.
 **/
int
cgroup_setup (NihList *cgroups, char * const *env, uid_t uid, gid_t gid)
{
	const char       *upstart_job = NULL;
	const char       *upstart_instance = NULL;
	nih_local char   *suffix = NULL;
	nih_local char  **cgroup_env = NULL;
	nih_local char   *envvar = NULL;
	int               instance = FALSE;
	uid_t             current_uid;
	gid_t             current_gid;

	/* Value of $UPSTART_CGROUP which takes the form:
	 *
	 *     upstart/${UPSTART_JOB}
	 *
	 * Or for instance jobs:
	 *
	 *     upstart/${UPSTART_JOB}-${UPSTART_INSTANCE}
	 *
	 * */
	nih_local char   *upstart_cgroup = NULL;

	nih_assert (cgroups);
	nih_assert (env);

	if (! cgroup_support_enabled ())
		return TRUE;

#if 0
	if (! cgroup_manager_connected ())
		return TRUE;
#endif

	if (NIH_LIST_EMPTY (cgroups))
		return TRUE;

	current_uid = geteuid ();
	current_gid = getegid ();

	cgroup_env = nih_str_array_new (NULL);
	if (! cgroup_env)
		nih_return_no_memory_error (FALSE);

	/* Copy the existing environment table */
	if (! environ_append (&cgroup_env, NULL, NULL, TRUE, env))
		nih_return_no_memory_error (FALSE);

	upstart_job = environ_get (cgroup_env, "UPSTART_JOB");
	nih_assert (upstart_job);

	upstart_instance = environ_get (cgroup_env, "UPSTART_INSTANCE");
	nih_assert (upstart_instance);

	if (*upstart_instance)
		instance = TRUE;

	suffix = nih_sprintf (NULL, "%s%s%s",
			upstart_job,
			instance ? "-" : "",
			instance ? upstart_instance : "");

	if (! suffix)
		nih_return_no_memory_error (FALSE);

	/* Remap the standard prefix to avoid creating sub-cgroups erroneously */
	cgroup_name_remap (suffix);

	upstart_cgroup = nih_sprintf (NULL, "upstart/%s", suffix);

	if (! upstart_cgroup)
		nih_return_no_memory_error (FALSE);

	/* FIXME */
	nih_message ("XXX:%s:%d: UPSTART_INSTANCE='%s'", __func__, __LINE__, upstart_instance);

	envvar = NIH_MUST (nih_sprintf (NULL, "%s=%s",
				UPSTART_CGROUP_ENVVAR,
				upstart_cgroup));

	/* FIXME */
	nih_message ("XXX:%s:%d: upstart_cgroup='%s'", __func__, __LINE__, upstart_cgroup);
	nih_message ("XXX:%s:%d: envvar='%s'", __func__, __LINE__, envvar);

	if (! environ_add (&cgroup_env, NULL, NULL, TRUE, envvar))
		nih_return_no_memory_error (FALSE);

	NIH_LIST_FOREACH (cgroups, iter) {
		CGroup *cgroup = (CGroup *)iter;

		NIH_LIST_FOREACH (&cgroup->names, iter2) {
			CGroupName      *cgname = (CGroupName *)iter2;
			nih_local char  *cgroup_path = NULL;

			cgroup_path = NIH_SHOULD (environ_expand (NULL,
						cgname->name,
						cgroup_env));

			if (! cgroup_path) {
				/* Failure to expand variables is an error */
				return FALSE;
			}

			/* Remap slash to underscore to avoid unexpected
			 * sub-cgroup creation.
			 */
			cgroup_name_remap (cgroup_path);

			/* FIXME */
			nih_message ("XXX:%s:%d: controller='%s', cgroup_path='%s'", __func__, __LINE__,
					cgroup->controller,
					cgroup_path);

			if (! cgroup_create (cgroup->controller, cgroup_path))
				return FALSE;

			nih_message ("XXX:%s:%d:", __func__, __LINE__);

			if (! cgroup_settings_apply (cgroup->controller,
						cgroup_path,
						&cgname->settings))
				return FALSE;

			nih_message ("XXX:%s:%d:", __func__, __LINE__);

			if ((uid == current_uid) && gid == current_gid) {
				/* No need to chown */
				continue;
			}

			if (! cgroup_chown (cgroup->controller, cgroup_path, uid, gid))
				return FALSE;

			nih_message ("XXX:%s:%d:", __func__, __LINE__);
		}
	}

	nih_message ("XXX:%s:%d:", __func__, __LINE__);

	return TRUE;
}

/**
 * cgroup_name_new:
 *
 * @parent: parent of new CGroupName.
 * @controller: CGroup controller name,
 * @name: name of cgroup to create,
 * @key: cgroup setting name (optional),
 * @value: cgroup setting vlaue (optional if @key not specified).
 *
 * Allocates and returns a new CGroupName object.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.
 *
 * Returns: newly allocated CGroupName or NULL if insufficient memory.
 **/
CGroupName *
cgroup_name_new (void *parent, const char *name)
{
	CGroupName *cgroup;

	nih_assert (name);

	cgroup = nih_new (parent, CGroupName);
	if (! cgroup)
		return NULL;

	nih_list_init (&cgroup->entry);

	nih_alloc_set_destructor (cgroup, nih_list_destroy);

	cgroup->name = nih_strdup (cgroup, name);
	if (! cgroup->name)
		goto error;

	cgroup->expanded = NULL;

	nih_list_init (&cgroup->settings);

	return cgroup;

error:
	nih_free (cgroup);
	return NULL;
}

/**
 * cgroup_name_serialise:
 * @name: CGroupName to serialise.
 *
 * Convert @name into a JSON representation for serialisation.
 * Caller must free returned value using json_object_put().
 *
 * Returns: JSON-serialised CGroupName object, or NULL on error.
 **/
json_object *
cgroup_name_serialise (CGroupName *name)
{
	json_object  *json;
	json_object  *json_settings;

	nih_assert (name);

	json = json_object_new_object ();
	if (! json)
		return NULL;

	if (! state_set_json_string_var_from_obj (json, name, name))
		goto error;

	json_settings = cgroup_setting_serialise_all (&name->settings);
	if (! json_settings)
		goto error;

	json_object_object_add (json, "settings", json_settings);

	return json;

error:
	json_object_put (json);
	return NULL;
}


/**
 * cgroup_name_serialise_all:
 *
 * @names: list of CGroupName objects.
 *
 * Convert CGroupName objects to JSON representation.
 *
 * Returns: JSON object containing array of CGroupName objects in JSON
 * form, or NULL on error.
 **/
json_object *
cgroup_name_serialise_all (NihList *names)
{
	json_object  *json;
	json_object  *json_name;

	nih_assert (names);

	json = json_object_new_array ();
	if (! json)
		return NULL;

	NIH_LIST_FOREACH (names, iter) {
		CGroupName *cgname = (CGroupName *)iter;

		json_name = cgroup_name_serialise (cgname);
		if (! json_name)
			goto error;

		json_object_array_add (json, json_name);
	}

	return json;

error:
	json_object_put (json);
	return NULL;
}

/**
 * cgroup_name_deserialise:
 *
 * @parent: parent of new CGroup object,
 * @json: JSON-serialised CGroup object to deserialise.
 *
 * Convert @json into a CGroup object.
 *
 * Returns: CGroup object, or NULL on error.
 **/
CGroupName *
cgroup_name_deserialise (void *parent, json_object *json)
{
	nih_local char  *name = NULL;
	CGroupName      *cgname;

	nih_assert (json);

	if (! state_check_json_type (json, object))
		return NULL;

	if (! state_get_json_string_var (json, "name", NULL, name))
		return NULL;

	cgname = cgroup_name_new (parent, name);
	if (! cgname)
		return NULL;

	if (cgroup_setting_deserialise_all (cgname, &cgname->settings, json) < 0)
		goto error;

	return cgname;

error:
	nih_free (cgname);
	return NULL;
}

/**
 * cgroup_name_deserialise_all:
 *
 * @parent: parent of new CGroupSetting objects,
 * @list: list to store cgroup name details in,
 * @json: JSON-serialised CGroupName objects to deserialise.
 *
 * Convert @json back into CGroupName objects.
 *
 * Returns: 0 on success, -1 on error.
 **/
int
cgroup_name_deserialise_all (void         *parent,
			     NihList      *list,
			     json_object  *json)
{
	json_object  *json_names;

	nih_assert (json);

	json_names = json_object_object_get (json, "names");

	if (! json_names)
		goto error;

	for (int i = 0; i < json_object_array_length (json_names); i++) {
		json_object   *json_name;
		CGroupName    *cgname;

		json_name = json_object_array_get_idx (json_names, i);
		if (! json_name)
			goto error;

		if (! state_check_json_type (json_name, object))
			goto error;

		cgname = cgroup_name_deserialise (parent, json_name);
		if (! cgname)
			goto error;

		nih_list_add (list, &cgname->entry);
	}

	return 0;

error:
	return -1;
}

/**
 * cgroup_setting_new:
 *
 * @parent: parent of new CGroupSetting,
 * @key: cgroup setting name,
 * @value: cgroup setting value (optional if @key not specified).
 *
 * Allocates and returns a new CGroupSetting object.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.
 *
 * Returns: newly allocated CGroupSetting or NULL if insufficient memory.
 **/
CGroupSetting *
cgroup_setting_new (void *parent, const char *key, const char *value)
{
	CGroupSetting *setting;

	nih_assert (key);

	setting = nih_new (parent, CGroupSetting);
	if (! setting)
		return NULL;

	nih_list_init (&setting->entry);

	nih_alloc_set_destructor (setting, nih_list_destroy);

	setting->key = nih_strdup (setting, key);
	if (! setting->key)
		goto error;

	if (value) {
		setting->value = nih_strdup (setting, value);
		if (! setting->value)
			goto error;
	} else {
		setting->value = NULL;
	}

	return setting;

error:
	nih_free (setting);
	return NULL;
}

/**
 * cgroup_setting_serialise:
 * @setting: CGroupSetting to serialise.
 *
 * Convert @setting into a JSON representation for serialisation.
 * Caller must free returned value using json_object_put().
 *
 * Returns: JSON-serialised CGroupSetting object, or NULL on error.
 **/
json_object *
cgroup_setting_serialise (const CGroupSetting *setting)
{
	json_object  *json;

	nih_assert (setting);

	json = json_object_new_object ();
	if (! json)
		return NULL;

	if (! state_set_json_string_var_from_obj (json, setting, key))
		goto error;

	if (! state_set_json_string_var_from_obj (json, setting, value))
		goto error;

	return json;

error:
	json_object_put (json);
	return NULL;
}

/**
 * cgroup_setting_serialise_all:
 *
 * @names: list of CGroupSetting objects.
 *
 * Convert CGroupSetting objects to JSON representation.
 *
 * Returns: JSON object containing array of CGroupSetting objects in JSON
 * form, or NULL on error.
 **/
json_object *
cgroup_setting_serialise_all (NihList *settings)
{
	json_object  *json;
	json_object  *json_setting;

	nih_assert (settings);

	json = json_object_new_array ();
	if (! json)
		return NULL;

	NIH_LIST_FOREACH (settings, iter) {
		CGroupSetting *setting = (CGroupSetting *)iter;

		json_setting = cgroup_setting_serialise (setting);
		if (! json_setting)
			goto error;

		json_object_array_add (json, json_setting);
	}

	return json;

error:
	json_object_put (json);
	return NULL;
}

/**
 * cgroup_setting_deserialise:
 * @parent: parent of new CGroupSetting object,
 * @json: JSON-serialised CGroupSetting object to deserialise.
 *
 * Convert @json into a CGroupSetting object.
 *
 * Returns: CGroupSetting object, or NULL on error.
 **/
CGroupSetting *
cgroup_setting_deserialise (void *parent, json_object *json)
{
	nih_local char  *key = NULL;
	nih_local char  *value = NULL;
	CGroupSetting   *setting;

	nih_assert (json);

	if (! state_check_json_type (json, object))
		return NULL;

	if (! state_get_json_string_var (json, "key", NULL, key))
		return NULL;

	if (! state_get_json_string_var (json, "value", NULL, value))
		return NULL;

	setting = cgroup_setting_new (parent, key, value);
	if (! setting)
		return NULL;

	return setting;
}

/**
 * cgroup_setting_deserialise_all:
 *
 * @parent: parent of new CGroupSetting objects,
 * @list: list to store cgroup settings details in,
 * @json: JSON-serialised CGroupSetting objects to deserialise.
 *
 * Convert @json back into CGroupSetting objects.
 *
 * Returns: 0 on success, -1 on error.
 **/
int
cgroup_setting_deserialise_all (void         *parent,
				NihList      *list,
				json_object  *json)
{
	json_object  *json_settings;

	nih_assert (json);

	json_settings = json_object_object_get (json, "settings");

	if (! json_settings)
		goto error;

	for (int i = 0; i < json_object_array_length (json_settings); i++) {
		json_object    *json_setting;
		CGroupSetting  *setting;

		json_setting = json_object_array_get_idx (json_settings, i);
		if (! json_setting)
			goto error;

		if (! state_check_json_type (json_setting, object))
			goto error;

		setting = cgroup_setting_deserialise (parent, json_setting);
		if (! setting)
			goto error;

		nih_list_add (list, &setting->entry);
	}

	return 0;

error:
	return -1;
}

/**
 * cgroup_manager_connected:
 *
 * Determine if already connected to the CGroup Manager.
 *
 * Returns: TRUE if connected, else FALSE.
 **/
int
cgroup_manager_connected (void)
{
	return !! cgroup_manager;
}

/**
 * cgroup_manager_serialise:
 *
 * Convert CGroup manager address into a JSON representation for
 * serialisation. Caller must free returned value using
 * json_object_put().
 *
 * Returns: JSON-serialised CGroup Manager object, or NULL on error.
 **/
json_object *
cgroup_manager_serialise (void)
{
	json_object  *json;

	json = json_object_new_object ();
	if (! json)
		return NULL;

	if (! state_set_json_string_var (json, "cgroup_manager_address",
				cgroup_manager_address))
		goto error;

	return json;

error:
	json_object_put (json);
	return NULL;
}

/**
 * cgroup_manager_deserialise:
 * @json: JSON-serialised CGroup object to deserialise.
 *
 * Convert @json into a CGroup object.
 *
 * Returns: 0 on success, -1 on error.
 **/
int
cgroup_manager_deserialise (json_object *json)
{
	nih_assert (json);

	nih_assert (! cgroup_manager);

	if (! state_get_json_string_var (json,"cgroup_manager_address",
				NULL, cgroup_manager_address))
		return -1;

	return 0;
}

/**
 * cgroup_manager_connect:
 *
 * @address: Address of CGroup Manager.
 *
 * Connect to the CGroup Manager.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
cgroup_manager_connect (const char *address)
{
	DBusConnection  *connection;
	DBusError        dbus_error;
	//int              fd;

	nih_assert (address);

	if (cgroup_manager)
		return 0;

	cgroup_manager_address = NIH_MUST (nih_strdup (NULL, address));

	dbus_error_init (&dbus_error);

	/* FIXME:
	 *
	 * - Hard-code well-known address (DBUS_ADDRESS_CGMANAGER) or use initctl command?
	 */
	connection = nih_dbus_connect (cgroup_manager_address, cgroup_manager_disconnected);
	if (! connection)
		return -1;

	dbus_connection_set_exit_on_disconnect (connection, FALSE);
	dbus_error_free (&dbus_error);

	/* FIXME: hard-coded values! */
	cgroup_manager = nih_dbus_proxy_new (NULL, connection,
					     NULL, /* peer-to-peer connection */
					     DBUS_PATH_CGMANAGER, NULL, NULL);
	if (! cgroup_manager) {
		dbus_connection_unref (connection);
		return -1;
	}

	cgroup_manager->auto_start = FALSE;

	/* Drop initial reference now the proxy holds one */
	dbus_connection_unref (connection);

	nih_debug ("Connected to cgroup manager");

	return 0;
}

/**
 * cgroup_manager_disconnected:
 *
 * This function is called when the connection to the CGroup manager
 * is dropped and our reference is about to be lost.
 **/
static void
cgroup_manager_disconnected (DBusConnection *connection)
{
	nih_assert (connection);
	nih_assert (cgroup_manager_address);

	nih_warn (_("Disconnected from CGroup manager"));

	cgroup_manager = NULL;
	nih_free (cgroup_manager_address);
	cgroup_manager_address = NULL;
}

/* FIXME: */
#if 0
static void
cgroup_manager_lost_handler (void *data, NihDBusProxy *proxy)
{
	nih_assert (proxy);

	nih_warn (_("Lost track of CGroup manager"));
}

static void
cgroup_manager_error_handler (void *data, NihDBusMessage *message)       
{
        NihError *err;

        err = nih_error_get ();        
        nih_warn ("%s", err->message); 
        nih_free (err);
}
#endif

/**
 * cgroup_create:
 * @controller: cgroup controller,
 * @path: relative cgroup path to create.
 *
 * Request the CGroup Manager create a cgroup.
 *
 * The CGroup Manager creates cgroups as:
 *
 *   "/sys/fs/cgroup/$controller/$name".
 *
 * Upstart will take the value specified by the cgroup stanza,
 * prepend
 * A standard prefix is applied to the specified @path (which must be
 * relative) such that $cgroup_path_suffix path will in fact be:
 *
 *   "upstart/$UPSTART_JOB-$UPSTART_INSTANCE/$requested_path".
 *
 * Note: No validation is done on @path: that is handled by the CGroup
 * manager.
 *
 * Returns: TRUE on success, FALSE on raised error.
 **/
int
cgroup_create (const char *controller, const char *path)
{
	int        ret = 0;
	int        existed = -1;
	pid_t      pid;

	nih_assert (controller);
	nih_assert (path);
	nih_assert (cgroup_manager);

	pid = getpid ();

	nih_message ("XXX:%s:%d: controller='%s', path='%s'",
			__func__, __LINE__,
			controller, path);

	/* FIXME: reconnect */
#if 1
	{
		nih_local char *saved = NULL;
		int ret;

		saved = NIH_MUST (nih_strdup (NULL, cgroup_manager_address));

	nih_message ("XXX:%s:%d:", __func__, __LINE__);fflush(NULL);
		nih_free (cgroup_manager);
	nih_message ("XXX:%s:%d:", __func__, __LINE__);fflush(NULL);
		cgroup_manager = NULL;
	nih_message ("XXX:%s:%d:", __func__, __LINE__);fflush(NULL);
		ret = cgroup_manager_connect (saved);
	nih_message ("XXX:%s:%d:cgroup_manager_connect returned %d", __func__, __LINE__, ret);fflush(NULL);
	}
#endif

	nih_message ("XXX:%s:%d:", __func__, __LINE__);fflush(NULL);

	/* Escape our existing cgroup for this controller by moving to
	 * the root cgroup to avoid creating groups below the current
	 * cgroup.
	 */
	ret = cgmanager_move_pid_abs_sync (NULL,
			cgroup_manager,
			controller,
			UPSTART_CGROUP_ROOT,
			pid);

	if (ret < 0)
		return FALSE;

	nih_debug ("Moved pid %d to root of '%s' controller cgroup",
			pid, controller);


	/* Ask cgmanager to create the cgroup */
	ret = cgmanager_create_sync (NULL,
			cgroup_manager,
			controller,
			path,
			&existed);

	if (ret < 0)
		return FALSE;

	nih_debug ("Created '%s' controller cgroup '%s'",
			controller, path);

	/* Get the CGroup manager to delete the cgroup once no more job
	 * processes remain in it. Never mind if auto-deletion occurs between
	 * a jobs processes since the group will be recreated anyway by
	 * cgroup_create().
	 *
	 * This may seem incorrect since if we create the group,
	 * then mark it to be auto-removed when empty, surely
	 * it will be immediately deleted? However, the way this works
	 * is that the group will be deleted once it has _become_ empty
	 * (having at some time *not* been empty).
	 *
	 * The logic of using auto-delete is slightly inefficient
	 * in terms of cgmanager usage, but is hugely beneficial to
	 * Upstart since it avoids having to store details of which
	 * groups were created by jobs and also avoids the complexity of
	 * the child (which is responsible for creating the cgroups)
	 * pass back these details asynchronously to the parent to avoid
	 * it blocking.
	 */
	nih_message ("XXX:%s:%d:", __func__, __LINE__);fflush(NULL);
	ret = cgmanager_remove_on_empty_sync (NULL,
			cgroup_manager,
			controller,
			path);

	nih_message ("XXX:%s:%d:ret=%d", __func__, __LINE__, ret);fflush(NULL);
	if (ret < 0)
		return FALSE;

	nih_debug ("Set remove on empty for '%s' controller cgroup '%s'",
			controller, path);

	return TRUE;
}

/**
 * cgroup_enter:
 *
 * @controller: cgroup controller,
 * @path: cgroup path to enter,
 * @pid: pid to move.
 *
 * Put the specified pid into the specified controller cgroup.
 *
 * Returns: TRUE on success, FALSE on raised error.
 **/
int
cgroup_enter (const char *controller, const char *path, pid_t pid)
{
	int     ret;

	nih_assert (controller);
	nih_assert (path);
	nih_assert (pid > 0);

	nih_assert (cgroup_manager);

	/* Move the pid into the appropriate cgroup */
	ret = cgmanager_move_pid_sync (NULL,
			cgroup_manager,
			controller,
			path,
			pid);

	if (ret < 0)
		return FALSE;

	nih_debug ("Moved pid %d to '%s' controller cgroup '%s'",
			pid, controller, path);

	return TRUE;
}

/**
 * cgroup_name_remap:
 *
 * @str: string to modify.
 *
 * Replace all occurences of slash in the specified string with
 * underscore. Used to avoid to avoid erroneous sub-cgroup creation.
 **/
static void
cgroup_name_remap (char *str)
{
	nih_assert (str);

	for (char *p = str; p && *p; p++)
		if (*p == '/') *p = '_';
}

/**
 * cgroup_add:
 *
 * FIXME: finish documenting params!!
 *
 * @parent: parent of new Cgroup,
 * @list: list to store cgroup details in,
 * @controller: name of cgroup controller,
 * @name: name of cgroup to create,
 * @key: cgroup setting name,
 * @value: value of @key.
 *
 * Add specified cgroup details to pre-existing @list.
 *
 * Note that all variables in @name must already have been expanded.
 *
 * Returns: TRUE on success, FALSE on error.
 **/
int
cgroup_add (void        *parent,
	    NihList     *cgroups,
	    const char  *controller,
	    const char  *name,
	    const char  *key,
	    const char  *value)
{
	CGroup         *cgroup = NULL;
	CGroupName     *cgname = NULL;
	CGroupSetting  *setting = NULL;
	int             found_cgroup = FALSE;
	int             found_cgroup_name = FALSE;
	int             found_setting = FALSE;

	nih_assert (cgroups);
	nih_assert (controller);

	/* If no name is specified, use the default Upstart-created
	 * path.
	 */
	if (! name)
		name = UPSTART_CGROUP_SHELL_ENVVAR;

	if (value)
		nih_assert (key);

	nih_message ("XXX:%s:%d:", __func__, __LINE__);fflush(NULL);

	NIH_LIST_FOREACH_SAFE (cgroups, iter) {
		cgroup = (CGroup *)iter;

		if (! strcmp (cgroup->controller, controller)) {

			found_cgroup = TRUE;

			NIH_LIST_FOREACH_SAFE (&cgroup->names, iter2) {
				cgname = (CGroupName *)iter2;

				if (! strcmp (cgname->name, name)) {
					found_cgroup_name = TRUE;

					if (! key)
						continue;

					NIH_LIST_FOREACH_SAFE (&cgname->settings, iter3) {
						setting = (CGroupSetting *)iter3;

						if (! strcmp (setting->key, key)) {
							char *new_value = NULL;

							found_setting = TRUE;

							/* Don't bother comparing value - just replace */
							if (setting->value)
								nih_free (setting->value);

							if (value) {
								new_value = nih_strdup (NULL, value);
								if (! new_value)
									return FALSE;

								setting->value = new_value;
								nih_ref (new_value, setting);
							} else {
								setting->value = NULL;
							}

							return TRUE;
						}
					}

					if (! found_setting) {

#if 1
						nih_message ("XXX:%s:%d: calling cgroup_setting_new(parent=cgname(%p))",
								__func__, __LINE__,
								cgname);
#endif

						setting = cgroup_setting_new (cgname, key, value);
						if (! setting)
							return FALSE;
						nih_list_add (&cgname->settings, &setting->entry);
					}
				}
			}

			if (! found_cgroup_name) {
				cgname = cgroup_name_new (cgroup, name);
				if (! cgname)
					return FALSE;
				nih_list_add (&cgroup->names, &cgname->entry);

				if (key) {
#if 1
					nih_message ("XXX:%s:%d: calling cgroup_setting_new(parent=cgname(%p))",
							__func__, __LINE__,
							cgname);
#endif
					setting = cgroup_setting_new (cgname, key, value);
					if (! setting)
						return FALSE;

					nih_list_add (&cgname->settings, &setting->entry);
				}
				return TRUE;
			}
		}
	}

	if (! found_cgroup) {
		cgroup = cgroup_new (parent, controller);
		if (! cgroup)
			return FALSE;

		nih_list_add (cgroups, &cgroup->entry);

		if (! found_cgroup_name) {
			cgname = cgroup_name_new (cgroup, name);
			if (! cgname)
				return FALSE;
			nih_list_add (&cgroup->names, &cgname->entry);

			if (key) {
#if 1
				nih_message ("XXX:%s:%d: calling cgroup_setting_new(parent=cgname(%p))",
						__func__, __LINE__,
						cgname);
#endif
				setting = cgroup_setting_new (cgname, key, value);
				if (! setting)
					return FALSE;

				nih_list_add (&cgname->settings, &setting->entry);
			}
		}
	}

	return TRUE;
}

/**
 * cgroup_settings_apply:
 *
 * @controller: controller,
 * @path: expanded path name,
 * @settings: List of CGroupSettings.
 *
 * Note that although @path has had all variables expanded, it is still
 * effectively a relative path since the cgroup manager handles
 * expanding it further.
 *
 * Returns: TRUE on success, FALSE on raised error.
 **/
int
cgroup_settings_apply (const char *controller, const char *path, NihList *settings)
{
	int               ret;

	nih_assert (controller);
	nih_assert (path);
	nih_assert (settings);
	nih_assert (cgroup_manager);

	NIH_LIST_FOREACH (settings, iter) {
		nih_local char *setting_key = NULL;

		CGroupSetting *setting = (CGroupSetting *)iter;

		/* setting files in a cgroup directory take the form "controller.key" */
		setting_key = nih_sprintf (NULL, "%s.%s",
				controller, setting->key);
		if (! setting_key)
			nih_return_no_memory_error (FALSE);

		ret = cgmanager_set_value_sync (NULL,
				cgroup_manager,
				controller,
				path,
				setting_key,
				setting->value ? setting->value : "");

		if (ret < 0)
			return FALSE;

	}

	nih_debug ("Applied cgroup settings to '%s' controller cgroup '%s'",
			controller, path);

	return TRUE;
}

#if 0
/**
 * cgroup_expand_paths:
 *
 * @parent: parent for @paths,
 * @cgroups: list of CGroup objects,
 * @env: environment table.
 *
 * Use @env to expand all variables in the cgroup names specified
 * in @cgroups, create the resulting cgroup paths and add them to
 * the cgroup_paths hash.
 *
 * Returns: TRUE on success, or FALSE on raised error.
 **/
int
cgroup_expand_paths (void           *parent,
		     NihList        *cgroups,
		     char * const   *env)
{
	const char       *upstart_job = NULL;
	const char       *upstart_instance = NULL;
	nih_local char   *suffix = NULL;
	nih_local char  **cgroup_env = NULL;
	nih_local char   *envvar = NULL;
	int               instance = FALSE;

	/* Value of $UPSTART_CGROUP which takes the form:
	 *
	 *     upstart/${UPSTART_JOB}
	 *
	 * Or for instance jobs:
	 *
	 *     upstart/${UPSTART_JOB}-${UPSTART_INSTANCE}
	 *
	 * */
	nih_local char   *upstart_cgroup = NULL;

	nih_assert (cgroups);
	nih_assert (env);

	if (! cgroup_support_enabled ())
		return TRUE;

	/* FIXME */
#if 0
	if (! cgroup_manager_connected ())
		return TRUE;
#endif

	if (NIH_LIST_EMPTY (cgroups))
		return TRUE;

	cgroup_env = nih_str_array_new (NULL);
	if (! cgroup_env)
		goto error;

	/* Copy the existing environment table */
	if (! environ_append (&cgroup_env, NULL, NULL, TRUE, env))
		nih_return_no_memory_error (FALSE);

	upstart_job = environ_get (cgroup_env, "UPSTART_JOB");
	nih_assert (upstart_job);

	upstart_instance = environ_get (cgroup_env, "UPSTART_INSTANCE");
	nih_assert (upstart_instance);

	if (*upstart_instance)
		instance = TRUE;

	suffix = nih_sprintf (NULL, "%s%s%s",
			upstart_job,
			instance ? "-" : "",
			instance ? upstart_instance : "");

	if (! suffix)
		goto error;

	/* Remap the standard prefix to avoid creating sub-cgroups erroneously */
	cgroup_name_remap (suffix);

	upstart_cgroup = nih_sprintf (NULL, "upstart/%s", suffix);

	if (! upstart_cgroup)
		goto error;

	/* FIXME */
	nih_message ("XXX:%s:%d: UPSTART_INSTANCE='%s'", __func__, __LINE__, upstart_instance);

	envvar = NIH_MUST (nih_sprintf (NULL, "UPSTART_CGROUP=%s", upstart_cgroup));

	/* FIXME */
	nih_message ("XXX:%s:%d: upstart_cgroup='%s'", __func__, __LINE__, upstart_cgroup);
	nih_message ("XXX:%s:%d: envvar='%s'", __func__, __LINE__, envvar);

	if (! environ_add (&cgroup_env, NULL, NULL, TRUE, envvar))
		goto error;

	NIH_LIST_FOREACH (cgroups, iter) {
		CGroup *cgroup = (CGroup *)iter;

		NIH_LIST_FOREACH (&cgroup->names, iter2) {
			CGroupName   *cgname = (CGroupName *)iter2;
			char         *p;

			/* TRUE if the path *starts with* '$UPSTART_CGROUP' */
			int              has_var = FALSE;

			/* Note that we don't support "${UPSTART_CGROUP}" */
			p = strstr (cgname->name, "$UPSTART_CGROUP");

			/* cgroup specifies UPSTART_CGROUP initially */
			if (p && p == cgname->name)
				has_var = TRUE;

			cgname->expanded = NIH_SHOULD (environ_expand (cgname,
						cgname->name,
						cgroup_env));

			if (! cgname->expanded) {
				/* Failure to expand any other variables
				 * is however an error.
				 */
				goto error;
			}

			/* Remap slash to underscore to avoid unexpected
			 * sub-cgroup creation.
			 */
			cgroup_name_remap (has_var
					? cgname->expanded + strlen ("$UPSTART_CGROUP")
					: cgname->expanded);


			if (! strcmp (cgname->name, cgname->expanded)) {
				/* expanded value is the same as the
				 * original, so don't bother storing the
				 * former.
				 */
				nih_free (cgname->expanded);
				cgname->expanded = NULL;
			}

			/* FIXME */
#if 1
			nih_message ("XXX:%s:%d: cgname: name='%s', expanded='%s'", __func__, __LINE__,
					cgname->name, cgname->expanded ? cgname->expanded : "");
#endif

#if 0
			if (! cgroup_create (cgroup->controller, cgroup_path))
				goto error;
#endif

#if 0
			/* Record the "full" (strictly still a relative suffix
			 * from the CGroup Managers perspective) path in the
			 * global table.
			 */
			if (! cgroup_path_new (NULL, cgroup->controller, cgroup_path))
				goto error;
#endif

#if 0
			if (! nih_str_array_add (paths, NULL, NULL, cgroup_path))
				goto error;
#endif
		}
	}

	return TRUE;

error:
	return FALSE;
}

/* FIXME: implement, and document */
int
cgroup_apply_paths (void)
{
	return TRUE;
}
#endif

/**
 * cgroup_enter_groups:
 *
 * @cgroups: list of CGroup objects.
 *
 * Move the current pid into the cgroups specified by @cgroups.
 *
 * Returns: TRUE on success, FALSE on raised error.
 **/
int
cgroup_enter_groups (NihList  *cgroups)
{
	pid_t   pid;

	nih_assert (cgroups);
	
	pid = getpid ();

	nih_message ("XXX:%s:%d:", __func__, __LINE__);fflush(NULL);

	if (! cgroup_support_enabled ())
		return TRUE;

	nih_message ("XXX:%s:%d:", __func__, __LINE__);fflush(NULL);

#if 0
	if (! cgroup_manager_connected ())
		return TRUE;
#endif

	if (NIH_LIST_EMPTY (cgroups))
		return TRUE;

	nih_message ("XXX:%s:%d:", __func__, __LINE__);fflush(NULL);

	NIH_LIST_FOREACH (cgroups, iter) {
		CGroup *cgroup = (CGroup *)iter;

		NIH_LIST_FOREACH (&cgroup->names, iter2) {
			CGroupName      *cgname = (CGroupName *)iter2;

			nih_message ("XXX:%s:%d:cgname='%s', expanded=%s", __func__, __LINE__,
					cgname->name,
					cgname->expanded ? cgname->expanded : "n/a");fflush(NULL);
			if (! cgroup_enter (cgroup->controller,
						cgname->expanded
						? cgname->expanded
						: cgname->name,
						pid))
				return FALSE;
		}
	}

	return TRUE;
}

int
cgroup_chown (const char  *controller,
	      const char  *path,
	      uid_t        uid,
	      gid_t        gid)
{
	int ret = 0;

	nih_assert (controller);
	nih_assert (path);
	nih_assert (cgroup_manager);

	/* Ask cgmanager to chown the path */
	ret = cgmanager_chown_sync (NULL,
			cgroup_manager,
			controller,
			path,
			uid,
			gid);

	if (ret < 0)
		return FALSE;

	nih_debug ("Changed ownership of '%s' controller cgroup '%s'",
			controller, path);

	return TRUE;
}
