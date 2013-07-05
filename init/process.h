/* upstart
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

#ifndef INIT_PROCESS_H
#define INIT_PROCESS_H

#include <nih/macros.h>

#include <json.h>

/**
 * ProcessType:
 *
 * This is used to enumerate the array of process definitions attached to
 * a job class, and the array of pids attached to a job instance.
 *
 * Note that PROCESS_INVALID would ideally be -1 but that isn't possible
 * since process_type_str_to_enum() would then not be able to distinguish
 * between an invalid ProcessType and the default value assigned to a
 * ProcessType. It also cannot be zero since that would upset iterating
 * through the (non-invalid) entries.
 **/
typedef enum process_type {
	/* initial value denoting no process */
	PROCESS_INVALID = -2,

	PROCESS_MAIN = 0,
	PROCESS_PRE_START,
	PROCESS_POST_START,
	PROCESS_PRE_STOP,
	PROCESS_POST_STOP,
	PROCESS_SECURITY,
	PROCESS_LAST,
} ProcessType;


/**
 * Process:
 * @script: whether a shell will be required,
 * @command: command or script to be run.
 *
 * This structure is used for process definitions in the job class, defining
 * processes that will be run by its instances.
 *
 * When @script is FALSE, @command is checked for shell characters; if there
 * are none, it is split on whitespace and executed directly using exec().
 * If there are shell characters, or @script is TRUE, @command is executed
 * using a shell.
 **/
typedef struct process {
	int    script;
	char  *command;
} Process;


NIH_BEGIN_EXTERN

Process *   process_new       (const void *parent)
	__attribute__ ((warn_unused_result));

const char *process_name      (ProcessType process)
	__attribute__ ((const));

json_object *process_serialise (const Process *process)
	__attribute__ ((warn_unused_result));

Process *process_deserialise (json_object *json, const void *parent)
	__attribute__ ((warn_unused_result));

json_object *
process_serialise_all (const Process * const * const processes)
	__attribute__ ((warn_unused_result));

ProcessType process_from_name (const char *process)
	__attribute__ ((warn_unused_result));

int
process_deserialise_all (json_object *json, const void *parent,
			 Process **processes)
	__attribute__ ((warn_unused_result));

const char *
process_type_enum_to_str (ProcessType type)
	__attribute__ ((warn_unused_result));

ProcessType
process_type_str_to_enum (const char *type)
	__attribute__ ((warn_unused_result));

NIH_END_EXTERN

#endif /* INIT_PROCESS_H */
