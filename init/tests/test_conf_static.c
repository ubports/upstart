/* upstart
 *
 * test_conf.c - test suite for init/conf.c
 *
 * Copyright © 2012 Canonical Ltd.
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

#include <limits.h>

#include <nih/test.h>

#include "conf.c"

void
test_toggle_conf_name (void)
{
	char override_ext[] = ".override";
	char dirname[PATH_MAX];
	char filename[PATH_MAX];
	JobClass *job;
	char *f;
	char *p;

	TEST_FUNCTION_FEATURE ("toggle_conf_name",
			"changing conf to override");

	TEST_FILENAME (dirname);
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");
	f = toggle_conf_name (NULL, filename);
	TEST_NE_P (f, NULL);

	p = strstr (f, ".override");
	TEST_NE_P (p, NULL);
	TEST_EQ_P (p, f+strlen (f) - strlen (override_ext));
	nih_free (f);

	TEST_FEATURE ("changing override to conf");
	strcpy (filename, dirname);
	strcat (filename, "/bar.override");
	f = toggle_conf_name (NULL, filename);
	TEST_NE_P (f, NULL);

	p = strstr (f, ".conf");
	TEST_NE_P (p, NULL);
	TEST_EQ_P (p, f+strlen (f) - strlen (".conf"));
	nih_free (f);

	/* test parent param */
	job = job_class_new (NULL, "foo", NULL);
	TEST_NE_P (job, NULL);

	f = toggle_conf_name (job, filename);
	TEST_NE_P (f, NULL);

	TEST_EQ (TRUE, nih_alloc_parent (f, job));

	nih_free (job);
}

int
main (int   argc,
      char *argv[])
{
	test_toggle_conf_name ();

	return 0;
}
