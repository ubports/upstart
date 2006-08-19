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


#include <stdlib.h>

#include <nih/macros.h>
#include <nih/main.h>
#include <nih/option.h>


static NihOption options[] = {
	NIH_OPTION_LAST
};


int
main (int   argc,
      char *argv[])
{
	nih_main_init (argv[0]);

	if (! nih_option_parser (NULL, argc, argv, options, TRUE))
		exit (1);

	return 0;
}
