/* upstart
 *
 * test_utmp.c - test suite for util/utmp.c
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

#include <nih/test.h>

#include <sys/time.h>
#include <sys/utsname.h>

#include <errno.h>
#include <stdio.h>
#include <utmpx.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/error.h>

#include "utmp.h"


void
test_read_runlevel (void)
{
	char           filename[PATH_MAX];
	FILE *         file;
	struct utmpx   utmp;
	struct utsname uts;
	struct timeval tv;
	int            runlevel;
	int            prevlevel;
	NihError *     err;

	TEST_FUNCTION ("utmp_read_runlevel");
	TEST_FILENAME (filename);


	/* Check that we can obtain both the current and previous runlevel
	 * from the utmp file.
	 */
	TEST_FEATURE ("with runlevel and previous");
	TEST_ALLOC_FAIL {
		unlink (filename);

		file = fopen (filename, "w");
		fclose (file);

		memset (&utmp, 0, sizeof utmp);

		utmp.ut_type = RUN_LVL;
		utmp.ut_pid = '2' + 'S' * 256;

		strcpy (utmp.ut_line, "~");
		strcpy (utmp.ut_id, "~~");
		strncpy (utmp.ut_user, "runlevel", sizeof utmp.ut_user);
		if (uname (&uts) == 0)
			strncpy (utmp.ut_host, uts.release,
				 sizeof utmp.ut_host);

		gettimeofday (&tv, NULL);
		utmp.ut_tv.tv_sec = tv.tv_sec;
		utmp.ut_tv.tv_usec = tv.tv_usec;

		utmpxname (filename);

		setutxent ();
		pututxline (&utmp);
		endutxent ();

		prevlevel = 0;

		runlevel = utmp_read_runlevel (filename, &prevlevel);

		TEST_EQ (runlevel, '2');
		TEST_EQ (prevlevel, 'S');
	}


	/* Check that if no filename is passed, it defaults to the currently
	 * set file.
	 */
	TEST_FEATURE ("with no filename");
	TEST_ALLOC_FAIL {
		unlink (filename);

		file = fopen (filename, "w");
		fclose (file);

		memset (&utmp, 0, sizeof utmp);

		utmp.ut_type = RUN_LVL;
		utmp.ut_pid = '2' + 'S' * 256;

		strcpy (utmp.ut_line, "~");
		strcpy (utmp.ut_id, "~~");
		strncpy (utmp.ut_user, "runlevel", sizeof utmp.ut_user);
		if (uname (&uts) == 0)
			strncpy (utmp.ut_host, uts.release,
				 sizeof utmp.ut_host);

		gettimeofday (&tv, NULL);
		utmp.ut_tv.tv_sec = tv.tv_sec;
		utmp.ut_tv.tv_usec = tv.tv_usec;

		utmpxname (filename);

		setutxent ();
		pututxline (&utmp);
		endutxent ();

		prevlevel = 0;

		runlevel = utmp_read_runlevel (NULL, &prevlevel);

		TEST_EQ (runlevel, '2');
		TEST_EQ (prevlevel, 'S');
	}


	/* Check that if there was no previous runlevel, the special 'N'
	 * runlevel is returned instead.
	 */
	TEST_FEATURE ("with no previous runlevel recorded");
	TEST_ALLOC_FAIL {
		unlink (filename);

		file = fopen (filename, "w");
		fclose (file);

		memset (&utmp, 0, sizeof utmp);

		utmp.ut_type = RUN_LVL;
		utmp.ut_pid = '2';

		strcpy (utmp.ut_line, "~");
		strcpy (utmp.ut_id, "~~");
		strncpy (utmp.ut_user, "runlevel", sizeof utmp.ut_user);
		if (uname (&uts) == 0)
			strncpy (utmp.ut_host, uts.release,
				 sizeof utmp.ut_host);

		gettimeofday (&tv, NULL);
		utmp.ut_tv.tv_sec = tv.tv_sec;
		utmp.ut_tv.tv_usec = tv.tv_usec;

		utmpxname (filename);

		setutxent ();
		pututxline (&utmp);
		endutxent ();

		prevlevel = 0;

		runlevel = utmp_read_runlevel (filename, &prevlevel);

		TEST_EQ (runlevel, '2');
		TEST_EQ (prevlevel, 'N');
	}


	/* Check that we can choose not to obtain the previous runlevel,
	 * and instead only obtain the current one.
	 */
	TEST_FEATURE ("with runlevel only");
	TEST_ALLOC_FAIL {
		unlink (filename);

		file = fopen (filename, "w");
		fclose (file);

		memset (&utmp, 0, sizeof utmp);

		utmp.ut_type = RUN_LVL;
		utmp.ut_pid = '2' + 'S' * 256;

		strcpy (utmp.ut_line, "~");
		strcpy (utmp.ut_id, "~~");
		strncpy (utmp.ut_user, "runlevel", sizeof utmp.ut_user);
		if (uname (&uts) == 0)
			strncpy (utmp.ut_host, uts.release,
				 sizeof utmp.ut_host);

		gettimeofday (&tv, NULL);
		utmp.ut_tv.tv_sec = tv.tv_sec;
		utmp.ut_tv.tv_usec = tv.tv_usec;

		utmpxname (filename);

		setutxent ();
		pututxline (&utmp);
		endutxent ();

		prevlevel = 0;

		runlevel = utmp_read_runlevel (filename, NULL);

		TEST_EQ (runlevel, '2');
	}


	/* Check that a raised ESRCH error is returned along with a
	 * negative value if we couldn't find a runlevel marker.
	 */
	TEST_FEATURE ("with no record");
	TEST_ALLOC_FAIL {
		unlink (filename);

		file = fopen (filename, "w");
		fclose (file);

		prevlevel = 0;

		runlevel = utmp_read_runlevel (filename, &prevlevel);

		TEST_LT (runlevel, 0);
		TEST_EQ (prevlevel, 0);

		err = nih_error_get ();
		TEST_EQ (err->number, ESRCH);
		nih_free (err);
	}


	/* Check that an empty runlevel record (e.g. by the shutdown tool)
	 * results in the 'N' runlevel being returned instead.
	 */
	TEST_FEATURE ("with shutdown record");
	TEST_ALLOC_FAIL {
		unlink (filename);

		file = fopen (filename, "w");
		fclose (file);

		memset (&utmp, 0, sizeof utmp);

		utmp.ut_type = RUN_LVL;
		utmp.ut_pid = 0;

		strcpy (utmp.ut_line, "~");
		strcpy (utmp.ut_id, "~~");
		strncpy (utmp.ut_user, "shutdown", sizeof utmp.ut_user);
		if (uname (&uts) == 0)
			strncpy (utmp.ut_host, uts.release,
				 sizeof utmp.ut_host);

		gettimeofday (&tv, NULL);
		utmp.ut_tv.tv_sec = tv.tv_sec;
		utmp.ut_tv.tv_usec = tv.tv_usec;

		utmpxname (filename);

		setutxent ();
		pututxline (&utmp);
		endutxent ();

		prevlevel = 0;

		runlevel = utmp_read_runlevel (filename, &prevlevel);

		TEST_EQ (runlevel, 'N');
		TEST_EQ (prevlevel, 'N');
	}


	/* Check that a corrupt runlevel record results in the 'N' runlevel
	 * being returned instead.
	 */
	TEST_FEATURE ("with corrupt record");
	TEST_ALLOC_FAIL {
		unlink (filename);

		file = fopen (filename, "w");
		fclose (file);

		memset (&utmp, 0, sizeof utmp);

		utmp.ut_type = RUN_LVL;
		utmp.ut_pid = -14 + -12 * 256;

		strcpy (utmp.ut_line, "~");
		strcpy (utmp.ut_id, "~~");
		strncpy (utmp.ut_user, "runlevel", sizeof utmp.ut_user);
		if (uname (&uts) == 0)
			strncpy (utmp.ut_host, uts.release,
				 sizeof utmp.ut_host);

		gettimeofday (&tv, NULL);
		utmp.ut_tv.tv_sec = tv.tv_sec;
		utmp.ut_tv.tv_usec = tv.tv_usec;

		utmpxname (filename);

		setutxent ();
		pututxline (&utmp);
		endutxent ();

		prevlevel = 0;

		runlevel = utmp_read_runlevel (filename, &prevlevel);

		TEST_EQ (runlevel, 'N');
		TEST_EQ (prevlevel, 'N');
	}


	unlink (filename);
}

void
test_get_runlevel (void)
{
	char           filename[PATH_MAX];
	FILE *         file;
	struct utmpx   utmp;
	struct utsname uts;
	struct timeval tv;
	int            runlevel;
	int            prevlevel;
	NihError *     err;

	TEST_FUNCTION ("utmp_get_runlevel");
	TEST_FILENAME (filename);


	/* Check that the function returns the contents of the environment
	 * in preference to the contents of the utmp file.
	 */
	TEST_FEATURE ("with environment");
	setenv ("RUNLEVEL", "3", TRUE);
	setenv ("PREVLEVEL", "2", TRUE);

	TEST_ALLOC_FAIL {
		unlink (filename);

		file = fopen (filename, "w");
		fclose (file);

		memset (&utmp, 0, sizeof utmp);

		utmp.ut_type = RUN_LVL;
		utmp.ut_pid = '2' + 'S' * 256;

		strcpy (utmp.ut_line, "~");
		strcpy (utmp.ut_id, "~~");
		strncpy (utmp.ut_user, "runlevel", sizeof utmp.ut_user);
		if (uname (&uts) == 0)
			strncpy (utmp.ut_host, uts.release,
				 sizeof utmp.ut_host);

		gettimeofday (&tv, NULL);
		utmp.ut_tv.tv_sec = tv.tv_sec;
		utmp.ut_tv.tv_usec = tv.tv_usec;

		utmpxname (filename);

		setutxent ();
		pututxline (&utmp);
		endutxent ();

		prevlevel = 0;

		runlevel = utmp_get_runlevel (filename, &prevlevel);

		TEST_EQ (runlevel, '3');
		TEST_EQ (prevlevel, '2');
	}

	unsetenv ("RUNLEVEL");
	unsetenv ("PREVLEVEL");


	/* Check that we can obtain both the current and previous runlevel
	 * from the utmp file when there's no environment set.
	 */
	TEST_FEATURE ("with runlevel and previous");
	TEST_ALLOC_FAIL {
		unlink (filename);

		file = fopen (filename, "w");
		fclose (file);

		memset (&utmp, 0, sizeof utmp);

		utmp.ut_type = RUN_LVL;
		utmp.ut_pid = '2' + 'S' * 256;

		strcpy (utmp.ut_line, "~");
		strcpy (utmp.ut_id, "~~");
		strncpy (utmp.ut_user, "runlevel", sizeof utmp.ut_user);
		if (uname (&uts) == 0)
			strncpy (utmp.ut_host, uts.release,
				 sizeof utmp.ut_host);

		gettimeofday (&tv, NULL);
		utmp.ut_tv.tv_sec = tv.tv_sec;
		utmp.ut_tv.tv_usec = tv.tv_usec;

		utmpxname (filename);

		setutxent ();
		pututxline (&utmp);
		endutxent ();

		prevlevel = 0;

		runlevel = utmp_get_runlevel (filename, &prevlevel);

		TEST_EQ (runlevel, '2');
		TEST_EQ (prevlevel, 'S');
	}


	/* Check that when the environment variables are set, but empty,
	 * the code substitutes 'N' instead.
	 */
	TEST_FEATURE ("with empty environment");
	setenv ("RUNLEVEL", "", TRUE);
	setenv ("PREVLEVEL", "", TRUE);

	TEST_ALLOC_FAIL {
		unlink (filename);

		file = fopen (filename, "w");
		fclose (file);

		memset (&utmp, 0, sizeof utmp);

		utmp.ut_type = RUN_LVL;
		utmp.ut_pid = '2' + 'S' * 256;

		strcpy (utmp.ut_line, "~");
		strcpy (utmp.ut_id, "~~");
		strncpy (utmp.ut_user, "runlevel", sizeof utmp.ut_user);
		if (uname (&uts) == 0)
			strncpy (utmp.ut_host, uts.release,
				 sizeof utmp.ut_host);

		gettimeofday (&tv, NULL);
		utmp.ut_tv.tv_sec = tv.tv_sec;
		utmp.ut_tv.tv_usec = tv.tv_usec;

		utmpxname (filename);

		setutxent ();
		pututxline (&utmp);
		endutxent ();

		prevlevel = 0;

		runlevel = utmp_get_runlevel (filename, &prevlevel);

		TEST_EQ (runlevel, 'N');
		TEST_EQ (prevlevel, 'N');
	}

	unsetenv ("RUNLEVEL");
	unsetenv ("PREVLEVEL");


	/* Check that when the runlevel environment variable is set, but not
	 * the previous level variable the runlevel from the variable is
	 * still returned and prevlevel is N not the contents of utmp.
	 */
	TEST_FEATURE ("with missing PREVLEVEL");
	setenv ("RUNLEVEL", "3", TRUE);

	TEST_ALLOC_FAIL {
		unlink (filename);

		file = fopen (filename, "w");
		fclose (file);

		memset (&utmp, 0, sizeof utmp);

		utmp.ut_type = RUN_LVL;
		utmp.ut_pid = '2' + 'S' * 256;

		strcpy (utmp.ut_line, "~");
		strcpy (utmp.ut_id, "~~");
		strncpy (utmp.ut_user, "runlevel", sizeof utmp.ut_user);
		if (uname (&uts) == 0)
			strncpy (utmp.ut_host, uts.release,
				 sizeof utmp.ut_host);

		gettimeofday (&tv, NULL);
		utmp.ut_tv.tv_sec = tv.tv_sec;
		utmp.ut_tv.tv_usec = tv.tv_usec;

		utmpxname (filename);

		setutxent ();
		pututxline (&utmp);
		endutxent ();

		prevlevel = 0;

		runlevel = utmp_get_runlevel (filename, &prevlevel);

		TEST_EQ (runlevel, '3');
		TEST_EQ (prevlevel, 'N');
	}

	unsetenv ("RUNLEVEL");


	/* Check that we can choose not to obtain the previous runlevel,
	 * and instead only obtain the current one.
	 */
	TEST_FEATURE ("with runlevel only");
	setenv ("RUNLEVEL", "3", TRUE);
	setenv ("PREVLEVEL", "2", TRUE);

	TEST_ALLOC_FAIL {
		unlink (filename);

		file = fopen (filename, "w");
		fclose (file);

		memset (&utmp, 0, sizeof utmp);

		utmp.ut_type = RUN_LVL;
		utmp.ut_pid = '2' + 'S' * 256;

		strcpy (utmp.ut_line, "~");
		strcpy (utmp.ut_id, "~~");
		strncpy (utmp.ut_user, "runlevel", sizeof utmp.ut_user);
		if (uname (&uts) == 0)
			strncpy (utmp.ut_host, uts.release,
				 sizeof utmp.ut_host);

		gettimeofday (&tv, NULL);
		utmp.ut_tv.tv_sec = tv.tv_sec;
		utmp.ut_tv.tv_usec = tv.tv_usec;

		utmpxname (filename);

		setutxent ();
		pututxline (&utmp);
		endutxent ();

		prevlevel = 0;

		runlevel = utmp_get_runlevel (filename, NULL);

		TEST_EQ (runlevel, '3');
	}

	unsetenv ("RUNLEVEL");
	unsetenv ("PREVLEVEL");


	/* Check that the function returns the contents of the environment
	 * even if the utmp file is empty.
	 */
	TEST_FEATURE ("with environment and no record");
	setenv ("RUNLEVEL", "2", TRUE);
	setenv ("PREVLEVEL", "S", TRUE);

	TEST_ALLOC_FAIL {
		unlink (filename);

		prevlevel = 0;

		runlevel = utmp_get_runlevel (filename, &prevlevel);

		TEST_EQ (runlevel, '2');
		TEST_EQ (prevlevel, 'S');
	}

	unsetenv ("RUNLEVEL");
	unsetenv ("PREVLEVEL");


	/* Check that a raised ESRCH error is returned along with a
	 * negative value if we couldn't find a runlevel marker.
	 */
	TEST_FEATURE ("with no record");
	TEST_ALLOC_FAIL {
		unlink (filename);

		file = fopen (filename, "w");
		fclose (file);

		prevlevel = 0;

		runlevel = utmp_get_runlevel (filename, &prevlevel);

		TEST_LT (runlevel, 0);
		TEST_EQ (prevlevel, 0);

		err = nih_error_get ();
		TEST_EQ (err->number, ESRCH);
		nih_free (err);
	}


	unlink (filename);
}


void
test_write_runlevel (void)
{
	char           utmp_file[PATH_MAX];
	char           wtmp_file[PATH_MAX];
	struct utmpx * utmp;
	struct utmpx   record;
	struct utsname uts;
	struct timeval tv;
	int            ret;
	NihError *     err;

	TEST_FUNCTION ("utmp_write_runlevel");
	TEST_FILENAME (utmp_file);
	TEST_FILENAME (wtmp_file);


	/* Check that we can write a runlevel record to both the utmp
	 * and wtmp files; the record should be a RUN_LVL entry with the
	 * "runlevel" user, pid containing both the new and previous
	 * runlevel, and other fields left as defaults.  Since the files
	 * are fresh, a reboot record should also be written.
	 */
	TEST_FEATURE ("with utmp and wtmp file");
	TEST_ALLOC_FAIL {
		unlink (utmp_file);
		fclose (fopen (utmp_file, "w"));

		unlink (wtmp_file);
		fclose (fopen (wtmp_file, "w"));

		ret = utmp_write_runlevel (utmp_file, wtmp_file, '2', 'S');

		TEST_EQ (ret, 0);

		utmpxname (utmp_file);

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, BOOT_TIME);
		TEST_EQ (utmp->ut_pid, 0);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "reboot");

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, '2' + 'S' * 256);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);


		utmpxname (wtmp_file);

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, BOOT_TIME);
		TEST_EQ (utmp->ut_pid, 0);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "reboot");

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, '2' + 'S' * 256);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);
	}


	/* Check that it's ok to have no known previous runlevels, it
	 * will be left blank in the file and reboot entries always
	 * added.
	 */
	TEST_FEATURE ("with no previous runlevel");
	TEST_ALLOC_FAIL {
		unlink (utmp_file);
		fclose (fopen (utmp_file, "w"));

		unlink (wtmp_file);
		fclose (fopen (wtmp_file, "w"));

		ret = utmp_write_runlevel (utmp_file, wtmp_file, '2', 0);

		TEST_EQ (ret, 0);

		utmpxname (utmp_file);

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, BOOT_TIME);
		TEST_EQ (utmp->ut_pid, 0);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "reboot");

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, '2');
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);


		utmpxname (wtmp_file);

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, BOOT_TIME);
		TEST_EQ (utmp->ut_pid, 0);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "reboot");

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, '2');
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);
	}


	/* Check that the user-facing 'N' for no previous runlevel is
	 * converted to zero and thus always left blank in the file and
	 * reboot entries always added.
	 */
	TEST_FEATURE ("with unknown previous runlevel");
	TEST_ALLOC_FAIL {
		unlink (utmp_file);
		fclose (fopen (utmp_file, "w"));

		unlink (wtmp_file);
		fclose (fopen (wtmp_file, "w"));

		ret = utmp_write_runlevel (utmp_file, wtmp_file, '2', 'N');

		TEST_EQ (ret, 0);

		utmpxname (utmp_file);

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, BOOT_TIME);
		TEST_EQ (utmp->ut_pid, 0);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "reboot");

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, '2');
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);


		utmpxname (wtmp_file);

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, BOOT_TIME);
		TEST_EQ (utmp->ut_pid, 0);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "reboot");

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, '2');
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);
	}


	/* Check that the new runlevel record replaces the existing record
	 * in the utmp file, but appends a new record to the wtmp file.
	 * Since the records match, no reboot record need to be written.
	 */
	TEST_FEATURE ("with existing records in files");
	TEST_ALLOC_FAIL {
		unlink (utmp_file);
		fclose (fopen (utmp_file, "w"));

		unlink (wtmp_file);
		fclose (fopen (wtmp_file, "w"));

		memset (&record, 0, sizeof record);

		record.ut_type = RUN_LVL;
		record.ut_pid = '2' + 'S' * 256;

		strcpy (record.ut_line, "~");
		strcpy (record.ut_id, "~~");
		strncpy (record.ut_user, "runlevel", sizeof record.ut_user);
		if (uname (&uts) == 0)
			strncpy (record.ut_host, uts.release,
				 sizeof record.ut_host);

		gettimeofday (&tv, NULL);
		record.ut_tv.tv_sec = tv.tv_sec;
		record.ut_tv.tv_usec = tv.tv_usec;

		utmpxname (utmp_file);

		setutxent ();
		pututxline (&record);
		endutxent ();

		updwtmpx (wtmp_file, &record);

		ret = utmp_write_runlevel (utmp_file, wtmp_file, '5', '2');

		TEST_EQ (ret, 0);

		utmpxname (utmp_file);

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, '5' + '2' * 256);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		if (utmp->ut_tv.tv_sec == tv.tv_sec) {
			TEST_NE (utmp->ut_tv.tv_usec, tv.tv_usec);
		} else {
			TEST_NE (utmp->ut_tv.tv_sec, tv.tv_sec);
		}

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);


		utmpxname (wtmp_file);

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, '2' + 'S' * 256);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		TEST_EQ (utmp->ut_tv.tv_sec, tv.tv_sec);
		TEST_EQ (utmp->ut_tv.tv_usec, tv.tv_usec);

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, '5' + '2' * 256);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		if (utmp->ut_tv.tv_sec == tv.tv_sec) {
			TEST_NE (utmp->ut_tv.tv_usec, tv.tv_usec);
		} else {
			TEST_NE (utmp->ut_tv.tv_sec, tv.tv_sec);
		}

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);
	}


	/* Check that if the existing utmp record does not match the
	 * previous runlevel stated in the change, a missed reboot is
	 * assumed and the reboot entry is prepended to the file first.
	 */
	TEST_FEATURE ("with missed reboot in utmp file");
	TEST_ALLOC_FAIL {
		unlink (utmp_file);
		fclose (fopen (utmp_file, "w"));

		unlink (wtmp_file);
		fclose (fopen (wtmp_file, "w"));

		memset (&record, 0, sizeof record);

		record.ut_type = RUN_LVL;
		record.ut_pid = '0' + '2' * 256;

		strcpy (record.ut_line, "~");
		strcpy (record.ut_id, "~~");
		strncpy (record.ut_user, "runlevel", sizeof record.ut_user);
		if (uname (&uts) == 0)
			strncpy (record.ut_host, uts.release,
				 sizeof record.ut_host);

		gettimeofday (&tv, NULL);
		record.ut_tv.tv_sec = tv.tv_sec;
		record.ut_tv.tv_usec = tv.tv_usec;

		utmpxname (utmp_file);

		setutxent ();
		pututxline (&record);
		endutxent ();

		record.ut_pid = 'S';

		updwtmpx (wtmp_file, &record);


		ret = utmp_write_runlevel (utmp_file, wtmp_file, '2', 'S');

		TEST_EQ (ret, 0);

		utmpxname (utmp_file);

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, '2' + 'S' * 256);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		if (utmp->ut_tv.tv_sec == tv.tv_sec) {
			TEST_NE (utmp->ut_tv.tv_usec, tv.tv_usec);
		} else {
			TEST_NE (utmp->ut_tv.tv_sec, tv.tv_sec);
		}

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, BOOT_TIME);
		TEST_EQ (utmp->ut_pid, 0);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "reboot");

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);


		utmpxname (wtmp_file);

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, 'S');
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		TEST_EQ (utmp->ut_tv.tv_sec, tv.tv_sec);
		TEST_EQ (utmp->ut_tv.tv_usec, tv.tv_usec);

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, '2' + 'S' * 256);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		if (utmp->ut_tv.tv_sec == tv.tv_sec) {
			TEST_NE (utmp->ut_tv.tv_usec, tv.tv_usec);
		} else {
			TEST_NE (utmp->ut_tv.tv_sec, tv.tv_sec);
		}

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);
	}


	/* Check that if the existing wtmp record does not match the
	 * previous runlevel stated in the change, a missed reboot is
	 * assumed and the reboot entry is appended to the file first.
	 */
	TEST_FEATURE ("with missed reboot in wtmp file");
	TEST_ALLOC_FAIL {
		unlink (utmp_file);
		fclose (fopen (utmp_file, "w"));

		unlink (wtmp_file);
		fclose (fopen (wtmp_file, "w"));

		memset (&record, 0, sizeof record);

		record.ut_type = RUN_LVL;
		record.ut_pid = 'S';

		strcpy (record.ut_line, "~");
		strcpy (record.ut_id, "~~");
		strncpy (record.ut_user, "runlevel", sizeof record.ut_user);
		if (uname (&uts) == 0)
			strncpy (record.ut_host, uts.release,
				 sizeof record.ut_host);

		gettimeofday (&tv, NULL);
		record.ut_tv.tv_sec = tv.tv_sec;
		record.ut_tv.tv_usec = tv.tv_usec;

		utmpxname (utmp_file);

		setutxent ();
		pututxline (&record);
		endutxent ();

		record.ut_pid = '0' + '2' * 256;

		updwtmpx (wtmp_file, &record);


		ret = utmp_write_runlevel (utmp_file, wtmp_file, '2', 'S');

		TEST_EQ (ret, 0);

		utmpxname (utmp_file);

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, '2' + 'S' * 256);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		if (utmp->ut_tv.tv_sec == tv.tv_sec) {
			TEST_NE (utmp->ut_tv.tv_usec, tv.tv_usec);
		} else {
			TEST_NE (utmp->ut_tv.tv_sec, tv.tv_sec);
		}

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);


		utmpxname (wtmp_file);


		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, '0' + '2' * 256);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		TEST_EQ (utmp->ut_tv.tv_sec, tv.tv_sec);
		TEST_EQ (utmp->ut_tv.tv_usec, tv.tv_usec);

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, BOOT_TIME);
		TEST_EQ (utmp->ut_pid, 0);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "reboot");

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, '2' + 'S' * 256);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		if (utmp->ut_tv.tv_sec == tv.tv_sec) {
			TEST_NE (utmp->ut_tv.tv_usec, tv.tv_usec);
		} else {
			TEST_NE (utmp->ut_tv.tv_sec, tv.tv_sec);
		}

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);
	}


	/* Check that an error writing to the utmp file is returned as
	 * a raised error, but doesn't prevent writing to the wtmp file.
	 */
	TEST_FEATURE ("with error writing to utmp file");
	TEST_ALLOC_FAIL {
		unlink (utmp_file);
		fclose (fopen (utmp_file, "w"));
		chmod (utmp_file, 0400);

		unlink (wtmp_file);
		fclose (fopen (wtmp_file, "w"));

		ret = utmp_write_runlevel (utmp_file, wtmp_file, '2', 'S');

		TEST_LT (ret, 0);

		err = nih_error_get ();
		TEST_EQ (err->number, EBADF);
		nih_free (err);

		utmpxname (utmp_file);

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);

		utmpxname (wtmp_file);

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, BOOT_TIME);
		TEST_EQ (utmp->ut_pid, 0);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "reboot");

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, '2' + 'S' * 256);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);
	}


	/* Check that an error writing to the wtmp file doesn't prevent
	 * writing to the utmp file and doesn't result in an error.
	 */
	TEST_FEATURE ("with error writing to wtmp file");
	TEST_ALLOC_FAIL {
		unlink (utmp_file);
		fclose (fopen (utmp_file, "w"));

		unlink (wtmp_file);
		fclose (fopen (wtmp_file, "w"));
		chmod (wtmp_file, 0400);

		ret = utmp_write_runlevel (utmp_file, wtmp_file, '2', 'S');

		TEST_EQ (ret, 0);

		utmpxname (utmp_file);

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, BOOT_TIME);
		TEST_EQ (utmp->ut_pid, 0);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "reboot");

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, '2' + 'S' * 256);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);


		utmpxname (wtmp_file);

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);
	}


	unlink (utmp_file);
	unlink (wtmp_file);
}

void
test_write_shutdown (void)
{
	char           utmp_file[PATH_MAX];
	char           wtmp_file[PATH_MAX];
	struct utmpx * utmp;
	struct utmpx   record;
	struct utsname uts;
	struct timeval tv;
	int            ret;
	NihError *     err;

	TEST_FUNCTION ("utmp_write_shutdown");
	TEST_FILENAME (utmp_file);
	TEST_FILENAME (wtmp_file);


	/* Check that we can write a shutdown record to both the utmp
	 * and wtmp files; the record should be a RUN_LVL entry with the
	 * "shutdown" user and other fields left as defaults.
	 */
	TEST_FEATURE ("with utmp and wtmp file");
	TEST_ALLOC_FAIL {
		unlink (utmp_file);
		fclose (fopen (utmp_file, "w"));

		unlink (wtmp_file);
		fclose (fopen (wtmp_file, "w"));

		ret = utmp_write_shutdown (utmp_file, wtmp_file);

		TEST_EQ (ret, 0);

		utmpxname (utmp_file);

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, 0);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "shutdown");

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);


		utmpxname (wtmp_file);

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, 0);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "shutdown");

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);
	}


	/* Check that the new shutdown record replaces the existing record
	 * in the utmp file, but appends a new record to the wtmp file.
	 */
	TEST_FEATURE ("with existing records in files");
	TEST_ALLOC_FAIL {
		unlink (utmp_file);
		fclose (fopen (utmp_file, "w"));

		unlink (wtmp_file);
		fclose (fopen (wtmp_file, "w"));

		memset (&record, 0, sizeof record);

		record.ut_type = RUN_LVL;
		record.ut_pid = 0;

		strcpy (record.ut_line, "~");
		strcpy (record.ut_id, "~~");
		strncpy (record.ut_user, "shutdown", sizeof record.ut_user);
		if (uname (&uts) == 0)
			strncpy (record.ut_host, uts.release,
				 sizeof record.ut_host);

		gettimeofday (&tv, NULL);
		record.ut_tv.tv_sec = tv.tv_sec;
		record.ut_tv.tv_usec = tv.tv_usec;

		utmpxname (utmp_file);

		setutxent ();
		pututxline (&record);
		endutxent ();

		updwtmpx (wtmp_file, &record);

		ret = utmp_write_shutdown (utmp_file, wtmp_file);

		TEST_EQ (ret, 0);

		utmpxname (utmp_file);

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, 0);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "shutdown");

		if (utmp->ut_tv.tv_sec == tv.tv_sec) {
			TEST_NE (utmp->ut_tv.tv_usec, tv.tv_usec);
		} else {
			TEST_NE (utmp->ut_tv.tv_sec, tv.tv_sec);
		}

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);


		utmpxname (wtmp_file);

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, 0);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "shutdown");

		TEST_EQ (utmp->ut_tv.tv_sec, tv.tv_sec);
		TEST_EQ (utmp->ut_tv.tv_usec, tv.tv_usec);

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, 0);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "shutdown");

		if (utmp->ut_tv.tv_sec == tv.tv_sec) {
			TEST_NE (utmp->ut_tv.tv_usec, tv.tv_usec);
		} else {
			TEST_NE (utmp->ut_tv.tv_sec, tv.tv_sec);
		}

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);
	}


	/* Check that an error writing to the utmp file is returned as
	 * a raised error, but doesn't prevent writing to the wtmp file.
	 */
	TEST_FEATURE ("with error writing to utmp file");
	TEST_ALLOC_FAIL {
		unlink (utmp_file);
		fclose (fopen (utmp_file, "w"));
		chmod (utmp_file, 0400);

		unlink (wtmp_file);
		fclose (fopen (wtmp_file, "w"));

		ret = utmp_write_shutdown (utmp_file, wtmp_file);

		TEST_LT (ret, 0);

		err = nih_error_get ();
		TEST_EQ (err->number, EBADF);
		nih_free (err);

		utmpxname (utmp_file);

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);

		utmpxname (wtmp_file);

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, 0);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "shutdown");
	}


	/* Check that an error writing to the wtmp file doesn't prevent
	 * writing to the utmp file and doesn't result in an error.
	 */
	TEST_FEATURE ("with error writing to wtmp file");
	TEST_ALLOC_FAIL {
		unlink (utmp_file);
		fclose (fopen (utmp_file, "w"));

		unlink (wtmp_file);
		fclose (fopen (wtmp_file, "w"));
		chmod (wtmp_file, 0400);

		ret = utmp_write_shutdown (utmp_file, wtmp_file);

		TEST_EQ (ret, 0);

		utmpxname (utmp_file);

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, 0);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "shutdown");

		utmpxname (wtmp_file);

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);
	}


	unlink (utmp_file);
	unlink (wtmp_file);
}


int
main (int   argc,
      char *argv[])
{
	nih_error_init ();

	test_read_runlevel ();
	test_get_runlevel ();

	test_write_runlevel ();
	test_write_shutdown ();
}
