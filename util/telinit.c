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

	connection = dbus_bus_get (DBUS_BUS_SYSTEM, &dbus_error);
	if (! connection) {
		nih_dbus_error_raise (dbus_error.name, dbus_error.message);
		dbus_error_free (&dbus_error);
		return NULL;
	}

	dbus_connection_set_exit_on_disconnect (connection, FALSE);
	dbus_error_free (&dbus_error);

	upstart = nih_dbus_proxy_new (parent, connection,
				      DBUS_SERVICE_UPSTART,
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
	nih_local NihDBusProxy *upstart = NULL;
	DBusPendingCall        *ret;

	upstart = upstart_open (NULL);
	if (! upstart)
		return -1;

	/* Fire and forget:
	 *
	 * Ask Upstart to restart itself using the async interface to
	 * avoid the client-side complaining if and when it detects that
	 * Upstart has severed all connections to perform the re-exec.
	 */
	ret = upstart_restart (upstart, NULL, NULL, NULL, 0);

	/* We don't care about the return code, but we have to keep
	 * the compiler happy.
	 */
	if (ret != (DBusPendingCall *)TRUE)
		return 0;

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
