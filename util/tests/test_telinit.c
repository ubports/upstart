/* upstart
 *
 * test_utmp.c - test suite for util/utmp.c
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

#include <nih/test.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/option.h>
#include <nih/main.h>


extern int env_option (NihOption *option, const char *arg);


void
test_env_option (void)
{
	NihOption opt;
	char **   value;
	int       ret;

	TEST_FUNCTION ("env_option");
	opt.value = &value;
	value = NULL;


	/* Check that the env_option function takes the argument as a string
	 * and appends it to the array, allocating it if necessary.
	 */
	TEST_FEATURE ("with first argument");
	ret = env_option (&opt, "FOO=BAR");

	TEST_EQ (ret, 0);
	TEST_ALLOC_SIZE (value, sizeof (char *) * 2);
	TEST_ALLOC_PARENT (value[0], value);
	TEST_EQ_STR (value[0], "FOO=BAR");
	TEST_EQ_P (value[1], NULL);


	/* Check that a repeated environment option is appended to the
	 * array.
	 */
	TEST_FEATURE ("with further argument");
	ret = env_option (&opt, "TEA=YES");

	TEST_EQ (ret, 0);
	TEST_ALLOC_SIZE (value, sizeof (char *) * 3);
	TEST_ALLOC_PARENT (value[0], value);
	TEST_ALLOC_PARENT (value[1], value);
	TEST_EQ_STR (value[0], "FOO=BAR");
	TEST_EQ_STR (value[1], "TEA=YES");
	TEST_EQ_P (value[2], NULL);


	nih_free (value);
}


int
main (int   argc,
      char *argv[])
{
	program_name = "test";

	test_env_option ();

	return 0;
}
