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


#include <sys/types.h>
#include <sys/stat.h>

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
#include <nih/logging.h>
#include <nih/error.h>

#include <upstart/job.h>

#include "job.h"
#include "event.h"
#include "cfgfile.h"


/**
 * WS:
 *
 * Definition of what characters we consider whitespace.
 **/
#define WS " \t\r"

/**
 * CNL:
 *
 * Definition of what characters nominally end a line; a comment start
 * character or a newline.
 **/
#define CNL "#\n"

/**
 * CNLWS:
 *
 * Defintion of what characters nominally separate tokens.
 **/
#define CNLWS " \t\r#\n"

/**
 * WatchInfo:
 * @parent: parent for jobs,
 * @prefix: prefix for job names.
 *
 * Data pointed passed to the config file watcher function.
 **/
typedef struct watch_info {
	void *parent;
	char *prefix;
} WatchInfo;


/* Prototypes for static functions */
static void    cfg_job_stanza    (Job *job, const char *filename,
				  ssize_t *lineno, const char *file,
				  ssize_t len, ssize_t *pos);

static char ** cfg_parse_args    (void *parent, const char *filename,
				  ssize_t *lineno, const char *file,
				  ssize_t len, ssize_t *pos);
static char *  cfg_parse_command (void *parent, const char *filename,
				  ssize_t *lineno, const char *file,
				  ssize_t len, ssize_t *pos);
static ssize_t cfg_next_token    (const char *filename, ssize_t *lineno,
				  const char *file, ssize_t len, ssize_t *pos,
				  char *dest, const char *delim, int dequote);

static char *  cfg_parse_script  (void *parent, const char *filename,
				  ssize_t *lineno, const char *file,
				  ssize_t len, ssize_t *pos);
static ssize_t cfg_script_end    (ssize_t *lineno, const char *file,
				  ssize_t len, ssize_t *pos);

static void    cfg_watcher       (WatchInfo *info, NihFileWatch *watch,
				  uint32_t events, const char *name);


/**
 * cfg_read_job:
 * @parent: parent of returned job,
 * @filename: name of file to read,
 * @name: name to call job.
 *
 * Reads the @filename given and uses the information within to construct
 * a new job structure named @name which is returned.
 *
 * Returns: newly allocated job structure, or %NULL if the file was invalid.
 **/
Job *
cfg_read_job (void       *parent,
	      const char *filename,
	      const char *name)
{
	Job        *job, *old_job;
	const char *file;
	ssize_t     len, pos, lineno;

	nih_assert (filename != NULL);
	nih_assert (name != NULL);

	/* Look for an old job with that name */
	old_job = job_find_by_name (name);

	/* Map the file into memory */
	file = nih_file_map (filename, O_RDONLY | O_NOCTTY, (size_t *)&len);
	if (! file) {
		NihError *err;

		err = nih_error_get ();
		nih_error (_("%s: unable to read: %s"), filename,
			   err->message);
		nih_free (err);

		return NULL;
	}

	/* Allocate the job */
	NIH_MUST (job = job_new (parent, name));
	nih_debug ("Loading %s from %s", job->name, filename);

	/* Parse the file */
	pos = 0;
	lineno = 0;
	while (pos < len) {
		/* Skip initial whitespace */
		while ((pos < len) && strchr (WS, file[pos]))
			pos++;

		/* Ignore lines that are just comments */
		if ((pos < len) && (file[pos] == '#'))
			while ((pos < len) && (file[pos] != '\n'))
				pos++;

		/* Ignore blank lines */
		if ((pos < len) && (file[pos] == '\n')) {
			lineno++;
			pos++;
			continue;
		}

		cfg_job_stanza (job, filename, &lineno, file, len, &pos);
	}

	/* Finished with the file */
	nih_file_unmap ((void *) file, len);


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
 * cfg_job_stanza:
 * @job: job to fill in,
 * @filename: name of file being parsed,
 * @lineno: current line number,
 * @file: memory mapped copy of file, or string buffer,
 * @len: length of @file,
 * @pos: position to read from.
 *
 * Parses a job stanza at the current location of @file, @pos should point
 * to the start of the first token that contains the stanza name.
 *
 * @filename and @lineno are used to report warnings, and @lineno is
 * incremented each time a new line is discovered in the file.
 *
 * @pos is updated to point to the next line in the configuration or will be
 * past the end of the file.
 **/
static void
cfg_job_stanza (Job        *job,
		const char *filename,
		ssize_t    *lineno,
		const char *file,
		ssize_t     len,
		ssize_t    *pos)
{
	ssize_t   tok_start, tok_end, tok_len, args_start;
	char    **args, **arg;

	nih_assert (job != NULL);
	nih_assert (filename != NULL);
	nih_assert (lineno != NULL);
	nih_assert (file != NULL);
	nih_assert (len > 0);
	nih_assert (pos != NULL);

	/* Find the end of the first simple token */
	tok_start = *pos;
	while ((*pos < len) && (! strchr (CNLWS, file[*pos])))
		(*pos)++;
	tok_end = *pos;
	tok_len = tok_end - tok_start;

	/* Skip further whitespace */
	while ((*pos < len) && strchr (WS, file[*pos]))
		(*pos)++;

	/* Parse arguments */
	args_start = *pos;
	arg = args = cfg_parse_args (job, filename, lineno, file, len, pos);

	if (! strncmp (file + tok_start, "description", tok_len)) {
		/* description WS <job description>
		 *
		 * describes the job, used for start/stop messages, etc.
		 */
		if (*arg) {
			if (job->description)
				nih_free (job->description);

			NIH_MUST (job->description = nih_strdup (job, *arg));
		} else {
			nih_warn ("%s:%d: %s", filename, *lineno,
				  _("expected job description"));
		}

	} else if (! strncmp (file + tok_start, "author", tok_len)) {
		/* author WS <author name>
		 *
		 * allows the author to be mentioned, maybe useful for
		 * about boxes and the like
		 */
		if (*arg) {
			if (job->author)
				nih_free (job->author);

			NIH_MUST (job->author = nih_strdup (job, *arg));
		} else {
			nih_warn ("%s:%d: %s", filename, *lineno,
				  _("expected author name"));
		}

	} else if (! strncmp (file + tok_start, "version", tok_len)) {
		/* version WS <version string>
		 *
		 * gives the version of the job, again only really useful
		 * for sysadmins
		 */
		if (*arg) {
			if (job->version)
				nih_free (job->version);

			NIH_MUST (job->version = nih_strdup (job, *arg));
		} else {
			nih_warn ("%s:%d: %s", filename, *lineno,
				  _("expected version string"));
		}

	} else if (! strncmp (file + tok_start, "depends", tok_len)) {
		/* depends WS <job name>...
		 *
		 * names a job that must be running before this one will
		 * start
		 */
		if (! *arg) {
			nih_warn ("%s:%d: %s", filename, *lineno,
				  _("expected job name"));
		}

		for (arg = args; *arg; arg++) {
			JobName *dep;

			NIH_MUST (dep = nih_new (job, JobName));
			NIH_MUST (dep->name = nih_strdup (job, *arg));
			nih_list_init (&dep->entry);
			nih_list_add (&job->depends, &dep->entry);
		}

	} else if (! strncmp (file + tok_start, "on", tok_len)) {
		/* on WS <event>
		 *
		 * names an event that will cause the job to be started.
		 */
		if (*arg) {
			Event *event;

			NIH_MUST (event = event_new (job, *arg));
			nih_list_add (&job->start_events, &event->entry);
		} else {
			nih_warn ("%s:%d: %s", filename, *lineno,
				  _("expected event name"));
		}

	} else if (! strncmp (file + tok_start, "start", tok_len)) {
		/* start WS on FWS <event>
		 * start WS script ... end WS script
		 *
		 * names an event that will cause the job to be started.
		 *
		 * second form declares a script that will be run while
		 * the job is starting.
		 */
		if (*arg && (! strcmp (*arg, "on"))) {
			if (*++arg) {
				Event *event;

				NIH_MUST (event = event_new (job, *arg));
				nih_list_add (&job->start_events,
					      &event->entry);
			} else {
				nih_warn ("%s:%d: %s", filename, *lineno,
					  _("expected event name"));
			}

		} else if (*arg && (! strcmp (*arg, "script"))) {
			if (job->start_script)
				nih_free (job->start_script);

			if (*++arg) {
				arg = NULL;
				nih_warn ("%s:%d: %s", filename, *lineno,
					  _("ignored additional arguments"));
			}

			job->start_script = cfg_parse_script (
				job, filename, lineno, file, len, pos);

		} else {
			nih_warn ("%s:%d: %s", filename, *lineno,
				  _("expected 'on' or 'script'"));
		}

	} else if (! strncmp (file + tok_start, "stop", tok_len)) {
		/* stop WS on FWS <event>
		 * stop WS script ... end WS script
		 *
		 * names an event that will cause the job to be stopped.
		 *
		 * second form declares a script that will be run while
		 * the job is stopping.
		 */
		if (*arg && (! strcmp (*arg, "on"))) {
			if (*++arg) {
				Event *event;

				NIH_MUST (event = event_new (job, *arg));
				nih_list_add (&job->stop_events,
					      &event->entry);
			} else {
				nih_warn ("%s:%d: %s", filename, *lineno,
					  _("expected event name"));
			}

		} else if (*arg && (! strcmp (*arg, "script"))) {
			if (job->stop_script)
				nih_free (job->stop_script);

			if (*++arg) {
				arg = NULL;
				nih_warn ("%s:%d: %s", filename, *lineno,
					  _("ignored additional arguments"));
			}

			job->stop_script = cfg_parse_script (
				job, filename, lineno, file, len, pos);

		} else {
			nih_warn ("%s:%d: %s", filename, *lineno,
				  _("expected 'on' or 'script'"));
		}

	} else if (! strncmp (file + tok_start, "exec", tok_len)) {
		/* exec WS <command>
		 *
		 * gives the command and its arguments that will be
		 * executed
		 */
		char *cmd;

		arg = NULL;
		*pos = args_start;

		cmd = cfg_parse_command (job, NULL, NULL, file, len, pos);
		if (cmd) {
			if (job->command)
				nih_free (job->command);

			job->command = cmd;
		} else {
			nih_warn ("%s:%d: %s", filename, *lineno,
				  _("expected command"));
		}

	} else if (! strncmp (file + tok_start, "daemon", tok_len)) {
		/* daemon (WS <command>)?
		 *
		 * indicates that the process forks into the background
		 * so we need to grovel for its process id
		 *
		 * optionally may give the command and its arguments that
		 * will be executed
		 */
		char *cmd;

		arg = NULL;
		*pos = args_start;

		job->daemon = TRUE;

		cmd = cfg_parse_command (job, NULL, NULL, file, len, pos);
		if (cmd) {
			if (job->command)
				nih_free (job->command);

			job->command = cmd;
		}

	} else if (! strncmp (file + tok_start, "respawn", tok_len)) {
		/* respawn (WS <command>)?
		 * respawn WS limit FWS limit FWS interval
		 * respawn WS script ... end WS script
		 *
		 * indicates that the job should be respawned if it
		 * should terminate.
		 *
		 * optionally may give the command and its arguments that
		 * will be executed
		 *
		 * second form declares the maximum number of times in
		 * interval that the job may be respawned
		 *
		 * third form declares a script that will be run while
		 * the job is respawning.
		 */
		char *cmd;

		if (*arg && (! strcmp (*arg, "script"))) {
			if (job->respawn_script)
				nih_free (job->respawn_script);

			if (*++arg) {
				arg = NULL;
				nih_warn ("%s:%d: %s", filename, *lineno,
					  _("ignored additional arguments"));
			}

			job->respawn_script = cfg_parse_script (
				job, filename, lineno, file, len, pos);
		} else if (*args && (! strcmp (*arg, "limit"))) {
			char   *endptr;
			int     limit;
			time_t  interval;

			/* Parse the limit value */
			if (arg && *++arg) {
				limit = strtol (*arg, &endptr, 10);
				if (*endptr || (limit < 0)) {
					arg = NULL;
					nih_warn ("%s:%d: %s",
						  filename, *lineno,
						  _("illegal value"));
				}
			} else if (arg) {
				arg = NULL;
				nih_warn ("%s:%d: %s", filename, *lineno,
					  _("expected limit"));
			}

			/* Parse the interval */
			if (arg && *++arg) {
				interval = strtol (*arg, &endptr, 10);
				if (*endptr || (interval < 0)) {
					arg = NULL;
					nih_warn ("%s:%d: %s",
						  filename, *lineno,
						  _("illegal value"));
				}
			} else if (arg) {
				arg = NULL;
				nih_warn ("%s:%d: %s", filename, *lineno,
					  _("expected interval"));
			}

			/* If we got both values, assign them */
			if (arg) {
				job->respawn_limit = limit;
				job->respawn_interval = interval;
			}
		} else {
			job->respawn = TRUE;

			arg = NULL;
			*pos = args_start;

			cmd = cfg_parse_command (job, NULL, NULL,
						 file, len, pos);

			if (cmd) {
				if (job->command)
					nih_free (job->command);

				job->command = cmd;
			}
		}

	} else if (! strncmp (file + tok_start, "script", tok_len)) {
		/* script ... end WS script
		 *
		 * declares the script that will be executed
		 */
		if (job->script)
			nih_free (job->script);

		if (*arg) {
			arg = NULL;
			nih_warn ("%s:%d: %s", filename, *lineno,
				  _("ignored additional arguments"));
		}

		job->script = cfg_parse_script (job, filename, lineno,
						file, len, pos);

	} else if (! strncmp (file + tok_start, "instance", tok_len)) {
		/* instance
		 *
		 * declares that multiple instances of the job may be
		 * running at one time, starting spawns a new one
		 */
		job->spawns_instance = TRUE;

		if (*arg)
			nih_warn ("%s:%d: %s", filename, *lineno,
				  _("ignored additional arguments"));

	} else if (! strncmp (file + tok_start, "pid", tok_len)) {
		/* pid WS file FWS <filename>
		 * pid WS binary FWS <filename>
		 * pid WS timeout FWS <timeout>
		 *
		 * the first form specifies the filename that init
		 * should look for after the process has been spawned
		 * to obtain the pid
		 *
		 * the second form specifies the filename of a binary
		 * to find in /proc
		 *
		 * the third form specifies the timeout after spawning
		 * the process before giving up and assuming it didn't
		 * start
		 */
		if (*arg && (! strcmp (*arg, "file"))) {
			if (*++arg) {
				if (job->pidfile)
					nih_free (job->pidfile);

				NIH_MUST (job->pidfile
					  = nih_strdup (job, *arg));
			} else {
				nih_warn ("%s:%d: %s", filename, *lineno,
					  _("expected pid filename"));
			}
		} else if (*arg && (! strcmp (*arg, "binary"))) {
			if (*++arg) {
				if (job->binary)
					nih_free (job->binary);

				NIH_MUST (job->binary
					  = nih_strdup (job, *arg));
			} else {
				nih_warn ("%s:%d: %s", filename, *lineno,
					  _("expected binary filename"));
			}
		} else if (*arg && (! strcmp (*arg, "timeout"))) {
			if (*++arg) {
				char   *endptr;
				time_t  timeout;

				timeout = strtol (*arg, &endptr, 10);
				if (*endptr || (timeout < 0)) {
					nih_warn ("%s:%d: %s",
						  filename, *lineno,
						  _("illegal value"));
				} else {
					job->pid_timeout = timeout;
				}
			} else {
				nih_warn ("%s:%d: %s", filename, *lineno,
					  _("expected timeout"));
			}
		} else {
			nih_warn ("%s:%d: %s", filename, *lineno,
				  _("expected 'file', 'binary' or "
				    "'timeout'"));
		}

	} else if (! strncmp (file + tok_start, "kill", tok_len)) {
		/* kill WS timeout FWS <seconds>
		 *
		 * specifies the maximum amount of time that init should
		 * wait for a process to die after sending SIGTERM,
		 * before sending SIGKILL
		 */
		if (*arg && (! strcmp (*arg, "timeout"))) {
			if (*++arg) {
				char   *endptr;
				time_t  timeout;

				timeout = strtol (*arg, &endptr, 10);
				if (*endptr || (timeout < 0)) {
					nih_warn ("%s:%d: %s",
						  filename, *lineno,
						  _("illegal value"));
				} else {
					job->kill_timeout = timeout;
				}
			} else {
				nih_warn ("%s:%d: %s", filename, *lineno,
					  _("expected timeout"));
			}
		} else {
			nih_warn ("%s:%d: %s", filename, *lineno,
				  _("expected 'timeout'"));
		}

	} else if (! strncmp (file + tok_start, "normalexit", tok_len)) {
		/* normalexit FWS <status>...
		 *
		 * specifies the exit statuses that should not cause the
		 * process to be respawned
		 */
		if (! *arg) {
			nih_warn ("%s:%d: %s", filename, *lineno,
				  _("expected exit status"));
		}

		for (arg = args; *arg; arg++) {
			unsigned long  status;
			char          *endptr;

			status = strtoul (*arg, &endptr, 10);
			if (*endptr || (status > INT_MAX)) {
				nih_warn ("%s:%d: %s", filename, *lineno,
					  _("illegal value"));
			} else {
				int *new_ne;

				NIH_MUST (new_ne = nih_realloc (
						  job->normalexit, job,
						  sizeof (int) *
						  (job->normalexit_len + 1)));

				job->normalexit = new_ne;
				job->normalexit[job->normalexit_len++]
					= (int) status;
			}
		}

	} else if (! strncmp (file + tok_start, "console", tok_len)) {
		/* console WS logged
		 * console WS output
		 * console WS owner
		 * console WS none
		 *
		 * selects how the process's console will be set
		 */
		if (*arg) {
			if (! strcmp (*arg, "logged")) {
				job->console = CONSOLE_LOGGED;
			} else if (! strcmp (*arg, "output")) {
				job->console = CONSOLE_OUTPUT;
			} else if (! strcmp (*arg, "owner")) {
				job->console = CONSOLE_OWNER;
			} else if (! strcmp (*arg, "none")) {
				job->console = CONSOLE_NONE;
			} else {
				nih_warn ("%s:%d: %s", filename, *lineno,
					  _("expected 'logged', 'output', "
					    "'owner' or 'none'"));
			}
		} else {
			nih_warn ("%s:%d: %s", filename, *lineno,
				  _("expected 'logged', 'output', "
				    "'owner' or 'none'"));
		}

	} else if (! strncmp (file + tok_start, "env", tok_len)) {
		/* env WS <var>=<value>
		 *
		 * defines a variable that should be set in the job's
		 * environment, <value> may be optionally quoted.
		 */
		if (*arg && strchr (*arg, '=')) {
			char **e, *env;
			int    envc = 0;

			for (e = job->env; e && *e; e++)
				envc++;

			NIH_MUST (e = nih_realloc (job->env, job,
						  sizeof (char *) *
						   (envc + 2)));
			NIH_MUST (env = nih_strdup (e, *arg));

			job->env = e;
			job->env[envc++] = env;
			job->env[envc] = NULL;
		} else {
			nih_warn ("%s:%d: %s", filename, *lineno,
				  _("expected variable setting"));
		}

	} else if (! strncmp (file + tok_start, "umask", tok_len)) {
		/* umask WS <mask>
		 *
		 * specifies the file creation mask for the job
		 */
		if (*arg) {
			unsigned long  mask;
			char          *endptr;

			mask = strtol (*arg, &endptr, 8);
			if (*endptr || (mask & ~0777)) {
				nih_warn ("%s:%d: %s", filename, *lineno,
					  _("illegal value"));
			} else {
				job->umask = (mode_t) mask;
			}
		} else {
			nih_warn ("%s:%d: %s", filename, *lineno,
				  _("expected file creation mask"));
		}

	} else if (! strncmp (file + tok_start, "nice", tok_len)) {
		/* nice WS <nice level>
		 *
		 * specifies the process priority for the job
		 */
		if (*arg) {
			long  nice;
			char *endptr;

			nice = strtol (*arg, &endptr, 10);
			if (*endptr || (nice < -20) || (nice > 19)) {
				nih_warn ("%s:%d: %s", filename, *lineno,
					  _("illegal value"));
			} else {
				job->nice = (int) nice;
			}
		} else {
			nih_warn ("%s:%d: %s", filename, *lineno,
				  _("expected nice level"));
		}

	} else if (! strncmp (file + tok_start, "limit", tok_len)) {
		/* limit WS <limit name> FWS <soft> FWS <hard>
		 *
		 * specifies the resource limits for the job
		 */
		int     resource;
		rlim_t  soft, hard;
		char   *endptr;

		/* Parse the resource type */
		if (*arg) {
			if (! strcmp (*arg, "as")) {
				resource = RLIMIT_AS;
			} else if (! strcmp (*arg, "core")) {
				resource = RLIMIT_CORE;
			} else if (! strcmp (*arg, "cpu")) {
				resource = RLIMIT_CPU;
			} else if (! strcmp (*arg, "data")) {
				resource = RLIMIT_DATA;
			} else if (! strcmp (*arg, "fsize")) {
				resource = RLIMIT_FSIZE;
			} else if (! strcmp (*arg, "memlock")) {
				resource = RLIMIT_MEMLOCK;
			} else if (! strcmp (*arg, "msgqueue")) {
				resource = RLIMIT_MSGQUEUE;
			} else if (! strcmp (*arg, "nice")) {
				resource = RLIMIT_NICE;
			} else if (! strcmp (*arg, "nofile")) {
				resource = RLIMIT_NOFILE;
			} else if (! strcmp (*arg, "nproc")) {
				resource = RLIMIT_NPROC;
			} else if (! strcmp (*arg, "rss")) {
				resource = RLIMIT_RSS;
			} else if (! strcmp (*arg, "rtprio")) {
				resource = RLIMIT_RTPRIO;
			} else if (! strcmp (*arg, "sigpending")) {
				resource = RLIMIT_SIGPENDING;
			} else if (! strcmp (*arg, "stack")) {
				resource = RLIMIT_STACK;
			} else {
				arg = NULL;
				nih_warn ("%s:%d: %s", filename, *lineno,
					  _("unknown limit type"));
			}
		} else {
			arg = NULL;
			nih_warn ("%s:%d: %s", filename, *lineno,
				  _("expected limit name"));
		}

		/* Parse the soft limit value */
		if (arg && *++arg) {
			soft = strtoul (*arg, &endptr, 10);
			if (*endptr) {
				arg = NULL;
				nih_warn ("%s:%d: %s", filename, *lineno,
					  _("illegal value"));
			}
		} else if (arg) {
			arg = NULL;
			nih_warn ("%s:%d: %s", filename, *lineno,
				  _("expected soft limit"));
		}

		/* Parse the hard limit value */
		if (arg && *++arg) {
			hard = strtoul (*arg, &endptr, 10);
			if (*endptr) {
				arg = NULL;
				nih_warn ("%s:%d: %s", filename, *lineno,
					  _("illegal value"));
			}
		} else if (arg) {
			arg = NULL;
			nih_warn ("%s:%d: %s", filename, *lineno,
				  _("expected hard limit"));
		}

		/* If we got all the values, assign them */
		if (arg) {
			if (! job->limits[resource])
				NIH_MUST (job->limits[resource]
					  = nih_new (job, struct rlimit));

			job->limits[resource]->rlim_cur = soft;
			job->limits[resource]->rlim_max = hard;
		}

	} else if (! strncmp (file + tok_start, "chroot", tok_len)) {
		/* chroot WS <directory>
		 *
		 * specifies the root directory of the job
		 */
		if (*arg) {
			if (job->chroot)
				nih_free (job->chroot);

			NIH_MUST (job->chroot = nih_strdup (job, *arg));
		} else {
			nih_warn ("%s:%d: %s", filename, *lineno,
				  _("expected directory name"));
		}

	} else if (! strncmp (file + tok_start, "chdir", tok_len)) {
		/* chdir WS <directory>
		 *
		 * specifies the working directory of the job
		 */
		if (*arg) {
			if (job->chdir)
				nih_free (job->chdir);

			NIH_MUST (job->chdir = nih_strdup (job, *arg));
		} else {
			nih_warn ("%s:%d: %s", filename, *lineno,
				  _("expected directory name"));
		}

	} else {
		arg = NULL;
		nih_warn ("%s:%d: %s", filename, *lineno,
			  _("ignored unknown stanza"));
	}

	/* Check we handled all arguments */
	if (arg && *arg && *++arg) {
		nih_warn ("%s:%d: %s", filename, *lineno,
			  _("ignored additional arguments"));
	}

	nih_free (args);
}

/**
 * cfg_parse_args:
 * @parent: parent of returned array,
 * @filename: name of file being parsed,
 * @lineno: current line number,
 * @file: memory mapped copy of file, or string buffer,
 * @len: length of @file,
 * @pos: position to read from.
 *
 * Parses a list of arguments at the current location of @file.  @pos
 * should point at the start of the arguments.
 *
 * @filename and @lineno are used to report warnings, and @lineno is
 * incremented each time a new line is discovered in the file.
 *
 * @pos is updated to point to the next line in the configuration or will be
 * past the end of the file.
 *
 * Returns: the list of arguments found.
 **/
static char **
cfg_parse_args (void       *parent,
		const char *filename,
		ssize_t    *lineno,
		const char *file,
		ssize_t     len,
		ssize_t    *pos)
{
	char   **args;
	size_t   nargs;

	nih_assert (file != NULL);
	nih_assert (len > 0);
	nih_assert (pos != NULL);

	/* Begin with an empty array */
	NIH_MUST (args = nih_alloc (parent, sizeof (char *)));
	args[0] = NULL;
	nargs = 0;

	/* Loop through the arguments until we hit a comment or newline */
	while ((*pos < len) && (! strchr (CNL, file[*pos]))) {
		ssize_t   arg_start, arg_len, arg_end;
		char    **new_args, *arg;

		/* Grab the next argument */
		arg_start = *pos;
		arg_len = cfg_next_token (filename, lineno, file, len, pos,
					  NULL, CNLWS, TRUE);
		arg_end = *pos;

		/* Skip any amount of whitespace between them, we also
		 * need to detect an escaped newline here.
		 */
		while (*pos < len) {
			if (file[*pos] == '\\') {
				/* Escape character, only continue scanning
				 * if the next character is newline
				 */
				if ((len - *pos > 1)
				    && (file[*pos + 1] == '\n')) {
					(*pos)++;
				} else {
					break;
				}
			} else if (! strchr (WS, file[*pos])) {
				break;
			}

			/* Whitespace characer */
			(*pos)++;
		}

		/* Extend the array and allocate room for the args */
		NIH_MUST (new_args = nih_realloc (
				  args, parent,
				  sizeof (char *) * (nargs + 2)));
		NIH_MUST (arg = nih_alloc (new_args, arg_len + 1));

		args = new_args;
		args[nargs++] = arg;
		args[nargs] = NULL;

		/* Copy in the new token */
		cfg_next_token (NULL, NULL, file + arg_start,
				arg_end - arg_start, NULL, arg, CNLWS, TRUE);
	}

	/* Spool forwards until the end of the line */
	while ((*pos < len) && (file[*pos] != '\n'))
		(*pos)++;

	/* Step over it */
	if (*pos < len) {
		if (lineno)
			(*lineno)++;
		(*pos)++;
	}

	return args;
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
 * to the start of the command.
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

	nih_assert (file != NULL);
	nih_assert (len > 0);
	nih_assert (pos != NULL);

	/* Find the length of string up to the first unescaped comment
	 * or newline.
	 */
	cmd_start = *pos;
	cmd_len = cfg_next_token (filename, lineno, file, len, pos,
				  NULL, CNL, FALSE);
	cmd_end = *pos;

	/* Spool forwards until the end of the line */
	while ((*pos < len) && (file[*pos] != '\n'))
		(*pos)++;

	/* Step over it */
	if (*pos < len) {
		if (lineno)
			(*lineno)++;
		(*pos)++;
	}

	/* If there's nothing to copy, bail out now */
	if (! cmd_len)
		return NULL;


	/* Now copy the string into the destination. */
	NIH_MUST (cmd = nih_alloc (parent, cmd_len + 1));
	cfg_next_token (NULL, NULL, file + cmd_start, cmd_end - cmd_start,
			NULL, cmd, CNL, FALSE);

	return cmd;
}

/**
 * cfg_next_token:
 * @filename: name of file being parsed,
 * @lineno: line number,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @dest: destination to copy to,
 * @delim: characters to stop on,
 * @dequote: remove quotes and escapes.
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
 * If you also want quotes to be removed and escaped characters to be
 * replaced with the character itself, set @dequote to %TRUE.
 *
 * Returns: the length of the token as it was/would be copied into @dest.
 **/
static ssize_t
cfg_next_token (const char *filename,
		ssize_t    *lineno,
		const char *file,
		ssize_t     len,
		ssize_t    *pos,
		char       *dest,
		const char *delim,
		int         dequote)
{
	ssize_t  ws = 0, nlws = 0, qc = 0, i = 0, p, ret;
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
	 *   qc     number of quote characters that need removing.
	 */

	for (p = (pos ? *pos : 0); p < len; p++) {
		int extra = 0, isq = FALSE;

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
				if (dequote)
					qc++;
			}
		} else if (file[p] == '\\') {
			slash = TRUE;
			continue;
		} else if (quote) {
			if (file[p] == quote) {
				quote = 0;
				isq = TRUE;
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
			isq = TRUE;
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

		/* Extra characters (the slash) needs to be copied
		 * unless we're dequoting the string
		 */
		if (extra && dest && (! dequote)) {
			memcpy (dest + i, file + p - extra, extra);
			i += extra;
		}

		if (dest && (! (isq && dequote)))
			dest[i++] = file[p];

		if (isq && dequote)
			qc++;

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
			nih_warn ("%s:%d: %s", filename, *lineno + 1,
				  _("ignored trailing slash"));

		ws++;
	}

	/* Leaving quotes open is generally bad, close it at the last
	 * piece of whitespace (ie. do nothing :p)
	 */
	if (quote) {
		if (filename)
			nih_warn ("%s:%d: %s", filename, *lineno + 1,
				  _("unterminated quoted string"));
	}


	/* The return value is the length of the token with any newlines and
	 * surrounding whitespace converted to a single character and any
	 * trailing whitespace removed.
	 *
	 * The actual end of the text read is returned in *pos.
	 */
	ret = p - (pos ? *pos : 0) - ws - nlws - qc;
	if (pos)
		*pos = p;

	return ret;
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
 * @pos should point to the start of the shell script fragment, after the
 * opening stanza.
 *
 * @filename and @lineno are used to report warnings, and @lineno is
 * incremented each time a new line is discovered in the file.
 *
 * @pos is updated to point to the next line in the configuration or will be
 * past the end of the file.
 *
 * Returns: the script contained in the fragment.
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

	/* We need to find the end of the script which is a line that looks
	 * like:
	 *
	 * 	WS? end WS script CNLWS?
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
			sh_end = *pos;
			nih_warn ("%s:%d: %s", filename, *lineno,
				  _("'end script' expected"));
			break;
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
 * @prefix may be %NULL.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
cfg_watch_dir (void       *parent,
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
