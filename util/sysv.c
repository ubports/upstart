/* upstart
 *
 * sysv.c - System V compatibility
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


#include <dbus/dbus.h>

#include <nih-dbus/dbus_error.h>
#include <nih-dbus/dbus_connection.h>
#include <nih-dbus/dbus_proxy.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/logging.h>
#include <nih/error.h>

#include "dbus/upstart.h"

#include "utmp.h"
#include "sysv.h"

#include "com.ubuntu.Upstart.h"


/**
 * RUNLEVEL_EVENT:
 *
 * Name of the event we emit on a runlevel change.
 **/
#define RUNLEVEL_EVENT "runlevel"


/* Prototypes for static functions */
static void error_handler (NihError **err, NihDBusMessage *message);


/**
 * dest_address:
 *
 * Address for private D-Bus connection.
 **/
const char *dest_address = DBUS_ADDRESS_UPSTART;


/**
 * sysv_change_runlevel:
 * @runlevel: new runlevel,
 * @extra_env: NULL-terminated array of additional environment.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
sysv_change_runlevel (int           runlevel,
		      char * const *extra_env,
		      const char *  utmp_file,
		      const char *  wtmp_file)
{
	int                     prevlevel;
	DBusError               dbus_error;
	DBusConnection *        connection;
	nih_local NihDBusProxy *upstart = NULL;
	nih_local char **       env = NULL;
	char *                  e;
	DBusPendingCall *       pending_call;
	NihError *              err;

	nih_assert (runlevel > 0);

	/* Get the previous runlevel from the environment or utmp */
	prevlevel = utmp_get_runlevel (utmp_file, NULL);
	if (prevlevel < 0) {
		nih_free (nih_error_get ());

		prevlevel = 'N';
	}

	/* Connect to Upstart via the private socket, establish a proxy and
	 * drop the initial connection reference since the proxy will hold
	 * one.
	 */
	dbus_error_init (&dbus_error);
	connection = dbus_connection_open (dest_address, &dbus_error);
	if (! connection) {
		nih_dbus_error_raise (dbus_error.name, dbus_error.message);
		dbus_error_free (&dbus_error);
		return -1;
	}
	dbus_error_free (&dbus_error);

	upstart = nih_dbus_proxy_new (NULL, connection,
				      NULL, DBUS_PATH_UPSTART,
				      NULL, NULL);
	if (! upstart) {
		dbus_connection_unref (connection);
		return -1;
	}

	upstart->auto_start = FALSE;

	dbus_connection_unref (connection);

	/* Construct the environment to the event, which must include the
	 * new runlevel and previous runlevel as the first two arguments
	 * followed by any additional environment.
	 */
	env = nih_str_array_new (NULL);
	if (! env)
		nih_return_no_memory_error (-1);

	e = nih_sprintf (NULL, "RUNLEVEL=%c", runlevel);
	if (! e)
		nih_return_no_memory_error (-1);

	if (! nih_str_array_addp (&env, NULL, NULL, e)) {
		nih_error_raise_no_memory ();
		nih_free (e);
		return -1;
	}

	e = nih_sprintf (NULL, "PREVLEVEL=%c", prevlevel);
	if (! e)
		nih_return_no_memory_error (-1);

	if (! nih_str_array_addp (&env, NULL, NULL, e)) {
		nih_error_raise_no_memory ();
		nih_free (e);
		return -1;
	}

	if (extra_env) {
		if (! nih_str_array_append (&env, NULL, NULL, extra_env))
			nih_return_no_memory_error (-1);
	}

	/* Write out the new runlevel record to utmp and wtmp, do this
	 * before calling EmitEvent so that the records are correct.
	 */
	if (utmp_write_runlevel (utmp_file, wtmp_file,
				 runlevel, prevlevel) < 0)
		nih_free (nih_error_get ());

	/* Make the EmitEvent call, we don't wait for the event to finish
	 * because sysvinit never did.
	 */
	err = NULL;
	pending_call = NIH_SHOULD (upstart_emit_event (
					   upstart, "runlevel", env, FALSE,
					   NULL,
					   (NihDBusErrorHandler)error_handler,
					   &err,
					   NIH_DBUS_TIMEOUT_NEVER));
	if (! pending_call)
		return -1;

	dbus_pending_call_block (pending_call);
	dbus_pending_call_unref (pending_call);

	if (err) {
		nih_error_raise_error (err);
		return -1;
	}

	return 0;
}

/**
 * error_handler:
 * @err: pointer to store error into,
 * @message: D-Bus message received.
 *
 * This function is called in the event of an error from a D-Bus method
 * call, it stashes the raised error in @err.
 **/
static void
error_handler (NihError **     err,
	       NihDBusMessage *message)
{
	*err = nih_error_steal ();
}
