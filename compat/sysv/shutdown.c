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
#include <nih/io.h>
#include <nih/main.h>
#include <nih/option.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <upstart/message.h>


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

/**
 * DEV_INITCTL:
 *
 * System V init control socket.
 **/
#ifndef DEV_INITCTL
#define DEV_INITCTL "/dev/initctl"
#endif


/* Prototypes for static functions */
static int   runlevel_setter   (NihOption *option, const char *arg);
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
 * runlevel:
 *
 * Runlevel to switch to.
 **/
static const char *runlevel = NULL;

/**
 * init_halt:
 *
 * Value of init_halt environment variable for event.
 **/
static const char *init_halt = NULL;

/**
 * what:
 *
 * What are we shutting down into (for the message).
 **/
static const char *what = NULL;

/**
 * cancel:
 *
 * TRUE if we should cancel an already running shutdown.
 **/
static int cancel = FALSE;

/**
 * warn_only:
 *
 * TRUE if we should only send the warning, and not perform the actual
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
	{ 'r', NULL, N_("reboot after shutdown"),
	  NULL, NULL, &runlevel, runlevel_setter },
	{ 'h', NULL, N_("halt or power off after shutdown"),
	  NULL, NULL, &runlevel, runlevel_setter },
	{ 'H', NULL, N_("halt after shutdown (implies -h)"),
	  NULL, NULL, &runlevel, runlevel_setter },
	{ 'P', NULL, N_("power off after shutdown (implies -h)"),
	  NULL, NULL, &runlevel, runlevel_setter },
	{ 'c', NULL, N_("cancel a running shutdown"),
	  NULL, NULL, &cancel, NULL },
	{ 'k', NULL, N_("only send warnings, don't shutdown"),
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
	pid_t    pid = 0;

	nih_main_init (argv[0]);

	nih_option_set_usage (_("TIME [MESSAGE]"));
	nih_option_set_synopsis (_("Bring the system down."));
	nih_option_set_help (
		_("TIME may have different formats, the most common is simply "
		  "the word 'now' which will bring the system down "
		  "immediately.  Other valid formats are +m, where m is the "
		  "number of minutes to wait until shutting down and hh:mm "
		  "which specifies the time on the 24hr clock.\n"
		  "\n"
		  "Logged in users are warned by a message sent to their "
		  "terminal, you may include an optional MESSAGE included "
		  "with this.  Messages can be sent without actually "
		  "bringing the system down by using the -k option.\n"
		  "\n"
		  "If TIME is given, the command will remain in the "
		  "foreground until the shutdown occurs.  It can be cancelled "
		  "by Control-C, or by another user using the -c option.\n"
		  "\n"
		  "The system is brought down into maintenance (single-user) "
		  "mode by default, you can change this with either the -r or "
		  "-h option which specify a reboot or system halt "
		  "respectively.  The -h option can be further modified with "
		  "-H or -P to specify whether to halt the system, or to "
		  "power it off afterwards.  The default is left up to the "
		  "shutdown scripts."));

	args = nih_option_parser (NULL, argc, argv, options, FALSE);
	if (! args)
		exit (1);

	/* If the runlevel wasn't given explicitly, set it to 1 so we go
	 * down into single-user mode.
	 */
	if (! runlevel) {
		runlevel = "1";
		init_halt = NULL;
		what = "maintenance";
	}


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
		nih_fatal (_("Need to be root"));
		exit (1);
	}

	/* Look for an existing pid file and deal with the existing
	 * process if there is one.
	 */
	pid = nih_main_read_pidfile ();
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
	if (nih_main_write_pidfile (getpid ()) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_warn ("%s: %s: %s", nih_main_get_pidfile(),
			  _("Unable to write pid file"), err->message);
		nih_free (err);
	}


	/* Ignore a whole bunch of signals */
	nih_signal_set_ignore (SIGCHLD);
	nih_signal_set_ignore (SIGHUP);
	nih_signal_set_ignore (SIGTSTP);
	nih_signal_set_ignore (SIGTTIN);
	nih_signal_set_ignore (SIGTTOU);

	/* Catch the usual quit signals */
	nih_signal_set_handler (SIGINT, nih_signal_handler);
	NIH_MUST (nih_signal_add_handler (NULL, SIGINT,
					  cancel_callback, NULL));
	nih_signal_set_handler (SIGQUIT, nih_signal_handler);
	NIH_MUST (nih_signal_add_handler (NULL, SIGQUIT,
					  cancel_callback, NULL));
	nih_signal_set_handler (SIGTERM, nih_signal_handler);
	NIH_MUST (nih_signal_add_handler (NULL, SIGTERM,
					  cancel_callback, NULL));

	/* Call a timer every minute until we shutdown */
	NIH_MUST (nih_timer_add_periodic (NULL, 60,
					  (NihTimerCb)timer_callback,
					  message));

	/* Hang around */
	nih_main_loop ();

	return 0;
}


/**
 * runlevel_setter:
 * @option: option found in arguments,
 * @arg: always NULL.
 *
 * This function is called whenever one of the -r, -h, -H or -P options
 * is found in the argument list.  It changes the runlevel to that implied
 * by the option.
 **/
static int
runlevel_setter (NihOption  *option,
		 const char *arg)
{
	char **value;

	nih_assert (option != NULL);
	nih_assert (option->value != NULL);
	nih_assert (arg == NULL);

	value = (char **)option->value;

	switch (option->option) {
	case 'r':
		*value = "6";
		init_halt = NULL;
		what = "reboot";
		break;
	case 'h':
		*value = "0";
		init_halt = NULL;
		what = "halt";
		break;
	case 'H':
		*value = "0";
		init_halt = "HALT";
		what = "halt";
		break;
	case 'P':
		*value = "0";
		init_halt = "POWEROFF";
		what = "power off";
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
	NihIoMessage  *message;
	char         **args, **env;
	int            sock;

	/* Connect to the daemon */
	sock = upstart_open ();
	if (sock < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal (_("Unable to establish control socket: %s"),
			   err->message);
		exit (1);
	}

	/* Build the message to send */
	NIH_MUST (args = nih_str_array_new (NULL));
	NIH_MUST (nih_str_array_add (&args, NULL, NULL, runlevel));

	if (init_halt) {
		char *e;

		NIH_MUST (e = nih_sprintf (NULL, "INIT_HALT=%s", init_halt));

		NIH_MUST (env = nih_str_array_new (NULL));
		NIH_MUST (nih_str_array_addp (&env, NULL, NULL, e));
	} else {
		env = NULL;
	}

	NIH_MUST (message = upstart_message_new (NULL, UPSTART_INIT_DAEMON,
						 UPSTART_EVENT_EMIT,
						 "runlevel", args, env));

	/* Send the message */
	if (nih_io_message_send (message, sock) < 0) {
		NihError *err;

		err = nih_error_get ();

		/* Connection Refused means that init isn't running, this
		 * might mean we've just upgraded to upstart and haven't
		 * yet rebooted ... so try /dev/initctl
		 */
		if (err->number == ECONNREFUSED)
			sysvinit_shutdown ();

		nih_fatal (_("Unable to send message: %s"), err->message);
		exit (1);
	}

	unlink (ETC_NOLOGIN);
	nih_main_unlink_pidfile ();

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
	unlink (ETC_NOLOGIN);
	nih_main_unlink_pidfile ();
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
	char *banner, *msg;

	nih_assert (message != NULL);

	if (delay > 1) {
		banner = nih_sprintf (NULL, _("The system is going down for "
					      "%s in %d minutes!"),
				      what, delay);
	} else if (delay) {
		banner = nih_sprintf (NULL, _("The system is going down for "
					      "%s IN ONE MINUTE!"),
				      what);
	} else {
		banner = nih_sprintf (NULL, _("The system is going down for "
					      "%s NOW!"),
				      what);
	}

	if (! banner)
		return NULL;

	msg = nih_sprintf (NULL, "\r%s\r\n%s", banner, message);
	nih_free (banner);

	return msg;
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
	char             *user, *tty, hostname[MAXHOSTNAMELEN];
	char             *banner1, *banner2;

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
	banner1 = nih_sprintf (NULL, _("Broadcast message from %s@%s"),
			       user, hostname);
	banner2 = nih_sprintf (NULL, _("(%s) at %d:%02d ..."),
			       tty, tm->tm_hour, tm->tm_min);


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
				fprintf (term, "\007\r\n%s\r\n\t%s\r\n\r\n",
					 banner1, banner2);
				fputs (message, term);
				fflush (term);
				fclose (term);
			}
		}
		alarm (0);
	}
	endutent ();

	nih_free (banner1);
	nih_free (banner2);

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
	request.runlevel = runlevel[0];


	/* Break syscalls with SIGALRM */
	act.sa_handler = alarm_handler;
	act.sa_flags = 0;
	sigemptyset (&act.sa_mask);
	sigaction (SIGALRM, &act, NULL);

	/* Try and open /dev/initctl */
	alarm (3);
	fd = open (DEV_INITCTL, O_WRONLY | O_NDELAY | O_NOCTTY);
	if (fd >= 0) {
		if (write (fd, &request, sizeof (request)) == sizeof (request))
			exit (0);

		close (fd);
	}

	alarm (0);
}
