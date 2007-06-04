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
	CFG_ILLEGAL_VALUE,
};

/* Error strings for defined messages */
#define CFG_ILLEGAL_VALUE_STR           N_("Illegal value")

#endif /* INIT_ERRORS_H */
