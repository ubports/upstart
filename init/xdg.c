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
 */

char *
get_home_subdir (char * suffix)
{
	char *dir;
	nih_assert (suffix && suffix[0]);
	
	dir = getenv("HOME");
	if ( dir && dir[0] ) {
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
 **/
char *
xdg_get_config_home (void)
{
	nih_local char  **env = NULL;
	size_t            len = 0;
	char             *dir;

	dir = getenv("XDG_CONFIG_HOME");
	
	if ( dir && dir[0] ) {
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
 **/
char **
xdg_get_config_dirs (void)
{
	char         *env_path;
	char         *result = NULL;
	size_t        len = 0;
	char        **dirs = NULL;

	env_path = getenv ("XDG_CONFIG_DIRS");
	if (! env_path || ! env_path[0])
		env_path = "/etc/xdg";

	dirs = nih_str_split(NULL, env_path, ":", TRUE);

	return dirs;
}

/**
 * xdg_get_dirs:
 *
 * Construct an array of user session config source paths to config dirs for a
 * particular user. This array can be iterated to add each of these
 * directories as config source dirs, when e.g. upstart is running as user session init.
 * This is a convenience function.
 *
 **/
char **
get_user_upstart_dirs (void)
{
	char       *path;
	char      **dirs = NULL;
	char  **all_dirs = NULL;

	all_dirs = nih_str_array_new (NULL);

	path = xdg_get_config_home ();
	if (path && path[0]) {
		NIH_MUST (nih_strcat_sprintf (&path, NULL, "/%s", INIT_XDG_SUBDIR));
		NIH_MUST (nih_str_array_add (&all_dirs, NULL, NULL, path));
		nih_free(path);
	}

	path = get_home_subdir (USERCONFDIR);
	if (path && path[0]) {
		NIH_MUST (nih_str_array_add (&all_dirs, NULL, NULL, path));
		nih_free(path);
	}

	dirs = xdg_get_config_dirs ();

	for (char **p = dirs; p && *p; p++) {
		NIH_MUST (nih_strcat_sprintf (p, NULL, "/%s", INIT_XDG_SUBDIR));
		NIH_MUST (nih_str_array_add (&all_dirs, NULL, NULL, *p));
	}

	return all_dirs;
}

