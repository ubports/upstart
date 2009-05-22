/* upstart
 *
 * Copyright © 2009 Canonical Ltd.
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

#ifndef INIT_PROCESS_H
#define INIT_PROCESS_H

#include <nih/macros.h>


/**
 * ProcessType:
 *
 * This is used to enumerate the array of process definitions attached to
 * a job class, and the array of pids attached to a job instance.
 **/
typedef enum process_type {
	PROCESS_MAIN,
	PROCESS_PRE_START,
	PROCESS_POST_START,
	PROCESS_PRE_STOP,
	PROCESS_POST_STOP,
	PROCESS_LAST
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
	__attribute__ ((warn_unused_result, malloc));

const char *process_name      (ProcessType process)
	__attribute__ ((const));
ProcessType process_from_name (const char *process);

NIH_END_EXTERN

#endif /* INIT_PROCESS_H */
