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


#include <sys/types.h>

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/main.h>
#include <nih/option.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <nih-dbus/dbus_error.h>
#include <nih-dbus/dbus_proxy.h>
#include <nih-dbus/errors.h>
#include <nih-dbus/dbus_connection.h>

#include "dbus/upstart.h"

#include "com.ubuntu.Upstart.h"

#include "sysv.h"


/* Prototypes for option functions */
int env_option (NihOption *option, const char *arg);

NihDBusProxy * upstart_open (const void *parent)
	__attribute__ ((warn_unused_result));

int restart_upstart (void)
	__attribute__ ((warn_unused_result));


/**
 * extra_env:
 *
 * Extra environment variables to append to the runlevel event.
 **/
char **extra_env = NULL;


/**
 * env_option:
 * @option: NihOption invoked,
 * @arg: argument to parse.
 *
 * This option setter is used to append @arg to the list of environment
 * variables pointed to by the value member of option, which must be a
 * pointer to a char **.
 *
 * The arg_name member of @option must not be NULL.
 *
 * Returns: zero on success, non-zero on error.
 **/
int
env_option (NihOption  *option,
	    const char *arg)
{
	char ***value;

	nih_assert (option != NULL);
	nih_assert (option->value != NULL);
	nih_assert (arg != NULL);

	value = (char ***)option->value;

	NIH_MUST (nih_str_array_add (value, NULL, NULL, arg));

	return 0;
}

/**
 * upstart_open:
 * @parent: parent object for new proxy.
 *
 * Opens a connection to the Upstart init daemon and returns a proxy
 * to the manager object. If @dest_name is not NULL, a connection is
 * instead opened to the system bus and the proxy linked to the
 * well-known name given.
 *
 * If @parent is not NULL, it should be a pointer to another object
 * which will be used as a parent for the returned proxy.  When all
 * parents of the returned proxy are freed, the returned proxy will
 * also be freed.
 *
 * Returns: newly allocated D-Bus proxy or NULL on raised error.
 **/
NihDBusProxy *
upstart_open (const void *parent)
{
	DBusError       dbus_error;
	DBusConnection *connection;
	NihDBusProxy *  upstart;

	dbus_error_init (&dbus_error);

	connection = dbus_connection_open (DBUS_ADDRESS_UPSTART, &dbus_error);
	if (! connection) {
		nih_dbus_error_raise (dbus_error.name, dbus_error.message);
		dbus_error_free (&dbus_error);
		return NULL;
	}

	dbus_connection_set_exit_on_disconnect (connection, FALSE);
	dbus_error_free (&dbus_error);

	upstart = nih_dbus_proxy_new (parent, connection,
				      NULL,
				      DBUS_PATH_UPSTART,
				      NULL, NULL);
	if (! upstart) {
		dbus_connection_unref (connection);
		return NULL;
	}

	upstart->auto_start = FALSE;

	/* Drop initial reference now the proxy holds one */
	dbus_connection_unref (connection);

	return upstart;
}

/**
 * restart_upstart:
 *
 * Request Upstart restart itself.
 *
 * Returns: 0 on SUCCESS, -1 on raised error.
 **/
int
restart_upstart (void)
{
	nih_local NihDBusProxy  *upstart = NULL;
	NihError                *err;
	int                      ret;

	upstart = upstart_open (NULL);
	if (! upstart)
		return -1;

	/* Ask Upstart to restart itself.
	 *
	 * Since it is not possible to serialise a D-Bus connection,
	 * Upstart is forced to sever all D-Bus client connections,
	 * including this one.
	 *
	 * Further, since the user expects telinit to block _until the
	 * re-exec has finished and Upstart is accepting connections
	 * once again_, the only solution is to wait for the forced
	 * disconnect, then poll until it is possible to create a new
	 * connection.
	 *
	 * Note that we don't (can't) care about the return code since
	 * it's not reliable:
	 *
	 * - either the re-exec request completed and D-Bus returned zero
	 *   before Upstart started the re-exec.
	 *
	 * - or the re-exec request completed but upstart started the
	 *   re-exec (severing all D-Bus connections) before D-Bus got a
	 *   chance to finish cleanly meaning we receive a return of -1.
	 *
	 * We cannot know exactly what happened so have to allow for
	 * both scenarios. Note the implicit assumption that the re-exec
	 * request itself was accepted. If this assumption is incorrect
	 * (should not be possible), the worst case scenario is that
	 * upstart does not re-exec and then we quickly drop out of the
	 * reconnect block since it never went offline.
	 */
	ret = upstart_restart_sync (NULL, upstart);

	if (getenv ("UPSTART_TELINIT_U_NO_WAIT")) {
		/* Get-out-of-jail-free card - should never be required, but
		 * paranoia never hurt anyone and we strive to provide
		 * backwards-compatibility where possible.
		 */
		return ret;
	}

	if (ret < 0) {
		err = nih_error_get ();
		nih_free (err);
	}

	nih_free (upstart);

	nih_debug ("Waiting for upstart to finish re-exec");

	/* We believe Upstart is now in the process of
	 * re-exec'ing so attempt forever to reconnect.
	 *
	 * This sounds dangerous but there is no other option,
	 * and a connection must be possible unless the system
	 * is completely broken.
	 */
	while (TRUE) {

		upstart = upstart_open (NULL);
		if (upstart)
			break;

		err = nih_error_get ();
		nih_free (err);

		/* Avoid DoS'ing the system whilst we wait */
		usleep (100000);
	}

	return 0;
}

#ifndef TEST
/**
 * options:
 *
 * Command-line options accepted.
 **/
static NihOption options[] = {
	{ 'e', NULL, N_("set environment variable in the runlevel event"),
	  NULL, "KEY=VALUE", &extra_env, env_option },

	/* Compatibility options, all ignored */
	{ 't', NULL, NULL, NULL, "SECONDS", NULL, NULL },

	NIH_OPTION_LAST
};


int
main (int   argc,
      char *argv[])
{
	char **args;
	int    runlevel;
	int    ret = 0;

	nih_main_init (argv[0]);

	nih_option_set_usage ("RUNLEVEL");
	nih_option_set_synopsis (_("Change runlevel."));
	nih_option_set_help (
		_("RUNLEVEL should be one of 0123456sS, where s and S are "
		  "considered identical.\n"
		  "\n"
		  "RUNLEVEL may also be Q or q to instruct the init daemon "
		  "to reload its configuration, this is rarely necessary "
		  "since the daemon watches its configuration for changes.\n"
		  "\n"
		  "RUNLEVEL may be U or u to instruct the init daemon to "
		  "re-execute itself, this is not recommended since Upstart "
		  "does not currently preserve its state.\n"));

	args = nih_option_parser (NULL, argc, argv, options, FALSE);
	if (! args)
		exit (1);

	/* First argument must be a single character we know */
	if (! args[0]) {
		fprintf (stderr, _("%s: missing runlevel\n"), program_name);
		nih_main_suggest_help ();
		exit (1);
	}
	if ((! strchr ("0123456SsQqUu", args[0][0])) || args[0][1]) {
		fprintf (stderr, _("%s: illegal runlevel: %s\n"),
			 program_name, args[0]);
		nih_main_suggest_help ();
		exit (1);
	}

	/* Check we're root */
	if (setuid (geteuid ()) < 0)
	    nih_warn (_("Couldn't set uid."));

	if (getuid ()) {
		nih_fatal (_("Need to be root"));
		exit (1);
	}

	/* Send the appropriate message */
	runlevel = args[0][0];

	switch (runlevel) {
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
		ret = sysv_change_runlevel (runlevel, extra_env, NULL, NULL);
		break;
	case 'S':
	case 's':
		ret = sysv_change_runlevel ('S', extra_env, NULL, NULL);
		break;
	case 'Q':
	case 'q':
		ret = kill (1, SIGHUP);
		if (ret < 0)
			nih_error_raise_system ();
		break;
	case 'U':
	case 'u':
		/* If /sbin/init is not Upstart, just exit non-zero */
		ret = restart_upstart ();
		break;
	default:
		nih_assert_not_reached ();
	}

	if (ret < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_error ("%s", err->message);
		nih_free (err);

		exit (1);
	}

	return 0;
}
#endif
