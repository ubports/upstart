/* upstart
 *
 * Copyright © 2007 Canonical Ltd.
 * Author: Scott James Remnant <scott@ubuntu.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef INIT_ERRORS_H
#define INIT_ERRORS_H

#include <nih/macros.h>
#include <nih/errors.h>


/* Allocated error numbers */
enum {
	UPSTART_ERROR_START = NIH_ERROR_APPLICATION_START,

	/* Errors while parsing configuration files */
	PARSE_ILLEGAL_INTERVAL,
	PARSE_ILLEGAL_EXIT,
	PARSE_ILLEGAL_UMASK,
	PARSE_ILLEGAL_NICE,
	PARSE_ILLEGAL_LIMIT,
	PARSE_EXPECTED_EVENT,
	PARSE_EXPECTED_OPERATOR,
	PARSE_MISMATCHED_PARENS,

	/* Errors while handling processes */
	PROCESS_ERROR,
};

/* Error strings for defined messages */
#define PARSE_ILLEGAL_INTERVAL_STR	N_("Illegal interval, expected number of seconds")
#define PARSE_ILLEGAL_EXIT_STR		N_("Illegal exit status, expected integer")
#define PARSE_ILLEGAL_UMASK_STR		N_("Illegal file creation mask, expected octal integer")
#define PARSE_ILLEGAL_NICE_STR		N_("Illegal nice value, expected -20 to 19")
#define PARSE_ILLEGAL_LIMIT_STR		N_("Illegal limit, expected 'unlimited' or integer")
#define PARSE_EXPECTED_EVENT_STR	N_("Expected event")
#define PARSE_EXPECTED_OPERATOR_STR	N_("Expected operator")
#define PARSE_MISMATCHED_PARENS_STR	N_("Mismatched parentheses")

#endif /* INIT_ERRORS_H */
