/* upstart
 *
 * test_events.c - test suite for util/events.c
 *
 * Copyright © 2007 Canonical Ltd.
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

#include <nih/test.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <stdio.h>
#include <stdlib.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/main.h>
#include <nih/io.h>
#include <nih/logging.h>
#include <nih/error.h>
#include <nih/errors.h>

#include <upstart/message.h>

#include <util/events.h>


int control_sock = -1;
int destination_pid = 0;

extern int upstart_disable_safeties;


void
test_emit_action (void)
{
	NihCommand    cmd;
	NihIoMessage *msg;
	size_t        len;
	FILE         *output;
	char         *args[4];
	int           ret, sock;


	TEST_FUNCTION ("emit_action");
	program_name = "test";

	nih_error_push_context ();
	nih_error_pop_context ();

	output = tmpfile ();

	control_sock = socket (PF_UNIX, SOCK_DGRAM, 0);
	sock = upstart_open ();
	destination_pid = getpid ();


	/* Check that calling the emit action from the emit command results
	 * in an event queue message being sent to the server with no
	 * arguments or environment attached.  Nothing should be output as
	 * a result of this command.
	 */
	TEST_FEATURE ("with single argument");
	cmd.command = "emit";
	args[0] = "foo";
	args[1] = NULL;

	TEST_ALLOC_FAIL {
		if (test_alloc_failed) {
			TEST_DIVERT_STDERR (output) {
				ret = emit_action (&cmd, args);
			}
			rewind (output);

			TEST_NE (ret, 0);

			TEST_FILE_EQ (output, ("test: Communication error: "
					       "Cannot allocate memory\n"));
			TEST_FILE_END (output);

			TEST_FILE_RESET (output);
			continue;
		}

		TEST_DIVERT_STDOUT (output) {
			ret = emit_action (&cmd, args);
		}
		rewind (output);

		TEST_EQ (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_ALLOC_SAFE {
			assert (msg = nih_io_message_recv (NULL, sock, &len));
		}

		TEST_EQ (msg->data->len, 22);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\0\010s\0\0\0\03fooAA", 22);

		nih_free (msg);
	}


	/* Check that providing multiple arguments results in the surplus
	 * being used as arguments to the event itself.
	 */
	TEST_FEATURE ("with additional command");
	cmd.command = "emit";
	args[0] = "foo";
	args[1] = "frodo";
	args[2] = "bilbo";
	args[3] = NULL;

	TEST_ALLOC_FAIL {
		if (test_alloc_failed) {
			TEST_DIVERT_STDERR (output) {
				ret = emit_action (&cmd, args);
			}
			rewind (output);

			TEST_NE (ret, 0);

			TEST_FILE_EQ (output, ("test: Communication error: "
					       "Cannot allocate memory\n"));
			TEST_FILE_END (output);

			TEST_FILE_RESET (output);
			continue;
		}

		TEST_DIVERT_STDOUT (output) {
			ret = emit_action (&cmd, args);
		}
		rewind (output);

		TEST_EQ (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_ALLOC_SAFE {
			assert (msg = nih_io_message_recv (NULL, sock, &len));
		}

		TEST_EQ (msg->data->len, 43);
		TEST_EQ_MEM (msg->data->buf,
			     ("upstart\n\0\0\0\010s\0\0\0\003foo"
			      "as\0\0\0\05frodos\0\0\0\05bilboSA"), 43);

		nih_free (msg);
	}


	/* Check that calling emit without any argument results in an error
	 * being sent to stderr.
	 */
	TEST_FEATURE ("with missing argument");
	args[0] = NULL;
	TEST_DIVERT_STDERR (output) {
		ret = emit_action (&cmd, args);
	}
	rewind (output);

	TEST_NE (ret, 0);

	TEST_FILE_EQ (output, "test: missing event name\n");
	TEST_FILE_EQ (output, "Try `test --help' for more information.\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	fclose (output);

	close (sock);
	close (control_sock);
}

void
test_env_option (void)
{
	NihOption   opt;
	char      **value;
	int         ret;

	TEST_FUNCTION ("env_option");
	opt.value = &value;
	value = NULL;
	program_name = "test";


	/* Check that the env_option function takes the argument as a string
	 * and appends it to the array, allocating it if necessary.
	 */
	TEST_FEATURE ("with first argument");
	ret = env_option (&opt, "FOO=BAR");

	TEST_EQ (ret, 0);
	TEST_ALLOC_SIZE (value, sizeof (char *) * 2);
	TEST_ALLOC_PARENT (value[0], value);
	TEST_EQ_STR (value[0], "FOO=BAR");
	TEST_EQ_P (value[1], NULL);


	/* Check that a repeated environment option is appended to the
	 * array.
	 */
	TEST_FEATURE ("with further argument");
	ret = env_option (&opt, "TEA=YES");

	TEST_EQ (ret, 0);
	TEST_ALLOC_SIZE (value, sizeof (char *) * 3);
	TEST_ALLOC_PARENT (value[0], value);
	TEST_ALLOC_PARENT (value[1], value);
	TEST_EQ_STR (value[0], "FOO=BAR");
	TEST_EQ_STR (value[1], "TEA=YES");
	TEST_EQ_P (value[2], NULL);


	/* Check that we can give an environment variable without an equals
	 * to have it picked up from the local environment instead.
	 */
	TEST_FEATURE ("with value from environment");
	setenv ("WIBBLE", "SNARF", TRUE);
	ret = env_option (&opt, "WIBBLE");

	TEST_EQ (ret, 0);
	TEST_ALLOC_SIZE (value, sizeof (char *) * 4);
	TEST_ALLOC_PARENT (value[0], value);
	TEST_ALLOC_PARENT (value[1], value);
	TEST_ALLOC_PARENT (value[2], value);
	TEST_EQ_STR (value[0], "FOO=BAR");
	TEST_EQ_STR (value[1], "TEA=YES");
	TEST_EQ_STR (value[2], "WIBBLE=SNARF");
	TEST_EQ_P (value[3], NULL);


	/* Check that a value not present in the environment is ignored.
	 */
	TEST_FEATURE ("with value not present in environment");
	unsetenv ("MELON");
	ret = env_option (&opt, "MELON");

	TEST_EQ (ret, 0);
	TEST_ALLOC_SIZE (value, sizeof (char *) * 4);
	TEST_ALLOC_PARENT (value[0], value);
	TEST_ALLOC_PARENT (value[1], value);
	TEST_ALLOC_PARENT (value[2], value);
	TEST_EQ_STR (value[0], "FOO=BAR");
	TEST_EQ_STR (value[1], "TEA=YES");
	TEST_EQ_STR (value[2], "WIBBLE=SNARF");
	TEST_EQ_P (value[3], NULL);
}


void
term_handler (int signum)
{
	exit (0);
}

void
test_events_action (void)
{
	NihCommand    cmd;
	NihIoMessage *msg;
	size_t        len;
	FILE         *output;
	pid_t         pid;
	char         *args[3];
	int           ret, sock, status;


	/* Check that the events command sends the watch events message to
	 * the server, and then receives all replies and outputs each event
	 * one per-line.
	 */
	TEST_FUNCTION ("events_action");
	program_name = "test";

	nih_error_push_context ();
	nih_error_pop_context ();

	output = tmpfile ();

	sock = upstart_open ();
	destination_pid = getpid ();

	cmd.command = "events";
	args[0] = "foo";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			signal (SIGTERM, term_handler);
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = events_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive UPSTART_WATCH_EVENTS */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 12);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\xc", 12);

	nih_free (msg);


	/* Send back a couple of events */
	msg = upstart_message_new (NULL, pid, UPSTART_EVENT, "wibble",
				   NULL, NULL);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_EVENT, "frodo",
				   NULL, NULL);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);


	/* Meh; no real way to know ... */
	usleep (500000);
	kill (pid, SIGTERM);


	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "test: wibble event\n");
	TEST_FILE_EQ (output, "test: frodo event\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	fclose (output);

	close (sock);
}


void
test_shutdown_action (void)
{
	NihCommand    cmd;
	NihIoMessage *msg;
	size_t        len;
	FILE         *output;
	char         *args[3];
	int           ret, sock;


	TEST_FUNCTION ("shutdown_action");
	program_name = "test";

	nih_error_push_context ();
	nih_error_pop_context ();

	output = tmpfile ();

	control_sock = socket (PF_UNIX, SOCK_DGRAM, 0);
	sock = upstart_open ();
	destination_pid = getpid ();


	/* Check that calling the shutdown action results in a shutdown
	 * message being sent to the server.  Nothing should be output
	 * as a result of this command.
	 */
	TEST_FEATURE ("with argument");
	cmd.command = "shutdown";
	args[0] = "foo";
	args[1] = NULL;

	TEST_ALLOC_FAIL {
		if (test_alloc_failed) {
			TEST_DIVERT_STDERR (output) {
				ret = shutdown_action (&cmd, args);
			}
			rewind (output);

			TEST_NE (ret, 0);

			TEST_FILE_EQ (output, ("test: Communication error: "
					       "Cannot allocate memory\n"));
			TEST_FILE_END (output);

			TEST_FILE_RESET (output);
			continue;
		}

		TEST_DIVERT_STDOUT (output) {
			ret = shutdown_action (&cmd, args);
		}
		rewind (output);

		TEST_EQ (ret, 0);

		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		TEST_ALLOC_SAFE {
			assert (msg = nih_io_message_recv (NULL, sock, &len));
		}

		TEST_EQ (msg->data->len, 20);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\0\xes\0\0\0\03foo", 20);

		nih_free (msg);
	}


	/* Check that calling shutdown without any argument results in an error
	 * being sent to stderr.
	 */
	TEST_FEATURE ("with missing argument");
	args[0] = NULL;
	TEST_DIVERT_STDERR (output) {
		ret = shutdown_action (&cmd, args);
	}
	rewind (output);

	TEST_NE (ret, 0);

	TEST_FILE_EQ (output, "test: missing event name\n");
	TEST_FILE_EQ (output, "Try `test --help' for more information.\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	fclose (output);

	close (sock);
	close (control_sock);
}


int
main (int   argc,
      char *argv[])
{
	test_emit_action ();
	test_env_option ();
	test_events_action ();
	test_shutdown_action ();

	return 0;
}
