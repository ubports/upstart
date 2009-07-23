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

#ifndef UTIL_SYSV_H
#define UTIL_SYSV_H

#include <nih/macros.h>


NIH_BEGIN_EXTERN

int sysv_change_runlevel (int runlevel, char * const *env,
			  const char *utmp_file, const char *wtmp_file)
	__attribute__ ((warn_unused_result));

NIH_END_EXTERN

#endif /* UTIL_SYSV_H */
