/* upstart
 *
 * cfgfile.c - configuration and job file parsing
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
#include <sys/time.h>
#include <sys/resource.h>

#include <time.h>
#include <dirent.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/timer.h>
#include <nih/file.h>
#include <nih/config.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <upstart/job.h>

#include "job.h"
#include "event.h"
#include "cfgfile.h"
#include "errors.h"


/**
 * WatchInfo:
 * @parent: parent for jobs,
 * @prefix: prefix for job names.
 *
 * Data pointed passed to the config file watcher function.
 **/
typedef struct watch_info {
	const void *parent;
	char       *prefix;
} WatchInfo;


/* Prototypes for static functions */
static int  cfg_stanza_description (Job *job, NihConfigStanza *stanza,
				    const char *file, size_t len,
				    size_t *pos, size_t *lineno);
static int  cfg_stanza_author      (Job *job, NihConfigStanza *stanza,
				    const char *file, size_t len,
				    size_t *pos, size_t *lineno);
static int  cfg_stanza_version     (Job *job, NihConfigStanza *stanza,
				    const char *file, size_t len,
				    size_t *pos, size_t *lineno);
static int  cfg_stanza_emits       (Job *job, NihConfigStanza *stanza,
				    const char *file, size_t len,
				    size_t *pos, size_t *lineno);
static int  cfg_stanza_on          (Job *job, NihConfigStanza *stanza,
				    const char *file, size_t len,
				    size_t *pos, size_t *lineno);
static int  cfg_stanza_start       (Job *job, NihConfigStanza *stanza,
				    const char *file, size_t len,
				    size_t *pos, size_t *lineno);
static int  cfg_stanza_stop        (Job *job, NihConfigStanza *stanza,
				    const char *file, size_t len,
				    size_t *pos, size_t *lineno);
static int  cfg_stanza_exec        (Job *job, NihConfigStanza *stanza,
				    const char *file, size_t len,
				    size_t *pos, size_t *lineno);
static int  cfg_stanza_daemon      (Job *job, NihConfigStanza *stanza,
				    const char *file, size_t len,
				    size_t *pos, size_t *lineno);
static int  cfg_stanza_respawn     (Job *job, NihConfigStanza *stanza,
				    const char *file, size_t len,
				    size_t *pos, size_t *lineno);
static int  cfg_stanza_script      (Job *job, NihConfigStanza *stanza,
				    const char *file, size_t len,
				    size_t *pos, size_t *lineno);
static int  cfg_stanza_instance    (Job *job, NihConfigStanza *stanza,
				    const char *file, size_t len,
				    size_t *pos, size_t *lineno);
static int  cfg_stanza_pid         (Job *job, NihConfigStanza *stanza,
				    const char *file, size_t len,
				    size_t *pos, size_t *lineno);
static int  cfg_stanza_kill        (Job *job, NihConfigStanza *stanza,
				    const char *file, size_t len,
				    size_t *pos, size_t *lineno);
static int  cfg_stanza_normalexit  (Job *job, NihConfigStanza *stanza,
				    const char *file, size_t len,
				    size_t *pos, size_t *lineno);
static int  cfg_stanza_console     (Job *job, NihConfigStanza *stanza,
				    const char *file, size_t len,
				    size_t *pos, size_t *lineno);
static int  cfg_stanza_env         (Job *job, NihConfigStanza *stanza,
				    const char *file, size_t len,
				    size_t *pos, size_t *lineno);
static int  cfg_stanza_umask       (Job *job, NihConfigStanza *stanza,
				    const char *file, size_t len,
				    size_t *pos, size_t *lineno);
static int  cfg_stanza_nice        (Job *job, NihConfigStanza *stanza,
				    const char *file, size_t len,
				    size_t *pos, size_t *lineno);
static int  cfg_stanza_limit       (Job *job, NihConfigStanza *stanza,
				    const char *file, size_t len,
				    size_t *pos, size_t *lineno);
static int  cfg_stanza_chroot      (Job *job, NihConfigStanza *stanza,
				    const char *file, size_t len,
				    size_t *pos, size_t *lineno);
static int  cfg_stanza_chdir       (Job *job, NihConfigStanza *stanza,
				    const char *file, size_t len,
				    size_t *pos, size_t *lineno);

static void cfg_watcher            (WatchInfo *info, NihFileWatch *watch,
				    uint32_t events, const char *name);


/**
 * stanzas:
 *
 * This is the table of known configuration file stanzas and the functions
 * that handle parsing them.
 **/
static NihConfigStanza stanzas[] = {
	{ "description", (NihConfigHandler)cfg_stanza_description },
	{ "author",      (NihConfigHandler)cfg_stanza_author      },
	{ "version",     (NihConfigHandler)cfg_stanza_version     },
	{ "emits",       (NihConfigHandler)cfg_stanza_emits       },
	{ "on",          (NihConfigHandler)cfg_stanza_on          },
	{ "start",       (NihConfigHandler)cfg_stanza_start       },
	{ "stop",        (NihConfigHandler)cfg_stanza_stop        },
	{ "exec",        (NihConfigHandler)cfg_stanza_exec        },
	{ "daemon",      (NihConfigHandler)cfg_stanza_daemon      },
	{ "respawn",     (NihConfigHandler)cfg_stanza_respawn     },
	{ "script",      (NihConfigHandler)cfg_stanza_script      },
	{ "instance",    (NihConfigHandler)cfg_stanza_instance    },
	{ "pid",         (NihConfigHandler)cfg_stanza_pid         },
	{ "kill",        (NihConfigHandler)cfg_stanza_kill        },
	{ "normalexit",  (NihConfigHandler)cfg_stanza_normalexit  },
	{ "console",     (NihConfigHandler)cfg_stanza_console     },
	{ "env",         (NihConfigHandler)cfg_stanza_env         },
	{ "umask",       (NihConfigHandler)cfg_stanza_umask       },
	{ "nice",        (NihConfigHandler)cfg_stanza_nice        },
	{ "limit",       (NihConfigHandler)cfg_stanza_limit       },
	{ "chroot",      (NihConfigHandler)cfg_stanza_chroot      },
	{ "chdir",       (NihConfigHandler)cfg_stanza_chdir       },

	NIH_CONFIG_LAST
};


/**
 * cfg_read_job:
 * @parent: parent of returned job,
 * @filename: name of file to read,
 * @name: name to call job.
 *
 * Reads the @filename given and uses the information within to construct
 * a new job structure named @name which is returned.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * Returns: newly allocated job structure, or NULL if the file was invalid.
 **/
Job *
cfg_read_job (const void *parent,
	      const char *filename,
	      const char *name)
{
	Job    *job, *old_job;
	size_t  lineno;

	nih_assert (filename != NULL);
	nih_assert (name != NULL);

	/* Look for an old job with that name */
	old_job = job_find_by_name (name);

	/* Allocate a new structure */
	NIH_MUST (job = job_new (parent, name));
	nih_debug ("Loading %s from %s", job->name, filename);

	/* Parse the file, if we can't parse the new file, we just return now
	 * without ditching the old job if there is one.
	 */
	lineno = 1;
	if (nih_config_parse (filename, NULL, &lineno, stanzas, job) < 0) {
		NihError *err;

		err = nih_error_get ();
		switch (err->number) {
		case NIH_CONFIG_EXPECTED_TOKEN:
		case NIH_CONFIG_UNEXPECTED_TOKEN:
		case NIH_CONFIG_TRAILING_SLASH:
		case NIH_CONFIG_UNTERMINATED_QUOTE:
		case NIH_CONFIG_UNTERMINATED_BLOCK:
		case NIH_CONFIG_UNKNOWN_STANZA:
		case CFG_ILLEGAL_VALUE:
			nih_error ("%s:%d: %s",
				   filename, lineno, err->message);
			break;
		default:
			nih_error (_("%s: unable to read: %s"), filename,
				   err->message);
			break;
		}

		nih_list_free (&job->entry);
		nih_free (err);

		return NULL;
	}


	/* Now we sanity check the job, checking for things that will
	 * cause assertions or bad behaviour later on, or just general
	 * warnings
	 */

	/* Must have one command or script */
	if ((job->script == NULL) && (job->command == NULL)) {
		nih_error (_("%s: 'exec' or 'script' must be specified"),
			   filename);
		nih_list_free (&job->entry);
		return NULL;
	}

	/* Must not have both command and script */
	if ((job->script != NULL) && (job->command != NULL)) {
		nih_error (_("%s: only one of 'exec' and 'script' may be specified"),
			   filename);
		nih_list_free (&job->entry);
		return NULL;
	}

	/* respawn script makes no sense unless respawn */
	if (job->respawn_script && (! job->respawn)) {
		nih_warn (_("%s: 'respawn script' ignored unless 'respawn' specified"),
			  filename);
	}

	/* pid file makes no sense unless respawn */
	if (job->pidfile && (! job->respawn)) {
		nih_warn (_("%s: 'pid file' ignored unless 'respawn' specified"),
			  filename);
	}

	/* pid binary makes no sense unless respawn */
	if (job->binary && (! job->respawn)) {
		nih_warn (_("%s: 'pid binary' ignored unless 'respawn' specified"),
			  filename);
	}


	/* Deal with the case where we're reloading an existing	job; we
	 * copy information out of the old structure and free that
	 */
	if (old_job) {
		time_t now;

		nih_debug ("Replacing existing %s job", job->name);

		job->goal = old_job->goal;
		job->state = old_job->state;
		job->process_state = old_job->process_state;
		job->pid = old_job->pid;

		now = time (NULL);

		if (old_job->kill_timer)
			NIH_MUST (job->kill_timer = nih_timer_add_timeout (
					  job, old_job->kill_timer->due - now,
					  old_job->kill_timer->callback, job));

		if (old_job->pid_timer)
			NIH_MUST (job->pid_timer = nih_timer_add_timeout (
					  job, old_job->pid_timer->due - now,
					  old_job->pid_timer->callback, job));

		nih_list_free (&old_job->entry);
	}

	return job;
}


/**
 * cfg_stanza_description:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a description stanza from @file, extracting a single argument
 * containing a description of the job.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_description (Job             *job,
			NihConfigStanza *stanza,
			const char      *file,
			size_t           len,
			size_t          *pos,
			size_t          *lineno)
{
	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (job->description)
		nih_free (job->description);

	job->description = nih_config_next_arg (job, file, len, pos, lineno);
	if (! job->description)
		return -1;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * cfg_stanza_author:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse an author stanza from @file, extracting a single argument
 * containing the author of the job.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_author (Job             *job,
		   NihConfigStanza *stanza,
		   const char      *file,
		   size_t           len,
		   size_t          *pos,
		   size_t          *lineno)
{
	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (job->author)
		nih_free (job->author);

	job->author = nih_config_next_arg (job, file, len, pos, lineno);
	if (! job->author)
		return -1;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * cfg_stanza_version:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a version stanza from @file, extracting a single argument
 * containing the version of the job.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_version (Job             *job,
		    NihConfigStanza *stanza,
		    const char      *file,
		    size_t           len,
		    size_t          *pos,
		    size_t          *lineno)
{
	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (job->version)
		nih_free (job->version);

	job->version = nih_config_next_arg (job, file, len, pos, lineno);
	if (! job->version)
		return -1;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * cfg_stanza_emits:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse an emits stanza from @file.  This stanza expects one or more
 * arguments giving the names of additional events that can be emitted
 * by this job.
 *
 * Arguments are allocated as Event structures and stored in the emits
 * list of the job.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_emits (Job             *job,
		  NihConfigStanza *stanza,
		  const char      *file,
		  size_t           len,
		  size_t          *pos,
		  size_t          *lineno)
{
	char **args, **arg;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (! nih_config_has_token (file, len, pos, lineno))
		nih_return_error (-1, NIH_CONFIG_EXPECTED_TOKEN,
				  _(NIH_CONFIG_EXPECTED_TOKEN_STR));

	args = nih_config_parse_args (NULL, file, len, pos, lineno);
	if (! args)
		return -1;

	for (arg = args; *arg; arg++) {
		Event *event;

		NIH_MUST (event = event_new (job, *arg));
		nih_list_add (&job->emits, &event->entry);
	}

	nih_free (args);

	return 0;
}

/**
 * cfg_stanza_on:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse an on stanza from @file, extracting a single argument containing
 * an event that starts the job.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_on (Job             *job,
	       NihConfigStanza *stanza,
	       const char      *file,
	       size_t           len,
	       size_t          *pos,
	       size_t          *lineno)
{
	Event *event;
	char  *name;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	name = nih_config_next_arg (NULL, file, len, pos, lineno);
	if (! name)
		return -1;

	NIH_MUST (event = event_new (job, name));
	nih_list_add (&job->start_events, &event->entry);

	nih_free (name);

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * cfg_stanza_start:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a start stanza from @file, extracting a single argument which can
 * be either "on" followed by an event name or "script" followed by a block.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_start (Job             *job,
		  NihConfigStanza *stanza,
		  const char      *file,
		  size_t           len,
		  size_t          *pos,
		  size_t          *lineno)
{
	char *arg;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	arg = nih_config_next_arg (NULL, file, len, pos, lineno);
	if (! arg)
		return -1;

	if (! strcmp (arg, "on")) {
		Event *event;
		char  *name;

		nih_free (arg);

		name = nih_config_next_arg (NULL, file, len, pos, lineno);
		if (! name)
			return -1;

		NIH_MUST (event = event_new (job, name));
		nih_list_add (&job->start_events, &event->entry);

		nih_free (name);

		return nih_config_skip_comment (file, len, pos, lineno);

	} else if (! strcmp (arg, "script")) {
		nih_free (arg);

		if (nih_config_skip_comment (file, len, pos, lineno) < 0)
			return -1;

		if (job->start_script)
			nih_free (job->start_script);

		job->start_script = nih_config_parse_block (job, file, len,
							    pos, lineno,
							    "script");
		if (! job->start_script)
			return -1;

		return 0;

	} else {
		nih_free (arg);

		nih_error_raise (NIH_CONFIG_UNKNOWN_STANZA,
				 _(NIH_CONFIG_UNKNOWN_STANZA_STR));
		return -1;
	}
}

/**
 * cfg_stanza_stop:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a stop stanza from @file, extracting a single argument which can
 * be either "on" followed by an event name or "script" followed by a block.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_stop (Job             *job,
		 NihConfigStanza *stanza,
		 const char      *file,
		 size_t           len,
		 size_t          *pos,
		 size_t          *lineno)
{
	char *arg;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	arg = nih_config_next_arg (NULL, file, len, pos, lineno);
	if (! arg)
		return -1;

	if (! strcmp (arg, "on")) {
		Event *event;
		char  *name;

		nih_free (arg);

		name = nih_config_next_arg (NULL, file, len, pos, lineno);
		if (! name)
			return -1;

		NIH_MUST (event = event_new (job, name));
		nih_list_add (&job->stop_events, &event->entry);

		nih_free (name);

		return nih_config_skip_comment (file, len, pos, lineno);

	} else if (! strcmp (arg, "script")) {
		nih_free (arg);

		if (nih_config_skip_comment (file, len, pos, lineno) < 0)
			return -1;

		if (job->stop_script)
			nih_free (job->stop_script);

		job->stop_script = nih_config_parse_block (job, file, len, pos,
							   lineno, "script");
		if (! job->stop_script)
			return -1;

		return 0;

	} else {
		nih_free (arg);

		nih_error_raise (NIH_CONFIG_UNKNOWN_STANZA,
				 _(NIH_CONFIG_UNKNOWN_STANZA_STR));
		return -1;
	}
}

/**
 * cfg_stanza_exec:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse an exec stanza from @file, extracting a complete command.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_exec (Job             *job,
		 NihConfigStanza *stanza,
		 const char      *file,
		 size_t           len,
		 size_t          *pos,
		 size_t          *lineno)
{
	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (job->command)
		nih_free (job->command);

	job->command = nih_config_parse_command (job, file, len, pos, lineno);
	if (! job->command)
		return -1;

	return 0;
}

/**
 * cfg_stanza_daemon:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a daemon stanza from @file, which may have a complete command
 * following it.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_daemon (Job             *job,
		   NihConfigStanza *stanza,
		   const char      *file,
		   size_t           len,
		   size_t          *pos,
		   size_t          *lineno)
{
	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	job->daemon = TRUE;

	if (! nih_config_has_token (file, len, pos, lineno))
		return nih_config_skip_comment (file, len, pos, lineno);


	if (job->command)
		nih_free (job->command);

	job->command = nih_config_parse_command (job, file, len, pos, lineno);
	if (! job->command)
		return -1;

	return 0;
}

/**
 * cfg_stanza_respawn:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a respawn stanza from @file, which may have a complete command
 * following it; "script" followed by a block, or a limit stanza.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_respawn (Job             *job,
		    NihConfigStanza *stanza,
		    const char      *file,
		    size_t           len,
		    size_t          *pos,
		    size_t          *lineno)
{
	char   *arg;
	size_t  arg_pos, arg_lineno;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (! nih_config_has_token (file, len, pos, lineno)) {
		job->respawn = TRUE;

		return nih_config_skip_comment (file, len, pos, lineno);
	}


	/* Peek at the next argument, and only update pos and lineno if
	 * we decide to take it.
	 */
	arg_pos = *pos;
	arg_lineno = (lineno ? *lineno : 1);
	arg = nih_config_next_arg (NULL, file, len, &arg_pos, &arg_lineno);
	if (! arg)
		return -1;

	if (! strcmp (arg, "script")) {
		nih_free (arg);
		*pos = arg_pos;
		if (lineno)
			*lineno = arg_lineno;

		if (nih_config_skip_comment (file, len, pos, lineno) < 0)
			return -1;

		if (job->respawn_script)
			nih_free (job->respawn_script);

		job->respawn_script = nih_config_parse_block (job, file, len,
							      pos, lineno,
							      "script");
		if (! job->respawn_script)
			return -1;

		return 0;

	} else if (! strcmp (arg, "limit")) {
		char *endptr;

		nih_free (arg);
		*pos = arg_pos;
		if (lineno)
			*lineno = arg_lineno;

		/* Parse the limit value */
		arg = nih_config_next_arg (NULL, file, len, pos, lineno);
		if (! arg)
			return -1;

		job->respawn_limit = strtol (arg, &endptr, 10);
		if (*endptr || (job->respawn_limit < 0)) {
			nih_free (arg);

			nih_error_raise (CFG_ILLEGAL_VALUE,
					 _(CFG_ILLEGAL_VALUE_STR));
			return -1;
		}
		nih_free (arg);

		/* Parse the timeout value */
		arg = nih_config_next_arg (NULL, file, len, pos, lineno);
		if (! arg)
			return -1;

		job->respawn_interval = strtol (arg, &endptr, 10);
		if (*endptr || (job->respawn_interval < 0)) {
			nih_free (arg);

			nih_error_raise (CFG_ILLEGAL_VALUE,
					 _(CFG_ILLEGAL_VALUE_STR));
			return -1;
		}
		nih_free (arg);

		return nih_config_skip_comment (file, len, pos, lineno);

	} else {
		nih_free (arg);

		job->respawn = TRUE;

		if (job->command)
			nih_free (job->command);

		job->command = nih_config_parse_command (job, file, len,
							 pos, lineno);
		if (! job->command)
			return -1;

		return 0;
	}
}

/**
 * cfg_stanza_script:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a script stanza from @file, extracting a following block.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_script (Job             *job,
		   NihConfigStanza *stanza,
		   const char      *file,
		   size_t           len,
		   size_t          *pos,
		   size_t          *lineno)
{
	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (nih_config_skip_comment (file, len, pos, lineno) < 0)
		return -1;

	if (job->script)
		nih_free (job->script);

	job->script = nih_config_parse_block (job, file, len, pos, lineno,
					      "script");
	if (! job->script)
		return -1;

	return 0;
}

/**
 * cfg_stanza_instance:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse an instance stanza from @file, which has no additional arguments.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_instance (Job             *job,
		     NihConfigStanza *stanza,
		     const char      *file,
		     size_t           len,
		     size_t          *pos,
		     size_t          *lineno)
{
	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	job->spawns_instance = TRUE;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * cfg_stanza_pid:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a pid stanza from @file, extracting a second-level stanza that
 * states which value to set from its argument.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_pid (Job             *job,
		NihConfigStanza *stanza,
		const char      *file,
		size_t           len,
		size_t          *pos,
		size_t          *lineno)
{
	char *arg;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	arg = nih_config_next_arg (NULL, file, len, pos, lineno);
	if (! arg)
		return -1;

	if (! strcmp (arg, "file")) {
		nih_free (arg);

		if (job->pidfile)
			nih_free (job->pidfile);

		job->pidfile = nih_config_next_arg (job, file, len,
						    pos, lineno);
		if (! job->pidfile)
			return -1;

		return nih_config_skip_comment (file, len, pos, lineno);

	} else if (! strcmp (arg, "binary")) {
		nih_free (arg);

		if (job->binary)
			nih_free (job->binary);

		job->binary = nih_config_next_arg (job, file, len,
						   pos, lineno);
		if (! job->binary)
			return -1;

		return nih_config_skip_comment (file, len, pos, lineno);

	} else if (! strcmp (arg, "timeout")) {
		char *endptr;

		nih_free (arg);

		arg = nih_config_next_arg (NULL, file, len, pos, lineno);
		if (! arg)
			return -1;

		job->pid_timeout = strtol (arg, &endptr, 10);
		if (*endptr || (job->pid_timeout < 0)) {
			nih_free (arg);

			nih_error_raise (CFG_ILLEGAL_VALUE,
					 _(CFG_ILLEGAL_VALUE_STR));
			return -1;
		}
		nih_free (arg);

		return nih_config_skip_comment (file, len, pos, lineno);

	} else {
		nih_free (arg);

		nih_error_raise (NIH_CONFIG_UNKNOWN_STANZA,
				 _(NIH_CONFIG_UNKNOWN_STANZA_STR));
		return -1;
	}
}

/**
 * cfg_stanza_kill:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a kill stanza from @file, extracting a second-level stanza that
 * states which value to set from its argument.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_kill (Job             *job,
		 NihConfigStanza *stanza,
		 const char      *file,
		 size_t           len,
		 size_t          *pos,
		 size_t          *lineno)
{
	char *arg;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	arg = nih_config_next_arg (NULL, file, len, pos, lineno);
	if (! arg)
		return -1;

	if (! strcmp (arg, "timeout")) {
		char *endptr;

		nih_free (arg);

		arg = nih_config_next_arg (NULL, file, len, pos, lineno);
		if (! arg)
			return -1;

		job->kill_timeout = strtol (arg, &endptr, 10);
		if (*endptr || (job->kill_timeout < 0)) {
			nih_free (arg);

			nih_error_raise (CFG_ILLEGAL_VALUE,
					 _(CFG_ILLEGAL_VALUE_STR));
			return -1;
		}
		nih_free (arg);

		return nih_config_skip_comment (file, len, pos, lineno);

	} else {
		nih_free (arg);

		nih_error_raise (NIH_CONFIG_UNKNOWN_STANZA,
				 _(NIH_CONFIG_UNKNOWN_STANZA_STR));
		return -1;
	}
}

/**
 * cfg_stanza_normalexit:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a normalexit stanza from @file.  This stanza expects one or more
 * arguments giving exit codes that the main process can return and be
 * considered to have been stopped normally.
 *
 * Arguments are stored in the normalexit array, and the normalexit_len
 * value updated.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_normalexit (Job             *job,
		       NihConfigStanza *stanza,
		       const char      *file,
		       size_t           len,
		       size_t          *pos,
		       size_t          *lineno)
{
	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (! nih_config_has_token (file, len, pos, lineno))
		nih_return_error (-1, NIH_CONFIG_EXPECTED_TOKEN,
				  _(NIH_CONFIG_EXPECTED_TOKEN_STR));

	while (nih_config_has_token (file, len, pos, lineno)) {
		unsigned long  status;
		char          *arg, *endptr;
		int           *new_ne;

		arg = nih_config_next_arg (NULL, file, len, pos, lineno);
		if (! arg)
			return -1;

		status = strtoul (arg, &endptr, 10);
		if (*endptr || (status > INT_MAX)) {
			nih_free (arg);
			nih_return_error (-1, CFG_ILLEGAL_VALUE,
					  _(CFG_ILLEGAL_VALUE_STR));
		}
		nih_free (arg);

		NIH_MUST (new_ne = nih_realloc (job->normalexit, job,
						sizeof (int) *
						(job->normalexit_len + 1)));

		job->normalexit = new_ne;
		job->normalexit[job->normalexit_len++] = (int) status;
	}

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * cfg_stanza_console:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a console stanza from @file, extracting a single argument that
 * specifies where console output should be sent.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_console (Job             *job,
		    NihConfigStanza *stanza,
		    const char      *file,
		    size_t           len,
		    size_t          *pos,
		    size_t          *lineno)
{
	char *arg;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	arg = nih_config_next_arg (NULL, file, len, pos, lineno);
	if (! arg)
		return -1;

	if (! strcmp (arg, "logged")) {
		job->console = CONSOLE_LOGGED;
	} else if (! strcmp (arg, "output")) {
		job->console = CONSOLE_OUTPUT;
	} else if (! strcmp (arg, "owner")) {
		job->console = CONSOLE_OWNER;
	} else if (! strcmp (arg, "none")) {
		job->console = CONSOLE_NONE;
	} else {
		nih_free (arg);

		nih_error_raise (NIH_CONFIG_UNKNOWN_STANZA,
				 _(NIH_CONFIG_UNKNOWN_STANZA_STR));
		return -1;
	}

	nih_free (arg);

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * cfg_stanza_env:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse an env stanza from @file, extracting a single argument of the form
 * VAR=VALUE.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_env (Job             *job,
		NihConfigStanza *stanza,
		const char      *file,
		size_t           len,
		size_t          *pos,
		size_t          *lineno)
{
	char **new_env, *env;
	int    envc = 0;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	/* Count the number of array elements so we can increase the size */
	for (new_env = job->env; new_env && *new_env; new_env++)
		envc++;

	NIH_MUST (new_env = nih_realloc (job->env, job,
					 sizeof (char *) * (envc + 2)));
	job->env = new_env;

	env = nih_config_next_arg (job->env, file, len, pos, lineno);
	if (! env)
		return -1;

	job->env[envc++] = env;
	job->env[envc] = NULL;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * cfg_stanza_umask:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a umask stanza from @file, extracting a single argument containing
 * a process file creation mask.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_umask (Job             *job,
		  NihConfigStanza *stanza,
		  const char      *file,
		  size_t           len,
		  size_t          *pos,
		  size_t          *lineno)
{
	char          *arg, *endptr;
	unsigned long  mask;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	arg = nih_config_next_arg (NULL, file, len, pos, lineno);
	if (! arg)
		return -1;

	mask = strtoul (arg, &endptr, 8);
	if (*endptr || (mask & ~0777)) {
		nih_free (arg);

		nih_error_raise (CFG_ILLEGAL_VALUE,
				 _(CFG_ILLEGAL_VALUE_STR));
		return -1;
	}
	nih_free (arg);

	job->umask = (mode_t)mask;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * cfg_stanza_nice:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a nice stanza from @file, extracting a single argument containing
 * a process priority.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_nice (Job             *job,
		 NihConfigStanza *stanza,
		 const char      *file,
		 size_t           len,
		 size_t          *pos,
		 size_t          *lineno)
{
	char *arg, *endptr;
	long  nice;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	arg = nih_config_next_arg (NULL, file, len, pos, lineno);
	if (! arg)
		return -1;

	nice = strtol (arg, &endptr, 10);
	if (*endptr || (nice < -20) || (nice > 19)) {
		nih_free (arg);

		nih_error_raise (CFG_ILLEGAL_VALUE,
				 _(CFG_ILLEGAL_VALUE_STR));
		return -1;
	}
	nih_free (arg);

	job->nice = (int)nice;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * cfg_stanza_limit:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a limit stanza from @file, extracting a second-level stanza that
 * states which l.imit to set from its two following arguments.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_limit (Job             *job,
		  NihConfigStanza *stanza,
		  const char      *file,
		  size_t           len,
		  size_t          *pos,
		  size_t          *lineno)
{
	int   resource;
	char *arg, *endptr;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	arg = nih_config_next_arg (NULL, file, len, pos, lineno);
	if (! arg)
		return -1;

	if (! strcmp (arg, "as")) {
		resource = RLIMIT_AS;
	} else if (! strcmp (arg, "core")) {
		resource = RLIMIT_CORE;
	} else if (! strcmp (arg, "cpu")) {
		resource = RLIMIT_CPU;
	} else if (! strcmp (arg, "data")) {
		resource = RLIMIT_DATA;
	} else if (! strcmp (arg, "fsize")) {
		resource = RLIMIT_FSIZE;
	} else if (! strcmp (arg, "memlock")) {
		resource = RLIMIT_MEMLOCK;
	} else if (! strcmp (arg, "msgqueue")) {
		resource = RLIMIT_MSGQUEUE;
	} else if (! strcmp (arg, "nice")) {
		resource = RLIMIT_NICE;
	} else if (! strcmp (arg, "nofile")) {
		resource = RLIMIT_NOFILE;
	} else if (! strcmp (arg, "nproc")) {
		resource = RLIMIT_NPROC;
	} else if (! strcmp (arg, "rss")) {
		resource = RLIMIT_RSS;
	} else if (! strcmp (arg, "rtprio")) {
		resource = RLIMIT_RTPRIO;
	} else if (! strcmp (arg, "sigpending")) {
		resource = RLIMIT_SIGPENDING;
	} else if (! strcmp (arg, "stack")) {
		resource = RLIMIT_STACK;
	} else {
		nih_free (arg);

		nih_error_raise (NIH_CONFIG_UNKNOWN_STANZA,
				 _(NIH_CONFIG_UNKNOWN_STANZA_STR));
		return -1;
	}

	nih_free (arg);


	/* Allocate a resource limit structure in that position */
	if (! job->limits[resource])
		NIH_MUST (job->limits[resource]
			  = nih_new (job, struct rlimit));


	/* Parse the soft limit value */
	arg = nih_config_next_arg (NULL, file, len, pos, lineno);
	if (! arg)
		return -1;

	job->limits[resource]->rlim_cur = strtoul (arg, &endptr, 10);
	if (*endptr) {
		nih_free (arg);

		nih_error_raise (CFG_ILLEGAL_VALUE,
				 _(CFG_ILLEGAL_VALUE_STR));
		return -1;
	}
	nih_free (arg);

	/* Parse the hard limit value */
	arg = nih_config_next_arg (NULL, file, len, pos, lineno);
	if (! arg)
		return -1;

	job->limits[resource]->rlim_max = strtoul (arg, &endptr, 10);
	if (*endptr) {
		nih_free (arg);

		nih_error_raise (CFG_ILLEGAL_VALUE,
				 _(CFG_ILLEGAL_VALUE_STR));
		return -1;
	}
	nih_free (arg);

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * cfg_stanza_chroot:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a chroot stanza from @file, extracting a single argument
 * containing a directory name.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_chroot (Job             *job,
		   NihConfigStanza *stanza,
		   const char      *file,
		   size_t           len,
		   size_t          *pos,
		   size_t          *lineno)
{
	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (job->chroot)
		nih_free (job->chroot);

	job->chroot = nih_config_next_arg (job, file, len, pos, lineno);
	if (! job->chroot)
		return -1;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * cfg_stanza_chdir:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a chdir stanza from @file, extracting a single argument
 * containing a directory name.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_chdir (Job             *job,
		  NihConfigStanza *stanza,
		  const char      *file,
		  size_t           len,
		  size_t          *pos,
		  size_t          *lineno)
{
	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (job->chdir)
		nih_free (job->chdir);

	job->chdir = nih_config_next_arg (job, file, len, pos, lineno);
	if (! job->chdir)
		return -1;

	return nih_config_skip_comment (file, len, pos, lineno);
}


/**
 * cfg_watch_dir:
 * @parent: parent of jobs,
 * @dirname: directory to watch,
 * @prefix: prefix to append to job names.
 *
 * Watch @dirname for creation or modification of configuration files or
 * sub-directories and parse them whenever they exist.  This also performs
 * the initial parsing of jobs in the directory.
 *
 * Jobs are named by joining @prefix and the name of the file under @dir,
 * @prefix may be NULL.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
cfg_watch_dir (const void *parent,
	       const char *dirname,
	       const char *prefix)
{
	DIR           *dir;
	struct dirent *ent;
	WatchInfo     *info;
	NihFileWatch  *watch;

	nih_assert (dirname != NULL);

	nih_info (_("Reading configuration from %s"), dirname);

	NIH_MUST (info = nih_new (NULL, WatchInfo));
	info->parent = parent;
	info->prefix = prefix ? nih_strdup (info, prefix) : NULL;

	/* FIXME we don't handle move yet */

	/* Add a watch so we can keep up to date */
	watch = nih_file_add_watch (NULL, dirname,
				    (IN_CREATE | IN_DELETE | IN_MODIFY),
				    (NihFileWatcher)cfg_watcher, info);
	if (! watch) {
		nih_free (info);
		return -1;
	}

	/* Read through any files already in the directory */
	dir = opendir (dirname);
	if (! dir) {
		nih_error_raise_system ();
		nih_free (info);
		return -1;
	}

	/* Just call the watcher function */
	while ((ent = readdir (dir)) != NULL)
		cfg_watcher (info, watch, IN_CREATE, ent->d_name);

	closedir (dir);

	return 0;
}

/**
 * cfg_watcher:
 * @info: watch information,
 * @watch: watch that generated the event,
 * @events: events that occurred,
 * @name: name of file that changed.
 *
 * This function is called whenever a configuration file directory we are
 * watching changes.  It arranges for the job to be parsed, or the new
 * directory to be watched.
 **/
static void
cfg_watcher (WatchInfo    *info,
	     NihFileWatch *watch,
	     uint32_t      events,
	     const char   *name)
{
	struct stat  statbuf;
	char        *filename, *jobname;

	nih_assert (watch != NULL);

	/* If this watch is now being ignored, free the info and watch */
	if (events & IN_IGNORED) {
		nih_debug ("Ceasing watching %s", watch->path);

		nih_list_free (&watch->entry);
		nih_free (info);
		return;
	}

	/* Otherwise name should be set and shouldn't begin . */
	if (! name)
		return;

	if ((name[0] == '\0') || strchr (name, '.') || strchr (name, '~')) {
		nih_debug ("Ignored %s/%s", watch->path, name);
		return;
	}

	/* FIXME better name checking required */

	/* FIXME we don't handle DELETE yet ... that should probably mark
	 * the running job as an instance or delete a stopped one
	 */
	if (events & IN_DELETE) {
		nih_debug ("Delete of %s/%s (ignored)", watch->path, name);
		return;
	}

	/* Construct filename and job name (also new prefix) */
	NIH_MUST (filename = nih_sprintf (NULL, "%s/%s", watch->path, name));
	if (info->prefix) {
		NIH_MUST (jobname = nih_sprintf (NULL, "%s/%s",
						 info->prefix, name));
	} else {
		NIH_MUST (jobname = nih_strdup (NULL, name));
	}

	/* Check we can stat it */
	if (stat (filename, &statbuf) < 0) {
		/* Bah, ignore the error */

	} else if (S_ISDIR (statbuf.st_mode)) {
		/* It's a directory, watch it */
		cfg_watch_dir (info->parent, filename, jobname);

	} else if (S_ISREG (statbuf.st_mode)) {
		/* It's a file, we parse it */
		cfg_read_job (info->parent, filename, jobname);

	}

	/* Free up */
	nih_free (jobname);
	nih_free (filename);
}
