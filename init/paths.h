/* upstart
 *
 * Copyright © 2010-2011 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
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

#ifndef INIT_PATHS_H
#define INIT_PATHS_H

/**
 * PATH:
 *
 * This is the default PATH set by the init process itself.
 **/
#ifndef PATH
#define _PATH "/usr/local/sbin:/usr/local/bin:/usr/bin:/usr/sbin:/sbin:/bin"
#ifdef EXTRA_PATH
#define PATH _PATH ":" EXTRA_PATH
#else
#define PATH _PATH
#endif
#endif


/**
 * CONSOLE:
 *
 * This is the console device we give to processes that want one.
 **/
#ifndef CONSOLE
#define CONSOLE "/dev/console"
#endif

/**
 * DEV_NULL:
 *
 * This is the console device we give to processes that do not want any
 * console.
 **/
#ifndef DEV_NULL
#define DEV_NULL "/dev/null"
#endif


/**
 * CONFFILE:
 *
 * Init daemon configuration file.
 **/
#ifndef CONFFILE
#define CONFFILE "/etc/init.conf"
#endif

/**
 * CONFDIR:
 *
 * Default top-level directory of the system configuration files.
 **/
#ifndef CONFDIR
#define CONFDIR "/etc/init"
#endif

/**
 * USERCONFDIR:
 *
 * Sub-directory of user's home directory for their jobs.
 **/
#ifndef USERCONFDIR
#define USERCONFDIR ".init"
#endif


/**
 * CONFDIR_ENV:
 *
 * If this environment variable is set, read configuration files
 * from the location specified, rather than CONFDIR.
 *
 * Value is expected to be the full path to an alternative job
 * configuration directory.
 **/
#ifndef CONFDIR_ENV
#define CONFDIR_ENV "UPSTART_CONFDIR"
#endif


/**
 * SHELL:
 *
 * This is the shell binary used whenever we need special processing for
 * a command or when we need to run a script.
 **/
#ifndef SHELL
#define SHELL "/bin/sh"
#endif

/**
 * SBINDIR:
 *
 * Directory containing system binaries.
 **/
#ifndef SBINDIR
#define SBINDIR "/sbin"
#endif

/**
 * TELINIT:
 *
 * This is the telinit binary used when init is executed as an ordinary
 * process.
 **/
#ifndef TELINIT
#define TELINIT SBINDIR "/telinit"
#endif


#endif /* INIT_PATHS_H */
