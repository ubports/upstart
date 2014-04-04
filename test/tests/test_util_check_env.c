/* upstart
 *
 * test_util_check_env.c - meta test to ensure environment sane for
 * running tests.
 *
 * Copyright © 2013 Canonical Ltd.
 * Author: James Hunt <james.hunt@canonical.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <limits.h>
#include <unistd.h>
#include <mntent.h>
#include <sys/vfs.h>

#include <nih/string.h>
#include <nih/test.h>
#include <nih/logging.h>

#include "test_util_common.h"

#ifndef OVERLAYFS_SUPER_MAGIC
#define OVERLAYFS_SUPER_MAGIC 0x794c764f
#endif

/**
 * check_for_overlayfs:
 *
 * Determine if the mount point used by the tests for creating temporary
 * files is using overlayfs.
 *
 * Returns: TRUE if temporary work area is on overlayfs, else FALSE.
 **/
int
check_for_overlayfs (void)
{
	struct statfs  statbuf;
	char           path[PATH_MAX];
	int            found = FALSE;

	/* Create a file in the temporary work area */
	TEST_FILENAME (path);
	fclose (fopen (path, "w"));

	/* Check it exits */
	assert0 (statfs (path, &statbuf));

	if (statbuf.f_type == OVERLAYFS_SUPER_MAGIC) {
		nih_warn ("Mountpoint for '%s' (needed by the Upstart tests) is an overlayfs "
				"filesystem, which does not support inotify.",
				path);
		found = TRUE;
	}

	assert0 (unlink (path));

	return found;
}

void print_my_cgroup(void)
{
	char *str;
	str = get_pid_cgroup("freezer", getpid());
	if (str) {
		nih_warn("I am in freezer cgroup: %s", str);
		TEST_EQ_STR(str, "/");
		nih_free(str);
	} else {
		TEST_FAILED("Failed to get my freezer cgroup");
	}
}

char *get_my_cgroup()
{
	char line[1024], *ret = NULL;
	FILE *f = fopen("/proc/self/cgroup", "r");

	while (fgets(line, 1024, f)) {
		char *p, *p2;
		if ((p = strchr(line, ':')) == NULL)
			continue;
		p++;
		if ((p2 = strchr(p, ':')) == NULL)
			continue;
		if (strncmp(p, "name=", 5) == 0)
			continue;
		ret = NIH_MUST( nih_strdup(NULL, p2+1) );
		break;
	}
	fclose(f);
	return ret;
}

int check_cgroup_sandbox(void)
{
	char *cg_prev = NULL, *cg_post = NULL;
	int ret = -1;

	cg_prev = get_my_cgroup();
	if (!cg_prev)
		return -1;
	if (setup_cgroup_sandbox() < 0) {
		nih_free(cg_prev);
		return -1;
	}
	cg_post = get_my_cgroup();
	if (!cg_post) {
		nih_free(cg_prev);
		return -1;
	}
	/* we should have moved cgroups, so the two should be different */
	if (strcmp(cg_prev, cg_post) != 0) {
		nih_warn("setup_cgroup_sandbox moved me from %s to %s",
				cg_prev, cg_post);
		ret = 0;
	}
	nih_free(cg_prev);
	nih_free(cg_post);
	return ret;
}

/**
 * test_checks:
 *
 * Perform any checks necessary before real tests are run.
 **/
void
test_checks (void)
{
	int ret;

	TEST_GROUP ("test environment");

	/*
	 * Warn (*) if overlayfs detected.
	 *
	 * (*) - Don't fail in the hope that one day someone might fix
	 * overlayfs.
	 */
	TEST_FEATURE ("checking for overlayfs");
	if (check_for_overlayfs ()) {
		nih_warn ("Found overlayfs mounts");
		nih_warn ("This environment will probably cause tests to fail mysteriously!!");
		nih_warn ("See bug LP:#882147 for further details.");
	}

	TEST_FEATURE ("checking for cgmanager");
	ret = connect_to_cgmanager();
	switch(ret) {
	case -2: TEST_FAILED("Found no cgroup manager"); break;
	case -1: TEST_FAILED("Error connecting to cgmanager"); break;
	case 0: print_my_cgroup(); break;
	default: TEST_FAILED("Unknown error from connect_to_cgmanager: %d", ret);
	}

	TEST_FEATURE("cgroup sandbox");
	TEST_EQ(check_cgroup_sandbox(), 0);
	disconnect_cgmanager();
}

int
main (int   argc,
      char *argv[])
{
	test_checks ();

	return 0;
}
