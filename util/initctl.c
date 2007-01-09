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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/main.h>
#include <nih/option.h>
#include <nih/command.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <upstart/job.h>
#include <upstart/message.h>


/* Prototypes for static functions */
static int handle_job_status   (void *data, pid_t pid, UpstartMessageType type,
				const char *name, JobGoal goal, JobState state,
				ProcessState process_state, pid_t process,
				const char *description);
static int handle_job_unknown  (void *data, pid_t pid, UpstartMessageType type,
				const char *name);
static int handle_job_list_end (void *data, pid_t pid,
				UpstartMessageType type);
static int handle_event        (void *data, pid_t pid, UpstartMessageType type,
				const char *name);


/**
 * control_sock:
 *
 * Control socket opened by the main function for communication with the
 * init daemon.
 **/
static int control_sock;


/**
 * handlers:
 *
 * Functions to be called when we receive replies from the server.
 **/
static UpstartMessage handlers[] = {
	{ UPSTART_INIT_DAEMON, UPSTART_JOB_STATUS,
	  (UpstartMessageHandler)handle_job_status },
	{ UPSTART_INIT_DAEMON, UPSTART_JOB_UNKNOWN,
	  (UpstartMessageHandler)handle_job_unknown },
	{ UPSTART_INIT_DAEMON, UPSTART_JOB_LIST_END,
	  (UpstartMessageHandler)handle_job_list_end },
	{ UPSTART_INIT_DAEMON, UPSTART_EVENT,
	  (UpstartMessageHandler)handle_event },

	UPSTART_MESSAGE_LAST
};



/**
 * start_options:
 *
 * Command-line options accepted for the start, stop and status commands.
 **/
static NihOption start_options[] = {
	NIH_OPTION_LAST
};

/**
 * start_action:
 * @command: command invoked for,
 * @args: arguments passed.
 *
 * Function invoked when the start, stop or status command is run.  The
 * arguments are expected to be a list of jobs that should have their status
 * changed.
 *
 * Returns: zero on success, exit status on error.
 **/
static int
start_action (NihCommand   *command,
	      char * const *args)
{
	NihError     *err;
	char * const *arg;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	if (! args[0]) {
		fprintf (stderr, _("%s: missing job name\n"), program_name);
		nih_main_suggest_help ();
		exit (1);
	}

	/* Iterate job names */
	for (arg = args; *arg; arg++) {
		NihIoMessage *message, *reply;
		size_t        len;

		/* Build the message to send */
		if (! strcmp (command->command, "start")) {
			NIH_MUST (message = upstart_message_new (
					  NULL, UPSTART_INIT_DAEMON,
					  UPSTART_JOB_START, *arg));
		} else if (! strcmp (command->command, "stop")) {
			NIH_MUST (message = upstart_message_new (
					  NULL, UPSTART_INIT_DAEMON,
					  UPSTART_JOB_STOP, *arg));
		} else if (! strcmp (command->command, "status")) {
			NIH_MUST (message = upstart_message_new (
					  NULL, UPSTART_INIT_DAEMON,
					  UPSTART_JOB_QUERY, *arg));
		} else {
			nih_assert_not_reached ();
		}

		/* Send the message */
		if (nih_io_message_send (message, control_sock) < 0)
			goto error;

		/* Wait for a single reply */
		reply = nih_io_message_recv (message, control_sock, &len);
		if (! reply)
			goto error;

		if (upstart_message_handle (reply, reply, handlers, NULL) < 0)
			goto error;

		nih_free (message);
	}

	return 0;

error:
	err = nih_error_get ();
	nih_error (_("Communication error: %s"), err->message);
	nih_free (err);

	exit (1);
}


/**
 * list_options:
 *
 * Command-line options accepted for the list command.
 **/
static NihOption list_options[] = {
	NIH_OPTION_LAST
};

/**
 * list_action:
 * @command: command invoked for,
 * @args: arguments passed.
 *
 * Function invoked when the list command is run.  No arguments are permitted.
 *
 * Returns: zero on success, exit status on error.
 **/
static int
list_action (NihCommand   *command,
	     char * const *args)
{
	NihIoMessage *message;
	NihError     *err;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	NIH_MUST (message = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
						 UPSTART_JOB_LIST));

	/* Send the message */
	if (nih_io_message_send (message, control_sock) < 0)
		goto error;

	/* Handle replies until a handler exits with a non-zero value,
	 * indicating either an error or the list end.
	 */
	for (;;) {
		NihIoMessage *reply;
		size_t        len;
		int           ret;

		reply = nih_io_message_recv (message, control_sock, &len);
		if (! reply)
			goto error;

		ret = upstart_message_handle (reply, reply, handlers, NULL);
		if (ret < 0) {
			goto error;
		} else if (ret > 0) {
			break;
		}

		nih_free (reply);
	}

	nih_free (message);

	return 0;

error:
	err = nih_error_get ();
	nih_error (_("Communication error: %s"), err->message);
	nih_free (err);

	exit (1);
}


/**
 * emit_options:
 *
 * Command-line options accepted for the emit and shutdown commands.
 **/
static NihOption emit_options[] = {
	NIH_OPTION_LAST
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
static int
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
		exit (1);
	}

	if (! strcmp (command->command, "emit")) {
		NIH_MUST (message = upstart_message_new (
				  NULL, UPSTART_INIT_DAEMON,
				  UPSTART_EVENT_QUEUE, args[0]));
	} else if (! strcmp (command->command, "trigger")) {
		NIH_MUST (message = upstart_message_new (
				  NULL, UPSTART_INIT_DAEMON,
				  UPSTART_EVENT_QUEUE, args[0]));
	} else if (! strcmp (command->command, "shutdown")) {
		NIH_MUST (message = upstart_message_new (
				  NULL, UPSTART_SHUTDOWN,
				  UPSTART_EVENT_QUEUE, args[0]));
	}

	/* Send the message */
	if (nih_io_message_send (message, control_sock) < 0)
		goto error;

	nih_free (message);

	return 0;

error:
	err = nih_error_get ();
	nih_error (_("Communication error: %s"), err->message);
	nih_free (err);

	exit (1);
}


/**
 * jobs_options:
 *
 * Command-line options accepted for the jobs command.
 **/
static NihOption jobs_options[] = {
	NIH_OPTION_LAST
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
static int
jobs_action (NihCommand   *command,
	     char * const *args)
{
	NihIoMessage *message;
	NihError     *err;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	NIH_MUST (message = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
						 UPSTART_WATCH_JOBS));

	/* Send the message */
	if (nih_io_message_send (message, control_sock) < 0)
		goto error;

	/* Receive all replies */
	for (;;) {
		NihIoMessage *reply;
		size_t        len;

		reply = nih_io_message_recv (message, control_sock, &len);
		if (! reply)
			goto error;

		if (upstart_message_handle (reply, reply, handlers, NULL) < 0)
			goto error;

		nih_free (reply);
	}

	nih_free (message);

	return 0;

error:
	err = nih_error_get ();
	nih_error (_("Communication error: %s"), err->message);
	nih_free (err);

	exit (1);
}


/**
 * events_options:
 *
 * Command-line options accepted for the events command.
 **/
static NihOption events_options[] = {
	NIH_OPTION_LAST
};

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
static int
events_action (NihCommand   *command,
	       char * const *args)
{
	NihIoMessage *message;
	NihError     *err;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	NIH_MUST (message = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
						 UPSTART_WATCH_EVENTS));

	/* Send the message */
	if (nih_io_message_send (message, control_sock) < 0)
		goto error;

	/* Receive all replies */
	for (;;) {
		NihIoMessage *reply;
		size_t        len;

		reply = nih_io_message_recv (message, control_sock, &len);
		if (! reply)
			goto error;

		if (upstart_message_handle (reply, reply, handlers, NULL) < 0)
			goto error;

		nih_free (reply);
	}

	nih_free (message);

	return 0;

error:
	err = nih_error_get ();
	nih_error (_("Communication error: %s"), err->message);
	nih_free (err);

	exit (1);
}


/**
 * options:
 *
 * Command-line options accepted for all arguments.
 **/
static NihOption options[] = {
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

	{ "emit", N_("EVENT"),
	  N_("Emit an event."),
	  N_("EVENT is the name of an event the init daemon should emit."),
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

	{ "shutdown", N_("EVENT"),
	  N_("Emit a shutdown event."),
	  N_("EVENT is the name of an event the init daemon should emit "
	     "after the shutdown event has been emitted."),
	  &event_commands, emit_options, emit_action },

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

	return ret;
}


/**
 * handle_job_status:
 * @data: data pointer,
 * @pid: origin of message,
 * @type: message type,
 * @name: name of job,
 * @goal: current goal,
 * @state: state of job,
 * @process_state: state of current process,
 * @pid: process id,
 * @description: description of job.
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
			      JobState            state,
			      ProcessState        process_state,
			      pid_t               process,
			      const char         *description)
{
	char *extra;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB_STATUS);
	nih_assert (name != NULL);

	if (state == JOB_WAITING) {
		NIH_MUST (extra = nih_strdup (NULL, ""));

	} else if ((process_state == PROCESS_SPAWNED)
		   || (process_state == PROCESS_NONE)) {
		NIH_MUST (extra = nih_sprintf (
				  NULL, ", process %s",
				  process_state_name (process_state)));
	} else {
		NIH_MUST (extra = nih_sprintf (
				  NULL, ", process %d %s", process,
				  process_state_name (process_state)));
	}

	nih_message ("%s (%s) %s%s", name, job_goal_name (goal),
		     job_state_name (state), extra);

	nih_free (extra);

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
 * Returns: zero on success, negative value on error.
 **/
static int handle_job_unknown  (void               *data,
				pid_t               pid,
				UpstartMessageType  type,
				const char         *name)
{
	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB_UNKNOWN);
	nih_assert (name != NULL);

	nih_warn (_("unknown job: %s\n"), name);

	return 0;
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
 * @name: name of event.
 *
 * Function called on receipt of a message notifying us of an event
 * emission.
 *
 * Builds a single-line string describing the event and outputs it.
 *
 * Returns: zero on success, negative value on error.
 **/
static int handle_event (void               *data,
			 pid_t               pid,
			 UpstartMessageType  type,
			 const char         *name)
{
	nih_assert (pid > 0);
	nih_assert (type == UPSTART_EVENT);
	nih_assert (name != NULL);

	nih_message (_("%s event"), name);

	return 0;
}
