/* upstart
 *
 * environ.c - environment table utilities
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
#include <nih/error.h>

#include "environ.h"
#include "errors.h"


/* Prototypes for static functions */
static char *environ_expand_until (char **str, const void *parent,
				   size_t *len, size_t *pos, char * const *env,
				   const char *until);


/**
 * environ_add:
 * @env: pointer to environment table,
 * @parent: parent object for new array,
 * @len: length of @env,
 * @replace: TRUE if existing entry should be replaced,
 * @str: string to add.
 *
 * Add the new environment variable @str to the table @env (which has @len
 * elements, excluding the final NULL element), either replacing an existing
 * entry or appended to the end.  Both the array and the new string within it
 * are allocated using nih_alloc().
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
 * If the array pointed to by @env is NULL, the array will be allocated
 * and @str the first element, and if @parent is not NULL, it should be a
 * pointer to another object which will be used as a parent for the returned
 * array.  When all parents of the returned array are freed, the returned
 * array will also be freed.
 *
 * When the array pointed to by @env is not NULL, @parent is ignored;
 * though it usual to pass a parent of @env for style reasons.
 *
 * Returns: new array pointer or NULL if insufficient memory.
 **/
char **
environ_add (char       ***env,
	     const void   *parent,
	     size_t       *len,
	     int           replace,
	     const char   *str)
{
	size_t           key, _len;
	char           **old_str;
	nih_local char  *new_str = NULL;

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
		new_str = nih_strdup (NULL, str);
		if (! new_str)
			return NULL;
	} else {
		const char *value;

		value = getenv (str);
		if (value) {
			new_str = nih_sprintf (NULL, "%s=%s", str, value);
			if (! new_str)
				return NULL;
		}
	}

	/* Check the environment table for an existing entry for the key,
	 * if we find one we either finish or overwrite it instead of
	 * extending the table.
	 */
	old_str = (char **)environ_lookup (*env, str, key);
	if (old_str && replace) {
		nih_unref (*old_str, *env);

		if (new_str) {
			*old_str = new_str;
			nih_ref (new_str, *env);
		} else {
			memmove (old_str, old_str + 1,
				 (char *)(*env + *len) - (char *)old_str);
			(*len)--;
		}

		return *env;
	} else if (old_str) {
		return *env;
	}

	/* No existing entry exists so extend the table instead.
	 */
	if (new_str) {
		if (! nih_str_array_addp (env, parent, len, new_str))
			return NULL;
	}

	return *env;
}

/**
 * environ_append:
 * @env: pointer to environment table,
 * @parent: parent object for new array,
 * @len: length of @env,
 * @replace: TRUE if existing entries should be replaced,
 * @new_env: environment table to append to @env.
 *
 * Appends the entries in the environment table @new_env to the existing
 * table @env (which has @len elements, excluding the final NULL element),
 * either replacing an existing entry entry or appended to the end.
 *
 * Both the array and the new strings within it are allocated using
 * nih_alloc().
 *
 * @len will be updated to contain the new array length and @env will
 * be updated to point to the new array pointer; use the return value
 * simply to check for success.
 *
 * If the array pointed to by @env is NULL, the array will be allocated
 * and @str the first element, and if @parent is not NULL, it should be a
 * pointer to another object which will be used as a parent for the returned
 * array.  When all parents of the returned array are freed, the returned
 * array will also be freed.
 *
 * When the array pointed to by @env is not NULL, @parent is ignored;
 * though it usual to pass a parent of @env for style reasons.
 *
 * Note that if this fails, some of the entries may have been appended
 * to the array already.  It's perfectly safe to call it again, since
 * existing entries will either be replaced with the same value or
 * retained (with the same value they're supposed to have).
 *
 * Returns: new array pointer or NULL if insufficient memory.
 **/
char **
environ_append (char       ***env,
		const void   *parent,
		size_t       *len,
		int           replace,
		char * const *new_env)
{
	char * const *e;

	nih_assert (env != NULL);

	for (e = new_env; e && *e; e++)
		if (! environ_add (env, parent, len, replace, *e))
			return NULL;

	return *env;
}


/**
 * environ_set:
 * @env: pointer to environment table,
 * @parent: parent object for new array,
 * @len: length of @env,
 * @replace: TRUE if existing entry should be replaced,
 * @format: format string.
 *
 * Add the new environment variable named @key with the value specified by
 * the format string @format to the table @env (which has @len elements,
 * excluding the final NULL element), either replacing an existing entry or
 * appended to the end.  Both the array and the new string within it
 * are allocated using nih_alloc().
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
 * If the array pointed to by @env is NULL, the array will be allocated
 * and @str the first element, and if @parent is not NULL, it should be a
 * pointer to another object which will be used as a parent for the returned
 * array.  When all parents of the returned array are freed, the returned
 * array will also be freed.
 *
 * When the array pointed to by @env is not NULL, @parent is ignored;
 * though it usual to pass a parent of @env for style reasons.
 *
 * Returns: new array pointer or NULL if insufficient memory.
 **/
char **
environ_set (char       ***env,
	     const void   *parent,
	     size_t       *len,
	     int           replace,
	     const char   *format,
	     ...)
{
	nih_local char  *str = NULL;
	va_list          args;
	char           **ret;

	nih_assert (env != NULL);
	nih_assert (format != NULL);

	va_start (args, format);
	str = nih_vsprintf (NULL, format, args);
	va_end (args);

	if (! str)
		return NULL;

	ret = environ_add (env, parent, len, replace, str);

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
 * which contains entries of KEY=VALUE form and return a pointer to the
 * value.
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
 * in the @env table given which contains entries of KEY=VALUE form and
 * return a pointer to the value.
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
	if (e) {
		const char *ret;

		ret = strchr (*e, '=');
		nih_assert (ret != NULL);

		return ret + 1;
	}

	return NULL;
}


/**
 * environ_valid:
 * @key: string to check,
 * @len: length of @key.
 *
 * Check whether the environment key @key, that is @len characters long,
 * is valid according to the usual rules.  Names may begin with an alpha or
 * an underscore, and then consist of any number of alphanumerics and
 * underscores.
 *
 * Returns: TRUE if @key is a valid variable name, FALSE otherwise.
 **/
int
environ_valid (const char *key,
	       size_t      len)
{
	nih_assert (key != NULL);

	if (! len)
		return FALSE;

	if ((*key != '_')
	    && ((*key < 'A') || (*key > 'Z'))
	    && ((*key < 'a') || (*key > 'z')))
		return FALSE;

	while (--len) {
		++key;
		if ((*key != '_')
		    && ((*key < 'A') || (*key > 'Z'))
		    && ((*key < 'a') || (*key > 'z'))
		    && ((*key < '0') || (*key > '9')))
			return FALSE;
	}

	return TRUE;
}

/**
 * environ_all_valid:
 * @env: NULL-terminated array of variables to check.
 *
 * Checks each of the environment variables in @env for validity; that is
 * each must be of KEY=VALUE form, and KEY must be a valid name for a
 * variable.
 *
 * This is intended for checking external data such as that in control
 * messages; environment lists defined in job definitions are actually
 * permitted to omit the VALUE to retrieve it from the environment.
 *
 * Returns: TRUE if all entries are valid, FALSE otherwise.
 **/
int
environ_all_valid (char * const *env)
{
	char * const *e;

	nih_assert (env != NULL);

	for (e = env; *e; e++) {
		char *value;

		value = strchr (*e, '=');
		if (! value)
			return FALSE;

		if (! environ_valid (*e, value - *e))
			return FALSE;
	}

	return TRUE;
}


/**
 * environ_expand:
 * @parent: parent object for new string,
 * @string: string to expand,
 * @env: NULL-terminated list of environment variables to use.
 *
 * Expand variable references in @string using the NULL-terminated list of
 * KEY=VALUE strings in the given @env table, returning a newly allocated
 * string with the references replaced by the values.
 *
 * Variables may be referenced trivially as $KEY, or where ambiguity is
 * present as ${KEY}.  References may be nested, so ${$KEY}} will first
 * expand $KEY, and then expand the variable named within that variable.
 *
 * Shell like operator expansions are also permitted.  ${KEY:-default}
 * will expand to $KEY if not unset or null, "default" otherwise.
 * ${KEY-default} is as above, but expands to null if $KEY is null.
 *
 * ${KEY:+alternate} will expand to null if $KEY is unset or null,
 * "alternate" otherwise.  ${KEY+alternate} is as above, but will expand
 * to "alternate" only if $KEY is unset.
 *
 * Unknown references are raised as an error instead of being substituted
 * with null, for that behaviour you should explicity use ${KEY-}
 *
 * If @parent is not NULL, it should be a pointer to another object which
 * will be used as a parent for the returned string.  When all parents
 * of the returned string are freed, the returned string will also be
 * freed.
 *
 * Returns: newly allocated string or NULL on raised error.
 **/
char *
environ_expand (const void   *parent,
		const char   *string,
		char * const *env)
{
	char   *str;
	size_t  len, pos;

	nih_assert (string != NULL);

	str = nih_strdup (parent, string);
	if (! str) {
		nih_error_raise_system ();
		return NULL;
	}

	len = strlen (string);
	pos = 0;

	if (! environ_expand_until (&str, parent, &len, &pos, env, ""))
		return NULL;

	return str;
}

/**
 * environ_expand_until:
 * @str: string being expanded,
 * @parent: parent object for new string,
 * @len: length of @str,
 * @pos: current position within @str,
 * @env: NULL-terminated list of environment variables to use,
 * @until: characters to stop expansion on.
 *
 * Perform an in-place expansion of variable references in @str using the
 * NULL-terminated list of KEY=VALUE strings in the given @env table,
 * stopping expansion when any of the characters in @until or the end of
 * @str is reached.
 *
 * See environ_expand() for the valid references.
 *
 * @len will be updated after each expansion to contain the new length,
 * which may be shorter or longer than before depending on the relative
 * lengths of the reference and value.
 *
 * @pos will be likewise updated to point to the character listed in @until.
 *
 * @parent is ignored, though for style reasons it is usual to pass a
 * parent of @str.
 *
 * Returns: reallocated string or NULL on raised error.
 **/
static char *
environ_expand_until (char        **str,
		      const void   *parent,
		      size_t       *len,
		      size_t       *pos,
		      char * const *env,
		      const char   *until)
{
	nih_assert (str != NULL);
	nih_assert (*str != NULL);
	nih_assert (len != NULL);
	nih_assert (pos != NULL);
	nih_assert (until != NULL);

	for (;;) {
		enum { OP_VALUE, OP_DEFAULT, OP_ALTERNATE } op = OP_VALUE;
		size_t      start, end;
		size_t      name_start, name_end;
		size_t      arg_start = 0, arg_end = 0;
		int         ignore_empty = FALSE;
		const char *value;
		size_t      value_len, offset;

		/* Locate the start of the next reference, if we have no
		 * further references, we can exit the loop and return the
		 * string.
		 */
		while (((*str)[*pos] != '$')
		       && (! strchr (until, (*str)[*pos])))
			(*pos)++;
		if ((*str)[*pos] != '$')
			break;

		/* Look at the next character to find out whether this is
		 * a simple expansion, a bracketed expansion or a lone
		 * dollar sign.
		 */
		start = (*pos)++;
		if (((*str)[*pos] == '_')
		    || (((*str)[*pos] >= 'A') && ((*str)[*pos] <= 'Z'))
		    || (((*str)[*pos] >= 'a') && ((*str)[*pos] <= 'z')))
		{
			/* Simple reference; use all following alphanumeric
			 * characters and leave pos pointing at the first
			 * non-reference character.
			 */
			name_start = (*pos)++;
			while (((*str)[*pos] == '_')
			       || (((*str)[*pos] >= 'A') && ((*str)[*pos] <= 'Z'))
			       || (((*str)[*pos] >= 'a') && ((*str)[*pos] <= 'z'))
			       || (((*str)[*pos] >= '0') && ((*str)[*pos] <= '9')))
				(*pos)++;
			name_end = end = (*pos);

		} else if (((*str)[*pos] == '{')
			   && ((*str)[*pos + 1] == '}')) {
			/* Empty bracketed expression; this is a special that
			 * is always replaced by the literal dollar sign.
			 */
			*pos += 2;
			end = *pos;

			value = "$";
			value_len = 1;
			goto subst;

		} else if ((*str)[*pos] == '{') {
			/* Bracketed reference; step over the bracket and
			 * treat the inner as another string to be expanded,
			 * terminated by any character that terminates the
			 * name part of the reference.
			 */
			name_start = ++(*pos);
			if (! environ_expand_until (str, parent, len, pos,
						    env, "}:-+"))
				return NULL;

			name_end = (*pos);

			/* Check the environment variable name is
			 * actually valid
			 */
			if (! environ_valid (*str + name_start,
					     name_end - name_start)) {
				nih_error_raise_printf (
					ENVIRON_ILLEGAL_PARAM,
					"%s: %.*s", _(ENVIRON_ILLEGAL_PARAM_STR),
					(int)(name_end - name_start),
					*str + name_start);

				goto error;
			}

			/* Check for an expression operator; if we find one,
			 * step over it and evalulate the rest of the bracketed
			 * expression to find the substitute value.
			 */
			if (((*str)[*pos] == ':')
			    && (*str)[*pos + 1] == '-') {
				(*pos) += 2;
				op = OP_DEFAULT;
				ignore_empty = TRUE;
			} else if (((*str)[*pos] == ':')
			    && (*str)[*pos + 1] == '+') {
				(*pos) += 2;
				op = OP_ALTERNATE;
				ignore_empty = TRUE;
			} else if ((*str)[*pos] == '-') {
				(*pos)++;
				op = OP_DEFAULT;
			} else if ((*str)[*pos] == '+') {
				(*pos)++;
				op = OP_ALTERNATE;
			} else if (((*str)[*pos] != '}')
				   && ((*str)[*pos] != '\0')) {
				nih_error_raise (ENVIRON_EXPECTED_OPERATOR,
						 _(ENVIRON_EXPECTED_OPERATOR_STR));
				goto error;
			}

			/* Expand any argument appearing after the expression
			 * operator; for simple value expansion, this will
			 * be almost a no-op, except we'll have defined values
			 */
			arg_start = *pos;
			if (! environ_expand_until (str, parent, len, pos,
							    env, "}"))
				return NULL;

			arg_end = (*pos);

			/* Make sure the final character ends the bracketed
			 * expression and that we haven't hit the end of the
			 * string.
			 */
			if ((*str)[*pos] != '}') {
				nih_error_raise (ENVIRON_MISMATCHED_BRACES,
						 _(ENVIRON_MISMATCHED_BRACES_STR));
				goto error;
			}

			end = ++(*pos);
		} else {
			continue;
		}

		/* Lookup the environment variable.  How we handle whether
		 * this is NULL or not depends on the operator in effect.
		 */
		value = environ_getn (env, *str + name_start,
				      name_end - name_start);

		switch (op) {
		case OP_VALUE:
			/* Value must be directly substituted from the
			 * environment; if it doesn't exist, raise an error.
			 */
			if (value == NULL) {
				nih_error_raise_printf (
					ENVIRON_UNKNOWN_PARAM,
					"%s: %.*s", _(ENVIRON_UNKNOWN_PARAM_STR),
					(int)(name_end - name_start),
					*str + name_start);
				goto error;
			}

			value_len = strlen (value);
			break;
		case OP_DEFAULT:
			/* Value may be directly substitued from the
			 * environment if set, otherwise we substitute from
			 * the argument to the expression.
			 */
			if ((value == NULL)
			    || (ignore_empty && (value[0] == '\0'))) {
				value = *str + arg_start;
				value_len = arg_end - arg_start;
			} else {
				value_len = strlen (value);
			}
			break;
		case OP_ALTERNATE:
			/* Substitute the empty string if the value is
			 * NULL or unset, otherwise substitute the argument
			 * to the expression.
			 */
			if ((value == NULL)
			    || (ignore_empty && (value[0] == '\0'))) {
				value = "";
				value_len = 0;
			} else {
				value = *str + arg_start;
				value_len = arg_end - arg_start;
			}
			break;
		default:
			nih_assert_not_reached ();
		}

	subst:
		/* Work out whether we need to extend the string to fit the
		 * value in place, then adjust the string so that there's
		 * the right gap for the value to slot in.
		 */
		if (value_len > end - start) {
			char *new_str;

			offset = value_len - (end - start);
			new_str = nih_realloc (*str, parent,
					       *len + offset + 1);
			if (! new_str) {
				nih_error_raise_system ();
				goto error;
			}

			*str = new_str;

			memmove (*str + end + offset, *str + end,
				 *len - end + 1);
			memmove (*str + start, value, value_len);

			*len += offset;
			*pos += offset;
		} else if (value_len < end - start) {
			offset = (end - start) - value_len;

			memmove (*str + start, value, value_len);
			memmove (*str + end - offset, *str + end,
				 *len - end + 1);

			*len -= offset;
			*pos -= offset;
		} else {
			memmove (*str + start, value, value_len);
		}
	}

	return *str;

error:
	nih_free (*str);
	*str = NULL;
	return NULL;
}
