/* upstart
 *
 * environ.c - environment table utilities
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/logging.h>

#include "environ.h"


/**
 * environ_add:
 * @env: pointer to environment table,
 * @parent: parent of @env,
 * @len: length of @env,
 * @str: string to add.
 *
 * Add the new environment variable @str to the table @env (which has @len
 * elements, excluding the final NULL element), either replacing an existing
 * entry or appended to the end.  Both the array and the new string within it
 * are allocated using nih_alloc(), @parent must be that of @env.
 *
 * @str may be in KEY=VALUE format, in which case the given key will be
 * replaced with that value or appended to the table; or it may simply
 * be in KEY format, which which case the value is taken from init's own
 * environment.
 *
 * @len will be updated to contain the new array length and @env will
 * be updated to point to the new array pointer; use the return value
 * simply to check for success.
 *
 * Returns: new array pointer or NULL if insufficient memory.
 **/
char **
environ_add (char       ***env,
	     const void   *parent,
	     size_t       *len,
	     const char   *str)
{
	size_t   key, _len;
	char   **old_str, *new_str;

	nih_assert (env != NULL);
	nih_assert (str != NULL);

	/* Calculate the length in case we need to remove entries */
	if (! len) {
		char **e;

		len = &_len;

		_len = 0;
		for (e = *env; e && *e; e++)
			_len++;
	}

	/* Calculate the length of the key in the string, if we reach the
	 * end of the string, then we lookup the value in the environment
	 * and use that as the new value otherwise use the string given.
	 */
	key = strcspn (str, "=");
	if (str[key] == '=') {
		new_str = nih_strdup (*env, str);
		if (! new_str)
			return NULL;
	} else {
		const char *value;

		value = getenv (str);
		if (value) {
			new_str = nih_sprintf (*env, "%s=%s", str, value);
			if (! new_str)
				return NULL;
		} else {
			new_str = NULL;
		}
	}

	/* Check the environment table for an existing entry for the key,
	 * if we find one we overwrite it instead of extending the table.
	 */
	old_str = (char **)environ_lookup (*env, str, key);
	if (old_str) {
		nih_free (*old_str);

		if (new_str) {
			*old_str = new_str;
		} else {
			memmove (old_str, old_str + 1,
				 (char *)(*env + *len) - (char *)old_str);
			(*len)--;
		}

		return *env;
	}

	/* No existing entry exists so extend the table instead.
	 */
	if (new_str) {
		if (! nih_str_array_addp (env, parent, len, new_str)) {
			nih_free (new_str);
			return NULL;
		}
	}

	return *env;
}


/**
 * environ_set:
 * @env: pointer to environment table,
 * @parent: parent of @env,
 * @len: length of @env,
 * @format: format string.
 *
 * Add the new environment variable named @key with the value specified by
 * the format string @format to the table @env (which has @len elements,
 * excluding the final NULL element), either replacing an existing entry or
 * appended to the end.  Both the array and the new string within it
 * are allocated using nih_alloc(), @parent must be that of @env.
 *
 * The resulting string may be in KEY=VALUE format, in which case the given
 * key will be replaced with that value or appended to the table; or it may
 * simply be in KEY format, which which case the value is taken from init's
 * own environment.
 *
 * @len will be updated to contain the new array length and @env will
 * be updated to point to the new array pointer; use the return value
 * simply to check for success.
 *
 * Returns: new array pointer or NULL if insufficient memory.
 **/
char **
environ_set (char       ***env,
	     const void   *parent,
	     size_t       *len,
	     const char   *format,
	     ...)
{
	char     *str;
	va_list   args;
	char    **ret;

	nih_assert (env != NULL);
	nih_assert (format != NULL);

	va_start (args, format);
	str = nih_vsprintf (NULL, format, args);
	va_end (args);

	if (! str)
		return NULL;

	ret = environ_add (env, parent, len, str);

	nih_free (str);

	return ret;
}


/**
 * environ_lookup:
 * @env: pointer to environment table,
 * @key: key to lookup,
 * @len: length of @key.
 *
 * Lookup the environment variable named @key, which is @len characters long,
 * in the @env table given which contains entries of KEY=VALUE form.
 *
 * Returns: pointer to entry in @env or NULL if not found.
 **/
char * const *
environ_lookup (char * const *env,
		const char   *key,
		size_t        len)
{
	char * const *e;

	nih_assert (key != NULL);

	for (e = env; e && *e; e++)
		if ((strncmp (*e, key, len) == 0) && ((*e)[len] == '='))
			return e;

	return NULL;
}


/**
 * environ_get:
 * @env: pointer to environment table,
 * @key: key to lookup.
 *
 * Lookup the environment variable named @key in the @env table given
 * which contains entries of KEY=VALUE form.
 *
 * Returns: string from @env or NULL if not found.
 **/
const char *
environ_get (char * const *env,
	     const char   *key)
{
	nih_assert (key != NULL);

	return environ_getn (env, key, strlen (key));
}

/**
 * environ_getn:
 * @env: pointer to environment table,
 * @key: key to lookup,
 * @len: length of @key.
 *
 * Lookup the environment variable named @key, which is @len characters long,
 * in the @env table given which contains entries of KEY=VALUE form.
 *
 * Returns: string from @env or NULL if not found.
 **/
const char *
environ_getn (char * const *env,
	      const char   *key,
	      size_t        len)
{
	char * const *e;

	nih_assert (key != NULL);

	e = environ_lookup (env, key, len);
	if (e)
		return *e;

	return NULL;
}
