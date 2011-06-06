/* upstart
 *
 * test_dbus.c - test suite for init/dbus.c
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

#include <nih/test.h>
#include <nih-dbus/test_dbus.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <dbus/dbus.h>

#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/string.h>
#include <nih/signal.h>
#include <nih/timer.h>
#include <nih/io.h>
#include <nih/main.h>
#include <nih/logging.h>
#include <nih/error.h>
#include <nih/errors.h>

#include <nih-dbus/dbus_error.h>
#include <nih-dbus/dbus_connection.h>
#include <nih-dbus/dbus_object.h>
#include <nih-dbus/errors.h>

#include "dbus/upstart.h"

#include "blocked.h"
#include "job_class.h"
#include "job.h"
#include "conf.h"
#include "control.h"
#include "errors.h"


extern const char *control_server_address;


void
test_server_open (void)
{
	NihError           *err;
	const char *        address;
	struct sockaddr_un  addr;
	socklen_t           addrlen;
	int                 ret, fd;

	TEST_FUNCTION ("control_server_open");
	program_name = "test";
	control_init ();
	nih_io_init ();

	control_server_address = "unix:abstract=/com/ubuntu/upstart/test";

	/* Check that control_server_open() creates a new listening D-Bus
	 * server and sets the control_server global.
	 */
	TEST_FEATURE ("with expected success");
	assert (NIH_LIST_EMPTY (nih_io_watches));

	ret = control_server_open ();

	TEST_EQ (ret, 0);
	TEST_NE_P (control_server, NULL);

	/* D-Bus provides no method to obtain the fd of the server, so
	 * we have to steal it.
	 */
	assert (! NIH_LIST_EMPTY (nih_io_watches));
	fd = ((NihIoWatch *)nih_io_watches->next)->fd;

	TEST_TRUE (fcntl (fd, F_GETFD) & FD_CLOEXEC);

	TEST_TRUE (dbus_server_get_is_connected (control_server));

	control_server_close ();

	dbus_shutdown ();


	/* Check that if something else is already listening on that address,
	 * control_server_open returns NULL and a D-Bus error code.
	 */
	TEST_FEATURE ("with already listening");
	fd = socket (PF_UNIX, SOCK_STREAM, 0);
	assert (fd >= 0);

	address = "unix:abstract=/com/ubuntu/upstart/test";
	assert (address = strchr (address, '/'));

	addr.sun_family = AF_UNIX;
	addr.sun_path[0] = '\0';
	strncpy (addr.sun_path + 1, address,
		 sizeof (addr.sun_path) - 1);

	addrlen = offsetof (struct sockaddr_un, sun_path) + 1;
	addrlen += strlen (address);

	assert0 (bind (fd, &addr, addrlen));

	assert0 (listen (fd, SOMAXCONN));

	ret = control_server_open ();

	TEST_LT (ret, 0);
	TEST_EQ_P (control_server, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_DBUS_ERROR);
	TEST_EQ_STR (((NihDBusError *)err)->name, DBUS_ERROR_ADDRESS_IN_USE);
	nih_free (err);

	dbus_shutdown ();

	close (fd);
}

void
test_server_connect (void)
{
	NihListEntry   *entry;
	NihDBusObject  *object;
	DBusConnection *conn;
	JobClass       *class1, *class2;
	Job            *job1, *job2;
	pid_t           pid;
	int             fd, wait_fd, status;

	TEST_FUNCTION ("control_server_connect");
	program_name = "test";
	control_init ();
	nih_io_init ();

	control_server_address = "unix:abstract=/com/ubuntu/upstart/test";

	assert0 (control_server_open ());
	assert (NIH_LIST_EMPTY (control_conns));


	/* Check that a new connection to our server is accepted and stored
	 * in the connections list, with the manager object registered on
	 * it.
	 */
	TEST_FEATURE ("with no jobs");
	TEST_CHILD_WAIT (pid, wait_fd) {
		DBusConnection *conn;

		control_server_close ();

		nih_signal_set_handler (SIGTERM, nih_signal_handler);
		assert (nih_signal_add_handler (NULL, SIGTERM,
						nih_main_term_signal, NULL));

		conn = nih_dbus_connect ("unix:abstract=/com/ubuntu/upstart/test", NULL);
		assert (conn != NULL);

		TEST_CHILD_RELEASE (wait_fd);

		nih_main_loop ();

		dbus_connection_unref (conn);

		dbus_shutdown ();

		exit (0);
	}

	assert (nih_timer_add_timeout (NULL, 1,
				       (NihTimerCb)nih_main_term_signal, NULL));

	nih_main_loop ();

	TEST_LIST_NOT_EMPTY (control_conns);
	entry = (NihListEntry *)control_conns->next;

	TEST_ALLOC_SIZE (entry, sizeof (NihListEntry));
	TEST_NE_P (entry->data, NULL);

	conn = entry->data;

	dbus_connection_get_unix_fd (conn, &fd);
	TEST_TRUE (fcntl (fd, F_GETFD) & FD_CLOEXEC);

	TEST_TRUE (dbus_connection_get_object_path_data (conn,
							 DBUS_PATH_UPSTART,
							 (void **)&object));

	TEST_ALLOC_SIZE (object, sizeof (NihDBusObject));
	TEST_EQ_STR (object->path, DBUS_PATH_UPSTART);
	TEST_EQ_P (object->data, NULL);

	kill (pid, SIGTERM);
	waitpid (pid, &status, 0);
	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	dbus_connection_close (conn);
	dbus_connection_unref (conn);

	nih_free (entry);


	/* Check that when there are existing jobs and instances, the
	 * new connection has them automatically registered.
	 */
	TEST_FEATURE ("with existing jobs");
	class1 = job_class_new (NULL, "foo");
	nih_hash_add (job_classes, &class1->entry);

	class2 = job_class_new (NULL, "bar");
	job1 = job_new (class2, "test1");
	job2 = job_new (class2, "test2");
	nih_hash_add (job_classes, &class2->entry);

	TEST_CHILD_WAIT (pid, wait_fd) {
		DBusConnection *conn;

		control_server_close ();

		nih_signal_set_handler (SIGTERM, nih_signal_handler);
		assert (nih_signal_add_handler (NULL, SIGTERM,
						nih_main_term_signal, NULL));

		conn = nih_dbus_connect ("unix:abstract=/com/ubuntu/upstart/test", NULL);
		assert (conn != NULL);

		TEST_CHILD_RELEASE (wait_fd);

		nih_main_loop ();

		dbus_connection_unref (conn);

		dbus_shutdown ();

		exit (0);
	}

	assert (nih_timer_add_timeout (NULL, 1,
				       (NihTimerCb)nih_main_term_signal, NULL));

	nih_main_loop ();

	TEST_LIST_NOT_EMPTY (control_conns);
	entry = (NihListEntry *)control_conns->next;

	TEST_ALLOC_SIZE (entry, sizeof (NihListEntry));
	TEST_NE_P (entry->data, NULL);

	conn = entry->data;

	dbus_connection_get_unix_fd (conn, &fd);
	TEST_TRUE (fcntl (fd, F_GETFD) & FD_CLOEXEC);

	TEST_TRUE (dbus_connection_get_object_path_data (conn,
							 DBUS_PATH_UPSTART,
							 (void **)&object));

	TEST_ALLOC_SIZE (object, sizeof (NihDBusObject));
	TEST_EQ_STR (object->path, DBUS_PATH_UPSTART);
	TEST_EQ_P (object->data, NULL);

	TEST_TRUE (dbus_connection_get_object_path_data (conn,
							 DBUS_PATH_UPSTART "/jobs/foo",
							 (void **)&object));

	TEST_ALLOC_SIZE (object, sizeof (NihDBusObject));
	TEST_EQ_STR (object->path, DBUS_PATH_UPSTART "/jobs/foo");
	TEST_EQ_P (object->data, class1);

	TEST_TRUE (dbus_connection_get_object_path_data (conn,
							 DBUS_PATH_UPSTART "/jobs/bar",
							 (void **)&object));

	TEST_ALLOC_SIZE (object, sizeof (NihDBusObject));
	TEST_EQ_STR (object->path, DBUS_PATH_UPSTART "/jobs/bar");
	TEST_EQ_P (object->data, class2);

	TEST_TRUE (dbus_connection_get_object_path_data (conn,
							 DBUS_PATH_UPSTART "/jobs/bar/test1",
							 (void **)&object));

	TEST_ALLOC_SIZE (object, sizeof (NihDBusObject));
	TEST_EQ_STR (object->path, DBUS_PATH_UPSTART "/jobs/bar/test1");
	TEST_EQ_P (object->data, job1);

	TEST_TRUE (dbus_connection_get_object_path_data (conn,
							 DBUS_PATH_UPSTART "/jobs/bar/test2",
							 (void **)&object));

	TEST_ALLOC_SIZE (object, sizeof (NihDBusObject));
	TEST_EQ_STR (object->path, DBUS_PATH_UPSTART "/jobs/bar/test2");
	TEST_EQ_P (object->data, job2);

	kill (pid, SIGTERM);
	waitpid (pid, &status, 0);
	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	dbus_connection_close (conn);
	dbus_connection_unref (conn);

	nih_free (entry);

	nih_free (class1);
	nih_free (class2);


	control_server_close ();

	dbus_shutdown ();
}

void
test_server_close (void)
{
	/* Check that control_server_close sets control_server back to
	 * NULL, as well as disconnected and unreferencing the server.
	 */
	TEST_FUNCTION ("control_server_close");
	control_init ();

	control_server_address = "unix:abstract=/com/ubuntu/upstart/test";

	assert0 (control_server_open ());
	assert (control_server != NULL);

	control_server_close ();

	TEST_EQ_P (control_server, NULL);

	dbus_shutdown ();
}


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
	TEST_DBUS_MESSAGE (conn, message);
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
	TEST_DBUS_MESSAGE (conn, message);
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
	FILE          *output;
	JobClass      *class1, *class2;
	Job           *job1, *job2;
	NihError      *err;
	NihListEntry  *entry;
	NihDBusObject *object;
	pid_t          pid;
	int            ret, wait_fd, fd, status;

	TEST_FUNCTION ("control_bus_open");
	program_name = "test";
	control_init ();
	output = tmpfile ();
	err = 0;


	/* Check that control_bus_open() opens a connection to the system bus,
	 * we test this by faking the registration part of the system bus and
	 * making sure everything works.  The control_bus global should be
	 * set to non-NULL and also stored in the connections list.
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

	TEST_LIST_NOT_EMPTY (control_conns);
	entry = (NihListEntry *)control_conns->next;

	TEST_ALLOC_SIZE (entry, sizeof (NihListEntry));
	TEST_EQ_P (entry->data, control_bus);

	dbus_connection_get_unix_fd (control_bus, &fd);
	TEST_TRUE (fcntl (fd, F_GETFD) & FD_CLOEXEC);

	TEST_TRUE (dbus_connection_get_object_path_data (control_bus,
							 DBUS_PATH_UPSTART,
							 (void **)&object));

	TEST_ALLOC_SIZE (object, sizeof (NihDBusObject));
	TEST_EQ_STR (object->path, DBUS_PATH_UPSTART);
	TEST_EQ_P (object->data, NULL);

	kill (pid, SIGTERM);
	waitpid (pid, &status, 0);
	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_DIVERT_STDERR (output) {
		control_bus_close ();
	}
	rewind (output);

	dbus_shutdown ();

	unsetenv ("DBUS_SYSTEM_BUS_ADDRESS");


	/* Check that existing jobs and instances are registered on the
	 * new bus connection.  This inherently checks that this does not
	 * cause signals to be emitted because our fake server expects the
	 * first message to be a request name method.
	 */
	TEST_FEATURE ("with existing jobs");
	drop_connection = FALSE;
	refuse_registration = FALSE;
	server_conn = NULL;

	class1 = job_class_new (NULL, "foo");
	nih_hash_add (job_classes, &class1->entry);

	class2 = job_class_new (NULL, "bar");
	job1 = job_new (class2, "test1");
	job2 = job_new (class2, "test2");
	nih_hash_add (job_classes, &class2->entry);

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

	TEST_LIST_NOT_EMPTY (control_conns);
	entry = (NihListEntry *)control_conns->next;

	TEST_ALLOC_SIZE (entry, sizeof (NihListEntry));
	TEST_EQ_P (entry->data, control_bus);

	TEST_TRUE (dbus_connection_get_object_path_data (control_bus,
							 DBUS_PATH_UPSTART,
							 (void **)&object));

	TEST_ALLOC_SIZE (object, sizeof (NihDBusObject));
	TEST_EQ_STR (object->path, DBUS_PATH_UPSTART);
	TEST_EQ_P (object->data, NULL);

	TEST_TRUE (dbus_connection_get_object_path_data (control_bus,
							 DBUS_PATH_UPSTART "/jobs/foo",
							 (void **)&object));

	TEST_ALLOC_SIZE (object, sizeof (NihDBusObject));
	TEST_EQ_STR (object->path, DBUS_PATH_UPSTART "/jobs/foo");
	TEST_EQ_P (object->data, class1);

	TEST_TRUE (dbus_connection_get_object_path_data (control_bus,
							 DBUS_PATH_UPSTART "/jobs/bar",
							 (void **)&object));

	TEST_ALLOC_SIZE (object, sizeof (NihDBusObject));
	TEST_EQ_STR (object->path, DBUS_PATH_UPSTART "/jobs/bar");
	TEST_EQ_P (object->data, class2);

	TEST_TRUE (dbus_connection_get_object_path_data (control_bus,
							 DBUS_PATH_UPSTART "/jobs/bar/test1",
							 (void **)&object));

	TEST_ALLOC_SIZE (object, sizeof (NihDBusObject));
	TEST_EQ_STR (object->path, DBUS_PATH_UPSTART "/jobs/bar/test1");
	TEST_EQ_P (object->data, job1);

	TEST_TRUE (dbus_connection_get_object_path_data (control_bus,
							 DBUS_PATH_UPSTART "/jobs/bar/test2",
							 (void **)&object));

	TEST_ALLOC_SIZE (object, sizeof (NihDBusObject));
	TEST_EQ_STR (object->path, DBUS_PATH_UPSTART "/jobs/bar/test2");
	TEST_EQ_P (object->data, job2);

	kill (pid, SIGTERM);
	waitpid (pid, &status, 0);
	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_DIVERT_STDERR (output) {
		control_bus_close ();
	}
	rewind (output);

	dbus_shutdown ();

	unsetenv ("DBUS_SYSTEM_BUS_ADDRESS");

	nih_free (class1);
	nih_free (class2);


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

	TEST_LIST_EMPTY (control_conns);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_DBUS_ERROR);
	/* Under valgrind we seem to get NoReply instead */
	if (strcmp (((NihDBusError *)err)->name, DBUS_ERROR_NO_REPLY)) {
		TEST_EQ_STR (((NihDBusError *)err)->name,
			     DBUS_ERROR_DISCONNECTED);
	}
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

	TEST_LIST_EMPTY (control_conns);

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

	TEST_LIST_EMPTY (control_conns);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_DBUS_ERROR);
	TEST_EQ_STR (((NihDBusError *)err)->name, DBUS_ERROR_NO_SERVER);
	nih_free (err);

	dbus_shutdown ();

	unsetenv ("DBUS_SYSTEM_BUS_ADDRESS");
}

void
test_bus_close (void)
{
	FILE         *output;
	NihListEntry *entry;
	pid_t         dbus_pid;

	/* Check that control_bus_close sets control_bus back to NULL
	 * and is removed from the list of active connections.
	 */
	TEST_FUNCTION ("control_bus_close");
	control_init ();
	output = tmpfile ();

	TEST_DBUS (dbus_pid);

	assert (NIH_LIST_EMPTY (control_conns));

	assert0 (control_bus_open ());
	assert (control_bus != NULL);

	assert (! NIH_LIST_EMPTY (control_conns));
	entry = (NihListEntry *)control_conns->next;

	TEST_FREE_TAG (entry);

	TEST_DIVERT_STDERR (output) {
		control_bus_close ();
	}
	rewind (output);

	TEST_EQ_P (control_bus, NULL);

	TEST_FREE (entry);

	TEST_LIST_EMPTY (control_conns);

	TEST_FILE_EQ (output, "test: Disconnected from system bus\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	fclose (output);

	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}


void
test_disconnected (void)
{
	FILE         *output;
	NihListEntry *entry;
	pid_t         dbus_pid;

	/* Check that if the bus connection is disconnected, control_bus is
	 * set back to NULL automatically.
	 */
	TEST_FUNCTION ("control_disconnected");
	program_name = "test";
	output = tmpfile ();
	control_init ();

	drop_connection = FALSE;
	refuse_registration = FALSE;
	server_conn = NULL;

	assert (NIH_LIST_EMPTY (control_conns));

	TEST_DBUS (dbus_pid);

	assert0 (control_bus_open ());
	assert (control_bus != NULL);

	assert (! NIH_LIST_EMPTY (control_conns));
	entry = (NihListEntry *)control_conns->next;

	TEST_FREE_TAG (entry);

	TEST_DBUS_END (dbus_pid);

	TEST_DIVERT_STDERR (output) {
		while (control_bus &&
		       dbus_connection_read_write_dispatch (control_bus, -1))
			;
	}
	rewind (output);

	TEST_EQ_P (control_bus, NULL);

	TEST_FREE (entry);

	TEST_LIST_EMPTY (control_conns);

	TEST_FILE_EQ (output, "test: Disconnected from system bus\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	fclose (output);

	dbus_shutdown ();
}


void
test_reload_configuration (void)
{
	FILE           *f;
	ConfSource     *source1, *source2, *source3;
	char            dirname[PATH_MAX], filename[PATH_MAX];
	NihDBusMessage *message;
	int             ret;

	/* Check that we can ask the daemon to reload its configuration;
	 * there's no need to simulate this deeply, we just set up a config
	 * and then reload it and see whether it turned up.
	 */
	TEST_FUNCTION ("control_reload_configuration");
	TEST_FILENAME (dirname);
	mkdir (dirname, 0755);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");
	source1 = conf_source_new (NULL, filename, CONF_FILE);

	f = fopen (filename, "w");
	fprintf (f, "#empty\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/bar");
	mkdir (filename, 0755);

	source2 = conf_source_new (NULL, filename, CONF_JOB_DIR);

	strcpy (filename, dirname);
	strcat (filename, "/bar/bar.conf");

	f = fopen (filename, "w");
	fprintf (f, "script\n");
	fprintf (f, "  echo\n");
	fprintf (f, "end script\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/baz");
	source3 = conf_source_new (NULL, filename, CONF_DIR);


	message = nih_new (NULL, NihDBusMessage);
	message->connection = NULL;
	message->message = NULL;

	ret = control_reload_configuration (NULL, message);

	TEST_EQ (ret, 0);

	nih_free (message);

	TEST_HASH_NOT_EMPTY (source1->files);

	TEST_HASH_NOT_EMPTY (source2->files);

	TEST_HASH_EMPTY (source3->files);

	nih_free (source1);
	nih_free (source2);
	nih_free (source3);


	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");
	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/bar/bar.conf");
	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/bar");
	rmdir (filename);

	rmdir (dirname);
}


void
test_get_job_by_name (void)
{
	NihDBusMessage *message = NULL;
	JobClass       *class;
	char           *path;
	NihError       *error;
	NihDBusError   *dbus_error;
	int             ret;

	TEST_FUNCTION ("control_get_job_by_name");
	nih_error_init ();
	job_class_init ();

	class = job_class_new (NULL, "test");
	nih_hash_add (job_classes, &class->entry);


	/* Check that when given a known job name, the path to that job
	 * is returned as a duplicate child of the message structure.
	 */
	TEST_FEATURE ("with known job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}

		ret = control_get_job_by_name (NULL, message, "test", &path);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);

			continue;
		}

		TEST_EQ (ret, 0);

		TEST_ALLOC_PARENT (path, message);
		TEST_EQ_STR (path, class->path);

		nih_free (message);
	}


	/* Check that when given an unknown job name, an unknown job
	 * D-Bus error is raised and an error returned.
	 */
	TEST_FEATURE ("with unknown job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}

		ret = control_get_job_by_name (NULL, message, "foo", &path);

		TEST_LT (ret, 0);

		error = nih_error_get ();
		TEST_EQ (error->number, NIH_DBUS_ERROR);
		TEST_ALLOC_SIZE (error, sizeof (NihDBusError));

		dbus_error = (NihDBusError *)error;
		TEST_EQ_STR (dbus_error->name,
			     DBUS_INTERFACE_UPSTART ".Error.UnknownJob");

		nih_free (error);

		nih_free (message);
	}


	/* Check that when given an illegal job name, an invalid args
	 * D-Bus error is raised and an error returned.
	 */
	TEST_FEATURE ("with illegal job name");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}

		ret = control_get_job_by_name (NULL, message, "", &path);

		TEST_LT (ret, 0);

		error = nih_error_get ();
		TEST_EQ (error->number, NIH_DBUS_ERROR);
		TEST_ALLOC_SIZE (error, sizeof (NihDBusError));

		dbus_error = (NihDBusError *)error;
		TEST_EQ_STR (dbus_error->name, DBUS_ERROR_INVALID_ARGS);

		nih_free (error);

		nih_free (message);
	}

	nih_free (class);
}

void
test_get_all_jobs (void)
{
	NihDBusMessage  *message = NULL;
	JobClass        *class1, *class2, *class3;
	NihError        *error;
	char           **paths;
	int              ret;

	TEST_FUNCTION ("control_get_all_jobs");
	nih_error_init ();
	job_class_init ();


	/* Check that paths for each of the registered jobs are returned
	 * in an array allocated as a child of the message structure.
	 */
	TEST_FEATURE ("with registered jobs");
	class1 = job_class_new (NULL, "frodo");
	nih_hash_add (job_classes, &class1->entry);

	class2 = job_class_new (NULL, "bilbo");
	nih_hash_add (job_classes, &class2->entry);

	class3 = job_class_new (NULL, "sauron");
	nih_hash_add (job_classes, &class3->entry);

	TEST_ALLOC_FAIL {
		int found1 = FALSE, found2 = FALSE, found3 = FALSE, i;

		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}

		ret = control_get_all_jobs (NULL, message, &paths);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);

			continue;
		}

		TEST_EQ (ret, 0);

		TEST_ALLOC_PARENT (paths, message);
		TEST_ALLOC_SIZE (paths, sizeof (char *) * 4);
		TEST_EQ_P (paths[3], NULL);

		for (i = 0; i < 3; i++) {
			TEST_ALLOC_PARENT (paths[i], paths);

			if (! strcmp (paths[i], class1->path))
				found1 = TRUE;
			if (! strcmp (paths[i], class2->path))
				found2 = TRUE;
			if (! strcmp (paths[i], class3->path))
				found3 = TRUE;
		}

		TEST_TRUE (found1);
		TEST_TRUE (found2);
		TEST_TRUE (found3);

		nih_free (message);
	}

	nih_free (class3);
	nih_free (class2);
	nih_free (class1);


	/* Check that when no jobs are registered, an empty array is
	 * returned instead of an error.
	 */
	TEST_FEATURE ("with no registered jobs");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}

		ret = control_get_all_jobs (NULL, message, &paths);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);

			continue;
		}

		TEST_EQ (ret, 0);

		TEST_ALLOC_PARENT (paths, message);
		TEST_ALLOC_SIZE (paths, sizeof (char *) * 1);
		TEST_EQ_P (paths[0], NULL);

		nih_free (message);
	}
}

void
test_emit_event (void)
{
	DBusConnection  *conn, *client_conn;
	pid_t            dbus_pid;
	DBusMessage     *method, *reply;
	NihDBusMessage  *message = NULL;
	dbus_uint32_t    serial;
	char           **env;
	int              ret;
	Event           *event;
	Blocked *        blocked;
	NihError        *error;
	NihDBusError    *dbus_error;

	TEST_FUNCTION ("control_emit_event");
	nih_error_init ();
	nih_main_loop_init ();
	event_init ();

	TEST_DBUS (dbus_pid);
	TEST_DBUS_OPEN (conn);
	TEST_DBUS_OPEN (client_conn);


	/* Check that we can emit an event with an empty environment list
	 * which will be added to the event queue while the message is
	 * blocked.  When the event is finished, the reply will be sent
	 * and the message structure freed.
	 */
	TEST_FEATURE ("with empty environment list");
	TEST_ALLOC_FAIL {
		method = dbus_message_new_method_call (
			dbus_bus_get_unique_name (conn),
			DBUS_PATH_UPSTART,
			DBUS_INTERFACE_UPSTART,
			"EmitEvent");

		dbus_connection_send (client_conn, method, &serial);
		dbus_connection_flush (client_conn);
		dbus_message_unref (method);

		TEST_DBUS_MESSAGE (conn, method);
		assert (dbus_message_get_serial (method) == serial);

		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = conn;
			message->message = method;

			TEST_FREE_TAG (message);

			env = nih_str_array_new (message);
		}

		ret = control_emit_event (NULL, message, "test", env, TRUE);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);
			dbus_message_unref (method);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_LIST_NOT_EMPTY (events);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "test");
		TEST_EQ_P (event->env[0], NULL);

		TEST_LIST_NOT_EMPTY (&event->blocking);

		blocked = (Blocked *)event->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, event);
		TEST_EQ (blocked->type, BLOCKED_EMIT_METHOD);
		TEST_EQ_P (blocked->message, message);

		TEST_ALLOC_PARENT (blocked->message, blocked);

		TEST_FREE_TAG (blocked);

		nih_discard (message);
		TEST_NOT_FREE (message);


		event_poll ();

		TEST_LIST_EMPTY (events);

		TEST_FREE (blocked);

		TEST_FREE (message);
		dbus_message_unref (method);

		dbus_connection_flush (conn);

		TEST_DBUS_MESSAGE (client_conn, reply);

		TEST_EQ (dbus_message_get_type (reply),
			 DBUS_MESSAGE_TYPE_METHOD_RETURN);
		TEST_EQ (dbus_message_get_reply_serial (reply), serial);

		dbus_message_unref (reply);
	}


	/* Check that if we supply an environment list, it's placed straight
	 * into the event.
	 */
	TEST_FEATURE ("with environment list");
	TEST_ALLOC_FAIL {
		method = dbus_message_new_method_call (
			dbus_bus_get_unique_name (conn),
			DBUS_PATH_UPSTART,
			DBUS_INTERFACE_UPSTART,
			"EmitEvent");

		dbus_connection_send (client_conn, method, &serial);
		dbus_connection_flush (client_conn);
		dbus_message_unref (method);

		TEST_DBUS_MESSAGE (conn, method);
		assert (dbus_message_get_serial (method) == serial);

		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = conn;
			message->message = method;

			TEST_FREE_TAG (message);

			env = nih_str_array_new (message);
			assert (nih_str_array_add (&env, message, NULL,
						   "FOO=BAR"));
			assert (nih_str_array_add (&env, message, NULL,
						   "BAR=BAZ"));
		}

		ret = control_emit_event (NULL, message, "test", env, TRUE);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);
			dbus_message_unref (method);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_LIST_NOT_EMPTY (events);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "test");
		TEST_EQ_STR (event->env[0], "FOO=BAR");
		TEST_EQ_STR (event->env[1], "BAR=BAZ");
		TEST_EQ_P (event->env[2], NULL);

		TEST_LIST_NOT_EMPTY (&event->blocking);

		blocked = (Blocked *)event->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, event);
		TEST_EQ (blocked->type, BLOCKED_EMIT_METHOD);
		TEST_EQ_P (blocked->message, message);

		TEST_ALLOC_PARENT (blocked->message, blocked);

		TEST_FREE_TAG (blocked);

		nih_discard (message);
		TEST_NOT_FREE (message);


		event_poll ();

		TEST_LIST_EMPTY (events);

		TEST_FREE (blocked);

		TEST_FREE (message);
		dbus_message_unref (method);

		dbus_connection_flush (conn);

		TEST_DBUS_MESSAGE (client_conn, reply);

		TEST_EQ (dbus_message_get_type (reply),
			 DBUS_MESSAGE_TYPE_METHOD_RETURN);
		TEST_EQ (dbus_message_get_reply_serial (reply), serial);

		dbus_message_unref (reply);
	}


	/* Check that we can emit an event without waiting for it to
	 * finish, we should get the reply straight away.
	 */
	TEST_FEATURE ("with no wait");
	TEST_ALLOC_FAIL {
		method = dbus_message_new_method_call (
			dbus_bus_get_unique_name (conn),
			DBUS_PATH_UPSTART,
			DBUS_INTERFACE_UPSTART,
			"EmitEvent");

		dbus_connection_send (client_conn, method, &serial);
		dbus_connection_flush (client_conn);
		dbus_message_unref (method);

		TEST_DBUS_MESSAGE (conn, method);
		assert (dbus_message_get_serial (method) == serial);

		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = conn;
			message->message = method;

			TEST_FREE_TAG (message);

			env = nih_str_array_new (message);
		}

		ret = control_emit_event (NULL, message, "test", env, FALSE);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);
			dbus_message_unref (method);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_LIST_NOT_EMPTY (events);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "test");
		TEST_EQ_P (event->env[0], NULL);

		TEST_LIST_EMPTY (&event->blocking);

		nih_discard (message);
		TEST_FREE (message);
		dbus_message_unref (method);

		dbus_connection_flush (conn);

		TEST_DBUS_MESSAGE (client_conn, reply);

		TEST_EQ (dbus_message_get_type (reply),
			 DBUS_MESSAGE_TYPE_METHOD_RETURN);
		TEST_EQ (dbus_message_get_reply_serial (reply), serial);

		dbus_message_unref (reply);


		event_poll ();

		TEST_LIST_EMPTY (events);
	}


	/* Check that if the event is marked as failed, an ordinary reply
	 * is not sent when its finished, but an error instead.
	 */
	TEST_FEATURE ("with failed event");
	method = dbus_message_new_method_call (
		dbus_bus_get_unique_name (conn),
		DBUS_PATH_UPSTART,
		DBUS_INTERFACE_UPSTART,
		"EmitEvent");

	dbus_connection_send (client_conn, method, &serial);
	dbus_connection_flush (client_conn);
	dbus_message_unref (method);

	TEST_DBUS_MESSAGE (conn, method);
	assert (dbus_message_get_serial (method) == serial);

	message = nih_new (NULL, NihDBusMessage);
	message->connection = conn;
	message->message = method;

	TEST_FREE_TAG (message);

	env = nih_str_array_new (NULL);

	ret = control_emit_event (NULL, message, "test", env, TRUE);
	nih_discard (env);

	TEST_EQ (ret, 0);

	TEST_LIST_NOT_EMPTY (events);

	event = (Event *)events->next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "test");
	TEST_EQ_P (event->env[0], NULL);

	nih_discard (message);
	TEST_NOT_FREE (message);


	event->failed = TRUE;
	event_poll ();

	TEST_LIST_EMPTY (events);

	TEST_FREE (message);
	dbus_message_unref (method);

	dbus_connection_flush (conn);

	TEST_DBUS_MESSAGE (client_conn, reply);

	TEST_TRUE (dbus_message_is_error (reply,
					  DBUS_INTERFACE_UPSTART ".Error.EventFailed"));
	TEST_EQ (dbus_message_get_reply_serial (reply), serial);

	dbus_message_unref (reply);


	/* Check that if the event name is empty, an error is returned
	 * immediately.
	 */
	TEST_FEATURE ("with empty name");
	method = dbus_message_new_method_call (
		dbus_bus_get_unique_name (conn),
		DBUS_PATH_UPSTART,
		DBUS_INTERFACE_UPSTART,
		"EmitEvent");

	dbus_connection_send (client_conn, method, &serial);
	dbus_connection_flush (client_conn);
	dbus_message_unref (method);

	TEST_DBUS_MESSAGE (conn, method);
	assert (dbus_message_get_serial (method) == serial);

	message = nih_new (NULL, NihDBusMessage);
	message->connection = conn;
	message->message = method;

	env = nih_str_array_new (message);

	ret = control_emit_event (NULL, message, "", env, TRUE);

	TEST_LT (ret, 0);

	dbus_error = (NihDBusError *)nih_error_get ();
	TEST_ALLOC_SIZE (dbus_error, sizeof (NihDBusError));
	TEST_EQ (dbus_error->number, NIH_DBUS_ERROR);
	TEST_EQ_STR (dbus_error->name, DBUS_ERROR_INVALID_ARGS);
	nih_free (dbus_error);

	nih_free (message);
	dbus_message_unref (method);


	/* Check that if an entry in the environment list is missing an
	 * equals, an error is returned immediately.
	 */
	TEST_FEATURE ("with missing equals in environment list");
	method = dbus_message_new_method_call (
		dbus_bus_get_unique_name (conn),
		DBUS_PATH_UPSTART,
		DBUS_INTERFACE_UPSTART,
		"EmitEvent");

	dbus_connection_send (client_conn, method, &serial);
	dbus_connection_flush (client_conn);
	dbus_message_unref (method);

	TEST_DBUS_MESSAGE (conn, method);
	assert (dbus_message_get_serial (method) == serial);

	message = nih_new (NULL, NihDBusMessage);
	message->connection = conn;
	message->message = method;

	env = nih_str_array_new (message);
	assert (nih_str_array_add (&env, message, NULL, "FOO_BAR"));

	ret = control_emit_event (NULL, message, "test", env, TRUE);

	TEST_LT (ret, 0);

	dbus_error = (NihDBusError *)nih_error_get ();
	TEST_ALLOC_SIZE (dbus_error, sizeof (NihDBusError));
	TEST_EQ (dbus_error->number, NIH_DBUS_ERROR);
	TEST_EQ_STR (dbus_error->name, DBUS_ERROR_INVALID_ARGS);
	nih_free (dbus_error);

	nih_free (message);
	dbus_message_unref (method);


	TEST_DBUS_CLOSE (conn);
	TEST_DBUS_CLOSE (client_conn);
	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}


void
test_get_version (void)
{
	NihDBusMessage *message = NULL;
	char           *version;
	NihError       *error;
	int             ret;

	/* Check that the function returns the package string as a newly
	 * allocated child of the message structure.
	 */
	TEST_FUNCTION ("control_get_version");
	nih_error_init ();
	job_class_init ();
	package_string = "init (upstart 1.0)";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}

		ret = control_get_version (NULL, message, &version);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);

			continue;
		}

		TEST_EQ (ret, 0);

		TEST_ALLOC_PARENT (version, message);
		TEST_EQ_STR (version, package_string);

		nih_free (message);
	}
}


void
test_get_log_priority (void)
{
	NihDBusMessage *message = NULL;
	char           *priority;
	NihError       *error;
	int             ret;

	TEST_FUNCTION ("control_get_log_priority");
	nih_error_init ();
	job_class_init ();

	/* Check that the function returns the correct string for the
	 * NIH_LOG_FATAL priority as an allocated child of the message
	 * structure.
	 */
	TEST_FEATURE ("with fatal");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}

		nih_log_priority = NIH_LOG_FATAL;

		ret = control_get_log_priority (NULL, message, &priority);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);

			continue;
		}

		TEST_EQ (ret, 0);

		TEST_ALLOC_PARENT (priority, message);
		TEST_EQ_STR (priority, "fatal");

		nih_free (message);
	}


	/* Check that the function returns the correct string for the
	 * NIH_LOG_ERROR priority as an allocated child of the message
	 * structure.
	 */
	TEST_FEATURE ("with error");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}

		nih_log_priority = NIH_LOG_ERROR;

		ret = control_get_log_priority (NULL, message, &priority);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);

			continue;
		}

		TEST_EQ (ret, 0);

		TEST_ALLOC_PARENT (priority, message);
		TEST_EQ_STR (priority, "error");

		nih_free (message);
	}


	/* Check that the function returns the correct string for the
	 * NIH_LOG_WARN priority as an allocated child of the message
	 * structure.
	 */
	TEST_FEATURE ("with warn");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}

		nih_log_priority = NIH_LOG_WARN;

		ret = control_get_log_priority (NULL, message, &priority);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);

			continue;
		}

		TEST_EQ (ret, 0);

		TEST_ALLOC_PARENT (priority, message);
		TEST_EQ_STR (priority, "warn");

		nih_free (message);
	}


	/* Check that the function returns the correct string for the
	 * NIH_LOG_MESSAGE priority as an allocated child of the message
	 * structure.
	 */
	TEST_FEATURE ("with message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}

		nih_log_priority = NIH_LOG_MESSAGE;

		ret = control_get_log_priority (NULL, message, &priority);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);

			continue;
		}

		TEST_EQ (ret, 0);

		TEST_ALLOC_PARENT (priority, message);
		TEST_EQ_STR (priority, "message");

		nih_free (message);
	}


	/* Check that the function returns the correct string for the
	 * NIH_LOG_INFO priority as an allocated child of the message
	 * structure.
	 */
	TEST_FEATURE ("with info");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}

		nih_log_priority = NIH_LOG_INFO;

		ret = control_get_log_priority (NULL, message, &priority);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);

			continue;
		}

		TEST_EQ (ret, 0);

		TEST_ALLOC_PARENT (priority, message);
		TEST_EQ_STR (priority, "info");

		nih_free (message);
	}


	/* Check that the function returns the correct string for the
	 * NIH_LOG_DEBUG priority as an allocated child of the message
	 * structure.
	 */
	TEST_FEATURE ("with debug");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}

		nih_log_priority = NIH_LOG_DEBUG;

		ret = control_get_log_priority (NULL, message, &priority);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);

			continue;
		}

		TEST_EQ (ret, 0);

		TEST_ALLOC_PARENT (priority, message);
		TEST_EQ_STR (priority, "debug");

		nih_free (message);
	}


	nih_log_priority = NIH_LOG_UNKNOWN;
}

void
test_set_log_priority (void)
{
	NihDBusMessage *message = NULL;
	NihError       *error;
	NihDBusError   *dbus_err;
	int             ret;

	TEST_FUNCTION ("control_set_log_priority");
	nih_error_init ();
	job_class_init ();

	/* Check that the function sets the log priority to NIH_LOG_FATAL
	 * when given the correct string.
	 */
	TEST_FEATURE ("with fatal");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}

		nih_log_priority = NIH_LOG_UNKNOWN;

		ret = control_set_log_priority (NULL, message, "fatal");

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);

			continue;
		}

		TEST_EQ (ret, 0);

		TEST_EQ (nih_log_priority, NIH_LOG_FATAL);

		nih_free (message);
	}


	/* Check that the function sets the log priority to NIH_LOG_ERROR
	 * when given the correct string.
	 */
	TEST_FEATURE ("with error");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}

		nih_log_priority = NIH_LOG_UNKNOWN;

		ret = control_set_log_priority (NULL, message, "error");

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);

			continue;
		}

		TEST_EQ (ret, 0);

		TEST_EQ (nih_log_priority, NIH_LOG_ERROR);

		nih_free (message);
	}


	/* Check that the function sets the log priority to NIH_LOG_WARN
	 * when given the correct string.
	 */
	TEST_FEATURE ("with warn");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}

		nih_log_priority = NIH_LOG_UNKNOWN;

		ret = control_set_log_priority (NULL, message, "warn");

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);

			continue;
		}

		TEST_EQ (ret, 0);

		TEST_EQ (nih_log_priority, NIH_LOG_WARN);

		nih_free (message);
	}


	/* Check that the function sets the log priority to NIH_LOG_MESSAGE
	 * when given the correct string.
	 */
	TEST_FEATURE ("with message");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}

		nih_log_priority = NIH_LOG_UNKNOWN;

		ret = control_set_log_priority (NULL, message, "message");

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);

			continue;
		}

		TEST_EQ (ret, 0);

		TEST_EQ (nih_log_priority, NIH_LOG_MESSAGE);

		nih_free (message);
	}


	/* Check that the function sets the log priority to NIH_LOG_INFO
	 * when given the correct string.
	 */
	TEST_FEATURE ("with info");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}

		nih_log_priority = NIH_LOG_UNKNOWN;

		ret = control_set_log_priority (NULL, message, "info");

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);

			continue;
		}

		TEST_EQ (ret, 0);

		TEST_EQ (nih_log_priority, NIH_LOG_INFO);

		nih_free (message);
	}


	/* Check that the function sets the log priority to NIH_LOG_DEBUG
	 * when given the correct string.
	 */
	TEST_FEATURE ("with debug");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}

		nih_log_priority = NIH_LOG_UNKNOWN;

		ret = control_set_log_priority (NULL, message, "debug");

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);

			continue;
		}

		TEST_EQ (ret, 0);

		TEST_EQ (nih_log_priority, NIH_LOG_DEBUG);

		nih_free (message);
	}


	/* Check that the function returns an invalid arguments error when
	 * given an unrecognised string.
	 */
	TEST_FEATURE ("with unknown string");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}

		nih_log_priority = NIH_LOG_MESSAGE;

		ret = control_set_log_priority (NULL, message, "wibble");

		TEST_LT (ret, 0);

		error = nih_error_get ();

		if (test_alloc_failed
		    && (error->number == ENOMEM)) {
			nih_free (error);

			nih_free (message);

			continue;
		}

		TEST_EQ (error->number, NIH_DBUS_ERROR);
		TEST_ALLOC_SIZE (error, sizeof (NihDBusError));

		dbus_err = (NihDBusError *)error;
		TEST_EQ_STR (dbus_err->name, DBUS_ERROR_INVALID_ARGS);

		nih_free (dbus_err);

		TEST_EQ (nih_log_priority, NIH_LOG_MESSAGE);

		nih_free (message);
	}


	nih_log_priority = NIH_LOG_UNKNOWN;
}


int
main (int   argc,
      char *argv[])
{
	test_server_open ();
	test_server_connect ();
	test_server_close ();

	test_bus_open ();
	test_bus_close ();

	test_disconnected ();

	test_reload_configuration ();

	test_get_job_by_name ();
	test_get_all_jobs ();

	test_emit_event ();

	test_get_version ();

	test_get_log_priority ();
	test_set_log_priority ();

	return 0;
}
