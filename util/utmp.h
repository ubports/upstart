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

#ifndef UTIL_UTMP_H
#define UTIL_UTMP_H

#include <nih/macros.h>


NIH_BEGIN_EXTERN

int utmp_read_runlevel  (const char *utmp_file, int *prevlevel)
	__attribute__ ((warn_unused_result));
int utmp_get_runlevel   (const char *utmp_file, int *prevlevel)
	__attribute__ ((warn_unused_result));

int utmp_write_runlevel (const char *utmp_file, const char *wtmp_file,
			 int runlevel, int prevlevel)
	__attribute__ ((warn_unused_result));
int utmp_write_shutdown (const char *utmp_file, const char *wtmp_file)
	__attribute__ ((warn_unused_result));

NIH_END_EXTERN

#endif /* UTIL_UTMP_H */
