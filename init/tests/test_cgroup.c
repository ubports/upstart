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
#include <nih/file.h>
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
test_cgroup_job_start (void)
{
	char             confdir[PATH_MAX];
	char             logdir[PATH_MAX];
	char             flagfile[PATH_MAX];
	nih_local char  *cmd = NULL;
	pid_t            dbus_pid = 0;
	pid_t            upstart_pid = 0;
	char           **output;
	size_t           lines;
	size_t           len;
	nih_local char  *logfile = NULL;
	nih_local char  *logfile_name = NULL;
	nih_local char  *contents = NULL;

	if (geteuid ()) {
		printf ("INFO: skipping %s tests as not running as root\n", __func__);
		fflush (NULL);
		return;
	}

	TEST_GROUP ("cgroup manager handling");

        TEST_FILENAME (confdir);
        TEST_EQ (mkdir (confdir, 0755), 0);

        TEST_FILENAME (logdir);
        TEST_EQ (mkdir (logdir, 0755), 0);

        TEST_FILENAME (flagfile);

	/* Use the "secret" interface */
	TEST_EQ (setenv ("UPSTART_CONFDIR", confdir, 1), 0);
	TEST_EQ (setenv ("UPSTART_LOGDIR", logdir, 1), 0);

	TEST_DBUS (dbus_pid);

	/*******************************************************************/
	TEST_FEATURE ("Ensure startup job does not start until cgmanager available");

	contents = nih_sprintf (NULL, 
			"start on startup\n"
			"\n"
			"cgroup memory mem-%s\n"
			"\n"
			"exec echo hello\n",
			__func__);
	TEST_NE_P (contents, NULL);

	CREATE_FILE (confdir, "cgroup.conf", contents);

	logfile_name = NIH_MUST (nih_sprintf (NULL, "%s/%s",
				logdir,
				"cgroup.log"));

	start_upstart_common (&upstart_pid, FALSE, FALSE, confdir, logdir, NULL);

	cmd = nih_sprintf (NULL, "%s status %s 2>&1", get_initctl (), "cgroup");
	TEST_NE_P (cmd, NULL);
	RUN_COMMAND (NULL, cmd, &output, &lines);
	TEST_EQ (lines, 1);

	/* job should *NOT* start on startup */
	TEST_EQ_STR (output[0], "cgroup stop/waiting");
	nih_free (output);

	TEST_FALSE (file_exists (logfile_name));

	cmd = nih_sprintf (NULL, "%s notify-cgroup-manager-address %s 2>&1",
			get_initctl (),
			CGMANAGER_DBUS_SOCK);
	TEST_NE_P (cmd, NULL);
	RUN_COMMAND (NULL, cmd, &output, &lines);
	TEST_EQ (lines, 0);

	WAIT_FOR_FILE (logfile_name);

	logfile = nih_file_read (NULL, logfile_name, &len);
	TEST_NE_P (logfile, NULL);

	TEST_EQ_STR (logfile, "hello\r\n");

	DELETE_FILE (confdir, "cgroup.conf");
	assert0 (unlink (logfile_name));

	/*******************************************************************/
	TEST_FEATURE ("Ensure bogus cgroups don't crash init");

	contents = nih_sprintf (NULL, 
			"cgroup name\n"
			"\n"
			"exec echo hello\n");
	TEST_NE_P (contents, NULL);

	CREATE_FILE (confdir, "cgroup-name.conf", contents);

	logfile_name = NIH_MUST (nih_sprintf (NULL, "%s/%s",
				logdir,
				"cgroup-name.log"));

	cmd = nih_sprintf (NULL, "%s status %s 2>&1", get_initctl (), "cgroup-name");
	TEST_NE_P (cmd, NULL);
	RUN_COMMAND (NULL, cmd, &output, &lines);
	TEST_EQ (lines, 1);

	/* job is not running yet */
	TEST_EQ_STR (output[0], "cgroup-name stop/waiting");
	nih_free (output);

	TEST_FALSE (file_exists (logfile_name));

	cmd = nih_sprintf (NULL, "%s start %s 2>&1", get_initctl (), "cgroup-name");
	TEST_NE_P (cmd, NULL);
	RUN_COMMAND (NULL, cmd, &output, &lines);
	TEST_EQ (lines, 1);

        TEST_EQ_STR (output[0], "initctl: Job failed to start");

	DELETE_FILE (confdir, "cgroup-name.conf");
	/*******************************************************************/

	STOP_UPSTART (upstart_pid);
	TEST_DBUS_END (dbus_pid);

        TEST_EQ (rmdir (confdir), 0);
        TEST_EQ (rmdir (logdir), 0);

	/*******************************************************************/

}

int
main (int   argc,
      char *argv[])
{
	test_cgroup_new ();
	test_cgroup_name_new ();
	test_cgroup_setting_new ();
	test_cgroup_job_start ();

	return 0;
}
