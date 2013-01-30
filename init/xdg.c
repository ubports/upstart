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
#include <sys/stat.h>
#include <sys/types.h>

#include <nih/alloc.h>
#include <nih/logging.h>
#include <nih/string.h>

#include "paths.h"
#include "xdg.h"

/**
 * user_mode:
 *
 * If TRUE, upstart runs in user session mode.
 **/
int user_mode = FALSE;

/**
 * session_file:
 *
 * Full path to file containing UPSTART_SESSION details (only set when
 * user_mode in operation).
 *
 * File is created on startup and removed on clean shutdown.
 **/
const char *session_file = NULL;

/**
 * get_subdir:
 * @dir: initial directory,
 * @suffix: sub-directory of @dir,
 * @create: flag to create sub-directory.
 * 
 * Construct path by appending @suffix to @dir. If @create
 * flag is TRUE, also attempt to create that directory.
 *
 * Errors upon directory creation are ignored.
 * 
 * Returns: Newly-allocated path, or NULL on error.
 **/
char *
get_subdir (const char *dir, const char *suffix, int create)
{
	char *newdir;

	nih_assert (dir != NULL);
	nih_assert (suffix != NULL);
	nih_assert (suffix[0]);
	
	if (dir[0] == '/') {
		newdir = nih_sprintf (NULL, "%s/%s", dir, suffix);
		if (! newdir)
			return NULL;
		if (create)
			mkdir (newdir, INIT_XDG_PATH_MODE);
		return newdir;
	}

	return NULL;
}

/**
 * get_home_subdir:
 *
 * @suffix: sub-directory name,
 * @create: flag to create sub-directory.
 *
 * Construct path to @suffix directory in user's HOME directory.
 * If @create is TRUE, also attempt to create that directory.
 *
 * Errors upon directory creation are ignored.
 * 
 * Returns: Newly-allocated path, or NULL on error.
 **/
char *
get_home_subdir (const char *suffix, int create)
{
	char *env;

	env = getenv ("HOME");
	if (! env)
		return NULL;

	return get_subdir (env, suffix, create);
}

/**
 * xdg_get_cache_home:
 *
 * Determine an XDG compliant XDG_CACHE_HOME
 *
 * Returns: newly-allocated path, or NULL on error.
 **/
char *
xdg_get_cache_home (void)
{
	char             *dir;

	dir = getenv ("XDG_CACHE_HOME");
	
	if (dir && dir[0] == '/') {
		mkdir (dir, INIT_XDG_PATH_MODE);
		dir = nih_strdup (NULL, dir);
		return dir;
	}

	/* Per XDG spec, we should only create dirs, if we are
	 * attempting to write and the dir is not there. Here we
	 * anticipate logging to happen really soon now, hence we
	 * pre-create the cache dir. That does not protect us from
	 * this directory disappering while upstart is running. =/
	 * hence this dir should be created each time we try to write
	 * log... */
	dir = get_home_subdir (".cache", TRUE);

	return dir;
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
	char  *dir;

	dir = getenv ("XDG_CONFIG_HOME");
	
	if (dir && dir[0] == '/') {
		mkdir (dir, INIT_XDG_PATH_MODE);
		dir = nih_strdup (NULL, dir);
		return dir;
	}

	/* Per XDG spec, we should only create dirs, if we are
	 * attempting to write to the dir. But we only read config
	 * dir. But we rather create it, to place inotify watch on
	 * it. */
	dir = get_home_subdir (".config", TRUE);

	return dir;
}

/**
 * xdg_get_runtime_dir:
 *
 * Determine an XDG compliant XDG_RUNTIME_DIR.
 *
 * Note: No attempt is made to create this directory since if it does
 * not exist, a non-priv user is unlikely be able to create it anyway.
 *
 * Returns: newly-allocated path, or NULL on error.
 **/
char *
xdg_get_runtime_dir (void)
{
	char *dir;

	dir = getenv ("XDG_RUNTIME_DIR");

	if (dir && dir[0] == '/')
		dir = nih_strdup (NULL, dir);

	return dir;
}

/**
 * get_session_dir:
 *
 * Determine full path to XDG-compliant session directory used to store
 * session files.
 *
 * Returns: Newly-allocated path, or NULL on error.
 **/
char *
get_session_dir (void)
{
	nih_local char  *runtime_dir = NULL;
	nih_local char  *dir = NULL;
	char            *session_dir;

	runtime_dir = xdg_get_runtime_dir ();

	if (runtime_dir && runtime_dir[0] == '/') {
		dir = get_subdir (runtime_dir, INIT_XDG_SUBDIR, TRUE);
		if (! dir)
			return NULL;

		session_dir = get_subdir (dir, INIT_XDG_SESSION_SUBDIR,
				TRUE);

		return session_dir;
	}

	return NULL;
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
		mkdir (path, INIT_XDG_PATH_MODE);
		if (! nih_str_array_add (&all_dirs, NULL, NULL, path))
			goto error;
		nih_free (path);
		path = NULL;
	}

	/* Legacy User's: ~/.init */
	path = get_home_subdir (USERCONFDIR, FALSE);
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
		if (*p[0] != '/')
			continue;
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

/**
 * get_user_log_dir:
 *
 * Constructs an XDG compliant path to a cache directory in the user's
 * home directory. It can be used to store logs.
 *
 * Returns: newly-allocated array of paths, or NULL or error.
 **/
char *
get_user_log_dir (void)
{
	nih_local char *path = NULL;
	char *dir = NULL;
	path = xdg_get_cache_home ();
	if (path && path[0] == '/') {
		dir = nih_sprintf (NULL, "%s/%s", path, INIT_XDG_SUBDIR);
		if (! dir)
			return NULL;
		mkdir (dir, INIT_XDG_PATH_MODE);
		return dir;
	}
	return NULL;
}
