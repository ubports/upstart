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

/**
 * UPSTART:
 *
 * The upstart binary.
 **/
#ifndef UPSTART

/* * * * * * * *  * * *** * * * * * * * * * ** * * * * * ** * *  * * */
/* * * * ** * **** * ** ** * * * * * **  * * ** * * * * * *  * * *  */
/* FIXME */
/* FIXME */
/* FIXME: 
 *
 */

#if 0
#define UPSTART SBINDIR "/init"
#endif
#define UPSTART "/data/bzr/upstart/upstart/init/init"

/* FIXME */
/* FIXME */
/* * * * ** * **** * ** ** * * * * * **  * * ** * * * * * *  * * *  */
/* * * * * * * *  * * *** * * * * * * * * * ** * * * * * ** * *  * * */
/* * * * ** * **** * ** ** * * * * * **  * * ** * * * * * *  * * *  */

#endif

/**
 * JOB_LOGDIR:
 *
 * Directory that jobs which specify CONSOLE_LOG will have their output
 * logged to.
 *
 **/
#ifndef JOB_LOGDIR
#define JOB_LOGDIR "/var/log/upstart"
#endif

/**
 * LOGDIR_ENV:
 *
 * Environment variable that if set specifies an alternative directory
 * to JOB_LOGDIR to write log files to.
 *
 **/
#ifndef LOGDIR_ENV
#define LOGDIR_ENV "UPSTART_LOGDIR" 
#endif


/**
 * File extension for standard configuration files.
 **/
#define CONF_EXT_STD ".conf"

/**
 * File extension for override files.
 *
 * Note that override files are not stored in the ConfSource 'files' hash:
 * all JobClass information from override files is added to the JobClass for
 * the corresponding (CONF_EXT_STD) object.
 **/
#define CONF_EXT_OVERRIDE ".override"

/**
 * Determine if specified path extension representes a standard
 * configuration file.
 *
 * @period: pointer to last period in path to check.
 *
 * Returns 1 if specified path extension matches that for a
 * standard configuration file, else return 0.
 **/
#define IS_CONF_EXT_STD(period) \
	(!strcmp (period, CONF_EXT_STD))

/**
 * Determine if specified path extension representes an
 * override file.
 *
 * @period: pointer to last period in path to check.
 *
 * Returns 1 if specified path extension matches that for
 * an override file, else return 0.
 **/
#define IS_CONF_EXT_OVERRIDE(period) \
	(!strcmp (period, CONF_EXT_OVERRIDE))

/**
 * Determine if specified filename has a valid configuration
 * file name extension.
 *
 * @period: pointer to last period in filename.
 *
 * Returns: TRUE if extension beyond @period is one of the
 * recognized types, else FALSE.
 **/
#define IS_CONF_EXT(period) \
	(IS_CONF_EXT_STD(period) || \
	 IS_CONF_EXT_OVERRIDE(period))

#endif /* INIT_PATHS_H */
