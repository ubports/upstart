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
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/resource.h>

#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <termios.h>

#include <linux/kd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/timer.h>
#include <nih/signal.h>
#include <nih/child.h>
#include <nih/option.h>
#include <nih/main.h>
#include <nih/error.h>
#include <nih/logging.h>

#include "process.h"
#include "job.h"
#include "event.h"
#include "control.h"
#include "cfgfile.h"
#include "paths.h"


/**
 * STATE_FD:
 *
 * File descriptor we read our state from.
 **/
#define STATE_FD 101


/* Prototypes for static functions */
static void reset_console   (void);
static void segv_handler    (int signum);
static void cad_handler     (void *data, NihSignal *signal);
static void kbd_handler     (void *data, NihSignal *signal);
static void stop_handler    (void *data, NihSignal *signal);
static void term_handler    (const char *prog, NihSignal *signal);
static void read_state      (int fd);
static void write_state     (int fd);


/**
 * restart:
 *
 * This is set to TRUE if we're being re-exec'd by an existing init
 * process.
 **/
static int restart = FALSE;


/**
 * options:
 *
 * Command-line options we accept.
 **/
static NihOption options[] = {
	{ 0, "restart", NULL, NULL, NULL, &restart, NULL },

	/* Ignore invalid options */
	{ '-', "--", NULL, NULL, NULL, NULL, NULL },

	NIH_OPTION_LAST
};


int
main (int   argc,
      char *argv[])
{
	char **args;
	int    ret, i;

	nih_main_init (argv[0]);

	nih_option_set_synopsis (_("Process management daemon."));
	nih_option_set_help (_("This daemon is normally executed by the "
			       "kernel and given process id 1 to denote its "
			       "special status.  When executed by a user "
			       "process, it will actually run /sbin/telinit "
			       "if that exists."));

	args = nih_option_parser (NULL, argc, argv, options, FALSE);
	if (! args)
		exit (1);

	/* Check we're root */
	if (getuid ()) {
		nih_error (_("Need to be root"));
		exit (1);
	}

	/* Check we're process #1 */
	if (getpid () > 1) {
		execv (TELINIT, argv);
		/* Ignore failure, probably just that telinit doesn't exist */

		nih_error (_("Not being executed as init"));
		exit (1);
	}


	/* Send all logging output to syslog */
	openlog (program_name, LOG_CONS, LOG_DAEMON);
	nih_log_set_logger (nih_logger_syslog);

	/* Close any file descriptors we inherited, and open the console
	 * device instead.  Normally we reset the console, unless we're
	 * inheriting one from another init process.
	 */
	for (i = 0; i < 3; i++)
		close (i);

	process_setup_console (NULL, CONSOLE_OUTPUT);
	if (! restart)
		reset_console ();


	/* Reset the signal state and install the signal handler for those
	 * signals we actually want to catch; this also sets those that
	 * can be sent to us, because we're special
	 */
	nih_signal_reset ();
	nih_signal_set_handler (SIGALRM,  nih_signal_handler);
	nih_signal_set_handler (SIGHUP,   nih_signal_handler);
	nih_signal_set_handler (SIGTSTP,  nih_signal_handler);
	nih_signal_set_handler (SIGCONT,  nih_signal_handler);
	nih_signal_set_handler (SIGTERM,  nih_signal_handler);
	nih_signal_set_handler (SIGINT,   nih_signal_handler);
	nih_signal_set_handler (SIGWINCH, nih_signal_handler);
	nih_signal_set_handler (SIGSEGV,  segv_handler);

	/* Ensure that we don't process events while paused */
	nih_signal_add_handler (NULL, SIGTSTP, stop_handler, NULL);
	nih_signal_add_handler (NULL, SIGCONT, stop_handler, NULL);

	/* Ask the kernel to send us SIGINT when control-alt-delete is
	 * pressed; generate an event with the same name.
	 */
	reboot (RB_DISABLE_CAD);
	nih_signal_add_handler (NULL, SIGINT, cad_handler, NULL);

	/* Ask the kernel to send us SIGWINCH when alt-uparrow is pressed;
	 * generate a kbdrequest event.
	 */
	ioctl (0, KDSIGACCEPT, SIGWINCH);
	nih_signal_add_handler (NULL, SIGWINCH, kbd_handler, NULL);

	/* SIGTERM instructs us to re-exec ourselves */
	nih_signal_add_handler (NULL, SIGTERM,
				(NihSignalHandler)term_handler, argv[0]);


	/* Reap all children that die */
	nih_child_add_watch (NULL, -1, job_child_reaper, NULL);

	/* Process the event queue and check the jobs for idleness
	 * every time through the main loop */
	nih_main_loop_add_func (NULL, (NihMainLoopCb)event_queue_run, NULL);
	nih_main_loop_add_func (NULL, (NihMainLoopCb)job_detect_idle, NULL);


	/* Become session and process group leader (should be already,
	 * but you never know what initramfs did
	 */
	setsid ();

	/* Open control socket */
	control_open ();

	/* Read configuration */
	cfg_watch_dir (NULL, CFG_DIR, NULL);

	/* Set the PATH environment variable */
	setenv ("PATH", PATH, TRUE);


	/* Generate and run the startup event or read the state from the
	 * init daemon that exec'd us
	 */
	if (! restart) {
		Job *logd;

		/* FIXME this is a bit of a hack, should have a list of
		 * essential services or something
		 */
		logd = job_find_by_name ("logd");
		if (logd) {
			job_start (logd);
			if (logd->state == JOB_RUNNING) {
				/* Hang around until logd signals that it's
				 * listening ... but not too long
				 */
				alarm (5);
				waitpid (logd->pid, NULL, WUNTRACED);
				kill (logd->pid, SIGCONT);
				alarm (0);
			}
		}

		event_queue (STARTUP_EVENT);
	} else {
		sigset_t mask;

		/* State file descriptor is fixed */
		read_state (STATE_FD);

		/* We're ok to receive signals again */
		sigemptyset (&mask);
		sigprocmask (SIG_SETMASK, &mask, NULL);
	}

	/* Run the event queue once, and detect anything idle */
	event_queue_run ();
	job_detect_idle ();

	/* Go! */
	ret = nih_main_loop ();

	return ret;
}


/**
 * reset_console:
 *
 * Set up the console flags to something sensible.  Cribbed from sysvinit,
 * initng, etc.
 **/
static void
reset_console (void)
{
	struct termios tty;

	tcgetattr (0, &tty);

	tty.c_cflag &= (CBAUD | CBAUDEX | CSIZE | CSTOPB | PARENB | PARODD);
	tty.c_cflag |= (HUPCL | CLOCAL | CREAD);

	/* Set up usual keys */
	tty.c_cc[VINTR]  = 3;   /* ^C */
	tty.c_cc[VQUIT]  = 28;  /* ^\ */
	tty.c_cc[VERASE] = 127;
	tty.c_cc[VKILL]  = 24;  /* ^X */
	tty.c_cc[VEOF]   = 4;   /* ^D */
	tty.c_cc[VTIME]  = 0;
	tty.c_cc[VMIN]   = 1;
	tty.c_cc[VSTART] = 17;  /* ^Q */
	tty.c_cc[VSTOP]  = 19;  /* ^S */
	tty.c_cc[VSUSP]  = 26;  /* ^Z */

	/* Pre and post processing */
	tty.c_iflag = (IGNPAR | ICRNL | IXON | IXANY);
	tty.c_oflag = (OPOST | ONLCR);
	tty.c_lflag = (ISIG | ICANON | ECHO | ECHOCTL | ECHOPRT | ECHOKE);

	/* Set the terminal line and flush it */
	tcsetattr (0, TCSANOW, &tty);
	tcflush (0, TCIOFLUSH);
}


/**
 * segv_handler:
 * @signum: signal number received.
 *
 * Handle receiving the SEGV signal, usually caused by one of our own
 * mistakes.  We deal with it by dumping core in a child process and
 * just carrying on in the parent.
 **/
static void
segv_handler (int signum)
{
	pid_t pid;

	pid = fork ();
	if (pid == 0) {
		struct sigaction act;
		struct rlimit    limit;
		sigset_t         mask;

		/* Mask out all signals */
		sigfillset (&mask);
		sigprocmask (SIG_SETMASK, &mask, NULL);

		/* Set the SEGV handler to the default so core is dumped */
		act.sa_handler = SIG_DFL;
		act.sa_flags = 0;
		sigemptyset (&act.sa_mask);
		sigaction (SIGSEGV, &act, NULL);

		/* Dump in the root directory */
		chdir ("/");

		/* Don't limit the core dump size */
		limit.rlim_cur = RLIM_INFINITY;
		limit.rlim_max = RLIM_INFINITY;
		setrlimit (RLIMIT_CORE, &limit);

		/* Raise the signal */
		raise (SIGSEGV);

		/* Unmask so that we receive it */
		sigdelset (&mask, SIGSEGV);
		sigprocmask (SIG_SETMASK, &mask, NULL);

		/* Wait for death */
		pause ();
		exit (0);
	} else if (pid > 0) {
		/* Wait for the core to be generated */
		waitpid (pid, NULL, 0);

		nih_error (_("Caught segmentation fault, core dumped"));
	} else {
		nih_error (_("Caught segmentation fault, unable to dump core"));
	}
}

/**
 * cad_handler:
 * @data: unused,
 * @signal: signal that called this handler.
 *
 * Handle having recieved the SIGINT signal, sent to us when somebody
 * presses Ctrl-Alt-Delete on the console.  We just generate a
 * ctrlaltdel event.
 **/
static void
cad_handler (void      *data,
	     NihSignal *signal)
{
	event_queue (CTRLALTDEL_EVENT);
}

/**
 * kbd_handler:
 * @data: unused,
 * @signal: signal that called this handler.
 *
 * Handle having recieved the SIGWINCH signal, sent to us when somebody
 * presses Alt-UpArrow on the console.  We just generate a
 * kbdrequest event.
 **/
static void
kbd_handler (void      *data,
	     NihSignal *signal)
{
	event_queue (KBDREQUEST_EVENT);
}

/**
 * stop_handler:
 * @data: unused,
 * @signal: signal caught.
 *
 * This is called when we receive the STOP, TSTP or CONT signals; we
 * adjust the paused variable appropriately so that the event queue and
 * job idle detection is not run.
 **/
static void
stop_handler (void      *data,
	      NihSignal *signal)
{
	nih_assert (signal != NULL);

	if (signal->signum == SIGCONT) {
		nih_info (_("Event queue resumed"));
		paused = FALSE;
	} else {
		nih_info (_("Event queue paused"));
		paused = TRUE;
	}
}


/**
 * term_handler:
 * @argv0: program to run,
 * @signal: signal caught.
 *
 * This is called when we receive the TERM signal, which instructs us
 * to reexec ourselves.
 **/
static void
term_handler (const char *argv0,
	      NihSignal  *signal)
{
	NihError *err;
	sigset_t  mask, oldmask;
	int       fds[2] = { -1, -1 };
	pid_t     pid;

	nih_assert (argv0 != NULL);
	nih_assert (signal != NULL);

	nih_warn (_("Re-executing %s"), argv0);

	/* Block signals while we work.  We're the last signal handler
	 * installed so this should mean that they're all handled now.
	 *
	 * The child must make sure that it unblocks these again when
	 * it's ready.
	 */
	sigfillset (&mask);
	sigprocmask (SIG_BLOCK, &mask, &oldmask);

	/* Close the control connection */
	control_close ();

	/* Create pipe */
	if (pipe (fds) < 0) {
		nih_error_raise_system ();
		goto error;
	}

	/* Fork a child that can send the state to the new init process */
	pid = fork ();
	if (pid < 0) {
		nih_error_raise_system ();
		goto error;
	} else if (pid == 0) {
		close (fds[0]);

		write_state (fds[1]);
		exit (0);
	} else {
		if (dup2 (fds[0], STATE_FD) < 0) {
			nih_error_raise_system ();
			goto error;
		}

		close (fds[0]);
		close (fds[1]);
		fds[0] = fds[1] = -1;
	}

	/* Argument list */
	execl (argv0, argv0, "--restart", NULL);
	nih_error_raise_system ();

error:
	err = nih_error_get ();
	nih_error (_("Failed to re-execute %s: %s"), argv0, err->message);
	nih_free (err);

	close (fds[0]);
	close (fds[1]);

	control_open ();

	sigprocmask (SIG_SETMASK, &oldmask, NULL);
}

/**
 * read_state:
 * @fd: file descriptor to read from.
 *
 * Read event and job state from @fd, which is a trivial line-based
 * protocol that we can keep the same without too much difficultly.  It's
 * tempting to use the control sockets for this, but they break too often.
 **/
static void
read_state (int fd)
{
	Job   *job = NULL;
	Event *event = NULL;
	FILE  *state;
	char   buf[80];

	nih_debug ("Reading state");

	/* Use stdio as it's a light-weight thing that won't change */
	state = fdopen (fd, "r");
	if (! state) {
		nih_warn (_("Unable to read from state descriptor: %s"),
			  strerror (errno));
		return;
	}

	/* It's just a series of simple lines; if one begins Job then it
	 * indicates the start of a Job description, otherwise if it
	 * begins Event then it's the start of an Event description.
	 *
	 * Lines beginning "." are assumed to belong to the current job
	 * or event.
	 */
	while (fgets (buf, sizeof (buf), state)) {
		char *ptr;

		/* Strip newline */
		ptr = strchr (buf, '\n');
		if (ptr)
			*ptr = '\0';

		if (! strncmp (buf, "Job ", 4)) {
			job = job_read_state (NULL, buf);
			event = NULL;
		} else if (! strncmp (buf, "Event ", 5)) {
			event = event_read_state (NULL, buf);
			job = NULL;
		} else if (buf[0] == '.') {
			if (job) {
				job = job_read_state (job, buf);
			} else if (event) {
				event = event_read_state (event, buf);
			}
		} else {
			event = NULL;
			job = NULL;
		}
	}

	if (fclose (state))
		nih_warn (_("Error after reading state: %s"),
			  strerror (errno));

	nih_debug ("State read from parent");
}

/**
 * write_state:
 * @fd: file descriptor to write to.
 *
 * Write event and job state to @fd, which is a trivial line-based
 * protocol that we can keep the same without too much difficultly.  It's
 * tempting to use the control sockets for this, but they break too often.
 **/
static void
write_state (int fd)
{
	FILE  *state;

	/* Use stdio as it's a light-weight thing that won't change */
	state = fdopen (fd, "w");
	if (! state) {
		nih_warn (_("Unable to write to state descriptor: %s"),
			  strerror (errno));
		return;
	}

	event_write_state (state);
	job_write_state (state);

	if (fclose (state))
		nih_warn (_("Error after writing state: %s"),
			  strerror (errno));
}
