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


/* Prototypes for static functions */
static int handle_event (void *data, pid_t pid, UpstartMessageType type,
			 const char *name, char * const *args,
			 char * const *env);


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

	UPSTART_MESSAGE_LAST
};


/**
 * emit_action:
 * @command: command invoked for,
 * @args: arguments passed.
 *
 * Function invoked when the emit or shutdown command is run.  An event name
 * is expected.
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
				       UPSTART_EVENT_QUEUE, args[0],
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
 * shutdown_action:
 * @command: command invoked for,
 * @args: arguments passed.
 *
 * Function invoked when the shutdown command is run.  An event name
 * is expected.
 *
 * Returns: zero on success, exit status on error.
 **/
int
shutdown_action (NihCommand   *command,
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
				       UPSTART_SHUTDOWN, args[0]);
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
