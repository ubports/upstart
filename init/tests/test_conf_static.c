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

	TEST_FUNCTION ("toggle_conf_name");
	TEST_FEATURE ("changing conf to override");

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

void
test_conf_to_job_name (void)
{
	char dirname[PATH_MAX];
	char         *filename;
	char             *name;

	TEST_FUNCTION ("conf_to_job_name");
	TEST_FEATURE ("with .conf file");
	TEST_FILENAME (dirname);
	filename = nih_sprintf (NULL, "%s/foo.conf", dirname);
	name = conf_to_job_name (dirname, filename);
	TEST_EQ_STR (name, "foo");
	nih_free (filename);
	nih_free (name);
	
	TEST_FEATURE ("with .override file");
	filename = nih_sprintf (NULL, "%s/foo.override", dirname);
	name = conf_to_job_name (dirname, filename);
	TEST_EQ_STR (name, "foo");
	nih_free (filename);
	nih_free (name);

	TEST_FEATURE ("with .conf in a sub-directory");
	filename = nih_sprintf (NULL, "%s/foo/bar.conf", dirname);
	name = conf_to_job_name (dirname, filename);
	TEST_EQ_STR (name, "foo/bar");
	nih_free (filename);
	nih_free (name);
	
	TEST_FEATURE ("without extension");
	filename = nih_sprintf (NULL, "%s/foo", dirname);
	name = conf_to_job_name (dirname, filename);
	TEST_EQ_STR (name, "foo");
	nih_free (filename);
	nih_free (name);

}

void
test_conf_get_best_override (void)
{
	const char *sources[] = {"peter", "paul", "mary", NULL};
	FILE                *f;
	char dirname[PATH_MAX];
	char              *dir;
	char         *expected;
	char             *path;

	TEST_FUNCTION ("conf_get_best_override");

	TEST_FILENAME (dirname);
	mkdir (dirname, 0755);
	
	for (const char **src = sources; *src; src++) {
		dir = nih_sprintf (NULL, "%s/%s", dirname, *src);
		TEST_EQ (mkdir (dir, 0755), 0);
		NIH_MUST (conf_source_new (NULL, dir, CONF_JOB_DIR));
		nih_free (dir);
	}

	TEST_FEATURE ("with no overrides");
	NIH_LIST_FOREACH (conf_sources, iter) {
		ConfSource *source = (ConfSource *)iter;
		path = conf_get_best_override ("foo", source);
		TEST_EQ_P (path, NULL);
	}

	TEST_FEATURE ("with single highest priority override");
	expected = nih_sprintf (NULL, "%s/%s/foo.override", dirname, sources[0]);
	f = fopen (expected, "w");
	TEST_NE_P (f, NULL);
	TEST_EQ (fclose (f), 0);

	NIH_LIST_FOREACH (conf_sources, iter) {
		ConfSource *source = (ConfSource *)iter;
		path = conf_get_best_override ("foo", source);
		TEST_EQ_STR (path, expected);
		nih_free (path);
	}	
	TEST_EQ (unlink (expected), 0);

	TEST_FEATURE ("with single middle priority override");
	expected = nih_sprintf (NULL, "%s/%s/foo.override", dirname, sources[1]);
	f = fopen (expected, "w");
	TEST_NE_P (f, NULL);
	TEST_EQ (fclose (f), 0);

	NIH_LIST_FOREACH (conf_sources, iter) {
		ConfSource *source = (ConfSource *)iter;
		path = conf_get_best_override ("foo", source);

		/* Poor-man's basename(1) */
		dir = conf_to_job_name (dirname, source->path);
		if (strcmp (dir, sources[0]) == 0) {
			TEST_EQ_P (path, NULL);
		} else {
			TEST_EQ_STR (path, expected);
			nih_free (path);
		}
		nih_free (dir);
	}	
	TEST_EQ (unlink (expected), 0);

	TEST_FEATURE ("with single lowest priority override");
	expected = nih_sprintf (NULL, "%s/%s/foo.override", dirname, sources[2]);
	f = fopen (expected, "w");
	TEST_NE_P (f, NULL);
	TEST_EQ (fclose (f), 0);

	NIH_LIST_FOREACH (conf_sources, iter) {
		ConfSource *source = (ConfSource *)iter;
		path = conf_get_best_override ("foo", source);

		/* Poor-man's basename(1) */
		dir = conf_to_job_name (dirname, source->path);
		if (strcmp (dir, sources[2]) == 0) {
			TEST_EQ_STR (path, expected);
			nih_free (path);
		} else {
			TEST_EQ_P (path, NULL);
		}
		nih_free (dir);
	}	
	TEST_EQ (unlink (expected), 0);

	/* Clean up */
	for (const char **src = sources; *src; src++) {
		dir = nih_sprintf (NULL, "%s/%s", dirname, *src);
		TEST_EQ (rmdir (dir), 0);
		nih_free (dir);
	}

	TEST_EQ (rmdir (dirname), 0);

	NIH_LIST_FOREACH_SAFE (conf_sources, iter) {
		ConfSource *source = (ConfSource *)iter;
		nih_free (source);
	}
}

int
main (int   argc,
      char *argv[])
{
	test_toggle_conf_name ();
	test_conf_to_job_name ();
	test_conf_get_best_override ();

	return 0;
}
