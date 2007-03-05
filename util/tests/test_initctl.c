/* upstart
 *
 * test_initctl.c - test suite for util/initctl.c
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
#include <nih/string.h>
#include <nih/main.h>
#include <nih/io.h>
#include <nih/option.h>
#include <nih/command.h>
#include <nih/logging.h>
#include <nih/error.h>
#include <nih/errors.h>

#include <upstart/enum.h>
#include <upstart/message.h>


extern int upstart_disable_safeties;

extern int env_option      (NihOption *option, const char *arg);

extern int jobs_action     (NihCommand *command, char * const *args);
extern int events_action   (NihCommand *command, char * const *args);
extern int start_action    (NihCommand *command, char * const *args);
extern int list_action     (NihCommand *command, char * const *args);
extern int emit_action     (NihCommand *command, char * const *args);

extern int control_sock;
extern int destination_pid;
extern char **emit_env;


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
test_jobs_action (void)
{
	NihCommand    cmd;
	NihIoMessage *msg;
	size_t        len;
	FILE         *output;
	pid_t         pid;
	char         *args[3];
	int           ret, sock, status;


	/* Check that the jobs command sends the watch jobs message to
	 * the server, and then receives all replies and outputs each job
	 * one per-line.
	 */
	TEST_FUNCTION ("jobs_action");
	program_name = "test";

	nih_error_push_context ();
	nih_error_pop_context ();

	output = tmpfile ();

	sock = upstart_open ();
	destination_pid = getpid ();

	cmd.command = "jobs";
	args[0] = "foo";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			signal (SIGTERM, term_handler);
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = jobs_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive UPSTART_WATCH_JOBS */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 12);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\xa", 12);

	nih_free (msg);


	/* Send back a couple of jobs */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "frodo",
				   JOB_START, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "frodo",
				   JOB_STOP, JOB_KILLED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "frodo",
				   JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "frodo",
				   JOB_STOP, JOB_WAITING, 0);
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

	TEST_FILE_EQ (output, "frodo (start) running\n");
	TEST_FILE_EQ (output, "frodo (stop) killed\n");
	TEST_FILE_EQ (output, "frodo (stop) stopping\n");
	TEST_FILE_EQ (output, "frodo (stop) waiting\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	fclose (output);

	close (sock);
}

void
test_events_action (void)
{
	NihCommand    cmd;
	NihIoMessage *msg;
	size_t        len;
	FILE         *output;
	pid_t         pid;
	char         *args[3], **argv, **env;
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
	msg = upstart_message_new (NULL, pid, UPSTART_EVENT,
				   0xdeafbeef, "wibble", NULL, NULL);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	argv = nih_str_array_new (NULL);
	NIH_MUST (nih_str_array_add (&argv, NULL, NULL, "foo"));
	NIH_MUST (nih_str_array_add (&argv, NULL, NULL, "bar"));

	env = nih_str_array_new (NULL);
	NIH_MUST (nih_str_array_add (&env, NULL, NULL, "FOO=BAR"));

	msg = upstart_message_new (NULL, pid, UPSTART_EVENT,
				   0xdeafbeef, "frodo", argv, env);
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

	TEST_FILE_EQ (output, "wibble\n");
	TEST_FILE_EQ (output, "frodo foo bar\n");
	TEST_FILE_EQ (output, "    FOO=BAR\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	fclose (output);

	close (sock);
}


void
test_start_action (void)
{
	NihCommand    cmd;
	NihIoMessage *msg;
	size_t        len;
	FILE         *output;
	pid_t         pid;
	char         *args[3];
	int           ret = 0, sock, status;


	TEST_FUNCTION ("start_action");
	program_name = "test";

	nih_error_push_context ();
	nih_error_pop_context ();

	output = tmpfile ();

	sock = upstart_open ();
	destination_pid = getpid ();

	setenv ("UPSTART_JOB", "oops", TRUE);


	/* Check that the start command sends the start job message to
	 * the server, and then receives a single status reply and outputs it.
	 */
	TEST_FEATURE ("with start command");
	cmd.command = "start";
	args[0] = "foo";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = start_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive UPSTART_JOB_START */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 20);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\01s\0\0\0\03foo", 20);

	nih_free (msg);


	/* Send back the status */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "foo",
				   JOB_START, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_LIST_END);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo (start) running\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that the stop command sends the stop job message to
	 * the server, and then receives a single status reply and outputs it.
	 */
	TEST_FEATURE ("with stop command");
	cmd.command = "stop";
	args[0] = "foo";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = start_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive UPSTART_JOB_STOP */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 20);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\02s\0\0\0\03foo", 20);

	nih_free (msg);


	/* Send back the status */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "foo",
				   JOB_STOP, JOB_KILLED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_LIST_END);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo (stop) killed\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that the status command sends the query job message to
	 * the server, and then receives a single status reply and outputs it.
	 */
	TEST_FEATURE ("with status command");
	cmd.command = "status";
	args[0] = "foo";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = start_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive UPSTART_JOB_QUERY */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 20);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\03s\0\0\0\03foo", 20);

	nih_free (msg);


	/* Send back the status */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "foo",
				   JOB_STOP, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_LIST_END);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo (stop) waiting\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that the command accepts multiple named jobs and sends
	 * and receives a message for each one.
	 */
	TEST_FEATURE ("with status command");
	cmd.command = "status";
	args[0] = "foo";
	args[1] = "bar";
	args[2] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = start_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive UPSTART_JOB_QUERY */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 20);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\03s\0\0\0\03foo", 20);

	nih_free (msg);

	/* Send back the status */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "foo",
				   JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_LIST_END);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Should receive another UPSTART_JOB_QUERY */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 20);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\03s\0\0\0\03bar", 20);

	nih_free (msg);

	/* Send back the status */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "bar",
				   JOB_START, JOB_SPAWNED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_LIST_END);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo (stop) pre-stop\n");
	TEST_FILE_EQ (output, "bar (start) spawned\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that if the command returns multiple replies, they are all
	 * output.
	 */
	TEST_FEATURE ("with multiple replies");
	cmd.command = "status";
	args[0] = "foo";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = start_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive UPSTART_JOB_QUERY */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 20);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\03s\0\0\0\03foo", 20);

	nih_free (msg);

	/* Send back the status */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "foo",
				   JOB_STOP, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "foo",
				   JOB_START, JOB_PRE_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "foo",
				   JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_LIST_END);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo (stop) waiting\n");
	TEST_FILE_EQ (output, "foo (start) pre-start\n");
	TEST_FILE_EQ (output, "foo (stop) pre-stop\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that the command can respond to an unknown job message
	 * and output it properly.
	 */
	TEST_FEATURE ("with unknown job");
	cmd.command = "start";
	args[0] = "foo";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDERR (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = start_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive UPSTART_JOB_START */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 20);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\01s\0\0\0\03foo", 20);

	nih_free (msg);

	/* Send back unknown job */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_UNKNOWN, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "test: unknown job: foo\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that calling start without any argument results in an error
	 * being sent to stderr.
	 */
	TEST_FEATURE ("with missing argument");
	unsetenv ("UPSTART_JOB");
	cmd.command = "start";
	args[0] = NULL;
	TEST_DIVERT_STDERR (output) {
		ret = start_action (&cmd, args);
	}
	rewind (output);

	TEST_NE (ret, 0);

	TEST_FILE_EQ (output, "test: missing job name\n");
	TEST_FILE_EQ (output, "Try `test --help' for more information.\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that it's ok to call start without any arguments if the
	 * UPSTART_JOB environment variable is set, as that variable can
	 * be used instead.
	 */
	TEST_FEATURE ("with UPSTART_JOB in environment");
	setenv ("UPSTART_JOB", "galen", TRUE);

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = start_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive UPSTART_JOB_START */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 22);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\01s\0\0\0\05galen", 22);

	nih_free (msg);


	/* Send back the status */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "galen",
				   JOB_START, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_LIST_END);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "galen (start) running\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	fclose (output);

	close (sock);
}

void
test_list_action (void)
{
	NihCommand    cmd;
	NihIoMessage *msg;
	size_t        len;
	FILE         *output;
	pid_t         pid;
	char         *args[3];
	int           ret, sock, status;


	/* Check that the list command sends the list jobs message to
	 * the server, and then receives all replies and outputs each job
	 * one per-line.
	 */
	TEST_FUNCTION ("list_action");
	program_name = "test";

	nih_error_push_context ();
	nih_error_pop_context ();

	output = tmpfile ();

	sock = upstart_open ();
	destination_pid = getpid ();

	cmd.command = "list";
	args[0] = "foo";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = list_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive UPSTART_JOB_LIST */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 12);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\06", 12);

	nih_free (msg);


	/* Send back a couple of jobs */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "frodo",
				   JOB_START, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "bilbo",
				   JOB_STOP, JOB_KILLED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "merry",
				   JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "pippin",
				   JOB_STOP, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_LIST_END);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "frodo (start) running\n");
	TEST_FILE_EQ (output, "bilbo (stop) killed\n");
	TEST_FILE_EQ (output, "merry (stop) stopping\n");
	TEST_FILE_EQ (output, "pippin (stop) waiting\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	fclose (output);

	close (sock);
}

void
test_emit_action (void)
{
	NihCommand    cmd;
	NihIoMessage *msg;
	size_t        len;
	FILE         *output;
	char         *args[4];
	pid_t         pid;
	int           ret = 0, sock, status;


	TEST_FUNCTION ("emit_action");
	program_name = "test";

	nih_error_push_context ();
	nih_error_pop_context ();

	output = tmpfile ();

	sock = upstart_open ();
	destination_pid = getpid ();


	/* Check that calling the emit action from the emit command results
	 * in an event emit message being sent to the server with no
	 * arguments or environment attached.
	 *
	 * The command should output the event information when handling
	 * begins, along with a summary of each job changed by it.
	 */
	TEST_FEATURE ("with single argument");
	cmd.command = "emit";
	args[0] = "foo";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = emit_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive UPSTART_EVENT_EMIT */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 22);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x02\x00s\0\0\0\03fooAA", 22);

	nih_free (msg);

	/* Send back a couple of messages */
	msg = upstart_message_new (NULL, pid, UPSTART_EVENT,
				   0xdeafbeef, "foo", NULL, NULL);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_EVENT_FINISHED,
				   0xdeafbeef, FALSE, "foo", NULL, NULL);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* The child should have exited on its own */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo\n");
	TEST_FILE_EQ (output, "test (start) waiting\n");
	TEST_FILE_EQ (output, "test (start) running\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that the exit status is not zero if the event failed, and
	 * a warning is output to stderr.
	 */
	TEST_FEATURE ("with failed event");
	cmd.command = "emit";
	args[0] = "foo";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			fflush (stderr);
			dup2 (fileno (stdout), fileno (stderr));

			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = emit_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive UPSTART_EVENT_EMIT */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 22);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x02\x00s\0\0\0\03fooAA", 22);

	nih_free (msg);

	/* Send back a couple of messages */
	msg = upstart_message_new (NULL, pid, UPSTART_EVENT,
				   0xdeafbeef, "foo", NULL, NULL);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_EVENT_FINISHED,
				   0xdeafbeef, TRUE, "foo", NULL, NULL);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* The child should have exited on its own */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 1);

	TEST_FILE_EQ (output, "foo\n");
	TEST_FILE_EQ (output, "test (start) waiting\n");
	TEST_FILE_EQ (output, "test (start) running\n");
	TEST_FILE_EQ (output, "test: foo event failed\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that providing multiple arguments results in the surplus
	 * being used as arguments to the event itself.
	 */
	TEST_FEATURE ("with additional arguments");
	cmd.command = "emit";
	args[0] = "foo";
	args[1] = "frodo";
	args[2] = "bilbo";
	args[3] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = emit_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive UPSTART_EVENT_EMIT */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 43);
	TEST_EQ_MEM (msg->data->buf,
		     ("upstart\n\0\0\x02\x00s\0\0\0\003foo"
		      "as\0\0\0\05frodos\0\0\0\05bilboSA"), 43);

	nih_free (msg);

	/* Send back a couple of messages */
	msg = upstart_message_new (NULL, pid, UPSTART_EVENT,
				   0xdeafbeef, "foo", &(args[1]), NULL);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_EVENT_FINISHED,
				   0xdeafbeef, FALSE, "foo", &(args[1]), NULL);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* The child should have exited on its own */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo frodo bilbo\n");
	TEST_FILE_EQ (output, "test (start) waiting\n");
	TEST_FILE_EQ (output, "test (start) running\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that providing multiple arguments results in the surplus
	 * being used as arguments to the event itself, and that the
	 * environment in emit_env is used.
	 */
	TEST_FEATURE ("with additional arguments and environment");
	cmd.command = "emit";
	args[0] = "foo";
	args[1] = "frodo";
	args[2] = "bilbo";
	args[3] = NULL;

	emit_env = nih_str_array_new (NULL);
	NIH_MUST (nih_str_array_add (&emit_env, NULL, NULL, "FOO=BAR"));

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = emit_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive UPSTART_EVENT_EMIT */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 56);
	TEST_EQ_MEM (msg->data->buf,
		     ("upstart\n\0\0\x02\x00s\0\0\0\003foo"
		      "as\0\0\0\05frodos\0\0\0\05bilboS"
		      "as\0\0\0\07FOO=BARS"), 56);

	nih_free (msg);

	/* Send back a couple of messages */
	msg = upstart_message_new (NULL, pid, UPSTART_EVENT,
				   0xdeafbeef, "foo", &(args[1]), emit_env);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_EVENT_FINISHED,
				   0xdeafbeef, FALSE, "foo",
				   &(args[1]), emit_env);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	nih_free (emit_env);
	emit_env = NULL;

	/* The child should have exited on its own */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo frodo bilbo\n");
	TEST_FILE_EQ (output, "    FOO=BAR\n");
	TEST_FILE_EQ (output, "test (start) waiting\n");
	TEST_FILE_EQ (output, "test (start) running\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


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




int
main (int   argc,
      char *argv[])
{
	test_env_option ();
	test_jobs_action ();
	test_events_action ();
	test_start_action ();
	test_list_action ();
	test_emit_action ();

	return 0;
}
