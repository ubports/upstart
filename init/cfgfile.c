/* upstart
 *
 * cfgfile.c - configuration and job file parsing
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


#include <string.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/logging.h>
#include <nih/error.h>

#include "cfgfile.h"


/**
 * WS:
 *
 * Definition of what characters we consider whitespace.
 **/
#define WS " \t\r"


/* Prototypes for static functions */
static char *  cfg_read_script (void *parent, const char *file,
				ssize_t len, ssize_t *pos)
static ssize_t cfg_script_end  (const char *file, ssize_t len, ssize_t *pos);


/**
 * cfg_read_script:
 * @parent: parent of returned string,
 * @file: memory mapped copy of file, or string buffer,
 * @len: length of @file,
 * @pos: position to read from.
 *
 * @pos should point to the start of the entire script fragment, including
 * the opening line.
 *
 * @pos is updated to point to the next line in the configuration or will be
 * past the end of the file.
 *
 * Returns: the script contained in the fragment or %NULL if the end could
 * not be found before the end of file.
 **/
static char *
cfg_read_script (void       *parent,
		 const char *file,
		 ssize_t     len,
		 ssize_t    *pos)
{
	char    *script;
	ssize_t  sh_start, sh_end, sh_len, ws, p;
	int      lines;

	nih_assert (file != NULL);
	nih_assert (len > 0);
	nih_assert (pos != NULL);

	/* Find the start of the script proper */
	while ((*pos < len) && (file[*pos] != '\n'))
		(*pos)++;

	/* Step over the newline */
	if (*pos < len) {
		(*pos)++;
	} else {
		return NULL;
	}

	/* Ok, we found the start of the script.  We now need to find the end
	 * of the script which is a line that looks like:
	 *
	 * 	WS end WS script (WS COMMENT?)?
	 *
	 * Just to make things more difficult for ourselves, we work out the
	 * common whitespace on the start of the script lines and remember
	 * not to copy those out later
	 */
	sh_start = *pos;
	sh_end = -1;
	ws = -1;
	lines = 0;

	while ((sh_end = cfg_script_end (file, len, pos)) < 0) {
		ssize_t line_start;

		lines++;
		line_start = *pos;
		if (ws < 0) {
			/* Count initial whitespace */
			while ((*pos < len) && strchr (WS, file[*pos]))
				(*pos)++;

			ws = *pos - line_start;
		} else {
			/* Compare how much whitespace matches the
			 * first line; and decrease the count if it's
			 * not as much.
			 */
			while ((*pos < len) && (*pos - line_start < ws)
			       && (file[sh_start + *pos - line_start]
				   == file[*pos]))
				(*pos)++;

			if (*pos - line_start < ws)
				ws = *pos - line_start;
		}

		/* Find the end of the line */
		while ((*pos < len) && (file[*pos] != '\n'))
			(*pos)++;

		/* Step over the newline */
		if (*pos < len) {
			(*pos)++;
		} else {
			return NULL;
		}

	}

	/*
	 * Copy the fragment into a string, removing common whitespace from
	 * the start.  We can be less strict here because we already know
	 * the contents, etc.
	 */

	sh_len = sh_end - sh_start - (ws * lines);
	NIH_MUST (script = nih_alloc (parent, sh_len + 1));
	script[0] = '\0';

	p = sh_start;
	while (p < sh_end) {
		size_t line_start;

		p += ws;
		line_start = p;

		while (file[p++] != '\n')
			;

		strncat (script, file + line_start, p - line_start);
	}

	return script;
}

/**
 * cfg_script_end:
 * @file: memory mapped copy of file, or string buffer,
 * @len: length of @file,
 * @pos: position to read from.
 *
 * Determines whether the current line is an end-of-script marker.
 *
 * @pos is updated to point to the next line in configuration or past the
 * end of file.
 *
 * Returns: index of script end (always the value of @pos at the time this
 * function was called) or -1 if it is not on this line.
 **/
static ssize_t
cfg_script_end (const char *file,
		ssize_t     len,
		ssize_t    *pos)
{
	ssize_t p, end;

	nih_assert (file != NULL);
	nih_assert (len > 0);
	nih_assert (pos != NULL);

	p = *pos;

	/* Skip initial whitespace */
	while ((p < len) && strchr (WS, file[p]))
		p++;

	/* Check the first word (check we have at least 4 chars because of
	 * the need for whitespace immediately after)
	 */
	if ((len - p < 4) || strncmp (file + p, "end", 3))
		return -1;

	/* Must be whitespace after */
	if (! strchr (WS, file[p + 3]))
		return -1;

	/* Find the second word */
	p += 3;
	while ((p < len) && strchr (WS, file[p]))
		p++;

	/* Check the second word */
	if ((len - p < 6) || strncmp (file + p, "script", 6))
		return -1;

	/* May be followed by whitespace */
	p += 6;
	while ((p < len) && strchr (WS, file[p]))
		p++;

	/* May be a comment, in which case eat up to the
	 * newline
	 */
	if ((p < len) && (file[p] == '#')) {
		while ((p < len) && (file[p] != '\n'))
			p++;
	}

	/* Should be end of string, or a newline */
	if ((p < len) && (file[p] != '\n'))
		return -1;

	/* Point past the new line */
	if (p < len)
		p++;

	/* Return the beginning of the line (which is the end of the script)
	 * but update pos to point past this line.
	 */
	end = *pos;
	*pos = p;

	return end;
}
