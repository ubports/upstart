/* upstart
 *
 * Copyright © 2008 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
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

#ifndef INIT_ENVIRON_H
#define INIT_ENVIRON_H

#include <nih/macros.h>


NIH_BEGIN_EXTERN

char **       environ_add    (char ***env, const void *parent, size_t *len,
			      const char *str)
	__attribute__ ((warn_unused_result));

char **       environ_set    (char ***env, const void *parent, size_t *len,
			      const char *format, ...)
	__attribute__ ((warn_unused_result));

char * const *environ_lookup (char * const *env, const char *key, size_t len);

const char *  environ_get    (char * const *env, const char *key);
const char *  environ_getn   (char * const *env, const char *key, size_t len);

NIH_END_EXTERN

#endif /* INIT_ENVIRON_H */
