/* upstart
 *
 * apparmor.c - handle AppArmor profiles
 *
 * Copyright © 2013 Canonical Ltd.
 * Author: Marc Deslauriers <marc.deslauriers@canonical.com>.
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */

#include <sys/types.h>
#include <sys/stat.h>

#include <stdio.h>
#include <unistd.h>
#include <limits.h>

#include <nih/signal.h>
#include <nih/string.h>

#include "apparmor.h"

/**
 * apparmor_switch:
 * @profile: AppArmor profile to switch to
 *
 * This function switches to a new AppArmor profile on exec
 *
 * Returns: zero on success, -1 on error
 **/
int
apparmor_switch (char *profile)
{
	nih_local char *filename = NULL;
	FILE           *f;

	nih_assert (profile != NULL);

	/* Silently fail if AppArmor isn't enabled. */
	if (! apparmor_available())
		return 0;

	filename = nih_sprintf (NULL, "/proc/%d/attr/exec", getpid());

	if (! filename)
		return -1;

	f = fopen (filename, "w");

	if (! f)
		return -1;

	fprintf (f, "exec %s\n", profile);

	if (fclose (f))
		return -1;

	return 0;
}

/**
 * apparmor_available:
 *
 * This function checks to see if AppArmor is available and enabled
 *
 * Returns: TRUE if AppArmor is available, FALSE if it isn't
 **/
int
apparmor_available (void)
{
	struct stat     statbuf;
	FILE           *f;
	int            value = 0;

	/* Do not load if AppArmor is disabled.
	 */
	f = fopen ("/sys/module/apparmor/parameters/enabled", "r");

	if (! f)
		return FALSE;

	value = fgetc (f);

	if (fclose (f))
		return FALSE;

	if (value != 'Y')
		return FALSE;

	/* Do not load if AppArmor parser isn't available.
	 */
	if (stat (APPARMOR_PARSER, &statbuf) == 0) {
		if(! (S_ISREG(statbuf.st_mode) && statbuf.st_mode & S_IXUSR))
			return FALSE;
	} else {
		return FALSE;
	}

	return TRUE;
}

