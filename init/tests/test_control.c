/* upstart
 *
 * test_dbus.c - test suite for init/dbus.c
 *
 * Copyright © 2008 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
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

#include <nih/test.h>

#include <sys/types.h>
#include <sys/wait.h>

#include <dbus/dbus.h>

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/signal.h>
#include <nih/main.h>
#include <nih/error.h>
#include <nih/errors.h>

#include <nih/dbus.h>

#include "control.h"
#include "errors.h"


static int drop_connection = FALSE;
static int refuse_registration = FALSE;
static DBusConnection *server_conn = NULL;

static int
my_connect_handler (DBusServer     *server,
		    DBusConnection *conn)
{
	DBusMessage   *message, *reply;
	char          *id = ":1";
	dbus_uint32_t  request;

	assert (server_conn == NULL);

	dbus_connection_ref (conn);

	/* Expect Hello from the client, return a fake unique name */
	while (! (message = dbus_connection_pop_message (conn)))
		dbus_connection_read_write (conn, -1);

	assert (dbus_message_is_method_call (message, DBUS_INTERFACE_DBUS,
					     "Hello"));

	reply = dbus_message_new_method_return (message);
	assert (reply != NULL);

	dbus_message_append_args (reply,
				  DBUS_TYPE_STRING, &id,
				  DBUS_TYPE_INVALID);

	assert (dbus_connection_send (conn, reply, NULL));
	dbus_connection_flush (conn);

	dbus_message_unref (reply);
	dbus_message_unref (message);

	if (drop_connection) {
		dbus_connection_unref (conn);
		return FALSE;
	}

	request = (refuse_registration ? DBUS_REQUEST_NAME_REPLY_EXISTS
		   : DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);

	/* Expect RequestName from the client, return a fake unique name */
	while (! (message = dbus_connection_pop_message (conn)))
		dbus_connection_read_write (conn, -1);

	assert (dbus_message_is_method_call (message, DBUS_INTERFACE_DBUS,
					     "RequestName"));

	reply = dbus_message_new_method_return (message);
	assert (reply != NULL);

	dbus_message_append_args (reply,
				  DBUS_TYPE_UINT32, &request,
				  DBUS_TYPE_INVALID);

	assert (dbus_connection_send (conn, reply, NULL));
	dbus_connection_flush (conn);

	dbus_message_unref (reply);
	dbus_message_unref (message);

	server_conn = conn;

	dbus_connection_unref (conn);

	return TRUE;
}

void
test_bus_open (void)
{
	NihError *err;
	pid_t     pid;
	int       ret, wait_fd, fd, status;

	TEST_FUNCTION ("control_bus_open");
	err = 0;

	/* Check that control_bus_open() opens a connection to the system bus,
	 * we test this by faking the registration part of the system bus and
	 * making sure everything works.  The control_bus global should be
	 * set to non-NULL.
	 */
	TEST_FEATURE ("with system bus");
	drop_connection = FALSE;
	refuse_registration = FALSE;
	server_conn = NULL;

	TEST_CHILD_WAIT (pid, wait_fd) {
		DBusServer *server;

		nih_signal_set_handler (SIGTERM, nih_signal_handler);
		assert (nih_signal_add_handler (NULL, SIGTERM,
						nih_main_term_signal, NULL));

		server = nih_dbus_server ("unix:abstract=/com/ubuntu/upstart/test",
					  my_connect_handler, NULL);

		TEST_CHILD_RELEASE (wait_fd);

		nih_main_loop ();

		assert (server_conn != NULL);

		dbus_connection_close (server_conn);
		dbus_connection_unref (server_conn);

		dbus_server_disconnect (server);
		dbus_server_unref (server);

		dbus_shutdown ();

		exit (0);
	}

	setenv ("DBUS_SYSTEM_BUS_ADDRESS",
		"unix:abstract=/com/ubuntu/upstart/test", TRUE);

	ret = control_bus_open ();

	TEST_EQ (ret, 0);
	TEST_NE_P (control_bus, NULL);

	dbus_connection_get_unix_fd (control_bus, &fd);
	TEST_TRUE (fcntl (fd, F_GETFD) & FD_CLOEXEC);

	kill (pid, SIGTERM);
	waitpid (pid, &status, 0);
	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	control_bus_close ();

	dbus_shutdown ();

	unsetenv ("DBUS_SYSTEM_BUS_ADDRESS");


	/* Check that if the system bus drops the connection during
	 * registration, control_bus_open returns an error.
	 */
	TEST_FEATURE ("with disconnect during registration");
	drop_connection = TRUE;
	refuse_registration = FALSE;
	server_conn = NULL;

	TEST_CHILD_WAIT (pid, wait_fd) {
		DBusServer *server;

		nih_signal_set_handler (SIGTERM, nih_signal_handler);
		assert (nih_signal_add_handler (NULL, SIGTERM,
						nih_main_term_signal, NULL));

		server = nih_dbus_server ("unix:abstract=/com/ubuntu/upstart/test",
					  my_connect_handler, NULL);

		TEST_CHILD_RELEASE (wait_fd);

		nih_main_loop ();

		assert (server_conn == NULL);

		dbus_server_disconnect (server);
		dbus_server_unref (server);

		dbus_shutdown ();

		exit (0);
	}

	setenv ("DBUS_SYSTEM_BUS_ADDRESS",
		"unix:abstract=/com/ubuntu/upstart/test", TRUE);

	ret = control_bus_open ();

	TEST_LT (ret, 0);
	TEST_EQ_P (control_bus, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_DBUS_ERROR);
	/* Under valgrind we seem to get NoReply instead */
	if (strcmp (((NihDBusError *)err)->name, DBUS_ERROR_NO_REPLY))
		TEST_EQ_STR (((NihDBusError *)err)->name,
			     DBUS_ERROR_DISCONNECTED);
	nih_free (err);

	kill (pid, SIGTERM);
	waitpid (pid, &status, 0);
	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	dbus_shutdown ();

	unsetenv ("DBUS_SYSTEM_BUS_ADDRESS");


	/* Check that if the system bus says our name is already taken,
	 * control_bus_open returns an error.
	 */
	TEST_FEATURE ("with our name already taken");
	drop_connection = FALSE;
	refuse_registration = TRUE;
	server_conn = NULL;

	TEST_CHILD_WAIT (pid, wait_fd) {
		DBusServer *server;

		nih_signal_set_handler (SIGTERM, nih_signal_handler);
		assert (nih_signal_add_handler (NULL, SIGTERM,
						nih_main_term_signal, NULL));

		server = nih_dbus_server ("unix:abstract=/com/ubuntu/upstart/test",
					  my_connect_handler, NULL);

		TEST_CHILD_RELEASE (wait_fd);

		nih_main_loop ();

		assert (server_conn != NULL);

		dbus_connection_close (server_conn);
		dbus_connection_unref (server_conn);

		dbus_server_disconnect (server);
		dbus_server_unref (server);

		dbus_shutdown ();

		exit (0);
	}

	setenv ("DBUS_SYSTEM_BUS_ADDRESS",
		"unix:abstract=/com/ubuntu/upstart/test", TRUE);

	ret = control_bus_open ();

	TEST_LT (ret, 0);
	TEST_EQ_P (control_bus, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, CONTROL_NAME_TAKEN);
	nih_free (err);

	kill (pid, SIGTERM);
	waitpid (pid, &status, 0);
	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	dbus_shutdown ();

	unsetenv ("DBUS_SYSTEM_BUS_ADDRESS");


	/* Check that if the system bus is not listening, control_bus_open
	 * returns NULL and a D-Bus error code.
	 */
	TEST_FEATURE ("with no system bus");
	setenv ("DBUS_SYSTEM_BUS_ADDRESS",
		"unix:abstract=/com/ubuntu/upstart/test", TRUE);

	ret = control_bus_open ();

	TEST_LT (ret, 0);
	TEST_EQ_P (control_bus, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_DBUS_ERROR);
	TEST_EQ_STR (((NihDBusError *)err)->name, DBUS_ERROR_NO_SERVER);
	nih_free (err);

	dbus_shutdown ();

	unsetenv ("DBUS_SYSTEM_BUS_ADDRESS");
}

void
test_bus_disconnected (void)
{
	FILE  *output;
	pid_t  pid;
	int    wait_fd, status;

	/* Check that if the bus is disconnected, control_bus is set back
	 * to NULL automatically.
	 */
	TEST_FUNCTION ("control_bus_disconnected");
	program_name = "test";
	output = tmpfile ();

	drop_connection = FALSE;
	refuse_registration = FALSE;
	server_conn = NULL;

	TEST_CHILD_WAIT (pid, wait_fd) {
		DBusServer *server;

		nih_signal_set_handler (SIGTERM, nih_signal_handler);
		assert (nih_signal_add_handler (NULL, SIGTERM,
						nih_main_term_signal, NULL));

		server = nih_dbus_server ("unix:abstract=/com/ubuntu/upstart/test",
					  my_connect_handler, NULL);

		TEST_CHILD_RELEASE (wait_fd);

		nih_main_loop ();

		assert (server_conn != NULL);

		dbus_connection_close (server_conn);
		dbus_connection_unref (server_conn);

		dbus_server_disconnect (server);
		dbus_server_unref (server);

		dbus_shutdown ();

		exit (0);
	}

	setenv ("DBUS_SYSTEM_BUS_ADDRESS",
		"unix:abstract=/com/ubuntu/upstart/test", TRUE);

	assert0 (control_bus_open ());
	assert (control_bus != NULL);

	kill (pid, SIGTERM);
	waitpid (pid, &status, 0);
	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_DIVERT_STDERR (output) {
		while (control_bus &&
		       dbus_connection_read_write_dispatch (control_bus, -1))
			;
	}
	rewind (output);

	TEST_EQ_P (control_bus, NULL);

	TEST_FILE_EQ (output, "test: Disconnected from system bus\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	fclose (output);

	dbus_shutdown ();

	unsetenv ("DBUS_SYSTEM_BUS_ADDRESS");
}

void
test_bus_close (void)
{
	/* Check that control_bus_close sets control_bus back to NULL. */
	TEST_FUNCTION ("control_bus_close");
	assert0 (control_bus_open ());
	assert (control_bus != NULL);

	control_bus_close ();

	TEST_EQ_P (control_bus, NULL);

	dbus_shutdown ();
}


int
main (int   argc,
      char *argv[])
{
	test_bus_open ();
	test_bus_disconnected ();
	test_bus_close ();

	return 0;
}
