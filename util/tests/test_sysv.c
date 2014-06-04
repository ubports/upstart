/* upstart
 *
 * test_sysv.c - test suite for util/sysv.c
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

#include <nih/test.h>
#include <nih-dbus/test_dbus.h>

#include <dbus/dbus.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/utsname.h>

#include <utmpx.h>
#include <limits.h>
#include <signal.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/timer.h>
#include <nih/signal.h>
#include <nih/child.h>
#include <nih/main.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <nih-dbus/dbus_error.h>
#include <nih-dbus/dbus_connection.h>
#include <nih-dbus/errors.h>

#include "dbus/upstart.h"

#include "utmp.h"
#include "sysv.h"


extern const char *dest_address;


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
test_change_runlevel (void)
{
	pid_t           server_pid;
	int             wait_fd;
	DBusServer *    server = NULL;
	DBusMessage *   method_call;
	DBusMessage *   reply = NULL;
	const char *    name_value;
	char **         args_value;
	int             args_elements;
	int             wait_value;
	char            utmp_file[PATH_MAX];
	char            wtmp_file[PATH_MAX];
	struct utmpx    record;
	struct utmpx *  utmp;
	struct utsname  uts;
	struct timeval  tv;
	int             ret;
	char *          args[3];
	NihError *      err;
	NihDBusError *  dbus_err;
	int             status;


	TEST_FUNCTION ("sysv_change_runlevel");
	dest_address = "unix:abstract=/com/ubuntu/upstart/test_sysv";

	TEST_FILENAME (utmp_file);
	TEST_FILENAME (wtmp_file);


	/* Check that we can change the runlevel, the previous runlevel
	 * should be obtained from /var/run/utmp and the utmp record updated
	 * as well as a new record being added to /var/log/wtmp.  An event
	 * should be emitted containing both the new and old runlevel
	 * as arguments.
	 */
	TEST_FEATURE ("with new runlevel");
	unsetenv ("RUNLEVEL");
	unsetenv ("PREVLEVEL");

	TEST_ALLOC_FAIL {
		TEST_CHILD_WAIT (server_pid, wait_fd) {
			TEST_ALLOC_SAFE {
				server = nih_dbus_server (dest_address,
							  my_connect_handler,
							  NULL);
				if (! server) {
				    NihError *err = nih_error_get ();
				    NihDBusError *dbus_err = (NihDBusError *)err;
				    nih_message ("%s: %s", dbus_err->name, dbus_err->message);
				}
				assert (server != NULL);
			}

			my_connect_handler_called = FALSE;
			last_connection = NULL;

			TEST_CHILD_RELEASE (wait_fd);

			/* Wait for a connection from the server */
			nih_main_loop ();

			assert (my_connect_handler_called);
			assert (last_connection);

			/* Expect the EmitEvent method call on the manager
			 * object, make sure the arguments are right and
			 * reply to acknowledge.
			 */
			TEST_DBUS_MESSAGE (last_connection, method_call);

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

			TEST_EQ_STR (name_value, "runlevel");

			TEST_EQ (args_elements, 2);
			TEST_EQ_STR (args_value[0], "RUNLEVEL=5");
			TEST_EQ_STR (args_value[1], "PREVLEVEL=2");
			dbus_free_string_array (args_value);

			TEST_FALSE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);
			}

			dbus_connection_send (last_connection, reply, NULL);
			dbus_connection_flush (last_connection);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			dbus_connection_close (last_connection);
			dbus_connection_unref (last_connection);

			dbus_server_disconnect (server);
			dbus_server_unref (server);

			dbus_shutdown ();

			exit (0);
		}


		unlink (utmp_file);
		fclose (fopen (utmp_file, "w"));

		unlink (wtmp_file);
		fclose (fopen (wtmp_file, "w"));

		memset (&record, 0, sizeof record);

		record.ut_type = RUN_LVL;
		record.ut_pid = '2' + 'S' * 256;

		strcpy (record.ut_line, "~");
		strcpy (record.ut_id, "~~");
		strncpy (record.ut_user, "runlevel", sizeof record.ut_user);
		if (uname (&uts) == 0)
			strncpy (record.ut_host, uts.release,
				 sizeof record.ut_host);

		gettimeofday (&tv, NULL);
		record.ut_tv.tv_sec = tv.tv_sec;
		record.ut_tv.tv_usec = tv.tv_usec;
		/* See comment in test_utmp */
		usleep(200);

		utmpxname (utmp_file);

		setutxent ();
		pututxline (&record);
		endutxent ();

		updwtmpx (wtmp_file, &record);


		ret = sysv_change_runlevel ('5', NULL, utmp_file, wtmp_file);

		if (test_alloc_failed
		    && (ret < 0)) {
			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);

			/* Make sure no runlevel was written */
			utmpxname (utmp_file);
			setutxent ();

			utmp = getutxent ();
			TEST_NE_P (utmp, NULL);

			TEST_EQ (utmp->ut_type, RUN_LVL);
			TEST_EQ (utmp->ut_pid, '2' + 'S' * 256);
			TEST_EQ_STR (utmp->ut_line, "~");
			TEST_EQ_STR (utmp->ut_id, "~~");
			TEST_EQ_STR (utmp->ut_user, "runlevel");

			TEST_EQ (utmp->ut_tv.tv_sec, tv.tv_sec);
			TEST_EQ (utmp->ut_tv.tv_usec, tv.tv_usec);

			utmp = getutxent ();
			TEST_EQ_P (utmp, NULL);

			endutxent ();


			utmpxname (wtmp_file);
			setutxent ();

			utmp = getutxent ();
			TEST_NE_P (utmp, NULL);

			TEST_EQ (utmp->ut_type, RUN_LVL);
			TEST_EQ (utmp->ut_pid, '2' + 'S' * 256);
			TEST_EQ_STR (utmp->ut_line, "~");
			TEST_EQ_STR (utmp->ut_id, "~~");
			TEST_EQ_STR (utmp->ut_user, "runlevel");

			TEST_EQ (utmp->ut_tv.tv_sec, tv.tv_sec);
			TEST_EQ (utmp->ut_tv.tv_usec, tv.tv_usec);

			utmp = getutxent ();
			TEST_EQ_P (utmp, NULL);

			endutxent ();

			dbus_shutdown ();
			continue;
		}

		TEST_EQ (ret, 0);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		utmpxname (utmp_file);
		setutxent ();

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, '5' + '2' * 256);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		if (utmp->ut_tv.tv_sec == tv.tv_sec) {
			TEST_NE (utmp->ut_tv.tv_usec, tv.tv_usec);
		} else {
			TEST_NE (utmp->ut_tv.tv_sec, tv.tv_sec);
		}

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);

		endutxent ();


		utmpxname (wtmp_file);
		setutxent ();

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, '2' + 'S' * 256);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		TEST_EQ (utmp->ut_tv.tv_sec, tv.tv_sec);
		TEST_EQ (utmp->ut_tv.tv_usec, tv.tv_usec);

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, '5' + '2' * 256);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		if (utmp->ut_tv.tv_sec == tv.tv_sec) {
			TEST_NE (utmp->ut_tv.tv_usec, tv.tv_usec);
		} else {
			TEST_NE (utmp->ut_tv.tv_sec, tv.tv_sec);
		}

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);

		endutxent ();

		dbus_shutdown ();
	}


	/* Check that when called from the rc-sysvinit script, with the
	 * RUNLEVEL and PREVLEVEL variables empty and no valid information
	 * in the utmp or wtmp files, N is used as the previous runlevel
	 * and reboot is added.
	 */
	TEST_FEATURE ("with sysvinit to 2 switch");
	setenv ("RUNLEVEL", "", TRUE);
	setenv ("PREVLEVEL", "", TRUE);

	TEST_ALLOC_FAIL {
		TEST_CHILD_WAIT (server_pid, wait_fd) {
			TEST_ALLOC_SAFE {
				server = nih_dbus_server (dest_address,
							  my_connect_handler,
							  NULL);
				assert (server != NULL);
			}

			my_connect_handler_called = FALSE;
			last_connection = NULL;

			TEST_CHILD_RELEASE (wait_fd);

			/* Wait for a connection from the server */
			nih_main_loop ();

			assert (my_connect_handler_called);
			assert (last_connection);

			/* Expect the EmitEvent method call on the manager
			 * object, make sure the arguments are right and
			 * reply to acknowledge.
			 */
			TEST_DBUS_MESSAGE (last_connection, method_call);

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

			TEST_EQ_STR (name_value, "runlevel");

			TEST_EQ (args_elements, 2);
			TEST_EQ_STR (args_value[0], "RUNLEVEL=2");
			TEST_EQ_STR (args_value[1], "PREVLEVEL=N");
			dbus_free_string_array (args_value);

			TEST_FALSE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);
			}

			dbus_connection_send (last_connection, reply, NULL);
			dbus_connection_flush (last_connection);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			dbus_connection_close (last_connection);
			dbus_connection_unref (last_connection);

			dbus_server_disconnect (server);
			dbus_server_unref (server);

			dbus_shutdown ();

			exit (0);
		}


		unlink (utmp_file);
		fclose (fopen (utmp_file, "w"));

		unlink (wtmp_file);
		fclose (fopen (wtmp_file, "w"));


		ret = sysv_change_runlevel ('2', NULL, utmp_file, wtmp_file);

		if (test_alloc_failed
		    && (ret < 0)) {
			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);

			/* Make sure no runlevel was written */
			utmpxname (utmp_file);
			setutxent ();

			utmp = getutxent ();
			TEST_EQ_P (utmp, NULL);

			endutxent ();


			utmpxname (wtmp_file);
			setutxent ();

			utmp = getutxent ();
			TEST_EQ_P (utmp, NULL);

			endutxent ();

			dbus_shutdown ();
			continue;
		}

		TEST_EQ (ret, 0);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		utmpxname (utmp_file);
		setutxent ();

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, BOOT_TIME);
		TEST_EQ (utmp->ut_pid, 0);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "reboot");

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, '2');
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);

		endutxent ();


		utmpxname (wtmp_file);
		setutxent ();

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, BOOT_TIME);
		TEST_EQ (utmp->ut_pid, 0);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "reboot");

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, '2');
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);

		endutxent ();

		dbus_shutdown ();
	}

	unsetenv ("RUNLEVEL");
	unsetenv ("PREVLEVEL");


	/* Check that when called from the rcS script, with the RUNLEVEL
	 * and PREVLEVEL variables set but no valid information in the utmp
	 * or wtmp files, the environment is used and reboot records are
	 * added as well.
	 */
	TEST_FEATURE ("with runlevel S to 2 switch");
	setenv ("RUNLEVEL", "S", TRUE);
	setenv ("PREVLEVEL", "", TRUE);

	TEST_ALLOC_FAIL {
		TEST_CHILD_WAIT (server_pid, wait_fd) {
			TEST_ALLOC_SAFE {
				server = nih_dbus_server (dest_address,
							  my_connect_handler,
							  NULL);
				assert (server != NULL);
			}

			my_connect_handler_called = FALSE;
			last_connection = NULL;

			TEST_CHILD_RELEASE (wait_fd);

			/* Wait for a connection from the server */
			nih_main_loop ();

			assert (my_connect_handler_called);
			assert (last_connection);

			/* Expect the EmitEvent method call on the manager
			 * object, make sure the arguments are right and
			 * reply to acknowledge.
			 */
			TEST_DBUS_MESSAGE (last_connection, method_call);

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

			TEST_EQ_STR (name_value, "runlevel");

			TEST_EQ (args_elements, 2);
			TEST_EQ_STR (args_value[0], "RUNLEVEL=2");
			TEST_EQ_STR (args_value[1], "PREVLEVEL=S");
			dbus_free_string_array (args_value);

			TEST_FALSE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);
			}

			dbus_connection_send (last_connection, reply, NULL);
			dbus_connection_flush (last_connection);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			dbus_connection_close (last_connection);
			dbus_connection_unref (last_connection);

			dbus_server_disconnect (server);
			dbus_server_unref (server);

			dbus_shutdown ();

			exit (0);
		}


		unlink (utmp_file);
		fclose (fopen (utmp_file, "w"));

		unlink (wtmp_file);
		fclose (fopen (wtmp_file, "w"));


		ret = sysv_change_runlevel ('2', NULL, utmp_file, wtmp_file);

		if (test_alloc_failed
		    && (ret < 0)) {
			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);

			/* Make sure no runlevel was written */
			utmpxname (utmp_file);
			setutxent ();

			utmp = getutxent ();
			TEST_EQ_P (utmp, NULL);

			endutxent ();


			utmpxname (wtmp_file);
			setutxent ();

			utmp = getutxent ();
			TEST_EQ_P (utmp, NULL);

			endutxent ();

			dbus_shutdown ();
			continue;
		}

		TEST_EQ (ret, 0);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		utmpxname (utmp_file);
		setutxent ();

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, BOOT_TIME);
		TEST_EQ (utmp->ut_pid, 0);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "reboot");

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, '2' + 'S' * 256);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);

		endutxent ();


		utmpxname (wtmp_file);
		setutxent ();

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, BOOT_TIME);
		TEST_EQ (utmp->ut_pid, 0);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "reboot");

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, '2' + 'S' * 256);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);

		endutxent ();

		dbus_shutdown ();
	}

	unsetenv ("RUNLEVEL");
	unsetenv ("PREVLEVEL");


	/* Check that additional environment variables are appended to
	 * the environment passed in the event.
	 */
	TEST_FEATURE ("with new runlevel");
	TEST_ALLOC_FAIL {
		TEST_CHILD_WAIT (server_pid, wait_fd) {
			TEST_ALLOC_SAFE {
				server = nih_dbus_server (dest_address,
							  my_connect_handler,
							  NULL);
				assert (server != NULL);
			}

			my_connect_handler_called = FALSE;
			last_connection = NULL;

			TEST_CHILD_RELEASE (wait_fd);

			/* Wait for a connection from the server */
			nih_main_loop ();

			assert (my_connect_handler_called);
			assert (last_connection);

			/* Expect the EmitEvent method call on the manager
			 * object, make sure the arguments are right and
			 * reply to acknowledge.
			 */
			TEST_DBUS_MESSAGE (last_connection, method_call);

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

			TEST_EQ_STR (name_value, "runlevel");

			TEST_EQ (args_elements, 4);
			TEST_EQ_STR (args_value[0], "RUNLEVEL=0");
			TEST_EQ_STR (args_value[1], "PREVLEVEL=2");
			TEST_EQ_STR (args_value[2], "INIT_HALT=poweroff");
			TEST_EQ_STR (args_value[3], "USER=scott");
			dbus_free_string_array (args_value);

			TEST_FALSE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_method_return (method_call);
			}

			dbus_connection_send (last_connection, reply, NULL);
			dbus_connection_flush (last_connection);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			dbus_connection_close (last_connection);
			dbus_connection_unref (last_connection);

			dbus_server_disconnect (server);
			dbus_server_unref (server);

			dbus_shutdown ();

			exit (0);
		}


		unlink (utmp_file);
		fclose (fopen (utmp_file, "w"));

		unlink (wtmp_file);
		fclose (fopen (wtmp_file, "w"));

		memset (&record, 0, sizeof record);

		record.ut_type = RUN_LVL;
		record.ut_pid = '2' + 'S' * 256;

		strcpy (record.ut_line, "~");
		strcpy (record.ut_id, "~~");
		strncpy (record.ut_user, "runlevel", sizeof record.ut_user);
		if (uname (&uts) == 0)
			strncpy (record.ut_host, uts.release,
				 sizeof record.ut_host);

		gettimeofday (&tv, NULL);
		record.ut_tv.tv_sec = tv.tv_sec;
		record.ut_tv.tv_usec = tv.tv_usec;
		/* See comment in test_utmp */
		usleep(200);

		utmpxname (utmp_file);

		setutxent ();
		pututxline (&record);
		endutxent ();

		updwtmpx (wtmp_file, &record);


		args[0] = "INIT_HALT=poweroff";
		args[1] = "USER=scott";
		args[2] = NULL;

		ret = sysv_change_runlevel ('0', args, utmp_file, wtmp_file);

		if (test_alloc_failed
		    && (ret < 0)) {
			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);

			/* Make sure no runlevel was written */
			utmpxname (utmp_file);
			setutxent ();

			utmp = getutxent ();
			TEST_NE_P (utmp, NULL);

			TEST_EQ (utmp->ut_type, RUN_LVL);
			TEST_EQ (utmp->ut_pid, '2' + 'S' * 256);
			TEST_EQ_STR (utmp->ut_line, "~");
			TEST_EQ_STR (utmp->ut_id, "~~");
			TEST_EQ_STR (utmp->ut_user, "runlevel");

			TEST_EQ (utmp->ut_tv.tv_sec, tv.tv_sec);
			TEST_EQ (utmp->ut_tv.tv_usec, tv.tv_usec);

			utmp = getutxent ();
			TEST_EQ_P (utmp, NULL);

			endutxent ();


			utmpxname (wtmp_file);
			setutxent ();

			utmp = getutxent ();
			TEST_NE_P (utmp, NULL);

			TEST_EQ (utmp->ut_type, RUN_LVL);
			TEST_EQ (utmp->ut_pid, '2' + 'S' * 256);
			TEST_EQ_STR (utmp->ut_line, "~");
			TEST_EQ_STR (utmp->ut_id, "~~");
			TEST_EQ_STR (utmp->ut_user, "runlevel");

			TEST_EQ (utmp->ut_tv.tv_sec, tv.tv_sec);
			TEST_EQ (utmp->ut_tv.tv_usec, tv.tv_usec);

			utmp = getutxent ();
			TEST_EQ_P (utmp, NULL);

			endutxent ();

			dbus_shutdown ();
			continue;
		}

		TEST_EQ (ret, 0);

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		utmpxname (utmp_file);
		setutxent ();

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, '0' + '2' * 256);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		if (utmp->ut_tv.tv_sec == tv.tv_sec) {
			TEST_NE (utmp->ut_tv.tv_usec, tv.tv_usec);
		} else {
			TEST_NE (utmp->ut_tv.tv_sec, tv.tv_sec);
		}

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);

		endutxent ();


		utmpxname (wtmp_file);
		setutxent ();

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, '2' + 'S' * 256);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		TEST_EQ (utmp->ut_tv.tv_sec, tv.tv_sec);
		TEST_EQ (utmp->ut_tv.tv_usec, tv.tv_usec);

		utmp = getutxent ();
		TEST_NE_P (utmp, NULL);

		TEST_EQ (utmp->ut_type, RUN_LVL);
		TEST_EQ (utmp->ut_pid, '0' + '2' * 256);
		TEST_EQ_STR (utmp->ut_line, "~");
		TEST_EQ_STR (utmp->ut_id, "~~");
		TEST_EQ_STR (utmp->ut_user, "runlevel");

		if (utmp->ut_tv.tv_sec == tv.tv_sec) {
			TEST_NE (utmp->ut_tv.tv_usec, tv.tv_usec);
		} else {
			TEST_NE (utmp->ut_tv.tv_sec, tv.tv_sec);
		}

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);

		endutxent ();

		dbus_shutdown ();
	}


	/* Check that an error connecting to the upstart daemon is returned
	 * as a raised error, without writing any runlevel entries to the
	 * logs.
	 */
	TEST_FEATURE ("with error connecting");
	TEST_ALLOC_FAIL {
		unlink (utmp_file);
		fclose (fopen (utmp_file, "w"));

		unlink (wtmp_file);
		fclose (fopen (wtmp_file, "w"));

		ret = sysv_change_runlevel ('5', NULL, utmp_file, wtmp_file);

		TEST_LT (ret, 0);

		err = nih_error_get ();

		if ((! test_alloc_failed) || (err->number != ENOMEM)) {
			TEST_EQ (err->number, NIH_DBUS_ERROR);
			TEST_ALLOC_SIZE (err, sizeof (NihDBusError));

			dbus_err = (NihDBusError *)err;
			TEST_EQ_STR (dbus_err->name, DBUS_ERROR_NO_SERVER);
		}

		nih_free (err);

		/* Make sure no runlevel was written */
		utmpxname (utmp_file);
		setutxent ();

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);

		endutxent ();


		utmpxname (wtmp_file);
		setutxent ();

		utmp = getutxent ();
		TEST_EQ_P (utmp, NULL);

		endutxent ();

		dbus_shutdown ();
	}


	/* Check that an error reply from the upstart daemon for the
	 * EmitEvent method is returned as a raised error.
	 */
	TEST_FEATURE ("with error reply from method");
	TEST_ALLOC_FAIL {
		TEST_CHILD_WAIT (server_pid, wait_fd) {
			TEST_ALLOC_SAFE {
				server = nih_dbus_server (dest_address,
							  my_connect_handler,
							  NULL);
				assert (server != NULL);
			}

			my_connect_handler_called = FALSE;
			last_connection = NULL;

			TEST_CHILD_RELEASE (wait_fd);

			/* Wait for a connection from the server */
			nih_main_loop ();

			assert (my_connect_handler_called);
			assert (last_connection);

			/* Expect the EmitEvent method call on the manager
			 * object, make sure the arguments are right and
			 * reply with an error.
			 */
			TEST_DBUS_MESSAGE (last_connection, method_call);

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

			TEST_EQ_STR (name_value, "runlevel");

			TEST_EQ (args_elements, 2);
			TEST_EQ_STR (args_value[0], "RUNLEVEL=2");
			TEST_EQ_STR (args_value[1], "PREVLEVEL=N");
			dbus_free_string_array (args_value);

			TEST_FALSE (wait_value);

			TEST_ALLOC_SAFE {
				reply = dbus_message_new_error (method_call,
								DBUS_ERROR_UNKNOWN_METHOD,
								"Unknown method");
			}

			dbus_connection_send (last_connection, reply, NULL);
			dbus_connection_flush (last_connection);

			dbus_message_unref (method_call);
			dbus_message_unref (reply);

			dbus_connection_close (last_connection);
			dbus_connection_unref (last_connection);

			dbus_server_disconnect (server);
			dbus_server_unref (server);

			dbus_shutdown ();

			exit (0);
		}

		unlink (utmp_file);
		fclose (fopen (utmp_file, "w"));

		unlink (wtmp_file);
		fclose (fopen (wtmp_file, "w"));

		ret = sysv_change_runlevel ('2', NULL, utmp_file, wtmp_file);

		TEST_LT (ret, 0);

		err = nih_error_get ();

		if (test_alloc_failed
		    && (err->number == ENOMEM)) {
			nih_free (err);

			kill (server_pid, SIGTERM);
			waitpid (server_pid, NULL, 0);

			dbus_shutdown ();
			continue;
		}

		waitpid (server_pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		TEST_EQ (err->number, NIH_DBUS_ERROR);
		TEST_ALLOC_SIZE (err, sizeof (NihDBusError));

		dbus_err = (NihDBusError *)err;
		TEST_EQ_STR (dbus_err->name, DBUS_ERROR_UNKNOWN_METHOD);
		nih_free (dbus_err);

		dbus_shutdown ();
	}


	unlink (utmp_file);
	unlink (wtmp_file);
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

	test_change_runlevel ();

	return 0;
}
