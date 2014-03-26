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

#ifndef INIT_ERRORS_H
#define INIT_ERRORS_H

#include <nih/macros.h>
#include <nih/errors.h>


/* Allocated error numbers */
enum {
	UPSTART_ERROR_START = NIH_ERROR_APPLICATION_START,

	/* Errors while dealing with environment */
	ENVIRON_ILLEGAL_PARAM,
	ENVIRON_UNKNOWN_PARAM,
	ENVIRON_EXPECTED_OPERATOR,
	ENVIRON_MISMATCHED_BRACES,

	/* Errors while handling job processes */
	JOB_PROCESS_ERROR,

	/* Errors while parsing configuration files */
	PARSE_ILLEGAL_INTERVAL,
	PARSE_ILLEGAL_EXIT,
	PARSE_ILLEGAL_SIGNAL,
	PARSE_ILLEGAL_UMASK,
	PARSE_ILLEGAL_NICE,
	PARSE_ILLEGAL_OOM,
	PARSE_ILLEGAL_LIMIT,
	PARSE_EXPECTED_EVENT,
	PARSE_EXPECTED_OPERATOR,
	PARSE_EXPECTED_VARIABLE,
	PARSE_MISMATCHED_PARENS,

	/* Errors while handling control requests */
	CONTROL_NAME_TAKEN,

	/* Errors while manipulating cgroups */
	CGROUP_ERROR,
};

/* Error strings for defined messages */
#define ENVIRON_ILLEGAL_PARAM_STR	N_("Illegal parameter")
#define ENVIRON_UNKNOWN_PARAM_STR	N_("Unknown parameter")
#define ENVIRON_EXPECTED_OPERATOR_STR	N_("Expected operator")
#define ENVIRON_MISMATCHED_BRACES_STR	N_("Mismatched braces")
#define JOB_PROCESS_INVALID_SETUID_STR	N_("Invalid setuid user name does not exist")
#define JOB_PROCESS_INVALID_SETGID_STR	N_("Invalid setgid group name does not exist")
#define JOB_PROCESS_SECURITY_STR      	N_("Failed to set security context")
#define PARSE_ILLEGAL_INTERVAL_STR	N_("Illegal interval, expected number of seconds")
#define PARSE_ILLEGAL_EXIT_STR		N_("Illegal exit status, expected integer")
#define PARSE_ILLEGAL_SIGNAL_STR	N_("Illegal signal status, expected integer")
#define PARSE_ILLEGAL_UMASK_STR		N_("Illegal file creation mask, expected octal integer")
#define PARSE_ILLEGAL_NICE_STR		N_("Illegal nice value, expected -20 to 19")
#define PARSE_ILLEGAL_OOM_STR		N_("Illegal oom adjustment, expected -16 to 15 or 'never'")
#define PARSE_ILLEGAL_OOM_SCORE_STR	N_("Illegal oom score adjustment, expected -999 to 1000 or 'never'")
#define PARSE_ILLEGAL_LIMIT_STR		N_("Illegal limit, expected 'unlimited' or integer")
#define PARSE_EXPECTED_EVENT_STR	N_("Expected event")
#define PARSE_EXPECTED_OPERATOR_STR	N_("Expected operator")
#define PARSE_EXPECTED_VARIABLE_STR	N_("Expected variable name before value")
#define PARSE_MISMATCHED_PARENS_STR	N_("Mismatched parentheses")
#define CONTROL_NAME_TAKEN_STR		N_("Name already taken")

#endif /* INIT_ERRORS_H */
