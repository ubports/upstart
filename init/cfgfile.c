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
#include <nih/string.h>
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
static ssize_t cfg_tokenise      (const char *filename, ssize_t *lineno,
				  const char *file, ssize_t len, ssize_t *pos,
				  char *dest, const char *delim);
static char *  cfg_parse_command (void *parent, const char *filename,
				  ssize_t *lineno, const char *file,
				  ssize_t len, ssize_t *pos);
static char *  cfg_parse_script  (void *parent, const char *filename,
				  ssize_t *lineno, const char *file,
				  ssize_t len, ssize_t *pos);
static ssize_t cfg_script_end    (ssize_t *lineno, const char *file,
				  ssize_t len, ssize_t *pos);


/**
 * cfg_tokenise:
 * @filename: name of file being parsed,
 * @lineno: line number,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @dest: destination to copy to,
 * @delim: characters to stop on.
 *
 * Extracts a single token from @file which is stopped when any character
 * in @delim is encountered outside of a quoted string and not escaped
 * using a backslash.
 *
 * @file may be a memory mapped file, in which case @pos should be given
 * as the offset within and @len should be the length of the file as a
 * whole.  Usually when @dest is given, @file is instead the pointer to
 * the start of the token and @len is the difference between the start
 * and end of the token (NOT the return value from this function).
 *
 * If @pos is given then it will be used as the offset within @file to
 * begin (otherwise the start is assumed), and will be updated to point
 * to @delim or past the end of the file.
 *
 * If @lineno is given it will be incremented each time a new line is
 * discovered in the file.
 *
 * If you want warnings to be output, pass both @filename and @lineno, which
 * will be used to output the warning message using the usual logging
 * functions.
 *
 * To copy the token into another string, collapsing any newlines and
 * surrounding whitespace to a single space, pass @dest which should be
 * pre-allocated to the right size (obtained by calling this function
 * with %NULL).
 *
 * Returns: the length of the token as it was/would be copied into @dest.
 **/
static ssize_t
cfg_tokenise (const char *filename,
	      ssize_t    *lineno,
	      const char *file,
	      ssize_t     len,
	      ssize_t    *pos,
	      char       *dest,
	      const char *delim)
{
	ssize_t  ws = 0, nlws = 0, i = 0, p, ret;
	int      slash = FALSE, quote = 0, nl = FALSE;

	nih_assert ((filename == NULL) || (lineno != NULL));
	nih_assert (file != NULL);
	nih_assert (len > 0);
	nih_assert (delim != NULL);

	/* We keep track of the following:
	 *   slash  whether a \ is in effect
	 *   quote  whether " or ' is in effect (set to which)
	 *   ws     number of consecutive whitespace chars so far
	 *   nlws   number of whitespace/newline chars
	 *   nl     TRUE if we need to copy ws into nlws at first non-WS
	 */

	for (p = (pos ? *pos : 0); p < len; p++) {
		int extra = 0;

		if (slash) {
			slash = FALSE;

			/* Escaped newline */
			if (file[p] == '\n') {
				nlws++;
				nl = TRUE;
				if (lineno)
					(*lineno)++;
				continue;
			} else {
				extra++;
			}
		} else if (file[p] == '\\') {
			slash = TRUE;
			continue;
		} else if (quote) {
			if (file[p] == quote) {
				quote = 0;
			} else if (file[p] == '\n') {
				nl = TRUE;
				if (lineno)
					(*lineno)++;
				continue;
			} else if (strchr (WS, file[p])) {
				ws++;
				continue;
			}
		} else if ((file[p] == '\"') || (file[p] == '\'')) {
			quote = file[p];
		} else if (strchr (delim, file[p])) {
			break;
		} else if (strchr (WS, file[p])) {
			ws++;
			continue;
		}

		if (nl) {
			/* Newline is recorded as a single space;
			 * any surrounding whitespace is lost.
			 */
			nlws += ws;
			if (dest)
				dest[i++] = ' ';
		} else if (ws && dest) {
			/* Whitespace that we've encountered to date is
			 * copied as it is.
			 */
			memcpy (dest + i, file + p - ws - extra, ws);
			i += ws;
		}

		/* Any extra characters are copied */
		if (extra && dest) {
			memcpy (dest + i, file + p - extra, extra);
			i += extra;
		}

		if (dest)
			dest[i++] = file[p];

		ws = 0;
		nl = FALSE;
		extra = 0;
	}

	/* Add the NULL byte */
	if (dest)
		dest[i++] = '\0';


	/* A trailing slash on the end of the file makes no sense, we'll
	 * assume they intended there to be a newline after it and ignore
	 * the character by treating it as whitespace.
	 */
	if (slash) {
		if (filename)
			nih_warn ("%s:%d: %s", filename, *lineno,
				  _("Ignored trailing slash"));

		ws++;
	}

	/* Leaving quotes open is generally bad, close it at the last
	 * piece of whitespace (ie. do nothing :p)
	 */
	if (quote) {
		if (filename)
			nih_warn ("%s:%d: %s", filename, *lineno,
				  _("Unterminated quoted string"));
	}


	/* The return value is the length of the token with any newlines and
	 * surrounding whitespace converted to a single character and any
	 * trailing whitespace removed.
	 *
	 * The actual end of the text read is returned in *pos.
	 */
	ret = p - (pos ? *pos : 0) - ws - nlws;
	if (pos)
		*pos = p;

	return ret;
}

/**
 * cfg_parse_command:
 * @parent: parent of returned string,
 * @filename: name of file being parsed,
 * @lineno: current line number,
 * @file: memory mapped copy of file, or string buffer,
 * @len: length of @file,
 * @pos: position to read from.
 *
 * Parses a command at the current location of @file.  @pos should point
 * at the start of the line, the first token is expected to contain the
 * stanza name.
 *
 * @filename and @lineno are used to report warnings, and @lineno is
 * incremented each time a new line is discovered in the file.
 *
 * @pos is updated to point to the next line in the configuration or will be
 * past the end of the file.
 *
 * Returns: the command string found or %NULL if one was not present.
 **/
static char *
cfg_parse_command (void       *parent,
		   const char *filename,
		   ssize_t    *lineno,
		   const char *file,
		   ssize_t     len,
		   ssize_t    *pos)
{
	char    *cmd;
	ssize_t  cmd_start, cmd_len, cmd_end;

	nih_assert (filename != NULL);
	nih_assert (lineno != NULL);
	nih_assert (file != NULL);
	nih_assert (len > 0);
	nih_assert (pos != NULL);

	/* Skip initial whitespace */
	while ((*pos < len) && strchr (WS, file[*pos]))
		(*pos)++;

	/* Skip the first token */
	while ((*pos < len) && (! strchr (WS, file[*pos])))
		(*pos)++;

	/* Skip further whitespace */
	while ((*pos < len) && strchr (WS, file[*pos]))
		(*pos)++;

	/* Find the length of string up to the first unescaped comment
	 * or newline.
	 */
	cmd_start = *pos;
	cmd_len = cfg_tokenise (filename, lineno, file, len, pos, NULL, "#\n");
	cmd_end = *pos;

	/* If we encountered the comment character, we need to spool
	 * forwards to the end of the line and step over it
	 */
	if ((*pos < len) && (file[*pos] != '\n')) {
		while ((*pos < len) && (file[*pos] != '\n'))
			(*pos)++;

		if (*pos < len) {
			(*lineno)++;
			(*pos)++;
		}
	}

	/* If there's nothing to copy, bail out now */
	if (! cmd_len)
		return NULL;


	/* Now copy the string into the destination. */
	NIH_MUST (cmd = nih_alloc (parent, cmd_len + 1));
	cfg_tokenise (NULL, NULL, file + cmd_start, cmd_end - cmd_start,
		      NULL, cmd, "#\n");

	return cmd;
}

/**
 * cfg_parse_script:
 * @parent: parent of returned string,
 * @filename: name of file being parsed,
 * @lineno: current line number,
 * @file: memory mapped copy of file, or string buffer,
 * @len: length of @file,
 * @pos: position to read from.
 *
 * Parses a shell script fragment at the current location of @file.
 * @pos should point to the start of the entire script fragment, including
 * the opening line.
 *
 * @filename and @lineno are used to report warnings, and @lineno is
 * incremented each time a new line is discovered in the file.
 *
 * @pos is updated to point to the next line in the configuration or will be
 * past the end of the file.
 *
 * Returns: the script contained in the fragment or %NULL if the end could
 * not be found before the end of file.
 **/
static char *
cfg_parse_script (void       *parent,
		  const char *filename,
		  ssize_t    *lineno,
		  const char *file,
		  ssize_t     len,
		  ssize_t    *pos)
{
	char    *script;
	ssize_t  sh_start, sh_end, sh_len, ws, p;
	int      lines;

	nih_assert (filename != NULL);
	nih_assert (lineno != NULL);
	nih_assert (file != NULL);
	nih_assert (len > 0);
	nih_assert (pos != NULL);

	/* Find the start of the script proper */
	while ((*pos < len) && (file[*pos] != '\n'))
		(*pos)++;

	/* Step over the newline */
	if (*pos < len) {
		(*lineno)++;
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

	while ((sh_end = cfg_script_end (lineno, file, len, pos)) < 0) {
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
			(*lineno)++;
			(*pos)++;
		} else {
			nih_warn ("%s:%d: %s", filename, *lineno,
				  _("end script expected"));
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
 * @lineno: current line number,
 * @file: memory mapped copy of file, or string buffer,
 * @len: length of @file,
 * @pos: position to read from.
 *
 * Determines whether the current line is an end-of-script marker.
 *
 * @pos is updated to point to the next line in configuration or past the
 * end of file.
 *
 * @lineno is incremented each time a new line is discovered in the file.
 *
 * Returns: index of script end (always the value of @pos at the time this
 * function was called) or -1 if it is not on this line.
 **/
static ssize_t
cfg_script_end (ssize_t    *lineno,
		const char *file,
		ssize_t     len,
		ssize_t    *pos)
{
	ssize_t p, end;

	nih_assert (lineno != NULL);
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
	if (p < len) {
		(*lineno)++;
		p++;
	}

	/* Return the beginning of the line (which is the end of the script)
	 * but update pos to point past this line.
	 */
	end = *pos;
	*pos = p;

	return end;
}
