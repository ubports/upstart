/* upstart
 *
 * test_jobs.c - test suite for util/jobs.c
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

#include <upstart/enum.h>
#include <upstart/message.h>

#include <util/jobs.h>


int control_sock = -1;
int destination_pid = 0;

extern int upstart_disable_safeties;


#if 0
void
test_emit_action (void)
{
	NihCommand    cmd;
	NihIoMessage *msg;
	size_t        len;
	FILE         *output;
	char         *args[3];
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
	 * in an event queue message being sent to the server.  Nothing
	 * should be output as a result of this command.
	 */
	TEST_FEATURE ("with emit command");
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

		TEST_EQ (msg->data->len, 19);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\0\010\0\0\0\003foo", 19);

		nih_free (msg);
	}


	/* Check that calling the emit action from the trigger command results
	 * in an event queue message being sent to the server.  Nothing
	 * should be output as a result of this command.
	 */
	TEST_FEATURE ("with trigger command");
	cmd.command = "trigger";
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

		TEST_EQ (msg->data->len, 19);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\0\010\0\0\0\003foo", 19);

		nih_free (msg);
	}


	/* Check that calling the emit action from the shutdown command
	 * results in a shutdown message being sent to the server.  Nothing
	 * should be output as a result of this command.
	 */
	TEST_FEATURE ("with shutdown command");
	cmd.command = "shutdown";
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

		TEST_EQ (msg->data->len, 19);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\0\xe\0\0\0\003foo", 19);

		nih_free (msg);
	}


	/* Check that calling emits without any argument results in an error
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
#endif


void
test_start_action (void)
{
	NihCommand    cmd;
	NihIoMessage *msg;
	size_t        len;
	FILE         *output;
	pid_t         pid;
	char         *args[3];
	int           ret, sock, status;


	TEST_FUNCTION ("start_action");
	program_name = "test";

	nih_error_push_context ();
	nih_error_pop_context ();

	output = tmpfile ();

	sock = upstart_open ();
	destination_pid = getpid ();


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

	TEST_EQ (msg->data->len, 19);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\01\0\0\0\03foo", 19);

	nih_free (msg);


	/* Send back the status */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "foo",
				   JOB_START, JOB_RUNNING, PROCESS_ACTIVE,
				   1000, "foo bar");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output,
		      "test: foo (start) running, process 1000 active\n");
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

	TEST_EQ (msg->data->len, 19);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\02\0\0\0\03foo", 19);

	nih_free (msg);


	/* Send back the status */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "foo",
				   JOB_STOP, JOB_STOPPING, PROCESS_KILLED,
				   1000, "foo bar");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output,
		      "test: foo (stop) stopping, process 1000 killed\n");
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

	TEST_EQ (msg->data->len, 19);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\03\0\0\0\03foo", 19);

	nih_free (msg);


	/* Send back the status */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "foo",
				   JOB_STOP, JOB_WAITING, PROCESS_NONE,
				   -1, "foo bar");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output,
		      "test: foo (stop) waiting\n");
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

	TEST_EQ (msg->data->len, 19);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\03\0\0\0\03foo", 19);

	nih_free (msg);

	/* Send back the status */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "foo",
				   JOB_STOP, JOB_WAITING, PROCESS_NONE,
				   -1, "foo bar");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Should receive another UPSTART_JOB_QUERY */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 19);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\03\0\0\0\03bar", 19);

	nih_free (msg);

	/* Send back the status */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "bar",
				   JOB_START, JOB_RUNNING, PROCESS_SPAWNED,
				   -1, "foo bar");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "test: foo (stop) waiting\n");
	TEST_FILE_EQ (output, "test: bar (start) running, process spawned\n");
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

	TEST_EQ (msg->data->len, 19);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\01\0\0\0\03foo", 19);

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
				   JOB_START, JOB_RUNNING, PROCESS_ACTIVE,
				   1000, "foo bar");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "bilbo",
				   JOB_STOP, JOB_STOPPING, PROCESS_KILLED,
				   1000, "foo bar");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "merry",
				   JOB_STOP, JOB_STOPPING, PROCESS_NONE,
				   -1, "foo bar");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "pippin",
				   JOB_STOP, JOB_WAITING, PROCESS_NONE,
				   -1, "foo bar");
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

	TEST_FILE_EQ (output,
		      "test: frodo (start) running, process 1000 active\n");
	TEST_FILE_EQ (output,
		      "test: bilbo (stop) stopping, process 1000 killed\n");
	TEST_FILE_EQ (output,
		      "test: merry (stop) stopping, process none\n");
	TEST_FILE_EQ (output,
		      "test: pippin (stop) waiting\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	fclose (output);

	close (sock);
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
				   JOB_START, JOB_RUNNING, PROCESS_ACTIVE,
				   1000, "foo bar");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "frodo",
				   JOB_STOP, JOB_STOPPING, PROCESS_KILLED,
				   1000, "foo bar");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "frodo",
				   JOB_STOP, JOB_STOPPING, PROCESS_NONE,
				   -1, "foo bar");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS, "frodo",
				   JOB_STOP, JOB_WAITING, PROCESS_NONE,
				   -1, "foo bar");
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

	TEST_FILE_EQ (output,
		      "test: frodo (start) running, process 1000 active\n");
	TEST_FILE_EQ (output,
		      "test: frodo (stop) stopping, process 1000 killed\n");
	TEST_FILE_EQ (output,
		      "test: frodo (stop) stopping, process none\n");
	TEST_FILE_EQ (output,
		      "test: frodo (stop) waiting\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	fclose (output);

	close (sock);
}


int
main (int   argc,
      char *argv[])
{
	test_start_action ();
	test_list_action ();
	test_jobs_action ();

	return 0;
}
