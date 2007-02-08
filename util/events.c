/* upstart
 *
 * events.c - commands dealing with events.
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/io.h>
#include <nih/main.h>
#include <nih/option.h>
#include <nih/command.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <upstart/enum.h>
#include <upstart/message.h>

#include <util/initctl.h>
#include <util/events.h>


/* Returns values from handle_event_finished */
#define EVENT_FAILED 1
#define EVENT_OK     2


/* Prototypes for static functions */
static int handle_event            (void *data, pid_t pid,
				    UpstartMessageType type, uint32_t id,
				    const char *name, char * const *args,
				    char * const *env);
static int handle_event_job_status (void *data, pid_t pid,
				    UpstartMessageType type, uint32_t id,
				    const char *name, JobGoal goal,
				    JobState state, pid_t process);
static int handle_event_finished   (void *data, pid_t pid,
				    UpstartMessageType type, uint32_t id,
				    int failed, const char *name,
				    char * const *args, char * const *env);


/**
 * emit_env:
 *
 * Environment variables to emit along with the event.
 **/
char **emit_env = NULL;


/**
 * handlers:
 *
 * Functions to be called when we receive replies from the server.
 **/
static UpstartMessage handlers[] = {
	{ -1, UPSTART_EVENT,
	  (UpstartMessageHandler)handle_event },
	{ -1, UPSTART_EVENT_JOB_STATUS,
	  (UpstartMessageHandler)handle_event_job_status },
	{ -1, UPSTART_EVENT_FINISHED,
	  (UpstartMessageHandler)handle_event_finished },

	UPSTART_MESSAGE_LAST
};


/**
 * emit_action:
 * @command: command invoked for,
 * @args: arguments passed.
 *
 * Function invoked when the emit command is run.  An event name
 * is expected, followed by optional arguments for the event.  We also use
 * the @emit_env variable, set by -e, to set the environment for the event.
 *
 * This does not return until the event has finished being emitted.
 *
 * Returns: zero on success, exit status on error.
 **/
int
emit_action (NihCommand   *command,
	     char * const *args)
{
	NihIoMessage *message;
	NihError     *err;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	if (! args[0]) {
		fprintf (stderr, _("%s: missing event name\n"), program_name);
		nih_main_suggest_help ();
		return 1;
	}

	message = upstart_message_new (NULL, destination_pid,
				       UPSTART_EVENT_EMIT, args[0],
				       args[1] ? &(args[1]) : NULL, emit_env);
	if (! message) {
		nih_error_raise_system ();
		goto error;
	}

	/* Send the message */
	if (nih_io_message_send (message, control_sock) < 0) {
		nih_free (message);
		goto error;
	}

	nih_free (message);

	/* Receive replies until we get the one indicating that the event
	 * has finished.
	 */
	for (;;) {
		NihIoMessage *reply;
		size_t        len;
		int           ret;

		reply = nih_io_message_recv (NULL, control_sock, &len);
		if (! reply)
			goto error;

		ret = upstart_message_handle (reply, reply, handlers, NULL);
		nih_free (reply);

		if (ret < 0) {
			goto error;
		} else if (ret == EVENT_FAILED) {
			return 1;
		} else if (ret == EVENT_OK) {
			return 0;
		}
	}


	return 0;

error:
	err = nih_error_get ();
	nih_error (_("Communication error: %s"), err->message);
	nih_free (err);

	return 1;
}

/**
 * env_option:
 * @option: NihOption invoked,
 * @arg: argument to parse.
 *
 * This option setter is used to append @arg to the list of environment
 * variables pointed to by the value member of option, which must be a
 * pointer to a char **.
 *
 * If @arg does not contain an '=', the current value from the environment
 * is taken instead.
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

	if (strchr (arg, '=')) {
		NIH_MUST (nih_str_array_add (value, NULL, NULL, arg));
	} else {
		char *env, *new_arg;

		env = getenv (arg);
		if (env) {
			NIH_MUST (new_arg = nih_sprintf (NULL, "%s=%s",
							 arg, env));
			NIH_MUST (nih_str_array_addp (value, NULL, NULL,
						      new_arg));
		}
	}

	return 0;
}


/**
 * events_action:
 * @command: command invoked for,
 * @args: arguments passed.
 *
 * Function invoked when the events command is run.  No arguments are
 * expected.
 *
 * Returns: zero on success, exit status on error.
 **/
int
events_action (NihCommand   *command,
	       char * const *args)
{
	NihIoMessage *message;
	NihError     *err;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	message = upstart_message_new (NULL, destination_pid,
				       UPSTART_WATCH_EVENTS);
	if (! message) {
		nih_error_raise_system ();
		goto error;
	}

	/* Send the message */
	if (nih_io_message_send (message, control_sock) < 0) {
		nih_free (message);
		goto error;
	}

	nih_free (message);


	/* Receive all replies */
	for (;;) {
		NihIoMessage *reply;
		size_t        len;

		reply = nih_io_message_recv (NULL, control_sock, &len);
		if (! reply)
			goto error;

		if (upstart_message_handle (reply, reply,
					    handlers, NULL) < 0) {
			nih_free (reply);
			goto error;
		}

		nih_free (reply);
	}

	return 0;

error:
	err = nih_error_get ();
	nih_error (_("Communication error: %s"), err->message);
	nih_free (err);

	return 1;
}


/**
 * handle_event:
 * @data: data pointer,
 * @pid: origin of message,
 * @type: message type,
 * @id: id of event,
 * @name: name of event,
 * @args: arguments to event,
 * @env: environment for event.
 *
 * Function called on receipt of a message notifying us of an event
 * emission.
 *
 * Builds a single-line string describing the event and its arguments,
 * followed by one line for each environment variable.
 *
 * Returns: zero on success, negative value on error.
 **/
static int handle_event (void               *data,
			 pid_t               pid,
			 UpstartMessageType  type,
			 uint32_t            id,
			 const char         *name,
			 char * const *      args,
			 char * const *      env)
{
	char         *msg;
	char * const *ptr;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_EVENT);
	nih_assert (name != NULL);

	NIH_MUST (msg = nih_strdup (NULL, name));
	for (ptr = args; ptr && *ptr; ptr++) {
		char *new_msg;

		NIH_MUST (new_msg = nih_realloc (msg, NULL, (strlen (msg)
							     + strlen (*ptr)
							     + 2)));
		msg = new_msg;
		strcat (msg, " ");
		strcat (msg, *ptr);
	}

	nih_message ("%s", msg);
	nih_free (msg);

	for (ptr = env; ptr && *ptr; ptr++)
		nih_message ("    %s", *ptr);

	return 0;
}

/**
 * handle_event_job_status:
 * @data: data pointer,
 * @pid: origin of message,
 * @type: message type,
 * @id: id of associated event,
 * @name: name of job,
 * @goal: current goal,
 * @state: state of job,
 * @pid: process id.
 *
 * Function called on receipt of a message containing the status of a job
 * changed by an event we've emitted.
 *
 * Builds a single-line string describing the message and outputs it.
 *
 * Returns: zero on success, negative value on error.
 **/
static int handle_event_job_status (void               *data,
				    pid_t               pid,
				    UpstartMessageType  type,
				    uint32_t            id,
				    const char         *name,
				    JobGoal             goal,
				    JobState            state,
				    pid_t               process)
{
	const char *format;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_EVENT_JOB_STATUS);
	nih_assert (name != NULL);

	if (process > 0) {
		format = _("%s (%s) %s, process %d");
	} else {
		format = _("%s (%s) %s");
	}

	nih_message (format, name, job_goal_name (goal),
		     job_state_name (state), process);

	return 0;
}

/**
 * handle_event_finished:
 * @data: data pointer,
 * @pid: origin of message,
 * @type: message type.
 * @id: id of event,
 * @failed: whether the event failed,
 * @name: name of event,
 * @args: arguments to event,
 * @env: environment for event.
 *
 * Function called on receipt of a message indicating that an event we're
 * watching has finished.  @failed indicates whether any job started or
 * stopped by this event failed; we use that to generate a return code.
 *
 * Returns: EVENT_FAILED if @failed is TRUE, otherwise EVENT_OK.
 **/
static int handle_event_finished (void               *data,
				  pid_t               pid,
				  UpstartMessageType  type,
				  uint32_t            id,
				  int                 failed,
				  const char         *name,
				  char * const *      args,
				  char * const *      env)
{
	nih_assert (pid > 0);
	nih_assert (type == UPSTART_EVENT_FINISHED);
	nih_assert (name != NULL);

	if (failed) {
		nih_warn (_("%s event failed"), name);

		return EVENT_FAILED;
	} else {
		return EVENT_OK;
	}
}
