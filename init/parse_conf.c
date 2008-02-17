/* upstart
 *
 * parse_conf.c - general configuration parsing
 *
 * Copyright © 2008 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
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
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/config.h>
#include <nih/logging.h>
#include <nih/error.h>

#include "conf.h"
#include "parse_conf.h"
#include "parse_job.h"


/**
 * stanzas:
 *
 * This is the table of known configuration stanzas and the functions
 * that handle parsing them.
 **/
static NihConfigStanza stanzas[] = {
	NIH_CONFIG_LAST
};


/**
 * parse_conf:
 * @conffile: configuration file being parsed,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * This function is used to parse a job definition from @file, taking the
 * name from the stanza itself.  A block is expected containing a sequence
 * of stanzas is expected, defining the parameters of the job.
 *
 * The necessary configuration item is allocated and attached to the file
 * automatically.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
parse_conf (ConfFile   *conffile,
	    const char *file,
	    size_t      len,
	    size_t     *pos,
	    size_t     *lineno)
{
	nih_assert (conffile != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (nih_config_parse_file (file, len, pos, lineno,
				   stanzas, conffile) < 0)
		return -1;

	return 0;
}
