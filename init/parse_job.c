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

#include "job.h"
#include "event.h"
#include "parse_job.h"
#include "errors.h"


/* Prototypes for static functions */
static int            parse_exec        (JobProcess *proc,
					 NihConfigStanza *stanza,
					 const char *file, size_t len,
					 size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int            parse_script      (JobProcess *proc,
					 NihConfigStanza *stanza,
					 const char *file, size_t len,
					 size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int            parse_process     (JobConfig *job, ProcessType process,
					 NihConfigStanza *stanza,
					 const char *file, size_t len,
					 size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static EventOperator *parse_on          (JobConfig *job,
					 NihConfigStanza *stanza,
					 const char *file, size_t len,
					 size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result, malloc));
static int            parse_on_operator (JobConfig *job,
					 NihConfigStanza *stanza,
					 const char *file, size_t len,
					 size_t *pos, size_t *lineno,
					 NihList *stack, EventOperator **root)
	__attribute__ ((warn_unused_result));
static int            parse_on_paren    (JobConfig *job,
					 NihConfigStanza *stanza,
					 const char *file, size_t len,
					 size_t *pos, size_t *lineno,
					 NihList *stack, EventOperator **root,
					 size_t *paren)
	__attribute__ ((warn_unused_result));
static int            parse_on_operand  (JobConfig *job,
					 NihConfigStanza *stanza,
					 const char *file, size_t len,
					 size_t *pos, size_t *lineno,
					 NihList *stack, EventOperator **root)
	__attribute__ ((warn_unused_result));
static int            parse_on_collect  (NihList *stack, EventOperator **root)
	__attribute__ ((warn_unused_result));

static int stanza_exec        (JobConfig *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_script      (JobConfig *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_pre_start   (JobConfig *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_post_start  (JobConfig *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_pre_stop    (JobConfig *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_post_stop   (JobConfig *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_start       (JobConfig *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_stop        (JobConfig *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_description (JobConfig *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_author      (JobConfig *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_version     (JobConfig *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_emits       (JobConfig *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_daemon      (JobConfig *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_respawn     (JobConfig *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_service     (JobConfig *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_instance    (JobConfig *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_pid         (JobConfig *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_kill        (JobConfig *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_normal      (JobConfig *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_console     (JobConfig *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_env         (JobConfig *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_umask       (JobConfig *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_nice        (JobConfig *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_limit       (JobConfig *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_chroot      (JobConfig *job, NihConfigStanza *stanza,
			       const char *file, size_t len,
			       size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));
static int stanza_chdir       (JobConfig *job, NihConfigStanza *stanza,
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
 * Returns: newly allocated JobConfig structure on success,
 * NULL on raised error.
 **/
JobConfig *
parse_job (const void *parent,
	   const char *name,
	   const char *file,
	   size_t      len,
	   size_t     *pos,
	   size_t     *lineno)
{
 	JobConfig *job;

	nih_assert (name != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	job = job_config_new (parent, name);
	if (! job)
		nih_return_system_error (NULL);

	if (nih_config_parse_file (file, len, pos, lineno, stanzas, job) < 0) {
		nih_free (job);
		return NULL;
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
parse_process (JobConfig       *job,
	       ProcessType      process,
	       NihConfigStanza *stanza,
	       const char      *file,
	       size_t           len,
	       size_t          *pos,
	       size_t          *lineno)
{
	char   *arg;
	size_t  a_pos, a_lineno;
	int     ret = -1;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	/* Allocate a new JobProcess structure if we need to */
	if (! job->process[process]) {
		job->process[process] = job_process_new (job->process);
		if (! job->process[process])
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
		nih_free (arg);

		ret = parse_exec (job->process[process], stanza,
				  file, len, &a_pos, &a_lineno);
	} else if (! strcmp (arg, "script")) {
		nih_free (arg);

		ret = parse_script (job->process[process], stanza,
				    file, len, &a_pos, &a_lineno);
	} else {
		nih_free (arg);

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
 * @job: job being parsed,
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
parse_on (JobConfig       *job,
	  NihConfigStanza *stanza,
	  const char      *file,
	  size_t           len,
	  size_t          *pos,
	  size_t          *lineno)
{
	NihList        stack;
	EventOperator *root = NULL;
	size_t         on_pos, on_lineno, paren = 0;

	nih_assert (job != NULL);
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
				if (parse_on_paren (job, stanza, file, len,
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
			} else if (parse_on_operator (job, stanza, file, len,
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
	if (parse_on_collect (&stack, &root) < 0) {
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
 * parse_on_operator:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number,
 * @stack: input operator stack,
 * @root: output operator.
 *
 * This function parses a single token from the arguments of the "or"
 * stanza.  If the token is not a valid operator, this will call
 * parse_on_operand() instead.
 *
 * Operators are pushed onto @stack after collecting any existing operators
 * and operands on the stack, and placing them as the operator's left child.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
parse_on_operator (JobConfig        *job,
		   NihConfigStanza  *stanza,
		   const char       *file,
		   size_t            len,
		   size_t           *pos,
		   size_t           *lineno,
		   NihList          *stack,
		   EventOperator   **root)
{
	size_t             a_pos, a_lineno;
	char              *arg;
	EventOperatorType  type;
	EventOperator     *oper;
	NihListEntry      *item;
	int                ret = -1;

	nih_assert (job != NULL);
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
		nih_free (arg);

		type = EVENT_AND;
	} else if (! strcmp (arg, "or")) {
		nih_free (arg);

		type = EVENT_OR;
	} else {
		nih_free (arg);

		return parse_on_operand (job, stanza, file, len, pos, lineno,
					 stack, root);
	}

	/* Before we push the new operator onto the stack, we need to collect
	 * any existing operators and operands.
	 */
	if (parse_on_collect (stack, root) < 0)
		return -1;

	/* Create the new operator, placing the existing root node as its
	 * left-hand child.
	 */
	oper = event_operator_new (job, type, NULL, NULL);
	if (! oper)
		nih_return_system_error (-1);

	nih_alloc_reparent (*root, oper);
	nih_tree_add (&oper->node, &(*root)->node, NIH_TREE_LEFT);
	*root = NULL;

	/* Push the new operator onto the stack */
	item = nih_list_entry_new (job);
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
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number,
 * @stack: input operator stack,
 * @root: output operator,
 * @paren: number of nested parenthese.
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
parse_on_paren (JobConfig        *job,
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

	nih_assert (job != NULL);
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
		item = nih_list_entry_new (job);
		if (! item)
			nih_return_system_error (-1);

		nih_list_add_after (stack, &item->entry);
		break;
	case ')':
		(*paren)--;

		/* Collect up to the first open paren marker. */
		if (parse_on_collect (stack, root) < 0)
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
 * @job: job being parsed,
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
parse_on_operand (JobConfig        *job,
		  NihConfigStanza  *stanza,
		  const char       *file,
		  size_t            len,
		  size_t           *pos,
		  size_t           *lineno,
		  NihList          *stack,
		  EventOperator   **root)
{
	EventOperator *oper;
	NihListEntry  *item;
	char          *arg;

	nih_assert (job != NULL);
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
		oper = event_operator_new (job, EVENT_MATCH, arg, NULL);
		if (! oper) {
			nih_error_raise_system ();
			nih_free (arg);
			return -1;
		}

		nih_free (arg);

		item = nih_list_entry_new (job);
		if (! item)
			nih_return_system_error (-1);

		item->data = oper;
		nih_list_add_after (stack, &item->entry);
	} else {
		/* Argument is an argument to the event on the top of the
		 * stack, so append it to the existing argument, array
		 * by reparenting the parsed string.
		 */
		if (! nih_str_array_addp (&oper->args, oper, NULL, arg)) {
			nih_error_raise_system ();
			nih_free (arg);
			return -1;
		}
	}

	return 0;
}

/**
 * parse_on_collect:
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
parse_on_collect (NihList          *stack,
		  EventOperator   **root)
{
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
			nih_alloc_reparent (*root, oper);
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
stanza_exec (JobConfig       *job,
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
stanza_script (JobConfig       *job,
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
stanza_pre_start (JobConfig       *job,
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
stanza_post_start (JobConfig       *job,
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
stanza_pre_stop (JobConfig       *job,
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
stanza_post_stop (JobConfig       *job,
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
stanza_start (JobConfig       *job,
	      NihConfigStanza *stanza,
	      const char      *file,
	      size_t           len,
	      size_t          *pos,
	      size_t          *lineno)
{
	char   *arg;
	size_t  a_pos, a_lineno;
	int     ret = -1;

	nih_assert (job != NULL);
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
		nih_free (arg);

		if (job->start_on)
			nih_free (job->start_on);

		job->start_on = parse_on (job, stanza, file, len,
					  &a_pos, &a_lineno);
		if (! job->start_on)
			goto finish;

		ret = 0;

	} else {
		nih_free (arg);

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
stanza_stop (JobConfig       *job,
	     NihConfigStanza *stanza,
	     const char      *file,
	     size_t           len,
	     size_t          *pos,
	     size_t          *lineno)
{
	char   *arg;
	size_t  a_pos, a_lineno;
	int     ret = -1;

	nih_assert (job != NULL);
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
		nih_free (arg);

		if (job->stop_on)
			nih_free (job->stop_on);

		job->stop_on = parse_on (job, stanza, file, len,
					 &a_pos, &a_lineno);
		if (! job->stop_on)
			goto finish;

		ret = 0;

	} else {
		nih_free (arg);

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
stanza_description (JobConfig       *job,
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
stanza_author (JobConfig       *job,
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
stanza_version (JobConfig       *job,
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
 * Arguments are allocated as NihListEntry structures, with the argument
 * as the string, and stored in the emits list of the job.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
stanza_emits (JobConfig       *job,
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
		NihListEntry *emits;

		emits = nih_list_entry_new (job);
		if (! emits) {
			nih_error_raise_system ();
			nih_free (args);
			return -1;
		}

		emits->str = *arg;
		nih_alloc_reparent (emits->str, emits);

		nih_list_add (&job->emits, &emits->entry);
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
stanza_daemon (JobConfig       *job,
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
stanza_respawn (JobConfig       *job,
		NihConfigStanza *stanza,
		const char      *file,
		size_t           len,
		size_t          *pos,
		size_t          *lineno)
{
	char   *arg;
	size_t  a_pos, a_lineno;
	int     ret = -1;

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
	a_pos = *pos;
	a_lineno = (lineno ? *lineno : 1);

	arg = nih_config_next_token (NULL, file, len, &a_pos, &a_lineno,
				     NIH_CONFIG_CNLWS, FALSE);
	if (! arg)
		goto finish;

	if (! strcmp (arg, "limit")) {
		char *endptr;

		nih_free (arg);

		/* Update error position to the limit value */
		*pos = a_pos;
		if (lineno)
			*lineno = a_lineno;

		/* Parse the limit value */
		arg = nih_config_next_arg (NULL, file, len, &a_pos, &a_lineno);
		if (! arg)
			goto finish;

		if (strcmp (arg, "unlimited")) {
			job->respawn_limit = strtol (arg, &endptr, 10);
			if (*endptr || (job->respawn_limit < 0)) {
				nih_free (arg);

				nih_return_error (-1, PARSE_ILLEGAL_LIMIT,
						  _(PARSE_ILLEGAL_LIMIT_STR));
			}
			nih_free (arg);

			/* Update error position to the timeout value */
			*pos = a_pos;
			if (lineno)
				*lineno = a_lineno;

			/* Parse the timeout value */
			arg = nih_config_next_arg (NULL, file, len,
						   &a_pos, &a_lineno);
			if (! arg)
				goto finish;

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

		ret = nih_config_skip_comment (file, len, &a_pos, &a_lineno);

	} else {
		nih_free (arg);

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
stanza_service (JobConfig       *job,
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
stanza_instance (JobConfig       *job,
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
stanza_pid (JobConfig       *job,
	    NihConfigStanza *stanza,
	    const char      *file,
	    size_t           len,
	    size_t          *pos,
	    size_t          *lineno)
{
	char   *arg;
	size_t  a_pos, a_lineno;
	int     ret = -1;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	a_pos = *pos;
	a_lineno = (lineno ? *lineno : 1);

	arg = nih_config_next_token (NULL, file, len, &a_pos, &a_lineno,
				     NIH_CONFIG_CNLWS, FALSE);
	if (! arg)
		goto finish;

	if (! strcmp (arg, "file")) {
		nih_free (arg);

		if (job->pid_file)
			nih_free (job->pid_file);

		job->pid_file = nih_config_next_arg (job, file, len,
						     &a_pos, &a_lineno);
		if (! job->pid_file)
			goto finish;

		ret = nih_config_skip_comment (file, len, &a_pos, &a_lineno);

	} else if (! strcmp (arg, "binary")) {
		nih_free (arg);

		if (job->pid_binary)
			nih_free (job->pid_binary);

		job->pid_binary = nih_config_next_arg (job, file, len,
						       &a_pos, &a_lineno);
		if (! job->pid_binary)
			goto finish;

		ret = nih_config_skip_comment (file, len, &a_pos, &a_lineno);

	} else if (! strcmp (arg, "timeout")) {
		char *endptr;

		nih_free (arg);

		/* Update error position to the timeout value */
		*pos = a_pos;
		if (lineno)
			*lineno = a_lineno;

		arg = nih_config_next_arg (NULL, file, len, &a_pos, &a_lineno);
		if (! arg)
			goto finish;

		job->pid_timeout = strtol (arg, &endptr, 10);
		if (*endptr || (job->pid_timeout < 0)) {
			nih_free (arg);

			nih_return_error (-1, PARSE_ILLEGAL_INTERVAL,
					  _(PARSE_ILLEGAL_INTERVAL_STR));
		}
		nih_free (arg);

		ret = nih_config_skip_comment (file, len, &a_pos, &a_lineno);

	} else {
		nih_free (arg);

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
stanza_kill (JobConfig       *job,
	     NihConfigStanza *stanza,
	     const char      *file,
	     size_t           len,
	     size_t          *pos,
	     size_t          *lineno)
{
	size_t  a_pos, a_lineno;
	int     ret = -1;
	char   *arg;

	nih_assert (job != NULL);
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
		char *endptr;

		nih_free (arg);

		/* Update error position to the timeout value */
		*pos = a_pos;
		if (lineno)
			*lineno = a_lineno;

		arg = nih_config_next_arg (NULL, file, len, &a_pos, &a_lineno);
		if (! arg)
			goto finish;

		job->kill_timeout = strtol (arg, &endptr, 10);
		if (*endptr || (job->kill_timeout < 0)) {
			nih_free (arg);

			nih_return_error (-1, PARSE_ILLEGAL_INTERVAL,
					  _(PARSE_ILLEGAL_INTERVAL_STR));
		}
		nih_free (arg);

		ret = nih_config_skip_comment (file, len, &a_pos, &a_lineno);

	} else {
		nih_free (arg);

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
stanza_normal (JobConfig       *job,
	       NihConfigStanza *stanza,
	       const char      *file,
	       size_t           len,
	       size_t          *pos,
	       size_t          *lineno)
{
	size_t  a_pos, a_lineno;
	int     ret = -1;
	char   *arg;

	nih_assert (job != NULL);
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
		nih_free (arg);

		do {
			unsigned long  status;
			char          *endptr;
			int           *new_ne, signum;

			/* Update error position to the exit status */
			*pos = a_pos;
			if (lineno)
				*lineno = a_lineno;

			arg = nih_config_next_arg (NULL, file, len,
						   &a_pos, &a_lineno);
			if (! arg)
				goto finish;

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
		} while (nih_config_has_token (file, len, &a_pos, &a_lineno));

		ret = nih_config_skip_comment (file, len, &a_pos, &a_lineno);
	} else {
		nih_free (arg);

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
stanza_console (JobConfig       *job,
		NihConfigStanza *stanza,
		const char      *file,
		size_t           len,
		size_t          *pos,
		size_t          *lineno)
{
	size_t  a_pos, a_lineno;
	int     ret = -1;
	char   *arg;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	a_pos = *pos;
	a_lineno = (lineno ? *lineno : 1);

	arg = nih_config_next_arg (NULL, file, len, &a_pos, &a_lineno);
	if (! arg)
		goto finish;

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

	ret = nih_config_skip_comment (file, len, &a_pos, &a_lineno);

finish:
	*pos = a_pos;
	if (lineno)
		*lineno = a_lineno;

	return ret;
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
stanza_env (JobConfig       *job,
	    NihConfigStanza *stanza,
	    const char      *file,
	    size_t           len,
	    size_t          *pos,
	    size_t          *lineno)
{
	char *env;

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
stanza_umask (JobConfig       *job,
	      NihConfigStanza *stanza,
	      const char      *file,
	      size_t           len,
	      size_t          *pos,
	      size_t          *lineno)
{
	char          *arg, *endptr;
	unsigned long  mask;
	size_t         a_pos, a_lineno;
	int            ret = -1;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	a_pos = *pos;
	a_lineno = (lineno ? *lineno : 1);

	arg = nih_config_next_arg (NULL, file, len, &a_pos, &a_lineno);
	if (! arg)
		goto finish;

	mask = strtoul (arg, &endptr, 8);
	if (*endptr || (mask & ~0777)) {
		nih_free (arg);

		nih_return_error (-1, PARSE_ILLEGAL_UMASK,
				  _(PARSE_ILLEGAL_UMASK_STR));
	}
	nih_free (arg);

	job->umask = (mode_t)mask;

	ret = nih_config_skip_comment (file, len, &a_pos, &a_lineno);

finish:
	*pos = a_pos;
	if (lineno)
		*lineno = a_lineno;

	return ret;
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
stanza_nice (JobConfig       *job,
	     NihConfigStanza *stanza,
	     const char      *file,
	     size_t           len,
	     size_t          *pos,
	     size_t          *lineno)
{
	char   *arg, *endptr;
	long    nice;
	size_t  a_pos, a_lineno;
	int     ret = -1;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	a_pos = *pos;
	a_lineno = (lineno ? *lineno : 1);

	arg = nih_config_next_arg (NULL, file, len, &a_pos, &a_lineno);
	if (! arg)
		goto finish;

	nice = strtol (arg, &endptr, 10);
	if (*endptr || (nice < -20) || (nice > 19)) {
		nih_free (arg);

		nih_return_error (-1, PARSE_ILLEGAL_NICE,
				  _(PARSE_ILLEGAL_NICE_STR));
	}
	nih_free (arg);

	job->nice = (int)nice;

	ret = nih_config_skip_comment (file, len, &a_pos, &a_lineno);

finish:
	*pos = a_pos;
	if (lineno)
		*lineno = a_lineno;

	return ret;
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
stanza_limit (JobConfig       *job,
	      NihConfigStanza *stanza,
	      const char      *file,
	      size_t           len,
	      size_t          *pos,
	      size_t          *lineno)
{
	int     resource;
	char   *arg, *endptr;
	size_t  a_pos, a_lineno;
	int     ret = -1;

	nih_assert (job != NULL);
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

	/* Update error position to the soft limit value */
	*pos = a_pos;
	if (lineno)
		*lineno = a_lineno;

	/* Parse the soft limit value */
	arg = nih_config_next_arg (NULL, file, len, &a_pos, &a_lineno);
	if (! arg)
		goto finish;

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

	/* Update error position to the hard limit value */
	*pos = a_pos;
	if (lineno)
		*lineno = a_lineno;

	/* Parse the hard limit value */
	arg = nih_config_next_arg (NULL, file, len, &a_pos, &a_lineno);
	if (! arg)
		goto finish;

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

	ret = nih_config_skip_comment (file, len, &a_pos, &a_lineno);

finish:
	*pos = a_pos;
	if (lineno)
		*lineno = a_lineno;

	return ret;
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
stanza_chroot (JobConfig       *job,
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
stanza_chdir (JobConfig       *job,
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
