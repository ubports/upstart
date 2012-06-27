/* upstart
 *
 * process.c - process definition handling
 *
 * Copyright © 2009 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
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

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/logging.h>
#include <nih/string.h>

#include "process.h"
#include "state.h"


/**
 * process_new:
 * @parent: parent of new process.
 *
 * Allocates and returns a new empty Process structure.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.
 *
 * Returns: newly allocated Process structure or NULL if insufficient memory.
 **/
Process *
process_new  (const void *parent)
{
	Process *process;

	process = nih_new (parent, Process);
	if (! process)
		return NULL;

	process->script = FALSE;
	process->command = NULL;

	return process;
}


/**
 * process_name:
 * @process: process type to convert.
 *
 * Converts an enumerated process type into the string used for the status
 * and for logging purposes.
 *
 * Returns: static string or NULL if action not known.
 **/
const char *
process_name (ProcessType process)
{
	switch (process) {
	case PROCESS_MAIN:
		return N_("main");
	case PROCESS_PRE_START:
		return N_("pre-start");
	case PROCESS_POST_START:
		return N_("post-start");
	case PROCESS_PRE_STOP:
		return N_("pre-stop");
	case PROCESS_POST_STOP:
		return N_("post-stop");
	default:
		return NULL;
	}
}

/**
 * process_from_name:
 * @process: process string to convert.
 *
 * Converts a process type string into the enumeration.
 *
 * Returns: enumerated action or -1 if not known.
 **/
ProcessType
process_from_name (const char *process)
{
	nih_assert (process != NULL);

	if (! strcmp (process, "main")) {
		return PROCESS_MAIN;
	} else if (! strcmp (process, "pre-start")) {
		return PROCESS_PRE_START;
	} else if (! strcmp (process, "post-start")) {
		return PROCESS_POST_START;
	} else if (! strcmp (process, "pre-stop")) {
		return PROCESS_PRE_STOP;
	} else if (! strcmp (process, "post-stop")) {
		return PROCESS_POST_STOP;
	} else {
		return -1;
	}
}

/**
 * process_serialise:
 * @process: process to serialise.
 *
 * Convert @process into a JSON representation for serialisation.
 * Caller must free returned value using json_object_put().
 *
 * Returns: JSON-serialised Process object, or NULL on error.
 **/
json_object *
process_serialise (const Process *process)
{
	json_object  *json;

	nih_assert (process);

	json = json_object_new_object ();
	if (! json)
		return NULL;

	if (! state_set_json_num_var_from_obj (json, process, script, int))
		goto error;

	if (! state_set_json_string_var_from_obj (json, process, command))
		goto error;

	return json;

error:
	json_object_put (json);
	return NULL;

}

/**
 * process_serialise_all:
 *
 * @processes: array of Processes.
 *
 * Convert array of Process objects to JSON representation.
 *
 * Returns: JSON object containing array of Processes, or NULL on error.
 */
json_object *
process_serialise_all (const Process * const * const processes)
{
	json_object *json;
	json_object *json_process;
	Process      dummy = { 0, "" };

	nih_assert (processes);

	json = json_object_new_array ();
	if (! json)
		return NULL;

	for (int i = 0; i < PROCESS_LAST; i++) {
		/* We must encode a blank entry for missing array elements
		 * to ensure correct deserialisation.
		 */
		json_process = process_serialise (processes[i]
				? processes[i]
				: &dummy);

		if (! json_process)
			goto error;

		if (json_object_array_add (json, json_process) < 0)
			goto error;
	}

	return json;

error:
	json_object_put (json);
	return NULL;
}

/**
 * process_deserialise:
 * @json: JSON-serialised Process object to deserialise.
 *
 * Convert @json into a Process object.
 *
 * Caller must manually nih_ref() returned object to a parent object.
 *
 * Returns: Process object, or NULL on error.
 **/

#if 1
/* FIXME: should we just make this the same as the other partial
 * objects for consistency?
 */
#endif
Process *
process_deserialise (json_object *json)
{
	Process  *process;

	nih_assert (json);

	if (! state_check_type (json, object))
		goto error;

	process = NIH_MUST (process_new (NULL));

	memset (process, '\0', sizeof (Process));

	if (! state_get_json_num_var_to_obj (json, process, script, int))
			goto error;

	if (! state_get_json_string_var_to_obj (json, process, command))
		goto error;

	return process;

error:
	nih_free (process);
	return NULL;
}

/**
 * process_deserialise_all:
 *
 * @json: root of JSON-serialised state,
 * @parent: parent of @processes,
 * @processes: pre-allocated pointer array to hold Process array.
 *
 * Convert JSON representation of processes back into
 * an array of Process objects.
 *
 * Returns: 0 on success, -1 on error.
 **/
int
process_deserialise_all (json_object *json, const void *parent, Process ***processes)
{
	json_object        *json_processes;
	Process            *process;
	nih_local Process  *partial = NULL;
	int                 i;

	nih_assert (json);
	nih_assert (parent);

	json_processes = json_object_object_get (json, "process");

	if (! json_processes)
		goto error;

	if (! state_check_type (json_processes, array))
		goto error;

	/* FIXME: Simplify? */
	for (i = 0, process = (*processes)[0];
		i < json_object_array_length (json_processes);
		i++, process++) {
		json_object *json_process;

		nih_assert (i <= PROCESS_LAST);

		json_process = json_object_array_get_idx (json_processes, i);
		if (! state_check_type (json_process, object))
			goto error;

		partial = process_deserialise (json_process);
		if (! partial)
			goto error;
		process = NIH_MUST (process_new (parent));
		process->command = NIH_MUST (nih_strdup	(process, partial->command));
		process->script = partial->script;
	}

	return 0;

error:
	return -1;
}
