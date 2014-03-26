/* upstart
 *
 * test_cgroup.c - test suite for init/cgroup.c
 *
 * Copyright  2013 Canonical Ltd.
 * Author: James Hunt <james.hunt@canonical.com>.
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

#include <nih/string.h>
#include <nih/test.h>

#include "cgroup.h"

#include "test_util_common.h"

extern NihHash *cgroup_paths;

void
test_cgroup_new (void)
{
	nih_local char *parent = NULL;
	CGroup         *cgroup;

	TEST_FUNCTION ("cgroup_new");

	cgroup_init ();
	parent = nih_strdup (NULL, "a parent object");
	TEST_NE_P (parent, NULL);

	TEST_FEATURE ("no parent, controller");
	TEST_ALLOC_FAIL {

		cgroup = cgroup_new (NULL, "cpuset");

		if (test_alloc_failed) {
			TEST_EQ_P (cgroup, NULL);
			continue;
		}

		TEST_NE_P (cgroup, NULL);

		TEST_ALLOC_SIZE (cgroup, sizeof (CGroup));

		TEST_ALLOC_PARENT (cgroup, NULL);

		TEST_EQ_STR (cgroup->controller, "cpuset");
		TEST_ALLOC_SIZE (cgroup->controller, 1+strlen ("cpuset"));
		TEST_ALLOC_PARENT (cgroup->controller, cgroup);

		TEST_LIST_EMPTY (&cgroup->names);

		nih_free (cgroup);
	}

	TEST_FEATURE ("parent, controller");
	TEST_ALLOC_FAIL {

		cgroup = cgroup_new (parent, "perf_event");

		if (test_alloc_failed) {
			TEST_EQ_P (cgroup, NULL);
			continue;
		}

		TEST_NE_P (cgroup, NULL);

		TEST_ALLOC_SIZE (cgroup, sizeof (CGroup));

		TEST_ALLOC_PARENT (cgroup, parent);

		TEST_EQ_STR (cgroup->controller, "perf_event");
		TEST_ALLOC_SIZE (cgroup->controller, 1+strlen ("perf_event"));
		TEST_ALLOC_PARENT (cgroup->controller, cgroup);

		TEST_LIST_EMPTY (&cgroup->names);

		nih_free (cgroup);
	}
}

void
test_cgroup_name_new (void)
{
	CGroupName     *cgname;
	nih_local char *parent = NULL;

	TEST_FUNCTION ("cgroup_name_new");

	cgroup_init ();
	parent = nih_strdup (NULL, "a parent object");
	TEST_NE_P (parent, NULL);

	TEST_FEATURE ("no parent, name");

	TEST_ALLOC_FAIL {
		cgname = cgroup_name_new (NULL, "foo.");

		if (test_alloc_failed) {
			TEST_EQ_P (cgname, NULL);
			continue;
		}

		TEST_NE_P (cgname, NULL);

		TEST_ALLOC_SIZE (cgname, sizeof (CGroupName));

		TEST_ALLOC_PARENT (cgname, NULL);

		TEST_EQ_STR (cgname->name, "foo.");
		TEST_ALLOC_SIZE (cgname->name, 1+strlen ("foo."));
		TEST_ALLOC_PARENT (cgname->name, cgname);

		TEST_LIST_EMPTY (&cgname->settings);
	}

	TEST_FEATURE ("parent, name");

	TEST_ALLOC_FAIL {
		cgname = cgroup_name_new (parent, "bar");

		if (test_alloc_failed) {
			TEST_EQ_P (cgname, NULL);
			continue;
		}

		TEST_NE_P (cgname, NULL);

		TEST_ALLOC_SIZE (cgname, sizeof (CGroupName));

		TEST_ALLOC_PARENT (cgname, parent);

		TEST_EQ_STR (cgname->name, "bar");
		TEST_ALLOC_SIZE (cgname->name, 1+strlen ("bar"));
		TEST_ALLOC_PARENT (cgname->name, cgname);

		TEST_LIST_EMPTY (&cgname->settings);
	}
}

void
test_cgroup_setting_new (void)
{
	CGroupSetting   *setting;
	nih_local char  *parent= NULL;

	cgroup_init ();

	parent = nih_strdup (NULL, "a parent object");
	TEST_NE_P (parent, NULL);

	TEST_FUNCTION ("cgroup_setting_new");

	TEST_FEATURE ("no parent, key, no value");
	TEST_ALLOC_FAIL {
		setting = cgroup_setting_new (NULL, "foo", NULL);

		if (test_alloc_failed) {
			TEST_EQ_P (setting, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (setting, sizeof (CGroupSetting));
		TEST_ALLOC_PARENT (setting, NULL);
		TEST_EQ_STR (setting->key, "foo");
		TEST_ALLOC_SIZE (setting->key, 1+strlen ("foo"));
		TEST_ALLOC_PARENT (setting->key, setting);

		nih_free (setting);
	}

	TEST_FEATURE ("parent, key, no value");
	TEST_ALLOC_FAIL {
		setting = cgroup_setting_new (parent, "hello world", NULL);

		if (test_alloc_failed) {
			TEST_EQ_P (setting, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (setting, sizeof (CGroupSetting));
		TEST_ALLOC_PARENT (setting, parent);
		TEST_EQ_STR (setting->key, "hello world");
		TEST_ALLOC_SIZE (setting->key, 1+strlen ("hello world"));
		TEST_ALLOC_PARENT (setting->key, setting);

		nih_free (setting);
	}

	TEST_FEATURE ("no parent, key, value");
	TEST_ALLOC_FAIL {
		setting = cgroup_setting_new (NULL, "hello world", "a value");

		if (test_alloc_failed) {
			TEST_EQ_P (setting, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (setting, sizeof (CGroupSetting));
		TEST_ALLOC_PARENT (setting, NULL);

		TEST_EQ_STR (setting->key, "hello world");
		TEST_ALLOC_SIZE (setting->key, 1+strlen ("hello world"));
		TEST_ALLOC_PARENT (setting->key, setting);

		TEST_EQ_STR (setting->value, "a value");
		TEST_ALLOC_SIZE (setting->value, 1+strlen ("a value"));
		TEST_ALLOC_PARENT (setting->value, setting);

		nih_free (setting);
	}

	TEST_FEATURE ("parent, key, value");
	TEST_ALLOC_FAIL {
		setting = cgroup_setting_new (parent, "hello world", "a value");

		if (test_alloc_failed) {
			TEST_EQ_P (setting, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (setting, sizeof (CGroupSetting));
		TEST_ALLOC_PARENT (setting, parent);

		TEST_EQ_STR (setting->key, "hello world");
		TEST_ALLOC_SIZE (setting->key, 1+strlen ("hello world"));
		TEST_ALLOC_PARENT (setting->key, setting);

		TEST_EQ_STR (setting->value, "a value");
		TEST_ALLOC_SIZE (setting->value, 1+strlen ("a value"));
		TEST_ALLOC_PARENT (setting->value, setting);

		nih_free (setting);
	}
}

void
test_path_new (void)
{
	char           *path = "foo/bar";
	CGroupPath     *cgpath;
	nih_local char *parent= NULL;

	TEST_FUNCTION ("cgroup_path_new");

	cgroup_init ();

	parent = nih_strdup (NULL, "a parent object");
	TEST_NE_P (parent, NULL);

	TEST_FEATURE ("no parent, path");

	TEST_ALLOC_FAIL {
		TEST_HASH_EMPTY (cgroup_paths);

		cgpath = cgroup_path_new (NULL, path);

		if (test_alloc_failed) {
			TEST_EQ_P (cgpath, NULL);
			continue;
		}

		TEST_NE_P (cgpath, NULL);

		TEST_ALLOC_SIZE (cgpath, sizeof (CGroupPath));

		TEST_ALLOC_PARENT (cgpath, NULL);
		TEST_EQ_STR (cgpath->path, path);
		TEST_ALLOC_SIZE (cgpath->path, 1+strlen (path));
		TEST_ALLOC_PARENT (cgpath->path, cgpath);
		TEST_EQ (cgpath->blockers, 1);

		TEST_HASH_NOT_EMPTY (cgroup_paths);

		nih_free (cgpath);
	}

	TEST_FEATURE ("parent and path");

	TEST_ALLOC_FAIL {
		TEST_HASH_EMPTY (cgroup_paths);

		cgpath = cgroup_path_new (parent, path);

		if (test_alloc_failed) {
			TEST_EQ_P (cgpath, NULL);
			continue;
		}

		TEST_NE_P (cgpath, NULL);

		TEST_ALLOC_SIZE (cgpath, sizeof (CGroupPath));

		TEST_ALLOC_PARENT (cgpath, parent);
		TEST_EQ_STR (cgpath->path, path);
		TEST_ALLOC_SIZE (cgpath->path, 1+strlen (path));
		TEST_ALLOC_PARENT (cgpath->path, cgpath);
		TEST_EQ (cgpath->blockers, 1);

		TEST_HASH_NOT_EMPTY (cgroup_paths);

		nih_free (cgpath);
	}
}

int
main (int   argc,
      char *argv[])
{
	test_cgroup_new ();
	test_cgroup_name_new ();
	test_cgroup_setting_new ();
	test_path_new ();

	return 0;
}
