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


#include <unistd.h>

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


int
main (int   argc,
      char *argv[])
{
	int ret, i;

	nih_main_init (argv[0]);

	nih_log_set_priority (NIH_LOG_DEBUG);


	/* Reset the signal state and install the signal handler for those
	 * signals we actually want to catch; this also sets those that
	 * can be sent to us, because we're special
	 */
	nih_signal_reset ();
	nih_signal_set_handler (SIGALRM, nih_signal_handler);
	nih_signal_set_handler (SIGTERM, nih_signal_handler);
	nih_signal_set_handler (SIGHUP,  nih_signal_handler);

	nih_signal_add_callback (NULL, SIGTERM, nih_main_term_signal, NULL);
	nih_signal_add_callback (NULL, SIGHUP, (NihSignalCb)cfg_read, NULL);

	/* Reap all children that die */
	nih_child_add_watch (NULL, -1, job_child_reaper, NULL);

	/* Process the event queue every time through the main loop */
	nih_main_loop_add_func (NULL, event_queue_run, NULL);


	/* Close any file descriptors we inherited, and open the console
	 * device instead
	 */
	for (i = 0; i < 3; i++)
		close (i);

	process_setup_console (CONSOLE_OUTPUT);


	/* Become session and process group leader (should be already,
	 * but you never know what initramfs did
	 */
	setsid ();

	/* Open control socket */
	control_open ();

	/* Read configuration */
	cfg_read ();


	/* Generate and run the startup event */
	event_queue_edge ("startup");
	event_queue_run (NULL, NULL);

	/* Go! */
	ret = nih_main_loop ();

	return ret;
}
