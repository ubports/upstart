/* upstart
 *
 * Copyright © 2010 Canonical Ltd.
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


#include <linux/reboot.h>
#include <sys/reboot.h>
#include <sys/syscall.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/main.h>
#include <nih/option.h>
#include <nih/logging.h>
#include <nih/error.h>

#include "utmp.h"


/**
 * DEV:
 *
 * Directory containing device nodes.
 **/
#ifndef DEV
#define DEV "/dev"
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
 * SHUTDOWN:
 *
 * Program to call when not called with -f.
 **/
#ifndef SHUTDOWN
#define SHUTDOWN SBINDIR "/shutdown"
#endif


/* Operation modes */
enum {
	REBOOT,
	HALT,
	POWEROFF,
	REBOOTCOMMAND,
};


/**
 * no_sync:
 *
 * TRUE to suppress the call to sync() before reboot().
 **/
static int no_sync = FALSE;

/**
 * force:
 *
 * TRUE to behave as if we're called by shutdown.
 **/
static int force = FALSE;

/**
 * poweroff:
 *
 * TRUE if the power should be switched off.
 **/
static int poweroff = FALSE;

/**
 * exit_only:
 *
 * TRUE if we should exit immediately.
 **/
static int exit_only = FALSE;


/**
 * options:
 *
 * Command-line options accepted.
 **/
static NihOption options[] = {
	{ 'n', "no-sync", N_("don't sync before reboot or halt"),
	  NULL, NULL, &no_sync, NULL },
	{ 'f', "force", N_("force reboot or halt, don't call shutdown(8)"),
	  NULL, NULL, &force, NULL },
	{ 'p', "poweroff", N_("switch off the power when called as halt"),
	  NULL, NULL, &poweroff, NULL },
	{ 'w', "wtmp-only", N_("don't actually reboot or halt, just write wtmp record"),
	  NULL, NULL, &exit_only, NULL },

	/* Compatibility options, all ignored */
	{ 'd', NULL, NULL, NULL, NULL, NULL, NULL },
	{ 'i', NULL, NULL, NULL, NULL, NULL, NULL },
	{ 'h', NULL, NULL, NULL, NULL, NULL, NULL },

	NIH_OPTION_LAST
};


int
main (int   argc,
      char *argv[])
{
	char **args;
	int    mode;
	int    runlevel;
	char  *rebootcommand=NULL;

	nih_main_init (argv[0]);

	mode = REBOOT;
	if (! strcmp (program_name, "halt")) {
		mode = HALT;
		nih_option_set_synopsis (_("Halt the system."));
	} else if (! strcmp (program_name, "poweroff")) {
		mode = POWEROFF;
		nih_option_set_synopsis (_("Power off the system."));
	} else {
		mode = REBOOT;
		nih_option_set_synopsis (_("Reboot the system."));
	}

	nih_option_set_help (
		_("This command is intended to instruct the kernel "
		  "to reboot or halt the system; when run without the -f "
		  "option, or when in a system runlevel other than 0 or 6, "
		  "it will actually execute /sbin/shutdown.\n"));

	args = nih_option_parser (NULL, argc, argv, options, FALSE);
	if (! args)
		exit (1);

	/* Check we're root */
	setuid (geteuid ());
	if (getuid ()) {
		nih_fatal (_("Need to be root"));
		exit (1);
	}

	/* If the system runlevel is 0 or 6, we always behave as if --force
	 * were given.
	 */
	runlevel = utmp_get_runlevel (NULL, NULL);
	if (runlevel < 0) {
		nih_free (nih_error_get ());
	} else if ((runlevel == '0') || (runlevel == '6')) {
		force = TRUE;
	}

	/* Check for -p if halt */
	if ((mode == HALT) && poweroff)
		mode = POWEROFF;

	/* Check for rebootcommand to pass in the syscall */
	if ((mode == REBOOT) && args && *args) {
		mode = REBOOTCOMMAND;
		rebootcommand = *args;
	}

	/* Normally we just exec shutdown, which notifies everyone and
	 * signals init.
	 */
	if ((! force) && (! exit_only)) {
		char *args[5];
		int   i = 0;

		args[i++] = SHUTDOWN;

		switch (mode) {
		case REBOOT:
			args[i++] = "-r";
			break;
		case HALT:
			args[i++] = "-h";
			args[i++] = "-H";
			break;
		case POWEROFF:
			args[i++] = "-h";
			args[i++] = "-P";
			break;
		}

		args[i++] = "now";
		args[i] = NULL;

		nih_info (_("Calling shutdown"));
		execv (args[0], args);

		nih_fatal (_("Unable to execute shutdown: %s"),
			   strerror (errno));
		exit (1);
	}

	/* Write the shutdown record */
	if (utmp_write_shutdown (NULL, NULL) < 0)
		nih_free (nih_error_get ());

	if (exit_only)
		exit (0);

	if (! no_sync)
		sync ();

	/* Re-enable Control-Alt-Delete in case it breaks */
	reboot (RB_ENABLE_CAD);

	/* Do the syscall */
	switch (mode) {
	case REBOOT:
		nih_info (_("Rebooting"));
		reboot (RB_AUTOBOOT);
		break;
	case HALT:
		nih_info (_("Halting"));
		reboot (RB_HALT_SYSTEM);
		break;
	case POWEROFF:
		nih_info (_("Powering off"));
		reboot (RB_POWER_OFF);
		break;
	case REBOOTCOMMAND:
		nih_info (_("Rebooting with %s"), rebootcommand);
		syscall (SYS_reboot,
	                 LINUX_REBOOT_MAGIC1,
			 LINUX_REBOOT_MAGIC2,
			 LINUX_REBOOT_CMD_RESTART2,
			 rebootcommand);
	}

	/* Shouldn't get here, but if we do, carry on */
	reboot (RB_DISABLE_CAD);

	return 0;
}
