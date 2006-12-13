/* upstart
 *
 * Copyright © 2006 Canonical Ltd.
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
#include <upstart/control.h>


/* Prototypes for static functions */
static int  open_socket      (void);
static void print_job_status (UpstartMsg *reply);
static void print_event      (UpstartMsg *reply);


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
	char * const *arg;
	int           sock;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	if (! args[0]) {
		fprintf (stderr, _("%s: missing job name\n"), program_name);
		nih_main_suggest_help ();
		exit (1);
	}

	sock = open_socket ();

	/* Iterate job names */
	for (arg = args; *arg; arg++) {
		UpstartMsg msg, *reply;

		/* Build the message to send */
		if (! strcmp (command->command, "start")) {
			msg.type = UPSTART_JOB_START;
			msg.job_start.name = *arg;
		} else if (! strcmp (command->command, "stop")) {
			msg.type = UPSTART_JOB_STOP;
			msg.job_stop.name = *arg;
		} else if (! strcmp (command->command, "status")) {
			msg.type = UPSTART_JOB_QUERY;
			msg.job_stop.name = *arg;
		}

		/* Send the message */
		if (upstart_send_msg (sock, &msg) < 0) {
			NihError *err;

			err = nih_error_get ();
			nih_error (_("Unable to send message: %s"),
				   err->message);
			exit (1);
		}

		/* Wait for the reply */
		reply = upstart_recv_msg (NULL, sock, NULL);
		if (! reply) {
			NihError *err;

			err = nih_error_get ();
			nih_error (_("Error receiving message: %s"),
				   err->message);
			exit (1);
		}

		switch (reply->type) {
		case UPSTART_JOB_STATUS:
			print_job_status (reply);
			break;
		case UPSTART_JOB_UNKNOWN:
			nih_warn (_("unknown job: %s\n"),
				  reply->job_unknown.name);
			break;
		default:
			nih_warn (_("unexpected reply (type %d)\n"),
				  reply->type);
		}

		nih_free (reply);
	}

	return 0;
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
	UpstartMsg msg;
	int        sock, receive_replies;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	sock = open_socket ();

	msg.type = UPSTART_JOB_LIST;

	/* Send the message */
	if (upstart_send_msg (sock, &msg) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_error (_("Unable to send message: %s"), err->message);
		exit (1);
	}

	/* Receive all replies */
	receive_replies = 1;
	while (receive_replies) {
		UpstartMsg *reply;

		reply = upstart_recv_msg (NULL, sock, NULL);
		if (! reply) {
			NihError *err;

			err = nih_error_get ();
			nih_error (_("Error receiving message: %s"),
				   err->message);
			exit (1);
		}

		switch (reply->type) {
		case UPSTART_JOB_STATUS:
			print_job_status (reply);
			break;
		case UPSTART_JOB_LIST_END:
			receive_replies = 0;
			break;
		case UPSTART_JOB_UNKNOWN:
			nih_warn (_("unknown job: %s\n"),
				  reply->job_unknown.name);
			break;
		default:
			nih_warn (_("unexpected reply (type %d)\n"),
				  reply->type);
		}

		nih_free (reply);
	}

	return 0;
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
	UpstartMsg msg;
	int        sock;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	if (! args[0]) {
		fprintf (stderr, _("%s: missing event name\n"), program_name);
		nih_main_suggest_help ();
		exit (1);
	}

	sock = open_socket ();

	if (! strcmp (command->command, "emit")) {
		msg.type = UPSTART_EVENT_QUEUE;
	} else if (! strcmp (command->command, "trigger")) {
		msg.type = UPSTART_EVENT_QUEUE;
	} else if (! strcmp (command->command, "shutdown")) {
		msg.type = UPSTART_SHUTDOWN;
	}
	msg.event_queue.name = args[0];

	/* Send the message */
	if (upstart_send_msg (sock, &msg) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_error (_("Unable to send message: %s"), err->message);
		exit (1);
	}

	return 0;
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
	UpstartMsg msg;
	int        sock;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	sock = open_socket ();

	msg.type = UPSTART_WATCH_JOBS;

	/* Send the message */
	if (upstart_send_msg (sock, &msg) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_error (_("Unable to send message: %s"),
			   err->message);
		exit (1);
	}

	/* Receive all replies */
	for (;;) {
		UpstartMsg *reply;

		reply = upstart_recv_msg (NULL, sock, NULL);
		if (! reply) {
			NihError *err;

			err = nih_error_get ();
			nih_error (_("Error receiving message: %s"),
				   err->message);
			exit (1);
		}

		switch (reply->type) {
		case UPSTART_JOB_STATUS:
			print_job_status (reply);
			break;
		default:
			nih_warn (_("unexpected reply (type %d)\n"),
				  reply->type);
		}

		nih_free (reply);
	}

	return 0;
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
	UpstartMsg msg;
	int        sock;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	sock = open_socket ();

	msg.type = UPSTART_WATCH_EVENTS;

	/* Send the message */
	if (upstart_send_msg (sock, &msg) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_error (_("Unable to send message: %s"),
			   err->message);
		exit (1);
	}

	/* Receive all replies */
	for (;;) {
		UpstartMsg *reply;

		reply = upstart_recv_msg (NULL, sock, NULL);
		if (! reply) {
			NihError *err;

			err = nih_error_get ();
			nih_error (_("Error receiving message: %s"),
				   err->message);
			exit (1);
		}

		switch (reply->type) {
		case UPSTART_EVENT:
			print_event (reply);
			break;
		default:
			nih_warn (_("unexpected reply (type %d)\n"),
				  reply->type);
		}

		nih_free (reply);
	}

	return 0;
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

	ret = nih_command_parser (NULL, argc, argv, options, commands);

	return ret;
}


/**
 * open_socket:
 *
 * Open a connection to the upstart daemon, this will exit the process if
 * not successful.
 *
 * Returns: socket or exits.
 **/
static int
open_socket (void)
{
	int sock;

	/* Check we're root */
	setuid (geteuid ());
	if (getuid ()) {
		nih_error (_("Need to be root"));
		exit (1);
	}

	/* Connect to the daemon */
	sock = upstart_open ();
	if (sock < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_error (_("Unable to establish control socket: %s"),
			   err->message);
		exit (1);
	}

	return sock;
}


/**
 * print_job_status:
 * @reply: message received.
 *
 * Output job status information received from the init daemon.
 **/
static void
print_job_status (UpstartMsg *reply)
{
	char *extra;

	nih_assert (reply != NULL);
	nih_assert (reply->type == UPSTART_JOB_STATUS);

	if (reply->job_status.state == JOB_WAITING) {
		extra = nih_strdup (NULL, "");

	} else if ((reply->job_status.process_state == PROCESS_SPAWNED)
		   || (reply->job_status.process_state == PROCESS_NONE)) {
		extra = nih_sprintf (NULL, ", process %s",
				     process_state_name (
					     reply->job_status.process_state));
	} else {
		extra = nih_sprintf (NULL, ", process %d %s",
				     reply->job_status.pid,
				     process_state_name (
					     reply->job_status.process_state));
	}

	nih_message ("%s (%s) %s%s", reply->job_status.name,
		     job_goal_name (reply->job_status.goal),
		     job_state_name (reply->job_status.state),
		     extra);

	nih_free (extra);
}


/**
 * print_event:
 * @reply: message received.
 *
 * Output the name of an event in a notification received from the
 * init daemon.
 **/
static void
print_event (UpstartMsg *reply)
{
	nih_assert (reply != NULL);
	nih_assert (reply->type == UPSTART_EVENT);

	nih_message (_("%s event"), reply->event.name);
}
