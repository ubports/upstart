/* FIXME: TODO: Thoughts:
 *
 * - remove all PRODUCTION_BUILD macros and adopt production flow.
 *
 * - XXX: audit memory management for all *_deserialise() and
 *   XXX: *_deserialise_all() functions!!
 *
 * - create meta header and encode/decode code!!
 *
 * - encode/decode jobs
 * - encode/decode rlimits
 * - resolve event circular dependency
 *
 * - should we only serialise settings that have a value? (This might
 *   simplify logic for scenarios where a new Upstart that deserialises
 *   data from an old Upstart doesn't find some expected values).
 *
 * - clear up strategy around failure - if we fail to
 *   serialise/deserialise any data, do we revert to stateless re-exec?
 *   (tied to above).
 *
 * - we are not being consistent wrt calling NIH_MUST() - resolve!!
 *
 * - XXX: write tests!
 */

/*
 *--------------------------------------------------------------------
 * = Design =
 *
 * == Serialisation ==
 *
 * - Each object provides a <object_>_serialise() function which
 *   converts a single object into a JSON-representation.
 *
 * - Each object provides a <object_>_serialise_all() function which
 *   converts all objects from their appropriate list/hash/tree into a
 *   JSON array representation.
 *
 * == Deserialisation ==
 *
 * - Each object provides a <object_>_deserialise() function which
 *   converts a single JSON-representation of an object back into an
 *   internal object.
 *
 * - Each object provides a <object_>_deserialise_all() function which
 *   converts all objects either back into a list/hash/tree (by
 *   calling <object>_new() as appropriate), or back into some object
 *   type that can then be attached to another object (for example
 *   process_deserialise_all() converts a JSON array of processes back
 *   into an array of Process objects which are then hooked onto a
 *   JobClass object).
 *
 * Note that objects returned by <object_>_deserialise() are generally
 * _partial_ objects: they are not true objects since they have not
 * been constructed. They are like templates for the real objects with
 * those elements filled in that the JSON encodes.
 *
 * Each partial object needs to be converted into a real object by:
 *
 * - creating an instance of that object (using <object_>_new()).
 * - copying the element data from the partial object back into the real
 *   object.
 *
 * These steps happens in <object_>_deserialise_all(). It is rather
 * tedious but does ensure that the resultant object is "sane". It
 * is also essential since the JSON representation of most objects does
 * _NOT_ encode all information about an object (for example the JSON
 * encoding for an Event does not encode 'blockers' and 'blocking').
 *--------------------------------------------------------------------
 */

/* upstart
 *
 * Copyright © 2012 Canonical Ltd.
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

#include <sys/time.h>
#include <sys/resource.h>

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

/* FIXME */
#if 1
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
#endif

/**
 * state_check_type:
 *
 * @object: JSON object,
 * @type: type of JSON primitive (without prefix).
 *
 * Returns: TRUE if type of @object is @type, else FALSE.
 **/
#define state_check_type(object, type) \
	(json_object_get_type (object) == json_type_ ## type)

/*
 * state_get_json_var_full:
 *
 * @json: json_object pointer,
 * @name: string name to search for in @json,
 * @type: type of JSON primitive (without prefix),
 * @json_var: name of json_object variable that will store value.
 *
 * Query @json, setting @json_var to be the JSON value of @name where
 * @json_var is of type @type.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_get_json_var_full(json, name, type, json_var) \
	((json_var = json_object_object_get (json, name)) && \
	  state_check_type (json_var, type))

/**
 * state_get_json_num_var:
 *
 * @json: json_object pointer,
 * @name: string name to search for in @json,
 * @type: type of @json_var (without prefix),
 * @var: variable of type @type to set to value encoded in @json.
 *
 * Specialisation of state_get_json_var_full() that works for JSON
 * types int, boolean, and double.
 *
 * Note that a distinct macro is required for non-string types since
 * unlike strings they can legitimately have the value of zero.
 *
 * Query @json, setting @var to value of @name in @json.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_get_json_num_var(json, name, type, var) \
	({json_object *json_var = NULL; \
	 int ret = state_get_json_var_full (json, name, type, json_var); \
	 if (json_var) var = json_object_get_ ## type (json_var); json_var && ret;})

/**
 * state_get_json_num_var_to_obj:
 *
 * @json: json_object pointer,
 * @object: pointer to internal object that is to be deserialised,
 * @name: name of numeric element within @object to be deserialised,
 *
 * Extract stringified @name from @json and set numeric element named
 * @name in @object to its value.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_get_json_num_var_to_obj(json, object, name, type) \
	({json_object *json_var = NULL; \
	 int ret = state_get_json_var_full (json, #name, type, json_var); \
	 if (json_var) object->name = json_object_get_ ## type (json_var); \
	 json_var && ret;})

/**
 * state_get_json_string_var:
 *
 * @json: json_object pointer,
 * @name: string name to search for in @json,
 * @var: string variable to set to value of @json_var.
 *
 * Specialisation of state_get_json_var_full() that works for the JSON string
 * type.
 *
 * Query @json, setting @var to be string value of @name.
 *
 * Caller must not free @var on successful completion of this macro,
 * and should copy memory @var is set to.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_get_json_string_var(json, name, var) \
	({json_object *json_var; \
	state_get_json_var_full (json, name, string, json_var) && \
	 (var = json_object_get_string (json_var));})


/**
 * state_get_json_string_var_to_obj;
 *
 * @json: json_object pointer,
 * @object: pointer to internal object that is to be deserialised,
 * @name: name of element within @object to be deserialised,
 *
 * Extract stringified @name from @json and set string element named
 * @name in @object to a newly allocated string copy.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_get_json_string_var_to_obj(json, object, name) \
	({json_object *json_var; \
	 const char *value = NULL; \
	 state_get_json_var_full (json, #name, string, json_var) && \
	 (value = json_object_get_string (json_var)) \
	 && (object->name = nih_strdup (object, value));})


/**
 * state_set_json_var_full:
 *
 * @json: json_object pointer,
 * @name: string name to add as key in @json,
 * @value: value corresponding to @name,
 * @type: JSON type (without prefix) representing type of @value,
 *
 * Add @value to @json with name @name and type @type.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_set_json_var_full(json, name, value, type) \
	({json_object *json_var = json_object_new_ ## type (value); \
	 if (json_var) json_object_object_add (json, name, json_var); json_var; })

/**
 * state_set_json_num_var_from_obj:
 *
 * @json: json_object pointer,
 * @object: pointer to internal object that is to be serialised,
 * @name: name of element within @object to be serialised,
 * @type: JSON type (without prefix) for field to be added.
 *
 * Add value of numeric entity @name in object @object to
 * @json with stringified @name.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_set_json_num_var_from_obj(json, object, name, type) \
	({json_object *json_var = json_object_new_ ## type ((type)(object)->name); \
	 if (json_var) json_object_object_add (json, #name, json_var); json_var; })

/**
 * state_set_json_string_var_from_obj:
 *
 * @json: json_object pointer,
 * @object: pointer to internal object that is to be serialised,
 * @name: name of element withing @object to be serialised,
 * @type: JSON type (without prefix) for field to be added.
 *
 * Add stringified @name to @json taking the value from the
 * @object element of name @name.
 *
 * String specialisation that can also handle NULL strings (by encoding
 * them with a nul string value).
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_set_json_string_var_from_obj(json, object, name) \
	({json_object *json_var = \
	 json_object_new_string (object->name ? object->name : ""); \
	 if (json_var) json_object_object_add (json, #name, json_var); \
	 json_var; })

/* FIXME: document */
#define state_partial_copy(parent, source, name) \
	(parent->name = source->name)

/**
 * state_copy_str_array_to_obj:
 *
 * @to: object to copy @array to,
 * @from: object from which to copy from,
 * @array: name of string array element in @from to copy.
 *
 * Copy string array @array from @from to @to.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_copy_str_array_to_obj(to, from, element) \
	(to->element = nih_str_array_copy (to, NULL, from->element))
/**
 * state_partial_copy_string:
 *
 * @parent: parent object for new string (parent of @var),
 * @source: object that is a partial object of the same type as @parent,
 * @name: string element in @source that is to be copied to @parent.
 *
 * Copy string @name from within @source to @parent.
 *
 * After the call, @parents version of @source contains a copy of
 * that from @source (which may be NULL if the version in @source
 * is either NULL or the null string).
 *
 * Returns: nothing.
 **/
#define state_partial_copy_string(parent, source, name) \
	({typeof (source->name) _name = source->name; \
	 	parent->name = _name && *(_name) \
	 	? NIH_MUST (nih_strdup (parent, _name)) \
	 	: NULL;})

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
	__attribute__ ((malloc, warn_unused_result));

json_object *
state_rlimit_serialise (const struct rlimit *rlimit)
	__attribute__ ((malloc, warn_unused_result));

json_object *
state_rlimit_serialise_all (struct rlimit * const *rlimits)
	__attribute__ ((malloc, warn_unused_result));

struct rlimit *
state_rlimit_deserialise (json_object *json)
	__attribute__ ((malloc, warn_unused_result));

int 
state_rlimit_deserialise_all (json_object *json, const void *parent,
			      struct rlimit *(*rlimits)[])
	__attribute__ ((warn_unused_result));

NIH_END_EXTERN

#endif /* INIT_STATE_H */
