/* upstart
 *
 * Copyright © 2009 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
 *

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
 * DEV_FD:
 *
 * Directory containing the special file descriptor nodes for the running
 * process.
 **/
#ifndef DEV_FD
#define DEV_FD "/dev/fd"
#endif


/**
 * CONFDIR:
 *
 * Top-level directory of the system configuration files.
 **/
#ifndef CONFDIR
#define CONFDIR "/etc/init"
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
