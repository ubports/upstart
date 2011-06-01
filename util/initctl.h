/* upstart
 *
 * Copyright © 2011 Canonical Ltd.
 * Author: James Hunt <james.hunt@canonical.com>
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

#ifndef INITCTL_H
#define INITCTL_H

/**
 * IS_OP_AND:
 * @token: string token to check.
 *
 * Return TRUE if @token is an 'AND' operator, else FALSE.
 **/
#define IS_OP_AND(token) \
	(! strcmp ((token), "/AND"))

/**
 * IS_OP_OR:
 * @token: string token to check.
 *
 * Return TRUE if @token is an 'OR' operator, else FALSE.
 **/
#define IS_OP_OR(token) \
	(! strcmp ((token), "/OR"))

/**
 * IS_OPERATOR
 * @token: string token to check.
 *
 * Return TRUE if @token is either an 'AND' or an 'OR' operator,
 * else FALSE.
 **/
#define IS_OPERATOR(token) \
	IS_OP_AND (token) || IS_OP_OR (token)


/**
 * GET_JOB_NAME:
 *
 * @var: char pointer variable which will be set to the name
 * of a job, or NULL,
 * @index: zero-based index of tokens where zero represents
 * the first token after the initial token,
 * @token: string to check.
 *
 * Determine name of job considering the specified token and its
 * index. If not a job, sets @name to NULL.
 *
 * Handles the following cases:
 *
 *   [start|stop] on <job_event> foo
 *     (index==0, job="foo" => name="foo")
 *
 *   [start|stop] on <job_event> JOB=foo
 *     (index==0, job="JOB=foo" => name="foo")
 *
 *   [start|stop] on <job_event> A=B JOB=foo
 *     (index==1, job="JOB=foo" => name="foo")
 *
 *   [start|stop] on <job_event> A=B c=hello JOB=foo
 *     (index==2, job="JOB=foo" => name="foo")
 *
 *   [start|stop] on <job_event> $JOB A=B c=hello
 *     (index==0, job="$JOB" => name="$JOB")
 *
 **/
#define GET_JOB_NAME(var, index, token)                              \
{                                                                    \
	char *_##var;                                                \
                                                                     \
	nih_assert (index >= 0);                                     \
	nih_assert (token);                                          \
                                                                     \
	var = NULL;                                                  \
                                                                     \
	_##var = strstr (token, "JOB=");                             \
                                                                     \
	if (_##var && _##var == token)                               \
		var = _##var + strlen ("JOB=");                      \
	else if (index == 0 ) {                                      \
		if (!strstr (token, "="))                            \
			var = token;                                 \
	}                                                            \
}


/**
 * IS_JOB_EVENT:
 * @token: string to check.
 *
 * Return TRUE if specified token refers to a standard job event, else
 * FALSE.
 **/
#define IS_JOB_EVENT(token)                     \
	(!strcmp (token, JOB_STARTING_EVENT) || \
	 !strcmp (token, JOB_STARTED_EVENT)  || \
	 !strcmp (token, JOB_STOPPING_EVENT) || \
	 !strcmp (token, JOB_STOPPED_EVENT))

/**
 * IS_INIT_EVENT:
 * @token: string to check.
 *
 * Return TRUE if specified token refers to an event emitted internally,
 * else FALSE.
 *
 * Note: the raw string entries below are required to accommodate
 * production versus debug builds (STARTUP_EVENT changes name depending
 * on build type).
 **/
#define IS_INIT_EVENT(token)                  \
	(!strcmp (token, STARTUP_EVENT)    || \
	 !strcmp (token, "debug")          || \
	 !strcmp (token, "startup")        || \
	 !strcmp (token, CTRLALTDEL_EVENT) || \
	 !strcmp (token, KBDREQUEST_EVENT) || \
	 !strcmp (token, PWRSTATUS_EVENT)  || \
	 IS_JOB_EVENT (token))

/**
 * STACK_EMPTY:
 * @stack: address of stack to check.
 *
 * Return TRUE if @stack is empty, else FALSE.
 **/
#define STACK_EMPTY(stack) \
	(NIH_LIST_EMPTY (stack))

/**
 * STACK_CREATE:
 * @name: nih_list variable to assign stack to.
 **/
#define STACK_CREATE(name) \
	name = NIH_MUST (nih_list_new (NULL))
/**
 * STACK_SHOW_POP:
 * @stack: Address of stack,
 * @str: string representing element popped off stack.
 *
 * Display message to denote that @str has been popped off @stack.
 * Does nothing if debug build not enabled.
 *
 * Note: we cannot assert that stack is not empty since a caller may
 * have just removed the last entry before calling us. Thus, it is up to
 * the caller to perform such checks.
 *
 * Does nothing if debug build not enabled.
 **/
#ifdef DEBUG_STACK
#define STACK_SHOW_POP(stack, str) \
	STACK_SHOW_CHANGE (stack, "popped", str)
#else
#define STACK_SHOW_POP(stack, str)
#endif

/**
 * STACK_SHOW_PUSH:
 * @stack: Address of stack,
 * @str: string representing element pushed onto stack.
 *
 * Display message to denote that @str has been pushed onto @stack.
 *
 * Does nothing if debug build not enabled.
 **/
#ifdef DEBUG_STACK
#define STACK_SHOW_PUSH(stack, str)              \
	STACK_SHOW_CHANGE (stack, "pushed", str) \
	                                         \
	nih_assert (! STACK_EMPTY (stack))
#else
#define STACK_SHOW_PUSH(stack, str)
#endif

/**
 * STACK_SHOW:
 * @stack: Address of stack.
 *
 * Display contents of @stack.
 *
 * Does nothing if debug build not enabled.
 **/
#ifdef DEBUG_STACK
#define STACK_SHOW(stack)                                     \
{                                                             \
	size_t depth = 0;                                     \
                                                              \
	nih_assert (stack);                                   \
                                                              \
	NIH_LIST_FOREACH (stack, iter) {                      \
		depth++;                                      \
	}                                                     \
                                                              \
	if (STACK_EMPTY (stack)) {                            \
		nih_message ("STACK@%p: empty", stack);       \
	} else {                                              \
		for (NihList *iter = (stack)->next;           \
				iter != (stack) &&            \
				((NihListEntry *)iter)->str;  \
				iter = iter->next, depth--) { \
			nih_message ("STACK@%p[%u]='%s'",     \
				(void *)stack,                \
				(unsigned int)(depth-1),      \
				((NihListEntry *)iter)->str); \
		}                                             \
	}                                                     \
}
#else
#define STACK_SHOW(stack)
#endif

/**
 * STACK_SHOW_CHANGE:
 * @stack: Address of stack,
 * @msg: message to show a stack change,
 * @element_str: string representing element changed on @stack.
 *
 * Display a message showing that the stack has been changed.
 *
 * Does nothing if debug build not enabled.
 **/
#ifdef DEBUG_STACK
#define STACK_SHOW_CHANGE(stack, msg, element_str) \
	nih_assert (msg);                          \
	nih_assert (element_str);                  \
	                                           \
	nih_message ("STACK@%p: %s '%s'",          \
		(void *)stack, msg, element_str);  \
	                                           \
	STACK_SHOW (stack);                        \
	                                           \
	nih_message (" "); /* spacer */
#else
#define STACK_SHOW_CHANGE(stack, msg, element_str)
#endif

/**
 * STACK_PUSH:
 * @stack: Address of stack,
 * @elem: element of type NihListEntry to add to stack.
 *
 * Add @elem to @stack and display message showing how stack changed.
 **/
#define STACK_PUSH(stack, elem)                     \
	nih_list_add_after (stack, &(elem)->entry); \
	                                            \
	STACK_SHOW_PUSH (stack,                     \
		((NihListEntry *)(elem))->str);     \
	                                            \
	nih_assert (! STACK_EMPTY (stack))

/**
 * STACK_PUSH_NEW_ELEM:
 * @stack: Address of stack,
 * @string: String to convert into a new NihListEntry
 * stack element and push onto @stack.
 *
 * Create new stack element from @string and push onto @stack,
 * displaying a message showing how stack changed.
 **/
#define STACK_PUSH_NEW_ELEM(stack, string)            \
{                                                     \
	NihListEntry *e;                              \
	                                              \
	nih_assert (stack);                           \
	nih_assert (string);                          \
	                                              \
	e = NIH_MUST (nih_new (stack, NihListEntry)); \
	nih_list_init (&e->entry);                    \
	e->str = NIH_MUST (nih_strdup (e, string));   \
        STACK_PUSH (stack, e);                        \
}

/**
 * STACK_POP:
 * @stack: Address of stack,
 * @list: list which top stack element will be added to.
 *
 * Remove top element from @stack, returning to caller as @list
 * and display message showing how stack changed.
 *
 * Note that @list is assumed to have had nih_list_new() called
 * on it already.
 **/
#define STACK_POP(stack, list)                     \
	nih_assert (stack);                        \
	nih_assert ((stack)->next);                \
	                                           \
	list = nih_list_add (list, (stack)->next); \
	STACK_SHOW_POP (stack,                     \
		((NihListEntry *)(list))->str)

/**
 * STACK_PEEK:
 * @stack: Address of stack.
 *
 * Return string value of top element on stack.
 **/
#define STACK_PEEK(stack) \
	(((NihListEntry *)(stack)->next)->str)

/**
 * ConditionHandlerData:
 *
 * @condition_name: "start on" or "stop on",
 * @job_class_name: name of *.conf file less the extension.
 *
 * Used to pass multiple values to job_class_get_start_on() /
 * job_class_get_stop_on() handlers.
 *
 **/
typedef struct condition_handler_data {
	const char *condition_name;
	const char *job_class_name;
} ConditionHandlerData;
#endif /* INITCTL_H */
