/* upstart
 *
 * parse_job.c - job definition parsing
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


#include <sys/time.h>
#include <sys/resource.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/signal.h>
#include <nih/config.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <upstart/enum.h>

#include "job.h"
#include "event.h"
#include "parse_job.h"
#include "errors.h"


/* Prototypes for static functions */
static int parse_exec         (JobProcess *proc, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int parse_script       (JobProcess *proc, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int parse_process      (Job *job, ProcessType process,
			       NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));

static int stanza_exec        (Job *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_script      (Job *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_pre_start   (Job *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_post_start  (Job *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_pre_stop    (Job *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_post_stop   (Job *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_start       (Job *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_stop        (Job *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_description (Job *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_author      (Job *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_version     (Job *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_emits       (Job *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_daemon      (Job *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_respawn     (Job *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_service     (Job *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_instance    (Job *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_pid         (Job *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_kill        (Job *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_normal      (Job *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_console     (Job *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_env         (Job *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_umask       (Job *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_nice        (Job *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_limit       (Job *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_chroot      (Job *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_chdir       (Job *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));


/**
 * stanzas:
 *
 * This is the table of known job definition stanzas and the functions
 * that handle parsing them.
 **/
static NihConfigStanza stanzas[] = {
	{ "exec",        (NihConfigHandler)stanza_exec        },
	{ "script",      (NihConfigHandler)stanza_script      },
	{ "pre-start",   (NihConfigHandler)stanza_pre_start   },
	{ "post-start",  (NihConfigHandler)stanza_post_start  },
	{ "pre-stop",    (NihConfigHandler)stanza_pre_stop    },
	{ "post-stop",   (NihConfigHandler)stanza_post_stop   },
	{ "start",       (NihConfigHandler)stanza_start       },
	{ "stop",        (NihConfigHandler)stanza_stop        },
	{ "description", (NihConfigHandler)stanza_description },
	{ "author",      (NihConfigHandler)stanza_author      },
	{ "version",     (NihConfigHandler)stanza_version     },
	{ "emits",       (NihConfigHandler)stanza_emits       },
	{ "daemon",      (NihConfigHandler)stanza_daemon      },
	{ "respawn",     (NihConfigHandler)stanza_respawn     },
	{ "service",     (NihConfigHandler)stanza_service     },
	{ "instance",    (NihConfigHandler)stanza_instance    },
	{ "pid",         (NihConfigHandler)stanza_pid         },
	{ "kill",        (NihConfigHandler)stanza_kill        },
	{ "normal",      (NihConfigHandler)stanza_normal      },
	{ "console",     (NihConfigHandler)stanza_console     },
	{ "env",         (NihConfigHandler)stanza_env         },
	{ "umask",       (NihConfigHandler)stanza_umask       },
	{ "nice",        (NihConfigHandler)stanza_nice        },
	{ "limit",       (NihConfigHandler)stanza_limit       },
	{ "chroot",      (NihConfigHandler)stanza_chroot      },
	{ "chdir",       (NihConfigHandler)stanza_chdir       },

	NIH_CONFIG_LAST
};


/**
 * parse_job:
 * @parent: parent of new job,
 * @name: name of new job,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * This function is used to parse a job definition from @file, for a job
 * named @name.  A sequence of stanzas is expected, defining the parameters
 * of the job.
 *
 * If an existing job already exists with the given @name, and the new job
 * is parsed successfully, then the new job is marked as a replacement for
 * the old one.
 *
 * Returns: newly allocated Job structure on success, NULL on raised error.
 **/
Job *
parse_job (const void *parent,
	   const char *name,
	   const char *file,
	   size_t      len,
	   size_t     *pos,
	   size_t     *lineno)
{
 	Job *job, *old_job;

	nih_assert (name != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	/* Look for an old job with that name */
	old_job = job_find_by_name (name);

	/* Allocate a new structure */
	job = job_new (parent, name);
	if (! job)
		nih_return_system_error (NULL);

	/* Parse the file, if we can't parse the new file, we just return now
	 * without ditching the old job if there is one.
	 */
	if (nih_config_parse_file (file, len, pos, lineno, stanzas, job) < 0) {
		nih_list_free (&job->entry);
		return NULL;
	}

	/* Deal with the case where we're reloading an existing	job; we
	 * mark the existing job as deleted, rather than copying in old data,
	 * since we don't want to mis-match scripts or configuration.
	 */
	if (old_job) {
		nih_info (_("Replacing existing %s job"), job->name);

		if ((old_job->replacement != NULL)
		    && (old_job->replacement != (void *)-1)) {
			nih_debug ("Discarding previous replacement");
			nih_list_free (&old_job->replacement->entry);
		}

		old_job->replacement = job;
		job->replacement_for = old_job;

		if (job_should_replace (old_job))
			job_change_state (old_job, job_next_state (old_job));
	}

	return job;
}


/**
 * parse_exec:
 * @proc: job process being parsed.
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * This function is used to parse the arguments to a job process's exec
 * stanza from @file, the command and its arguments are expected to follow
 * and will be the command run for the job.
 *
 * The JobProcess for this to be parsed into should have already been
 * allocated.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
parse_exec (JobProcess      *proc,
	    NihConfigStanza *stanza,
	    const char      *file,
	    size_t           len,
	    size_t          *pos,
	    size_t          *lineno)
{
	nih_assert (proc != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (! nih_config_has_token (file, len, pos, lineno))
		nih_return_error (-1, NIH_CONFIG_EXPECTED_TOKEN,
				  _(NIH_CONFIG_EXPECTED_TOKEN_STR));

	if (proc->command)
		nih_free (proc->command);

	proc->script = FALSE;
	proc->command = nih_config_parse_command (proc, file, len,
						  pos, lineno);

	if (! proc->command)
		return -1;

	return 0;
}

/**
 * parse_script:
 * @proc: job process being parsed.
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * This function is used to parse a script block for a job process's script
 * stanza from @file.  A block terminated with "end script" is expected to
 * follow, and will be stored in the command for the job.
 *
 * The JobProcess for this to be parsed into should have already been
 * allocated.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
parse_script (JobProcess      *proc,
	      NihConfigStanza *stanza,
	      const char      *file,
	      size_t           len,
	      size_t          *pos,
	      size_t          *lineno)
{
	nih_assert (proc != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (nih_config_skip_comment (file, len, pos, lineno) < 0)
		return -1;

	if (proc->command)
		nih_free (proc->command);

	proc->script = TRUE;
	proc->command = nih_config_parse_block (proc, file, len,
						pos, lineno, "script");

	if (! proc->command)
		return -1;

	return 0;
}

/**
 * parse_process:
 * @job: job being parsed,
 * @process: which process is being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * This function is used to allocate a JobProcess for @process within @job,
 * and expects either "exec" or "script" to follow, calling parse_exec()
 * or parse_script() appropriately.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
parse_process (Job             *job,
	       ProcessType      process,
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

	arg = nih_config_next_token (NULL, file, len, pos, lineno,
				     NIH_CONFIG_CNLWS, FALSE);
	if (! arg)
		return -1;

	if (! job->process[process]) {
		job->process[process] = job_process_new (job->process);
		if (! job->process[process]) {
			nih_error_raise_system ();
			nih_free (arg);
			return -1;
		}
	}

	if (! strcmp (arg, "exec")) {
		nih_free (arg);

		return parse_exec (job->process[process], stanza,
				   file, len, pos, lineno);
	} else if (! strcmp (arg, "script")) {
		nih_free (arg);

		return parse_script (job->process[process], stanza,
				     file, len, pos, lineno);
	} else {
		nih_free (arg);

		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				  _(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}
}


/**
 * stanza_exec:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse an exec stanza from @file by allocating the main job process and
 * calling parse_exec() to parse it.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_exec (Job             *job,
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

	if (! job->process[PROCESS_MAIN]) {
		job->process[PROCESS_MAIN] = job_process_new (job->process);
		if (! job->process[PROCESS_MAIN])
			nih_return_system_error (-1);
	}

	return parse_exec (job->process[PROCESS_MAIN], stanza,
			   file, len, pos, lineno);
}

/**
 * stanza_script:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a script stanza from @file by allocating the main job process and
 * calling parse_script() to parse it.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_script (Job             *job,
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

	if (! job->process[PROCESS_MAIN]) {
		job->process[PROCESS_MAIN] = job_process_new (job->process);
		if (! job->process[PROCESS_MAIN])
			nih_return_system_error (-1);
	}

	return parse_script (job->process[PROCESS_MAIN], stanza,
			     file, len, pos, lineno);
}

/**
 * stanza_pre_start:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a pre-start stanza from @file by calling parse_process()
 * with PROCESS_PRE_START to parse it.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_pre_start (Job             *job,
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

	return parse_process (job, PROCESS_PRE_START, stanza,
			      file, len, pos, lineno);
}

/**
 * stanza_post_start:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a post-start stanza from @file by calling parse_process()
 * with PROCESS_POST_START to parse it.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_post_start (Job             *job,
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

	return parse_process (job, PROCESS_POST_START, stanza,
			      file, len, pos, lineno);
}

/**
 * stanza_pre_stop:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a pre-stop stanza from @file by calling parse_process()
 * with PROCESS_PRE_STOP to parse it.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_pre_stop (Job             *job,
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

	return parse_process (job, PROCESS_PRE_STOP, stanza,
			      file, len, pos, lineno);
}

/**
 * stanza_post_stop:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a post-stop stanza from @file by calling parse_process()
 * with PROCESS_POST_STOP to parse it.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_post_stop (Job             *job,
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

	return parse_process (job, PROCESS_POST_STOP, stanza,
			      file, len, pos, lineno);
}

/**
 * stanza_start:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a start stanza from @file.  This stanza expects a second "on"
 * argument, followed by an event which is allocated as an EventInfo structure
 * and stored in the start events list of the job.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_start (Job             *job,
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

	arg = nih_config_next_token (NULL, file, len, pos, lineno,
				     NIH_CONFIG_CNLWS, FALSE);
	if (! arg)
		return -1;

	if (! strcmp (arg, "on")) {
		EventInfo *event;
		char      *name;

		nih_free (arg);

		name = nih_config_next_arg (NULL, file, len, pos, lineno);
		if (! name)
			return -1;

		event = event_info_new (job, name, NULL, NULL);
		if (! event) {
			nih_error_raise_system ();
			nih_free (name);
			return -1;
		}
		nih_free (name);

		event->args = nih_config_parse_args (event, file, len,
						     pos, lineno);
		if (! event->args) {
			nih_free (event);
			return -1;
		}

		nih_list_add (&job->start_events, &event->entry);

		return 0;

	} else {
		nih_free (arg);

		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				  _(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}
}

/**
 * stanza_stop:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a stop stanza from @file.  This stanza expects a second "on"
 * argument, followed by an event which is allocated as an EventInfo structure
 * and stored in the stop events list of the job.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_stop (Job             *job,
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

	arg = nih_config_next_token (NULL, file, len, pos, lineno,
				     NIH_CONFIG_CNLWS, FALSE);
	if (! arg)
		return -1;

	if (! strcmp (arg, "on")) {
		EventInfo *event;
		char      *name;

		nih_free (arg);

		name = nih_config_next_arg (NULL, file, len, pos, lineno);
		if (! name)
			return -1;

		event = event_info_new (job, name, NULL, NULL);
		if (! event) {
			nih_error_raise_system ();
			nih_free (name);
			return -1;
		}
		nih_free (name);

		event->args = nih_config_parse_args (event, file, len,
						     pos, lineno);
		if (! event->args) {
			nih_free (event);
			return -1;
		}

		nih_list_add (&job->stop_events, &event->entry);

		return 0;

	} else {
		nih_free (arg);

		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				  _(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}
}

/**
 * stanza_description:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a description stanza from @file.  This stanza expects a single
 * argument giving a human-readable description of the job which is
 * stored for later use.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_description (Job             *job,
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
 * stanza_author:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse an author stanza from @file.  This stanza expects a single
 * argument giving a human-readable author name for the job which is
 * stored for later use.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_author (Job             *job,
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
 * stanza_version:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a version stanza from @file.  This stanza expects a single
 * argument giving a human-readable version number for the job which is
 * stored for later use.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_version (Job             *job,
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
 * stanza_emits:
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
 * Arguments are allocated as EventInfo structures and stored in the emits
 * list of the job.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_emits (Job             *job,
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
		EventInfo *event;

		event = event_info_new (job, *arg, NULL, NULL);
		if (! event) {
			nih_error_raise_system ();
			nih_free (args);
			return -1;
		}

		nih_list_add (&job->emits, &event->entry);
	}

	nih_free (args);

	return 0;
}

/**
 * stanza_daemon:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a daemon stanza from @file.  This sets the daemon flag for the
 * job and has no arguments.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_daemon (Job             *job,
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

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * stanza_respawn:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a daemon stanza from @file.  This either has no arguments, in
 * which case it sets the respawn and service flags for the job, or it has
 * the "limit" argument and sets the respawn rate limit.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_respawn (Job             *job,
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

	/* Deal with the no-argument form first */
	if (! nih_config_has_token (file, len, pos, lineno)) {
		job->respawn = TRUE;
		job->service = TRUE;

		return nih_config_skip_comment (file, len, pos, lineno);
	}


	/* Take the next argument, a sub-stanza keyword. */
	arg = nih_config_next_token (NULL, file, len, pos, lineno,
				     NIH_CONFIG_CNLWS, FALSE);
	if (! arg)
		return -1;

	if (! strcmp (arg, "limit")) {
		char *endptr;

		nih_free (arg);

		/* Parse the limit value */
		arg = nih_config_next_arg (NULL, file, len, pos, lineno);
		if (! arg)
			return -1;

		if (strcmp (arg, "unlimited")) {
			job->respawn_limit = strtol (arg, &endptr, 10);
			if (*endptr || (job->respawn_limit < 0)) {
				nih_free (arg);

				nih_return_error (-1, PARSE_ILLEGAL_LIMIT,
						  _(PARSE_ILLEGAL_LIMIT_STR));
			}
			nih_free (arg);

			/* Parse the timeout value */
			arg = nih_config_next_arg (NULL, file, len, pos, lineno);
			if (! arg)
				return -1;

			job->respawn_interval = strtol (arg, &endptr, 10);
			if (*endptr || (job->respawn_interval < 0)) {
				nih_free (arg);

				nih_return_error (-1, PARSE_ILLEGAL_INTERVAL,
						  _(PARSE_ILLEGAL_INTERVAL_STR));
			}
		} else {
			job->respawn_limit = 0;
			job->respawn_interval = 0;
		}

		nih_free (arg);

		return nih_config_skip_comment (file, len, pos, lineno);

	} else {
		nih_free (arg);

		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				  _(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}
}

/**
 * stanza_service:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a service stanza from @file.  This sets the service flag for the
 * job, and takes no further arguments.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_service (Job             *job,
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

	job->service = TRUE;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * stanza_instance:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse an instance stanza from @file, this expects no arguments and
 * simply sets the instance flag in the job.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_instance (Job             *job,
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

	job->instance = TRUE;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * stanza_pid:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a pid stanza from @file.  This stanza expects an second-level
 * stanza argument indicating which job parameter to set, followed by
 * an argument that sets that.  All are related to discovering the pid
 * of a forked daemon.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_pid (Job             *job,
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

	arg = nih_config_next_token (NULL, file, len, pos, lineno,
				     NIH_CONFIG_CNLWS, FALSE);
	if (! arg)
		return -1;

	if (! strcmp (arg, "file")) {
		nih_free (arg);

		if (job->pid_file)
			nih_free (job->pid_file);

		job->pid_file = nih_config_next_arg (job, file, len,
						     pos, lineno);
		if (! job->pid_file)
			return -1;

		return nih_config_skip_comment (file, len, pos, lineno);

	} else if (! strcmp (arg, "binary")) {
		nih_free (arg);

		if (job->pid_binary)
			nih_free (job->pid_binary);

		job->pid_binary = nih_config_next_arg (job, file, len,
						       pos, lineno);
		if (! job->pid_binary)
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

			nih_return_error (-1, PARSE_ILLEGAL_INTERVAL,
					  _(PARSE_ILLEGAL_INTERVAL_STR));
		}
		nih_free (arg);

		return nih_config_skip_comment (file, len, pos, lineno);

	} else {
		nih_free (arg);

		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				  _(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}
}

/**
 * stanza_kill:
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
stanza_kill (Job             *job,
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

	arg = nih_config_next_token (NULL, file, len, pos, lineno,
				     NIH_CONFIG_CNLWS, FALSE);
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

			nih_return_error (-1, PARSE_ILLEGAL_INTERVAL,
					  _(PARSE_ILLEGAL_INTERVAL_STR));
		}
		nih_free (arg);

		return nih_config_skip_comment (file, len, pos, lineno);

	} else {
		nih_free (arg);

		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				  _(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}
}

/**
 * stanza_normal:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a normal stanza from @file.  This stanza expects a single "exit"
 * argument, followed by one or more arguments giving signal names or
 * exit codes that the main process can return and be considered to have been
 * stopped normally.
 *
 * Arguments are stored in the normalexit array, and the normalexit_len
 * value updated.  Signals are stored in the higher bytes.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_normal (Job             *job,
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

	arg = nih_config_next_token (NULL, file, len, pos, lineno,
				     NIH_CONFIG_CNLWS, FALSE);
	if (! arg)
		return -1;

	if (! strcmp (arg, "exit")) {
		nih_free (arg);

		do {
			unsigned long  status;
			char          *endptr;
			int           *new_ne, signum;

			arg = nih_config_next_arg (NULL, file, len,
						   pos, lineno);
			if (! arg)
				return -1;

			signum = nih_signal_from_name (arg);
			if (signum < 0) {
				status = strtoul (arg, &endptr, 10);
				if (*endptr || (status > INT_MAX)) {
					nih_free (arg);
					nih_return_error (-1, PARSE_ILLEGAL_EXIT,
							  _(PARSE_ILLEGAL_EXIT_STR));
				}
			} else {
				status = signum << 8;
			}

			nih_free (arg);

			new_ne = nih_realloc (job->normalexit, job,
					      sizeof (int) * (job->normalexit_len + 1));
			if (! new_ne)
				nih_return_system_error (-1);

			job->normalexit = new_ne;
			job->normalexit[job->normalexit_len++] = (int) status;
		} while (nih_config_has_token (file, len, pos, lineno));

		return nih_config_skip_comment (file, len, pos, lineno);
	} else {
		nih_free (arg);

		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				  _(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}
}

/**
 * stanza_console:
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
stanza_console (Job             *job,
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

		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				  _(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}

	nih_free (arg);

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * stanza_env:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse an env stanza from @file, extracting a single argument of the form
 * VAR=VALUE.  These are stored in the env array, which is increased in
 * size to accomodate the new value.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_env (Job             *job,
	    NihConfigStanza *stanza,
	    const char      *file,
	    size_t           len,
	    size_t          *pos,
	    size_t          *lineno)
{
	char  *env;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	env = nih_config_next_arg (job->env, file, len, pos, lineno);
	if (! env)
		return -1;

	if (! nih_str_array_addp (&job->env, job, NULL, env)) {
		nih_error_raise_system ();
		nih_free (env);
		return -1;
	}

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * stanza_umask:
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
stanza_umask (Job             *job,
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

		nih_return_error (-1, PARSE_ILLEGAL_UMASK,
				  _(PARSE_ILLEGAL_UMASK_STR));
	}
	nih_free (arg);

	job->umask = (mode_t)mask;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * stanza_nice:
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
stanza_nice (Job             *job,
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

		nih_return_error (-1, PARSE_ILLEGAL_NICE,
				  _(PARSE_ILLEGAL_NICE_STR));
	}
	nih_free (arg);

	job->nice = (int)nice;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * stanza_limit:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a limit stanza from @file, extracting a second-level stanza that
 * states which limit to set from its two following arguments.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_limit (Job             *job,
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

		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				  _(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}

	nih_free (arg);


	if (! job->limits[resource]) {
		job->limits[resource] = nih_new (job, struct rlimit);
		if (! job->limits[resource])
			nih_return_system_error (-1);
	}

	/* Parse the soft limit value */
	arg = nih_config_next_arg (NULL, file, len, pos, lineno);
	if (! arg)
		return -1;

	if (strcmp (arg, "unlimited")) {
		job->limits[resource]->rlim_cur = strtoul (arg, &endptr, 10);
		if (*endptr) {
			nih_free (arg);

			nih_return_error (-1, PARSE_ILLEGAL_LIMIT,
					  _(PARSE_ILLEGAL_LIMIT_STR));
		}
	} else {
		job->limits[resource]->rlim_cur = RLIM_INFINITY;
	}
	nih_free (arg);

	/* Parse the hard limit value */
	arg = nih_config_next_arg (NULL, file, len, pos, lineno);
	if (! arg)
		return -1;

	if (strcmp (arg, "unlimited")) {
		job->limits[resource]->rlim_max = strtoul (arg, &endptr, 10);
		if (*endptr) {
			nih_free (arg);

			nih_return_error (-1, PARSE_ILLEGAL_LIMIT,
					  _(PARSE_ILLEGAL_LIMIT_STR));
		}
	} else {
		job->limits[resource]->rlim_max = RLIM_INFINITY;
	}
	nih_free (arg);

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * stanza_chroot:
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
stanza_chroot (Job             *job,
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
 * stanza_chdir:
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
stanza_chdir (Job             *job,
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
