/* upstart
 *
 * test_initctl.c - test suite for util/initctl.c
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

#include <dbus/dbus.h>

#include <stdio.h>
#include <signal.h>
#include <unistd.h>

#include <nih-dbus/dbus_error.h>
#include <nih-dbus/dbus_connection.h>
#include <nih-dbus/dbus_proxy.h>
#include <nih-dbus/errors.h>

#include <nih/macros.h>
#include <nih/timer.h>
#include <nih/signal.h>
#include <nih/child.h>
#include <nih/main.h>
#include <nih/command.h>
#include <nih/error.h>

#include "dbus/upstart.h"


extern int system_bus;
extern char *dest_name;
extern const char *dest_address;
extern int no_wait;

extern NihDBusProxy *upstart_open (const void *parent)
	__attribute__ ((warn_unused_result, malloc));
extern char *        job_status   (const void *parent,
				   NihDBusProxy *job_class, NihDBusProxy *job)
	__attribute__ ((warn_unused_result, malloc));

extern int start_action                (NihCommand *command, char * const *args);
extern int stop_action                 (NihCommand *command, char * const *args);
extern int restart_action              (NihCommand *command, char * const *args);
extern int reload_action               (NihCommand *command, char * const *args);
extern int status_action               (NihCommand *command, char * const *args);
extern int list_action                 (NihCommand *command, char * const *args);
extern int emit_action                 (NihCommand *command, char * const *args);
extern int reload_configuration_action (NihCommand *command, char * const *args);
extern int version_action              (NihCommand *command, char * const *args);
extern int log_priority_action         (NihCommand *command, char * const *args);


static int my_connect_handler_called = FALSE;
static DBusConnection *last_connection = NULL;

static int
my_connect_handler (DBusServer *    server,
		    DBusConnection *connection)
{
	my_connect_handler_called++;

	last_connection = connection;

	nih_main_loop_exit (0);

	return TRUE;
}

void
test_upstart_open (void)
{
	DBusServer *    server = NULL;
	pid_t           dbus_pid;
	DBusConnection *server_conn = NULL;
	NihDBusProxy *  proxy = NULL;
	FILE *          output;

	TEST_FUNCTION ("upstart_open");
	output = tmpfile ();


	/* Check that we can create a proxy to Upstart's private internal
	 * server, and that this is the default behaviour if we don't
	 * fiddle with the other options.  The returned proxy should
	 * hold the only reference to the connection.
	 */
	TEST_FEATURE ("with private connection");
	TEST_ALLOC_FAIL {
		system_bus = FALSE;
		dest_name = NULL;
		dest_address = "unix:abstract=/com/ubuntu/upstart/test";

		TEST_ALLOC_SAFE {
			server = nih_dbus_server (dest_address,
						  my_connect_handler,
						  NULL);
			assert (server != NULL);
		}

		my_connect_handler_called = FALSE;
		last_connection = NULL;

		TEST_DIVERT_STDERR (output) {
			proxy = upstart_open (NULL);
		}
		rewind (output);

		if (test_alloc_failed
		    && (proxy == NULL)) {
			TEST_FILE_EQ (output, "test: Cannot allocate memory\n");
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			if (last_connection) {
				dbus_connection_close (last_connection);
				dbus_connection_unref (last_connection);
			}

			dbus_server_disconnect (server);
			dbus_server_unref (server);

			dbus_shutdown ();
			continue;
		}

		nih_main_loop ();

		TEST_TRUE (my_connect_handler_called);
		TEST_NE_P (last_connection, NULL);

		TEST_NE_P (proxy, NULL);
		TEST_ALLOC_SIZE (proxy, sizeof (NihDBusProxy));

		TEST_NE_P (proxy->connection, NULL);
		TEST_EQ_P (proxy->name, NULL);
		TEST_EQ_P (proxy->owner, NULL);
		TEST_EQ_STR (proxy->path, DBUS_PATH_UPSTART);
		TEST_ALLOC_PARENT (proxy->path, proxy);
		TEST_FALSE (proxy->auto_start);

		TEST_EQ_P (proxy->lost_handler, NULL);
		TEST_EQ_P (proxy->data, NULL);

		nih_free (proxy);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		dbus_connection_close (last_connection);
		dbus_connection_unref (last_connection);

		dbus_server_disconnect (server);
		dbus_server_unref (server);

		dbus_shutdown ();
	}


	/* Check that we can create a connection to Upstart via the system
	 * bus.  The returned proxy should use the default name on that
	 * bus.
	 */
	TEST_FEATURE ("with system bus connection");
	TEST_ALLOC_FAIL {
		system_bus = TRUE;
		dest_name = NULL;
		dest_address = DBUS_ADDRESS_UPSTART;

		TEST_DBUS (dbus_pid);
		TEST_DBUS_OPEN (server_conn);

		assert (dbus_bus_request_name (server_conn, DBUS_SERVICE_UPSTART,
					       0, NULL)
			== DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);

		TEST_DIVERT_STDERR (output) {
			proxy = upstart_open (NULL);
		}
		rewind (output);

		if (test_alloc_failed
		    && (proxy == NULL)) {
			TEST_FILE_EQ (output, "test: Cannot allocate memory\n");
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_DBUS_CLOSE (server_conn);
			TEST_DBUS_END (dbus_pid);

			dbus_shutdown ();
			continue;
		}

		TEST_NE_P (proxy, NULL);
		TEST_ALLOC_SIZE (proxy, sizeof (NihDBusProxy));

		TEST_NE_P (proxy->connection, NULL);
		TEST_EQ_STR (proxy->name, DBUS_SERVICE_UPSTART);
		TEST_ALLOC_PARENT (proxy->name, proxy);
		TEST_EQ_STR (proxy->owner, dbus_bus_get_unique_name (server_conn));
		TEST_ALLOC_PARENT (proxy->owner, proxy);
		TEST_EQ_STR (proxy->path, DBUS_PATH_UPSTART);
		TEST_ALLOC_PARENT (proxy->path, proxy);
		TEST_FALSE (proxy->auto_start);

		TEST_EQ_P (proxy->lost_handler, NULL);
		TEST_EQ_P (proxy->data, NULL);

		nih_free (proxy);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_DBUS_CLOSE (server_conn);
		TEST_DBUS_END (dbus_pid);

		dbus_shutdown ();
	}


	/* Check that we can create a connection to Upstart via the system
	 * bus and a different well known name.
	 */
	TEST_FEATURE ("with system bus connection and different name");
	TEST_ALLOC_FAIL {
		system_bus = TRUE;
		dest_name = "com.ubuntu.UpstartTest";
		dest_address = DBUS_ADDRESS_UPSTART;

		TEST_DBUS (dbus_pid);
		TEST_DBUS_OPEN (server_conn);

		assert (dbus_bus_request_name (server_conn, "com.ubuntu.UpstartTest",
					       0, NULL)
			== DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);

		TEST_DIVERT_STDERR (output) {
			proxy = upstart_open (NULL);
		}
		rewind (output);

		if (test_alloc_failed
		    && (proxy == NULL)) {
			TEST_FILE_EQ (output, "test: Cannot allocate memory\n");
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_DBUS_CLOSE (server_conn);
			TEST_DBUS_END (dbus_pid);

			dbus_shutdown ();
			continue;
		}

		TEST_NE_P (proxy, NULL);
		TEST_ALLOC_SIZE (proxy, sizeof (NihDBusProxy));

		TEST_NE_P (proxy->connection, NULL);
		TEST_EQ_STR (proxy->name, "com.ubuntu.UpstartTest");
		TEST_ALLOC_PARENT (proxy->name, proxy);
		TEST_EQ_STR (proxy->owner, dbus_bus_get_unique_name (server_conn));
		TEST_ALLOC_PARENT (proxy->owner, proxy);
		TEST_EQ_STR (proxy->path, DBUS_PATH_UPSTART);
		TEST_ALLOC_PARENT (proxy->path, proxy);
		TEST_FALSE (proxy->auto_start);

		TEST_EQ_P (proxy->lost_handler, NULL);
		TEST_EQ_P (proxy->data, NULL);

		nih_free (proxy);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_DBUS_CLOSE (server_conn);
		TEST_DBUS_END (dbus_pid);

		dbus_shutdown ();
	}


	/* Check that when we attempt to connect to Upstart's private
	 * internal server, and it's not listening, that an appropriate
	 * error is output.
	 */
	TEST_FEATURE ("with non-listening private connection");
	TEST_ALLOC_FAIL {
		system_bus = FALSE;
		dest_name = NULL;
		dest_address = "unix:abstract=/com/ubuntu/upstart/test";

		TEST_DIVERT_STDERR (output) {
			proxy = upstart_open (NULL);
		}
		rewind (output);

		TEST_EQ_P (proxy, NULL);

		TEST_FILE_EQ (output, ("test: Unable to connect to Upstart: "
				       "Failed to connect to socket /com/ubuntu/upstart/test: "
				       "Connection refused\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		dbus_shutdown ();
	}


	/* Check that when we attempt to connect to the system bus,
	 * and it's not listening, that an appropriate error is output.
	 */
	TEST_FEATURE ("with non-listening system bus");
	TEST_ALLOC_FAIL {
		system_bus = TRUE;
		dest_name = NULL;
		dest_address = DBUS_ADDRESS_UPSTART;

		assert0 (setenv ("DBUS_SYSTEM_BUS_ADDRESS",
				 "unix:abstract=/com/ubuntu/upstart/test",
				 TRUE));

		TEST_DIVERT_STDERR (output) {
			proxy = upstart_open (NULL);
		}
		rewind (output);

		TEST_EQ_P (proxy, NULL);

		TEST_FILE_EQ (output, ("test: Unable to connect to system bus: "
				       "Failed to connect to socket /com/ubuntu/upstart/test: "
				       "Connection refused\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		dbus_shutdown ();

		unsetenv ("DBUS_SYSTEM_BUS_ADDRESS");
	}


	/* Check that an error and suggestion for help is output
	 * when --dest is given without --system.
	 */
	TEST_FEATURE ("with --dest but without --system");
	TEST_ALLOC_FAIL {
		system_bus = FALSE;
		dest_name = "com.ubuntu.Upstart";
		dest_address = DBUS_ADDRESS_UPSTART;

		TEST_DIVERT_STDERR (output) {
			proxy = upstart_open (NULL);
		}
		rewind (output);

		TEST_EQ_P (proxy, NULL);

		TEST_FILE_EQ (output, "test: --dest given without --system\n");
		TEST_FILE_EQ (output, "Try `test --help' for more information.\n");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		dbus_shutdown ();
	}


	fclose (output);
}


void
test_job_status (void)
{
	pid_t           dbus_pid;
	DBusConnection *server_conn;
	DBusConnection *client_conn;
	pid_t           server_pid;
	DBusMessage *   method_call;
	const char *    interface;
	const char *    property;
	DBusMessage *   reply = NULL;
	DBusMessageIter iter;
	DBusMessageIter arrayiter;
	DBusMessageIter dictiter;
	DBusMessageIter subiter;
	DBusMessageIter prociter;
	DBusMessageIter structiter;
	const char *    str_value;
	int32_t         int32_value;
	NihDBusProxy *  job_class = NULL;
	NihDBusProxy *  job = NULL;
	char *          str;
	NihError *      err;
	NihDBusError *  dbus_err;
	int             status;

	TEST_FUNCTION ("job_status");
	TEST_DBUS (dbus_pid);
	TEST_DBUS_OPEN (server_conn);
	TEST_DBUS_OPEN (client_conn);


	/* Check that we can generate a string for a job instance that
	 * is running with a main process.  The function should request
	 * the name of the job class, and then request all of the
	 * properties of the job instance.
	 */
	TEST_FEATURE ("with running main process");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the Get call for the name, reply with the
			 * name.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
			TEST_EQ_STR (property, "name");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "test";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&iter, &subiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetAll call for the properties, reply
			 * with the properties.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"GetAll"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test/_");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
								  (DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_VARIANT_AS_STRING
								   DBUS_DICT_ENTRY_END_CHAR_AS_STRING),
								  &arrayiter);

				/* Name */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "name";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Goal */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "goal";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "start";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* State */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "state";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "running";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Processes */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "processes";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  (DBUS_TYPE_ARRAY_AS_STRING
								   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &subiter);

				dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
								  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &prociter);

				dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
								  NULL,
								  &structiter);

				str_value = "main";
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
								&str_value);

				int32_value = 3648;
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
								&int32_value);

				dbus_message_iter_close_container (&prociter, &structiter);

				dbus_message_iter_close_container (&subiter, &prociter);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				dbus_message_iter_close_container (&iter, &arrayiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (client_conn);
			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		TEST_ALLOC_SAFE {
			job_class = nih_dbus_proxy_new (NULL, client_conn,
							dbus_bus_get_unique_name (server_conn),
							DBUS_PATH_UPSTART "/jobs/test",
							NULL, NULL);
			job = nih_dbus_proxy_new (NULL, client_conn,
						  dbus_bus_get_unique_name (server_conn),
						  DBUS_PATH_UPSTART "/jobs/test/_",
						  NULL, NULL);
		}

		str = job_status (NULL, job_class, job);

		if (test_alloc_failed
		    && (str == NULL)) {
			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			nih_free (job);
			nih_free (job_class);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ_STR (str, "test start/running, process 3648");

		nih_free (str);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		nih_free (job);
		nih_free (job_class);
	}


	/* Check that we can generate a string for a named job instance,
	 * the name should be placed in the returned string in brackets
	 * after the job config name.
	 */
	TEST_FEATURE ("with named instance");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the Get call for the name, reply with the
			 * name.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
			TEST_EQ_STR (property, "name");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "test";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&iter, &subiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetAll call for the properties, reply
			 * with the properties.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"GetAll"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test/beetroot");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
								  (DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_VARIANT_AS_STRING
								   DBUS_DICT_ENTRY_END_CHAR_AS_STRING),
								  &arrayiter);

				/* Name */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "name";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "beetroot";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Goal */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "goal";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "start";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* State */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "state";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "running";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Processes */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "processes";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  (DBUS_TYPE_ARRAY_AS_STRING
								   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &subiter);

				dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
								  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &prociter);

				dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
								  NULL,
								  &structiter);

				str_value = "main";
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
								&str_value);

				int32_value = 3648;
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
								&int32_value);

				dbus_message_iter_close_container (&prociter, &structiter);

				dbus_message_iter_close_container (&subiter, &prociter);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				dbus_message_iter_close_container (&iter, &arrayiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (client_conn);
			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		TEST_ALLOC_SAFE {
			job_class = nih_dbus_proxy_new (NULL, client_conn,
							dbus_bus_get_unique_name (server_conn),
							DBUS_PATH_UPSTART "/jobs/test",
							NULL, NULL);
			job = nih_dbus_proxy_new (NULL, client_conn,
						  dbus_bus_get_unique_name (server_conn),
						  DBUS_PATH_UPSTART "/jobs/test/beetroot",
						  NULL, NULL);
		}

		str = job_status (NULL, job_class, job);

		if (test_alloc_failed
		    && (str == NULL)) {
			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			nih_free (job);
			nih_free (job_class);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ_STR (str, "test (beetroot) start/running, process 3648");

		nih_free (str);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		nih_free (job);
		nih_free (job_class);
	}


	/* Check that we can generate a string for a job instance in a
	 * state that doesn't come with a process, only the goal and
	 * state should be output.
	 */
	TEST_FEATURE ("with no process");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the Get call for the name, reply with the
			 * name.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
			TEST_EQ_STR (property, "name");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "test";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&iter, &subiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetAll call for the properties, reply
			 * with the properties.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"GetAll"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test/_");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
								  (DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_VARIANT_AS_STRING
								   DBUS_DICT_ENTRY_END_CHAR_AS_STRING),
								  &arrayiter);

				/* Name */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "name";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Goal */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "goal";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "stop";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* State */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "state";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "stopping";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Processes */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "processes";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  (DBUS_TYPE_ARRAY_AS_STRING
								   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &subiter);

				dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
								  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &prociter);

				dbus_message_iter_close_container (&subiter, &prociter);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				dbus_message_iter_close_container (&iter, &arrayiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (client_conn);
			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		TEST_ALLOC_SAFE {
			job_class = nih_dbus_proxy_new (NULL, client_conn,
							dbus_bus_get_unique_name (server_conn),
							DBUS_PATH_UPSTART "/jobs/test",
							NULL, NULL);
			job = nih_dbus_proxy_new (NULL, client_conn,
						  dbus_bus_get_unique_name (server_conn),
						  DBUS_PATH_UPSTART "/jobs/test/_",
						  NULL, NULL);
		}

		str = job_status (NULL, job_class, job);

		if (test_alloc_failed
		    && (str == NULL)) {
			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			nih_free (job);
			nih_free (job_class);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ_STR (str, "test stop/stopping");

		nih_free (str);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		nih_free (job);
		nih_free (job_class);
	}


	/* Check that we can generate a string for a job instance with
	 * a running pre-start process, since this is a standard state
	 * with a process, the pid should simply follow the state.
	 */
	TEST_FEATURE ("with running pre-start process");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the Get call for the name, reply with the
			 * name.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
			TEST_EQ_STR (property, "name");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "test";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&iter, &subiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetAll call for the properties, reply
			 * with the properties.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"GetAll"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test/_");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
								  (DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_VARIANT_AS_STRING
								   DBUS_DICT_ENTRY_END_CHAR_AS_STRING),
								  &arrayiter);

				/* Name */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "name";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Goal */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "goal";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "start";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* State */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "state";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "pre-start";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Processes */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "processes";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  (DBUS_TYPE_ARRAY_AS_STRING
								   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &subiter);

				dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
								  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &prociter);

				dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
								  NULL,
								  &structiter);

				str_value = "pre-start";
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
								&str_value);

				int32_value = 1014;
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
								&int32_value);

				dbus_message_iter_close_container (&prociter, &structiter);

				dbus_message_iter_close_container (&subiter, &prociter);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				dbus_message_iter_close_container (&iter, &arrayiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (client_conn);
			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		TEST_ALLOC_SAFE {
			job_class = nih_dbus_proxy_new (NULL, client_conn,
							dbus_bus_get_unique_name (server_conn),
							DBUS_PATH_UPSTART "/jobs/test",
							NULL, NULL);
			job = nih_dbus_proxy_new (NULL, client_conn,
						  dbus_bus_get_unique_name (server_conn),
						  DBUS_PATH_UPSTART "/jobs/test/_",
						  NULL, NULL);
		}

		str = job_status (NULL, job_class, job);

		if (test_alloc_failed
		    && (str == NULL)) {
			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			nih_free (job);
			nih_free (job_class);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ_STR (str, "test start/pre-start, process 1014");

		nih_free (str);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		nih_free (job);
		nih_free (job_class);
	}


	/* Check that we can generate a string for a job instance with
	 * a running post-stop process, since this is a standard state
	 * with a process, the pid should simply follow the state.
	 */
	TEST_FEATURE ("with running post-stop process");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the Get call for the name, reply with the
			 * name.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
			TEST_EQ_STR (property, "name");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "test";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&iter, &subiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetAll call for the properties, reply
			 * with the properties.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"GetAll"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test/_");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
								  (DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_VARIANT_AS_STRING
								   DBUS_DICT_ENTRY_END_CHAR_AS_STRING),
								  &arrayiter);

				/* Name */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "name";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Goal */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "goal";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "stop";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* State */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "state";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "post-stop";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Processes */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "processes";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  (DBUS_TYPE_ARRAY_AS_STRING
								   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &subiter);

				dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
								  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &prociter);

				dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
								  NULL,
								  &structiter);

				str_value = "post-stop";
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
								&str_value);

				int32_value = 9764;
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
								&int32_value);

				dbus_message_iter_close_container (&prociter, &structiter);

				dbus_message_iter_close_container (&subiter, &prociter);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				dbus_message_iter_close_container (&iter, &arrayiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (client_conn);
			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		TEST_ALLOC_SAFE {
			job_class = nih_dbus_proxy_new (NULL, client_conn,
							dbus_bus_get_unique_name (server_conn),
							DBUS_PATH_UPSTART "/jobs/test",
							NULL, NULL);
			job = nih_dbus_proxy_new (NULL, client_conn,
						  dbus_bus_get_unique_name (server_conn),
						  DBUS_PATH_UPSTART "/jobs/test/_",
						  NULL, NULL);
		}

		str = job_status (NULL, job_class, job);

		if (test_alloc_failed
		    && (str == NULL)) {
			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			nih_free (job);
			nih_free (job_class);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ_STR (str, "test stop/post-stop, process 9764");

		nih_free (str);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		nih_free (job);
		nih_free (job_class);
	}


	/* Check that we can generate a string for a job instance with
	 * a running post-start process, but no main process.  Since this
	 * is not a standard state process, the process name should be
	 * prefixed.
	 */
	TEST_FEATURE ("with running post-start process only");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the Get call for the name, reply with the
			 * name.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
			TEST_EQ_STR (property, "name");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "test";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&iter, &subiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetAll call for the properties, reply
			 * with the properties.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"GetAll"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test/_");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
								  (DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_VARIANT_AS_STRING
								   DBUS_DICT_ENTRY_END_CHAR_AS_STRING),
								  &arrayiter);

				/* Name */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "name";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Goal */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "goal";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "start";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* State */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "state";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "post-start";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Processes */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "processes";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  (DBUS_TYPE_ARRAY_AS_STRING
								   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &subiter);

				dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
								  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &prociter);

				dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
								  NULL,
								  &structiter);

				str_value = "post-start";
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
								&str_value);

				int32_value = 2137;
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
								&int32_value);

				dbus_message_iter_close_container (&prociter, &structiter);

				dbus_message_iter_close_container (&subiter, &prociter);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				dbus_message_iter_close_container (&iter, &arrayiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (client_conn);
			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		TEST_ALLOC_SAFE {
			job_class = nih_dbus_proxy_new (NULL, client_conn,
							dbus_bus_get_unique_name (server_conn),
							DBUS_PATH_UPSTART "/jobs/test",
							NULL, NULL);
			job = nih_dbus_proxy_new (NULL, client_conn,
						  dbus_bus_get_unique_name (server_conn),
						  DBUS_PATH_UPSTART "/jobs/test/_",
						  NULL, NULL);
		}

		str = job_status (NULL, job_class, job);

		if (test_alloc_failed
		    && (str == NULL)) {
			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			nih_free (job);
			nih_free (job_class);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ_STR (str, "test start/post-start, (post-start) process 2137");

		nih_free (str);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		nih_free (job);
		nih_free (job_class);
	}


	/* Check that we can generate a string for a job instance with
	 * a running pre-stop process, but no main process.  Since this
	 * is not a standard state process, the process name should be
	 * prefixed.
	 */
	TEST_FEATURE ("with running pre-stop process only");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the Get call for the name, reply with the
			 * name.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
			TEST_EQ_STR (property, "name");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "test";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&iter, &subiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetAll call for the properties, reply
			 * with the properties.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"GetAll"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test/_");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
								  (DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_VARIANT_AS_STRING
								   DBUS_DICT_ENTRY_END_CHAR_AS_STRING),
								  &arrayiter);

				/* Name */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "name";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Goal */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "goal";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "stop";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* State */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "state";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "pre-stop";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Processes */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "processes";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  (DBUS_TYPE_ARRAY_AS_STRING
								   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &subiter);

				dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
								  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &prociter);

				dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
								  NULL,
								  &structiter);

				str_value = "pre-stop";
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
								&str_value);

				int32_value = 7864;
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
								&int32_value);

				dbus_message_iter_close_container (&prociter, &structiter);

				dbus_message_iter_close_container (&subiter, &prociter);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				dbus_message_iter_close_container (&iter, &arrayiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (client_conn);
			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		TEST_ALLOC_SAFE {
			job_class = nih_dbus_proxy_new (NULL, client_conn,
							dbus_bus_get_unique_name (server_conn),
							DBUS_PATH_UPSTART "/jobs/test",
							NULL, NULL);
			job = nih_dbus_proxy_new (NULL, client_conn,
						  dbus_bus_get_unique_name (server_conn),
						  DBUS_PATH_UPSTART "/jobs/test/_",
						  NULL, NULL);
		}

		str = job_status (NULL, job_class, job);

		if (test_alloc_failed
		    && (str == NULL)) {
			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			nih_free (job);
			nih_free (job_class);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ_STR (str, "test stop/pre-stop, (pre-stop) process 7864");

		nih_free (str);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		nih_free (job);
		nih_free (job_class);
	}

	/* Check that we can generate a string for a job instance that
	 * is running with a main process and a simultaneous post-start
	 * process.  The main process should be output on the first line
	 * along with the state, the pid of the post-start process should
	 * follow indented on the next line.
	 */
	TEST_FEATURE ("with running main and post-start processes");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the Get call for the name, reply with the
			 * name.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
			TEST_EQ_STR (property, "name");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "test";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&iter, &subiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetAll call for the properties, reply
			 * with the properties.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"GetAll"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test/_");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
								  (DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_VARIANT_AS_STRING
								   DBUS_DICT_ENTRY_END_CHAR_AS_STRING),
								  &arrayiter);

				/* Name */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "name";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Goal */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "goal";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "start";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* State */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "state";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "post-start";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Processes */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "processes";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  (DBUS_TYPE_ARRAY_AS_STRING
								   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &subiter);

				dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
								  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &prociter);

				dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
								  NULL,
								  &structiter);

				str_value = "main";
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
								&str_value);

				int32_value = 3648;
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
								&int32_value);

				dbus_message_iter_close_container (&prociter, &structiter);

				dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
								  NULL,
								  &structiter);

				str_value = "post-start";
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
								&str_value);

				int32_value = 2137;
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
								&int32_value);

				dbus_message_iter_close_container (&prociter, &structiter);

				dbus_message_iter_close_container (&subiter, &prociter);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				dbus_message_iter_close_container (&iter, &arrayiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (client_conn);
			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		TEST_ALLOC_SAFE {
			job_class = nih_dbus_proxy_new (NULL, client_conn,
							dbus_bus_get_unique_name (server_conn),
							DBUS_PATH_UPSTART "/jobs/test",
							NULL, NULL);
			job = nih_dbus_proxy_new (NULL, client_conn,
						  dbus_bus_get_unique_name (server_conn),
						  DBUS_PATH_UPSTART "/jobs/test/_",
						  NULL, NULL);
		}

		str = job_status (NULL, job_class, job);

		if (test_alloc_failed
		    && (str == NULL)) {
			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			nih_free (job);
			nih_free (job_class);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ_STR (str, ("test start/post-start, process 3648\n"
				   "\tpost-start process 2137"));

		nih_free (str);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		nih_free (job);
		nih_free (job_class);
	}


	/* Check that we can generate a string for a job instance that
	 * is running with a main process and a simultaneous pre-stop
	 * process.  The main process should be output on the first line
	 * along with the state, the pid of the pre-stop process should
	 * follow indented on the next line.
	 */
	TEST_FEATURE ("with running main and pre-stop processes");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the Get call for the name, reply with the
			 * name.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
			TEST_EQ_STR (property, "name");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "test";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&iter, &subiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetAll call for the properties, reply
			 * with the properties.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"GetAll"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test/_");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
								  (DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_VARIANT_AS_STRING
								   DBUS_DICT_ENTRY_END_CHAR_AS_STRING),
								  &arrayiter);

				/* Name */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "name";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Goal */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "goal";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "stop";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* State */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "state";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "pre-stop";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Processes */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "processes";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  (DBUS_TYPE_ARRAY_AS_STRING
								   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &subiter);

				dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
								  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &prociter);

				dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
								  NULL,
								  &structiter);

				str_value = "main";
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
								&str_value);

				int32_value = 3648;
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
								&int32_value);

				dbus_message_iter_close_container (&prociter, &structiter);

				dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
								  NULL,
								  &structiter);

				str_value = "pre-stop";
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
								&str_value);

				int32_value = 7864;
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
								&int32_value);

				dbus_message_iter_close_container (&prociter, &structiter);

				dbus_message_iter_close_container (&subiter, &prociter);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				dbus_message_iter_close_container (&iter, &arrayiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (client_conn);
			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		TEST_ALLOC_SAFE {
			job_class = nih_dbus_proxy_new (NULL, client_conn,
							dbus_bus_get_unique_name (server_conn),
							DBUS_PATH_UPSTART "/jobs/test",
							NULL, NULL);
			job = nih_dbus_proxy_new (NULL, client_conn,
						  dbus_bus_get_unique_name (server_conn),
						  DBUS_PATH_UPSTART "/jobs/test/_",
						  NULL, NULL);
		}

		str = job_status (NULL, job_class, job);

		if (test_alloc_failed
		    && (str == NULL)) {
			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			nih_free (job);
			nih_free (job_class);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ_STR (str, ("test stop/pre-stop, process 3648\n"
				   "\tpre-stop process 7864"));

		nih_free (str);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		nih_free (job);
		nih_free (job_class);
	}


	/* Check that we can generate a string for a job instance that
	 * is running with a main process, but in one of the "unusual"
	 * states to catch the process in.  The process should be output
	 * as normal.
	 */
	TEST_FEATURE ("with running main process in spawned state");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the Get call for the name, reply with the
			 * name.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
			TEST_EQ_STR (property, "name");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "test";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&iter, &subiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetAll call for the properties, reply
			 * with the properties.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"GetAll"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test/_");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
								  (DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_VARIANT_AS_STRING
								   DBUS_DICT_ENTRY_END_CHAR_AS_STRING),
								  &arrayiter);

				/* Name */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "name";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Goal */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "goal";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "start";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* State */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "state";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "spawned";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Processes */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "processes";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  (DBUS_TYPE_ARRAY_AS_STRING
								   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &subiter);

				dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
								  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &prociter);

				dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
								  NULL,
								  &structiter);

				str_value = "main";
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
								&str_value);

				int32_value = 3648;
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
								&int32_value);

				dbus_message_iter_close_container (&prociter, &structiter);

				dbus_message_iter_close_container (&subiter, &prociter);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				dbus_message_iter_close_container (&iter, &arrayiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (client_conn);
			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		TEST_ALLOC_SAFE {
			job_class = nih_dbus_proxy_new (NULL, client_conn,
							dbus_bus_get_unique_name (server_conn),
							DBUS_PATH_UPSTART "/jobs/test",
							NULL, NULL);
			job = nih_dbus_proxy_new (NULL, client_conn,
						  dbus_bus_get_unique_name (server_conn),
						  DBUS_PATH_UPSTART "/jobs/test/_",
						  NULL, NULL);
		}

		str = job_status (NULL, job_class, job);

		if (test_alloc_failed
		    && (str == NULL)) {
			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			nih_free (job);
			nih_free (job_class);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ_STR (str, "test start/spawned, process 3648");

		nih_free (str);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		nih_free (job);
		nih_free (job_class);
	}


	/* Check that the function catches the job instance going away
	 * in the mean time (and the server returning the unknown method
	 * error), and handles that as an instance that has freshly
	 * stopped.
	 */
	TEST_FEATURE ("with unknown instance");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the Get call for the name, reply with the
			 * name.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
			TEST_EQ_STR (property, "name");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "test";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&iter, &subiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetAll call for the properties, reply
			 * with the unknown method error.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"GetAll"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test/_");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_error (method_call,
								DBUS_ERROR_UNKNOWN_METHOD,
								"Unknown method");
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (client_conn);
			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		TEST_ALLOC_SAFE {
			job_class = nih_dbus_proxy_new (NULL, client_conn,
							dbus_bus_get_unique_name (server_conn),
							DBUS_PATH_UPSTART "/jobs/test",
							NULL, NULL);
			job = nih_dbus_proxy_new (NULL, client_conn,
						  dbus_bus_get_unique_name (server_conn),
						  DBUS_PATH_UPSTART "/jobs/test/_",
						  NULL, NULL);
		}

		str = job_status (NULL, job_class, job);

		if (test_alloc_failed
		    && (str == NULL)) {
			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			nih_free (job);
			nih_free (job_class);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ_STR (str, "test stop/waiting");

		nih_free (str);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		nih_free (job);
		nih_free (job_class);
	}


	/* Check that NULL can be given as the job instance, and that the
	 * function only requests the name of the job class and outputs
	 * as if there was no instance.
	 */
	TEST_FEATURE ("with NULL for instance");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the Get call for the name, reply with the
			 * name.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
			TEST_EQ_STR (property, "name");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "test";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&iter, &subiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (client_conn);
			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		TEST_ALLOC_SAFE {
			job_class = nih_dbus_proxy_new (NULL, client_conn,
							dbus_bus_get_unique_name (server_conn),
							DBUS_PATH_UPSTART "/jobs/test",
							NULL, NULL);
		}

		str = job_status (NULL, job_class, NULL);

		if (test_alloc_failed
		    && (str == NULL)) {
			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			nih_free (job_class);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ_STR (str, "test stop/waiting");

		nih_free (str);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		nih_free (job_class);
	}


	/* Check that when the function is passed a bad job class proxy,
	 * it returns the error received from the server.
	 */
	TEST_FEATURE ("with bad job class");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the Get call for the name, reply with an
			 * error.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
			TEST_EQ_STR (property, "name");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_error (method_call,
								DBUS_ERROR_UNKNOWN_METHOD,
								"Unknown method");
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (client_conn);
			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		TEST_ALLOC_SAFE {
			job_class = nih_dbus_proxy_new (NULL, client_conn,
							dbus_bus_get_unique_name (server_conn),
							DBUS_PATH_UPSTART "/jobs/test",
							NULL, NULL);
			job = nih_dbus_proxy_new (NULL, client_conn,
						  dbus_bus_get_unique_name (server_conn),
						  DBUS_PATH_UPSTART "/jobs/test/_",
						  NULL, NULL);
		}

		str = job_status (NULL, job_class, job);

		TEST_EQ_P (str, NULL);

		err = nih_error_get ();

		if (test_alloc_failed
		    && (err->number == ENOMEM)) {
			nih_free (err);

			nih_free (job);
			nih_free (job_class);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ (err->number, NIH_DBUS_ERROR);
		TEST_ALLOC_SIZE (err, sizeof (NihDBusError));
		dbus_err = (NihDBusError *)err;

		TEST_EQ_STR (dbus_err->name, DBUS_ERROR_UNKNOWN_METHOD);
		nih_free (dbus_err);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		nih_free (job);
		nih_free (job_class);
	}


	TEST_DBUS_CLOSE (client_conn);
	TEST_DBUS_CLOSE (server_conn);
	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}


void
test_start_action (void)
{
	pid_t           dbus_pid;
	DBusConnection *server_conn;
	FILE *          output;
	FILE *          errors;
	pid_t           server_pid;
	DBusMessage *   method_call;
	DBusMessage *   reply = NULL;
	const char *    name_value;
	char **         args_value;
	int             args_elements;
	int             wait_value;
	const char *    str_value;
	const char *    interface;
	const char *    property;
	DBusMessageIter iter;
	DBusMessageIter subiter;
	DBusMessageIter arrayiter;
	DBusMessageIter dictiter;
	DBusMessageIter prociter;
	DBusMessageIter structiter;
	int32_t         int32_value;
	NihCommand      command;
	char *          args[4];
	int             ret = 0;
	int             status;

	TEST_FUNCTION ("start_action");
	TEST_DBUS (dbus_pid);
	TEST_DBUS_OPEN (server_conn);

	assert (dbus_bus_request_name (server_conn, DBUS_SERVICE_UPSTART,
				       0, NULL)
			== DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);

	TEST_DBUS_MESSAGE (server_conn, method_call);
	assert (dbus_message_is_signal (method_call, DBUS_INTERFACE_DBUS,
					"NameAcquired"));
	dbus_message_unref (method_call);

	system_bus = TRUE;
	dest_name = DBUS_SERVICE_UPSTART;
	dest_address = DBUS_ADDRESS_UPSTART;

	output = tmpfile ();
	errors = tmpfile ();


	/* Check that the start action with a single argument given looks
	 * up a job with that name, and then calls the Start command
	 * passing a NULL array for the events and TRUE for wait.  Once
	 * it receives the reply, it will then make queries to obtain the
	 * status of the command and print the output.
	 */
	TEST_FEATURE ("with single argument");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Start method call on the job object,
			 * make sure the environment and wait arguments
			 * are right and reply with an instance path to
			 * acknowledge.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"Start"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_BOOLEAN, &wait_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 0);
			dbus_free_string_array (args_value);

			TEST_TRUE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test/_";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* We allow the client to get the properties as many
			 * times as it likes, since it repeats this in out
			 * of memory cases.
			 */
			for (;;) {
				void hup_handler (int signum) { _exit (0); }
				signal (SIGHUP, hup_handler);

				/* Expect the Get call for the job name, reply with
				 * the name.
				 */
				TEST_DBUS_MESSAGE (server_conn, method_call);

				TEST_TRUE (dbus_message_is_method_call (method_call,
									DBUS_INTERFACE_PROPERTIES,
									"Get"));

				TEST_EQ_STR (dbus_message_get_path (method_call),
					     DBUS_PATH_UPSTART "/jobs/test");

				TEST_TRUE (dbus_message_get_args (method_call, NULL,
								  DBUS_TYPE_STRING, &interface,
								  DBUS_TYPE_STRING, &property,
								  DBUS_TYPE_INVALID));

				TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
				TEST_EQ_STR (property, "name");

				TEST_ALLOC_SAFE {
					reply = dbus_message_new_method_return (method_call);

					dbus_message_iter_init_append (reply, &iter);

					dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "test";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&iter, &subiter);
				}

				dbus_connection_send (server_conn, reply, NULL);
				dbus_connection_flush (server_conn);

				dbus_message_unref (method_call);
				dbus_message_unref (reply);

				/* Expect the GetAll call for the instance properties,
				 * reply with the properties.
				 */
				TEST_DBUS_MESSAGE (server_conn, method_call);

				TEST_TRUE (dbus_message_is_method_call (method_call,
									DBUS_INTERFACE_PROPERTIES,
									"GetAll"));

				TEST_EQ_STR (dbus_message_get_path (method_call),
					     DBUS_PATH_UPSTART "/jobs/test/_");

				TEST_TRUE (dbus_message_get_args (method_call, NULL,
								  DBUS_TYPE_STRING, &interface,
								  DBUS_TYPE_INVALID));

				TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

				TEST_ALLOC_SAFE {
					reply = dbus_message_new_method_return (method_call);

					dbus_message_iter_init_append (reply, &iter);

					dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
									  (DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
									   DBUS_TYPE_STRING_AS_STRING
									   DBUS_TYPE_VARIANT_AS_STRING
									   DBUS_DICT_ENTRY_END_CHAR_AS_STRING),
									  &arrayiter);

					/* Name */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "name";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					/* Goal */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "goal";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "start";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					/* State */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "state";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "running";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					/* Processes */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "processes";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  (DBUS_TYPE_ARRAY_AS_STRING
									   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
									   DBUS_TYPE_STRING_AS_STRING
									   DBUS_TYPE_INT32_AS_STRING
									   DBUS_STRUCT_END_CHAR_AS_STRING),
									  &subiter);

					dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
									  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
									   DBUS_TYPE_STRING_AS_STRING
									   DBUS_TYPE_INT32_AS_STRING
									   DBUS_STRUCT_END_CHAR_AS_STRING),
									  &prociter);

					dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
									  NULL,
									  &structiter);

					str_value = "main";
					dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
									&str_value);

					int32_value = 3648;
					dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
									&int32_value);

					dbus_message_iter_close_container (&prociter, &structiter);

					dbus_message_iter_close_container (&subiter, &prociter);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					dbus_message_iter_close_container (&iter, &arrayiter);
				}

				dbus_connection_send (server_conn, reply, NULL);
				dbus_connection_flush (server_conn);

				dbus_message_unref (method_call);
				dbus_message_unref (reply);
			}

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = start_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_EQ (output, "test start/running, process 3648\n");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGHUP);
		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);
	}


	/* Check that additional arguments to the start action are passed
	 * as entries in the environment argument of the command.
	 */
	TEST_FEATURE ("with multiple arguments");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Start method call on the job object,
			 * make sure the environment and wait arguments
			 * are right and reply with an instance path to
			 * acknowledge.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"Start"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_BOOLEAN, &wait_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 2);
			TEST_EQ_STR (args_value[0], "FOO=foo");
			TEST_EQ_STR (args_value[1], "BAR=bar");
			dbus_free_string_array (args_value);

			TEST_TRUE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test/_";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* We allow the client to get the properties as many
			 * times as it likes, since it repeats this in out
			 * of memory cases.
			 */
			for (;;) {
				void hup_handler (int signum) { _exit (0); }
				signal (SIGHUP, hup_handler);

				/* Expect the Get call for the job name, reply with
				 * the name.
				 */
				TEST_DBUS_MESSAGE (server_conn, method_call);

				TEST_TRUE (dbus_message_is_method_call (method_call,
									DBUS_INTERFACE_PROPERTIES,
									"Get"));

				TEST_EQ_STR (dbus_message_get_path (method_call),
					     DBUS_PATH_UPSTART "/jobs/test");

				TEST_TRUE (dbus_message_get_args (method_call, NULL,
								  DBUS_TYPE_STRING, &interface,
								  DBUS_TYPE_STRING, &property,
								  DBUS_TYPE_INVALID));

				TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
				TEST_EQ_STR (property, "name");

				TEST_ALLOC_SAFE {
					reply = dbus_message_new_method_return (method_call);

					dbus_message_iter_init_append (reply, &iter);

					dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "test";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&iter, &subiter);
				}

				dbus_connection_send (server_conn, reply, NULL);
				dbus_connection_flush (server_conn);

				dbus_message_unref (method_call);
				dbus_message_unref (reply);

				/* Expect the GetAll call for the instance properties,
				 * reply with the properties.
				 */
				TEST_DBUS_MESSAGE (server_conn, method_call);

				TEST_TRUE (dbus_message_is_method_call (method_call,
									DBUS_INTERFACE_PROPERTIES,
									"GetAll"));

				TEST_EQ_STR (dbus_message_get_path (method_call),
					     DBUS_PATH_UPSTART "/jobs/test/_");

				TEST_TRUE (dbus_message_get_args (method_call, NULL,
								  DBUS_TYPE_STRING, &interface,
								  DBUS_TYPE_INVALID));

				TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

				TEST_ALLOC_SAFE {
					reply = dbus_message_new_method_return (method_call);

					dbus_message_iter_init_append (reply, &iter);

					dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
									  (DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
									   DBUS_TYPE_STRING_AS_STRING
									   DBUS_TYPE_VARIANT_AS_STRING
									   DBUS_DICT_ENTRY_END_CHAR_AS_STRING),
									  &arrayiter);

					/* Name */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "name";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					/* Goal */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "goal";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "start";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					/* State */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "state";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "running";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					/* Processes */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "processes";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  (DBUS_TYPE_ARRAY_AS_STRING
									   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
									   DBUS_TYPE_STRING_AS_STRING
									   DBUS_TYPE_INT32_AS_STRING
									   DBUS_STRUCT_END_CHAR_AS_STRING),
									  &subiter);

					dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
									  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
									   DBUS_TYPE_STRING_AS_STRING
									   DBUS_TYPE_INT32_AS_STRING
									   DBUS_STRUCT_END_CHAR_AS_STRING),
									  &prociter);

					dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
									  NULL,
									  &structiter);

					str_value = "main";
					dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
									&str_value);

					int32_value = 3648;
					dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
									&int32_value);

					dbus_message_iter_close_container (&prociter, &structiter);

					dbus_message_iter_close_container (&subiter, &prociter);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					dbus_message_iter_close_container (&iter, &arrayiter);
				}

				dbus_connection_send (server_conn, reply, NULL);
				dbus_connection_flush (server_conn);

				dbus_message_unref (method_call);
				dbus_message_unref (reply);
			}

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = "FOO=foo";
		args[2] = "BAR=bar";
		args[3] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = start_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_EQ (output, "test start/running, process 3648\n");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGHUP);
		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);
	}


	/* Check that the --no-wait option results in the method call
	 * being made with wait as FALSE.
	 */
	TEST_FEATURE ("with no wait");
	no_wait = TRUE;

	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Start method call on the job object,
			 * make sure the environment and wait arguments
			 * are right and reply with an instance path to
			 * acknowledge.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"Start"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_BOOLEAN, &wait_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 0);
			dbus_free_string_array (args_value);

			TEST_FALSE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test/_";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* We allow the client to get the properties as many
			 * times as it likes, since it repeats this in out
			 * of memory cases.
			 */
			for (;;) {
				void hup_handler (int signum) { _exit (0); }
				signal (SIGHUP, hup_handler);

				/* Expect the Get call for the job name, reply with
				 * the name.
				 */
				TEST_DBUS_MESSAGE (server_conn, method_call);

				TEST_TRUE (dbus_message_is_method_call (method_call,
									DBUS_INTERFACE_PROPERTIES,
									"Get"));

				TEST_EQ_STR (dbus_message_get_path (method_call),
					     DBUS_PATH_UPSTART "/jobs/test");

				TEST_TRUE (dbus_message_get_args (method_call, NULL,
								  DBUS_TYPE_STRING, &interface,
								  DBUS_TYPE_STRING, &property,
								  DBUS_TYPE_INVALID));

				TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
				TEST_EQ_STR (property, "name");

				TEST_ALLOC_SAFE {
					reply = dbus_message_new_method_return (method_call);

					dbus_message_iter_init_append (reply, &iter);

					dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "test";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&iter, &subiter);
				}

				dbus_connection_send (server_conn, reply, NULL);
				dbus_connection_flush (server_conn);

				dbus_message_unref (method_call);
				dbus_message_unref (reply);

				/* Expect the GetAll call for the instance properties,
				 * reply with the properties.
				 */
				TEST_DBUS_MESSAGE (server_conn, method_call);

				TEST_TRUE (dbus_message_is_method_call (method_call,
									DBUS_INTERFACE_PROPERTIES,
									"GetAll"));

				TEST_EQ_STR (dbus_message_get_path (method_call),
					     DBUS_PATH_UPSTART "/jobs/test/_");

				TEST_TRUE (dbus_message_get_args (method_call, NULL,
								  DBUS_TYPE_STRING, &interface,
								  DBUS_TYPE_INVALID));

				TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

				TEST_ALLOC_SAFE {
					reply = dbus_message_new_method_return (method_call);

					dbus_message_iter_init_append (reply, &iter);

					dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
									  (DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
									   DBUS_TYPE_STRING_AS_STRING
									   DBUS_TYPE_VARIANT_AS_STRING
									   DBUS_DICT_ENTRY_END_CHAR_AS_STRING),
									  &arrayiter);

					/* Name */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "name";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					/* Goal */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "goal";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "start";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					/* State */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "state";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "running";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					/* Processes */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "processes";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  (DBUS_TYPE_ARRAY_AS_STRING
									   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
									   DBUS_TYPE_STRING_AS_STRING
									   DBUS_TYPE_INT32_AS_STRING
									   DBUS_STRUCT_END_CHAR_AS_STRING),
									  &subiter);

					dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
									  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
									   DBUS_TYPE_STRING_AS_STRING
									   DBUS_TYPE_INT32_AS_STRING
									   DBUS_STRUCT_END_CHAR_AS_STRING),
									  &prociter);

					dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
									  NULL,
									  &structiter);

					str_value = "main";
					dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
									&str_value);

					int32_value = 3648;
					dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
									&int32_value);

					dbus_message_iter_close_container (&prociter, &structiter);

					dbus_message_iter_close_container (&subiter, &prociter);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					dbus_message_iter_close_container (&iter, &arrayiter);
				}

				dbus_connection_send (server_conn, reply, NULL);
				dbus_connection_flush (server_conn);

				dbus_message_unref (method_call);
				dbus_message_unref (reply);
			}

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = start_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_EQ (output, "test start/running, process 3648\n");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGHUP);
		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);
	}

	no_wait = FALSE;


	/* Check that the start action may be called without arguments
	 * when inside an instance process, due to the environment variables
	 * set there.  The job should be stilled looked up, but then the
	 * instance should be looked up via GetInstanceByName and the Start
	 * command run directly on the instance instead in a no-wait fashion.
	 */
	TEST_FEATURE ("with no arguments when called from job process");
	setenv ("UPSTART_JOB", "test", TRUE);
	setenv ("UPSTART_INSTANCE", "foo", TRUE);

	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetInstanceByName method call on the
			 * job object, make sure the instance name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"GetInstanceByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "foo");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test/foo";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Start method call on the instance object,
			 * make the wait argument is FALSE and reply to
			 * to acknowledge.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_INSTANCE,
								"Start"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test/foo");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_BOOLEAN, &wait_value,
							  DBUS_TYPE_INVALID));

			TEST_FALSE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* We allow the client to get the properties as many
			 * times as it likes, since it repeats this in out
			 * of memory cases.
			 */
			for (;;) {
				void hup_handler (int signum) { _exit (0); }
				signal (SIGHUP, hup_handler);

				/* Expect the Get call for the job name, reply with
				 * the name.
				 */
				TEST_DBUS_MESSAGE (server_conn, method_call);

				TEST_TRUE (dbus_message_is_method_call (method_call,
									DBUS_INTERFACE_PROPERTIES,
									"Get"));

				TEST_EQ_STR (dbus_message_get_path (method_call),
					     DBUS_PATH_UPSTART "/jobs/test");

				TEST_TRUE (dbus_message_get_args (method_call, NULL,
								  DBUS_TYPE_STRING, &interface,
								  DBUS_TYPE_STRING, &property,
								  DBUS_TYPE_INVALID));

				TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
				TEST_EQ_STR (property, "name");

				TEST_ALLOC_SAFE {
					reply = dbus_message_new_method_return (method_call);

					dbus_message_iter_init_append (reply, &iter);

					dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "test";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&iter, &subiter);
				}

				dbus_connection_send (server_conn, reply, NULL);
				dbus_connection_flush (server_conn);

				dbus_message_unref (method_call);
				dbus_message_unref (reply);

				/* Expect the GetAll call for the instance properties,
				 * reply with the properties.
				 */
				TEST_DBUS_MESSAGE (server_conn, method_call);

				TEST_TRUE (dbus_message_is_method_call (method_call,
									DBUS_INTERFACE_PROPERTIES,
									"GetAll"));

				TEST_EQ_STR (dbus_message_get_path (method_call),
					     DBUS_PATH_UPSTART "/jobs/test/foo");

				TEST_TRUE (dbus_message_get_args (method_call, NULL,
								  DBUS_TYPE_STRING, &interface,
								  DBUS_TYPE_INVALID));

				TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

				TEST_ALLOC_SAFE {
					reply = dbus_message_new_method_return (method_call);

					dbus_message_iter_init_append (reply, &iter);

					dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
									  (DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
									   DBUS_TYPE_STRING_AS_STRING
									   DBUS_TYPE_VARIANT_AS_STRING
									   DBUS_DICT_ENTRY_END_CHAR_AS_STRING),
									  &arrayiter);

					/* Name */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "name";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "foo";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					/* Goal */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "goal";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "start";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					/* State */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "state";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "running";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					/* Processes */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "processes";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  (DBUS_TYPE_ARRAY_AS_STRING
									   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
									   DBUS_TYPE_STRING_AS_STRING
									   DBUS_TYPE_INT32_AS_STRING
									   DBUS_STRUCT_END_CHAR_AS_STRING),
									  &subiter);

					dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
									  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
									   DBUS_TYPE_STRING_AS_STRING
									   DBUS_TYPE_INT32_AS_STRING
									   DBUS_STRUCT_END_CHAR_AS_STRING),
									  &prociter);

					dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
									  NULL,
									  &structiter);

					str_value = "main";
					dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
									&str_value);

					int32_value = 3648;
					dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
									&int32_value);

					dbus_message_iter_close_container (&prociter, &structiter);

					dbus_message_iter_close_container (&subiter, &prociter);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					dbus_message_iter_close_container (&iter, &arrayiter);
				}

				dbus_connection_send (server_conn, reply, NULL);
				dbus_connection_flush (server_conn);

				dbus_message_unref (method_call);
				dbus_message_unref (reply);
			}

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = start_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_EQ (output, "test (foo) start/running, process 3648\n");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGHUP);
		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);
	}

	unsetenv ("UPSTART_JOB");
	unsetenv ("UPSTART_INSTANCE");


	/* Check that if an error is received from the GetJobByName call,
	 * the message attached is printed to standard error and the
	 * command exits.
	 */
	TEST_FEATURE ("with error reply to GetJobByName");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with an error.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_error (method_call,
								DBUS_ERROR_UNKNOWN_METHOD,
								"Unknown method");
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = start_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_MATCH (errors, "test: *\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGTERM);
		waitpid (server_pid, NULL, 0);
	}


	/* Check that if an error is received from the Start call,
	 * the message attached is printed to standard error and the
	 * command exits.
	 */
	TEST_FEATURE ("with error reply to Start");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Start method call on the job object,
			 * make sure the environment and wait arguments
			 * are right and reply with an error.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"Start"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_BOOLEAN, &wait_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 0);
			dbus_free_string_array (args_value);

			TEST_TRUE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_error (method_call,
								DBUS_ERROR_UNKNOWN_METHOD,
								"Unknown method");
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = start_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_MATCH (errors, "test: *\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGTERM);
		waitpid (server_pid, NULL, 0);
	}


	/* Check that if an error is received from the status query,
	 * the message attached is printed to standard error and the
	 * command exits.
	 */
	TEST_FEATURE ("with error reply to status query");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Start method call on the job object,
			 * make sure the environment and wait arguments
			 * are right and reply with an instance path to
			 * acknowledge.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"Start"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_BOOLEAN, &wait_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 0);
			dbus_free_string_array (args_value);

			TEST_TRUE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test/_";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Get call for the job name, reply with
			 * an error.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
				     DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
			TEST_EQ_STR (property, "name");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_error (method_call,
								DBUS_ERROR_UNKNOWN_METHOD,
								"Unknown method");
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = start_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_MATCH (errors, "test: *\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGTERM);
		waitpid (server_pid, NULL, 0);
	}


	/* Check that a missing argument results in an error being output
	 * to stderr along with a suggestion of help.
	 */
	TEST_FEATURE ("with missing argument");
	TEST_ALLOC_FAIL {
		memset (&command, 0, sizeof command);

		args[0] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = start_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_EQ (errors, "test: missing job name\n");
		TEST_FILE_EQ (errors, "Try `test --help' for more information.\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);
	}


	fclose (errors);
	fclose (output);

	TEST_DBUS_CLOSE (server_conn);
	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}


void
test_stop_action (void)
{
	pid_t           dbus_pid;
	DBusConnection *server_conn;
	FILE *          output;
	FILE *          errors;
	pid_t           server_pid;
	DBusMessage *   method_call;
	DBusMessage *   reply = NULL;
	const char *    name_value;
	char **         args_value;
	int             args_elements;
	int             wait_value;
	const char *    str_value;
	const char *    interface;
	const char *    property;
	DBusMessageIter iter;
	DBusMessageIter subiter;
	NihCommand      command;
	char *          args[4];
	int             ret = 0;
	int             status;

	TEST_FUNCTION ("stop_action");
	TEST_DBUS (dbus_pid);
	TEST_DBUS_OPEN (server_conn);

	assert (dbus_bus_request_name (server_conn, DBUS_SERVICE_UPSTART,
				       0, NULL)
			== DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);

	TEST_DBUS_MESSAGE (server_conn, method_call);
	assert (dbus_message_is_signal (method_call, DBUS_INTERFACE_DBUS,
					"NameAcquired"));
	dbus_message_unref (method_call);

	system_bus = TRUE;
	dest_name = DBUS_SERVICE_UPSTART;
	dest_address = DBUS_ADDRESS_UPSTART;

	output = tmpfile ();
	errors = tmpfile ();


	/* Check that the stop action with a single argument given looks
	 * up a job with that name, then looks up the instance with a NULL
	 * arguments array (to get the path for later) and then calls the
	 * Stop command passing a NULL array for the events and TRUE for wait.
	 * Once it receives the reply, it will then make queries to obtain the
	 * status of the command and print the output.
	 */
	TEST_FEATURE ("with single argument");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetInstance method call on the
			 * job object, make sure the environment args are
			 * passed and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"GetInstance"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 0);
			dbus_free_string_array (args_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test/_";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Stop method call on the job object,
			 * make sure the environment and wait arguments
			 * are right and reply to acknowledge.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"Stop"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_BOOLEAN, &wait_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 0);
			dbus_free_string_array (args_value);

			TEST_TRUE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* We allow the client to get the properties as many
			 * times as it likes, since it repeats this in out
			 * of memory cases.
			 */
			for (;;) {
				void hup_handler (int signum) { _exit (0); }
				signal (SIGHUP, hup_handler);

				/* Expect the Get call for the job name, reply with
				 * the name.
				 */
				TEST_DBUS_MESSAGE (server_conn, method_call);

				TEST_TRUE (dbus_message_is_method_call (method_call,
									DBUS_INTERFACE_PROPERTIES,
									"Get"));

				TEST_EQ_STR (dbus_message_get_path (method_call),
					     DBUS_PATH_UPSTART "/jobs/test");

				TEST_TRUE (dbus_message_get_args (method_call, NULL,
								  DBUS_TYPE_STRING, &interface,
								  DBUS_TYPE_STRING, &property,
								  DBUS_TYPE_INVALID));

				TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
				TEST_EQ_STR (property, "name");

				TEST_ALLOC_SAFE {
					reply = dbus_message_new_method_return (method_call);

					dbus_message_iter_init_append (reply, &iter);

					dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "test";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&iter, &subiter);
				}

				dbus_connection_send (server_conn, reply, NULL);
				dbus_connection_flush (server_conn);

				dbus_message_unref (method_call);
				dbus_message_unref (reply);

				/* Expect the GetAll call for the instance properties,
				 * reply with an unknown method error since
				 * there will be no instance at this point.
				 */
				TEST_DBUS_MESSAGE (server_conn, method_call);

				TEST_TRUE (dbus_message_is_method_call (method_call,
									DBUS_INTERFACE_PROPERTIES,
									"GetAll"));

				TEST_EQ_STR (dbus_message_get_path (method_call),
					     DBUS_PATH_UPSTART "/jobs/test/_");

				TEST_TRUE (dbus_message_get_args (method_call, NULL,
								  DBUS_TYPE_STRING, &interface,
								  DBUS_TYPE_INVALID));

				TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

				TEST_ALLOC_SAFE {
					reply = dbus_message_new_error (method_call,
									DBUS_ERROR_UNKNOWN_METHOD,
									"Unknown method");
				}

				dbus_connection_send (server_conn, reply, NULL);
				dbus_connection_flush (server_conn);

				dbus_message_unref (method_call);
				dbus_message_unref (reply);
			}

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = stop_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_EQ (output, "test stop/waiting\n");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGHUP);
		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);
	}


	/* Check that additional arguments to the stop action are passed
	 * as entries in the environment argument of the command.
	 */
	TEST_FEATURE ("with multiple arguments");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetInstance method call on the
			 * job object, make sure the environment args are
			 * passed and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"GetInstance"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 2);
			TEST_EQ_STR (args_value[0], "FOO=foo");
			TEST_EQ_STR (args_value[1], "BAR=bar");
			dbus_free_string_array (args_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test/_";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Stop method call on the job object,
			 * make sure the environment and wait arguments
			 * are right and reply to acknowledge.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"Stop"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_BOOLEAN, &wait_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 2);
			TEST_EQ_STR (args_value[0], "FOO=foo");
			TEST_EQ_STR (args_value[1], "BAR=bar");
			dbus_free_string_array (args_value);

			TEST_TRUE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* We allow the client to get the properties as many
			 * times as it likes, since it repeats this in out
			 * of memory cases.
			 */
			for (;;) {
				void hup_handler (int signum) { _exit (0); }
				signal (SIGHUP, hup_handler);

				/* Expect the Get call for the job name, reply with
				 * the name.
				 */
				TEST_DBUS_MESSAGE (server_conn, method_call);

				TEST_TRUE (dbus_message_is_method_call (method_call,
									DBUS_INTERFACE_PROPERTIES,
									"Get"));

				TEST_EQ_STR (dbus_message_get_path (method_call),
					     DBUS_PATH_UPSTART "/jobs/test");

				TEST_TRUE (dbus_message_get_args (method_call, NULL,
								  DBUS_TYPE_STRING, &interface,
								  DBUS_TYPE_STRING, &property,
								  DBUS_TYPE_INVALID));

				TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
				TEST_EQ_STR (property, "name");

				TEST_ALLOC_SAFE {
					reply = dbus_message_new_method_return (method_call);

					dbus_message_iter_init_append (reply, &iter);

					dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "test";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&iter, &subiter);
				}

				dbus_connection_send (server_conn, reply, NULL);
				dbus_connection_flush (server_conn);

				dbus_message_unref (method_call);
				dbus_message_unref (reply);

				/* Expect the GetAll call for the instance properties,
				 * reply with an unknown method error since
				 * there will be no instance at this point.
				 */
				TEST_DBUS_MESSAGE (server_conn, method_call);

				TEST_TRUE (dbus_message_is_method_call (method_call,
									DBUS_INTERFACE_PROPERTIES,
									"GetAll"));

				TEST_EQ_STR (dbus_message_get_path (method_call),
					     DBUS_PATH_UPSTART "/jobs/test/_");

				TEST_TRUE (dbus_message_get_args (method_call, NULL,
								  DBUS_TYPE_STRING, &interface,
								  DBUS_TYPE_INVALID));

				TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

				TEST_ALLOC_SAFE {
					reply = dbus_message_new_error (method_call,
									DBUS_ERROR_UNKNOWN_METHOD,
									"Unknown method");
				}

				dbus_connection_send (server_conn, reply, NULL);
				dbus_connection_flush (server_conn);

				dbus_message_unref (method_call);
				dbus_message_unref (reply);
			}

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = "FOO=foo";
		args[2] = "BAR=bar";
		args[3] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = stop_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_EQ (output, "test stop/waiting\n");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGHUP);
		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);
	}


	/* Check that the --no-wait option results in the method call
	 * being made with wait as FALSE.
	 */
	TEST_FEATURE ("with no wait");
	no_wait = TRUE;

	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetInstance method call on the
			 * job object, make sure the environment args are
			 * passed and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"GetInstance"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 0);
			dbus_free_string_array (args_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test/_";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Stop method call on the job object,
			 * make sure the environment and wait arguments
			 * are right and reply to acknowledge.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"Stop"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_BOOLEAN, &wait_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 0);
			dbus_free_string_array (args_value);

			TEST_FALSE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* We allow the client to get the properties as many
			 * times as it likes, since it repeats this in out
			 * of memory cases.
			 */
			for (;;) {
				void hup_handler (int signum) { _exit (0); }
				signal (SIGHUP, hup_handler);

				/* Expect the Get call for the job name, reply with
				 * the name.
				 */
				TEST_DBUS_MESSAGE (server_conn, method_call);

				TEST_TRUE (dbus_message_is_method_call (method_call,
									DBUS_INTERFACE_PROPERTIES,
									"Get"));

				TEST_EQ_STR (dbus_message_get_path (method_call),
					     DBUS_PATH_UPSTART "/jobs/test");

				TEST_TRUE (dbus_message_get_args (method_call, NULL,
								  DBUS_TYPE_STRING, &interface,
								  DBUS_TYPE_STRING, &property,
								  DBUS_TYPE_INVALID));

				TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
				TEST_EQ_STR (property, "name");

				TEST_ALLOC_SAFE {
					reply = dbus_message_new_method_return (method_call);

					dbus_message_iter_init_append (reply, &iter);

					dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "test";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&iter, &subiter);
				}

				dbus_connection_send (server_conn, reply, NULL);
				dbus_connection_flush (server_conn);

				dbus_message_unref (method_call);
				dbus_message_unref (reply);

				/* Expect the GetAll call for the instance properties,
				 * reply with an unknown method error since
				 * there will be no instance at this point.
				 */
				TEST_DBUS_MESSAGE (server_conn, method_call);

				TEST_TRUE (dbus_message_is_method_call (method_call,
									DBUS_INTERFACE_PROPERTIES,
									"GetAll"));

				TEST_EQ_STR (dbus_message_get_path (method_call),
					     DBUS_PATH_UPSTART "/jobs/test/_");

				TEST_TRUE (dbus_message_get_args (method_call, NULL,
								  DBUS_TYPE_STRING, &interface,
								  DBUS_TYPE_INVALID));

				TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

				TEST_ALLOC_SAFE {
					reply = dbus_message_new_error (method_call,
									DBUS_ERROR_UNKNOWN_METHOD,
									"Unknown method");
				}

				dbus_connection_send (server_conn, reply, NULL);
				dbus_connection_flush (server_conn);

				dbus_message_unref (method_call);
				dbus_message_unref (reply);
			}

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = stop_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_EQ (output, "test stop/waiting\n");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGHUP);
		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);
	}

	no_wait = FALSE;


	/* Check that the stop action may be called without arguments
	 * when inside an instance process, due to the environment variables
	 * set there.  The job should be still looked up, but then the
	 * instance should be looked up via GetInstanceByName and the Stop
	 * command run directly on the instance instead in a no-wait fashion.
	 */
	TEST_FEATURE ("with no arguments when called from job process");
	setenv ("UPSTART_JOB", "test", TRUE);
	setenv ("UPSTART_INSTANCE", "foo", TRUE);

	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetInstanceByName method call on the
			 * job object, make sure the instance name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"GetInstanceByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "foo");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test/foo";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Stop method call on the instance object,
			 * make the wait argument is FALSE and reply to
			 * to acknowledge.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_INSTANCE,
								"Stop"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test/foo");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_BOOLEAN, &wait_value,
							  DBUS_TYPE_INVALID));

			TEST_FALSE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* We allow the client to get the properties as many
			 * times as it likes, since it repeats this in out
			 * of memory cases.
			 */
			for (;;) {
				void hup_handler (int signum) { _exit (0); }
				signal (SIGHUP, hup_handler);

				/* Expect the Get call for the job name, reply with
				 * the name.
				 */
				TEST_DBUS_MESSAGE (server_conn, method_call);

				TEST_TRUE (dbus_message_is_method_call (method_call,
									DBUS_INTERFACE_PROPERTIES,
									"Get"));

				TEST_EQ_STR (dbus_message_get_path (method_call),
					     DBUS_PATH_UPSTART "/jobs/test");

				TEST_TRUE (dbus_message_get_args (method_call, NULL,
								  DBUS_TYPE_STRING, &interface,
								  DBUS_TYPE_STRING, &property,
								  DBUS_TYPE_INVALID));

				TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
				TEST_EQ_STR (property, "name");

				TEST_ALLOC_SAFE {
					reply = dbus_message_new_method_return (method_call);

					dbus_message_iter_init_append (reply, &iter);

					dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "test";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&iter, &subiter);
				}

				dbus_connection_send (server_conn, reply, NULL);
				dbus_connection_flush (server_conn);

				dbus_message_unref (method_call);
				dbus_message_unref (reply);

				/* Expect the GetAll call for the instance properties,
				 * reply with an unknown method error since
				 * there will be no instance at this point.
				 */
				TEST_DBUS_MESSAGE (server_conn, method_call);

				TEST_TRUE (dbus_message_is_method_call (method_call,
									DBUS_INTERFACE_PROPERTIES,
									"GetAll"));

				TEST_EQ_STR (dbus_message_get_path (method_call),
					     DBUS_PATH_UPSTART "/jobs/test/foo");

				TEST_TRUE (dbus_message_get_args (method_call, NULL,
								  DBUS_TYPE_STRING, &interface,
								  DBUS_TYPE_INVALID));

				TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

				TEST_ALLOC_SAFE {
					reply = dbus_message_new_error (method_call,
									DBUS_ERROR_UNKNOWN_METHOD,
									"Unknown method");
				}

				dbus_connection_send (server_conn, reply, NULL);
				dbus_connection_flush (server_conn);

				dbus_message_unref (method_call);
				dbus_message_unref (reply);
			}

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = stop_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_EQ (output, "test stop/waiting\n");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGHUP);
		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);
	}

	unsetenv ("UPSTART_JOB");
	unsetenv ("UPSTART_INSTANCE");


	/* Check that if an error is received from the GetJobByName call,
	 * the message attached is printed to standard error and the
	 * command exits.
	 */
	TEST_FEATURE ("with error reply to GetJobByName");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with an error.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_error (method_call,
								DBUS_ERROR_UNKNOWN_METHOD,
								"Unknown method");
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = stop_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_MATCH (errors, "test: *\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGTERM);
		waitpid (server_pid, NULL, 0);
	}


	/* Check that if an error is received from the GetInstance call,
	 * the message attached is printed to standard error and the
	 * command exits.
	 */
	TEST_FEATURE ("with error reply to GetInstance");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetInstance method call on the
			 * job object, make sure the environment args are
			 * passed and reply with an error.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"GetInstance"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 0);
			dbus_free_string_array (args_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_error (method_call,
								DBUS_ERROR_UNKNOWN_METHOD,
								"Unknown method");
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = stop_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_MATCH (errors, "test: *\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGTERM);
		waitpid (server_pid, NULL, 0);
	}


	/* Check that if an error is received from the Stop call,
	 * the message attached is printed to standard error and the
	 * command exits.
	 */
	TEST_FEATURE ("with error reply to Stop");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetInstance method call on the
			 * job object, make sure the environment args are
			 * passed and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"GetInstance"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 0);
			dbus_free_string_array (args_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test/_";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Stop method call on the job object,
			 * make sure the environment and wait arguments
			 * are right and reply with an error.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"Stop"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_BOOLEAN, &wait_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 0);
			dbus_free_string_array (args_value);

			TEST_TRUE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_error (method_call,
								DBUS_ERROR_UNKNOWN_METHOD,
								"Unknown method");
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = stop_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_MATCH (errors, "test: *\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGTERM);
		waitpid (server_pid, NULL, 0);
	}


	/* Check that if an error is received from the status query,
	 * the message attached is printed to standard error and the
	 * command exits.
	 */
	TEST_FEATURE ("with error reply to status query");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetInstance method call on the
			 * job object, make sure the environment args are
			 * passed and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"GetInstance"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 0);
			dbus_free_string_array (args_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test/_";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Stop method call on the job object,
			 * make sure the environment and wait arguments
			 * are right and reply with an instance path to
			 * acknowledge.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"Stop"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_BOOLEAN, &wait_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 0);
			dbus_free_string_array (args_value);

			TEST_TRUE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test/_";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Get call for the job name, reply with
			 * an error.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
				     DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
			TEST_EQ_STR (property, "name");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_error (method_call,
								DBUS_ERROR_UNKNOWN_METHOD,
								"Unknown method");
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = stop_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_MATCH (errors, "test: *\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGTERM);
		waitpid (server_pid, NULL, 0);
	}


	/* Check that a missing argument results in an error being output
	 * to stderr along with a suggestion of help.
	 */
	TEST_FEATURE ("with missing argument");
	TEST_ALLOC_FAIL {
		memset (&command, 0, sizeof command);

		args[0] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = stop_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_EQ (errors, "test: missing job name\n");
		TEST_FILE_EQ (errors, "Try `test --help' for more information.\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);
	}


	fclose (errors);
	fclose (output);

	TEST_DBUS_CLOSE (server_conn);
	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}


void
test_restart_action (void)
{
	pid_t           dbus_pid;
	DBusConnection *server_conn;
	FILE *          output;
	FILE *          errors;
	pid_t           server_pid;
	DBusMessage *   method_call;
	DBusMessage *   reply = NULL;
	const char *    name_value;
	char **         args_value;
	int             args_elements;
	int             wait_value;
	const char *    str_value;
	const char *    interface;
	const char *    property;
	DBusMessageIter iter;
	DBusMessageIter subiter;
	DBusMessageIter arrayiter;
	DBusMessageIter dictiter;
	DBusMessageIter prociter;
	DBusMessageIter structiter;
	int32_t         int32_value;
	NihCommand      command;
	char *          args[4];
	int             ret = 0;
	int             status;

	TEST_FUNCTION ("restart_action");
	TEST_DBUS (dbus_pid);
	TEST_DBUS_OPEN (server_conn);

	assert (dbus_bus_request_name (server_conn, DBUS_SERVICE_UPSTART,
				       0, NULL)
			== DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);

	TEST_DBUS_MESSAGE (server_conn, method_call);
	assert (dbus_message_is_signal (method_call, DBUS_INTERFACE_DBUS,
					"NameAcquired"));
	dbus_message_unref (method_call);

	system_bus = TRUE;
	dest_name = DBUS_SERVICE_UPSTART;
	dest_address = DBUS_ADDRESS_UPSTART;

	output = tmpfile ();
	errors = tmpfile ();


	/* Check that the restart action with a single argument given looks
	 * up a job with that name, and then calls the Restart command
	 * passing a NULL array for the events and TRUE for wait.  Once
	 * it receives the reply, it will then make queries to obtain the
	 * status of the command and print the output.
	 */
	TEST_FEATURE ("with single argument");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Start method call on the job object,
			 * make sure the environment and wait arguments
			 * are right and reply with an instance path to
			 * acknowledge.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"Restart"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_BOOLEAN, &wait_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 0);
			dbus_free_string_array (args_value);

			TEST_TRUE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test/_";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* We allow the client to get the properties as many
			 * times as it likes, since it repeats this in out
			 * of memory cases.
			 */
			for (;;) {
				void hup_handler (int signum) { _exit (0); }
				signal (SIGHUP, hup_handler);

				/* Expect the Get call for the job name, reply with
				 * the name.
				 */
				TEST_DBUS_MESSAGE (server_conn, method_call);

				TEST_TRUE (dbus_message_is_method_call (method_call,
									DBUS_INTERFACE_PROPERTIES,
									"Get"));

				TEST_EQ_STR (dbus_message_get_path (method_call),
					     DBUS_PATH_UPSTART "/jobs/test");

				TEST_TRUE (dbus_message_get_args (method_call, NULL,
								  DBUS_TYPE_STRING, &interface,
								  DBUS_TYPE_STRING, &property,
								  DBUS_TYPE_INVALID));

				TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
				TEST_EQ_STR (property, "name");

				TEST_ALLOC_SAFE {
					reply = dbus_message_new_method_return (method_call);

					dbus_message_iter_init_append (reply, &iter);

					dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "test";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&iter, &subiter);
				}

				dbus_connection_send (server_conn, reply, NULL);
				dbus_connection_flush (server_conn);

				dbus_message_unref (method_call);
				dbus_message_unref (reply);

				/* Expect the GetAll call for the instance properties,
				 * reply with the properties.
				 */
				TEST_DBUS_MESSAGE (server_conn, method_call);

				TEST_TRUE (dbus_message_is_method_call (method_call,
									DBUS_INTERFACE_PROPERTIES,
									"GetAll"));

				TEST_EQ_STR (dbus_message_get_path (method_call),
					     DBUS_PATH_UPSTART "/jobs/test/_");

				TEST_TRUE (dbus_message_get_args (method_call, NULL,
								  DBUS_TYPE_STRING, &interface,
								  DBUS_TYPE_INVALID));

				TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

				TEST_ALLOC_SAFE {
					reply = dbus_message_new_method_return (method_call);

					dbus_message_iter_init_append (reply, &iter);

					dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
									  (DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
									   DBUS_TYPE_STRING_AS_STRING
									   DBUS_TYPE_VARIANT_AS_STRING
									   DBUS_DICT_ENTRY_END_CHAR_AS_STRING),
									  &arrayiter);

					/* Name */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "name";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					/* Goal */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "goal";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "start";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					/* State */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "state";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "running";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					/* Processes */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "processes";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  (DBUS_TYPE_ARRAY_AS_STRING
									   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
									   DBUS_TYPE_STRING_AS_STRING
									   DBUS_TYPE_INT32_AS_STRING
									   DBUS_STRUCT_END_CHAR_AS_STRING),
									  &subiter);

					dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
									  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
									   DBUS_TYPE_STRING_AS_STRING
									   DBUS_TYPE_INT32_AS_STRING
									   DBUS_STRUCT_END_CHAR_AS_STRING),
									  &prociter);

					dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
									  NULL,
									  &structiter);

					str_value = "main";
					dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
									&str_value);

					int32_value = 3648;
					dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
									&int32_value);

					dbus_message_iter_close_container (&prociter, &structiter);

					dbus_message_iter_close_container (&subiter, &prociter);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					dbus_message_iter_close_container (&iter, &arrayiter);
				}

				dbus_connection_send (server_conn, reply, NULL);
				dbus_connection_flush (server_conn);

				dbus_message_unref (method_call);
				dbus_message_unref (reply);
			}

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = restart_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_EQ (output, "test start/running, process 3648\n");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGHUP);
		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);
	}


	/* Check that additional arguments to the restart action are passed
	 * as entries in the environment argument of the command.
	 */
	TEST_FEATURE ("with multiple arguments");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Restart method call on the job object,
			 * make sure the environment and wait arguments
			 * are right and reply with an instance path to
			 * acknowledge.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"Restart"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_BOOLEAN, &wait_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 2);
			TEST_EQ_STR (args_value[0], "FOO=foo");
			TEST_EQ_STR (args_value[1], "BAR=bar");
			dbus_free_string_array (args_value);

			TEST_TRUE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test/_";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* We allow the client to get the properties as many
			 * times as it likes, since it repeats this in out
			 * of memory cases.
			 */
			for (;;) {
				void hup_handler (int signum) { _exit (0); }
				signal (SIGHUP, hup_handler);

				/* Expect the Get call for the job name, reply with
				 * the name.
				 */
				TEST_DBUS_MESSAGE (server_conn, method_call);

				TEST_TRUE (dbus_message_is_method_call (method_call,
									DBUS_INTERFACE_PROPERTIES,
									"Get"));

				TEST_EQ_STR (dbus_message_get_path (method_call),
					     DBUS_PATH_UPSTART "/jobs/test");

				TEST_TRUE (dbus_message_get_args (method_call, NULL,
								  DBUS_TYPE_STRING, &interface,
								  DBUS_TYPE_STRING, &property,
								  DBUS_TYPE_INVALID));

				TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
				TEST_EQ_STR (property, "name");

				TEST_ALLOC_SAFE {
					reply = dbus_message_new_method_return (method_call);

					dbus_message_iter_init_append (reply, &iter);

					dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "test";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&iter, &subiter);
				}

				dbus_connection_send (server_conn, reply, NULL);
				dbus_connection_flush (server_conn);

				dbus_message_unref (method_call);
				dbus_message_unref (reply);

				/* Expect the GetAll call for the instance properties,
				 * reply with the properties.
				 */
				TEST_DBUS_MESSAGE (server_conn, method_call);

				TEST_TRUE (dbus_message_is_method_call (method_call,
									DBUS_INTERFACE_PROPERTIES,
									"GetAll"));

				TEST_EQ_STR (dbus_message_get_path (method_call),
					     DBUS_PATH_UPSTART "/jobs/test/_");

				TEST_TRUE (dbus_message_get_args (method_call, NULL,
								  DBUS_TYPE_STRING, &interface,
								  DBUS_TYPE_INVALID));

				TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

				TEST_ALLOC_SAFE {
					reply = dbus_message_new_method_return (method_call);

					dbus_message_iter_init_append (reply, &iter);

					dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
									  (DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
									   DBUS_TYPE_STRING_AS_STRING
									   DBUS_TYPE_VARIANT_AS_STRING
									   DBUS_DICT_ENTRY_END_CHAR_AS_STRING),
									  &arrayiter);

					/* Name */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "name";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					/* Goal */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "goal";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "start";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					/* State */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "state";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "running";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					/* Processes */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "processes";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  (DBUS_TYPE_ARRAY_AS_STRING
									   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
									   DBUS_TYPE_STRING_AS_STRING
									   DBUS_TYPE_INT32_AS_STRING
									   DBUS_STRUCT_END_CHAR_AS_STRING),
									  &subiter);

					dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
									  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
									   DBUS_TYPE_STRING_AS_STRING
									   DBUS_TYPE_INT32_AS_STRING
									   DBUS_STRUCT_END_CHAR_AS_STRING),
									  &prociter);

					dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
									  NULL,
									  &structiter);

					str_value = "main";
					dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
									&str_value);

					int32_value = 3648;
					dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
									&int32_value);

					dbus_message_iter_close_container (&prociter, &structiter);

					dbus_message_iter_close_container (&subiter, &prociter);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					dbus_message_iter_close_container (&iter, &arrayiter);
				}

				dbus_connection_send (server_conn, reply, NULL);
				dbus_connection_flush (server_conn);

				dbus_message_unref (method_call);
				dbus_message_unref (reply);
			}

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = "FOO=foo";
		args[2] = "BAR=bar";
		args[3] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = restart_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_EQ (output, "test start/running, process 3648\n");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGHUP);
		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);
	}


	/* Check that the --no-wait option results in the method call
	 * being made with wait as FALSE.
	 */
	TEST_FEATURE ("with no wait");
	no_wait = TRUE;

	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Restart method call on the job object,
			 * make sure the environment and wait arguments
			 * are right and reply with an instance path to
			 * acknowledge.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"Restart"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_BOOLEAN, &wait_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 0);
			dbus_free_string_array (args_value);

			TEST_FALSE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test/_";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* We allow the client to get the properties as many
			 * times as it likes, since it repeats this in out
			 * of memory cases.
			 */
			for (;;) {
				void hup_handler (int signum) { _exit (0); }
				signal (SIGHUP, hup_handler);

				/* Expect the Get call for the job name, reply with
				 * the name.
				 */
				TEST_DBUS_MESSAGE (server_conn, method_call);

				TEST_TRUE (dbus_message_is_method_call (method_call,
									DBUS_INTERFACE_PROPERTIES,
									"Get"));

				TEST_EQ_STR (dbus_message_get_path (method_call),
					     DBUS_PATH_UPSTART "/jobs/test");

				TEST_TRUE (dbus_message_get_args (method_call, NULL,
								  DBUS_TYPE_STRING, &interface,
								  DBUS_TYPE_STRING, &property,
								  DBUS_TYPE_INVALID));

				TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
				TEST_EQ_STR (property, "name");

				TEST_ALLOC_SAFE {
					reply = dbus_message_new_method_return (method_call);

					dbus_message_iter_init_append (reply, &iter);

					dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "test";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&iter, &subiter);
				}

				dbus_connection_send (server_conn, reply, NULL);
				dbus_connection_flush (server_conn);

				dbus_message_unref (method_call);
				dbus_message_unref (reply);

				/* Expect the GetAll call for the instance properties,
				 * reply with the properties.
				 */
				TEST_DBUS_MESSAGE (server_conn, method_call);

				TEST_TRUE (dbus_message_is_method_call (method_call,
									DBUS_INTERFACE_PROPERTIES,
									"GetAll"));

				TEST_EQ_STR (dbus_message_get_path (method_call),
					     DBUS_PATH_UPSTART "/jobs/test/_");

				TEST_TRUE (dbus_message_get_args (method_call, NULL,
								  DBUS_TYPE_STRING, &interface,
								  DBUS_TYPE_INVALID));

				TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

				TEST_ALLOC_SAFE {
					reply = dbus_message_new_method_return (method_call);

					dbus_message_iter_init_append (reply, &iter);

					dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
									  (DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
									   DBUS_TYPE_STRING_AS_STRING
									   DBUS_TYPE_VARIANT_AS_STRING
									   DBUS_DICT_ENTRY_END_CHAR_AS_STRING),
									  &arrayiter);

					/* Name */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "name";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					/* Goal */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "goal";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "start";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					/* State */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "state";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "running";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					/* Processes */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "processes";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  (DBUS_TYPE_ARRAY_AS_STRING
									   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
									   DBUS_TYPE_STRING_AS_STRING
									   DBUS_TYPE_INT32_AS_STRING
									   DBUS_STRUCT_END_CHAR_AS_STRING),
									  &subiter);

					dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
									  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
									   DBUS_TYPE_STRING_AS_STRING
									   DBUS_TYPE_INT32_AS_STRING
									   DBUS_STRUCT_END_CHAR_AS_STRING),
									  &prociter);

					dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
									  NULL,
									  &structiter);

					str_value = "main";
					dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
									&str_value);

					int32_value = 3648;
					dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
									&int32_value);

					dbus_message_iter_close_container (&prociter, &structiter);

					dbus_message_iter_close_container (&subiter, &prociter);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					dbus_message_iter_close_container (&iter, &arrayiter);
				}

				dbus_connection_send (server_conn, reply, NULL);
				dbus_connection_flush (server_conn);

				dbus_message_unref (method_call);
				dbus_message_unref (reply);
			}

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = restart_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_EQ (output, "test start/running, process 3648\n");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGHUP);
		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);
	}

	no_wait = FALSE;


	/* Check that the restart action may be called without arguments
	 * when inside an instance process, due to the environment variables
	 * set there.  The job should be stilled looked up, but then the
	 * instance should be looked up via GetInstanceByName and the Restart
	 * command run directly on the instance instead in a no-wait fashion.
	 */
	TEST_FEATURE ("with no arguments when called from job process");
	setenv ("UPSTART_JOB", "test", TRUE);
	setenv ("UPSTART_INSTANCE", "foo", TRUE);

	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetInstanceByName method call on the
			 * job object, make sure the instance name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"GetInstanceByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "foo");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test/foo";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Restart method call on the instance
			 * object, make the wait argument is FALSE and reply to
			 * to acknowledge.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_INSTANCE,
								"Restart"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test/foo");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_BOOLEAN, &wait_value,
							  DBUS_TYPE_INVALID));

			TEST_FALSE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* We allow the client to get the properties as many
			 * times as it likes, since it repeats this in out
			 * of memory cases.
			 */
			for (;;) {
				void hup_handler (int signum) { _exit (0); }
				signal (SIGHUP, hup_handler);

				/* Expect the Get call for the job name, reply with
				 * the name.
				 */
				TEST_DBUS_MESSAGE (server_conn, method_call);

				TEST_TRUE (dbus_message_is_method_call (method_call,
									DBUS_INTERFACE_PROPERTIES,
									"Get"));

				TEST_EQ_STR (dbus_message_get_path (method_call),
					     DBUS_PATH_UPSTART "/jobs/test");

				TEST_TRUE (dbus_message_get_args (method_call, NULL,
								  DBUS_TYPE_STRING, &interface,
								  DBUS_TYPE_STRING, &property,
								  DBUS_TYPE_INVALID));

				TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
				TEST_EQ_STR (property, "name");

				TEST_ALLOC_SAFE {
					reply = dbus_message_new_method_return (method_call);

					dbus_message_iter_init_append (reply, &iter);

					dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "test";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&iter, &subiter);
				}

				dbus_connection_send (server_conn, reply, NULL);
				dbus_connection_flush (server_conn);

				dbus_message_unref (method_call);
				dbus_message_unref (reply);

				/* Expect the GetAll call for the instance properties,
				 * reply with the properties.
				 */
				TEST_DBUS_MESSAGE (server_conn, method_call);

				TEST_TRUE (dbus_message_is_method_call (method_call,
									DBUS_INTERFACE_PROPERTIES,
									"GetAll"));

				TEST_EQ_STR (dbus_message_get_path (method_call),
					     DBUS_PATH_UPSTART "/jobs/test/foo");

				TEST_TRUE (dbus_message_get_args (method_call, NULL,
								  DBUS_TYPE_STRING, &interface,
								  DBUS_TYPE_INVALID));

				TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

				TEST_ALLOC_SAFE {
					reply = dbus_message_new_method_return (method_call);

					dbus_message_iter_init_append (reply, &iter);

					dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
									  (DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
									   DBUS_TYPE_STRING_AS_STRING
									   DBUS_TYPE_VARIANT_AS_STRING
									   DBUS_DICT_ENTRY_END_CHAR_AS_STRING),
									  &arrayiter);

					/* Name */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "name";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "foo";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					/* Goal */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "goal";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "start";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					/* State */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "state";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  DBUS_TYPE_STRING_AS_STRING,
									  &subiter);

					str_value = "running";
					dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					/* Processes */
					dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
									  NULL,
									  &dictiter);

					str_value = "processes";
					dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
									&str_value);

					dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
									  (DBUS_TYPE_ARRAY_AS_STRING
									   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
									   DBUS_TYPE_STRING_AS_STRING
									   DBUS_TYPE_INT32_AS_STRING
									   DBUS_STRUCT_END_CHAR_AS_STRING),
									  &subiter);

					dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
									  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
									   DBUS_TYPE_STRING_AS_STRING
									   DBUS_TYPE_INT32_AS_STRING
									   DBUS_STRUCT_END_CHAR_AS_STRING),
									  &prociter);

					dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
									  NULL,
									  &structiter);

					str_value = "main";
					dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
									&str_value);

					int32_value = 3648;
					dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
									&int32_value);

					dbus_message_iter_close_container (&prociter, &structiter);

					dbus_message_iter_close_container (&subiter, &prociter);

					dbus_message_iter_close_container (&dictiter, &subiter);

					dbus_message_iter_close_container (&arrayiter, &dictiter);

					dbus_message_iter_close_container (&iter, &arrayiter);
				}

				dbus_connection_send (server_conn, reply, NULL);
				dbus_connection_flush (server_conn);

				dbus_message_unref (method_call);
				dbus_message_unref (reply);
			}

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = restart_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_EQ (output, "test (foo) start/running, process 3648\n");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGHUP);
		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);
	}

	unsetenv ("UPSTART_JOB");
	unsetenv ("UPSTART_INSTANCE");


	/* Check that if an error is received from the GetJobByName call,
	 * the message attached is printed to standard error and the
	 * command exits.
	 */
	TEST_FEATURE ("with error reply to GetJobByName");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with an error.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_error (method_call,
								DBUS_ERROR_UNKNOWN_METHOD,
								"Unknown method");
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = restart_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_MATCH (errors, "test: *\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGTERM);
		waitpid (server_pid, NULL, 0);
	}


	/* Check that if an error is received from the Restart call,
	 * the message attached is printed to standard error and the
	 * command exits.
	 */
	TEST_FEATURE ("with error reply to Restart");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Restart method call on the job object,
			 * make sure the environment and wait arguments
			 * are right and reply with an error.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"Restart"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_BOOLEAN, &wait_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 0);
			dbus_free_string_array (args_value);

			TEST_TRUE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_error (method_call,
								DBUS_ERROR_UNKNOWN_METHOD,
								"Unknown method");
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = restart_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_MATCH (errors, "test: *\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGTERM);
		waitpid (server_pid, NULL, 0);
	}


	/* Check that if an error is received from the status query,
	 * the message attached is printed to standard error and the
	 * command exits.
	 */
	TEST_FEATURE ("with error reply to status query");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Restart method call on the job object,
			 * make sure the environment and wait arguments
			 * are right and reply with an instance path to
			 * acknowledge.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"Restart"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_BOOLEAN, &wait_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 0);
			dbus_free_string_array (args_value);

			TEST_TRUE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test/_";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Get call for the job name, reply with
			 * an error.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
				     DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
			TEST_EQ_STR (property, "name");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_error (method_call,
								DBUS_ERROR_UNKNOWN_METHOD,
								"Unknown method");
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = restart_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_MATCH (errors, "test: *\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGTERM);
		waitpid (server_pid, NULL, 0);
	}


	/* Check that a missing argument results in an error being output
	 * to stderr along with a suggestion of help.
	 */
	TEST_FEATURE ("with missing argument");
	TEST_ALLOC_FAIL {
		memset (&command, 0, sizeof command);

		args[0] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = restart_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_EQ (errors, "test: missing job name\n");
		TEST_FILE_EQ (errors, "Try `test --help' for more information.\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);
	}


	fclose (errors);
	fclose (output);

	TEST_DBUS_CLOSE (server_conn);
	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}


void
test_reload_action (void)
{
	pid_t           dbus_pid;
	DBusConnection *server_conn;
	FILE *          output;
	FILE *          errors;
	pid_t           server_pid;
	pid_t           proc_pid;
	DBusMessage *   method_call;
	DBusMessage *   reply = NULL;
	const char *    name_value;
	char **         args_value;
	int             args_elements;
	const char *    str_value;
	const char *    interface;
	const char *    property;
	DBusMessageIter iter;
	DBusMessageIter subiter;
	DBusMessageIter arrayiter;
	DBusMessageIter structiter;
	int32_t         int32_value;
	NihCommand      command;
	char *          args[4];
	int             ret = 0;
	int             status;

	TEST_FUNCTION ("reload_action");
	TEST_DBUS (dbus_pid);
	TEST_DBUS_OPEN (server_conn);

	assert (dbus_bus_request_name (server_conn, DBUS_SERVICE_UPSTART,
				       0, NULL)
			== DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);

	TEST_DBUS_MESSAGE (server_conn, method_call);
	assert (dbus_message_is_signal (method_call, DBUS_INTERFACE_DBUS,
					"NameAcquired"));
	dbus_message_unref (method_call);

	system_bus = TRUE;
	dest_name = DBUS_SERVICE_UPSTART;
	dest_address = DBUS_ADDRESS_UPSTART;

	output = tmpfile ();
	errors = tmpfile ();


	/* Check that the reload action with a single argument given looks
	 * up a job with that name, then requests the list of processes
	 * sending a SIGHUP signal to the main process.
	 */
	TEST_FEATURE ("with single argument");
	TEST_ALLOC_FAIL {
		TEST_CHILD (proc_pid) {
			pause ();
		}

		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetInstance method call on the
			 * job object, make sure the environment args are
			 * passed and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"GetInstance"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 0);
			dbus_free_string_array (args_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test/_";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Get call for the processes, reply with
			 * a main process pid.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test/_");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);
			TEST_EQ_STR (property, "processes");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
								  (DBUS_TYPE_ARRAY_AS_STRING
								   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &subiter);

				dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
								  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &arrayiter);

				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_STRUCT,
								  NULL,
								  &structiter);

				str_value = "main";
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
								&str_value);

				int32_value = proc_pid;
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
								&int32_value);

				dbus_message_iter_close_container (&arrayiter, &structiter);

				dbus_message_iter_close_container (&subiter, &arrayiter);

				dbus_message_iter_close_container (&iter, &subiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = reload_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);

			kill (proc_pid, SIGTERM);
			waitpid (proc_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGHUP);
		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		waitpid (proc_pid, &status, 0);
		TEST_TRUE (WIFSIGNALED (status));
		TEST_EQ (WTERMSIG (status), SIGHUP);
	}


	/* Check that additional arguments to the restart action are passed
	 * as entries in the environment argument of the command.
	 */
	TEST_FEATURE ("with multiple arguments");
	TEST_ALLOC_FAIL {
		TEST_CHILD (proc_pid) {
			pause ();
		}

		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetInstance method call on the
			 * job object, make sure the environment args are
			 * passed and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"GetInstance"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 2);
			TEST_EQ_STR (args_value[0], "FOO=foo");
			TEST_EQ_STR (args_value[1], "BAR=bar");
			dbus_free_string_array (args_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test/_";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Get call for the processes, reply with
			 * a main process pid.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test/_");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);
			TEST_EQ_STR (property, "processes");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
								  (DBUS_TYPE_ARRAY_AS_STRING
								   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &subiter);

				dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
								  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &arrayiter);

				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_STRUCT,
								  NULL,
								  &structiter);

				str_value = "main";
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
								&str_value);

				int32_value = proc_pid;
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
								&int32_value);

				dbus_message_iter_close_container (&arrayiter, &structiter);

				dbus_message_iter_close_container (&subiter, &arrayiter);

				dbus_message_iter_close_container (&iter, &subiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = "FOO=foo";
		args[2] = "BAR=bar";
		args[3] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = reload_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);

			kill (proc_pid, SIGTERM);
			waitpid (proc_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGHUP);
		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		waitpid (proc_pid, &status, 0);
		TEST_TRUE (WIFSIGNALED (status));
		TEST_EQ (WTERMSIG (status), SIGHUP);
	}


	/* Check that the reload action may be called without arguments
	 * when inside an instance process, due to the environment variables
	 * set there.  The job should be stilled looked up, but then the
	 * instance should be looked up via GetInstanceByName instead.
	 */
	TEST_FEATURE ("with no arguments when called from job process");
	setenv ("UPSTART_JOB", "test", TRUE);
	setenv ("UPSTART_INSTANCE", "foo", TRUE);

	TEST_ALLOC_FAIL {
		TEST_CHILD (proc_pid) {
			pause ();
		}

		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetInstanceByName method call on the
			 * job object, make sure the instance name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"GetInstanceByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "foo");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test/foo";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Get call for the processes, reply with
			 * a main process pid.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test/foo");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);
			TEST_EQ_STR (property, "processes");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
								  (DBUS_TYPE_ARRAY_AS_STRING
								   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &subiter);

				dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
								  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &arrayiter);

				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_STRUCT,
								  NULL,
								  &structiter);

				str_value = "main";
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
								&str_value);

				int32_value = proc_pid;
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
								&int32_value);

				dbus_message_iter_close_container (&arrayiter, &structiter);

				dbus_message_iter_close_container (&subiter, &arrayiter);

				dbus_message_iter_close_container (&iter, &subiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = reload_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);

			kill (proc_pid, SIGTERM);
			waitpid (proc_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGHUP);
		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		waitpid (proc_pid, &status, 0);
		TEST_TRUE (WIFSIGNALED (status));
		TEST_EQ (WTERMSIG (status), SIGHUP);
	}

	unsetenv ("UPSTART_JOB");
	unsetenv ("UPSTART_INSTANCE");


	/* Check that if an error is received from the GetJobByName call,
	 * the message attached is printed to standard error and the
	 * command exits.
	 */
	TEST_FEATURE ("with error reply to GetJobByName");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with an error.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_error (method_call,
								DBUS_ERROR_UNKNOWN_METHOD,
								"Unknown method");
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = reload_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_MATCH (errors, "test: *\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGTERM);
		waitpid (server_pid, NULL, 0);
	}


	/* Check that if an error is received from the GetInstance call,
	 * the message attached is printed to standard error and the
	 * command exits.
	 */
	TEST_FEATURE ("with error reply to GetInstance");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetInstance method call on the
			 * job object, make sure the environment args are
			 * passed and reply with an error.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"GetInstance"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 0);
			dbus_free_string_array (args_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_error (method_call,
								DBUS_ERROR_UNKNOWN_METHOD,
								"Unknown method");
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = reload_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_MATCH (errors, "test: *\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGTERM);
		waitpid (server_pid, NULL, 0);
	}


	/* Check that a missing argument results in an error being output
	 * to stderr along with a suggestion of help.
	 */
	TEST_FEATURE ("with missing argument");
	TEST_ALLOC_FAIL {
		memset (&command, 0, sizeof command);

		args[0] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = reload_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_EQ (errors, "test: missing job name\n");
		TEST_FILE_EQ (errors, "Try `test --help' for more information.\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);
	}


	fclose (errors);
	fclose (output);

	TEST_DBUS_CLOSE (server_conn);
	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}


void
test_status_action (void)
{
	pid_t           dbus_pid;
	DBusConnection *server_conn;
	FILE *          output;
	FILE *          errors;
	pid_t           server_pid;
	DBusMessage *   method_call;
	DBusMessage *   reply = NULL;
	const char *    name_value;
	char **         args_value;
	int             args_elements;
	const char *    str_value;
	const char *    interface;
	const char *    property;
	DBusMessageIter iter;
	DBusMessageIter subiter;
	DBusMessageIter arrayiter;
	DBusMessageIter dictiter;
	DBusMessageIter prociter;
	DBusMessageIter structiter;
	int32_t         int32_value;
	NihCommand      command;
	char *          args[4];
	int             ret = 0;
	int             status;

	TEST_FUNCTION ("status_action");
	TEST_DBUS (dbus_pid);
	TEST_DBUS_OPEN (server_conn);

	assert (dbus_bus_request_name (server_conn, DBUS_SERVICE_UPSTART,
				       0, NULL)
			== DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);

	TEST_DBUS_MESSAGE (server_conn, method_call);
	assert (dbus_message_is_signal (method_call, DBUS_INTERFACE_DBUS,
					"NameAcquired"));
	dbus_message_unref (method_call);

	system_bus = TRUE;
	dest_name = DBUS_SERVICE_UPSTART;
	dest_address = DBUS_ADDRESS_UPSTART;

	output = tmpfile ();
	errors = tmpfile ();


	/* Check that the status action with a single argument given looks
	 * up a job with that name, then looks up the instance with a NULL
	 * arguments array (to get the path for later) and then makes
	 * queries to obtain the status of that instance printing the
	 * output.
	 */
	TEST_FEATURE ("with single argument");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetInstance method call on the
			 * job object, make sure the environment args are
			 * passed and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"GetInstance"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 0);
			dbus_free_string_array (args_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test/_";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Get call for the job name, reply with
			 * the name.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
				     DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
			TEST_EQ_STR (property, "name");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "test";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&iter, &subiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetAll call for the instance properties,
			 * reply with the properties.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"GetAll"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
				     DBUS_PATH_UPSTART "/jobs/test/_");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
								  (DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_VARIANT_AS_STRING
								   DBUS_DICT_ENTRY_END_CHAR_AS_STRING),
								  &arrayiter);

				/* Name */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "name";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Goal */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "goal";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "start";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* State */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "state";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "running";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Processes */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "processes";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  (DBUS_TYPE_ARRAY_AS_STRING
								   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &subiter);

				dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
								  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &prociter);

				dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
								  NULL,
								  &structiter);

				str_value = "main";
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
								&str_value);

				int32_value = 3648;
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
								&int32_value);

				dbus_message_iter_close_container (&prociter, &structiter);

				dbus_message_iter_close_container (&subiter, &prociter);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				dbus_message_iter_close_container (&iter, &arrayiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = status_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_EQ (output, "test start/running, process 3648\n");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);
	}


	/* Check that additional arguments to the status action are passed
	 * as entries in the environment to GetInstance.
	 */
	TEST_FEATURE ("with multiple arguments");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetInstance method call on the
			 * job object, make sure the environment args are
			 * passed and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"GetInstance"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 2);
			TEST_EQ_STR (args_value[0], "FOO=foo");
			TEST_EQ_STR (args_value[1], "BAR=bar");
			dbus_free_string_array (args_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test/_";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Get call for the job name, reply with
			 * the name.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
				     DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
			TEST_EQ_STR (property, "name");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "test";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&iter, &subiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetAll call for the instance properties,
			 * reply with the properties.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"GetAll"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
				     DBUS_PATH_UPSTART "/jobs/test/_");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
								  (DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_VARIANT_AS_STRING
								   DBUS_DICT_ENTRY_END_CHAR_AS_STRING),
								  &arrayiter);

				/* Name */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "name";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Goal */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "goal";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "start";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* State */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "state";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "running";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Processes */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "processes";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  (DBUS_TYPE_ARRAY_AS_STRING
								   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &subiter);

				dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
								  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &prociter);

				dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
								  NULL,
								  &structiter);

				str_value = "main";
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
								&str_value);

				int32_value = 3648;
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
								&int32_value);

				dbus_message_iter_close_container (&prociter, &structiter);

				dbus_message_iter_close_container (&subiter, &prociter);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				dbus_message_iter_close_container (&iter, &arrayiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = "FOO=foo";
		args[2] = "BAR=bar";
		args[3] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = status_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_EQ (output, "test start/running, process 3648\n");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);
	}


	/* Check that the status action may be called without arguments
	 * when inside an instance process, due to the environment variables
	 * set there.  The job should be still looked up, but then the
	 * instance should be looked up via GetInstanceByName and the
	 * queries run on that instance.
	 */
	TEST_FEATURE ("with no arguments when called from job process");
	setenv ("UPSTART_JOB", "test", TRUE);
	setenv ("UPSTART_INSTANCE", "foo", TRUE);

	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetInstanceByName method call on the
			 * job object, make sure the instance name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"GetInstanceByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "foo");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test/foo";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Get call for the job name, reply with
			 * the name.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
				     DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
			TEST_EQ_STR (property, "name");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "test";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&iter, &subiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetAll call for the instance properties,
			 * reply with the properties.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"GetAll"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
				     DBUS_PATH_UPSTART "/jobs/test/foo");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
								  (DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_VARIANT_AS_STRING
								   DBUS_DICT_ENTRY_END_CHAR_AS_STRING),
								  &arrayiter);

				/* Name */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "name";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "foo";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Goal */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "goal";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "start";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* State */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "state";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "running";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Processes */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "processes";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  (DBUS_TYPE_ARRAY_AS_STRING
								   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &subiter);

				dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
								  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &prociter);

				dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
								  NULL,
								  &structiter);

				str_value = "main";
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
								&str_value);

				int32_value = 3648;
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
								&int32_value);

				dbus_message_iter_close_container (&prociter, &structiter);

				dbus_message_iter_close_container (&subiter, &prociter);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				dbus_message_iter_close_container (&iter, &arrayiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = status_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_EQ (output, "test (foo) start/running, process 3648\n");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);
	}

	unsetenv ("UPSTART_JOB");
	unsetenv ("UPSTART_INSTANCE");


	/* Check that an unknown instance error from the GetInstance call
	 * is treated as a stopped job; the job name should still be
	 * queried but not the instance properties.
	 */
	TEST_FEATURE ("with unknown instance");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetInstance method call on the
			 * job object, make sure the environment args are
			 * passed and reply with an error
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"GetInstance"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 0);
			dbus_free_string_array (args_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_error (method_call,
								DBUS_INTERFACE_UPSTART ".Error.UnknownInstance",
								"Unknown instance");
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Get call for the job name, reply with
			 * the name.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
				     DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
			TEST_EQ_STR (property, "name");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "test";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&iter, &subiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = status_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_EQ (output, "test stop/waiting\n");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);
	}


	/* Check that if an error is received from the GetJobByName call,
	 * the message attached is printed to standard error and the
	 * command exits.
	 */
	TEST_FEATURE ("with error reply to GetJobByName");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with an error.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_error (method_call,
								DBUS_ERROR_UNKNOWN_METHOD,
								"Unknown method");
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = status_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_MATCH (errors, "test: *\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGTERM);
		waitpid (server_pid, NULL, 0);
	}


	/* Check that if an error is received from the GetInstance command,
	 * the message attached is printed to standard error and the
	 * command exits.
	 */
	TEST_FEATURE ("with error reply to GetInstance");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetInstance method call on the
			 * job object, make sure the environment args are
			 * passed and reply with an error.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"GetInstance"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 0);
			dbus_free_string_array (args_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_error (method_call,
								DBUS_ERROR_UNKNOWN_METHOD,
								"Unknown method");
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = status_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_MATCH (errors, "test: *\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGTERM);
		waitpid (server_pid, NULL, 0);
	}


	/* Check that if an error is received from the status query,
	 * the message attached is printed to standard error and the
	 * command exits.
	 */
	TEST_FEATURE ("with error reply to status query");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetJobByName method call on the
			 * manager object, make sure the job name is passed
			 * and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetJobByName"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "test");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetInstance method call on the
			 * job object, make sure the environment args are
			 * passed and reply with a path.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"GetInstance"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_INVALID));

			TEST_EQ (args_elements, 0);
			dbus_free_string_array (args_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				str_value = DBUS_PATH_UPSTART "/jobs/test/_";

				dbus_message_append_args (reply,
							  DBUS_TYPE_OBJECT_PATH, &str_value,
							  DBUS_TYPE_INVALID);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Get call for the job name, reply with
			 * an error.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
				     DBUS_PATH_UPSTART "/jobs/test");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
			TEST_EQ_STR (property, "name");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_error (method_call,
								DBUS_ERROR_UNKNOWN_METHOD,
								"Unknown method");
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "test";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = status_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_MATCH (errors, "test: *\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGTERM);
		waitpid (server_pid, NULL, 0);
	}


	/* Check that a missing argument results in an error being output
	 * to stderr along with a suggestion of help.
	 */
	TEST_FEATURE ("with missing argument");
	TEST_ALLOC_FAIL {
		memset (&command, 0, sizeof command);

		args[0] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = status_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_EQ (errors, "test: missing job name\n");
		TEST_FILE_EQ (errors, "Try `test --help' for more information.\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);
	}


	fclose (errors);
	fclose (output);

	TEST_DBUS_CLOSE (server_conn);
	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}


void
test_list_action (void)
{
	pid_t           dbus_pid;
	DBusConnection *server_conn;
	FILE *          output;
	FILE *          errors;
	pid_t           server_pid;
	DBusMessage *   method_call;
	DBusMessage *   reply = NULL;
	const char *    str_value;
	const char *    interface;
	const char *    property;
	DBusMessageIter iter;
	DBusMessageIter subiter;
	DBusMessageIter arrayiter;
	DBusMessageIter dictiter;
	DBusMessageIter prociter;
	DBusMessageIter structiter;
	int32_t         int32_value;
	NihCommand      command;
	char *          args[1];
	int             ret = 0;
	int             status;

	TEST_FUNCTION ("list_action");
	TEST_DBUS (dbus_pid);
	TEST_DBUS_OPEN (server_conn);

	assert (dbus_bus_request_name (server_conn, DBUS_SERVICE_UPSTART,
				       0, NULL)
			== DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);

	TEST_DBUS_MESSAGE (server_conn, method_call);
	assert (dbus_message_is_signal (method_call, DBUS_INTERFACE_DBUS,
					"NameAcquired"));
	dbus_message_unref (method_call);

	system_bus = TRUE;
	dest_name = DBUS_SERVICE_UPSTART;
	dest_address = DBUS_ADDRESS_UPSTART;

	output = tmpfile ();
	errors = tmpfile ();


	/* Check that the list action makes the GetAllJobs method call
	 * to obtain a list of paths, then for each job calls the
	 * GetAllInstances method call to obtain a list of the instances.
	 * If there are instances, the job name and instance properties are
	 * requested and output; if there are not instances, only the
	 * job name is requested and output.
	 */
	TEST_FEATURE ("with valid reply");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetAllJobs method call on the
			 * manager object, reply with a list of interesting
			 * paths.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetAllJobs"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
								  DBUS_TYPE_OBJECT_PATH_AS_STRING,
								  &arrayiter);

				str_value = DBUS_PATH_UPSTART "/jobs/frodo";
				dbus_message_iter_append_basic (&arrayiter,
								DBUS_TYPE_OBJECT_PATH,
								&str_value);

				str_value = DBUS_PATH_UPSTART "/jobs/bilbo";
				dbus_message_iter_append_basic (&arrayiter,
								DBUS_TYPE_OBJECT_PATH,
								&str_value);

				str_value = DBUS_PATH_UPSTART "/jobs/drogo";
				dbus_message_iter_append_basic (&arrayiter,
								DBUS_TYPE_OBJECT_PATH,
								&str_value);

				dbus_message_iter_close_container (&iter, &arrayiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetAllInstances method call on the
			 * first job object, reply with an empty list.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"GetAllInstances"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/frodo");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
								  DBUS_TYPE_OBJECT_PATH_AS_STRING,
								  &arrayiter);

				dbus_message_iter_close_container (&iter, &arrayiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Get call for the name of the
			 * first job, reply with the name.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
				     DBUS_PATH_UPSTART "/jobs/frodo");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
			TEST_EQ_STR (property, "name");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "frodo";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&iter, &subiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetAllInstances method call on the
			 * second job object, reply with a single instance.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"GetAllInstances"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/bilbo");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
								  DBUS_TYPE_OBJECT_PATH_AS_STRING,
								  &arrayiter);

				str_value = DBUS_PATH_UPSTART "/jobs/bilbo/_";
				dbus_message_iter_append_basic (&arrayiter,
								DBUS_TYPE_OBJECT_PATH,
								&str_value);

				dbus_message_iter_close_container (&iter, &arrayiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Get call for the name of the
			 * second job, reply with the name.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
				     DBUS_PATH_UPSTART "/jobs/bilbo");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
			TEST_EQ_STR (property, "name");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "bilbo";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&iter, &subiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetAll call for the instance properties,
			 * reply with the properties.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"GetAll"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
				     DBUS_PATH_UPSTART "/jobs/bilbo/_");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
								  (DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_VARIANT_AS_STRING
								   DBUS_DICT_ENTRY_END_CHAR_AS_STRING),
								  &arrayiter);

				/* Name */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "name";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Goal */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "goal";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "start";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* State */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "state";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "running";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Processes */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "processes";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  (DBUS_TYPE_ARRAY_AS_STRING
								   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &subiter);

				dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
								  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &prociter);

				dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
								  NULL,
								  &structiter);

				str_value = "main";
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
								&str_value);

				int32_value = 3648;
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
								&int32_value);

				dbus_message_iter_close_container (&prociter, &structiter);

				dbus_message_iter_close_container (&subiter, &prociter);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				dbus_message_iter_close_container (&iter, &arrayiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetAllInstances method call on the
			 * third job object, reply with a couple of
			 * named instances
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"GetAllInstances"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/drogo");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
								  DBUS_TYPE_OBJECT_PATH_AS_STRING,
								  &arrayiter);

				str_value = DBUS_PATH_UPSTART "/jobs/drogo/foo";
				dbus_message_iter_append_basic (&arrayiter,
								DBUS_TYPE_OBJECT_PATH,
								&str_value);

				str_value = DBUS_PATH_UPSTART "/jobs/drogo/bar";
				dbus_message_iter_append_basic (&arrayiter,
								DBUS_TYPE_OBJECT_PATH,
								&str_value);

				dbus_message_iter_close_container (&iter, &arrayiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Get call for the name of the
			 * third job, reply with the name.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
				     DBUS_PATH_UPSTART "/jobs/drogo");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
			TEST_EQ_STR (property, "name");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "drogo";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&iter, &subiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetAll call for the first of its
			 * instances, reply with the properties.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"GetAll"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
				     DBUS_PATH_UPSTART "/jobs/drogo/foo");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
								  (DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_VARIANT_AS_STRING
								   DBUS_DICT_ENTRY_END_CHAR_AS_STRING),
								  &arrayiter);

				/* Name */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "name";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "foo";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Goal */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "goal";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "stop";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* State */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "state";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "pre-stop";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Processes */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "processes";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  (DBUS_TYPE_ARRAY_AS_STRING
								   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &subiter);

				dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
								  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &prociter);

				dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
								  NULL,
								  &structiter);

				str_value = "main";
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
								&str_value);

				int32_value = 6312;
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
								&int32_value);

				dbus_message_iter_close_container (&prociter, &structiter);

				dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
								  NULL,
								  &structiter);

				str_value = "pre-stop";
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
								&str_value);

				int32_value = 8609;
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
								&int32_value);

				dbus_message_iter_close_container (&prociter, &structiter);

				dbus_message_iter_close_container (&subiter, &prociter);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				dbus_message_iter_close_container (&iter, &arrayiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Get call for the name of the
			 * third job, reply with the name.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
				     DBUS_PATH_UPSTART "/jobs/drogo");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
			TEST_EQ_STR (property, "name");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "drogo";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&iter, &subiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetAll call for the second of its
			 * instances, reply with its properties.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"GetAll"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
				     DBUS_PATH_UPSTART "/jobs/drogo/bar");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
								  (DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_VARIANT_AS_STRING
								   DBUS_DICT_ENTRY_END_CHAR_AS_STRING),
								  &arrayiter);

				/* Name */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "name";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "bar";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Goal */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "goal";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "start";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* State */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "state";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "post-stop";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Processes */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "processes";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  (DBUS_TYPE_ARRAY_AS_STRING
								   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &subiter);

				dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
								  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &prociter);

				dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
								  NULL,
								  &structiter);

				str_value = "post-stop";
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
								&str_value);

				int32_value = 7465;
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
								&int32_value);

				dbus_message_iter_close_container (&prociter, &structiter);

				dbus_message_iter_close_container (&subiter, &prociter);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				dbus_message_iter_close_container (&iter, &arrayiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = list_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			/* May have had some output */
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_EQ (output, "frodo stop/waiting\n");
		TEST_FILE_EQ (output, "bilbo start/running, process 3648\n");
		TEST_FILE_EQ (output, "drogo (foo) stop/pre-stop, process 6312\n");
		TEST_FILE_EQ (output, "\tpre-stop process 8609\n");
		TEST_FILE_EQ (output, "drogo (bar) start/post-stop, process 7465\n");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);
	}


	/* Check that an error reply from the GetAllInstances command
	 * is assumed to mean that the job went away, and thus the job
	 * is simply not printed rather than causing the function to end,
	 */
	TEST_FEATURE ("with error reply to GetAllInstances");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetAllJobs method call on the
			 * manager object, reply with a list of interesting
			 * paths.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetAllJobs"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
								  DBUS_TYPE_OBJECT_PATH_AS_STRING,
								  &arrayiter);

				str_value = DBUS_PATH_UPSTART "/jobs/foo";
				dbus_message_iter_append_basic (&arrayiter,
								DBUS_TYPE_OBJECT_PATH,
								&str_value);

				str_value = DBUS_PATH_UPSTART "/jobs/bar";
				dbus_message_iter_append_basic (&arrayiter,
								DBUS_TYPE_OBJECT_PATH,
								&str_value);

				dbus_message_iter_close_container (&iter, &arrayiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetAllInstances method call on the
			 * first job object, reply with an error.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"GetAllInstances"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/foo");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_error (method_call,
								DBUS_ERROR_UNKNOWN_METHOD,
								"Unknown method");
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetAllInstances method call on the
			 * second job object, reply with a single instance.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART_JOB,
								"GetAllInstances"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART "/jobs/bar");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
								  DBUS_TYPE_OBJECT_PATH_AS_STRING,
								  &arrayiter);

				str_value = DBUS_PATH_UPSTART "/jobs/bar/_";
				dbus_message_iter_append_basic (&arrayiter,
								DBUS_TYPE_OBJECT_PATH,
								&str_value);

				dbus_message_iter_close_container (&iter, &arrayiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the Get call for the name of the
			 * second job, reply with the name.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
				     DBUS_PATH_UPSTART "/jobs/bar");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_JOB);
			TEST_EQ_STR (property, "name");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "bar";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&iter, &subiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			/* Expect the GetAll call for the instance properties,
			 * reply with the properties.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"GetAll"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
				     DBUS_PATH_UPSTART "/jobs/bar/_");

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART_INSTANCE);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
								  (DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_VARIANT_AS_STRING
								   DBUS_DICT_ENTRY_END_CHAR_AS_STRING),
								  &arrayiter);

				/* Name */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "name";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Goal */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "goal";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "start";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* State */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "state";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "running";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				/* Processes */
				dbus_message_iter_open_container (&arrayiter, DBUS_TYPE_DICT_ENTRY,
								  NULL,
								  &dictiter);

				str_value = "processes";
				dbus_message_iter_append_basic (&dictiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_open_container (&dictiter, DBUS_TYPE_VARIANT,
								  (DBUS_TYPE_ARRAY_AS_STRING
								   DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &subiter);

				dbus_message_iter_open_container (&subiter, DBUS_TYPE_ARRAY,
								  (DBUS_STRUCT_BEGIN_CHAR_AS_STRING
								   DBUS_TYPE_STRING_AS_STRING
								   DBUS_TYPE_INT32_AS_STRING
								   DBUS_STRUCT_END_CHAR_AS_STRING),
								  &prociter);

				dbus_message_iter_open_container (&prociter, DBUS_TYPE_STRUCT,
								  NULL,
								  &structiter);

				str_value = "main";
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_STRING,
								&str_value);

				int32_value = 3648;
				dbus_message_iter_append_basic (&structiter, DBUS_TYPE_INT32,
								&int32_value);

				dbus_message_iter_close_container (&prociter, &structiter);

				dbus_message_iter_close_container (&subiter, &prociter);

				dbus_message_iter_close_container (&dictiter, &subiter);

				dbus_message_iter_close_container (&arrayiter, &dictiter);

				dbus_message_iter_close_container (&iter, &arrayiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = list_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			/* May have had some output */
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_EQ (output, "bar start/running, process 3648\n");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);
	}


	/* Check that if an error is received from the GetAllJobs call,
	 * the message attached is printed to standard error and the
	 * command exits.
	 */
	TEST_FEATURE ("with error reply to GetAllJobs");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the GetAllJobs method call on the
			 * manager object, reply with an error.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"GetAllJobs"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_error (method_call,
								DBUS_ERROR_UNKNOWN_METHOD,
								"Unknown method");
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = list_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_MATCH (errors, "test: *\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGTERM);
		waitpid (server_pid, NULL, 0);
	}


	fclose (errors);
	fclose (output);

	TEST_DBUS_CLOSE (server_conn);
	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}


void
test_emit_action (void)
{
	pid_t           dbus_pid;
	DBusConnection *server_conn;
	FILE *          output;
	FILE *          errors;
	pid_t           server_pid;
	DBusMessage *   method_call;
	DBusMessage *   reply = NULL;
	const char *    name_value;
	char **         args_value;
	int             args_elements;
	int             wait_value;
	NihCommand      command;
	char *          args[4];
	int             ret = 0;
	int             status;

	TEST_FUNCTION ("emit_action");
	TEST_DBUS (dbus_pid);
	TEST_DBUS_OPEN (server_conn);

	assert (dbus_bus_request_name (server_conn, DBUS_SERVICE_UPSTART,
				       0, NULL)
			== DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);

	TEST_DBUS_MESSAGE (server_conn, method_call);
	assert (dbus_message_is_signal (method_call, DBUS_INTERFACE_DBUS,
					"NameAcquired"));
	dbus_message_unref (method_call);

	system_bus = TRUE;
	dest_name = DBUS_SERVICE_UPSTART;
	dest_address = DBUS_ADDRESS_UPSTART;

	output = tmpfile ();
	errors = tmpfile ();


	/* Check that the emit action passes a single argument to the
	 * server in the EmitEvent command as the name of the event,
	 * along with a NULL array for the events.  Make sure that wait
	 * is TRUE by default.
	 */
	TEST_FEATURE ("with single argument");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the EmitEvent method call on the manager
			 * object, make sure the arguments are right and
			 * reply to acknowledge.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"EmitEvent"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_BOOLEAN, &wait_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "wibble");

			TEST_EQ (args_elements, 0);
			dbus_free_string_array (args_value);

			TEST_TRUE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "wibble";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = emit_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);
	}


	/* Check that additional arguments to the emit action are passed
	 * as entries in the environment argument of the command.
	 */
	TEST_FEATURE ("with multiple arguments");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the EmitEvent method call on the manager
			 * object, make sure the arguments are right and
			 * reply to acknowledge.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"EmitEvent"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_BOOLEAN, &wait_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "wibble");

			TEST_EQ (args_elements, 2);
			TEST_EQ_STR (args_value[0], "FOO=foo");
			TEST_EQ_STR (args_value[1], "BAR=bar");
			dbus_free_string_array (args_value);

			TEST_TRUE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "wibble";
		args[1] = "FOO=foo";
		args[2] = "BAR=bar";
		args[3] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = emit_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);
	}


	/* Check that the --no-wait option results in the method call
	 * being made with wait as FALSE.
	 */
	TEST_FEATURE ("with no wait");
	no_wait = TRUE;

	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the EmitEvent method call on the manager
			 * object, make sure the arguments are right and
			 * reply to acknowledge.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"EmitEvent"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_BOOLEAN, &wait_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "wibble");

			TEST_EQ (args_elements, 0);
			dbus_free_string_array (args_value);

			TEST_FALSE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "wibble";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = emit_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);
	}

	no_wait = FALSE;


	/* Check that if an error is received from the command,
	 * the message attached is printed to standard error and the
	 * command exits.
	 */
	TEST_FEATURE ("with error reply");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the EmitEvent method call on the manager
			 * object, make sure the arguments are right and
			 * reply with an error.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"EmitEvent"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &name_value,
							  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args_value, &args_elements,
							  DBUS_TYPE_BOOLEAN, &wait_value,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (name_value, "wibble");

			TEST_EQ (args_elements, 0);
			dbus_free_string_array (args_value);

			TEST_TRUE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_error (method_call,
								DBUS_ERROR_UNKNOWN_METHOD,
								"Unknown method");
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "wibble";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = emit_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_MATCH (errors, "test: *\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGTERM);
		waitpid (server_pid, NULL, 0);
	}


	/* Check that a missing argument results in an error being output
	 * to stderr along with a suggestion of help.
	 */
	TEST_FEATURE ("with missing argument");
	TEST_ALLOC_FAIL {
		memset (&command, 0, sizeof command);

		args[0] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = emit_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_EQ (errors, "test: missing event name\n");
		TEST_FILE_EQ (errors, "Try `test --help' for more information.\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);
	}


	fclose (errors);
	fclose (output);

	TEST_DBUS_CLOSE (server_conn);
	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}


void
test_reload_configuration_action (void)
{
	pid_t           dbus_pid;
	DBusConnection *server_conn;
	FILE *          output;
	FILE *          errors;
	pid_t           server_pid;
	DBusMessage *   method_call;
	DBusMessage *   reply = NULL;
	NihCommand      command;
	char *          args[1];
	int             ret = 0;
	int             status;

	TEST_FUNCTION ("reload_configuration_action");
	TEST_DBUS (dbus_pid);
	TEST_DBUS_OPEN (server_conn);

	assert (dbus_bus_request_name (server_conn, DBUS_SERVICE_UPSTART,
				       0, NULL)
			== DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);

	TEST_DBUS_MESSAGE (server_conn, method_call);
	assert (dbus_message_is_signal (method_call, DBUS_INTERFACE_DBUS,
					"NameAcquired"));
	dbus_message_unref (method_call);

	system_bus = TRUE;
	dest_name = DBUS_SERVICE_UPSTART;
	dest_address = DBUS_ADDRESS_UPSTART;

	output = tmpfile ();
	errors = tmpfile ();


	/* Check that the reload_configuration sends the method call to the
	 * server.
	 */
	TEST_FEATURE ("with command");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the ReloadConfiguration method call for
			 * the manager object, reply to acknowledge.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"ReloadConfiguration"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = reload_configuration_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);
	}


	/* Check that if an error is received from the command,
	 * the message attached is printed to standard error and the
	 * command exits.
	 */
	TEST_FEATURE ("with error reply");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the ReloadConfiguration method call for
			 * the manager object, reply with an error.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_UPSTART,
								"ReloadConfiguration"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_error (method_call,
								DBUS_ERROR_UNKNOWN_METHOD,
								"Unknown method");
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = reload_configuration_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_MATCH (errors, "test: *\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGTERM);
		waitpid (server_pid, NULL, 0);
	}


	fclose (errors);
	fclose (output);

	TEST_DBUS_CLOSE (server_conn);
	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}


void
test_version_action (void)
{
	pid_t           dbus_pid;
	DBusConnection *server_conn;
	FILE *          output;
	FILE *          errors;
	pid_t           server_pid;
	DBusMessage *   method_call;
	const char *    interface;
	const char *    property;
	DBusMessage *   reply = NULL;
	DBusMessageIter iter;
	DBusMessageIter subiter;
	const char *    str_value;
	NihCommand      command;
	char *          args[1];
	int             ret = 0;
	int             status;

	TEST_FUNCTION ("version_action");
	TEST_DBUS (dbus_pid);
	TEST_DBUS_OPEN (server_conn);

	assert (dbus_bus_request_name (server_conn, DBUS_SERVICE_UPSTART,
				       0, NULL)
			== DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);

	TEST_DBUS_MESSAGE (server_conn, method_call);
	assert (dbus_message_is_signal (method_call, DBUS_INTERFACE_DBUS,
					"NameAcquired"));
	dbus_message_unref (method_call);

	system_bus = TRUE;
	dest_name = DBUS_SERVICE_UPSTART;
	dest_address = DBUS_ADDRESS_UPSTART;

	output = tmpfile ();
	errors = tmpfile ();


	/* Check that the version action queries the server for its
	 * version property, and prints the result to standard output.
	 */
	TEST_FEATURE ("with valid reply");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the Get call for the version property,
			 * reply with the string we want printed.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART);
			TEST_EQ_STR (property, "version");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "init (upstart 1.0.0)";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&iter, &subiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = version_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_EQ (output, "init (upstart 1.0.0)\n");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);
	}


	/* Check that if an error is received from the query command,
	 * the message attached is printed to standard error and the
	 * command exits.
	 */
	TEST_FEATURE ("with error reply");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the Get call for the version property,
			 * reply with an error.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART);
			TEST_EQ_STR (property, "version");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_error (method_call,
								DBUS_ERROR_UNKNOWN_METHOD,
								"Unknown method");
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = version_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_MATCH (errors, "test: *\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGTERM);
		waitpid (server_pid, NULL, 0);
	}


	fclose (errors);
	fclose (output);

	TEST_DBUS_CLOSE (server_conn);
	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}


void
test_log_priority_action (void)
{
	pid_t           dbus_pid;
	DBusConnection *server_conn;
	FILE *          output;
	FILE *          errors;
	pid_t           server_pid;
	DBusMessage *   method_call;
	const char *    interface;
	const char *    property;
	DBusMessage *   reply = NULL;
	DBusMessageIter iter;
	DBusMessageIter subiter;
	const char *    str_value;
	char *          signature;
	NihCommand      command;
	char *          args[2];
	int             ret = 0;
	int             status;

	TEST_FUNCTION ("log_priority_action");
	TEST_DBUS (dbus_pid);
	TEST_DBUS_OPEN (server_conn);

	assert (dbus_bus_request_name (server_conn, DBUS_SERVICE_UPSTART,
				       0, NULL)
			== DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);

	TEST_DBUS_MESSAGE (server_conn, method_call);
	assert (dbus_message_is_signal (method_call, DBUS_INTERFACE_DBUS,
					"NameAcquired"));
	dbus_message_unref (method_call);

	system_bus = TRUE;
	dest_name = DBUS_SERVICE_UPSTART;
	dest_address = DBUS_ADDRESS_UPSTART;

	output = tmpfile ();
	errors = tmpfile ();


	/* Check that, when called without arguments, the log_priority action
	 * queries the server for its log_priority property and prints the
	 * result to standard output.
	 */
	TEST_FEATURE ("with no arguments");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the Get call for the log_priority property,
			 * reply with the string we want printed.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART);
			TEST_EQ_STR (property, "log_priority");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);

				dbus_message_iter_init_append (reply, &iter);

				dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
								  DBUS_TYPE_STRING_AS_STRING,
								  &subiter);

				str_value = "message";
				dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
								&str_value);

				dbus_message_iter_close_container (&iter, &subiter);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = log_priority_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_EQ (output, "message\n");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);
	}



	/* Check that, when called with an argument, the log_priority action
	 * passes that to the server to set its log_priority property.
	 */
	TEST_FEATURE ("with argument");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the Set call for the log_priority property,
			 * send an acknowledgement reply.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Set"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);
			TEST_EQ_STR (dbus_message_get_signature (method_call),
				     (DBUS_TYPE_STRING_AS_STRING
				      DBUS_TYPE_STRING_AS_STRING
				      DBUS_TYPE_VARIANT_AS_STRING));

			dbus_message_iter_init (method_call, &iter);

			dbus_message_iter_get_basic (&iter, &interface);
			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART);

			dbus_message_iter_next (&iter);

			dbus_message_iter_get_basic (&iter, &property);
			TEST_EQ_STR (property, "log_priority");

			dbus_message_iter_next (&iter);
			dbus_message_iter_recurse (&iter, &subiter);

			signature = dbus_message_iter_get_signature (&subiter);
			TEST_EQ_STR (signature, DBUS_TYPE_STRING_AS_STRING);
			dbus_free (signature);

			dbus_message_iter_get_basic (&subiter, &str_value);
			TEST_EQ_STR (str_value, "info");


			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "info";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = log_priority_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		if (test_alloc_failed
		    && (ret != 0)) {
			TEST_FILE_END (output);
			TEST_FILE_RESET (output);

			TEST_FILE_EQ (errors, "test: Cannot allocate memory\n");
			TEST_FILE_END (errors);
			TEST_FILE_RESET (errors);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);
	}


	/* Check that if an error is received from the query command,
	 * the message attached is printed to standard error and the
	 * command exits.
	 */
	TEST_FEATURE ("with no arguments and error reply");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the Get call for the log_priority property,
			 * reply with an error.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);

			TEST_TRUE (dbus_message_get_args (method_call, NULL,
							  DBUS_TYPE_STRING, &interface,
							  DBUS_TYPE_STRING, &property,
							  DBUS_TYPE_INVALID));

			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART);
			TEST_EQ_STR (property, "log_priority");

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_error (method_call,
								DBUS_ERROR_UNKNOWN_METHOD,
								"Unknown method");
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = log_priority_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_MATCH (errors, "test: *\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGTERM);
		waitpid (server_pid, NULL, 0);
	}


	/* Check that if an error is received from the query command,
	 * the message attached is printed to standard error and the
	 * command exits.
	 */
	TEST_FEATURE ("with argument and error reply");
	TEST_ALLOC_FAIL {
		TEST_CHILD (server_pid) {
			/* Expect the Set call for the log_priority property,
			 * send an error back.
			 */
			TEST_DBUS_MESSAGE (server_conn, method_call);

			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Set"));

			TEST_EQ_STR (dbus_message_get_path (method_call),
							    DBUS_PATH_UPSTART);
			TEST_EQ_STR (dbus_message_get_signature (method_call),
				     (DBUS_TYPE_STRING_AS_STRING
				      DBUS_TYPE_STRING_AS_STRING
				      DBUS_TYPE_VARIANT_AS_STRING));

			dbus_message_iter_init (method_call, &iter);

			dbus_message_iter_get_basic (&iter, &interface);
			TEST_EQ_STR (interface, DBUS_INTERFACE_UPSTART);

			dbus_message_iter_next (&iter);

			dbus_message_iter_get_basic (&iter, &property);
			TEST_EQ_STR (property, "log_priority");

			dbus_message_iter_next (&iter);
			dbus_message_iter_recurse (&iter, &subiter);

			signature = dbus_message_iter_get_signature (&subiter);
			TEST_EQ_STR (signature, DBUS_TYPE_STRING_AS_STRING);
			dbus_free (signature);

			dbus_message_iter_get_basic (&subiter, &str_value);
			TEST_EQ_STR (str_value, "info");


			TEST_ALLOC_SAFE {
				reply = dbus_message_new_error (method_call,
								DBUS_ERROR_UNKNOWN_METHOD,
								"Unknown method");
			}

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();

			exit (0);
		}

		memset (&command, 0, sizeof command);

		args[0] = "info";
		args[1] = NULL;

		TEST_DIVERT_STDOUT (output) {
			TEST_DIVERT_STDERR (errors) {
				ret = log_priority_action (&command, args);
			}
		}
		rewind (output);
		rewind (errors);

		TEST_GT (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_FILE_MATCH (errors, "test: *\n");
		TEST_FILE_END (errors);
		TEST_FILE_RESET (errors);

		kill (server_pid, SIGTERM);
		waitpid (server_pid, NULL, 0);
	}


	fclose (errors);
	fclose (output);

	TEST_DBUS_CLOSE (server_conn);
	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}


int
main (int   argc,
      char *argv[])
{
	nih_error_init ();
	nih_timer_init ();
	nih_signal_init ();
	nih_child_init ();
	nih_main_loop_init ();
	program_name = "test";

	test_upstart_open ();
	test_job_status ();

	test_start_action ();
	test_stop_action ();
	test_restart_action ();
	test_reload_action ();
	test_status_action ();
	test_list_action ();
	test_emit_action ();
	test_reload_configuration_action ();
	test_version_action ();
	test_log_priority_action ();

	return 0;
}
