/* upstart
 *
 * Copyright © 2006 Canonical Ltd.
 * Author: Scott James Remnant <scott@ubuntu.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <sys/types.h>
#include <sys/time.h>
#include <sys/utsname.h>

#include <time.h>
#include <utmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/main.h>
#include <nih/option.h>
#include <nih/logging.h>
#include <nih/error.h>


/* Prototypes for static functions */
static void store (short type, pid_t pid, const char *user);


/**
 * reboot:
 *
 * TRUE if we need to store a reboot record.
 **/
static int reboot = FALSE;

/**
 * set:
 *
 * Run level to store in the utmp file.
 **/
static char *set = NULL;


/**
 * options:
 *
 * Command-line options accepted.
 **/
static NihOption options[] = {
	{ 0, "reboot", N_("store time of system boot"),
	  NULL, NULL, &reboot, NULL },
	{ 0, "set", N_("store new runlevel"),
	  NULL, "RUNLEVEL", &set, NULL },

	NIH_OPTION_LAST
};


int
main (int   argc,
      char *argv[])
{
	struct utmp   utmp, *lvl;
	char        **args, prev = 0, cur = 0;

	nih_main_init (argv[0]);

	nih_option_set_usage (_("[UTMP]"));
	nih_option_set_synopsis (_("Output previous and current runlevel."));
	nih_option_set_help (_("The system /var/run/utmp file is used "
			       "unless the alternate file UTMP is given.\n"
			       "\n"
			       "Normally this will only output the most "
			       "recent runlevel record in the utmp file, "
			       "the --set option can be used to add a new "
			       "record.  RUNLEVEL should be one of 0123456S.\n"
			       "\n"
			       "Alternately a reboot record may be added "
			       "to the file by using the --reboot option, "
			       "this will not output anything."));

	args = nih_option_parser (NULL, argc, argv, options, FALSE);
	if (! args)
		exit (1);

	/* Allow overriding of the utmp filename */
	if (args[0])
		utmpname (args[0]);

	/* Store the reboot time? */
	if (reboot) {
		store (BOOT_TIME, 0, "reboot");
		exit (0);
	}


	/* Retrieve the last runlevel marker */
	memset (&utmp, 0, sizeof (utmp));
	utmp.ut_type = RUN_LVL;

	setutent ();
	lvl = getutid (&utmp);
	if (lvl) {
		prev = lvl->ut_pid / 256;
		if (! prev)
			prev = 'N';

		cur = lvl->ut_pid % 256;
	}
	endutent ();

	/* Set the new runlevel */
	if (set) {
		/* Sanity check */
		if ((strlen (set) != 1) || (! strchr ("0123456S", set[0]))) {
			fprintf (stderr, _("%s: illegal runlevel: %s\n"),
				 program_name, set);
			nih_main_suggest_help ();
			exit (1);
		}

		store (RUN_LVL, set[0] + cur * 256, "runlevel");

		prev = cur;
		if (! prev)
			prev = 'N';

		cur = set[0];
	}

	if (cur) {
		printf ("%c %c\n", prev, cur);
	} else {
		printf ("unknown\n");
		exit (1);
	}

	return 0;
}


/**
 * store:
 * @type: type of entry,
 * @pid: pid for entry,
 * @user: username to store.
 *
 * Write an entry to the utmp and wtmp files, the id and line are always
 * "~~" and "~" respectively.
 **/
static void
store (short       type,
       pid_t       pid,
       const char *user)
{
	struct utmp    utmp;
	struct utsname uts;

	nih_assert (user != NULL);
	nih_assert (strlen (user) > 0);

	memset (&utmp, 0, sizeof (utmp));

	utmp.ut_type = type;
	utmp.ut_pid = pid;

	strcpy (utmp.ut_line, "~");
	strcpy (utmp.ut_id, "~~");
	strncpy (utmp.ut_user, user, sizeof (utmp.ut_user));
	if (uname (&uts) == 0)
		strncpy (utmp.ut_host, uts.release, sizeof (utmp.ut_host));

	gettimeofday ((struct timeval *)&utmp.ut_tv, NULL);

	/* Write utmp entry */
	setutent ();
	pututline (&utmp);
	endutent ();

	/* Write wtmp entry */
	updwtmp (WTMP_FILE, &utmp);
}
