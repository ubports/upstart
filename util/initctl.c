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
#include <nih/main.h>
#include <nih/option.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <upstart/job.h>
#include <upstart/control.h>


/**
 * options:
 *
 * Command-line options accepted for all arguments.
 **/
static NihOption options[] = {
	NIH_OPTION_LAST
};


int
main (int   argc,
      char *argv[])
{
	UpstartMsg   msg;
	int          sock, expect_reply = 1;
	char       **args;

	nih_main_init (argv[0]);

	args = nih_option_parser (NULL, argc, argv, options, TRUE);
	if (! args)
		exit (1);

	/* Check we're root */
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

	/* First argument is the command */
	if (args[0] == NULL) {
		fprintf (stderr, _("%s: missing command\n"), program_name);
		nih_main_suggest_help ();
		exit (1);
	} else if (! strcmp (args[0], "start")) {
		if (args[1] == NULL) {
			fprintf (stderr, _("%s: missing argument\n"),
				 program_name);
			nih_main_suggest_help ();
			exit (1);
		} else if (args[2] != NULL) {
			fprintf (stderr, _("%s: unexpected argument\n"),
				 program_name);
			nih_main_suggest_help ();
			exit (1);
		}

		msg.type = UPSTART_JOB_START;
		msg.job_start.name = args[1];
	} else if (! strcmp (args[0], "stop")) {
		if (args[1] == NULL) {
			fprintf (stderr, _("%s: missing argument\n"),
				 program_name);
			nih_main_suggest_help ();
			exit (1);
		} else if (args[2] != NULL) {
			fprintf (stderr, _("%s: unexpected argument\n"),
				 program_name);
			nih_main_suggest_help ();
			exit (1);
		}

		msg.type = UPSTART_JOB_STOP;
		msg.job_stop.name = args[1];
	} else if (! strcmp (args[0], "status")) {
		if (args[1] == NULL) {
			fprintf (stderr, _("%s: missing argument\n"),
				 program_name);
			nih_main_suggest_help ();
			exit (1);
		} else if (args[2] != NULL) {
			fprintf (stderr, _("%s: unexpected argument\n"),
				 program_name);
			nih_main_suggest_help ();
			exit (1);
		}

		msg.type = UPSTART_JOB_QUERY;
		msg.job_query.name = args[1];
	} else if (! strcmp (args[0], "trigger")) {
		if (args[1] == NULL) {
			fprintf (stderr, _("%s: missing argument\n"),
				 program_name);
			nih_main_suggest_help ();
			exit (1);
		}

		msg.type = UPSTART_EVENT_QUEUE_EDGE;
		msg.event_queue_edge.name = args[1];
		expect_reply = 0;
	} else if (! strcmp (args[0], "set")) {
		if ((args[1] == NULL) || (args[2] == NULL)) {
			fprintf (stderr, _("%s: missing argument\n"),
				 program_name);
			nih_main_suggest_help ();
			exit (1);
		} else if (args[3] != NULL) {
			fprintf (stderr, _("%s: unexpected argument\n"),
				 program_name);
			nih_main_suggest_help ();
			exit (1);
		}

		msg.type = UPSTART_EVENT_QUEUE_LEVEL;
		msg.event_queue_level.name = args[1];
		msg.event_queue_level.level = args[2];
		expect_reply = 0;
	} else if (! strcmp (args[0], "jobs")) {
		if (args[1] != NULL) {
			fprintf (stderr, _("%s: unexpected argument\n"),
				 program_name);
			nih_main_suggest_help ();
			exit (1);
		}

		msg.type = UPSTART_WATCH_JOBS;
		expect_reply = -1;
	} else if (! strcmp (args[0], "events")) {
		if (args[1] != NULL) {
			fprintf (stderr, _("%s: unexpected argument\n"),
				 program_name);
			nih_main_suggest_help ();
			exit (1);
		}

		msg.type = UPSTART_WATCH_EVENTS;
		expect_reply = -1;
	} else {
		fprintf (stderr, _("%s: unknown command: %s\n"),
			 program_name, args[0]);
		nih_main_suggest_help ();
		exit (1);
	}

	/* Send the message */
	if (upstart_send_msg (sock, &msg) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_error (_("Unable to send message: %s"), err->message);
		exit (1);
	}

	/* Listen for replies */
	while (expect_reply) {
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
			printf ("%s (%s) %s", reply->job_status.name,
				job_goal_name (reply->job_status.goal),
				job_state_name (reply->job_status.state));

			if (reply->job_status.state == JOB_WAITING) {
				printf ("\n");

			} else if ((reply->job_status.process_state == PROCESS_SPAWNED)
				   || (reply->job_status.process_state == PROCESS_NONE)) {
				printf (", process %s\n",
					process_state_name (reply->job_status.process_state));
			} else {
				printf (", process %d %s\n",
					reply->job_status.pid,
					process_state_name (reply->job_status.process_state));
			}
			break;
		case UPSTART_JOB_UNKNOWN:
			fprintf (stderr, _("%s: Unknown job: %s\n"),
				 program_name, reply->job_unknown.name);
			break;
		case UPSTART_EVENT:
			if (reply->event.level != NULL) {
				printf ("%s %s event\n", reply->event.name,
					reply->event.level);
			} else {
				printf ("%s event\n", reply->event.name);
			}
			break;
		default:
			fprintf (stderr, _("%s: Unexpected reply (type %d)\n"),
				 program_name, reply->type);
		}

		if (expect_reply > 0)
			expect_reply--;
	}

	return 0;
}
