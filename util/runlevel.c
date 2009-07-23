/* upstart
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


#include <utmpx.h>
#include <errno.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/main.h>
#include <nih/option.h>
#include <nih/logging.h>
#include <nih/error.h>

#include "utmp.h"


/**
 * options:
 *
 * Command-line options accepted.
 **/
static NihOption options[] = {
	NIH_OPTION_LAST
};


int
main (int   argc,
      char *argv[])
{
	char **args;
	int    runlevel;
	int    prevlevel;

	nih_main_init (argv[0]);

	nih_option_set_usage (_("[UTMP]"));
	nih_option_set_synopsis (_("Output previous and current runlevel."));
	nih_option_set_help (
		_("The system /var/run/utmp file is used unless the alternate "
		  "file UTMP is given.\n"));

	args = nih_option_parser (NULL, argc, argv, options, FALSE);
	if (! args)
		exit (1);

	runlevel = NIH_SHOULD (utmp_get_runlevel (args[0], &prevlevel));
	if (runlevel < 0) {
		NihError *err;

		err = nih_error_get ();
		if (err->number == ESRCH) {
			nih_message ("unknown");
		} else {
			nih_error ("%s: %s", args[0] ?: _PATH_UTMPX,
				   err->message);
		}

		nih_free (err);
		exit (1);
	} else if (runlevel == 'N') {
		nih_message ("unknown");
		exit (1);
	}

	nih_message ("%c %c", prevlevel, runlevel);

	return 0;
}
