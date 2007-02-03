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

#include <stdlib.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/main.h>
#include <nih/option.h>
#include <nih/command.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <upstart/message.h>

#include <util/initctl.h>
#include <util/jobs.h>
#include <util/events.h>


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
 * Command-line options accepted for the emit and shutdown commands.
 **/
NihOption emit_options[] = {
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
