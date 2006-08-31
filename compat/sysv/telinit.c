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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/string.h>
#include <nih/main.h>
#include <nih/option.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <upstart/control.h>


/**
 * options:
 *
 * Command-line options accepted.
 **/
static NihOption options[] = {
	/* Compatibility options, all ignored */
	{ 't', NULL, NULL, NULL, "SECONDS", NULL, NULL },
	{ 'e', NULL, NULL, NULL, "VAR=VAL", NULL, NULL },

	NIH_OPTION_LAST
};


int
main (int   argc,
      char *argv[])
{
	char       **args;
	UpstartMsg   msg;
	int          sock;

	nih_main_init (argv[0]);

	args = nih_option_parser (NULL, argc, argv, options, FALSE);
	if (! args)
		exit (1);

	/* First argument must be a single character we know */
	if ((! args[0]) || (! strchr ("0123456SsQqabcUu", args[0][0]))) {
		fprintf (stderr, _("%s: illegal runlevel: %s"),
			 program_name, args[0]);
		nih_main_suggest_help ();
		exit (1);
	}

	/* Check we're root */
	setuid (geteuid ());
	if (getuid ()) {
		nih_error (_("Need to be root"));
		exit (1);
	}


	/* Build the message */
	msg.type = UPSTART_JOB_START;
	switch (args[0][0]) {
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case 'S':
		msg.job_start.name = nih_sprintf (NULL, "rc%c", args[0][0]);
		break;
	case 's':
		msg.job_start.name = "rcS";
		break;
	case '0':
		msg.job_start.name = "rc0-poweroff";
		break;
	default:
		/* Ignore other arguments */
		exit (0);
	}


	/* Connect to the daemon */
	sock = upstart_open ();
	if (sock < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_error (_("Unable to establish control socket: %s"),
			   err->message);
		exit (1);
	}

	/* Send the message */
	if (upstart_send_msg (sock, &msg) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_error (_("Unable to send message: %s"), err->message);
		exit (1);
	}

	return 0;
}
