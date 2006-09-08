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


#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>

#include <pwd.h>
#include <utmp.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/timer.h>
#include <nih/string.h>
#include <nih/signal.h>
#include <nih/main.h>
#include <nih/option.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <upstart/control.h>


/**
 * PID_FILE:
 *
 * Where we store our process id so that a running shutdown can be detected
 * or cancelled.
 **/
#define PID_FILE "/var/run/shutdown.pid"

/**
 * ETC_NOLOGIN:
 *
 * File we write to to prevent logins.
 **/
#define ETC_NOLOGIN "/etc/nologin"

/**
 * DEV:
 *
 * Directory containing tty device nodes.
 **/
#define DEV "/dev"


/* Prototypes for static functions */
static int   event_setter      (NihOption *option, const char *arg);
static void  shutdown_now      (void)
	__attribute__ ((noreturn));
static void  cancel_callback   (void *data, NihSignal *signal)
	__attribute__ ((noreturn));
static void  timer_callback    (const char *message);
static char *warning_message   (const char *message)
	__attribute__ ((warn_unused_result));
static void  wall              (const char *message);
static void  sysvinit_shutdown (void);


/**
 * event:
 *
 * Event to send after the shutdown event.
 **/
static char *event = NULL;

/**
 * cancel:
 *
 * True if we should cancel an already running shutdown.
 **/
static int cancel = FALSE;

/**
 * warn_only:
 *
 * True if we should only send the warning, and not perform the actual
 * shutdown.
 **/
static int warn_only = FALSE;

/**
 * when:
 *
 * Time to shutdown, parsed from the old -g argument.
 **/
static char *when = NULL;

/**
 * delay:
 *
 * How long until we shutdown.
 **/
static int delay = 0;


/**
 * options:
 *
 * Command-line options accepted for all arguments.
 **/
static NihOption options[] = {
	{ 'e', "event", N_("shutdown event to send"),
	  NULL, "EVENT", &event, NULL },
	{ 'r', "reboot", N_("reboot after shutdown"),
	  NULL, NULL, &event, event_setter },
	{ 'h', "shutdown", N_("halt or power off after shutdown"),
	  NULL, NULL, &event, event_setter },
	{ 'H', "halt", N_("halt after shutdown (implies -h)"),
	  NULL, NULL, &event, event_setter },
	{ 'P', "poweroff", N_("power off after shutdown (implies -h)"),
	  NULL, NULL, &event, event_setter },
	{ 'c', "cancel", N_("cancel a running shutdown"),
	  NULL, NULL, &cancel, NULL },
	{ 'k', "warn-only", N_("only send warnings, don't shutdown"),
	  NULL, NULL, &warn_only, NULL },

	/* Compatibility option for specifying time */
	{ 'g', NULL, NULL, NULL, "TIME", &when, NULL },

	/* Compatibility options, all ignored */
	{ 'a', NULL, NULL, NULL, NULL, NULL, NULL },
	{ 'n', NULL, NULL, NULL, NULL, NULL, NULL },
	{ 'f', NULL, NULL, NULL, NULL, NULL, NULL },
	{ 'F', NULL, NULL, NULL, NULL, NULL, NULL },
	{ 'i', NULL, NULL, NULL, "LEVEL", NULL, NULL },
	{ 't', NULL, NULL, NULL, "SECS", NULL, NULL },
	{ 'y', NULL, NULL, NULL, NULL, NULL, NULL },

	NIH_OPTION_LAST
};


int
main (int   argc,
      char *argv[])
{
	char   **args, *message, *msg;
	int      arg;
	size_t   messagelen;
	FILE    *pidfile;
	pid_t    pid = 0;

	nih_main_init (argv[0]);

	args = nih_option_parser (NULL, argc, argv, options, FALSE);
	if (! args)
		exit (1);

	/* If the event wasn't given explicitly, set it to maintenance */
	if (! event)
		NIH_MUST (event = nih_strdup (NULL, "maintenance"));


	/* When may be specified with -g, or must be first argument */
	if (! (cancel || when || args[0])) {
		fprintf (stderr, _("%s: time expected\n"), program_name);
		nih_main_suggest_help ();
		exit (1);
	} else if (! (cancel || when)) {
		NIH_MUST (when = nih_strdup (NULL, args[0]));
		arg = 1;
	} else {
		arg = 0;
	}

	/* Parse the time argument */
	if (when) {
		if (! strcmp (when, "now")) {
			/* "now" means, err, now */
			delay = 0;
		} else if (strchr (when, ':')) {
			/* Clock time */
			long       hours, mins;
			char      *endptr;
			struct tm *tm;
			time_t     now;

			hours = strtoul (when, &endptr, 10);
			if ((*endptr != ':') || (hours < 0) || (hours > 23)) {
				fprintf (stderr, _("%s: illegal hour value\n"),
					 program_name);
				nih_main_suggest_help ();
				exit (1);
			}

			mins = strtoul (endptr + 1, &endptr, 10);
			if (*endptr || (mins < 0) || (mins > 59)) {
				fprintf (stderr,
					 _("%s: illegal minute value\n"),
					 program_name);
				nih_main_suggest_help ();
				exit (1);
			}

			/* Subtract the current time to get the delay.
			 * Add a whole day if we go negative */
			now = time (NULL);
			tm = localtime (&now);
			delay = (((hours * 60) + mins)
				 - ((tm->tm_hour * 60) + tm->tm_min));
			if (delay < 0)
				delay += 1440;
		} else {
			/* Delay in minutes */
			char *endptr;

			delay = strtoul (when, &endptr, 10);
			if (*endptr || (delay < 0)) {
				fprintf (stderr, _("%s: illegal time value\n"),
					 program_name);
				nih_main_suggest_help ();
				exit (1);
			}
		}
		nih_free (when);
	}


	/* The rest of the arguments are a message.
	 * Really this should be just the next argument, but that's not
	 * how this has been traditionally done *sigh*
	 */
	NIH_MUST (message = nih_strdup (NULL, ""));
	messagelen = 0;
	for (; args[arg]; arg++) {
		char *new_message;

		NIH_MUST (new_message = nih_realloc (
				  message, NULL,
				  messagelen + strlen(args[arg]) + 4));
		message = new_message;

		strcat (message, args[arg]);
		strcat (message, " ");
		messagelen += strlen (args[arg]) + 1;
	}

	/* Terminate with \r\n */
	if (messagelen)
		strcat (message, "\r\n");


	/* Check we're root, or setuid root */
	setuid (geteuid ());
	if (getuid ()) {
		nih_error (_("Need to be root"));
		exit (1);
	}

	/* Look for an existing pid file */
	pidfile = fopen (PID_FILE, "r");
	if (pidfile) {
		fscanf (pidfile, "%d", &pid);
		fclose (pidfile);
	}

	/* Deal with the existing process */
	if (pid > 0) {
		if (cancel) {
			if (kill (pid, SIGINT) < 0) {
				nih_error (_("Shutdown is not running"));
				exit (1);
			}

			if (messagelen)
				wall (message);

			exit (0);
		} else if (kill (pid, 0) == 0) {
			nih_error (_("Another shutdown is already running"));
			exit (1);
		}
	} else if (cancel) {
		nih_error (_("Cannot find pid of running shutdown"));
		exit (1);
	}

	/* Send an initial message */
	NIH_MUST (msg = warning_message (message));
	wall (msg);
	nih_free (msg);

	if (warn_only)
		exit (0);


	/* Give us a sane environment */
	chdir ("/");
	umask (022);

	/* Shutdown now? */
	if (! delay)
		shutdown_now ();

	/* Save our pid so we can be interrupted later */
	unlink (PID_FILE);
	pidfile = fopen (PID_FILE, "w");
	if (pidfile) {
		fprintf (pidfile, "%d\n", getpid ());
		fclose (pidfile);
	} else {
		nih_warn (_("Unable to save pid to %s"), PID_FILE);
	}


	/* Ignore a whole bunch of signals */
	nih_signal_set_ignore (SIGCHLD);
	nih_signal_set_ignore (SIGHUP);
	nih_signal_set_ignore (SIGTSTP);
	nih_signal_set_ignore (SIGTTIN);
	nih_signal_set_ignore (SIGTTOU);

	/* Catch the usual quit signals */
	nih_signal_set_handler (SIGINT, nih_signal_handler);
	nih_signal_add_callback (NULL, SIGINT, cancel_callback, NULL);
	nih_signal_set_handler (SIGQUIT, nih_signal_handler);
	nih_signal_add_callback (NULL, SIGQUIT, cancel_callback, NULL);
	nih_signal_set_handler (SIGTERM, nih_signal_handler);
	nih_signal_add_callback (NULL, SIGTERM, cancel_callback, NULL);

	/* Call a timer every minute until we shutdown */
	nih_timer_add_periodic (NULL, 60,
				(NihTimerCb)timer_callback, message);

	/* Hang around */
	nih_main_loop ();

	return 0;
}


/**
 * event_setter:
 * @option: option found in arguments,
 * @arg: always %NULL.
 *
 * This function is called whenever one of the -r, -h, -H or -P options
 * is found in the argument list.  It changes the event (which can also
 * be set by -e/--event) to that implied by the option.
 **/
static int
event_setter (NihOption  *option,
	      const char *arg)
{
	char **value;

	nih_assert (option != NULL);
	nih_assert (option->value != NULL);
	nih_assert (arg == NULL);

	value = (char **)option->value;

	if (*value)
		nih_free (*value);

	switch (option->option) {
	case 'r':
		NIH_MUST (*value = nih_strdup (NULL, "reboot"));
		break;
	case 'h':
		NIH_MUST (*value = nih_strdup (NULL, "halt"));
		break;
	case 'H':
		NIH_MUST (*value = nih_strdup (NULL, "system-halt"));
		break;
	case 'P':
		NIH_MUST (*value = nih_strdup (NULL, "power-off"));
		break;
	}

	return 0;
}


/**
 * shutdown_now:
 *
 * Send a signal to init to shut down the machine.
 *
 * This does not return.
 **/
static void
shutdown_now (void)
{
	UpstartMsg msg;
	int        sock;

	/* Connect to the daemon */
	sock = upstart_open ();
	if (sock < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_error (_("Unable to establish control socket: %s"),
			   err->message);
		exit (1);
	}

	/* Build the message to send */
	msg.type = UPSTART_SHUTDOWN;
	msg.shutdown.name = event;

	/* Send the message */
	if (upstart_send_msg (sock, &msg) < 0) {
		NihError *err;

		err = nih_error_get ();

		/* Connection Refused means that init isn't running, this
		 * might mean we've just upgraded to upstart and haven't
		 * yet rebooted ... so try /dev/initctl
		 */
		if (err->number == ECONNREFUSED)
			sysvinit_shutdown ();

		nih_error (_("Unable to send message: %s"), err->message);
		exit (1);
	}

	unlink (PID_FILE);
	unlink (ETC_NOLOGIN);

	exit (0);
}

/**
 * cancel_callback:
 * @data: not used,
 * @signal: signal caught.
 *
 * This callback is run whenever one of the "cancel running shutdown"
 * signals is sent to us.
 *
 * This does not return.
 **/
static void
cancel_callback (void      *data,
		 NihSignal *signal)
{
	nih_error (_("Shutdown cancelled"));
	unlink (PID_FILE);
	unlink (ETC_NOLOGIN);
	exit (0);
}

/**
 * timer_callback:
 * @message: message to display.
 *
 * This callback is run every minute until we are ready to shutdown, it
 * ensures regular warnings are sent to logged in users and handles
 * preventing new logins.  Once time is up, it handles shutting down.
 *
 * This will modify delay each time it is called.
 **/
static void
timer_callback (const char *message)
{
	char *msg;
	int   warn = FALSE;

	delay--;
	NIH_MUST (msg = warning_message (message));


	/* Write /etc/nologin with less than 5 minutes remaining */
	if (delay <= 5) {
		FILE *nologin;

		nologin = fopen (ETC_NOLOGIN, "w");
		if (nologin) {
			fputs (msg, nologin);
			fclose (nologin);
		}
	}

	/* Only warn at particular intervals */
	if (delay < 10) {
		warn = TRUE;
	} else if (delay < 60) {
		warn = (delay % 15 ? FALSE : TRUE);
	} else if (delay < 180) {
		warn = (delay % 30 ? FALSE : TRUE);
	} else {
		warn = (delay % 60 ? FALSE : TRUE);
	}

	if (warn)
		wall (msg);

	/* Shutdown the machine at zero */
	if (! delay)
		shutdown_now ();

	nih_free (msg);
}


/**
 * warning_message:
 * @message: user message.
 *
 * Prefixes the message with details about how long until the shutdown
 * completes.
 *
 * Returns: newly allocated string.
 **/
static char *
warning_message (const char *message)
{
	nih_assert (message != NULL);

	if (delay > 1) {
		return nih_sprintf (NULL, _("\rThe system is going down for "
					    "%s in %d minutes!\r\n%s"),
				    event, delay, message);
	} else if (delay) {
		return nih_sprintf (NULL, _("\rThe system is going down for "
					    "%s IN ONE MINUTE!\r\n%s"),
				    event, message);
	} else {
		return nih_sprintf (NULL, _("\rThe system is going down for "
					    "%s NOW!\r\n%s"),
				    event, message);
	}

	return NULL;
}

/**
 * alarm_handler:
 * @signum: signal called.
 *
 * Empty function used to cause the ALRM signal to break a syscall.
 **/
static void
alarm_handler (int signum)
{
}

/**
 * wall:
 * @message: message to send.
 *
 * Send a message to all logged in users; based largely on the code from
 * bsdutils.  This is done in a child process to stop anything blocking.
 **/
static void
wall (const char *message)
{
	struct sigaction  act;
	struct utmp      *ent;
	pid_t             pid;
	time_t            now;
	struct tm        *tm;
	char             *user, *tty, hostname[MAXHOSTNAMELEN], *banner;

	pid = fork ();
	if (pid < 0) {
		nih_warn (_("Unable to fork child-process to warn users: %s"),
			  strerror (errno));
		return;
	} else if (pid > 0) {
		return;
	}

	/* Break syscalls with SIGALRM */
	act.sa_handler = alarm_handler;
	act.sa_flags = 0;
	sigemptyset (&act.sa_mask);
	sigaction (SIGALRM, &act, NULL);


	/* Get username for banner */
	user = getlogin ();
	if (! user) {
		struct passwd *pw;

		pw = getpwuid (getuid ());
		if (pw)
			user = pw->pw_name;
	}
	if (! user) {
		if (getuid ()) {
			NIH_MUST (user = nih_sprintf (NULL, "uid %d",
						      getuid ()));
		} else {
			user = "root";
		}
	}

	/* Get hostname for banner */
	gethostname (hostname, sizeof (hostname));

	/* Get terminal for banner */
	tty = ttyname (0);
	if (! tty)
		tty = "unknown";

	/* Get time */
	now = time (NULL);
	tm = localtime (&now);

	/* Construct banner */
	NIH_MUST (banner = nih_sprintf (
			  NULL, _("\007\r\nBroadcast message from %s@%s\r\n"
				  "\t(%s) at %d:%02d ...\r\n\r\n"),
			  user, hostname, tty, tm->tm_hour, tm->tm_min));


	/* Iterate entries in the utmp file */
	setutent ();
	while ((ent = getutent ()) != NULL) {
		char dev[PATH_MAX + 1];
		int  fd;

		/* Ignore entries without a name, or not a user process */
		if ((ent->ut_type != USER_PROCESS)
		    || (! strlen (ent->ut_user)))
			continue;

		/* Construct the device path */
		if (strncmp (ent->ut_line, DEV "/", 5)) {
			snprintf (dev, sizeof (dev),
				  "%s/%s", DEV, ent->ut_line);
		} else {
			snprintf (dev, sizeof (dev), "%s", ent->ut_line);
		}

		alarm (2);
		fd = open (dev, O_WRONLY | O_NDELAY | O_NOCTTY);
		if ((fd >= 0) && isatty (fd)) {
			FILE *term;

			term = fdopen (fd, "w");
			if (term) {
				fputs (banner, term);
				fputs (message, term);
				fflush (term);
				fclose (term);
			}
		}
		alarm (0);
	}
	endutent ();

	exit (0);
}


/**
 * struct request:
 *
 * This is the structure passed across /dev/initctl.
 **/
struct request {
	int  magic;
	int  cmd;
	int  runlevel;
	int  sleeptime;
	char data[368];
};

/**
 * sysvinit_shutdown:
 *
 * Attempt to shutdown a running sysvinit /sbin/init using its /dev/initctl
 * socket.
 **/
static void
sysvinit_shutdown (void)
{
	struct sigaction act;
	struct request   request;
	int              fd;

	/* Fill in the magic values */
	memset (&request, 0, sizeof (request));
	request.magic = 0x03091969;
	request.sleeptime = 5;
	request.cmd = 1;

	/* Select a runlevel based on the event name */
	if (! strcmp (event, "maintenance")) {
		request.runlevel = '1';
	} else if (! strcmp (event, "reboot")) {
		request.runlevel = '6';
	} else {
		request.runlevel = '0';
	}


	/* Break syscalls with SIGALRM */
	act.sa_handler = alarm_handler;
	act.sa_flags = 0;
	sigemptyset (&act.sa_mask);
	sigaction (SIGALRM, &act, NULL);

	/* Try and open /dev/initctl */
	alarm (3);
	fd = open ("/dev/initctl", O_WRONLY | O_NDELAY | O_NOCTTY);
	if (fd >= 0) {
		if (write (fd, &request, sizeof (request)) == sizeof (request))
			exit (0);

		close (fd);
	}

	alarm (0);
}
