/* upstart
 *
 * process.c - process definition handling
 *
 * Copyright © 2009 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
 *

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <string.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/logging.h>

#include "process.h"


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
