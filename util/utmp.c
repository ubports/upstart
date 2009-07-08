/* upstart
 *
 * utmp.c - utmp and wtmp handling
 *
 * Copyright © 2009 Canonical Ltd.
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <sys/time.h>
#include <sys/utsname.h>

#include <utmpx.h>
#include <stdlib.h>
#include <string.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/logging.h>
#include <nih/error.h>

#include "utmp.h"


/* Prototypes for static functions */
static void utmp_entry      (struct utmpx *utmp, short type, pid_t pid,
			     const char *line, const char *id,
			     const char *user);
static int  utmp_write      (const char *utmp_file, const struct utmpx *utmp)
	__attribute__ ((warn_unused_result));
static void wtmp_write      (const char *wtmp_file, const struct utmpx *utmp);


/**
 * SHUTDOWN_TIME:
 *
 * The sysvinit last utility expects a special "shutdown" RUN_LVL entry,
 * and abuses the type to distinguish that.  We'll do the same.
 **/
#define SHUTDOWN_TIME 254


/**
 * utmp_read_runlevel:
 * @utmp_file: utmp or wtmp file to read from,
 * @prevlevel: pointer to store previous runlevel in.
 *
 * Reads the the most recent runlevel entry from @utmp_file, returning
 * the runlevel from it.  If @prevlevel is not NULL, the previous runlevel
 * will be stored in that variable.
 *
 * @utmp_file may be either a utmp or wtmp file, if NULL the default
 * /var/run/utmp is used.
 *
 * Returns: runlevel on success, negative value on raised error.
 **/
int
utmp_read_runlevel (const char *utmp_file,
		    int *       prevlevel)
{
	struct utmpx  utmp;
	struct utmpx *lvl;
	int           runlevel;

	memset (&utmp, 0, sizeof utmp);
	utmp.ut_type = RUN_LVL;

	if (utmp_file)
		utmpxname (utmp_file);
	setutxent ();

	lvl = getutxid (&utmp);
	if (! lvl) {
		nih_error_raise_system ();
		endutxent ();
		return -1;
	}

	runlevel = lvl->ut_pid % 256 ?: 'N';
	if (runlevel < 0)
		runlevel = 'N';
	if (prevlevel) {
		*prevlevel = lvl->ut_pid / 256 ?: 'N';
		if (*prevlevel < 0)
			*prevlevel = 'N';
	}

	endutxent ();

	return runlevel;
}

/**
 * utmp_get_runlevel:
 * @utmp_file: utmp or wtmp file to read from,
 * @prevlevel: pointer to store previous runlevel in.
 *
 * If the RUNLEVEL and PREVLEVEL environment variables are set, returns
 * the current and previous runlevels from those otherwise calls
 * utmp_read_runlevel() to read the most recent runlevel entry from
 * @utmp_file.
 *
 * Returns: runlevel on success, negative value on raised error.
 **/
int
utmp_get_runlevel (const char *utmp_file,
		   int *       prevlevel)
{
	const char *renv;
	const char *penv;

	renv = getenv ("RUNLEVEL");
	penv = getenv ("PREVLEVEL");

	if (renv) {
		if (prevlevel)
			*prevlevel = penv && penv[0] ? penv[0] : 'N';

		return renv[0] ?: 'N';
	}

	return utmp_read_runlevel (utmp_file, prevlevel);
}


/**
 * utmp_write_runlevel:
 * @utmp_file: utmp file,
 * @wtmp_file: wtmp file,
 * @runlevel: new runlevel,
 * @prevlevel: previous runlevel.
 *
 * Write a runlevel change record from @prevlevel to @runlevel to @utmp_file,
 * or /var/run/utmp if @utmp_file is NULL, and to @wtmp_file, or /var/log/wtmp
 * if @wtmp_file is NULL.
 *
 * Errors writing to the wtmp file are ignored.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
utmp_write_runlevel (const char *utmp_file,
		     const char *wtmp_file,
		     int         runlevel,
		     int         prevlevel)
{
	struct utmpx reboot;
	struct utmpx utmp;
	int          savedlevel;
	int          ret;

	nih_assert (runlevel > 0);
	nih_assert (prevlevel >= 0);

	utmp_entry (&reboot, BOOT_TIME, 0, NULL, NULL, NULL);

	/* Check for the previous runlevel entry in utmp, if it doesn't
	 * match then we assume a missed reboot so write the boot time
	 * record out first.
	 */
	savedlevel = utmp_read_runlevel (utmp_file, NULL);
	if (savedlevel != prevlevel) {
		if (savedlevel < 0)
			nih_free (nih_error_get ());

		if (utmp_write (utmp_file, &reboot) < 0)
			nih_free (nih_error_get ());
	}

	/* Check for the previous runlevel entry in wtmp, if it doesn't
	 * match then we assume a missed reboot so write the boot time
	 * record out first.
	 */
	savedlevel = utmp_read_runlevel (wtmp_file, NULL);
	if (savedlevel != prevlevel) {
		if (savedlevel < 0)
			nih_free (nih_error_get ());

		wtmp_write (wtmp_file, &reboot);
	}

	/* Write the runlevel change record */
	utmp_entry (&utmp, RUN_LVL, runlevel + prevlevel * 256,
		    NULL, NULL, NULL);

	ret = utmp_write (utmp_file, &utmp);
	wtmp_write (wtmp_file, &utmp);

	return ret;
}


/**
 * utmp_write_shutdown:
 * @utmp_file: utmp file to write to,
 * @wtmp_file: wtmp file to write to.
 *
 * Write a shutdown utmp record to @utmp_file, or /var/run/utmp if
 * @utmp_file is NULL, and to @wtmp_file, or /var/log/wtmp if @wtmp_file
 * is NULL.
 *
 * Errors writing to the wtmp file are ignored.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
utmp_write_shutdown (const char *utmp_file,
		     const char *wtmp_file)
{
	struct utmpx utmp;
	int          ret;

	utmp_entry (&utmp, SHUTDOWN_TIME, 0, NULL, NULL, NULL);

	ret = utmp_write (utmp_file, &utmp);
	wtmp_write (wtmp_file, &utmp);

	return ret;
}


/**
 * utmp_entry:
 * @utmp: utmp entry to fill,
 * @type: type of record,
 * @pid: process id of login process,
 * @line: device name of tty,
 * @id: terminal name suffix,
 * @user: username.
 *
 * Fill the utmp entry @utmp with the details passed, setting the auxiliary
 * information such as host and time to sensible defaults.  Depending on
 * @type, the other arguments may be ignored.
 *
 * When @type is BOOT_TIME, or the special SHUTDOWN_TIME, all arguments
 * are ignored.  When @type is RUN_LVL, the @line, @id and @user lines are
 * ignored.
 *
 * Any existing values in @utmp before this call will be lost.
 **/
static void
utmp_entry (struct utmpx *utmp,
	    short         type,
	    pid_t         pid,
	    const char *  line,
	    const char *  id,
	    const char *  user)
{
	struct utsname uts;
	struct timeval tv;

	nih_assert (utmp != NULL);
	nih_assert (type != EMPTY);

	switch (type) {
	case BOOT_TIME:
		pid = 0;
		line = "~";
		id = "~~";
		user = "reboot";
		break;
	case SHUTDOWN_TIME:
		type = RUN_LVL;
		pid = 0;
		line = "~";
		id = "~~";
		user = "shutdown";
		break;
	case RUN_LVL:
		nih_assert (pid != 0);
		line = "~";
		id = "~~";
		user = "runlevel";
		break;
	default:
		nih_assert (line != NULL);
		nih_assert (id != NULL);
		nih_assert (user != NULL);
	}

	memset (utmp, 0, sizeof (struct utmpx));

	utmp->ut_type = type;
	utmp->ut_pid = pid;

	strncpy (utmp->ut_line, line, sizeof utmp->ut_line);
	strncpy (utmp->ut_id, id, sizeof utmp->ut_id);
	strncpy (utmp->ut_user, user, sizeof utmp->ut_user);

	if (uname (&uts) == 0)
		strncpy (utmp->ut_host, uts.release, sizeof utmp->ut_host);

	gettimeofday (&tv, NULL);
	utmp->ut_tv.tv_sec = tv.tv_sec;
	utmp->ut_tv.tv_usec = tv.tv_usec;
}

/**
 * utmp_write:
 * @utmp_file: utmp file to write to,
 * @utmp: utmp entry to write.
 *
 * Write the utmp entry @utmp to @utmp_file, or /var/run/utmp if @utmp_file
 * is NULL.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
utmp_write (const char *        utmp_file,
	    const struct utmpx *utmp)
{
	nih_assert (utmp != NULL);

	utmpxname (utmp_file ?: _PATH_UTMPX);

	setutxent ();

	if (! pututxline (utmp)) {
		nih_error_raise_system ();
		endutxent ();
		return -1;
	}

	endutxent ();

	return 0;
}

/**
 * wtmp_write:
 * @wtmp_file: wtmp file to write to,
 * @utmp: utmp entry to write.
 *
 * Write the utmp entry @utmp to @wtmp_file, or /var/log/wtmp if @utmp_file
 * is NULL.
 **/
static void
wtmp_write (const char *        wtmp_file,
	    const struct utmpx *utmp)
{
	nih_assert (utmp != NULL);

	updwtmpx (wtmp_file ?: _PATH_WTMPX, utmp);
}
