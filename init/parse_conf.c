/* upstart
 *
 * parse_conf.c - general configuration parsing
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


#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/config.h>
#include <nih/logging.h>
#include <nih/error.h>

#include "conf.h"
#include "parse_conf.h"
#include "parse_job.h"


/* Prototypes for static functions */
static int stanza_job (ConfFile *conffile, NihConfigStanza *stanza,
		       const char *file, size_t len,
		       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));


/**
 * stanzas:
 *
 * This is the table of known configuration stanzas and the functions
 * that handle parsing them.
 **/
static NihConfigStanza stanzas[] = {
	{ "job", (NihConfigHandler)stanza_job },

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


/**
 * stanza_job:
 * @conffile: configuration file being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * This function is used to parse the job stanza from @file.  A block
 * terminated with "end job" is expected to follow, containing a sequence
 * of job definition stanzas.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_job (ConfFile        *conffile,
	    NihConfigStanza *stanza,
	    const char      *file,
	    size_t           len,
	    size_t          *pos,
	    size_t          *lineno)
{
	ConfItem *item;
	char     *name;
	size_t    b_pos, b_lineno, b_end;
	int       ret = 0;

	nih_assert (conffile != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	/* Expect a single argument containing the name of the new job */
	name = nih_config_next_token (NULL, file, len, pos, lineno,
				      NIH_CONFIG_CNLWS, FALSE);
	if (! name)
		return -1;

	/* Skip over any following comment to the beginning of the block */
	if (nih_config_skip_comment (file, len, pos, lineno) < 0) {
		nih_free (name);
		return -1;
	}

	/* Skip over the block, calculating the length as we go.  We store
	 * the end position and line number in separate variables and set
	 * them back over the top after we've successfully parsed the job
	 * (or if we fail to skip the block).
	 *
	 * This allows errors raised in the job parsing to have the
	 * appropriate pos and lineno for those errors rather than always
	 * being at the end of the block.
	 */
	b_pos = *pos;
	b_lineno = (lineno ? *lineno : 1);
	b_end = 0;

	if (nih_config_skip_block (file, len, &b_pos, &b_lineno,
				   "job", &b_end) < 0) {
		ret = -1;
		goto finish;
	}

	/* Now parse the item from the content of the block only.
	 *
	 * We use the end position of the block as the length (since it's
	 * relative to it), thus we can use the existing pos and lineno
	 * pointers.  If this fails, we leave them where they are and don't
	 * copy those of the block end over the top.
	 */
	nih_debug ("Loading %s from %s", name, conffile->path);
	item = conf_item_new (conffile, CONF_JOB);
	if (! item) {
		nih_error_raise_system ();
		ret = -1;
		goto finish;
	}

	item->job = parse_job (NULL, name, file, b_end, pos, lineno);
	if (! item->job) {
		nih_list_free (&item->entry);
		nih_free (name);
		return -1;
	}

finish:
	nih_free (name);

	*pos = b_pos;
	if (lineno)
		*lineno = b_lineno;

	return ret;
}
