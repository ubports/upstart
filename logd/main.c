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


#include <nih/macros.h>
#include <nih/signal.h>
#include <nih/main.h>
#include <nih/logging.h>


int
main (int   argc,
      char *argv[])
{
	int ret;

	nih_main_init (argv[0]);

	nih_log_set_priority (NIH_LOG_DEBUG);

	nih_signal_set_handler (SIGTERM, nih_signal_handler);
	nih_signal_add_callback (NULL, SIGTERM, nih_main_term_signal, NULL);

	nih_signal_set_handler (SIGINT, nih_signal_handler);
	nih_signal_add_callback (NULL, SIGINT, nih_main_term_signal, NULL);


	nih_main_version ();

	ret = nih_main_loop ();

	return ret;
}
