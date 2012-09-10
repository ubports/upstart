/* upstart
 *
 * parse_job.c - job definition parsing
 *
 * Copyright © 2010,2011 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <sys/time.h>
#include <sys/resource.h>

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/signal.h>
#include <nih/logging.h>
#include <nih/error.h>

#include "process.h"
#include "job_class.h"
#include "event.h"
#include "parse_job.h"
#include "errors.h"


/* Prototypes for static functions */
static int            parse_exec        (Process *process,
					 NihConfigStanza *stanza,
					 const char *file, size_t len,
					 size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int            parse_script      (Process *process,
					 NihConfigStanza *stanza,
					 const char *file, size_t len,
					 size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int            parse_process     (JobClass *class, ProcessType process,
					 NihConfigStanza *stanza,
					 const char *file, size_t len,
					 size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static EventOperator *parse_on          (JobClass *class,
					 NihConfigStanza *stanza,
					 const char *file, size_t len,
					 size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result, malloc));
static int            parse_on_operator (JobClass *class,
					 NihConfigStanza *stanza,
					 const char *file, size_t len,
					 size_t *pos, size_t *lineno,
					 NihList *stack, EventOperator **root)
	__attribute__ ((warn_unused_result));
static int            parse_on_paren    (JobClass *class,
					 NihConfigStanza *stanza,
					 const char *file, size_t len,
					 size_t *pos, size_t *lineno,
					 NihList *stack, EventOperator **root,
					 size_t *paren)
	__attribute__ ((warn_unused_result));
static int            parse_on_operand  (JobClass *class,
					 NihConfigStanza *stanza,
					 const char *file, size_t len,
					 size_t *pos, size_t *lineno,
					 NihList *stack, EventOperator **root)
	__attribute__ ((warn_unused_result));
static int            parse_on_collect  (JobClass *class,
					 NihList *stack, EventOperator **root)
	__attribute__ ((warn_unused_result));

static int stanza_instance    (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));

static int stanza_description (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_author      (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_version     (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));

static int stanza_env         (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_export      (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));

static int stanza_start       (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_stop        (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_emits       (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));

static int stanza_exec        (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_script      (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_pre_start   (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_post_start  (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_pre_stop    (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_post_stop   (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));

static int stanza_expect      (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_task        (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));

static int stanza_kill        (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));

static int stanza_respawn     (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_normal      (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));

static int stanza_console     (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));

static int stanza_umask       (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_nice        (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_oom         (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_limit       (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_chroot      (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_chdir       (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_setuid      (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_setgid      (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_debug       (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_manual      (JobClass *class, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_usage       (JobClass *class, NihConfigStanza *stanza,
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
	{ "instance",    (NihConfigHandler)stanza_instance    },
	{ "description", (NihConfigHandler)stanza_description },
	{ "author",      (NihConfigHandler)stanza_author      },
	{ "version",     (NihConfigHandler)stanza_version     },
	{ "env",         (NihConfigHandler)stanza_env         },
	{ "export",      (NihConfigHandler)stanza_export      },
	{ "start",       (NihConfigHandler)stanza_start       },
	{ "stop",        (NihConfigHandler)stanza_stop        },
	{ "emits",       (NihConfigHandler)stanza_emits       },
	{ "exec",        (NihConfigHandler)stanza_exec        },
	{ "script",      (NihConfigHandler)stanza_script      },
	{ "pre-start",   (NihConfigHandler)stanza_pre_start   },
	{ "post-start",  (NihConfigHandler)stanza_post_start  },
	{ "pre-stop",    (NihConfigHandler)stanza_pre_stop    },
	{ "post-stop",   (NihConfigHandler)stanza_post_stop   },
	{ "expect",      (NihConfigHandler)stanza_expect      },
	{ "task",        (NihConfigHandler)stanza_task        },
	{ "kill",        (NihConfigHandler)stanza_kill        },
	{ "respawn",     (NihConfigHandler)stanza_respawn     },
	{ "normal",      (NihConfigHandler)stanza_normal      },
	{ "console",     (NihConfigHandler)stanza_console     },
	{ "umask",       (NihConfigHandler)stanza_umask       },
	{ "nice",        (NihConfigHandler)stanza_nice        },
	{ "oom",         (NihConfigHandler)stanza_oom         },
	{ "limit",       (NihConfigHandler)stanza_limit       },
	{ "chroot",      (NihConfigHandler)stanza_chroot      },
	{ "chdir",       (NihConfigHandler)stanza_chdir       },
	{ "setuid",      (NihConfigHandler)stanza_setuid      },
	{ "setgid",      (NihConfigHandler)stanza_setgid      },
	{ "debug",       (NihConfigHandler)stanza_debug       },
	{ "manual",      (NihConfigHandler)stanza_manual      },
	{ "usage",       (NihConfigHandler)stanza_usage       },

	NIH_CONFIG_LAST
};


/**
 * parse_job:
 * @parent: parent object for new job,
 * @session: session,
 * @update: If not NULL, update the existing specified JobClass,
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
 * If @parent is not NULL, it should be a pointer to another object which
 * will be used as a parent for the returned job.  When all parents
 * of the returned job are freed, the returned job will also be
 * freed.
 *
 * Returns: if @update is NULL, returns new JobClass structure on success, NULL on raised error.
 * If @update is not NULL, returns @update or NULL on error.
 **/
JobClass *
parse_job (const void *parent,
	   Session    *session,
	   JobClass   *update,
	   const char *name,
	   const char *file,
	   size_t      len,
	   size_t     *pos,
	   size_t     *lineno)
{
 	JobClass *class;

	nih_assert (name != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (update) {
		class = update;
		nih_debug ("Reusing JobClass %s (%s)",
				class->name, class->path);
	} else {
		nih_debug ("Creating new JobClass %s",
			  name);
		class = job_class_new (parent, name, session);
		if (! class)
			nih_return_system_error (NULL);
	}

	if (nih_config_parse_file (file, len, pos, lineno,
				stanzas, class) < 0) {
		if (!update)
			nih_free (class);
		return NULL;
	}

	return class;
}


/**
 * parse_exec:
 * @process: process being parsed.
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * This function is used to parse the arguments to a process's exec
 * stanza from @file, the command and its arguments are expected to follow
 * and will be the command run for the job.
 *
 * The Process for this to be parsed into should have already been
 * allocated.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
parse_exec (Process         *process,
	    NihConfigStanza *stanza,
	    const char      *file,
	    size_t           len,
	    size_t          *pos,
	    size_t          *lineno)
{
	nih_assert (process != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (! nih_config_has_token (file, len, pos, lineno))
		nih_return_error (-1, NIH_CONFIG_EXPECTED_TOKEN,
				  _(NIH_CONFIG_EXPECTED_TOKEN_STR));

	if (process->command)
		nih_unref (process->command, process);

	process->script = FALSE;
	process->command = nih_config_parse_command (process, file, len,
						     pos, lineno);

	if (! process->command)
		return -1;

	return 0;
}

/**
 * parse_script:
 * @process: process being parsed.
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * This function is used to parse a script block for a process's script
 * stanza from @file.  A block terminated with "end script" is expected to
 * follow, and will be stored in the command for the job.
 *
 * The Process for this to be parsed into should have already been
 * allocated.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
parse_script (Process         *process,
	      NihConfigStanza *stanza,
	      const char      *file,
	      size_t           len,
	      size_t          *pos,
	      size_t          *lineno)
{
	nih_assert (process != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (nih_config_skip_comment (file, len, pos, lineno) < 0)
		return -1;

	if (process->command)
		nih_unref (process->command, process);

	process->script = TRUE;
	process->command = nih_config_parse_block (process, file, len,
						   pos, lineno, "script");

	if (! process->command)
		return -1;

	return 0;
}

/**
 * parse_process:
 * @job: job class being parsed,
 * @process: which process is being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * This function is used to allocate a Process for @process within @class,
 * and expects either "exec" or "script" to follow, calling parse_exec()
 * or parse_script() appropriately.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
parse_process (JobClass        *class,
	       ProcessType      process,
	       NihConfigStanza *stanza,
	       const char      *file,
	       size_t           len,
	       size_t          *pos,
	       size_t          *lineno)
{
	nih_local char *arg = NULL;
	size_t          a_pos, a_lineno;
	int             ret = -1;

	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	/* Allocate a new Process structure if we need to */
	if (! class->process[process]) {
		class->process[process] = process_new (class->process);
		if (! class->process[process])
			nih_return_system_error (-1);
	}

	a_pos = *pos;
	a_lineno = (lineno ? *lineno:  1);

	/* Parse the next argument to find out what type of process this is */
	arg = nih_config_next_token (NULL, file, len, &a_pos, &a_lineno,
				     NIH_CONFIG_CNLWS, FALSE);
	if (! arg)
		goto finish;

	if (! strcmp (arg, "exec")) {
		ret = parse_exec (class->process[process], stanza,
				  file, len, &a_pos, &a_lineno);
	} else if (! strcmp (arg, "script")) {
		ret = parse_script (class->process[process], stanza,
				    file, len, &a_pos, &a_lineno);
	} else {
		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				  _(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}

finish:
	*pos = a_pos;
	if (lineno)
		*lineno = a_lineno;

	return ret;
}


/**
 * parse_on:
 * @class: job class being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * This function is used to parse the arguments to an "on" stanza as an
 * event expression.  Names and arguments to events, intermixed with
 * operators and grouped by parentheses are expected to follow and are
 * allocated as a tree of EventOperator structures, the root of which is
 * returned.
 *
 * Returns: EventOperator at root of expression tree on success, NULL
 * on raised error.
 **/
static EventOperator *
parse_on (JobClass        *class,
	  NihConfigStanza *stanza,
	  const char      *file,
	  size_t           len,
	  size_t          *pos,
	  size_t          *lineno)
{
	NihList        stack;
	EventOperator *root = NULL;
	size_t         on_pos, on_lineno, paren = 0;

	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	nih_list_init (&stack);

	on_pos = *pos;
	on_lineno = (lineno ? *lineno : 1);

	/* Parse all of the tokens that we find following the configuration
	 * stanza; unlike other stanzas we happily parse multiple lines
	 * provided that we're inside parens, and we permit comments at the
	 * end of those lines.
	 */
	do {
		nih_config_skip_whitespace (file, len, &on_pos, &on_lineno);

		do {
			/* Update error position */
			*pos = on_pos;
			if (lineno)
				*lineno = on_lineno;

			/* Peek at the first character, since open and
			 * close parens aren't picked up by our normal
			 * tokeniser.
			 */
			if ((*pos < len) && strchr ("()", file[*pos])) {
				if (parse_on_paren (class, stanza, file, len,
						    &on_pos, &on_lineno,
						    &stack, &root,
						    &paren) < 0) {
					root = NULL;
					goto finish;
				}

			/* Otherwise it's either an operator or operand;
			 * parse it as an operator first, that function
			 * handles transfer to parse_on_operand() in the
			 * case of unrecognised token.
			 */
			} else if (parse_on_operator (class, stanza, file, len,
						      &on_pos, &on_lineno,
						      &stack, &root) < 0) {
				root = NULL;
				goto finish;
			}

		} while (nih_config_has_token (file, len,
					       &on_pos, &on_lineno));

		if (nih_config_skip_comment (file, len,
					     &on_pos, &on_lineno) < 0)
			nih_assert_not_reached ();
	} while ((on_pos < len) && paren);

	/* The final operator and operand should be still on the stack and
	 * need collecting; if not, take the stack pointer out before
	 * returning otherwise we'll try and access it.
	 */
	if (parse_on_collect (class, &stack, &root) < 0) {
		nih_list_remove (&stack);
		return NULL;
	}

	/* If the stack isn't empty, then we've hit an open parenthesis and
	 * not found a matching close one.  We've probably parsed the entire
	 * file by accident!
	 */
	if (! NIH_LIST_EMPTY (&stack)) {
		nih_error_raise (PARSE_MISMATCHED_PARENS,
				 _(PARSE_MISMATCHED_PARENS_STR));
		root = NULL;
		goto finish;
	}


finish:
	/* Remove the stack pointer from the list of items, otherwise we'll
	 * return it and we'll try and access it when freed.
	 */
	nih_list_remove (&stack);

	*pos = on_pos;
	if (lineno)
		*lineno = on_lineno;

	return root;
}

/**
 * parse_on_simple:
 * @class: job class being parsed,
 * @stanza_name: name of stanza type to parse ("start" or "stop"),
 * @string: string to parse.
 *
 * Parse either a "start" or "stop" condition from @string (which must
 * start with the first byte beyond either "start on" or "stop on".
 *
 * Returns: EventOperator at root of expression tree on success, NULL
 * on raised error.
 **/
EventOperator *
parse_on_simple (JobClass *class, const char *stanza_name, const char *string)
{
	EventOperator    *root = NULL;
	NihConfigStanza  *stanza;
	size_t            pos = 0;
	size_t            lineno = 0;
	size_t            len;

	nih_assert (class);
	nih_assert (stanza_name);
	nih_assert (string);

	/* Find the appropriate config stanza */
	for (NihConfigStanza *s = stanzas; s->name; s++) {
		if (! strcmp (stanza_name, s->name)) {
			stanza = s;
			break;
		}
	}

	nih_assert (stanza);

	len = strlen (string);

	root = parse_on (class, stanza, string, len, &pos, &lineno);

	return root;
}

/**
 * parse_on_operator:
 * @class: job class being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number,
 * @stack: input operator stack,
 * @root: output operator.
 *
 * This function parses a single token from the arguments of the "on"
 * stanza.  If the token is not a valid operator, this will call
 * parse_on_operand() instead.
 *
 * Operators are pushed onto @stack after collecting any existing operators
 * and operands on the stack, and placing them as the operator's left child.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
parse_on_operator (JobClass         *class,
		   NihConfigStanza  *stanza,
		   const char       *file,
		   size_t            len,
		   size_t           *pos,
		   size_t           *lineno,
		   NihList          *stack,
		   EventOperator   **root)
{
	size_t             a_pos, a_lineno;
	nih_local char    *arg = NULL;
	EventOperatorType  type;
	EventOperator     *oper;
	NihListEntry      *item;
	int                ret = -1;

	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);
	nih_assert (stack != NULL);
	nih_assert (root != NULL);

	/* Get the next token to see whether it's an operator keyword that
	 * we recognise, don't dequote since this allows people to quote
	 * operators to turn them into ordinary operands.
	 */
	a_pos = *pos;
	a_lineno = (lineno ? *lineno : 1);

	arg = nih_config_next_token (NULL, file, len, &a_pos, &a_lineno,
				     "()" NIH_CONFIG_CNLWS, FALSE);
	if (! arg)
		goto finish;

	/* Compare token against known operators; if it isn't, then rewind
	 * back to the starting position and deal with it as an operand.
	 */
	if (! strcmp (arg, "and")) {
		type = EVENT_AND;
	} else if (! strcmp (arg, "or")) {
		type = EVENT_OR;
	} else {
		return parse_on_operand (class, stanza, file, len, pos, lineno,
					 stack, root);
	}

	/* Before we push the new operator onto the stack, we need to collect
	 * any existing operators and operands.
	 */
	if (parse_on_collect (class, stack, root) < 0)
		return -1;

	/* Create the new operator, placing the existing root node as its
	 * left-hand child.
	 */
	oper = event_operator_new (class, type, NULL, NULL);
	if (! oper)
		nih_return_system_error (-1);

	nih_ref (*root, oper);
	nih_unref (*root, class);

	nih_tree_add (&oper->node, &(*root)->node, NIH_TREE_LEFT);
	*root = NULL;

	/* Push the new operator onto the stack */
	item = nih_list_entry_new (class);
	if (! item)
		nih_return_system_error (-1);

	item->data = oper;
	nih_list_add_after (stack, &item->entry);

	ret = 0;

finish:
	*pos = a_pos;
	if (lineno)
		*lineno = a_lineno;

	return ret;
}

/**
 * parse_on_paren:
 * @class: job class being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number,
 * @stack: input operator stack,
 * @root: output operator,
 * @paren: number of nested parentheses.
 *
 * This function deals with an open or close parenthesis in the arguments
 * of the "on" stanza; it must only be called when the character at the
 * current position is either.
 *
 * @paren is incremented for each open parenthesis, and decremented for
 * each close one.  This is a gross check for whether the parsing is
 * currently within a grouping, and used by parse_on() to ignore newlines
 * within them.
 *
 * An open parenthesis pushes a NULL operator onto the stack, this stops
 * parse_on_collect() from collecting beyond it.
 *
 * A close parenthesis collects all operators on the stack up to the
 * first (matching) marker, and removes the marker.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
parse_on_paren (JobClass         *class,
		NihConfigStanza  *stanza,
		const char       *file,
		size_t            len,
		size_t           *pos,
		size_t           *lineno,
		NihList          *stack,
		EventOperator   **root,
		size_t           *paren)
{
	NihListEntry  *item;
	EventOperator *oper;

	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);
	nih_assert (stack != NULL);
	nih_assert (root != NULL);
	nih_assert (paren != NULL);

	switch (file[*pos]) {
	case '(':
		(*paren)++;

		/* An open parenthesis may only occur if we're in a valid
		 * state for an operand; we must have no items on the stack,
		 * or an open parenthesis on the stack, or an operator
		 * on the stack; and must have nothing collected.
		 */
		item = (! NIH_LIST_EMPTY (stack)
			? (NihListEntry *)stack->next: NULL);
		oper = (item ? (EventOperator *)item->data : NULL);
		if (*root || (item && oper && (oper->type == EVENT_MATCH)))
			nih_return_error (-1, PARSE_EXPECTED_OPERATOR,
					  _(PARSE_EXPECTED_OPERATOR_STR));

		/* We push a NULL item onto the operator stack to denote
		 * the beginning of a parenthesis group, this prevents us
		 * popping past it later.
		 */
		item = nih_list_entry_new (class);
		if (! item)
			nih_return_system_error (-1);

		nih_list_add_after (stack, &item->entry);
		break;
	case ')':
		(*paren)--;

		/* Collect up to the first open paren marker. */
		if (parse_on_collect (class, stack, root) < 0)
			return -1;

		/* If we run out of stack, then we have mismatched parens. */
		if (NIH_LIST_EMPTY (stack))
			nih_return_error (-1, PARSE_MISMATCHED_PARENS,
					  _(PARSE_MISMATCHED_PARENS_STR));

		/* The top item on the stack should be the open parenthesis
		 * marker, which we want to discard.
		 */
		nih_free (stack->next);
		break;
	default:
		nih_assert_not_reached ();
	}

	/* Skip over the paren and any following whitespace */
	(*pos)++;
	nih_config_skip_whitespace (file, len, pos, lineno);

	return 0;
}

/**
 * parse_on_operand:
 * @class: job class being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number,
 * @stack: input operator stack,
 * @root: output operator.
 *
 * This function parses a single operand to the "or" stanza.  An operand
 * is any token not considered an operator, such as the name of an event
 * or arguments to that event.
 *
 * If the item on the top of @stack is an EVENT_MATCH operator, the operand
 * is added to that operator's argument list; otherwise the operand is
 * treated as the name of an event and a new EVENT_MATCH operator pushed
 * onto the stack.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
parse_on_operand (JobClass         *class,
		  NihConfigStanza  *stanza,
		  const char       *file,
		  size_t            len,
		  size_t           *pos,
		  size_t           *lineno,
		  NihList          *stack,
		  EventOperator   **root)
{
	EventOperator  *oper;
	NihListEntry   *item;
	nih_local char *arg = NULL;

	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);
	nih_assert (stack != NULL);
	nih_assert (root != NULL);

	arg = nih_config_next_token (NULL, file, len, pos, lineno,
				     "()" NIH_CONFIG_CNLWS, TRUE);
	if (! arg)
		return -1;

	/* Look at the item on the top of the stack; if it is an
	 * EVENT_MATCH operator then the operand is an argument to that event;
	 * otherwise if the stack is empty, or the top item is an operator
	 * of some kind, then the operand begins a new EVENT_MATCH operator.
	 */
	item = (! NIH_LIST_EMPTY (stack) ? (NihListEntry *)stack->next: NULL);
	oper = (item ? (EventOperator *)item->data : NULL);
	if ((! item) || (! oper) || (oper->type != EVENT_MATCH)) {
		/* Argument is the name of an event to be matched; create
		 * an EventOperator to match it and push it onto the stack.
		 *
		 * We get away with not popping anything here because we
		 * know that we can never end up with two events on the top
		 * of the stack.
		 */
		oper = event_operator_new (class, EVENT_MATCH, arg, NULL);
		if (! oper)
			nih_return_system_error (-1);

		item = nih_list_entry_new (class);
		if (! item)
			nih_return_system_error (-1);

		item->data = oper;
		nih_list_add_after (stack, &item->entry);
	} else {
		char **e;
		int    pos = TRUE;

		/* Argument is an environment variable for the event on
		 * the top of the stack, so we append it there.
		 */
		if (! nih_str_array_addp (&oper->env, oper, NULL, arg))
			nih_return_system_error (-1);

		/* Sanity check the event's environment to ensure that no
		 * positional arguments follow name-based ones.
		 */
		for (e = oper->env; e && *e; e++) {
			if (strchr (*e, '=')) {
				pos = FALSE;
			} else if (! pos) {
				nih_error_raise (PARSE_EXPECTED_VARIABLE,
						 _(PARSE_EXPECTED_VARIABLE_STR));
				return -1;
			}
		}
	}

	return 0;
}

/**
 * parse_on_collect:
 * @class: job class being parsed,
 * @stack: input operator stack,
 * @root: output operator.
 *
 * This function collects the input operators from @stack, up until the
 * beginning of the stack or a group within it (denoted by a stack item
 * with NULL data), and places the collected operator tree in @root.
 *
 * @root may point to a NULL pointer, or to a previously collected
 * operator; in which case it will become the right-hand child of the
 * operator on the top of the stack.
 *
 * On return from this function, @root will always point to a non-NULL
 * pointer since it is an error to fail to collect from the stack.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
parse_on_collect (JobClass       *class,
		  NihList        *stack,
		  EventOperator **root)
{
	nih_assert (class != NULL);
	nih_assert (stack != NULL);
	nih_assert (root != NULL);

	NIH_LIST_FOREACH_SAFE (stack, iter) {
		NihListEntry  *item = (NihListEntry *)iter;
		EventOperator *oper = (EventOperator *)item->data;

		/* Stop on the opening of a parenthesis group */
		if (! oper)
			break;

		/* Remove the item from the stack */
		nih_free (item);

		/* Make the existing root node a child of the new operator;
		 * there must be one for operators, and must not be one for
		 * event matches.
		 */
		if ((oper->type != EVENT_MATCH) && (*root)) {
			nih_ref (*root, oper);
			nih_unref (*root, class);

			nih_tree_add (&oper->node, &(*root)->node,
				      NIH_TREE_RIGHT);
		} else if (oper->type != EVENT_MATCH) {
			nih_return_error (-1, PARSE_EXPECTED_EVENT,
					  _(PARSE_EXPECTED_EVENT_STR));
		} else if (*root) {
			nih_return_error (-1, PARSE_EXPECTED_OPERATOR,
					  _(PARSE_EXPECTED_OPERATOR_STR));
		}

		/* Make the operator the new root */
		*root = oper;
	}

	/* If we failed to collect any operands, an event was expected */
	if (! *root)
		nih_return_error (-1, PARSE_EXPECTED_EVENT,
				  _(PARSE_EXPECTED_EVENT_STR));

	return 0;
}


/**
 * stanza_debug:
 * @class: job class being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a debug stanza from @file. No parameters are supported.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_debug (JobClass           *class,
		 NihConfigStanza *stanza,
		 const char      *file,
		 size_t           len,
		 size_t          *pos,
		 size_t          *lineno)
{
	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	class->debug = TRUE;

	return 0;
}


/**
 * stanza_manual:
 * @class: job class being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a manual stanza from @file. No parameters are supported.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_manual (JobClass           *class,
		 NihConfigStanza *stanza,
		 const char      *file,
		 size_t           len,
		 size_t          *pos,
		 size_t          *lineno)
{
	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	/* manual simply disregards any start on events seen previously */

	nih_debug ("disregarding start on events for %s",
			class->name);

	if (class->start_on)
		nih_unref (class->start_on, class);

	class->start_on = NULL;

	return 0;
}

/**
 * stanza_instance:
 * @class: job class being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse an instance stanza from @file, this has an argument specifying
 * the instance name pattern which is stored in the class's instance member
 * and expanded before use.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_instance (JobClass        *class,
		 NihConfigStanza *stanza,
		 const char      *file,
		 size_t           len,
		 size_t          *pos,
		 size_t          *lineno)
{
	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (class->instance)
		nih_unref (class->instance, class);

	class->instance = nih_config_next_arg (class, file, len, pos, lineno);
	if (! class->instance)
		return -1;

	return nih_config_skip_comment (file, len, pos, lineno);
}


/**
 * stanza_description:
 * @class: job class being parsed,
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
stanza_description (JobClass        *class,
		    NihConfigStanza *stanza,
		    const char      *file,
		    size_t           len,
		    size_t          *pos,
		    size_t          *lineno)
{
	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (class->description)
		nih_unref (class->description, class);

	class->description = nih_config_next_arg (class, file,
						  len, pos, lineno);
	if (! class->description)
		return -1;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * stanza_author:
 * @class: job class being parsed,
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
stanza_author (JobClass        *class,
	       NihConfigStanza *stanza,
	       const char      *file,
	       size_t           len,
	       size_t          *pos,
	       size_t          *lineno)
{
	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (class->author)
		nih_unref (class->author, class);

	class->author = nih_config_next_arg (class, file, len, pos, lineno);
	if (! class->author)
		return -1;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * stanza_version:
 * @class: job class being parsed,
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
stanza_version (JobClass        *class,
		NihConfigStanza *stanza,
		const char      *file,
		size_t           len,
		size_t          *pos,
		size_t          *lineno)
{
	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (class->version)
		nih_unref (class->version, class);

	class->version = nih_config_next_arg (class, file, len, pos, lineno);
	if (! class->version)
		return -1;

	return nih_config_skip_comment (file, len, pos, lineno);
}


/**
 * stanza_env:
 * @class: job class being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse an env stanza from @file, extracting a single argument of the form
 * VAR or VAR=VALUE.  These are stored in the env array, which is increased
 * in size to accommodate the new value.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_env (JobClass        *class,
	    NihConfigStanza *stanza,
	    const char      *file,
	    size_t           len,
	    size_t          *pos,
	    size_t          *lineno)
{
	nih_local char *env = NULL;

	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	env = nih_config_next_arg (NULL, file, len, pos, lineno);
	if (! env)
		return -1;

	if (! nih_str_array_addp (&class->env, class, NULL, env))
		nih_return_system_error (-1);

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * stanza_export:
 * @class: job class being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse an export stanza from @file, extracting one or more arguments
 * containing environment variable names.  These are stored in the export
 * array, which is increased in size to accomodate the new values.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_export (JobClass        *class,
	       NihConfigStanza *stanza,
	       const char      *file,
	       size_t           len,
	       size_t          *pos,
	       size_t          *lineno)
{
	nih_local char **args = NULL;
	char           **arg;

	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (! nih_config_has_token (file, len, pos, lineno))
		nih_return_error (-1, NIH_CONFIG_EXPECTED_TOKEN,
				  _(NIH_CONFIG_EXPECTED_TOKEN_STR));

	args = nih_config_parse_args (NULL, file, len, pos, lineno);
	if (! args)
		return -1;

	for (arg = args; *arg; arg++)
		if (! nih_str_array_addp (&class->export, class, NULL, *arg))
			nih_return_system_error (-1);

	return 0;
}


/**
 * stanza_start:
 * @class: job class being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a start stanza from @file.  This stanza expects a second "on"
 * argument, followed by an event which is allocated as an EventOperator
 * structure and stored in the start events list of the class.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_start (JobClass        *class,
	      NihConfigStanza *stanza,
	      const char      *file,
	      size_t           len,
	      size_t          *pos,
	      size_t          *lineno)
{
	nih_local char *arg = NULL;
	size_t          a_pos, a_lineno;
	int             ret = -1;

	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	a_pos = *pos;
	a_lineno = (lineno ? *lineno : 1);

	arg = nih_config_next_token (NULL, file, len, &a_pos, &a_lineno,
				     NIH_CONFIG_CNLWS, FALSE);
	if (! arg)
		goto finish;

	if (! strcmp (arg, "on")) {
		if (class->start_on)
			nih_unref (class->start_on, class);

		class->start_on = parse_on (class, stanza, file, len,
					    &a_pos, &a_lineno);
		if (! class->start_on)
			goto finish;

		ret = 0;

	} else {
		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				  _(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}

finish:
	*pos = a_pos;
	if (lineno)
		*lineno = a_lineno;

	return ret;
}

/**
 * stanza_stop:
 * @class: job class being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a stop stanza from @file.  This stanza expects a second "on"
 * argument, followed by an event which is allocated as an EventInfo structure
 * and stored in the stop events list of the class and copied to the instance
 * later.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_stop (JobClass        *class,
	     NihConfigStanza *stanza,
	     const char      *file,
	     size_t           len,
	     size_t          *pos,
	     size_t          *lineno)
{
	nih_local char *arg = NULL;
	size_t          a_pos, a_lineno;
	int             ret = -1;

	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	a_pos = *pos;
	a_lineno = (lineno ? *lineno : 1);

	arg = nih_config_next_token (NULL, file, len, &a_pos, &a_lineno,
				     NIH_CONFIG_CNLWS, FALSE);
	if (! arg)
		goto finish;

	if (! strcmp (arg, "on")) {
		if (class->stop_on)
			nih_unref (class->stop_on, class);

		class->stop_on = parse_on (class, stanza, file, len,
					   &a_pos, &a_lineno);
		if (! class->stop_on)
			goto finish;

		ret = 0;

	} else {
		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				  _(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}

finish:
	*pos = a_pos;
	if (lineno)
		*lineno = a_lineno;

	return ret;
}

/**
 * stanza_emits:
 * @class: job class being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse an emits stanza from @file.  This stanza expects one or more
 * arguments giving the names of additional events that can be emitted
 * by this job.  These are stored in the emits array, which is increased
 * in size to accomodate the new values.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_emits (JobClass        *class,
	      NihConfigStanza *stanza,
	      const char      *file,
	      size_t           len,
	      size_t          *pos,
	      size_t          *lineno)
{
	nih_local char **args = NULL;
	char           **arg;

	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (! nih_config_has_token (file, len, pos, lineno))
		nih_return_error (-1, NIH_CONFIG_EXPECTED_TOKEN,
				  _(NIH_CONFIG_EXPECTED_TOKEN_STR));

	args = nih_config_parse_args (NULL, file, len, pos, lineno);
	if (! args)
		return -1;

	for (arg = args; *arg; arg++)
		if (! nih_str_array_addp (&class->emits, class, NULL, *arg))
			nih_return_system_error (-1);

	return 0;
}


/**
 * stanza_exec:
 * @class: job class being parsed,
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
stanza_exec (JobClass        *class,
	     NihConfigStanza *stanza,
	     const char      *file,
	     size_t           len,
	     size_t          *pos,
	     size_t          *lineno)
{
	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (! class->process[PROCESS_MAIN]) {
		class->process[PROCESS_MAIN] = process_new (class->process);
		if (! class->process[PROCESS_MAIN])
			nih_return_system_error (-1);
	}

	return parse_exec (class->process[PROCESS_MAIN], stanza,
			   file, len, pos, lineno);
}

/**
 * stanza_script:
 * @class: job class being parsed,
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
stanza_script (JobClass        *class,
	       NihConfigStanza *stanza,
	       const char      *file,
	       size_t           len,
	       size_t          *pos,
	       size_t          *lineno)
{
	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (! class->process[PROCESS_MAIN]) {
		class->process[PROCESS_MAIN] = process_new (class->process);
		if (! class->process[PROCESS_MAIN])
			nih_return_system_error (-1);
	}

	return parse_script (class->process[PROCESS_MAIN], stanza,
			     file, len, pos, lineno);
}

/**
 * stanza_pre_start:
 * @class: job class being parsed,
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
stanza_pre_start (JobClass        *class,
		  NihConfigStanza *stanza,
		  const char      *file,
		  size_t           len,
		  size_t          *pos,
		  size_t          *lineno)
{
	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	return parse_process (class, PROCESS_PRE_START, stanza,
			      file, len, pos, lineno);
}

/**
 * stanza_post_start:
 * @class: job class being parsed,
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
stanza_post_start (JobClass        *class,
		   NihConfigStanza *stanza,
		   const char      *file,
		   size_t           len,
		   size_t          *pos,
		   size_t          *lineno)
{
	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	return parse_process (class, PROCESS_POST_START, stanza,
			      file, len, pos, lineno);
}

/**
 * stanza_pre_stop:
 * @class: job class being parsed,
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
stanza_pre_stop (JobClass        *class,
		 NihConfigStanza *stanza,
		 const char      *file,
		 size_t           len,
		 size_t          *pos,
		 size_t          *lineno)
{
	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	return parse_process (class, PROCESS_PRE_STOP, stanza,
			      file, len, pos, lineno);
}

/**
 * stanza_post_stop:
 * @class: job class being parsed,
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
stanza_post_stop (JobClass        *class,
		  NihConfigStanza *stanza,
		  const char      *file,
		  size_t           len,
		  size_t          *pos,
		  size_t          *lineno)
{
	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	return parse_process (class, PROCESS_POST_STOP, stanza,
			      file, len, pos, lineno);
}


/**
 * stanza_expect:
 * @class: job class being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse an expect stanza from @file.  This stanza expects a single argument
 * single argument giving one of the possible ExpectType enumerations which
 * sets the class's expect member.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_expect (JobClass        *class,
	       NihConfigStanza *stanza,
	       const char      *file,
	       size_t           len,
	       size_t          *pos,
	       size_t          *lineno)
{
	size_t          a_pos, a_lineno;
	int             ret = -1;
	nih_local char *arg = NULL;

	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	a_pos = *pos;
	a_lineno = (lineno ? *lineno : 1);

	arg = nih_config_next_arg (NULL, file, len, &a_pos, &a_lineno);
	if (! arg)
		goto finish;

	if (! strcmp (arg, "stop")) {
		class->expect = EXPECT_STOP;
	} else if (! strcmp (arg, "daemon")) {
		class->expect = EXPECT_DAEMON;
	} else if (! strcmp (arg, "fork")) {
		class->expect = EXPECT_FORK;
	} else if (! strcmp (arg, "none")) {
		class->expect = EXPECT_NONE;
	} else {
		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				  _(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}

	ret = nih_config_skip_comment (file, len, &a_pos, &a_lineno);

finish:
	*pos = a_pos;
	if (lineno)
		*lineno = a_lineno;

	return ret;
}

/**
 * stanza_task:
 * @class: job class being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a task stanza from @file.  This sets the task flag for the class, and
 * takes no further arguments.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_task (JobClass        *class,
	     NihConfigStanza *stanza,
	     const char      *file,
	     size_t           len,
	     size_t          *pos,
	     size_t          *lineno)
{
	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	class->task = TRUE;

	return nih_config_skip_comment (file, len, pos, lineno);
}


/**
 * stanza_kill:
 * @class: job class being parsed,
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
stanza_kill (JobClass        *class,
	     NihConfigStanza *stanza,
	     const char      *file,
	     size_t           len,
	     size_t          *pos,
	     size_t          *lineno)
{
	size_t          a_pos, a_lineno;
	int             ret = -1;
	char           *endptr;
	nih_local char *arg = NULL;

	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	a_pos = *pos;
	a_lineno = (lineno ? *lineno : 1);

	arg = nih_config_next_token (NULL, file, len, &a_pos, &a_lineno,
				     NIH_CONFIG_CNLWS, FALSE);
	if (! arg)
		goto finish;

	if (! strcmp (arg, "timeout")) {
		nih_local char *timearg = NULL;

		/* Update error position to the timeout value */
		*pos = a_pos;
		if (lineno)
			*lineno = a_lineno;

		timearg = nih_config_next_arg (NULL, file, len,
					       &a_pos, &a_lineno);
		if (! timearg)
			goto finish;

		errno = 0;
		class->kill_timeout = strtol (timearg, &endptr, 10);
		if (errno || *endptr || (class->kill_timeout < 0))
			nih_return_error (-1, PARSE_ILLEGAL_INTERVAL,
					  _(PARSE_ILLEGAL_INTERVAL_STR));
	} else if (! strcmp (arg, "signal")) {
		unsigned long   status;
		nih_local char *sigarg = NULL;
		int		signal;

		/* Update error position to the exit status */
		*pos = a_pos;
		if (lineno)
			*lineno = a_lineno;

		sigarg = nih_config_next_arg (NULL, file, len, &a_pos,
					      &a_lineno);

		if (! sigarg)
			goto finish;

		signal = nih_signal_from_name (sigarg);
		if (signal < 0) {
			errno = 0;
			status = strtoul (sigarg, &endptr, 10);
			if (errno || *endptr || (status > INT_MAX))
				nih_return_error (-1, PARSE_ILLEGAL_SIGNAL,
						  _(PARSE_ILLEGAL_SIGNAL_STR));
		}

		/* Set the signal */
		class->kill_signal = signal;
	} else {
		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				  _(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}

	ret = nih_config_skip_comment (file, len, &a_pos, &a_lineno);

finish:
	*pos = a_pos;
	if (lineno)
		*lineno = a_lineno;

	return ret;
}


/**
 * stanza_respawn:
 * @class: job class being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a daemon stanza from @file.  This either has no arguments, in
 * which case it sets the respawn flag for the job, or it has the "limit"
 * argument and sets the respawn rate limit.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_respawn (JobClass        *class,
		NihConfigStanza *stanza,
		const char      *file,
		size_t           len,
		size_t          *pos,
		size_t          *lineno)
{
	nih_local char *arg = NULL;
	size_t          a_pos, a_lineno;
	int             ret = -1;

	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	/* Deal with the no-argument form first */
	if (! nih_config_has_token (file, len, pos, lineno)) {
		class->respawn = TRUE;

		return nih_config_skip_comment (file, len, pos, lineno);
	}


	/* Take the next argument, a sub-stanza keyword. */
	a_pos = *pos;
	a_lineno = (lineno ? *lineno : 1);

	arg = nih_config_next_token (NULL, file, len, &a_pos, &a_lineno,
				     NIH_CONFIG_CNLWS, FALSE);
	if (! arg)
		goto finish;

	if (! strcmp (arg, "limit")) {
		nih_local char *limitarg = NULL;
		char           *endptr;

		/* Update error position to the limit value */
		*pos = a_pos;
		if (lineno)
			*lineno = a_lineno;

		/* Parse the limit value */
		limitarg = nih_config_next_arg (NULL, file, len,
						&a_pos, &a_lineno);
		if (! limitarg)
			goto finish;

		if (strcmp (limitarg, "unlimited")) {
			nih_local char *timearg = NULL;

			errno = 0;
			class->respawn_limit = strtol (limitarg, &endptr, 10);
			if (errno || *endptr || (class->respawn_limit < 0))
				nih_return_error (-1, PARSE_ILLEGAL_LIMIT,
						  _(PARSE_ILLEGAL_LIMIT_STR));

			/* Update error position to the timeout value */
			*pos = a_pos;
			if (lineno)
				*lineno = a_lineno;

			/* Parse the timeout value */
			timearg = nih_config_next_arg (NULL, file, len,
						       &a_pos, &a_lineno);
			if (! timearg)
				goto finish;

			errno = 0;
			class->respawn_interval = strtol (timearg, &endptr, 10);
			if (errno || *endptr || (class->respawn_interval < 0))
				nih_return_error (-1, PARSE_ILLEGAL_INTERVAL,
						  _(PARSE_ILLEGAL_INTERVAL_STR));
		} else {
			class->respawn_limit = 0;
			class->respawn_interval = 0;
		}

		ret = nih_config_skip_comment (file, len, &a_pos, &a_lineno);

	} else {
		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				  _(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}

finish:
	*pos = a_pos;
	if (lineno)
		*lineno = a_lineno;

	return ret;
}

/**
 * stanza_normal:
 * @class: job class being parsed,
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
stanza_normal (JobClass        *class,
	       NihConfigStanza *stanza,
	       const char      *file,
	       size_t           len,
	       size_t          *pos,
	       size_t          *lineno)
{
	size_t          a_pos, a_lineno;
	int             ret = -1;
	nih_local char *arg = NULL;

	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	a_pos = *pos;
	a_lineno = (lineno ? *lineno : 1);

	arg = nih_config_next_token (NULL, file, len, &a_pos, &a_lineno,
				     NIH_CONFIG_CNLWS, FALSE);
	if (! arg)
		goto finish;

	if (! strcmp (arg, "exit")) {
		do {
			unsigned long   status;
			nih_local char *exitarg = NULL;
			char           *endptr;
			int            *new_ne, signum;

			/* Update error position to the exit status */
			*pos = a_pos;
			if (lineno)
				*lineno = a_lineno;

			exitarg = nih_config_next_arg (NULL, file, len,
						       &a_pos, &a_lineno);
			if (! exitarg)
				goto finish;

			signum = nih_signal_from_name (exitarg);
			if (signum < 0) {
				errno = 0;
				status = strtoul (exitarg, &endptr, 10);
				if (errno || *endptr || (status > INT_MAX))
					nih_return_error (-1, PARSE_ILLEGAL_EXIT,
							  _(PARSE_ILLEGAL_EXIT_STR));
			} else {
				status = signum << 8;
			}

			new_ne = nih_realloc (class->normalexit, class,
					      sizeof (int) * (class->normalexit_len + 1));
			if (! new_ne)
				nih_return_system_error (-1);

			class->normalexit = new_ne;
			class->normalexit[class->normalexit_len++] = (int) status;
		} while (nih_config_has_token (file, len, &a_pos, &a_lineno));

		ret = nih_config_skip_comment (file, len, &a_pos, &a_lineno);
	} else {
		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				  _(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}

finish:
	*pos = a_pos;
	if (lineno)
		*lineno = a_lineno;

	return ret;
}


/**
 * stanza_console:
 * @class: job class being parsed,
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
stanza_console (JobClass        *class,
		NihConfigStanza *stanza,
		const char      *file,
		size_t           len,
		size_t          *pos,
		size_t          *lineno)
{
	size_t          a_pos, a_lineno;
	int             ret = -1;
	nih_local char *arg = NULL;

	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	a_pos = *pos;
	a_lineno = (lineno ? *lineno : 1);

	arg = nih_config_next_arg (NULL, file, len, &a_pos, &a_lineno);
	if (! arg)
		goto finish;

	class->console = job_class_console_type (arg);

	if (class->console == (ConsoleType)-1) {
		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				_(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}

	ret = nih_config_skip_comment (file, len, &a_pos, &a_lineno);

finish:
	*pos = a_pos;
	if (lineno)
		*lineno = a_lineno;

	return ret;
}


/**
 * stanza_umask:
 * @class: job class being parsed,
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
stanza_umask (JobClass        *class,
	      NihConfigStanza *stanza,
	      const char      *file,
	      size_t           len,
	      size_t          *pos,
	      size_t          *lineno)
{
	nih_local char *arg = NULL;
	char           *endptr;
	size_t          a_pos, a_lineno;
	int             ret = -1;

	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	a_pos = *pos;
	a_lineno = (lineno ? *lineno : 1);

	arg = nih_config_next_arg (NULL, file, len, &a_pos, &a_lineno);
	if (! arg)
		goto finish;

	errno = 0;
	class->umask = (mode_t)strtoul (arg, &endptr, 8);
	if (errno || *endptr || (class->umask & ~0777))
		nih_return_error (-1, PARSE_ILLEGAL_UMASK,
				  _(PARSE_ILLEGAL_UMASK_STR));

	ret = nih_config_skip_comment (file, len, &a_pos, &a_lineno);

finish:
	*pos = a_pos;
	if (lineno)
		*lineno = a_lineno;

	return ret;
}

/**
 * stanza_nice:
 * @class: job class being parsed,
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
stanza_nice (JobClass        *class,
	     NihConfigStanza *stanza,
	     const char      *file,
	     size_t           len,
	     size_t          *pos,
	     size_t          *lineno)
{
	nih_local char *arg = NULL;
	char           *endptr;
	size_t          a_pos, a_lineno;
	int             ret = -1;

	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	a_pos = *pos;
	a_lineno = (lineno ? *lineno : 1);

	arg = nih_config_next_arg (NULL, file, len, &a_pos, &a_lineno);
	if (! arg)
		goto finish;

	errno = 0;
	class->nice = (int)strtol (arg, &endptr, 10);
	if (errno || *endptr || (class->nice < -20) || (class->nice > 19))
		nih_return_error (-1, PARSE_ILLEGAL_NICE,
				  _(PARSE_ILLEGAL_NICE_STR));

	ret = nih_config_skip_comment (file, len, &a_pos, &a_lineno);

finish:
	*pos = a_pos;
	if (lineno)
		*lineno = a_lineno;

	return ret;
}

/**
 * stanza_oom:
 * @class: job class being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse an oom stanza from @file, extracting a single argument containing
 * a OOM killer adjustment (which may be "never" for the magic -17 value).
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_oom (JobClass        *class,
	    NihConfigStanza *stanza,
	    const char      *file,
	    size_t           len,
	    size_t          *pos,
	    size_t          *lineno)
{
	nih_local char *arg = NULL;
	char           *endptr;
	size_t          a_pos, a_lineno;
	int		oom_adj;
	int             ret = -1;

	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	a_pos = *pos;
	a_lineno = (lineno ? *lineno : 1);

	arg = nih_config_next_arg (NULL, file, len, &a_pos, &a_lineno);
	if (! arg)
		goto finish;

	if (! strcmp (arg, "score")) {
		nih_local char *scorearg = NULL;

		/* Update error position to the score value */
		*pos = a_pos;
		if (lineno)
			*lineno = a_lineno;

		scorearg = nih_config_next_arg (NULL, file, len,
						&a_pos, &a_lineno);
		if (! scorearg)
			goto finish;

		if (! strcmp (scorearg, "never")) {
			class->oom_score_adj = -1000;
		} else {
			errno = 0;
			class->oom_score_adj = (int)strtol (scorearg, &endptr, 10);
			if (errno || *endptr ||
			    (class->oom_score_adj < -1000) ||
			    (class->oom_score_adj > 1000))
				nih_return_error (-1, PARSE_ILLEGAL_OOM,
						  _(PARSE_ILLEGAL_OOM_SCORE_STR));
		}
	} else if (! strcmp (arg, "never")) {
		class->oom_score_adj = -1000;
	} else {
		errno = 0;
		oom_adj = (int)strtol (arg, &endptr, 10);
		class->oom_score_adj = (oom_adj * 1000) / ((oom_adj < 0) ? 17 : 15);
		if (errno || *endptr || (oom_adj < -17) || (oom_adj > 15))
			nih_return_error (-1, PARSE_ILLEGAL_OOM,
					  _(PARSE_ILLEGAL_OOM_STR));
	}

	ret = nih_config_skip_comment (file, len, &a_pos, &a_lineno);

finish:
	*pos = a_pos;
	if (lineno)
		*lineno = a_lineno;

	return ret;
}

/**
 * stanza_limit:
 * @class: job class being parsed,
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
stanza_limit (JobClass        *class,
	      NihConfigStanza *stanza,
	      const char      *file,
	      size_t           len,
	      size_t          *pos,
	      size_t          *lineno)
{
	int             resource;
	nih_local char *arg = NULL, *softarg = NULL, *hardarg = NULL;
	char           *endptr;
	size_t          a_pos, a_lineno;
	int             ret = -1;

	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	a_pos = *pos;
	a_lineno = (lineno ? *lineno : 1);

	arg = nih_config_next_arg (NULL, file, len, &a_pos, &a_lineno);
	if (! arg)
		goto finish;

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
		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				  _(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}


	if (! class->limits[resource]) {
		class->limits[resource] = nih_new (class, struct rlimit);
		if (! class->limits[resource])
			nih_return_system_error (-1);
	}

	/* Update error position to the soft limit value */
	*pos = a_pos;
	if (lineno)
		*lineno = a_lineno;

	/* Parse the soft limit value */
	softarg = nih_config_next_arg (NULL, file, len, &a_pos, &a_lineno);
	if (! softarg)
		goto finish;

	if (strcmp (softarg, "unlimited")) {
		errno = 0;
		class->limits[resource]->rlim_cur = strtoul (softarg, &endptr,
							     10);
		if (errno || *endptr)
			nih_return_error (-1, PARSE_ILLEGAL_LIMIT,
					  _(PARSE_ILLEGAL_LIMIT_STR));
	} else {
		class->limits[resource]->rlim_cur = RLIM_INFINITY;
	}

	/* Update error position to the hard limit value */
	*pos = a_pos;
	if (lineno)
		*lineno = a_lineno;

	/* Parse the hard limit value */
	hardarg = nih_config_next_arg (NULL, file, len, &a_pos, &a_lineno);
	if (! hardarg)
		goto finish;

	if (strcmp (hardarg, "unlimited")) {
		errno = 0;
		class->limits[resource]->rlim_max = strtoul (hardarg, &endptr,
							     10);
		if (errno || *endptr)
			nih_return_error (-1, PARSE_ILLEGAL_LIMIT,
					  _(PARSE_ILLEGAL_LIMIT_STR));
	} else {
		class->limits[resource]->rlim_max = RLIM_INFINITY;
	}

	ret = nih_config_skip_comment (file, len, &a_pos, &a_lineno);

finish:
	*pos = a_pos;
	if (lineno)
		*lineno = a_lineno;

	return ret;
}

/**
 * stanza_chroot:
 * @class: job class being parsed,
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
stanza_chroot (JobClass        *class,
	       NihConfigStanza *stanza,
	       const char      *file,
	       size_t           len,
	       size_t          *pos,
	       size_t          *lineno)
{
	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (class->chroot)
		nih_unref (class->chroot, class);

	class->chroot = nih_config_next_arg (class, file, len, pos, lineno);
	if (! class->chroot)
		return -1;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * stanza_chdir:
 * @class: job class being parsed,
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
stanza_chdir (JobClass        *class,
	      NihConfigStanza *stanza,
	      const char      *file,
	      size_t           len,
	      size_t          *pos,
	      size_t          *lineno)
{
	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (class->chdir)
		nih_unref (class->chdir, class);

	class->chdir = nih_config_next_arg (class, file, len, pos, lineno);
	if (! class->chdir)
		return -1;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * stanza_setuid:
 * @class: job class being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a setuid stanza from @file, extracting a single argument
 * containing a user name.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_setuid (JobClass        *class,
	       NihConfigStanza *stanza,
	       const char      *file,
	       size_t           len,
	       size_t          *pos,
	       size_t          *lineno)
{
	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (class->setuid)
		nih_unref (class->setuid, class);

	class->setuid = nih_config_next_arg (class, file, len, pos, lineno);
	if (! class->setuid)
		return -1;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * stanza_setgid:
 * @class: job class being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a setgid stanza from @file, extracting a single argument
 * containing a group name.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_setgid (JobClass        *class,
	       NihConfigStanza *stanza,
	       const char      *file,
	       size_t           len,
	       size_t          *pos,
	       size_t          *lineno)
{
	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (class->setgid)
		nih_unref (class->setgid, class);

	class->setgid = nih_config_next_arg (class, file, len, pos, lineno);
	if (! class->setgid)
		return -1;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * stanza_usage:
 * @class: job class being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a usage stanza from @file, extracting a single argument
 * containing a usage message.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_usage (JobClass        *class,
	      NihConfigStanza *stanza,
	      const char      *file,
	      size_t           len,
	      size_t          *pos,
	      size_t          *lineno)
{
	nih_assert (class != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (class->usage)
		nih_unref (class->usage, class);

	class->usage = nih_config_next_arg (class, file, len, pos, lineno);
	if (! class->usage)
		return -1;

	return nih_config_skip_comment (file, len, pos, lineno);
}
