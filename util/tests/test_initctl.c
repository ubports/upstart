/* upstart
 *
 * test_initctl.c - test suite for util/initctl.c
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


NIH_BEGIN_EXTERN

int upstart_disable_safeties;

int env_option          (NihOption *option, const char *arg);
int start_action        (NihCommand *command, char * const *args);
int stop_action         (NihCommand *command, char * const *args);
int status_action       (NihCommand *command, char * const *args);
int list_action         (NihCommand *command, char * const *args);
int emit_action         (NihCommand *command, char * const *args);
int jobs_action         (NihCommand *command, char * const *args);
int events_action       (NihCommand *command, char * const *args);
int version_action      (NihCommand *command, char * const *args);
int log_priority_action (NihCommand *command, char * const *args);

int control_sock;
int destination_pid;
int show_ids;
int by_id;
int no_wait;
char **emit_env;

NIH_END_EXTERN


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


	nih_free (value);
}


static void
maybe_message_send (NihIoMessage *message,
		    int           fd)
{
	if (nih_io_message_send (message, fd) < 0) {
		NihError *err;

		err = nih_error_get ();
		TEST_EQ (err->number, ECONNREFUSED);
		nih_free (err);
	}
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
	int           ret, sock, status;

	TEST_FUNCTION ("start_action");
	program_name = "test";

	output = tmpfile ();

	sock = upstart_open ();
	destination_pid = getpid ();

	nih_error_push_context ();
	nih_error_pop_context ();


	/* Check that the start command can have a single argument containing
	 * a job name, that is sends a request to start that job and outputs
	 * status messages received until the job reaches the final goal.
	 */
	TEST_FEATURE ("with single job to start by name");
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

	/* Should receive an UPSTART_JOB_START message containing the job
	 * name.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\02s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB,
				   0xdeadbeef, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START, JOB_STARTING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START, JOB_STARTING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_PRE_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_PRE_START, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_PRE_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START, JOB_SPAWNED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1010);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START, JOB_SPAWNED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_POST_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1010);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_POST_START, 1050);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_POST_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1010);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_FINISHED,
				   0xdeadbeef, "foo", FALSE, -1, 0);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo (start) waiting\n");
	TEST_FILE_EQ (output, "foo (start) starting\n");
	TEST_FILE_EQ (output, "foo (start) pre-start, process 1000\n");
	TEST_FILE_EQ (output, "foo (start) spawned, process 1010\n");
	TEST_FILE_EQ (output, "foo (start) post-start, process 1050\n");
	TEST_FILE_EQ (output, "\tmain process 1010\n");
	TEST_FILE_EQ (output, "foo (start) running, process 1010\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that the start command can be called with --by-id and have
	 * a single argument containing a job id, and the request has a NULL
	 * name and the given id.
	 */
	TEST_FEATURE ("with single job to start by id");
	by_id = TRUE;
	cmd.command = "start";
	args[0] = "1000";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = start_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive an UPSTART_JOB_START message containing the job
	 * id..
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 18);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\02Su\0\0\x3\xe8", 18);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB,
				   1000, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_START, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_START, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_START, JOB_STARTING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_START, JOB_STARTING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_START, JOB_PRE_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_START, JOB_PRE_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_START, JOB_SPAWNED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_START, JOB_SPAWNED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_START, JOB_POST_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_START, JOB_POST_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_START, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_START, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_FINISHED,
				   1000, "foo", FALSE, -1, 0);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo (start) waiting\n");
	TEST_FILE_EQ (output, "foo (start) starting\n");
	TEST_FILE_EQ (output, "foo (start) pre-start\n");
	TEST_FILE_EQ (output, "foo (start) spawned, process 1100\n");
	TEST_FILE_EQ (output, "foo (start) post-start, (main) process 1100\n");
	TEST_FILE_EQ (output, "foo (start) running, process 1100\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	by_id = FALSE;


	/* Check that in the case of no arguments, the UPSTART_JOB_ID
	 * environment variable is used (in preferences to UPSTART_JOB);
	 * this should imply the --no-wait flag, since otherwise it'd wait
	 * for itself.
	 */
	TEST_FEATURE ("with single job to start from UPSTART_JOB_ID");
	setenv ("UPSTART_JOB_ID", "1000", TRUE);
	setenv ("UPSTART_JOB", "foo", TRUE);

	cmd.command = "start";
	args[0] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = start_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive an UPSTART_JOB_START message containing the job
	 * id from the environment variable.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 18);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\02Su\0\0\x3\xe8", 18);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB,
				   1000, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_START, JOB_WAITING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_START, JOB_WAITING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_START, JOB_STARTING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_START, JOB_STARTING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_START, JOB_PRE_START);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_START, JOB_PRE_START);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_START, JOB_SPAWNED);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_START, JOB_SPAWNED);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_START, JOB_POST_START);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_START, JOB_POST_START);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_START, JOB_RUNNING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_START, JOB_RUNNING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_FINISHED,
				   1000, "foo", FALSE, -1, 0);
	maybe_message_send (msg, sock);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo: goal changed\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	unsetenv ("UPSTART_JOB_ID");
	unsetenv ("UPSTART_JOB");


	/* Check that if there are no arguments and UPSTART_JOB_ID is not set,
	 * the UPSTART_JOB environment variable is used.  This should imply
	 * the --no-wait flag, since otherwise it'd wait for itself.
	 */
	TEST_FEATURE ("with single job to start from UPSTART_JOB");
	setenv ("UPSTART_JOB", "foo", TRUE);

	cmd.command = "start";
	args[0] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = start_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive an UPSTART_JOB_START message containing the job
	 * name from the environment variable.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\02s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB,
				   1000, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_START, JOB_WAITING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_START, JOB_WAITING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_START, JOB_STARTING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_START, JOB_STARTING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_START, JOB_PRE_START);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_START, JOB_PRE_START);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_START, JOB_SPAWNED);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_START, JOB_SPAWNED);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_START, JOB_POST_START);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_START, JOB_POST_START);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_START, JOB_RUNNING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_START, JOB_RUNNING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_FINISHED,
				   1000, "foo", FALSE, -1, 0);
	maybe_message_send (msg, sock);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo: goal changed\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	unsetenv ("UPSTART_JOB");


	/* Check that output includes the job id when show ids is given, also
	 * check that when a job fails to start, the final message is a
	 * warning about that and the exit status is one.
	 */
	TEST_FEATURE ("with job ids shown and failed job");
	show_ids = TRUE;
	cmd.command = "start";
	args[0] = "foo";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			dup2 (STDOUT_FILENO, STDERR_FILENO);

			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = start_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive an UPSTART_JOB_START message containing the job
	 * name.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\02s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB,
				   1000, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_START, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_START, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_START, JOB_STARTING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_START, JOB_STARTING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_START, JOB_PRE_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_START, JOB_PRE_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_START, JOB_SPAWNED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1010);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_START, JOB_SPAWNED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_START, JOB_POST_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1010);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_START, JOB_POST_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_START, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1010);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_START, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_FINISHED,
				   1000, "foo", TRUE, PROCESS_MAIN,
				   SIGSEGV << 8);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 1);

	TEST_FILE_EQ (output, "foo [#1000] (start) waiting\n");
	TEST_FILE_EQ (output, "foo [#1000] (start) starting\n");
	TEST_FILE_EQ (output, "foo [#1000] (start) pre-start\n");
	TEST_FILE_EQ (output, "foo [#1000] (start) spawned, process 1010\n");
	TEST_FILE_EQ (output, ("foo [#1000] (start) post-start, "
			       "(main) process 1010\n"));
	TEST_FILE_EQ (output, "foo [#1000] (start) running, process 1010\n");
	TEST_FILE_EQ (output, ("test: foo [#1000] main process "
			       "killed by SEGV signal\n"));
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	show_ids = FALSE;


	/* Check that --no-wait causes only a message that the job caused has
	 * changed to be output.
	 */
	TEST_FEATURE ("with single job and no wait");
	no_wait = TRUE;

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

	/* Should receive an UPSTART_JOB_START message containing the job
	 * name.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\02s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with; expect anything after the first one to get connection
	 * refused.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB,
				   0xdeadbeef, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START, JOB_WAITING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START, JOB_WAITING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START, JOB_STARTING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START, JOB_STARTING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_PRE_START);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_PRE_START);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START, JOB_SPAWNED);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START, JOB_SPAWNED);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_POST_START);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_POST_START);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START, JOB_RUNNING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START, JOB_RUNNING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_FINISHED,
				   0xdeadbeef, "foo", FALSE, -1, 0);
	maybe_message_send (msg, sock);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo: goal changed\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	no_wait = FALSE;


	/* Check that we can start multiple jobs which should send both start
	 * requests at once and and then receive all of the replies the second
	 * job finishes.
	 */
	TEST_FEATURE ("with multiple jobs");
	cmd.command = "start";
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

	/* Should receive two UPSTART_JOB_START message containing the job
	 * names.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\02s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\02s\0\0\0\3baru\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB,
				   0xdeadbeef, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START, JOB_STARTING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START, JOB_STARTING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB,
				   0xdeafbeef, "bar");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeafbeef, "bar", JOB_START, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeafbeef, "bar", JOB_START, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeafbeef, "bar", JOB_START, JOB_STARTING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeafbeef, "bar", JOB_START, JOB_STARTING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_PRE_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_PRE_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START, JOB_SPAWNED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START, JOB_SPAWNED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_POST_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_POST_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_FINISHED,
				   0xdeadbeef, "foo", FALSE, -1, 0);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeafbeef, "bar", JOB_START,
				   JOB_PRE_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeafbeef, "bar", JOB_START,
				   JOB_PRE_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeafbeef, "bar", JOB_START, JOB_SPAWNED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeafbeef, "bar", JOB_START, JOB_SPAWNED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeafbeef, "bar", JOB_START,
				   JOB_POST_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeafbeef, "bar", JOB_START,
				   JOB_POST_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "bar", JOB_START, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeafbeef, "bar", JOB_START, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_FINISHED,
				   0xdeafbeef, "bar", FALSE, -1, 0);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo (start) waiting\n");
	TEST_FILE_EQ (output, "foo (start) starting\n");
	TEST_FILE_EQ (output, "bar (start) waiting\n");
	TEST_FILE_EQ (output, "bar (start) starting\n");
	TEST_FILE_EQ (output, "foo (start) pre-start\n");
	TEST_FILE_EQ (output, "foo (start) spawned, process 1000\n");
	TEST_FILE_EQ (output, "foo (start) post-start, (main) process 1000\n");
	TEST_FILE_EQ (output, "foo (start) running, process 1000\n");
	TEST_FILE_EQ (output, "bar (start) pre-start\n");
	TEST_FILE_EQ (output, "bar (start) spawned, process 1100\n");
	TEST_FILE_EQ (output, "bar (start) post-start, (main) process 1100\n");
	TEST_FILE_EQ (output, "bar (start) running, process 1100\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that we can start multiple jobs with --no-wait, all of the
	 * messages should be sent in one batch and only the goal changed
	 * messages output, no matter what order replies come in.
	 */
	TEST_FEATURE ("with multiple jobs and no wait");
	no_wait = TRUE;

	cmd.command = "start";
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

	/* Should receive two UPSTART_JOB_START message containing the job
	 * names.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\02s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\02s\0\0\0\3baru\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB,
				   0xdeadbeef, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START, JOB_STARTING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START, JOB_STARTING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB,
				   0xdeafbeef, "bar");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeafbeef, "bar", JOB_START, JOB_WAITING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeafbeef, "bar", JOB_START, JOB_WAITING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeafbeef, "bar", JOB_START, JOB_STARTING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeafbeef, "bar", JOB_START, JOB_STARTING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_PRE_START);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_PRE_START);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START, JOB_SPAWNED);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START, JOB_SPAWNED);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_POST_START);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_POST_START);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START, JOB_RUNNING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START, JOB_RUNNING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_FINISHED,
				   0xdeadbeef, "foo", FALSE, -1, 0);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeafbeef, "bar", JOB_START,
				   JOB_PRE_START);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeafbeef, "bar", JOB_START,
				   JOB_PRE_START);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeafbeef, "bar", JOB_START, JOB_SPAWNED);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeafbeef, "bar", JOB_START, JOB_SPAWNED);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeafbeef, "bar", JOB_START,
				   JOB_POST_START);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeafbeef, "bar", JOB_START,
				   JOB_POST_START);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeafbeef, "bar", JOB_START, JOB_RUNNING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeafbeef, "bar", JOB_START, JOB_RUNNING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_FINISHED,
				   0xdeafbeef, "bar", FALSE, -1, 0);
	maybe_message_send (msg, sock);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo: goal changed\n");
	TEST_FILE_EQ (output, "bar: goal changed\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	no_wait = FALSE;


	/* Check that if we attempt to start a job that hits its respawn
	 * limit, we get an appropriate error message back.
	 */
	TEST_FEATURE ("with job that hits respawn limit");
	cmd.command = "start";
	args[0] = "foo";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			dup2 (STDOUT_FILENO, STDERR_FILENO);

			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = start_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive an UPSTART_JOB_START message containing the job
	 * name.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\02s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with if the job fails to respawn.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB,
				   0xdeadbeef, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_STOP, JOB_STARTING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_STOP, JOB_STARTING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_STOP, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_STOP, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_FINISHED,
				   0xdeadbeef, "foo", TRUE, -1, 0);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 1);

	TEST_FILE_EQ (output, "foo (start) waiting\n");
	TEST_FILE_EQ (output, "foo (stop) starting\n");
	TEST_FILE_EQ (output, "foo (stop) waiting\n");
	TEST_FILE_EQ (output, "test: foo respawning too fast, stopped\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that an unknown job name response results in an error
	 * containing the name that was unknown.
	 */
	TEST_FEATURE ("with unknown job name");
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

	/* Should receive an UPSTART_JOB_START message containing the job
	 * name.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\02s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the job unknown error containing the name */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_UNKNOWN,
				   "foo", 0);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 1);

	TEST_FILE_EQ (output, "test: Unknown job: foo\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that an unknown job id response results in an error
	 * containing the id that was unknown.
	 */
	TEST_FEATURE ("with unknown job id");
	by_id = TRUE;

	cmd.command = "start";
	args[0] = "1000";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDERR (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = start_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive an UPSTART_JOB_START message containing the job
	 * id.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 18);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\02Su\0\0\x3\xe8", 18);

	nih_free (msg);

	/* Send back the job unknown error containing the id */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_UNKNOWN,
				   NULL, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 1);

	TEST_FILE_EQ (output, "test: Unknown job: #1000\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	by_id = FALSE;


	/* Check that an invalid job response results in an error being
	 * output.
	 */
	TEST_FEATURE ("with invalid job");
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

	/* Should receive an UPSTART_JOB_START message containing the job
	 * name.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\02s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the job invalid error */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_INVALID,
				   0xdeafbeef, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 1);

	TEST_FILE_EQ (output, "test: Invalid job: foo\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that an unchanged job response results in a warning being
	 * output, but the exit status being zero.
	 */
	TEST_FEATURE ("with unchanged job");
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

	/* Should receive an UPSTART_JOB_START message containing the job
	 * name.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\02s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the job unchanged reply */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_UNCHANGED,
				   0xdeafbeef, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "test: Job not changed: foo\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that if all arguments are omitted, and there's no environment
	 * variable to fall back on, an error is output.
	 */
	TEST_FEATURE ("with no arguments or environment");
	cmd.command = "start";
	args[0] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDERR (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = start_action (&cmd, args);
			exit (ret);
		}
	}

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 1);

	TEST_FILE_EQ (output, "test: missing job name\n");
	TEST_FILE_EQ (output, "Try `test --help' for more information.\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that when passing jobs by id, a non-numeric id raises an
	 * error.
	 */
	TEST_FEATURE ("with non-numeric job id");
	by_id = TRUE;

	cmd.command = "start";
	args[0] = "abcd";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDERR (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = start_action (&cmd, args);
			exit (ret);
		}
	}

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 1);

	TEST_FILE_EQ (output, "test: invalid job id: abcd\n");
	TEST_FILE_EQ (output, "Try `test --help' for more information.\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	by_id = FALSE;


	/* Check that when passing jobs by id, a partially non-numeric id
	 * raises an error.
	 */
	TEST_FEATURE ("with partially non-numeric job id");
	by_id = TRUE;

	cmd.command = "start";
	args[0] = "999xyz";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDERR (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = start_action (&cmd, args);
			exit (ret);
		}
	}

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 1);

	TEST_FILE_EQ (output, "test: invalid job id: 999xyz\n");
	TEST_FILE_EQ (output, "Try `test --help' for more information.\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	by_id = FALSE;


	fclose (output);

	close (sock);
}

void
test_stop_action (void)
{
	NihCommand    cmd;
	NihIoMessage *msg;
	size_t        len;
	FILE         *output;
	pid_t         pid;
	char         *args[3];
	int           ret, sock, status;

	TEST_FUNCTION ("stop_action");
	program_name = "test";

	output = tmpfile ();

	sock = upstart_open ();
	destination_pid = getpid ();

	nih_error_push_context ();
	nih_error_pop_context ();


	/* Check that the stop command can have a single argument containing
	 * a job name, that is sends a request to stop that job and outputs
	 * status messages received until the job reaches the final goal.
	 */
	TEST_FEATURE ("with single job to stop by name");
	cmd.command = "stop";
	args[0] = "foo";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = stop_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive an UPSTART_JOB_STOP message containing the job
	 * name.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\03s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB,
				   0xdeadbeef, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_STOP, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_STOP, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_PRE_STOP, 1050);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_STOP, JOB_KILLED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_STOP, JOB_KILLED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_STOP, JOB_POST_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_POST_STOP, 1010);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_STOP, JOB_POST_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_STOP, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_STOP, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_FINISHED,
				   0xdeadbeef, "foo", FALSE, -1, 0);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo (stop) running, process 1000\n");
	TEST_FILE_EQ (output, "foo (stop) pre-stop, process 1050\n");
	TEST_FILE_EQ (output, "\tmain process 1000\n");
	TEST_FILE_EQ (output, "foo (stop) stopping, process 1000\n");
	TEST_FILE_EQ (output, "foo (stop) killed, process 1000\n");
	TEST_FILE_EQ (output, "foo (stop) post-stop, process 1010\n");
	TEST_FILE_EQ (output, "foo (stop) waiting\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that the stop command can be called with --by-id and have
	 * a single argument containing a job id, and the request has a NULL
	 * name and the given id.
	 */
	TEST_FEATURE ("with single job to stop by id");
	by_id = TRUE;
	cmd.command = "stop";
	args[0] = "1000";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = stop_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive an UPSTART_JOB_STOP message containing the job
	 * id..
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 18);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\03Su\0\0\x3\xe8", 18);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB,
				   1000, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_KILLED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_KILLED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_POST_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_POST_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_FINISHED,
				   1000, "foo", FALSE, -1, 0);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo (stop) running, process 1000\n");
	TEST_FILE_EQ (output, "foo (stop) pre-stop, (main) process 1000\n");
	TEST_FILE_EQ (output, "foo (stop) stopping, process 1000\n");
	TEST_FILE_EQ (output, "foo (stop) killed, process 1000\n");
	TEST_FILE_EQ (output, "foo (stop) post-stop\n");
	TEST_FILE_EQ (output, "foo (stop) waiting\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	by_id = FALSE;


	/* Check that in the case of no arguments, the UPSTART_JOB_ID
	 * environment variable is used (in preferences to UPSTART_JOB).
	 * this should imply the --no-wait flag, since otherwise it'd wait
	 * for itself.
	 */
	TEST_FEATURE ("with single job to stop from UPSTART_JOB_ID");
	setenv ("UPSTART_JOB_ID", "1000", TRUE);
	setenv ("UPSTART_JOB", "foo", TRUE);

	cmd.command = "stop";
	args[0] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = stop_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive an UPSTART_JOB_STOP message containing the job
	 * id from the environment variable.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 18);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\03Su\0\0\x3\xe8", 18);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB,
				   1000, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_RUNNING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_RUNNING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_PRE_STOP);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_PRE_STOP);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_STOPPING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_STOPPING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_KILLED);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_KILLED);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_POST_STOP);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_POST_STOP);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_WAITING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_WAITING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_FINISHED,
				   1000, "foo", FALSE, -1, 0);
	maybe_message_send (msg, sock);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo: goal changed\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	unsetenv ("UPSTART_JOB_ID");
	unsetenv ("UPSTART_JOB");


	/* Check that if there are no arguments and UPSTART_JOB_ID is not set,
	 * the UPSTART_JOB environment variable is used.  This should imply
	 * the --no-wait flag, since otherwise it'd wait for itself.
	 */
	TEST_FEATURE ("with single job to stop from UPSTART_JOB");
	setenv ("UPSTART_JOB", "foo", TRUE);

	cmd.command = "stop";
	args[0] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = stop_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive an UPSTART_JOB_STOP message containing the job
	 * name from the environment variable.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\03s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB,
				   1000, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_RUNNING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_RUNNING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_PRE_STOP);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_PRE_STOP);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_STOPPING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_STOPPING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_KILLED);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_KILLED);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_POST_STOP);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_POST_STOP);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_WAITING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_WAITING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_FINISHED,
				   1000, "foo", FALSE, -1, 0);
	maybe_message_send (msg, sock);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo: goal changed\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	unsetenv ("UPSTART_JOB");


	/* Check that output includes the job id when show ids is given, also
	 * check that when a job fails while being stopped, the final message
	 * is a* warning about that and the exit status is one.
	 */
	TEST_FEATURE ("with job ids shown and failed job");
	show_ids = TRUE;
	cmd.command = "stop";
	args[0] = "foo";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			dup2 (STDOUT_FILENO, STDERR_FILENO);

			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = stop_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive an UPSTART_JOB_STOP message containing the job
	 * name.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\03s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB,
				   1000, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_KILLED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_KILLED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_POST_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_POST_STOP, 1010);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_POST_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_FINISHED,
				   1000, "foo", TRUE, PROCESS_POST_STOP,
				   2);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 1);

	TEST_FILE_EQ (output, "foo [#1000] (stop) running, process 1000\n");
	TEST_FILE_EQ (output, ("foo [#1000] (stop) pre-stop, "
			       "(main) process 1000\n"));
	TEST_FILE_EQ (output, "foo [#1000] (stop) stopping, process 1000\n");
	TEST_FILE_EQ (output, "foo [#1000] (stop) killed, process 1000\n");
	TEST_FILE_EQ (output, "foo [#1000] (stop) post-stop, process 1010\n");
	TEST_FILE_EQ (output, "foo [#1000] (stop) waiting\n");
	TEST_FILE_EQ (output, ("test: foo [#1000] post-stop process "
			       "terminated with status 2\n"));
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	show_ids = FALSE;


	/* Check that --no-wait causes only a message that the job caused has
	 * changed to be output.
	 */
	TEST_FEATURE ("with single job and no wait");
	no_wait = TRUE;

	cmd.command = "stop";
	args[0] = "foo";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = stop_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive an UPSTART_JOB_STOP message containing the job
	 * name.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\03s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with; expect anything after the first one to get connection
	 * refused.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB,
				   0xdeadbeef, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_STOP, JOB_RUNNING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_STOP, JOB_RUNNING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_STOP, JOB_PRE_STOP);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_STOP, JOB_PRE_STOP);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_STOP, JOB_STOPPING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_STOP, JOB_STOPPING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_STOP, JOB_KILLED);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_STOP, JOB_KILLED);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_STOP, JOB_POST_STOP);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_STOP, JOB_POST_STOP);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_STOP, JOB_WAITING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_STOP, JOB_WAITING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_FINISHED,
				   0xdeadbeef, "foo", FALSE, -1, 0);
	maybe_message_send (msg, sock);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo: goal changed\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	no_wait = FALSE;


	/* Check that we can stop multiple jobs which should send both stop
	 * requests at once and and then receive all of the replies the second
	 * job finishes.
	 */
	TEST_FEATURE ("with multiple jobs");
	cmd.command = "stop";
	args[0] = "foo";
	args[1] = "bar";
	args[2] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = stop_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive two UPSTART_JOB_STOP message containing the job
	 * names.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\03s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\03s\0\0\0\3baru\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB,
				   0xdeadbeef, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_STOP, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_STOP, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB,
				   0xdeadbeef, "bar");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeafbeef, "bar", JOB_STOP, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeafbeef, "bar", JOB_STOP, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeafbeef, "bar", JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeafbeef, "bar", JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeafbeef, "bar", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeafbeef, "bar", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_STOP, JOB_KILLED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_STOP, JOB_KILLED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeafbeef, "bar", JOB_STOP, JOB_KILLED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeafbeef, "bar", JOB_STOP, JOB_KILLED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_STOP, JOB_POST_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_STOP, JOB_POST_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_STOP, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_STOP, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_FINISHED,
				   0xdeadbeef, "foo", FALSE, -1, 0);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeafbeef, "bar", JOB_STOP, JOB_POST_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeafbeef, "bar", JOB_STOP, JOB_POST_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeafbeef, "bar", JOB_STOP, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeafbeef, "bar", JOB_STOP, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_FINISHED,
				   0xdeafbeef, "bar", FALSE, -1, 0);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo (stop) running, process 1000\n");
	TEST_FILE_EQ (output, "foo (stop) pre-stop, (main) process 1000\n");
	TEST_FILE_EQ (output, "foo (stop) stopping, process 1000\n");
	TEST_FILE_EQ (output, "bar (stop) running, process 1100\n");
	TEST_FILE_EQ (output, "bar (stop) pre-stop, (main) process 1100\n");
	TEST_FILE_EQ (output, "bar (stop) stopping, process 1100\n");
	TEST_FILE_EQ (output, "foo (stop) killed, process 1000\n");
	TEST_FILE_EQ (output, "bar (stop) killed, process 1100\n");
	TEST_FILE_EQ (output, "foo (stop) post-stop\n");
	TEST_FILE_EQ (output, "foo (stop) waiting\n");
	TEST_FILE_EQ (output, "bar (stop) post-stop\n");
	TEST_FILE_EQ (output, "bar (stop) waiting\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that we can stop multiple jobs with --no-wait, all of the
	 * messages should be sent in one batch and only the goal changed
	 * messages output, no matter what order replies come in.
	 */
	TEST_FEATURE ("with multiple jobs and no wait");
	no_wait = TRUE;

	cmd.command = "stop";
	args[0] = "foo";
	args[1] = "bar";
	args[2] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = stop_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive two UPSTART_JOB_STOP message containing the job
	 * names.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\03s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\03s\0\0\0\3baru\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB,
				   0xdeadbeef, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_STOP, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_STOP, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB,
				   0xdeadbeef, "bar");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeafbeef, "bar", JOB_STOP, JOB_RUNNING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeafbeef, "bar", JOB_STOP, JOB_RUNNING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeafbeef, "bar", JOB_STOP, JOB_PRE_STOP);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeafbeef, "bar", JOB_STOP, JOB_PRE_STOP);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeafbeef, "bar", JOB_STOP, JOB_STOPPING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeafbeef, "bar", JOB_STOP, JOB_STOPPING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_STOP, JOB_KILLED);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_STOP, JOB_KILLED);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeafbeef, "bar", JOB_STOP, JOB_KILLED);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeafbeef, "bar", JOB_STOP, JOB_KILLED);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_STOP, JOB_POST_STOP);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_STOP, JOB_POST_STOP);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_STOP, JOB_WAITING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_STOP, JOB_WAITING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_FINISHED,
				   0xdeadbeef, "foo", FALSE, -1, 0);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeafbeef, "bar", JOB_STOP, JOB_POST_STOP);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeafbeef, "bar", JOB_STOP, JOB_POST_STOP);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeafbeef, "bar", JOB_STOP, JOB_WAITING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeafbeef, "bar", JOB_STOP, JOB_WAITING);
	maybe_message_send (msg, sock);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_FINISHED,
				   0xdeafbeef, "bar", FALSE, -1, 0);
	maybe_message_send (msg, sock);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo: goal changed\n");
	TEST_FILE_EQ (output, "bar: goal changed\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	no_wait = FALSE;


	/* Check that if stopping one job actually changes multiple (because
	 * it's an instance) we wait for all of the jobs to finish.
	 */
	TEST_FEATURE ("with instance jobs");
	show_ids = TRUE;

	cmd.command = "stop";
	args[0] = "foo";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = stop_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive a single UPSTART_JOB_STOP message containing the job
	 * name.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\03s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB,
				   1000, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB,
				   2000, "bar");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   2000, "bar", JOB_STOP, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   2000, "bar", JOB_STOP, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   2000, "bar", JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   2000, "bar", JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   2000, "bar", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   2000, "bar", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_KILLED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_KILLED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   2000, "bar", JOB_STOP, JOB_KILLED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   2000, "bar", JOB_STOP, JOB_KILLED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_POST_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_POST_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_FINISHED,
				   1000, "foo", FALSE, -1, 0);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   2000, "bar", JOB_STOP, JOB_POST_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   2000, "bar", JOB_STOP, JOB_POST_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   2000, "bar", JOB_STOP, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   2000, "bar", JOB_STOP, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_FINISHED,
				   2000, "bar", FALSE, -1, 0);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo [#1000] (stop) running, process 1000\n");
	TEST_FILE_EQ (output, ("foo [#1000] (stop) pre-stop, "
			       "(main) process 1000\n"));
	TEST_FILE_EQ (output, "foo [#1000] (stop) stopping, process 1000\n");
	TEST_FILE_EQ (output, "bar [#2000] (stop) running, process 1100\n");
	TEST_FILE_EQ (output, ("bar [#2000] (stop) pre-stop, "
			       "(main) process 1100\n"));
	TEST_FILE_EQ (output, "bar [#2000] (stop) stopping, process 1100\n");
	TEST_FILE_EQ (output, "foo [#1000] (stop) killed, process 1000\n");
	TEST_FILE_EQ (output, "bar [#2000] (stop) killed, process 1100\n");
	TEST_FILE_EQ (output, "foo [#1000] (stop) post-stop\n");
	TEST_FILE_EQ (output, "foo [#1000] (stop) waiting\n");
	TEST_FILE_EQ (output, "bar [#2000] (stop) post-stop\n");
	TEST_FILE_EQ (output, "bar [#2000] (stop) waiting\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	show_ids = FALSE;


	/* Check that an unknown job name response results in an error
	 * containing the name that was unknown.
	 */
	TEST_FEATURE ("with unknown job name");
	cmd.command = "stop";
	args[0] = "foo";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDERR (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = stop_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive an UPSTART_JOB_STOP message containing the job
	 * name.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\03s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the job unknown error containing the name */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_UNKNOWN,
				   "foo", 0);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 1);

	TEST_FILE_EQ (output, "test: Unknown job: foo\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that an unknown job id response results in an error
	 * containing the id that was unknown.
	 */
	TEST_FEATURE ("with unknown job id");
	by_id = TRUE;

	cmd.command = "stop";
	args[0] = "1000";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDERR (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = stop_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive an UPSTART_JOB_STOP message containing the job
	 * id.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 18);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\03Su\0\0\x3\xe8", 18);

	nih_free (msg);

	/* Send back the job unknown error containing the id */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_UNKNOWN,
				   NULL, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 1);

	TEST_FILE_EQ (output, "test: Unknown job: #1000\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	by_id = FALSE;


	/* Check that an invalid job response results in an error being
	 * output.
	 */
	TEST_FEATURE ("with invalid job");
	cmd.command = "stop";
	args[0] = "foo";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDERR (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = stop_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive an UPSTART_JOB_STOP message containing the job
	 * name.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\03s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the job invalid error */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_INVALID,
				   0xdeafbeef, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 1);

	TEST_FILE_EQ (output, "test: Invalid job: foo\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that an unchanged job response results in a warning being
	 * output, but the exit status being zero.
	 */
	TEST_FEATURE ("with unchanged job");
	cmd.command = "stop";
	args[0] = "foo";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDERR (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = stop_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive an UPSTART_JOB_STOP message containing the job
	 * name.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\03s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the job unchanged reply */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_UNCHANGED,
				   0xdeafbeef, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "test: Job not changed: foo\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that if all arguments are omitted, and there's no environment
	 * variable to fall back on, an error is output.
	 */
	TEST_FEATURE ("with no arguments or environment");
	cmd.command = "stop";
	args[0] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDERR (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = stop_action (&cmd, args);
			exit (ret);
		}
	}

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 1);

	TEST_FILE_EQ (output, "test: missing job name\n");
	TEST_FILE_EQ (output, "Try `test --help' for more information.\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that when passing jobs by id, a non-numeric id raises an
	 * error.
	 */
	TEST_FEATURE ("with non-numeric job id");
	by_id = TRUE;

	cmd.command = "stop";
	args[0] = "abcd";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDERR (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = stop_action (&cmd, args);
			exit (ret);
		}
	}

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 1);

	TEST_FILE_EQ (output, "test: invalid job id: abcd\n");
	TEST_FILE_EQ (output, "Try `test --help' for more information.\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	by_id = FALSE;


	/* Check that when passing jobs by id, a partially non-numeric id
	 * raises an error.
	 */
	TEST_FEATURE ("with partially non-numeric job id");
	by_id = TRUE;

	cmd.command = "stop";
	args[0] = "999xyz";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDERR (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = stop_action (&cmd, args);
			exit (ret);
		}
	}

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 1);

	TEST_FILE_EQ (output, "test: invalid job id: 999xyz\n");
	TEST_FILE_EQ (output, "Try `test --help' for more information.\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	by_id = FALSE;


	fclose (output);

	close (sock);
}

void
test_status_action (void)
{
	NihCommand    cmd;
	NihIoMessage *msg;
	size_t        len;
	FILE         *output;
	pid_t         pid;
	char         *args[3];
	int           ret, sock, status;

	TEST_FUNCTION ("status_action");
	program_name = "test";

	output = tmpfile ();

	sock = upstart_open ();
	destination_pid = getpid ();

	nih_error_push_context ();
	nih_error_pop_context ();


	/* Check that the status command can have a single argument containing
	 * a job name, that it sends a request to query that job and outputs
	 * the single status message received.
	 */
	TEST_FEATURE ("with single job to query by name");
	cmd.command = "status";
	args[0] = "foo";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = status_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive an UPSTART_JOB_QUERY message containing the job
	 * name.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\01s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_POST_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_POST_START, 1050);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_POST_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo (start) post-start, process 1050\n");
	TEST_FILE_EQ (output, "\tmain process 1000\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that the status command can be called with --by-id and have
	 * a single argument containing a job id, and the request has a NULL
	 * name and the given id.
	 */
	TEST_FEATURE ("with single job to query by id");
	by_id = TRUE;
	cmd.command = "status";
	args[0] = "1000";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = status_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive an UPSTART_JOB_QUERY message containing the job
	 * id..
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 18);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\01Su\0\0\x3\xe8", 18);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_START, JOB_PRE_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_START, JOB_PRE_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo (start) pre-start\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	by_id = FALSE;


	/* Check that output includes the job id when show ids is given.
	 */
	TEST_FEATURE ("with job ids shown");
	show_ids = TRUE;
	cmd.command = "status";
	args[0] = "foo";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			dup2 (STDOUT_FILENO, STDERR_FILENO);

			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = status_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive an UPSTART_JOB_QUERY message containing the job
	 * name.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\01s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "foo", JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "foo", JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, ("foo [#1000] (stop) pre-stop, "
			       "(main) process 1000\n"));
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	show_ids = FALSE;


	/* Check that we can query the status of multiple jobs which should
	 * send both query requests at once and and then receive both of the
	 * replies.
	 */
	TEST_FEATURE ("with multiple jobs");
	cmd.command = "status";
	args[0] = "foo";
	args[1] = "bar";
	args[2] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = status_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive two UPSTART_JOB_QUERY message containing the job
	 * names.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\01s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\01s\0\0\0\3baru\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_PRE_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_PRE_START, 1010);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_PRE_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeafbeef, "bar", JOB_START, JOB_SPAWNED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeafbeef, "bar", JOB_START, JOB_SPAWNED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo (start) pre-start, process 1010\n");
	TEST_FILE_EQ (output, "bar (start) spawned, process 1100\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that we can query the status of an instance job, and have
	 * the status of each instance returned underneath the main one.
	 */
	TEST_FEATURE ("with instance job");
	cmd.command = "status";
	args[0] = "foo";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = status_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive an UPSTART_JOB_QUERY message containing the job
	 * name.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\01s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_INSTANCE,
				   0xdeaf0000, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeaf1000, "foo", JOB_START,
				   JOB_POST_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_POST_START, 1050);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeaf1000, "foo", JOB_START,
				   JOB_POST_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeaf1100, "foo", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeaf1100, "foo", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_INSTANCE_END,
				   0xdeaf0000, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo (instance)\n");
	TEST_FILE_EQ (output, "    (start) post-start, process 1050\n");
	TEST_FILE_EQ (output, "\tmain process 1000\n");
	TEST_FILE_EQ (output, "    (stop) stopping, process 1100\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that ids of instances of the job are also shown when
	 * show-ids is set.
	 */
	TEST_FEATURE ("with instance job and show ids");
	show_ids = TRUE;

	cmd.command = "status";
	args[0] = "foo";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = status_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive an UPSTART_JOB_QUERY message containing the job
	 * name.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\01s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_INSTANCE,
				   1000, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   2000, "foo", JOB_START,
				   JOB_POST_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_POST_START, 1050);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   2000, "foo", JOB_START,
				   JOB_POST_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   3000, "foo", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   3000, "foo", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_INSTANCE_END,
				   1000, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo [#1000] (instance)\n");
	TEST_FILE_EQ (output, ("    [#2000] (start) post-start, "
			       "process 1050\n"));
	TEST_FILE_EQ (output, "\tmain process 1000\n");
	TEST_FILE_EQ (output, "    [#3000] (stop) stopping, process 1100\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	show_ids = FALSE;


	/* Check that the right information is output if we receive both
	 * instance and ordinary jobs in the reply.
	 */
	TEST_FEATURE ("with multiple mixed jobs");
	cmd.command = "status";
	args[0] = "foo";
	args[1] = "bar";
	args[2] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = status_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive two UPSTART_JOB_QUERY message containing the job
	 * names.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\01s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\01s\0\0\0\3baru\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_INSTANCE,
				   0xdeaf0000, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeaf1000, "foo", JOB_START,
				   JOB_POST_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_POST_START, 1050);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeaf1000, "foo", JOB_START,
				   JOB_POST_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeaf1100, "foo", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeaf1100, "foo", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_INSTANCE_END,
				   0xdeaf0000, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeafbeef, "bar", JOB_START, JOB_SPAWNED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeafbeef, "bar", JOB_START, JOB_SPAWNED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo (instance)\n");
	TEST_FILE_EQ (output, "    (start) post-start, process 1050\n");
	TEST_FILE_EQ (output, "\tmain process 1000\n");
	TEST_FILE_EQ (output, "    (stop) stopping, process 1100\n");
	TEST_FILE_EQ (output, "bar (start) spawned, process 1100\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that an unknown job name response results in an error
	 * containing the name that was unknown.
	 */
	TEST_FEATURE ("with unknown job name");
	cmd.command = "status";
	args[0] = "foo";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDERR (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = status_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive an UPSTART_JOB_QUERY message containing the job
	 * name.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\01s\0\0\0\3foou\0\0\0\0", 25);

	nih_free (msg);

	/* Send back the job unknown error containing the name */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_UNKNOWN,
				   "foo", 0);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 1);

	TEST_FILE_EQ (output, "test: Unknown job: foo\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that an unknown job id response results in an error
	 * containing the id that was unknown.
	 */
	TEST_FEATURE ("with unknown job id");
	by_id = TRUE;

	cmd.command = "status";
	args[0] = "1000";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDERR (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = status_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive an UPSTART_JOB_QUERY message containing the job
	 * id.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 18);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\01Su\0\0\x3\xe8", 18);

	nih_free (msg);

	/* Send back the job unknown error containing the id */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_UNKNOWN,
				   NULL, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 1);

	TEST_FILE_EQ (output, "test: Unknown job: #1000\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	by_id = FALSE;


	/* Check that if all arguments are omitted, an error is output.
	 */
	TEST_FEATURE ("with no arguments");
	cmd.command = "status";
	args[0] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDERR (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = status_action (&cmd, args);
			exit (ret);
		}
	}

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 1);

	TEST_FILE_EQ (output, "test: missing job name\n");
	TEST_FILE_EQ (output, "Try `test --help' for more information.\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that when passing jobs by id, a non-numeric id raises an
	 * error.
	 */
	TEST_FEATURE ("with non-numeric job id");
	by_id = TRUE;

	cmd.command = "status";
	args[0] = "abcd";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDERR (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = status_action (&cmd, args);
			exit (ret);
		}
	}

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 1);

	TEST_FILE_EQ (output, "test: invalid job id: abcd\n");
	TEST_FILE_EQ (output, "Try `test --help' for more information.\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	by_id = FALSE;


	/* Check that when passing jobs by id, a partially non-numeric id
	 * raises an error.
	 */
	TEST_FEATURE ("with partially non-numeric job id");
	by_id = TRUE;

	cmd.command = "status";
	args[0] = "999xyz";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDERR (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = status_action (&cmd, args);
			exit (ret);
		}
	}

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 1);

	TEST_FILE_EQ (output, "test: invalid job id: 999xyz\n");
	TEST_FILE_EQ (output, "Try `test --help' for more information.\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	by_id = FALSE;


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

	TEST_FUNCTION ("list_action");
	program_name = "test";

	output = tmpfile ();

	sock = upstart_open ();
	destination_pid = getpid ();

	nih_error_push_context ();
	nih_error_pop_context ();


	/* Check that we can request a list without a pattern, that it sends
	 * a find request with NULL for the pattern, receives all responses
	 * and sorts them before outputting.
	 */
	TEST_FEATURE ("without pattern");
	cmd.command = "list";
	args[0] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = list_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive an UPSTART_JOB_FIND message containing NULL
	 * for the pattern.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 13);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\x00S", 13);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_LIST, NULL);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "frodo", JOB_START,
				   JOB_POST_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_POST_START, 1050);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "frodo", JOB_START,
				   JOB_POST_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_INSTANCE,
				   0xdeaf0000, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeaf1000, "foo", JOB_START,
				   JOB_POST_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_POST_START, 1050);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeaf1000, "foo", JOB_START,
				   JOB_POST_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeaf1100, "foo", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeaf1100, "foo", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_INSTANCE_END,
				   0xdeaf0000, "foo");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "fwap", JOB_START, JOB_PRE_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "fwap", JOB_START, JOB_PRE_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_LIST_END, NULL);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "foo (instance)\n");
	TEST_FILE_EQ (output, "    (start) post-start, process 1050\n");
	TEST_FILE_EQ (output, "\tmain process 1000\n");
	TEST_FILE_EQ (output, "    (stop) stopping, process 1100\n");
	TEST_FILE_EQ (output, "frodo (start) post-start, process 1050\n");
	TEST_FILE_EQ (output, "\tmain process 1000\n");
	TEST_FILE_EQ (output, "fwap (start) pre-start\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that we can request a list with a pattern, and that we handle
	 * an empty list being returned as a result.
	 */
	TEST_FEATURE ("with pattern and no matches");
	cmd.command = "list";
	args[0] = "b*";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			dup2 (STDOUT_FILENO, STDERR_FILENO);

			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = list_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive an UPSTART_JOB_FIND message containing the pattern.
	 */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 19);
	TEST_EQ_MEM (msg->data->buf,
		     "upstart\n\0\0\x01\x00s\0\0\0\2b*", 19);

	nih_free (msg);

	/* Send back the typical sequences of messages that upstart would
	 * reply with.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_LIST, "b*");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_LIST_END, "b*");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 1);

	TEST_FILE_EQ (output, "test: No jobs matching `b*'\n");
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

	msg = upstart_message_new (NULL, pid, UPSTART_EVENT_CAUSED,
				   0xdeafbeef);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_EVENT_CAUSED,
				   0xdeafbeef);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START, JOB_STARTING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START, JOB_STARTING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_EVENT_CAUSED,
				   0xdeafbeef);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_PRE_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_PRE_START, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_PRE_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_EVENT_CAUSED,
				   0xdeafbeef);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START, JOB_SPAWNED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1010);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START, JOB_SPAWNED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_EVENT_CAUSED,
				   0xdeafbeef);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_POST_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1010);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_POST_START, 1050);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_POST_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_EVENT_CAUSED,
				   0xdeafbeef);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1010);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_EVENT_CAUSED,
				   0xdeafbeef);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "bar", JOB_STOP, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "bar", JOB_STOP, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_EVENT_CAUSED,
				   0xdeafbeef);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "bar", JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "bar", JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_EVENT_CAUSED,
				   0xdeafbeef);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "bar", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "bar", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_EVENT_CAUSED,
				   0xdeafbeef);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   2000, "bar", JOB_STOP, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   2000, "bar", JOB_STOP, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_EVENT_CAUSED,
				   0xdeafbeef);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   2000, "bar", JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   2000, "bar", JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_EVENT_CAUSED,
				   0xdeafbeef);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   2000, "bar", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   2000, "bar", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_EVENT_CAUSED,
				   0xdeafbeef);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "bar", JOB_STOP, JOB_KILLED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "bar", JOB_STOP, JOB_KILLED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_EVENT_CAUSED,
				   0xdeafbeef);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   2000, "bar", JOB_STOP, JOB_KILLED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   2000, "bar", JOB_STOP, JOB_KILLED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_EVENT_CAUSED,
				   0xdeafbeef);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "bar", JOB_STOP, JOB_POST_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "bar", JOB_STOP, JOB_POST_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_EVENT_CAUSED,
				   0xdeafbeef);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "bar", JOB_STOP, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "bar", JOB_STOP, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_EVENT_CAUSED,
				   0xdeafbeef);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   2000, "bar", JOB_STOP, JOB_POST_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   2000, "bar", JOB_STOP, JOB_POST_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_EVENT_CAUSED,
				   0xdeafbeef);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   2000, "bar", JOB_STOP, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   2000, "bar", JOB_STOP, JOB_WAITING);
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
	TEST_FILE_EQ (output, "foo (start) waiting\n");
	TEST_FILE_EQ (output, "foo (start) starting\n");
	TEST_FILE_EQ (output, "foo (start) pre-start, process 1000\n");
	TEST_FILE_EQ (output, "foo (start) spawned, process 1010\n");
	TEST_FILE_EQ (output, "foo (start) post-start, process 1050\n");
	TEST_FILE_EQ (output, "\tmain process 1010\n");
	TEST_FILE_EQ (output, "foo (start) running, process 1010\n");
	TEST_FILE_EQ (output, "bar (stop) running, process 1000\n");
	TEST_FILE_EQ (output, "bar (stop) pre-stop, (main) process 1000\n");
	TEST_FILE_EQ (output, "bar (stop) stopping, process 1000\n");
	TEST_FILE_EQ (output, "bar (stop) running, process 1100\n");
	TEST_FILE_EQ (output, "bar (stop) pre-stop, (main) process 1100\n");
	TEST_FILE_EQ (output, "bar (stop) stopping, process 1100\n");
	TEST_FILE_EQ (output, "bar (stop) killed, process 1000\n");
	TEST_FILE_EQ (output, "bar (stop) killed, process 1100\n");
	TEST_FILE_EQ (output, "bar (stop) post-stop\n");
	TEST_FILE_EQ (output, "bar (stop) waiting\n");
	TEST_FILE_EQ (output, "bar (stop) post-stop\n");
	TEST_FILE_EQ (output, "bar (stop) waiting\n");
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
			dup2 (STDOUT_FILENO, STDERR_FILENO);

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

			nih_free (emit_env);
			emit_env = NULL;

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
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that calling emit without any argument results in an error
	 * being sent to stderr.
	 */
	TEST_FEATURE ("with missing argument");
	args[0] = NULL;
	TEST_CHILD (pid) {
		TEST_DIVERT_STDERR (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = emit_action (&cmd, args);
			exit (ret);
		}
	}

	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 1);

	TEST_FILE_EQ (output, "test: missing event name\n");
	TEST_FILE_EQ (output, "Try `test --help' for more information.\n");
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

	/* Should receive UPSTART_SUBSCRIBE_JOBS */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 12);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\x10\x00", 12);

	nih_free (msg);

	/* Send back a couple of jobs */
	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START, JOB_STARTING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START, JOB_STARTING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_PRE_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_PRE_START, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_PRE_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START, JOB_SPAWNED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1010);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START, JOB_SPAWNED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_POST_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1010);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_POST_START, 1050);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START,
				   JOB_POST_START);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   0xdeadbeef, "foo", JOB_START, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1010);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   0xdeadbeef, "foo", JOB_START, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "bar", JOB_STOP, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "bar", JOB_STOP, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "bar", JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "bar", JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "bar", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "bar", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   2000, "bar", JOB_STOP, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   2000, "bar", JOB_STOP, JOB_RUNNING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   2000, "bar", JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   2000, "bar", JOB_STOP, JOB_PRE_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   2000, "bar", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   2000, "bar", JOB_STOP, JOB_STOPPING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "bar", JOB_STOP, JOB_KILLED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1000);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "bar", JOB_STOP, JOB_KILLED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   2000, "bar", JOB_STOP, JOB_KILLED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_PROCESS,
				   PROCESS_MAIN, 1100);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   2000, "bar", JOB_STOP, JOB_KILLED);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "bar", JOB_STOP, JOB_POST_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "bar", JOB_STOP, JOB_POST_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   1000, "bar", JOB_STOP, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   1000, "bar", JOB_STOP, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   2000, "bar", JOB_STOP, JOB_POST_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   2000, "bar", JOB_STOP, JOB_POST_STOP);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS,
				   2000, "bar", JOB_STOP, JOB_WAITING);
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	msg = upstart_message_new (NULL, pid, UPSTART_JOB_STATUS_END,
				   2000, "bar", JOB_STOP, JOB_WAITING);
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

	TEST_FILE_EQ (output, "foo (start) waiting\n");
	TEST_FILE_EQ (output, "foo (start) starting\n");
	TEST_FILE_EQ (output, "foo (start) pre-start, process 1000\n");
	TEST_FILE_EQ (output, "foo (start) spawned, process 1010\n");
	TEST_FILE_EQ (output, "foo (start) post-start, process 1050\n");
	TEST_FILE_EQ (output, "\tmain process 1010\n");
	TEST_FILE_EQ (output, "foo (start) running, process 1010\n");
	TEST_FILE_EQ (output, "bar (stop) running, process 1000\n");
	TEST_FILE_EQ (output, "bar (stop) pre-stop, (main) process 1000\n");
	TEST_FILE_EQ (output, "bar (stop) stopping, process 1000\n");
	TEST_FILE_EQ (output, "bar (stop) running, process 1100\n");
	TEST_FILE_EQ (output, "bar (stop) pre-stop, (main) process 1100\n");
	TEST_FILE_EQ (output, "bar (stop) stopping, process 1100\n");
	TEST_FILE_EQ (output, "bar (stop) killed, process 1000\n");
	TEST_FILE_EQ (output, "bar (stop) killed, process 1100\n");
	TEST_FILE_EQ (output, "bar (stop) post-stop\n");
	TEST_FILE_EQ (output, "bar (stop) waiting\n");
	TEST_FILE_EQ (output, "bar (stop) post-stop\n");
	TEST_FILE_EQ (output, "bar (stop) waiting\n");
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
	args[0] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			signal (SIGTERM, term_handler);
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = events_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive UPSTART_SUBSCRIBE_EVENTS */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 12);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\x10\x10", 12);

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
	nih_free (argv);
	nih_free (env);


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
test_version_action (void)
{
	NihCommand    cmd;
	NihIoMessage *msg;
	size_t        len;
	FILE         *output;
	pid_t         pid;
	char         *args[3];
	int           ret, sock, status;


	/* Check that the version command sends the version query command to
	 * the server, and then prints the result received.
	 */
	TEST_FUNCTION ("version_action");
	program_name = "test";

	nih_error_push_context ();
	nih_error_pop_context ();

	output = tmpfile ();

	sock = upstart_open ();
	destination_pid = getpid ();

	cmd.command = "version";
	args[0] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = version_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive UPSTART_VERSION_QUERY */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 12);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\x00\x01", 12);

	nih_free (msg);

	/* Send back the typical message that upstart would reply with.
	 */
	msg = upstart_message_new (NULL, pid, UPSTART_VERSION,
				   "upstart 0.5.0");
	assert (nih_io_message_send (msg, sock) > 0);
	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_EQ (output, "upstart 0.5.0\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	fclose (output);

	close (sock);
}

void
test_log_priority_action (void)
{
	NihCommand    cmd;
	NihIoMessage *msg;
	size_t        len;
	FILE         *output;
	pid_t         pid;
	char         *args[3];
	int           ret, sock, status;

	TEST_FUNCTION ("log_priority_action");
	program_name = "test";

	nih_error_push_context ();
	nih_error_pop_context ();

	output = tmpfile ();

	sock = upstart_open ();
	destination_pid = getpid ();


	/* Check that the log-priority command accepts "debug" as an argument,
	 * sends the command to the server, and then exits.
	 */
	cmd.command = "log-priority";
	args[0] = "debug";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = log_priority_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive UPSTART_LOG_PRIORITY with the right level. */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 17);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\x00\x02u\0\0\0\x1", 17);

	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that the log-priority command accepts "info" as an argument,
	 * sends the command to the server, and then exits.
	 */
	cmd.command = "log-priority";
	args[0] = "info";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = log_priority_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive UPSTART_LOG_PRIORITY with the right level. */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 17);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\x00\x02u\0\0\0\x2", 17);

	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that the log-priority command accepts "message" as an
	 * argument, sends the command to the server, and then exits.
	 */
	cmd.command = "log-priority";
	args[0] = "message";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = log_priority_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive UPSTART_LOG_PRIORITY with the right level. */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 17);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\x00\x02u\0\0\0\x3", 17);

	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that the log-priority command accepts "warn" as an argument,
	 * sends the command to the server, and then exits.
	 */
	cmd.command = "log-priority";
	args[0] = "warn";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = log_priority_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive UPSTART_LOG_PRIORITY with the right level. */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 17);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\x00\x02u\0\0\0\x4", 17);

	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that the log-priority command accepts "error" as an argument,
	 * sends the command to the server, and then exits.
	 */
	cmd.command = "log-priority";
	args[0] = "error";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = log_priority_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive UPSTART_LOG_PRIORITY with the right level. */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 17);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\x00\x02u\0\0\0\x5", 17);

	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that the log-priority command accepts "fatal" as an argument,
	 * sends the command to the server, and then exits.
	 */
	cmd.command = "log-priority";
	args[0] = "fatal";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDOUT (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = log_priority_action (&cmd, args);
			exit (ret);
		}
	}

	/* Should receive UPSTART_LOG_PRIORITY with the right level. */
	assert (msg = nih_io_message_recv (NULL, sock, &len));

	TEST_EQ (msg->data->len, 17);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\x00\x02u\0\0\0\x6", 17);

	nih_free (msg);

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that if the argumet is forgotten an error is output.
	 */
	TEST_FEATURE ("with no argument");
	cmd.command = "log-priority";
	args[0] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDERR (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = log_priority_action (&cmd, args);
			exit (ret);
		}
	}

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 1);

	TEST_FILE_EQ (output, "test: missing priority\n");
	TEST_FILE_EQ (output, "Try `test --help' for more information.\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	/* Check that if the argument isn't a vaid log level an error is
	 * output.
	 */
	TEST_FEATURE ("with unknown argument");
	cmd.command = "log-priority";
	args[0] = "wibble";
	args[1] = NULL;

	TEST_CHILD (pid) {
		TEST_DIVERT_STDERR (output) {
			upstart_disable_safeties = TRUE;

			control_sock = upstart_open ();
			ret = log_priority_action (&cmd, args);
			exit (ret);
		}
	}

	/* Reap the child, check the output */
	waitpid (pid, &status, 0);
	rewind (output);

	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 1);

	TEST_FILE_EQ (output, "test: invalid priority\n");
	TEST_FILE_EQ (output, "Try `test --help' for more information.\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);


	fclose (output);

	close (sock);
}


int
main (int   argc,
      char *argv[])
{
	test_env_option ();
	test_start_action ();
	test_stop_action ();
	test_status_action ();
	test_list_action ();
	test_emit_action ();
	test_jobs_action ();
	test_events_action ();
	test_version_action ();
	test_log_priority_action ();

	return 0;
}
