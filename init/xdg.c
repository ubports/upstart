/* upstart
 *
 * xdg.c - XDG compliant path constructor
 *
 * Copyright © 2012 Canonical Ltd.
 * Author: Dmitrijs Ledkovs <dmitrijs.ledkovs@canonical.com>
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

#include <stdlib.h>

#include <nih/alloc.h>
#include <nih/logging.h>
#include <nih/string.h>

#include "paths.h"
#include "xdg.h"

/**
 * get_home_subdir:
 *
 * Construct path to directory in user's HOME dir.
 * 
 * Returns: newly-allocated path, or NULL on error.
 */

char *
get_home_subdir (const char * suffix)
{
	char *dir;
	nih_assert (suffix && suffix[0]);
	
	dir = getenv ("HOME");
	if (dir && dir[0]) {
		dir = nih_sprintf (NULL, "%s/%s", dir, suffix);
		return dir;
	}

	return NULL;
}

/**
 * xdg_get_config_home:
 *
 * Determine an XDG compliant XDG_CONFIG_HOME
 *
 * Returns: newly-allocated path, or NULL on error.
 **/
char *
xdg_get_config_home (void)
{
	nih_local char  **env = NULL;
	char             *dir;

	dir = getenv ("XDG_CONFIG_HOME");
	
	if (dir && dir[0]) {
		dir = nih_strdup (NULL, dir);
		return dir;
	}

	dir = get_home_subdir (".config");

	return dir;
}

/**
 * xdg_get_config_dirs:
 *
 * Determine a list of XDG compliant XDG_CONFIG_DIRS
 *
 * Returns: newly-allocated array of paths, or NULL on error.
 **/
char **
xdg_get_config_dirs (void)
{
	char         *env_path;
	char        **dirs = NULL;

	env_path = getenv ("XDG_CONFIG_DIRS");
	if (! env_path || ! env_path[0])
		env_path = "/etc/xdg";

	dirs = nih_str_split (NULL, env_path, ":", TRUE);

	return dirs;
}

/**
 * get_user_upstart_dirs:
 *
 * Construct an array of user session config source paths to config
 * dirs for a particular user. This array is sorted in highest
 * priority order and therefore can be iterated to add each of these
 * directories as config source dirs, when e.g. upstart is running as
 * user session init.
 *
 * Returns: newly-allocated array of paths, or NULL or error.
 **/
char **
get_user_upstart_dirs (void)
{
	char       *path = NULL;
	char      **dirs = NULL;
	char  **all_dirs = NULL;

	all_dirs = nih_str_array_new (NULL);
	if (! all_dirs)
		goto error;

	/* The current order is inline with Enhanced User Sessions Spec */

	/* User's: ~/.config/upstart or XDG_CONFIG_HOME/upstart */
	path = xdg_get_config_home ();
	if (! path)
		goto error;

	if (path && path[0]) {
	        if (! nih_strcat_sprintf (&path, NULL, "/%s", INIT_XDG_SUBDIR))
			goto error;
		if (! nih_str_array_add (&all_dirs, NULL, NULL, path))
			goto error;
		nih_free (path);
		path = NULL;
	}

	/* Legacy User's: ~/.init */
	path = get_home_subdir (USERCONFDIR);
	if (! path)
		goto error;

	if (path && path[0]) {
		if (! nih_str_array_add (&all_dirs, NULL, NULL, path))
			goto error;
		nih_free (path);
		path = NULL;
	}

	/* Systems': XDG_CONFIG_DIRS/upstart */
	dirs = xdg_get_config_dirs ();
	if (! dirs)
		goto error;

	for (char **p = dirs; p && *p; p++) {
		if (! nih_strcat_sprintf (p, NULL, "/%s", INIT_XDG_SUBDIR))
			goto error;
		if (! nih_str_array_add (&all_dirs, NULL, NULL, *p))
			goto error;
	}
	nih_free (dirs);
	dirs = NULL;

	/* System's read-only location */
	if (! nih_str_array_add (&all_dirs, NULL, NULL, SYSTEM_USERCONFDIR))
		goto error;

	return all_dirs;
	
error:
	if (path)
		nih_free (path);

	if (dirs)
		nih_free (dirs);

	if (all_dirs)
		nih_free (all_dirs);

	return NULL;
}

