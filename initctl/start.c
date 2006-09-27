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
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/main.h>
#include <nih/option.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <upstart/control.h>
#include <upstart/job.h>


/* Operation modes */
enum {
	START,
	STOP,
	STATUS
};


/**
 * options:
 *
 * Command-line options accepted.
 **/
static NihOption options[] = {
	NIH_OPTION_LAST
};


int
main (int   argc,
      char *argv[])
{
	char **args, **arg;
	int    mode, sock;

	nih_main_init (argv[0]);
	nih_option_set_usage ("JOB...");

	mode = START;
	if (! strcmp (program_name, "stop")) {
		mode = STOP;
	} else if (! strcmp (program_name, "status")) {
		mode = STATUS;
	}

	args = nih_option_parser (NULL, argc, argv, options, FALSE);
	if (! args)
		exit (1);

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


	/* Iterate arguments */
	for (arg = args; *arg; arg++) {
		UpstartMsg msg, *reply;

		/* Build the message to send */
		switch (mode) {
		case START:
			msg.type = UPSTART_JOB_START;
			msg.job_start.name = *arg;
			break;
		case STOP:
			msg.type = UPSTART_JOB_STOP;
			msg.job_stop.name = *arg;
			break;
		case STATUS:
			msg.type = UPSTART_JOB_QUERY;
			msg.job_stop.name = *arg;
			break;
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
		default:
			fprintf (stderr, _("%s: Unexpected reply (type %d)\n"),
				 program_name, reply->type);
		}

		nih_free (reply);
	}

	return 0;
}
