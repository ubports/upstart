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

#include <nih/string.h>
#include <nih/test.h>
#include <nih/logging.h>

#include "test_util_common.h"

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
	struct stat    statbuf;
	char           path[PATH_MAX];
	const char    *fs = "overlayfs";
	unsigned int   maj;
	unsigned int   min;
	const char    *mounts = "/proc/self/mounts";
	FILE          *mtab;
	struct mntent *mnt;
	int            found = FALSE;

	/* Create a file in the temporary work area */
	TEST_FILENAME (path);
	fclose (fopen (path, "w"));

	/* Check it exits */
	assert0 (stat (path, &statbuf));

	/* Extract device details */
	maj = major (statbuf.st_dev);
	min = minor (statbuf.st_dev);

	mtab = fopen (mounts, "r");
	TEST_NE_P (mtab, NULL);

	/* Look through mount table */
	while ((mnt = getmntent (mtab))) {
		unsigned int mount_maj;
		unsigned int mount_min;

		assert0 (stat (mnt->mnt_dir, &statbuf));
		mount_maj = major (statbuf.st_dev);
		mount_min = minor (statbuf.st_dev);

		if (! strcmp (mnt->mnt_type, fs) && mount_maj == maj && mount_min == min) {
			found = TRUE;
			nih_warn ("Mountpoint '%s' (needed by the Upstart tests) is an '%s' "
					"filesystem which does not support inotify.",
					mnt->mnt_dir,
					fs);
			goto out;
		}
	}

out:
	fclose (mtab);
	assert0 (unlink (path));

	return found;
}

/**
 * test_checks:
 *
 * Perform any checks necessary before real tests are run.
 **/
void
test_checks (void)
{
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
}

int
main (int   argc,
      char *argv[])
{
	test_checks ();

	return 0;
}
