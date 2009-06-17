/* upstart
 *
 * test_parse_conf.c - test suite for init/parse_conf.c
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
#include <nih/list.h>
#include <nih/error.h>
#include <nih/errors.h>

#include "parse_conf.h"
#include "conf.h"
#include "errors.h"


void
test_parse_conf (void)
{
	ConfSource *source;
	ConfFile   *file;
	NihError   *err;
	size_t      pos, lineno;
	char        buf[1024];
	int         ret;

	TEST_FUNCTION ("parse_conf");
	source = conf_source_new (NULL, "/path", CONF_DIR);
	file = conf_file_new (source, "/path/file");

	/* Check that a simple configuration can be parsed.
	 */
	TEST_FEATURE ("with simple file");
	strcpy (buf, "# nothing to test\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		ret = parse_conf (file, buf, strlen (buf), &pos, &lineno);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (ret, 0);
	}

	nih_free (source);
}


int
main (int   argc,
      char *argv[])
{
	test_parse_conf ();

	return 0;
}
