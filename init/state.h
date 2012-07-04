/* FIXME: TODO: Thoughts:
 *
 * - remove all PRODUCTION_BUILD macros and adopt production flow.
 *
 * - check "NULL session" deserialisation handling.
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
 *
 * == Macros ==
 *
 * Some of the macros defind here may appear needlessly trivial.
 * However, their value lies in their ability to avoid having to
 * duplicate element names when copying data from partial objects and
 * JSON (where it would be easy to forget to update some part of an
 * expresion and end up corrupting/duplicating data elements).
 * Safety first! :)
 *
 * == Data Types ==
 *
 * Care has to be taken to ensure no data loss in handling typedef'ed
 * values as they are nominally opaque and may have unusually large
 * representations on some systems.
 *
 * Although RFC 4627, the JSON "memo" (not a spec!) alludes to
 * JSON being a subset of ECMA-262, it does not specify the size of
 * integer types! However, ECMA-262 specifies them as 64-bit.
 *
 * JSON-C up to version 0.9 encoded all integers as a native 'int'
 * type (which may have been 32-bit or 64-bit) but with version 0.10 it
 * now provides a 32-bit and 64-bit integer interface (although all
 * integers are actually stored as 64-bit internally).
 *
 * As such, we have to check the size of all typedef'd types and
 * encode/decode them using either the 32-bit or 64-bit JSON-C integer
 * interfaces (note that technically it is only the decoding that can
 * cause data loss due to JSON-C storing all integer values as 64-bit
 * internally).
 *
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
 * state_check_json_type:
 *
 * @object: JSON object,
 * @type: type of JSON primitive (without prefix).
 *
 * Compare type of @object with @type.
 * 
 * Note: This could be achieved entirely as a macro but for
 * the fact that JSON-C does not define a 64-bit integer type
 * (and yet allows integers to be handled either as 32-bit
 * or 64-bit).
 *
 * Returns: TRUE if type of @object is @type, else FALSE.
 **/

#define state_check_json_type(object, type) \
    (json_object_get_type (object) == state_get_json_type (#type))

/*
 * state_get_json_var_full:
 *
 * @json: json_object pointer,
 * @name: string name to search for in @json,
 * @json_type: type of JSON primitive (without prefix),
 * @json_var: name of json_object variable that will store value.
 *
 * Query @json, setting @json_var to be the JSON value of @name where
 * @json_var is of type @type.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_get_json_var_full(json, name, type, json_var) \
	((json_var = json_object_object_get (json, name)) && \
	  state_check_json_type (json_var, type))

/**
 * state_get_json_num_var:
 *
 * @json: json_object pointer,
 * @name: string name to search for in @json,
 * @type_json: type of @var (without prefix),
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
#define state_get_json_num_var(json, name, type_json, var) \
	({json_object *_json_var = NULL; \
	 int ret = state_get_json_var_full (json, name, type_json, _json_var); \
	 if (_json_var) \
	 	var = json_object_get_ ## type_json (_json_var); \
	 _json_var && ret;})

/**
 * state_get_json_num_var_to_obj:
 *
 * @json: json_object pointer,
 * @object: pointer to internal object that is to be deserialised,
 * @name: name of numeric element within @object to be deserialised,
 * @type_json: type of @name in @json (without prefix).
 *
 * Extract stringified @name from @json and set numeric element named
 * @name in @object to its value.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_get_json_num_var_to_obj(json, object, name, type_json) \
	({json_object *_json_var = NULL; \
	 int ret = state_get_json_var_full (json, #name, type_json, _json_var); \
	 if (_json_var) \
	 	object->name = json_object_get_ ## type_json (_json_var); \
	 _json_var && ret;})


/**
 * state_new_json_int:
 *
 * @value: value to encode
 *
 * Encode @value as a JSON integer.
 *
 * Returns: json_object that encodes @value.
 */
#define state_new_json_int(size, value) \
	 ((size_t)(size) > sizeof (int) \
	 ? json_object_new_int64 (value) \
	 : json_object_new_int (value))

/**
 * state_serialise_int_array:
 *
 * @type: native type of elements within @array,
 * @array: array to serialise
 * @len: length of @array.
 *
 * Returns: JSON-serialised @array, or NULL on error.
 **/
#define state_serialise_int_array(type, array, len) \
	(sizeof (type) == (size_t)4 \
	 ? state_serialise_int32_array ((int32_t *)array, len) \
	 : state_serialise_int64_array ((int64_t *)array, len))

/**
 * state_deserialise_int_array:
 *
 * @parent: parent object for new array,
 * @json: JSON array object representing an integer array,
 * @type: native type of elements within @array,
 * @array: array of integers,
 * @len: length of @array.
 *
 * Convert JSON array object @json into an array of integers whose size
 * is the same as the size of @type.
 *
 * Returns: 0 on success, -1 on ERROR.
 **/
#define state_deserialise_int_array(parent, json, type, array, len) \
	(sizeof (type) == (size_t)4 \
	 ? state_deserialise_int32_array (parent, json, (int32_t **)array, len) \
	 : state_deserialise_int64_array (parent, json, (int64_t **)array, len))

/**
 * state_get_json_int32_var_to_obj:
 *
 * @json: json_object pointer,
 * @object: pointer to internal object that is to be deserialised,
 * @name: name of 32-bit numeric element within @object to be deserialised.
 *
 * Extract stringified @name from @json and set 32-bit integer element
 * named @name in @object to its value.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_get_json_int32_var_to_obj(json, object, name) \
	  (state_get_json_num_var_to_obj (json, object, name, int))

/**
 * state_get_json_int64_var_to_obj:
 *
 * @json: json_object pointer,
 * @object: pointer to internal object that is to be deserialised,
 * @name: name of 64-bit numeric element within @object to be deserialised.
 *
 * Extract stringified @name from @json and set 64-bit integer element
 * named @name in @object to its value.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_get_json_int64_var_to_obj(json, object, name) \
	  (state_get_json_num_var_to_obj (json, object, name, int64))

/**
 * state_get_json_int_var_to_obj:
 *
 * @json: json_object pointer,
 * @object: pointer to internal object that is to be deserialised,
 * @name: name of numeric element within @object to be deserialised.
 *
 * Extract stringified @name from @json and set integer element named
 * @name in @object to its value.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_get_json_int_var_to_obj(json, object, name) \
	(sizeof (object->name) == (size_t)4 \
		? state_get_json_int32_var_to_obj (json, object, name) \
		: state_get_json_int64_var_to_obj (json, object, name))
/**
 * state_get_json_string_var:
 *
 * @json: json_object pointer,
 * @name: string name to search for in @json,
 * @var: string variable to set to value of @name in @json.
 *
 * Specialisation of state_get_json_var_full() that works for
 * the JSON string type.
 *
 * Query @json, setting @var to be string value of @name.
 *
 * Caller must not free @var on successful completion of this macro,
 * and should copy memory @var is set to.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_get_json_string_var(json, name, var) \
	({json_object *_json_var; \
	state_get_json_var_full (json, name, string, _json_var) && \
	 (var = json_object_get_string (_json_var));})


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
	({json_object *_json_var; \
	 const char *value = NULL; \
	 state_get_json_var_full (json, #name, string, _json_var) && \
	 (value = json_object_get_string (_json_var)) \
	 && (object->name = nih_strdup (object, value));})

/**
 * state_set_json_var_full:
 *
 * @json: json_object pointer,
 * @name: string name to add as key in @json,
 * @value: value corresponding to @name,
 * @type_json: JSON type (without prefix) representing type of @value.
 *
 * Add @value to @json with name @name and type @type.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_set_json_var_full(json, name, value, type_json) \
	({json_object *_json_var; \
	 _json_var = json_object_new_ ## type_json (value); \
	 if (_json_var) json_object_object_add (json, name, _json_var); \
	 _json_var;})


/**
 * state_set_json_num_var_from_obj:
 *
 * @json: json_object pointer,
 * @object: pointer to internal object that is to be serialised,
 * @name: name of element within @object to be serialised,
 * @type_json: JSON type (without prefix) for field to be added,
 * @type: native type of object represented by @name.
 *
 * Add value of numeric entity @name in object @object to
 * @json with stringified @name.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_set_json_num_var_from_obj(json, object, name, type_json, type) \
	({json_object *_json_var = \
	 json_object_new_ ## type_json ((type)(object ? (object)->name : 0)); \
	 if (_json_var) json_object_object_add (json, #name, _json_var); \
	 _json_var;})

/**
 * state_set_json_int32_var_from_obj:
 *
 * @json: json_object pointer,
 * @object: pointer to internal object that is to be serialised,
 * @name: name of 32-bit integer element within @object to be serialised,
 *
 * Add value of 32-bit integer entity @name in object @object to
 * @json with stringified @name.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_set_json_int32_var_from_obj(json, object, name) \
	state_set_json_num_var_from_obj (json, object, name, int, int32_t)

/**
 * state_set_json_int64_var_from_obj:
 *
 * @json: json_object pointer,
 * @object: pointer to internal object that is to be serialised,
 * @name: name of 64-bit integer element within @object to be serialised,
 *
 * Add value of 64-bit integer entity @name in object @object to
 * @json with stringified @name.
 *
 * Note: The @type pased to state_set_json_num_var_from_obj() looks
 * wrong, but remember that there is only a single 'json_type_int'
 * value (which encompasses both 32-bit and 64-bit values).
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_set_json_int64_var_from_obj(json, object, name) \
	state_set_json_num_var_from_obj (json, object, name, int, int64_t)

/**
 * state_set_json_int_var_from_obj:
 *
 * @json: json_object pointer,
 * @object: pointer to internal object that is to be serialised,
 * @name: name of integer element within @object to be serialised,
 *
 * Add value of integer entity @name in object @object to
 * @json with stringified @name.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_set_json_int_var_from_obj(json, object, name) \
	(sizeof (object->name) == (size_t)4 \
	? state_set_json_int32_var_from_obj (json, object, name) \
	: state_set_json_int64_var_from_obj (json, object, name))

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
	({json_object *_json_var = \
	 json_object_new_string (object->name ? object->name : ""); \
	 if (_json_var) json_object_object_add (json, #name, _json_var); \
	 _json_var;})

/**
 * state_set_json_str_array_from_obj:
 *
 * @json: json_object pointer,
 * @object: pointer to internal object that is to be serialised,
 * @name: name of element withing @object to be serialised.
 *
 * Copy string array @name from @object to @json.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_set_json_str_array_from_obj(json, object, name) \
	({json_object *_json_var; \
	 _json_var = object->name \
	 ? state_serialise_str_array (object->name) \
	 : json_object_new_array (); \
	 if (_json_var) json_object_object_add (json, #name, _json_var); \
	 _json_var;})

/**
 * state_partial_copy_int:
 *
 * @to: object to assign @name to,
 * @from: object from which to take value from,
 * @name: name of integer element in @from to assign to @to.
 *
 * Copy integer value of @name element from @from to @to.
 *
 * Trivial, but removes any risk of incorrect assignment due to
 * "name mixups".
 *
 * Returns: value of @name.
 *
 **/
#define state_partial_copy_int(parent, source, name) \
	(parent->name = source->name)

/**
 * state_partial_copy_string:
 *
 * @parent: parent object for new string (parent of @var),
 * @source: object that is a partial object of the same type as @parent,
 * @name: string element in @source that is to be copied to @parent.
 *
 * Copy string @name from within @source to @parent.
 *
 * XXX: If @source's version of @name is either NULL or the nul string,
 * @parents @name will be set to NULL.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_partial_copy_string(parent, source, name) \
	({typeof (source->name) _name = source->name; \
	 	parent->name = _name && *(_name) \
	 	? NIH_MUST (nih_strdup (parent, _name)) \
	 	: NULL; \
	 	_name && *(_name) ? parent->name ? 1 : 0: 1;})

/**
 * state_get_json_str_array_to_obj:
 *
 * @json: json_object pointer,
 * @object: pointer to internal object that is to be deserialised,
 * @name: name of element within @object to be deserialised.
 *
 * Deserialise stringified @name from @json into an array of strings and
 * assign to @name within @object.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_get_json_str_array_to_obj(json, object, name) \
	({json_object *_json_var = NULL; \
	 (state_get_json_var_full (json, #name, array, _json_var)) && \
	(object->name = state_deserialise_str_array (object, _json_var, FALSE));})

/**
 * state_get_json_env_array_to_obj:
 *
 * @json: json_object pointer,
 * @object: pointer to internal object that is to be deserialised,
 * @name: name of element within @object to be deserialised.
 *
 * Deserialise stringified @name from @json into an array of environment
 * variable strings and assign to @name within @object.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_get_json_env_array_to_obj(json, object, name) \
	({json_object *_json_var = NULL; \
	 (state_get_json_var_full (json, #name, array, _json_var)) && \
	(object->name = state_deserialise_str_array (object, _json_var, TRUE));})

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
 * state_copy_event_oper_to_obj:
 *
 * @to: object to copy @event_oper to,
 * @from: object from which to copy from,
 * @event_oper: name of EventOperator element in @from to copy.
 *
 * Copy EventOperator @event_oper from @from to @to.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_copy_event_oper_to_obj(to, from, event_oper) \
	({if (from->event_oper) \
	 	to->event_oper = event_operator_copy (to, from->event_oper); \
	 else \
	 	to->event_oper = NULL; \
	 from->event_oper ? to->event_oper ? 1 : 0 : 1;})

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
/* FIXME: TESTING and DEBUG only */
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
state_serialise_str_array (char ** const array)
	__attribute__ ((warn_unused_result));

json_object *
state_serialise_int32_array (int32_t *array, int count)
	__attribute__ ((malloc, warn_unused_result));

json_object *
state_serialise_int64_array (int64_t *array, int count)
	__attribute__ ((malloc, warn_unused_result));

char **
state_deserialise_str_array (void *parent, json_object *json, int env)
	__attribute__ ((malloc, warn_unused_result));

int
state_deserialise_int32_array (void *parent, json_object *json,
		int32_t **array, size_t *len)
	__attribute__ ((warn_unused_result));

int
state_deserialise_int64_array (void *parent, json_object *json,
		int64_t **array, size_t *len)
	__attribute__ ((warn_unused_result));

json_object *
state_rlimit_serialise_all (struct rlimit * const *rlimits)
	__attribute__ ((malloc, warn_unused_result));

int 
state_rlimit_deserialise_all (json_object *json, const void *parent,
			      struct rlimit *(*rlimits)[])
	__attribute__ ((warn_unused_result));

char *state_collapse_env (char **env)
	__attribute__ ((malloc, warn_unused_result));

enum json_type
state_get_json_type (const char *short_type)
	__attribute__ ((warn_unused_result));

NIH_END_EXTERN

#endif /* INIT_STATE_H */
