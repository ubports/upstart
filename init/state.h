/* FIXME: TODO: Thoughts:
 *
 *--------------------------------------------------------------------
 * XXX:XXX: * XXX:XXX: * XXX:XXX: * XXX:XXX: * XXX:XXX: * XXX:XXX:
 *
 * - XXX: Deferred work:
 *   - handling of Upstart-in-initramfs - for this to work, it would be
 *     necessary to serialise ConfSources along with the following:
 *
 *     (1) inode number of source->path
 *     (2) inode number of '/'
 *     (3) the FS device details (stat /|grep ^Device)
 *
 *     (1)-(3) would allow the deserialisation logic to discriminate
 *     between two job configuration files with the same name, but
 *     different contents (one from the initramfs, one from the root
 *     filesystem).
 *
 *     XXX: Further, to support Upstart-in-intramfs would require
 *     changes to the 'restart' logic since currently, no initial event
 *     will be emitted when Upstart is re-exec'd.
 *
 *     Since this isn't implemented, there is a restriction currently
 *     (that can only be handled procedurally!) of not supporting
 *     Upstart-in-initramfs or more precisely not supporting a job with
 *     the same name in two different filesystem contexts.
 *
 *     Note too that (2)+(3) are the only reliable method for Upstart to
 *     detect that is *has* changed filesystem context.
 *
 *   - ptrace handling: unlikely to hit that scenario.
 *
 *   - Log objects (so after stateful re-exec, no further log output
 *     will be produced).
 *
 *   - parent/child timeout handling: we won't support down-grading initially.
 *
 *   - dbus-connections: only be re-exec'ing post-boot so bridges won't
 *     suffer too much.
 *
 * XXX:XXX: * XXX:XXX: * XXX:XXX: * XXX:XXX: * XXX:XXX: * XXX:XXX:
 *--------------------------------------------------------------------
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
 * == Error Handling ==
 *
 * If stateful re-exec fails, Upstart must perform a stateless reexec:
 * this obviously is not ideal since it results in all state being
 * discarded, but atleast init continues to run.
 *
 * === Serialisation ===
 *
 * Upstart will serialise all internal objects in as rich a
 * serialisation as possible.
 *
 * The error handling strategy for serialisation is easy: if any of the
 * serialisation steps fail, error immediately.
 *
 * === Deserialisation ===
 *
 * This is the more difficult aspect: deserialisation must contend
 * with possible partial errors and handle them intelligently:
 *
 * - missing keys and/or values in the JSON:
 *
 *   - indicates either a bug or a change in the JSON serialisation dat
 *     format of the higher-numbered versioned of Upstart.
 *
 *   - deserialise as 0 or NULL as appropriate if possible.
 *
 * - unexpected JSON entries:
 *
 *   - ignored as this indicates a downgrade (and it's clearly
 *     unreasonable to expect a lower-numbered version of Upstart to
 *     understand the syntax from a higher-numbered version of Upstart).
 *
 * - invalid JSON entries:
 *
 *   - only strategy is to error immediately.
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
 * An example of a problematic platform is Linux on AMD64 where:
 *
 * - sizeof(int)    == 4 bytes
 * - sizeof(time_t) == 8 bytes
 *
 * For platforms such as this, using JSON-C 0.90 it would be necessary
 * to encode all opaque types whose size is greater than sizeof(int)
 * as strings to avoid data loss. However, this makes for ugly
 * and error-prone code.
 *
 * == Enum Handling ==
 *
 * Handling enums is problematic since is we JSON-encode the enum _value_
 * as an integer, and if the newer version of Upstart has either:
 *
 * (a) changed the _order_ of the enum entries, or
 * (b) removed the encoded value entirely...
 *
 * We cannot proceed with stateful re-exec as undefined behaviour would
 * result since not only would the deserialised enum value be incorrect,
 * but the two scenarios above cannot even be detected at runtime.
 *
 * The only safe solution is to encode all enum values as strings,
 * requiring two new functions per enum type: one to map an enum value
 * to a string and another to convert a string representation back into
 * an enum value.
 *
 * With this strategy, both scenarios above are handled: if either
 * function fails to find the provided value, the error scenario
 * can be handled (by reverting to stateless re-exec. Keeping the two
 * extra functions per enum updated to match changes in
 * the actual enum provides an extra maintenance burden but this problem
 * is vastly more preferable than having to deal with insiduous data
 * corruption caused by an incorrect program. The burden is minimized by
 * using the following two macros...
 *
 *     state_get_json_enum_var ()
 *     state_set_json_enum_var ()
 *
 * ... coupled with the typedefs EnumSerialiser and EnumDeserialiser
 * along with the two extra functions per enum.
 *
 * === Alternative Approach ===
 *
 * An alternative approach would be to create a meta header in the JSON which
 * encodes a "version" number for each enum type. If the encoded version
 * number differs from the currently running enum version number, it
 * would be necessary to call a function to convert the old value to the
 * new enum value. Something like:
 *
 *   <enum_value> <object>_remap_<enum_name> (int old_value);
 *
 * For example,
 *
 *   JobGoal job_remap_jobgoal (int old_value);
 *
 * Note that old_value must be an 'int' since it may no longer be a
 * legitimate enum value.
 *
 * However, this strategy is more burdensome since any change to any
 * enum must also be coupled with a version bump and that is easy to
 * forget. With the approach adopted, this is never a problem since
 * forgetting to update one of the two functions per enum is guaranteed
 * to result in an error condition. Whereas, if the enum-specific version
 * number is not bumped, silent corruption could occur as no error
 * scenario could be detected.
 *
 * == Circular Dependencies ==
 *
 * Events can refer to Jobs via event->blocking and Jobs can refer to
 * Events via job->blocker and job->blocking, hence a circular
 * dependency may exist.
 *
 * === Serialisation ===
 *
 * Serialisation is handled "in a single pass" by iterating the events
 * list and encoding any blocking objects found, then iterating the
 * JobClasses instances list to encoding any blocking objects found for
 * each Job. A single pass works in this case since we're just encoding
 * the names of Jobs and Events. Some of these might not yet have been
 * encoded, but we know they _will_ eventually be encoded.
 *
 * === Deserialisation ===
 *
 * Deserialisation is more difficult. The process is as follows:
 *
 * - Convert all JSON-encoded events back into real Events.
 *   Note that event->blocking and event->blockers will _NOT_ be
 *   set at this stage.
 * - Convert all JSON-encoded jobs back into real Jobs.
 *   Note that job->blocker and job->blocking will _NOT_ be
 *   set at this stage.
 * - Re-iterate over all Jobs and...
 *   - set job->blocker if encoded in the JSON.
 *   - create any job->blocking links to other jobs.
 *   - create any job->blocking links to any events.
 *   - create any job->blocking links to any D-Bus messages.
 * - Re-iterate over all Events and...
 *   - create any event->blocking links to any jobs.
 *   - create any event->blocking links to any D-Bus messages.
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

#include <stdio.h>
#include <errno.h>

#include <sys/time.h>
#include <sys/resource.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>

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
#if 0
/**
 * STATE_VERSION:
 *
 * Newest serialistion data format version supported.
 *
 * Increment when new versions are introduced but ensure output
 * compatible with all prior versions can be generated.
 **/
#define STATE_VERSION 1
#endif

/**
 * STATE_WAIT_SECS:
 *
 * Time to wait in seconds for the re-exec state file descriptor to be
 * ready for writing. If this timeout is reached, the new PID 1 instance
 * must either have got into trouble or not support stateful re-exec.
 **/
#define STATE_WAIT_SECS 3

/**
 * state_enum_to_str:
 *
 * Helper macro for EnumSerialiser functions.
 **/
#define state_enum_to_str(enum_value, num) \
	if (enum_value == num) \
		return #enum_value

/**
 * state_str_to_enum:
 *
 * Helper macro for EnumDeserialiser functions.
 **/
#define state_str_to_enum(enum_value, str) \
	if (! strcmp (#enum_value, str)) \
		return enum_value

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
 * _state_get_json_num_var:
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
 * Query @json, setting @var to value of @name.
 *
 * XXX: Do not call directly (to avoid hard-coding assumptions about
 * XXX: integer sizes) - use state_get_json_int* macros instead.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define _state_get_json_num_var(json, name, type_json, var) \
	({json_object *_json_var = NULL; \
	 int ret = state_get_json_var_full (json, name, type_json, _json_var); \
	 errno = 0; \
	 if (_json_var) \
	 	var = json_object_get_ ## type_json (_json_var); \
	 _json_var && ret && errno != EINVAL;})

/**
 * state_get_json_int32_var:
 *
 * @json: json_object pointer,
 * @name: string name of 32-bit numeric element within
 *        @json to be deserialised,
 * @var: variable to set to value encoded in @json.
 *
 * Query @json, setting @var to 32-bit integer value of @name.
 *
 * XXX: May be called directly, but preferable to call
 * XXX: state_get_json_int_var() to ensure portability.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_get_json_int32_var(json, name, var) \
	 (_state_get_json_num_var (json, name, int, var))

/**
 * state_get_json_int64_var:
 *
 * @json: json_object pointer,
 * @name: string name of 64-bit numeric element within
 *        @json to be deserialised,
 * @var: variable to set to value encoded in @json.
 *
 * Query @json, setting @var to 64-bit integer value of @name.
 *
 * XXX: May be called directly, but preferable to call
 * XXX: state_get_json_int_var() to ensure portability.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_get_json_int64_var(json, name, var) \
	 (_state_get_json_num_var (json, name, int64, var))

/**
 * state_get_json_int_var:
 *
 * @json: json_object pointer,
 * @name: string name of numeric element within
 *        @json to be deserialised,
 * @var: variable to set to value encoded in @json.
 *
 * Query @json, setting @var to integer value of @name.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_get_json_int_var(json, name, var) \
	(sizeof (var) == (size_t)4 \
		? state_get_json_int32_var (json, name, var) \
		: state_get_json_int64_var (json, name, var))

/**
 * _state_get_json_num_var_to_obj:
 *
 * @json: json_object pointer,
 * @object: pointer to internal object that is to be deserialised,
 * @name: name of numeric element within @object to be deserialised,
 * @type_json: type of @name in @json (without prefix).
 *
 * Extract stringified @name from @json and set numeric element named
 * @name in @object to its value.
 *
 * XXX: Do not call directly (to avoid hard-coding assumptions about
 * XXX: integer sizes) - use state_get_json_int* macros instead.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define _state_get_json_num_var_to_obj(json, object, name, type_json) \
	 (_state_get_json_num_var (json, #name, type_json, ((object)->name)))

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
 * XXX: May be called directly, but preferable to call
 * XXX: state_get_json_int_var_to_obj() to ensure portability.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_get_json_int32_var_to_obj(json, object, name) \
	  (_state_get_json_num_var_to_obj (json, object, name, int))


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
 * XXX: May be called directly, but preferable to call
 * XXX: state_get_json_int_var_to_obj() to ensure portability.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_get_json_int64_var_to_obj(json, object, name) \
	  (_state_get_json_num_var_to_obj (json, object, name, int64))


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
 * state_get_json_enum_var:
 *
 * @json: json_object pointer,
 * @func: function to convert string value of enum to a real enum,
 * @name: name of enum value within @json to deserialise,
 * @var: name of enum variable to assign value extracted from JSON to.
 *
 * Extract the string value of an enum of type @name from JSON and
 * assign value to @var.
 *
 * @func must accept a stringified enum value name and return an integer
 * enum value.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_get_json_enum_var(json, func, name, var) \
	 ({int tmp = -1; \
	  const char *_value = NULL; \
	  EnumDeserialiser f = (EnumDeserialiser)func; \
	  int ret; \
	  ret = state_get_json_string_var (json, name, _value); \
	  if (ret) { \
		tmp = f (_value); \
		if (tmp != -1) \
			var = tmp; \
	  } ret && tmp != -1; })

/**
 * state_set_json_enum_var:
 * 
 * @json: json_object pointer,
 * @func: function to convert enum value into a string representation,
 * @name: name to associate with enum value in @json,
 * @value: enum value to add.
 *
 * Add enum with value @value to @json with string name @name.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_set_json_enum_var(json, func, name, value) \
	 ({const char *enum_value; \
	  EnumSerialiser f = (EnumSerialiser)func; \
	  enum_value = f (value); \
	  enum_value && \
	  state_set_json_string_var (json, name, enum_value);})

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
	(state_set_json_num_var_from_obj (json, object, name, int, int32_t))

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
	(state_set_json_num_var_from_obj (json, object, name, int, int64_t))

/**
 * state_set_json_int32_var:
 *
 * @json: json_object pointer,
 * @name: name to give @var in @json,
 * @var: 32-bit integer variable to be serialised.
 *
 * Add value of 32-bit integer entity @var to @json with name @name.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_set_json_int32_var(json, name, var) \
	 (state_set_json_var_full (json, name, var, int))

/**
 * state_set_json_int64_var:
 *
 * @json: json_object pointer,
 * @name: name to give @var in @json,
 * @var: 64-bit integer variable to be serialised.
 *
 * Add value of 64-bit integer entity @var to @json with name @name.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_set_json_int64_var(json, name, var) \
	 (state_set_json_var_full (json, name, var, int64))

/**
 * state_set_json_int_var:
 *
 * @json: json_object pointer,
 * @name: name to give @var in @json,
 * @var: integer variable to be serialised.
 *
 * Add value of integer entity @var to @json with name @name.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_set_json_int_var(json, name, var) \
	(sizeof (var) == (size_t)4 \
	? state_set_json_int32_var (json, name, var) \
	: state_set_json_int64_var (json, name, var))

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
 * state_set_json_string_var:
 *
 * @json: json_object pointer,
 * @name: name of element to add to @json,
 * @value: value to assign @name in @json.
 *
 * Add @name to @json with value @value.
 *
 * Returns: TRUE on success, or FALSE on error.
 **/
#define state_set_json_string_var(json, name, value) \
	({json_object *_json_var = \
	 json_object_new_string (value ? value : ""); \
	 if (_json_var) json_object_object_add (json, name, _json_var); \
	 _json_var;})

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
	((parent)->name = (source)->name)

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

/* FIXME: remove */
#if 0
void state_init          (int version);

int  state_get_version   (void)
	__attribute__ ((warn_unused_result));

void state_show_version  (void);

int  state_serialiseable (void)
	__attribute__ ((warn_unused_result));
#endif

int  state_read          (int fd)
	__attribute__ ((warn_unused_result));

int  state_write         (int fd)
	__attribute__ ((warn_unused_result));

int  state_read_objects  (int fd)
	__attribute__ ((warn_unused_result));

int  state_write_objects (int fd)
	__attribute__ ((warn_unused_result));

int  state_to_string (char **json_string, size_t *len)
	__attribute__ ((warn_unused_result));

int    state_from_string (const char *state)
	__attribute__ ((warn_unused_result));

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

/* FIXME: remove - not needed? */
#if 0
int
state_serialise_resolve_deps (json_object *json_events,
		json_object *json_job_classes)
	__attribute__ ((warn_unused_result));
#endif

/**
 * EnumSerialiser:
 *
 * @value: enum value.
 *
 * Convert @value to a string value.
 *
 * Returns: string value of @value, or NULL if @value is unknown.
 **/
typedef const char *(*EnumSerialiser) (int value);

/**
 * EnumDeserialiser:
 *
 * @name: string representation of an enum value.
 *
 * Convert @name to an enum value.
 *
 * Returns: enum value of @name, or -1 if @name is unknown.
 **/
typedef int (*EnumDeserialiser) (const char *name);

int
state_deserialise_resolve_deps (json_object *json)
	__attribute__ ((warn_unused_result));

json_object *
state_serialise_blocking (const NihList *blocking)
	__attribute__ ((malloc, warn_unused_result));

int
state_deserialise_blocking (void *parent, NihList *list,
			    json_object *json)
	__attribute__ ((warn_unused_result));

int state_fd_valid (int fd)
	__attribute__ ((warn_unused_result));

NIH_END_EXTERN

#endif /* INIT_STATE_H */
