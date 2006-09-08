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

#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/signal.h>
#include <nih/main.h>
#include <nih/option.h>
#include <nih/logging.h>


/**
 * daemonise:
 *
 * This is set to %TRUE if we should become a daemon, rather than just
 * running in the foreground.
 **/
static int daemonise = FALSE;

/**
 * options:
 *
 * Command-line options accepted for all arguments.
 **/
static NihOption options[] = {
	{ 0, "daemon", N_("Detach and run in the background"),
	  NULL, NULL, &daemonise, NULL },

	NIH_OPTION_LAST
};


int
main (int   argc,
      char *argv[])
{
	char **args;
	int    ret;

	nih_main_init (argv[0]);

	args = nih_option_parser (NULL, argc, argv, options, FALSE);
	if (! args)
		exit (1);

	if (args[0] != NULL) {
		fprintf (stderr, _("%s: unexpected argument\n"), program_name);
		nih_main_suggest_help ();
		exit (1);
	}

	/* Check we're root */
	if (getuid ()) {
		nih_error (_("Need to be root"));
		exit (1);
	}

	/* Become daemon */
	if (daemonise)
		nih_main_daemonise ();


	/* Send all logging output to syslog */
	openlog (program_name, LOG_CONS | LOG_PID, LOG_DAEMON);
	nih_log_set_logger (nih_logger_syslog);

	/* Handle TERM signal gracefully */
	nih_signal_set_handler (SIGTERM, nih_signal_handler);
	nih_signal_add_callback (NULL, SIGTERM, nih_main_term_signal, NULL);

	ret = nih_main_loop ();

	return ret;
}
