/* upstart
 *
 * Copyright © 2007 Canonical Ltd.
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
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/io.h>
#include <nih/main.h>
#include <nih/option.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <upstart/message.h>


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
	char         **args;
	NihIoMessage  *message;
	int            sock;

	nih_main_init (argv[0]);

	nih_option_set_usage ("RUNLEVEL");
	nih_option_set_synopsis (_("Change runlevel."));
	nih_option_set_help (
		_("RUNLEVEL should be one of 0123456S."));

	args = nih_option_parser (NULL, argc, argv, options, FALSE);
	if (! args)
		exit (1);

	/* First argument must be a single character we know */
	if ((! args[0]) || (! strchr ("0123456SsQqabcUu", args[0][0]))
	    || args[0][1]) {
		fprintf (stderr, _("%s: illegal runlevel: %s\n"),
			 program_name, args[0]);
		nih_main_suggest_help ();
		exit (1);
	}

	/* Ignore further arguments */
	args[1] = NULL;

	/* Check we're root */
	setuid (geteuid ());
	if (getuid ()) {
		nih_error (_("Need to be root"));
		exit (1);
	}


	/* Build the message */
	switch (args[0][0]) {
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
		NIH_MUST (message = upstart_message_new (
				  NULL, UPSTART_INIT_DAEMON,
				  UPSTART_EVENT_EMIT, "runlevel", args, NULL));
		break;
	case 'S':
	case 's':
		args[0][0] = 'S';
		NIH_MUST (message = upstart_message_new (
				  NULL, UPSTART_INIT_DAEMON,
				  UPSTART_EVENT_EMIT, "runlevel", args, NULL));
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
	if (nih_io_message_send (message, sock) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_error (_("Unable to send message: %s"), err->message);
		exit (1);
	}

	return 0;
}
