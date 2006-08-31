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


#include <linux/kd.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/resource.h>

#include <signal.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>
#include <termios.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/timer.h>
#include <nih/signal.h>
#include <nih/child.h>
#include <nih/main.h>
#include <nih/logging.h>

#include "process.h"
#include "job.h"
#include "event.h"
#include "control.h"
#include "cfgfile.h"


/* Prototypes for static functions */
static void reset_console (void);
static void segv_handler  (int signum);
static void cad_handler   (void *data, NihSignal *signal);
static void kbd_handler   (void *data, NihSignal *signal);
static void stop_handler  (void *data, NihSignal *signal);


int
main (int   argc,
      char *argv[])
{
	int ret, i;

	nih_main_init (argv[0]);

	openlog (program_name, LOG_CONS, LOG_DAEMON);

	nih_log_set_logger (nih_logger_syslog);

	/* Close any file descriptors we inherited, and open the console
	 * device instead
	 */
	for (i = 0; i < 3; i++)
		close (i);

	process_setup_console (CONSOLE_OUTPUT);
	reset_console ();

	/* Check we're root */
	if (getuid ()) {
		nih_error (_("Need to be root"));
		exit (1);
	}

	/* Check we're process #1 */
	if (getpid () > 1) {
		nih_error (_("Not being executed as init"));
		exit (1);
	}

	/* Reset the signal state and install the signal handler for those
	 * signals we actually want to catch; this also sets those that
	 * can be sent to us, because we're special
	 */
	nih_signal_reset ();
	nih_signal_set_handler (SIGALRM,  nih_signal_handler);
	nih_signal_set_handler (SIGHUP,   nih_signal_handler);
	nih_signal_set_handler (SIGTSTP,  nih_signal_handler);
	nih_signal_set_handler (SIGCONT,  nih_signal_handler);
	nih_signal_set_handler (SIGINT,   nih_signal_handler);
	nih_signal_set_handler (SIGWINCH, nih_signal_handler);
	nih_signal_set_handler (SIGSEGV,  segv_handler);

	/* Ensure that we don't process events while paused */
	nih_signal_add_callback (NULL, SIGTSTP, stop_handler, NULL);
	nih_signal_add_callback (NULL, SIGCONT, stop_handler, NULL);

	/* Ask the kernel to send us SIGINT when control-alt-delete is
	 * pressed; generate an event with the same name.
	 */
	reboot (RB_DISABLE_CAD);
	nih_signal_add_callback (NULL, SIGINT, cad_handler, NULL);

	/* Ask the kernel to send us SIGWINCH when alt-uparrow is pressed;
	 * generate a kbdrequest event.
	 */
	ioctl (0, KDSIGACCEPT, SIGWINCH);
	nih_signal_add_callback (NULL, SIGWINCH, kbd_handler, NULL);


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


	/* Generate and run the startup event */
	event_queue ("startup");
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
 * @signum:
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
 * control-alt-delete event.
 **/
static void
cad_handler (void      *data,
	     NihSignal *signal)
{
	event_queue ("control-alt-delete");
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
	event_queue ("kbdrequest");
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
