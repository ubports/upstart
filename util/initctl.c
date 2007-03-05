/* upstart
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

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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


/* Returns values from handle_event_finished */
#define EVENT_FAILED 1
#define EVENT_OK     2


/* Prototypes for option and command functions */
int env_option      (NihOption *option, const char *arg);

int jobs_action     (NihCommand *command, char * const *args);
int events_action   (NihCommand *command, char * const *args);
int start_action    (NihCommand *command, char * const *args);
int list_action     (NihCommand *command, char * const *args);
int emit_action     (NihCommand *command, char * const *args);


/* Prototypes for static functions */
static int do_job                  (NihCommand *command, const char *job);
static int handle_job_status       (void *data, pid_t pid,
				    UpstartMessageType type,
				    const char *name, JobGoal goal,
				    JobState state);
static int handle_job_unknown      (void *data, pid_t pid,
				    UpstartMessageType type,
				    const char *name);
static int handle_job_list_end     (void *data, pid_t pid,
				    UpstartMessageType type);

static int handle_event            (void *data, pid_t pid,
				    UpstartMessageType type, uint32_t id,
				    const char *name, char * const *args,
				    char * const *env);
static int handle_event_finished   (void *data, pid_t pid,
				    UpstartMessageType type, uint32_t id,
				    int failed, const char *name,
				    char * const *args, char * const *env);



/**
 * control_sock:
 *
 * Control socket opened by the main function for communication with the
 * init daemon.
 **/
int control_sock;

/**
 * destination_pid:
 *
 * Process id to send the message to; nearly always the default of 1.
 **/
int destination_pid = 1;

/**
 * emit_env:
 *
 * Environment variables to emit along with the event.
 **/
char **emit_env = NULL;


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
 * handlers:
 *
 * Functions to be called when we receive replies from the server.
 **/
static UpstartMessage handlers[] = {
	{ -1, UPSTART_JOB_STATUS,
	  (UpstartMessageHandler)handle_job_status },
	{ -1, UPSTART_JOB_UNKNOWN,
	  (UpstartMessageHandler)handle_job_unknown },
	{ -1, UPSTART_JOB_LIST_END,
	  (UpstartMessageHandler)handle_job_list_end },
	{ -1, UPSTART_EVENT,
	  (UpstartMessageHandler)handle_event },
	{ -1, UPSTART_EVENT_FINISHED,
	  (UpstartMessageHandler)handle_event_finished },

	UPSTART_MESSAGE_LAST
};

/**
 * jobs_action:
 * @command: command invoked for,
 * @args: arguments passed.
 *
 * Function invoked when the jobs command is run.  No arguments are
 * expected.
 *
 * Returns: zero on success, exit status on error.
 **/
int
jobs_action (NihCommand   *command,
	     char * const *args)
{
	NihIoMessage *message;
	NihError     *err;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	message = upstart_message_new (NULL, destination_pid,
				       UPSTART_WATCH_JOBS);

	/* Send the message */
	if (nih_io_message_send (message, control_sock) < 0)
		goto error;

	nih_free (message);


	/* Receive all replies */
	for (;;) {
		NihIoMessage *reply;
		size_t        len;
		int           ret;

		reply = nih_io_message_recv (NULL, control_sock, &len);
		if (! reply)
			goto error;

		ret = upstart_message_handle (reply, reply, handlers, NULL);
		nih_free (reply);

		if (ret < 0)
			goto error;
	}

	return 0;

error:
	err = nih_error_get ();
	nih_error (_("Communication error: %s"), err->message);
	nih_free (err);

	return 1;
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
 * start_action:
 * @command: command invoked for,
 * @args: arguments passed.
 *
 * Function invoked when the start, stop or status command is run.  The
 * arguments are expected to be a list of jobs that should have their status
 * changed, if no arguments are given then the UPSTART_JOB environment
 * variable is checked instead.
 *
 * Returns: zero on success, exit status on error.
 **/
int
start_action (NihCommand   *command,
	      char * const *args)
{
	NihError     *err;
	char * const *arg;
	char         *this_job;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	this_job = getenv ("UPSTART_JOB");

	if (args[0]) {
		/* Iterate job names */
		for (arg = args; *arg; arg++) {
			if (do_job (command, *arg) < 0)
				goto error;
		}

	} else if (this_job) {
		/* Fallback to current job (from environment). */
		if (do_job (command, this_job) < 0)
			goto error;

	} else {
		fprintf (stderr, _("%s: missing job name\n"), program_name);
		nih_main_suggest_help ();
		return 1;
	}

	return 0;

error:
	err = nih_error_get ();
	nih_error (_("Communication error: %s"), err->message);
	nih_free (err);

	return 1;
}

/**
 * do_job:
 * @command: command invoked for,
 * @job: job to be changed.
 *
 * Either starts, stops or queries the status of @job, depending on
 * @command; sending the message to the server and waiting for the status
 * reply.
 *
 * Returns: zero on success, exit status on error.
 **/
static int
do_job (NihCommand *command,
	const char *job)
{
	NihIoMessage *message;

	nih_assert (command != NULL);
	nih_assert (job != NULL);

	/* Build the message to send */
	if (! strcmp (command->command, "start")) {
		message = upstart_message_new (NULL, destination_pid,
					       UPSTART_JOB_START, job);
	} else if (! strcmp (command->command, "stop")) {
		message = upstart_message_new (NULL, destination_pid,
					       UPSTART_JOB_STOP, job);
	} else if (! strcmp (command->command, "status")) {
		message = upstart_message_new (NULL, destination_pid,
					       UPSTART_JOB_QUERY, job);
	} else {
		nih_assert_not_reached ();
	}

	/* Send the message */
	if (nih_io_message_send (message, control_sock) < 0) {
		nih_free (message);
		return -1;
	}

	nih_free (message);


	/* Handle replies until a handler exits with a non-zero value,
	 * indicating either an error or the list end.
	 */
	for (;;) {
		NihIoMessage *reply;
		size_t        len;
		int           ret;

		reply = nih_io_message_recv (NULL, control_sock, &len);
		if (! reply)
			return -1;

		ret = upstart_message_handle (reply, reply, handlers, NULL);
		nih_free (reply);

		if (ret < 0) {
			return -1;
		} else if (ret > 0) {
			break;
		}
	}

	return 0;
}

/**
 * list_action:
 * @command: command invoked for,
 * @args: arguments passed.
 *
 * Function invoked when the list command is run.  No arguments are permitted.
 *
 * Returns: zero on success, exit status on error.
 **/
int
list_action (NihCommand   *command,
	     char * const *args)
{
	NihIoMessage *message;
	NihError     *err;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	message = upstart_message_new (NULL, destination_pid,
				       UPSTART_JOB_LIST);

	/* Send the message */
	if (nih_io_message_send (message, control_sock) < 0)
		goto error;

	nih_free (message);


	/* Handle replies until a handler exits with a non-zero value,
	 * indicating either an error or the list end.
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
		} else if (ret > 0) {
			break;
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
 * handle_job_status:
 * @data: data pointer,
 * @pid: origin of message,
 * @type: message type,
 * @name: name of job,
 * @goal: current goal,
 * @state: state of job,
 * @pid: process id.
 *
 * Function called on receipt of a message containing the status of a job,
 * either as a result of changing the goal, querying the state or as part
 * of a list of jobs.
 *
 * Builds a single-line string describing the message and outputs it.
 *
 * Returns: zero on success, negative value on error.
 **/
static int handle_job_status (void               *data,
			      pid_t               pid,
			      UpstartMessageType  type,
			      const char         *name,
			      JobGoal             goal,
			      JobState            state)
{
	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB_STATUS);
	nih_assert (name != NULL);

	nih_message (_("%s (%s) %s"), name, job_goal_name (goal),
		     job_state_name (state));

	return 0;
}

/**
 * handle_job_unknown:
 * @data: data pointer,
 * @pid: origin of message,
 * @type: message type,
 * @name: name of job.
 *
 * Function called on receipt of a message alerting us to an unknown job
 * in an attempt to change the goal or query the state.
 *
 * Outputs a warning message containing the job name.
 *
 * Returns: positive value to end loop.
 **/
static int handle_job_unknown  (void               *data,
				pid_t               pid,
				UpstartMessageType  type,
				const char         *name)
{
	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB_UNKNOWN);
	nih_assert (name != NULL);

	nih_warn (_("unknown job: %s"), name);

	return 1;
}

/**
 * handle_job_list_end:
 * @data: data pointer,
 * @pid: origin of message,
 * @type: message type.
 *
 * Function called on receipt of a message indicating the end of a job list.
 *
 * Returns: positive value to end loop.
 **/
static int handle_job_list_end (void               *data,
				pid_t               pid,
				UpstartMessageType  type)
{
	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB_LIST_END);

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


#ifndef TEST
/**
 * options:
 *
 * Command-line options accepted for all arguments.
 **/
static NihOption options[] = {
	{ 'p', "pid", "destination process", NULL, "PID", &destination_pid,
	  nih_option_int },

	NIH_OPTION_LAST
};

/**
 * start_options:
 *
 * Command-line options accepted for the start, stop and status commands.
 **/
NihOption start_options[] = {
	NIH_OPTION_LAST
};

/**
 * list_options:
 *
 * Command-line options accepted for the list command.
 **/
NihOption list_options[] = {
	NIH_OPTION_LAST
};

/**
 * jobs_options:
 *
 * Command-line options accepted for the jobs command.
 **/
NihOption jobs_options[] = {
	NIH_OPTION_LAST
};

/**
 * emit_options:
 *
 * Command-line options accepted for the emit command.
 **/
NihOption emit_options[] = {
	{ 'e', NULL, "set environment variable in jobs changed by this event",
	  NULL, "NAME[=VALUE]", &emit_env, env_option },

	NIH_OPTION_LAST
};

/**
 * events_options:
 *
 * Command-line options accepted for the events command.
 **/
NihOption events_options[] = {
	NIH_OPTION_LAST
};


/**
 * job_group:
 *
 * Group of commands related to jobs.
 **/
static NihCommandGroup job_commands = { N_("Job") };

/**
 * event_group:
 *
 * Group of commands related to events.
 **/
static NihCommandGroup event_commands = { N_("Event") };

/**
 * commands:
 *
 * Commands accepts as the first non-option argument, or program name.
 **/
static NihCommand commands[] = {
	{ "start", N_("JOB..."),
	  N_("Start jobs."),
	  N_("JOB is one or more job names that are to be started."),
	  &job_commands, start_options, start_action },

	{ "stop", N_("JOB..."),
	  N_("Stop jobs."),
	  N_("JOB is one or more job names that are to be stopped."),
	  &job_commands, start_options, start_action },

	{ "status", N_("JOB..."),
	  N_("Query status of jobs."),
	  N_("JOB is one or more job names that are to be queried."),
	  &job_commands, start_options, start_action },

	{ "list", NULL,
	  N_("List known jobs."),
	  NULL,
	  &job_commands, list_options, list_action },

	{ "emit", N_("EVENT [ARG]..."),
	  N_("Emit an event."),
	  N_("EVENT is the name of an event the init daemon should emit, "
	     "which may have zero or more arguments specified by ARG.  These "
	     "may be matched in the job definition, and are passed to any "
	     "scripts run by the job.\n\n"
	     "Events may also pass environment variables to the job scripts, "
	     "defined using -e.  A value may be specified in the option, or "
	     "if omitted, the value is taken from the environment or ignored "
	     "if not present there."),
	  &event_commands, emit_options, emit_action },

	{ "trigger", N_("EVENT"),
	  NULL,
	  NULL,
	  &event_commands, emit_options, emit_action },

	{ "jobs", NULL,
	  N_("Receive notification of job state changes."),
	  NULL,
	  &job_commands, jobs_options, jobs_action },

	{ "events", NULL,
	  N_("Receive notification of emitted events."),
	  NULL,
	  &event_commands, events_options, events_action },

	NIH_COMMAND_LAST
};


int
main (int   argc,
      char *argv[])
{
	int ret;

	nih_main_init (argv[0]);

	/* Check we're root */
	setuid (geteuid ());
	if (getuid ()) {
		nih_error (_("Need to be root"));
		exit (1);
	}

	/* Connect to the daemon */
	control_sock = upstart_open ();
	if (control_sock < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_error (_("Unable to establish control socket: %s"),
			   err->message);
		exit (1);
	}

	ret = nih_command_parser (NULL, argc, argv, options, commands);
	if (ret < 0)
		exit (1);

	return ret;
}
#endif
