/* upstart
 *
 * Copyright Â© Â 2012 Canonical Ltd.
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

#ifndef INIT_STATE_H
#define INIT_STATE_H

#include <nih/macros.h>
#include <nih/alloc.h>

#include <stdio.h>

#include <json.h>

#if 0
/**
 * STATE_VERSION_MIN:
 *
 * Oldest serialistion data format version supported.
 **/
#define STATE_VERSION_MIN 1
#endif

/**
 * STATE_VERSION:
 *
 * Newest serialistion data format version supported.
 *
 * Increment when new versions are introduced but ensure output
 * compatible with all prior versions can be generated.
 **/
#define STATE_VERSION 1

/**
 * STATE_WAIT_SECS:
 *
 * Time to wait in seconds for the re-exec state file descriptor to be
 * ready for writing. If this timeout is reached, the new PID 1 instance
 * must either have got into trouble or not support stateful re-exec.
 **/
#define STATE_WAIT_SECS 3

/**
 * state_check_type:
 *
 * @object: JSON object,
 * @type: type of JSON primitive.
 *
 * Returns: TRUE if type of @object is @type, else FALSE.
 **/
#define state_check_type(object, type) \
	(json_object_get_type (object) == json_type_ ## type)

/**
 * state_get_json_var:
 *
 * @json: json_object pointer,
 * @name: string name to search for in @json,
 * @type: type of JSON primitive,
 * @json_var: name of json_object variable that will store value.
 *
 * Query @json, setting @json_var to be the JSON value of @name where
 * @json_var is of type @type.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_get_json_var(json, name, type, json_var) \
(! (!(json_var = json_object_object_get (json, name)) || \
    state_check_type (json_var, type)))

/**
 * state_set_json_var:
 *
 * @json: json_object pointer,
 * @object: pointer to internal object that is to be serialised,
 * @name: name of element within @object to be serialised,
 * @type: JSON type for field to be added.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_set_json_var(json, object, name, type) \
	({json_object *json_var = json_object_new_ ## type ((type)(object)->name); \
	 json_object_object_add (json, #name, json_var); json_var; })

/**
 * state_set_json_var:
 *
 * @json: json_object pointer,
 * @object: pointer to internal object that is to be serialised,
 * @name: name of element withing @object to be serialised,
 * @type: JSON type for field to be added.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_set_json_string_var(json, object, name) \
	({json_object *json_var = json_object_new_string ((char *)(object)->name ? object->name : ""); \
	 json_object_object_add (json, #name, json_var); json_var; })

/**
 * state_set_json_var_full:
 *
 * @json: json_object pointer,
 * @name: string name for a field to add to @json,
 * @value: value of @name,
 * @type: JSON type for field to be added,
 * @json_var: name of json_object variable that will store value @value.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_set_json_var_full(json, name, value, type, json_var) \
	({json_var = json_object_new_ ## type (value); \
	 json_object_object_add (json, name, json_var); json_var; })

/**
 * state_get_json_string_var:
 *
 * @parent: parent object for new string (parent of @var),
 * @json: json_object pointer,
 * @name: string name to search for in @json,
 * @json_var: name of json_object variable that will store value,
 * @var: string variable to set to value of @json_var.
 *
 * Specialisation of state_get_json_var() that works for the JSON string
 * type.
 *
 * Query @json, setting @json_var to be the JSON value of @name where
 * @json_var is of type @type and then setting @var to be the string
 * value of @json_var.
 *
 * Caller must not free @var on successfull completion of this macro,
 * and should copy memory @var is set to.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_get_json_string_var(json, name, json_var, var) \
	(state_get_json_var (json, name, string, json_var) && \
	 (var = json_object_get_string (json_var)))

/**
 * state_get_json_simple_var:
 *
 * @json: json_object pointer,
 * @name: string name to search for in @json,
 * @type: type of @json_var,
 * @json_var: name of json_object variable that will store value,
 * @var: variable of type @type to set to value of @json_var.
 *
 * Specialisation of state_get_json_var() that works for JSON
 * types int, boolean, and double.
 *
 * Note that a distinct macro is required for non-string types since
 * unlike strings they can legitimately have the value of zero.
 *
 * Query @json, setting @json_var to be the JSON value of @name where
 * @json_var is of type @type and then setting @var to the value
 * of @json_var.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_get_json_simple_var(json, name, type, json_var, var) \
	(state_get_json_var (json, name, type, json_var) && \
	 ((var = json_object_get_ ## type (json_var)) || 1==1))

NIH_BEGIN_EXTERN

void state_init          (int version);

int  state_get_version   (void)
	__attribute__ ((warn_unused_result));

void state_show_version  (void);

int  state_serialiseable (void)
	__attribute__ ((warn_unused_result));

int  state_read          (int fd)
	__attribute__ ((warn_unused_result));

int  state_write         (int fd)
	__attribute__ ((warn_unused_result));

int  state_read_objects   (int fd)
	__attribute__ ((warn_unused_result));

int  state_write_objects   (int fd)
	__attribute__ ((warn_unused_result));

#if 1
/* FIXME */
char * state_to_string       (void)
	__attribute__ ((warn_unused_result));
int    state_from_string (const char *state)
	__attribute__ ((warn_unused_result));
#endif

#if 0
int    state_get_session_idx (const char *session_name)
	__attribute__ ((warn_unused_result));
#endif

int    state_toggle_cloexec (int fd, int set)
	__attribute__ ((warn_unused_result));

json_object *
state_serialize_str_array (char ** const array)
	__attribute__ ((warn_unused_result));

json_object *
state_serialize_int_array (int *array, int count)
	__attribute__ ((warn_unused_result));

char **
state_deserialize_str_array (void *parent, json_object *json)
	__attribute__ ((warn_unused_result, malloc));

NIH_END_EXTERN

#endif /* INIT_STATE_H */
