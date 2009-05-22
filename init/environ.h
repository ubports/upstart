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

#ifndef INIT_ENVIRON_H
#define INIT_ENVIRON_H

#include <nih/macros.h>


NIH_BEGIN_EXTERN

char **       environ_add       (char ***env, const void *parent, size_t *len,
				 int replace, const char *str)
	__attribute__ ((warn_unused_result));
char **       environ_append    (char ***env, const void *parent, size_t *len,
				 int replace, char * const *new_env)
	__attribute__ ((warn_unused_result));

char **       environ_set       (char ***env, const void *parent, size_t *len,
				 int replace, const char *format, ...)
	__attribute__ ((warn_unused_result));

char * const *environ_lookup    (char * const *env, const char *key,
				 size_t len);

const char *  environ_get       (char * const *env, const char *key);
const char *  environ_getn      (char * const *env, const char *key,
				 size_t len);

int           environ_valid     (const char *key, size_t len);
int           environ_all_valid (char * const *env);

char *        environ_expand    (const void *parent, const char *string,
				 char * const *env)
	__attribute__ ((malloc, warn_unused_result));

NIH_END_EXTERN

#endif /* INIT_ENVIRON_H */
