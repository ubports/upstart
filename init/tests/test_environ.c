/* upstart
 *
 * test_environ.c - test suite for init/environ.c
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

#include <nih/test.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/error.h>

#include <errno.h>

#include "environ.h"
#include "errors.h"


void
test_add (void)
{
	char   **env = NULL, **ret;
	size_t   len = 0;

	TEST_FUNCTION ("environ_add");

	/* Check that we can add a variable to a new environment table
	 * and that it is appended to the array.
	 */
	TEST_FEATURE ("with empty table");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			len = 0;
			env = nih_str_array_new (NULL);
		}

		ret = environ_add (&env, NULL, &len, TRUE, "FOO=BAR");

		if (test_alloc_failed) {
			TEST_EQ_P (ret, NULL);

			TEST_EQ (len, 0);
			TEST_EQ_P (env[0], NULL);

			nih_free (env);
			continue;
		}

		TEST_NE_P (ret, NULL);

		TEST_EQ (len, 1);
		TEST_ALLOC_PARENT (env[0], env);
		TEST_ALLOC_SIZE (env[0], 8);
		TEST_EQ_STR (env[0], "FOO=BAR");
		TEST_EQ_P (env[1], NULL);

		nih_free (env);
	}


	/* Check that we can add a variable to an environment table with
	 * existing different entries and that it is appended to the array.
	 */
	TEST_FEATURE ("with new variable");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			len = 0;
			env = nih_str_array_new (NULL);
			assert (nih_str_array_add (&env, NULL, &len,
						   "FOO=BAR"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "BAR=BAZ"));
		}

		ret = environ_add (&env, NULL, &len, TRUE,
				   "FRODO=BAGGINS");

		if (test_alloc_failed) {
			TEST_EQ_P (ret, NULL);

			TEST_EQ (len, 2);
			TEST_EQ_STR (env[0], "FOO=BAR");
			TEST_EQ_STR (env[1], "BAR=BAZ");
			TEST_EQ_P (env[2], NULL);

			nih_free (env);
			continue;
		}

		TEST_NE_P (ret, NULL);

		TEST_EQ (len, 3);
		TEST_EQ_STR (env[0], "FOO=BAR");
		TEST_EQ_STR (env[1], "BAR=BAZ");
		TEST_ALLOC_PARENT (env[2], env);
		TEST_ALLOC_SIZE (env[2], 14);
		TEST_EQ_STR (env[2], "FRODO=BAGGINS");
		TEST_EQ_P (env[3], NULL);

		nih_free (env);
	}


	/* Check that we can add a variable from the environment to the table
	 * and that it is appended to the array.
	 */
	TEST_FEATURE ("with new variable from environment");
	putenv ("FRODO=BAGGINS");

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			len = 0;
			env = nih_str_array_new (NULL);
			assert (nih_str_array_add (&env, NULL, &len,
						   "FOO=BAR"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "BAR=BAZ"));
		}

		ret = environ_add (&env, NULL, &len, TRUE,
				   "FRODO");

		if (test_alloc_failed) {
			TEST_EQ_P (ret, NULL);

			TEST_EQ (len, 2);
			TEST_EQ_STR (env[0], "FOO=BAR");
			TEST_EQ_STR (env[1], "BAR=BAZ");
			TEST_EQ_P (env[2], NULL);

			nih_free (env);
			continue;
		}

		TEST_NE_P (ret, NULL);

		TEST_EQ (len, 3);
		TEST_EQ_STR (env[0], "FOO=BAR");
		TEST_EQ_STR (env[1], "BAR=BAZ");
		TEST_ALLOC_PARENT (env[2], env);
		TEST_ALLOC_SIZE (env[2], 14);
		TEST_EQ_STR (env[2], "FRODO=BAGGINS");
		TEST_EQ_P (env[3], NULL);

		nih_free (env);
	}

	unsetenv ("FRODO");


	/* Check that when we attempt to add a variable that's not in the
	 * environment, the table is not extended.
	 */
	TEST_FEATURE ("with new variable unset in environment");
	unsetenv ("FRODO");

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			len = 0;
			env = nih_str_array_new (NULL);
			assert (nih_str_array_add (&env, NULL, &len,
						   "FOO=BAR"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "BAR=BAZ"));
		}

		ret = environ_add (&env, NULL, &len, TRUE,
				   "FRODO");

		if (test_alloc_failed) {
			TEST_EQ_P (ret, NULL);

			TEST_EQ (len, 2);
			TEST_EQ_STR (env[0], "FOO=BAR");
			TEST_EQ_STR (env[1], "BAR=BAZ");
			TEST_EQ_P (env[2], NULL);

			nih_free (env);
			continue;
		}

		TEST_NE_P (ret, NULL);

		TEST_EQ (len, 2);
		TEST_EQ_STR (env[0], "FOO=BAR");
		TEST_EQ_STR (env[1], "BAR=BAZ");
		TEST_EQ_P (env[2], NULL);

		nih_free (env);
	}


	/* Check that we can replace a variable in the environment table
	 * when one already exists with the same or different value.
	 */
	TEST_FEATURE ("with replacement variable");
	TEST_ALLOC_FAIL {
		char *old_env;

		TEST_ALLOC_SAFE {
			len = 0;
			env = nih_str_array_new (NULL);
			assert (nih_str_array_add (&env, NULL, &len,
						   "FOO=BAR"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "BAR=BAZ"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "FRODO=BAGGINS"));
		}

		old_env = env[1];
		TEST_FREE_TAG (old_env);

		ret = environ_add (&env, NULL, &len, TRUE,
				   "BAR=WIBBLE");

		if (test_alloc_failed) {
			TEST_EQ_P (ret, NULL);
			TEST_NOT_FREE (old_env);

			TEST_EQ (len, 3);
			TEST_EQ_STR (env[0], "FOO=BAR");
			TEST_EQ_STR (env[1], "BAR=BAZ");
			TEST_EQ_STR (env[2], "FRODO=BAGGINS");
			TEST_EQ_P (env[3], NULL);

			nih_free (env);
			continue;
		}

		TEST_FREE (old_env);

		TEST_NE_P (ret, NULL);

		TEST_EQ (len, 3);
		TEST_EQ_STR (env[0], "FOO=BAR");
		TEST_ALLOC_PARENT (env[1], env);
		TEST_ALLOC_SIZE (env[1], 11);
		TEST_EQ_STR (env[1], "BAR=WIBBLE");
		TEST_EQ_STR (env[2], "FRODO=BAGGINS");
		TEST_EQ_P (env[3], NULL);

		nih_free (env);
	}


	/* Check that we can replace a variable from the environment in the
	 * environment table when one already exists with the same or
	 * different value.
	 */
	TEST_FEATURE ("with replacement variable from environment");
	putenv ("BAR=WIBBLE");

	TEST_ALLOC_FAIL {
		char *old_env;

		TEST_ALLOC_SAFE {
			len = 0;
			env = nih_str_array_new (NULL);
			assert (nih_str_array_add (&env, NULL, &len,
						   "FOO=BAR"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "BAR=BAZ"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "FRODO=BAGGINS"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "BILBO=TOOK"));
		}

		old_env = env[1];
		TEST_FREE_TAG (old_env);

		ret = environ_add (&env, NULL, &len, TRUE, "BAR");

		if (test_alloc_failed) {
			TEST_EQ_P (ret, NULL);
			TEST_NOT_FREE (old_env);

			TEST_EQ (len, 4);
			TEST_EQ_STR (env[0], "FOO=BAR");
			TEST_EQ_STR (env[1], "BAR=BAZ");
			TEST_EQ_STR (env[2], "FRODO=BAGGINS");
			TEST_EQ_STR (env[3], "BILBO=TOOK");
			TEST_EQ_P (env[4], NULL);

			nih_free (env);
			continue;
		}

		TEST_FREE (old_env);

		TEST_NE_P (ret, NULL);

		TEST_EQ (len, 4);
		TEST_EQ_STR (env[0], "FOO=BAR");
		TEST_ALLOC_PARENT (env[1], env);
		TEST_ALLOC_SIZE (env[1], 11);
		TEST_EQ_STR (env[1], "BAR=WIBBLE");
		TEST_EQ_STR (env[2], "FRODO=BAGGINS");
		TEST_EQ_STR (env[3], "BILBO=TOOK");
		TEST_EQ_P (env[4], NULL);

		nih_free (env);
	}

	unsetenv ("BAR");


	/* Check that when we attempt to replace a variable that's unset
	 * in the environment, the existing variable is removed from the
	 * table.
	 */
	TEST_FEATURE ("with replacement variable unset in environment");
	unsetenv ("BAR");

	TEST_ALLOC_FAIL {
		char *old_env;

		TEST_ALLOC_SAFE {
			len = 0;
			env = nih_str_array_new (NULL);
			assert (nih_str_array_add (&env, NULL, &len,
						   "FOO=BAR"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "BAR=BAZ"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "FRODO=BAGGINS"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "BILBO=TOOK"));
		}

		old_env = env[1];
		TEST_FREE_TAG (old_env);

		ret = environ_add (&env, NULL, &len, TRUE, "BAR");

		if (test_alloc_failed) {
			TEST_EQ_P (ret, NULL);
			TEST_NOT_FREE (old_env);

			TEST_EQ (len, 4);
			TEST_EQ_STR (env[0], "FOO=BAR");
			TEST_EQ_STR (env[1], "BAR=BAZ");
			TEST_EQ_STR (env[2], "FRODO=BAGGINS");
			TEST_EQ_STR (env[3], "BILBO=TOOK");
			TEST_EQ_P (env[4], NULL);

			nih_free (env);
			continue;
		}

		TEST_FREE (old_env);

		TEST_NE_P (ret, NULL);

		TEST_EQ (len, 3);
		TEST_EQ_STR (env[0], "FOO=BAR");
		TEST_EQ_STR (env[1], "FRODO=BAGGINS");
		TEST_EQ_STR (env[2], "BILBO=TOOK");
		TEST_EQ_P (env[3], NULL);

		nih_free (env);
	}

	unsetenv ("BAR");


	/* Check that we can add a variable to an environment table with
	 * existing different entries and that it is appended to the array,
	 * even if replace is FALSE.
	 */
	TEST_FEATURE ("with new variable but no replace");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			len = 0;
			env = nih_str_array_new (NULL);
			assert (nih_str_array_add (&env, NULL, &len,
						   "FOO=BAR"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "BAR=BAZ"));
		}

		ret = environ_add (&env, NULL, &len, FALSE,
				   "FRODO=BAGGINS");

		if (test_alloc_failed) {
			TEST_EQ_P (ret, NULL);

			TEST_EQ (len, 2);
			TEST_EQ_STR (env[0], "FOO=BAR");
			TEST_EQ_STR (env[1], "BAR=BAZ");
			TEST_EQ_P (env[2], NULL);

			nih_free (env);
			continue;
		}

		TEST_NE_P (ret, NULL);

		TEST_EQ (len, 3);
		TEST_EQ_STR (env[0], "FOO=BAR");
		TEST_EQ_STR (env[1], "BAR=BAZ");
		TEST_ALLOC_PARENT (env[2], env);
		TEST_ALLOC_SIZE (env[2], 14);
		TEST_EQ_STR (env[2], "FRODO=BAGGINS");
		TEST_EQ_P (env[3], NULL);

		nih_free (env);
	}


	/* Check that when a variable already exists in the environment
	 * table, and we're not replacing, the original value is left
	 * untouched.
	 */
	TEST_FEATURE ("with existing variable");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			len = 0;
			env = nih_str_array_new (NULL);
			assert (nih_str_array_add (&env, NULL, &len,
						   "FOO=BAR"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "BAR=BAZ"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "FRODO=BAGGINS"));
		}

		ret = environ_add (&env, NULL, &len, FALSE,
				   "BAR=WIBBLE");

		if (test_alloc_failed) {
			TEST_EQ_P (ret, NULL);

			TEST_EQ (len, 3);
			TEST_EQ_STR (env[0], "FOO=BAR");
			TEST_EQ_STR (env[1], "BAR=BAZ");
			TEST_EQ_STR (env[2], "FRODO=BAGGINS");
			TEST_EQ_P (env[3], NULL);

			nih_free (env);
			continue;
		}

		TEST_NE_P (ret, NULL);

		TEST_EQ (len, 3);
		TEST_EQ_STR (env[0], "FOO=BAR");
		TEST_EQ_STR (env[1], "BAR=BAZ");
		TEST_EQ_STR (env[2], "FRODO=BAGGINS");
		TEST_EQ_P (env[3], NULL);

		nih_free (env);
	}


	/* Check that when a variable from the environment already exists in
	 * the environment table, and we're not replacing, the original value
	 * is left untouched.
	 */
	TEST_FEATURE ("with existing variable from environment");
	putenv ("BAR=WIBBLE");

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			len = 0;
			env = nih_str_array_new (NULL);
			assert (nih_str_array_add (&env, NULL, &len,
						   "FOO=BAR"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "BAR=BAZ"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "FRODO=BAGGINS"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "BILBO=TOOK"));
		}

		ret = environ_add (&env, NULL, &len, FALSE, "BAR");

		if (test_alloc_failed) {
			TEST_EQ_P (ret, NULL);

			TEST_EQ (len, 4);
			TEST_EQ_STR (env[0], "FOO=BAR");
			TEST_EQ_STR (env[1], "BAR=BAZ");
			TEST_EQ_STR (env[2], "FRODO=BAGGINS");
			TEST_EQ_STR (env[3], "BILBO=TOOK");
			TEST_EQ_P (env[4], NULL);

			nih_free (env);
			continue;
		}

		TEST_NE_P (ret, NULL);

		TEST_EQ (len, 4);
		TEST_EQ_STR (env[0], "FOO=BAR");
		TEST_EQ_STR (env[1], "BAR=BAZ");
		TEST_EQ_STR (env[2], "FRODO=BAGGINS");
		TEST_EQ_STR (env[3], "BILBO=TOOK");
		TEST_EQ_P (env[4], NULL);

		nih_free (env);
	}

	unsetenv ("BAR");


	/* Check that when a variable from the environment is unset it
	 * does not remove an existing variable in the environment table
	 * if we're not replacing.
	 */
	TEST_FEATURE ("with existing variable unset in environment");
	unsetenv ("BAR");

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			len = 0;
			env = nih_str_array_new (NULL);
			assert (nih_str_array_add (&env, NULL, &len,
						   "FOO=BAR"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "BAR=BAZ"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "FRODO=BAGGINS"));
			assert (nih_str_array_add (&env, NULL, &len,
						   "BILBO=TOOK"));
		}

		ret = environ_add (&env, NULL, &len, FALSE, "BAR");

		if (test_alloc_failed) {
			TEST_EQ_P (ret, NULL);

			TEST_EQ (len, 4);
			TEST_EQ_STR (env[0], "FOO=BAR");
			TEST_EQ_STR (env[1], "BAR=BAZ");
			TEST_EQ_STR (env[2], "FRODO=BAGGINS");
			TEST_EQ_STR (env[3], "BILBO=TOOK");
			TEST_EQ_P (env[4], NULL);

			nih_free (env);
			continue;
		}

		TEST_NE_P (ret, NULL);

		TEST_EQ (len, 4);
		TEST_EQ_STR (env[0], "FOO=BAR");
		TEST_EQ_STR (env[1], "BAR=BAZ");
		TEST_EQ_STR (env[2], "FRODO=BAGGINS");
		TEST_EQ_STR (env[3], "BILBO=TOOK");
		TEST_EQ_P (env[4], NULL);

		nih_free (env);
	}

	unsetenv ("BAR");
}

void
test_append (void)
{
	char   **env = NULL, **new_env, **ret;
	size_t   len = 0;

	TEST_FUNCTION ("environ_append");

	/* Check that we can append all new entries onto the end of an
	 * existing environment table, without modifying the entries passed.
	 */
	TEST_FEATURE ("with new entries");
	new_env = nih_str_array_new (NULL);
	assert (environ_add (&new_env, NULL, NULL, TRUE, "MILK=white"));
	assert (environ_add (&new_env, NULL, NULL, TRUE, "TEA=green"));

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			len = 0;
			env = nih_str_array_new (NULL);
			assert (environ_add (&env, NULL, &len, TRUE, "FOO=BAR"));
			assert (environ_add (&env, NULL, &len, TRUE, "BAR=BAZ"));
		}

		ret = environ_append (&env, NULL, &len, TRUE, new_env);

		if (test_alloc_failed) {
			TEST_EQ_P (ret, NULL);
			nih_free (env);
			continue;
		}

		TEST_EQ_P (ret, env);
		TEST_EQ (len, 4);

		TEST_EQ_STR (env[0], "FOO=BAR");
		TEST_EQ_STR (env[1], "BAR=BAZ");
		TEST_EQ_STR (env[2], "MILK=white");
		TEST_EQ_STR (env[3], "TEA=green");
		TEST_EQ_P (env[4], NULL);

		nih_free (env);
	}

	nih_free (new_env);


	/* Check that if entries are being replaced, those values from the
	 * new table replace the values in the old table.
	 */
	TEST_FEATURE ("with replacement entries");
	new_env = nih_str_array_new (NULL);
	assert (environ_add (&new_env, NULL, NULL, TRUE, "MILK=white"));
	assert (environ_add (&new_env, NULL, NULL, TRUE, "TEA=green"));
	assert (environ_add (&new_env, NULL, NULL, TRUE, "FOO=apricot"));

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			len = 0;
			env = nih_str_array_new (NULL);
			assert (environ_add (&env, NULL, &len, TRUE, "FOO=BAR"));
			assert (environ_add (&env, NULL, &len, TRUE, "BAR=BAZ"));
		}

		ret = environ_append (&env, NULL, &len, TRUE, new_env);

		if (test_alloc_failed) {
			TEST_EQ_P (ret, NULL);
			nih_free (env);
			continue;
		}

		TEST_EQ_P (ret, env);
		TEST_EQ (len, 4);

		TEST_EQ_STR (env[0], "FOO=apricot");
		TEST_EQ_STR (env[1], "BAR=BAZ");
		TEST_EQ_STR (env[2], "MILK=white");
		TEST_EQ_STR (env[3], "TEA=green");
		TEST_EQ_P (env[4], NULL);

		nih_free (env);
	}

	nih_free (new_env);


	/* Check that if entries are being preserved, those values from the
	 * new table are ignored.
	 */
	TEST_FEATURE ("with preserve existing entries");
	new_env = nih_str_array_new (NULL);
	assert (environ_add (&new_env, NULL, NULL, TRUE, "MILK=white"));
	assert (environ_add (&new_env, NULL, NULL, TRUE, "TEA=green"));
	assert (environ_add (&new_env, NULL, NULL, TRUE, "FOO=apricot"));

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			len = 0;
			env = nih_str_array_new (NULL);
			assert (environ_add (&env, NULL, &len, TRUE, "FOO=BAR"));
			assert (environ_add (&env, NULL, &len, TRUE, "BAR=BAZ"));
		}

		ret = environ_append (&env, NULL, &len, FALSE, new_env);

		if (test_alloc_failed) {
			TEST_EQ_P (ret, NULL);
			nih_free (env);
			continue;
		}

		TEST_EQ_P (ret, env);
		TEST_EQ (len, 4);

		TEST_EQ_STR (env[0], "FOO=BAR");
		TEST_EQ_STR (env[1], "BAR=BAZ");
		TEST_EQ_STR (env[2], "MILK=white");
		TEST_EQ_STR (env[3], "TEA=green");
		TEST_EQ_P (env[4], NULL);

		nih_free (env);
	}

	nih_free (new_env);
}


void
test_set (void)
{
	char   **env = NULL, **ret;
	size_t   len = 0;

	TEST_FUNCTION ("environ_set");

	/* Check that an environment variable can be set from a format
	 * string.
	 */
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			len = 0;
			env = nih_str_array_new (NULL);
		}

		ret = environ_set (&env, NULL, &len, TRUE, "FOO=%d", 1234);

		if (test_alloc_failed) {
			TEST_EQ_P (ret, NULL);

			TEST_EQ (len, 0);
			TEST_EQ_P (env[0], NULL);

			nih_free (env);
			continue;
		}

		TEST_NE_P (ret, NULL);

		TEST_EQ (len, 1);
		TEST_ALLOC_PARENT (env[0], env);
		TEST_ALLOC_SIZE (env[0], 9);
		TEST_EQ_STR (env[0], "FOO=1234");
		TEST_EQ_P (env[1], NULL);

		nih_free (env);
	}
}


void
test_lookup (void)
{
	char        **env = NULL;
	size_t        len = 0;
	char * const *ret;

	TEST_FUNCTION ("environ_lookup");

	len = 0;
	env = nih_str_array_new (NULL);


	/* Check that an empty table always returns NULL. */
	TEST_FEATURE ("with empty table");
	ret = environ_lookup (env, "FOO", 3);

	TEST_EQ_P (ret, NULL);


	assert (nih_str_array_add (&env, NULL, &len, "FOOLISH=no"));
	assert (nih_str_array_add (&env, NULL, &len, "BAR=BAZ"));


	/* Check that a key that is present is returned. */
	TEST_FEATURE ("with key to be found");
	ret = environ_lookup (env, "BAR", 3);

	TEST_EQ_P (ret, &env[1]);


	/* Check that a key that doesn't exist returns NULL. */
	TEST_FEATURE ("with key not found");
	ret = environ_lookup (env, "MEEP", 4);

	TEST_EQ_P (ret, NULL);


	/* Check that the key is not prefix-matched. */
	TEST_FEATURE ("with key that is prefix of another");
	ret = environ_lookup (env, "FOO", 3);

	TEST_EQ_P (ret, NULL);


	/* Check that the length is honoured. */
	TEST_FEATURE ("with longer key");
	ret = environ_lookup (env, "FOOLISH", 3);

	TEST_EQ_P (ret, NULL);


	nih_free (env);
}

void
test_get (void)
{
	char       **env = NULL;
	size_t       len = 0;
	const char  *ret;

	TEST_FUNCTION ("environ_get");

	len = 0;
	env = nih_str_array_new (NULL);


	/* Check that an empty table always returns NULL. */
	TEST_FEATURE ("with empty table");
	ret = environ_get (env, "FOO");

	TEST_EQ_P (ret, NULL);


	assert (nih_str_array_add (&env, NULL, &len, "FOOLISH=no"));
	assert (nih_str_array_add (&env, NULL, &len, "BAR=BAZ"));


	/* Check that a key that is present is returned. */
	TEST_FEATURE ("with key to be found");
	ret = environ_get (env, "BAR");

	TEST_EQ_STR (ret, "BAZ");


	/* Check that a key that doesn't exist returns NULL. */
	TEST_FEATURE ("with key not found");
	ret = environ_get (env, "MEEP");

	TEST_EQ_P (ret, NULL);


	/* Check that the key is not prefix-matched. */
	TEST_FEATURE ("with key that is prefix of another");
	ret = environ_get (env, "FOO");

	TEST_EQ_P (ret, NULL);


	nih_free (env);
}

void
test_getn (void)
{
	char       **env = NULL;
	size_t       len = 0;
	const char  *ret;

	TEST_FUNCTION ("environ_getn");

	len = 0;
	env = nih_str_array_new (NULL);


	/* Check that an empty table always returns NULL. */
	TEST_FEATURE ("with empty table");
	ret = environ_getn (env, "FOO", 3);

	TEST_EQ_P (ret, NULL);


	assert (nih_str_array_add (&env, NULL, &len, "FOOLISH=no"));
	assert (nih_str_array_add (&env, NULL, &len, "BAR=BAZ"));


	/* Check that a key that is present is returned. */
	TEST_FEATURE ("with key to be found");
	ret = environ_getn (env, "BAR", 3);

	TEST_EQ_STR (ret, "BAZ");


	/* Check that a key that doesn't exist returns NULL. */
	TEST_FEATURE ("with key not found");
	ret = environ_getn (env, "MEEP", 4);

	TEST_EQ_P (ret, NULL);


	/* Check that the key is not prefix-matched. */
	TEST_FEATURE ("with key that is prefix of another");
	ret = environ_getn (env, "FOO", 3);

	TEST_EQ_P (ret, NULL);


	/* Check that the length is honoured. */
	TEST_FEATURE ("with longer key");
	ret = environ_getn (env, "FOOLISH", 3);

	TEST_EQ_P (ret, NULL);


	nih_free (env);
}


void
test_valid (void)
{
	int valid;

	TEST_FUNCTION ("environ_valid");

	/* Check that an all-uppercase key is valid. */
	TEST_FEATURE ("with uppercase key");
	valid = environ_valid ("FOO", 3);

	TEST_TRUE (valid);


	/* Check that an all-lowercase key is valid. */
	TEST_FEATURE ("with lowercase key");
	valid = environ_valid ("foo", 3);

	TEST_TRUE (valid);


	/* Check that an all-alphanumeric key is valid. */
	TEST_FEATURE ("with alphanumeric key");
	valid = environ_valid ("Foo45", 5);

	TEST_TRUE (valid);


	/* Check that an underscores in the key are valid. */
	TEST_FEATURE ("with underscores in key");
	valid = environ_valid ("FOO_45", 6);

	TEST_TRUE (valid);


	/* Check that a key may begin with an underscore. */
	TEST_FEATURE ("with initial underscore");
	valid = environ_valid ("_FOO", 4);

	TEST_TRUE (valid);


	/* Check that a key may not begin with a number. */
	TEST_FEATURE ("with initial number");
	valid = environ_valid ("9FOO", 4);

	TEST_FALSE (valid);


	/* Check that a key may not begin with any other character. */
	TEST_FEATURE ("with initial dash");
	valid = environ_valid ("-FOO", 4);

	TEST_FALSE (valid);


	/* Check that a key may not contain dashes. */
	TEST_FEATURE ("with dash");
	valid = environ_valid ("FOO-BAR", 7);

	TEST_FALSE (valid);


	/* Check that a key may not contain spaces. */
	TEST_FEATURE ("with space");
	valid = environ_valid ("FOO BAR", 7);

	TEST_FALSE (valid);


	/* Check that the length is honoured. */
	TEST_FEATURE ("with longer string then key");
	valid = environ_valid ("FOO BAR", 3);

	TEST_TRUE (valid);
}

void
test_all_valid (void)
{
	char **env;
	int    valid;

	TEST_FUNCTION ("environ_all_valid");

	/* Check that a valid environment table returns TRUE. */
	TEST_FEATURE ("with valid table");
	env = nih_str_array_new (NULL);
	assert (nih_str_array_add (&env, NULL, NULL, "FOO=BAR"));
	assert (nih_str_array_add (&env, NULL, NULL, "BAR=BAZ"));

	valid = environ_all_valid (env);

	TEST_TRUE (valid);

	nih_free (env);


	/* Check that an empty environment table is valid. */
	TEST_FEATURE ("with empty table");
	env = nih_str_array_new (NULL);

	valid = environ_all_valid (env);

	TEST_TRUE (valid);

	nih_free (env);


	/* Check that an entry without an equals means the table is not
	 * valid.
	 */
	TEST_FEATURE ("with missing equals");
	env = nih_str_array_new (NULL);
	assert (nih_str_array_add (&env, NULL, NULL, "FOO=BAR"));
	assert (nih_str_array_add (&env, NULL, NULL, "BAR"));
	assert (nih_str_array_add (&env, NULL, NULL, "WIBBLE=woo"));

	valid = environ_all_valid (env);

	TEST_FALSE (valid);

	nih_free (env);


	/* Check that an entry with an invalid key name means the table
	 * is also not valid.
	 */
	TEST_FEATURE ("with invalid key");
	env = nih_str_array_new (NULL);
	assert (nih_str_array_add (&env, NULL, NULL, "FOO=BAR"));
	assert (nih_str_array_add (&env, NULL, NULL, "BAR BEE=FOO"));
	assert (nih_str_array_add (&env, NULL, NULL, "WIBBLE=woo"));

	valid = environ_all_valid (env);

	TEST_FALSE (valid);

	nih_free (env);
}


void
test_expand (void)
{
	NihError *error;
	char     *env[7], *str;

	TEST_FUNCTION ("environ_expand");
	env[0] = "FOO=frodo";
	env[1] = "BAR=bilbo";
	env[2] = "BAZ=xx";
	env[3] = "HOBBIT=FOO";
	env[4] = "NULL=";
	env[5] = "DOH=oops";
	env[6] = NULL;

	nih_error_push_context();
	nih_error_pop_context ();


	/* Check that we can expand a string containing no expansion.
	 */
	TEST_FEATURE ("with no expansion");
	TEST_ALLOC_FAIL {
		str = environ_expand (NULL, "this is a test", env);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);
			continue;
		}

		TEST_EQ_STR (str, "this is a test");

		nih_free (str);
	}


	/* Check that we can expand a simple string containing a reference
	 * from the environment, with the reference replaced by the environment
	 * variable value.
	 */
	TEST_FEATURE ("with simple expansion");
	TEST_ALLOC_FAIL {
		str = environ_expand (NULL, "this is a $FOO test", env);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);
			continue;
		}

		TEST_EQ_STR (str, "this is a frodo test");

		nih_free (str);
	}


	/* Check that we can expand a simple string containing a reference
	 * from the environment that is smaller than the reference, with the
	 * reference replaced by the environment variable value.
	 */
	TEST_FEATURE ("with simple expansion of smaller value");
	TEST_ALLOC_FAIL {
		str = environ_expand (NULL, "this is a $BAZ test", env);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);
			continue;
		}

		TEST_EQ_STR (str, "this is a xx test");

		nih_free (str);
	}


	/* Check that we can expand a simple string containing a reference
	 * from the environment that is exactly the same size as the
	 * reference, with the reference replaced by the environment variable
	 * value.
	 */
	TEST_FEATURE ("with simple expansion of same size value");
	TEST_ALLOC_FAIL {
		str = environ_expand (NULL, "this is a $DOH test", env);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);
			continue;
		}

		TEST_EQ_STR (str, "this is a oops test");

		nih_free (str);
	}


	/* Check that we can expand a string containing multiple simple
	 * references, with each replaced by the variable value.
	 */
	TEST_FEATURE ("with multiple simple expansions");
	TEST_ALLOC_FAIL {
		str = environ_expand (NULL, "test $FOO $BAR$BAZ", env);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);
			continue;
		}

		TEST_EQ_STR (str, "test frodo bilboxx");

		nih_free (str);
	}


	/* Check that we can expand a string containing a bracketed
	 * reference, allowing it to nestle against other alphanumerics.
	 */
	TEST_FEATURE ("with simple bracketed expression");
	TEST_ALLOC_FAIL {
		str = environ_expand (NULL, "${BAR}test", env);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);
			continue;
		}

		TEST_EQ_STR (str, "bilbotest");

		nih_free (str);
	}


	/* Check that we can expand a string containing multiple bracketed
	 * references, allowing it to nestle against other alphanumerics.
	 */
	TEST_FEATURE ("with multiple simple bracketed expression");
	TEST_ALLOC_FAIL {
		str = environ_expand (NULL, "${BAR}${FOO}test${BAZ}", env);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);
			continue;
		}

		TEST_EQ_STR (str, "bilbofrodotestxx");

		nih_free (str);
	}


	/* Check that simple expressions may appear within bracketed
	 * expressions, causing them to be evaluted and the evalution
	 * serving as the reference.
	 */
	TEST_FEATURE ("with simple expression inside bracketed expression");
	TEST_ALLOC_FAIL {
		str = environ_expand (NULL, "${$HOBBIT} baggins", env);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);
			continue;
		}

		TEST_EQ_STR (str, "frodo baggins");

		nih_free (str);
	}


	/* Check that bracketed expressions may appear within bracketed
	 * expressions.
	 */
	TEST_FEATURE ("with bracketed expression inside bracketed expression");
	TEST_ALLOC_FAIL {
		str = environ_expand (NULL, "${${HOBBIT}} baggins", env);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);
			continue;
		}

		TEST_EQ_STR (str, "frodo baggins");

		nih_free (str);
	}


	/* Check that we can substitute a default value if the variable
	 * we were after was unset.
	 */
	TEST_FEATURE ("with bracketed default expression");
	TEST_ALLOC_FAIL {
		str = environ_expand (NULL, "${MEEP-a }test", env);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);
			continue;
		}

		TEST_EQ_STR (str, "a test");

		nih_free (str);
	}


	/* Check that a default expression uses the environment value if
	 * it is actually set.
	 */
	TEST_FEATURE ("with bracketed default expression for set variable");
	TEST_ALLOC_FAIL {
		str = environ_expand (NULL, "${BAZ-a }test", env);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);
			continue;
		}

		TEST_EQ_STR (str, "xxtest");

		nih_free (str);
	}


	/* Check that a default expression uses the environment value if
	 * it is actually set, even if it is NULL.
	 */
	TEST_FEATURE ("with bracketed default expression for null variable");
	TEST_ALLOC_FAIL {
		str = environ_expand (NULL, "${NULL-a }test", env);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);
			continue;
		}

		TEST_EQ_STR (str, "test");

		nih_free (str);
	}


	/* Check that we can substitute a default value if the variable
	 * we were after was unset (or null).
	 */
	TEST_FEATURE ("with bracketed default or null expression");
	TEST_ALLOC_FAIL {
		str = environ_expand (NULL, "${MEEP:-a }test", env);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);
			continue;
		}

		TEST_EQ_STR (str, "a test");

		nih_free (str);
	}


	/* Check that a default or null expression uses the environment value
	 * if it is actually set and not null.
	 */
	TEST_FEATURE ("with bracketed default or null expression for set variable");
	TEST_ALLOC_FAIL {
		str = environ_expand (NULL, "${BAZ:-a }test", env);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);
			continue;
		}

		TEST_EQ_STR (str, "xxtest");

		nih_free (str);
	}


	/* Check that we can substitute a default value if the variable
	 * we were after was null.
	 */
	TEST_FEATURE ("with bracketed default or null expression for null variable");
	TEST_ALLOC_FAIL {
		str = environ_expand (NULL, "${NULL:-a }test", env);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);
			continue;
		}

		TEST_EQ_STR (str, "a test");

		nih_free (str);
	}


	/* Check that we don't substitute an alternate value if the
	 * variable we were after was unset.
	 */
	TEST_FEATURE ("with bracketed alternate expression");
	TEST_ALLOC_FAIL {
		str = environ_expand (NULL, "${MEEP+good }test", env);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);
			continue;
		}

		TEST_EQ_STR (str, "test");

		nih_free (str);
	}


	/* Check that we use the alternate value if the environment variable
	 * is actually set.
	 */
	TEST_FEATURE ("with bracketed alternate expression for set variable");
	TEST_ALLOC_FAIL {
		str = environ_expand (NULL, "${BAZ+good }test", env);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);
			continue;
		}

		TEST_EQ_STR (str, "good test");

		nih_free (str);
	}


	/* Check that we use the alternate value if the environment variable
	 * is set, even if it is NULL.
	 */
	TEST_FEATURE ("with bracketed alternate expression for null variable");
	TEST_ALLOC_FAIL {
		str = environ_expand (NULL, "${NULL+good }test", env);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);
			continue;
		}

		TEST_EQ_STR (str, "good test");

		nih_free (str);
	}


	/* Check that we don't substitute an alternate value if the
	 * variable we were after was unset (or null).
	 */
	TEST_FEATURE ("with bracketed alternate or null expression");
	TEST_ALLOC_FAIL {
		str = environ_expand (NULL, "${MEEP:+good }test", env);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);
			continue;
		}

		TEST_EQ_STR (str, "test");

		nih_free (str);
	}


	/* Check that we use the alternate value if the environment variable
	 * is actually set and not null.
	 */
	TEST_FEATURE ("with bracketed alternate or null expression for set variable");
	TEST_ALLOC_FAIL {
		str = environ_expand (NULL, "${BAZ:+good }test", env);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);
			continue;
		}

		TEST_EQ_STR (str, "good test");

		nih_free (str);
	}


	/* Check that we don't substitute an alternate value if the
	 * variable we were after was set, but was null.
	 */
	TEST_FEATURE ("with bracketed alternate or null expression for null variable");
	TEST_ALLOC_FAIL {
		str = environ_expand (NULL, "${NULL:+good }test", env);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);
			continue;
		}

		TEST_EQ_STR (str, "test");

		nih_free (str);
	}


	/* Check that references on either side of an expression are
	 * expanded before evaluation.
	 */
	TEST_FEATURE ("with references in bracketed expression argument");
	TEST_ALLOC_FAIL {
		str = environ_expand (NULL, "${$BAZ:-${$HOBBIT}}test", env);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);
			continue;
		}

		TEST_EQ_STR (str, "frodotest");

		nih_free (str);
	}


	/* Check that a literal dollar sign with no following text is
	 * treated just as a dollar sign.
	 */
	TEST_FEATURE ("with dollar sign in whitespace");
	TEST_ALLOC_FAIL {
		str = environ_expand (NULL, "this is a $ test", env);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);
			continue;
		}

		TEST_EQ_STR (str, "this is a $ test");

		nih_free (str);
	}


	/* Check that a literal dollar sign in text can be followed by empty
	 * brackets to be just as a dollar sign.
	 */
	TEST_FEATURE ("with bracketed dollar sign");
	TEST_ALLOC_FAIL {
		str = environ_expand (NULL, "${}test", env);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);
			continue;
		}

		TEST_EQ_STR (str, "$test");

		nih_free (str);
	}


	/* Check that attempting to expand an unknown variable results in
	 * an error being raised.
	 */
	TEST_FEATURE ("with simple expansion of unknown variable");
	str = environ_expand (NULL, "this is a $WIBBLE test", env);

	TEST_EQ_P (str, NULL);

	error = nih_error_get ();
	TEST_EQ (error->number, ENVIRON_UNKNOWN_PARAM);
	nih_free (error);


	/* Check that attempting to expand an unknown variable results in
	 * an error being raised.
	 */
	TEST_FEATURE ("with bracketed expansion of unknown variable");
	str = environ_expand (NULL, "this is a ${WIBBLE} test", env);

	TEST_EQ_P (str, NULL);

	error = nih_error_get ();
	TEST_EQ (error->number, ENVIRON_UNKNOWN_PARAM);
	nih_free (error);


	/* Check that attempting to expand an unknown variable results in
	 * an error being raised.
	 */
	TEST_FEATURE ("with expansion of unknown variable within expression name");
	str = environ_expand (NULL, "this is a ${$WIBBLE:-$FOO} test", env);

	TEST_EQ_P (str, NULL);

	error = nih_error_get ();
	TEST_EQ (error->number, ENVIRON_UNKNOWN_PARAM);
	nih_free (error);


	/* Check that attempting to expand an unknown variable results in
	 * an error being raised.
	 */
	TEST_FEATURE ("with expansion of unknown variable within expression argument");
	str = environ_expand (NULL, "this is a ${$FOO:-$WIBBLE} test", env);

	TEST_EQ_P (str, NULL);

	error = nih_error_get ();
	TEST_EQ (error->number, ENVIRON_UNKNOWN_PARAM);
	nih_free (error);


	/* Check that attempting to expand an illegal variable name results in
	 * an error being raised.
	 */
	TEST_FEATURE ("with expansion of illegal variable");
	str = environ_expand (NULL, "this is a ${WIB WOB} test", env);

	TEST_EQ_P (str, NULL);

	error = nih_error_get ();
	TEST_EQ (error->number, ENVIRON_ILLEGAL_PARAM);
	nih_free (error);


	/* Check that inventing a new operator results in an error
	 * being raised.
	 */
	TEST_FEATURE ("with unknown operator in expression");
	str = environ_expand (NULL, "this is a ${$FOO:!$BAR test", env);

	TEST_EQ_P (str, NULL);

	error = nih_error_get ();
	TEST_EQ (error->number, ENVIRON_EXPECTED_OPERATOR);
	nih_free (error);


	/* Check that forgetting to close a brace results in an error
	 * being raised.
	 */
	TEST_FEATURE ("with missing close brace after expression");
	str = environ_expand (NULL, "this is a ${$FOO:-$BAR test", env);

	TEST_EQ_P (str, NULL);

	error = nih_error_get ();
	TEST_EQ (error->number, ENVIRON_MISMATCHED_BRACES);
	nih_free (error);
}


int
main (int   argc,
      char *argv[])
{
	test_add ();
	test_append ();
	test_set ();
	test_lookup ();
	test_get ();
	test_getn ();
	test_valid ();
	test_all_valid ();
	test_expand ();

	return 0;
}
