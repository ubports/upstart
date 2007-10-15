/* upstart
 *
 * test_event.c - test suite for init/event.c
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

#include <nih/test.h>

#include <sys/types.h>
#include <sys/wait.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/io.h>

#include "job.h"
#include "event.h"


void
test_new (void)
{
	Event         *event;
	char         **args, **env;
	unsigned int   last_id = -1;

	/* Check that we can create a new event; the structure should
	 * be allocated with nih_alloc(), placed in a list and all of the
	 * details filled in.
	 */
	TEST_FUNCTION ("event_new");
	event_init ();

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			args = nih_str_array_new (NULL);
			NIH_MUST (nih_str_array_add (&args, NULL, NULL,
						     "foo"));
			NIH_MUST (nih_str_array_add (&args, NULL, NULL,
						     "bar"));

			env = nih_str_array_new (NULL);
			NIH_MUST (nih_str_array_add (&env, NULL, NULL,
						     "FOO=BAR"));
		}

		event = event_new (NULL, "test", args, env);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_NE (event->id, last_id);
		last_id = event->id;

		TEST_EQ (event->progress, EVENT_PENDING);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);

		TEST_EQ_STR (event->name, "test");
		TEST_ALLOC_PARENT (event->name, event);

		TEST_EQ_P (event->args, args);
		TEST_ALLOC_PARENT (event->args, event);

		TEST_EQ_P (event->env, env);
		TEST_ALLOC_PARENT (event->env, event);

		nih_list_free (&event->entry);
	}
}

void
test_find_by_id (void)
{
	Event        *event, *ret;
	unsigned int  id;

	TEST_FUNCTION ("event_find_by_id");

	/* Check that we can locate an event in the queue by its id, and
	 * have it returned.
	 */
	TEST_FEATURE ("with id in pending queue");
	event = event_new (NULL, "test", NULL, NULL);

	ret = event_find_by_id (event->id);

	TEST_EQ_P (ret, event);

	id = event->id;
	nih_list_free (&event->entry);


	/* Check that we get NULL if the id isn't in either queue. */
	TEST_FEATURE ("with id not in either queue");
	ret = event_find_by_id (id);

	TEST_EQ_P (ret, NULL);
}


void
test_ref (void)
{
	Event *event;

	/* Check that calling event_ref increments the number of references
	 * that the event has, while leaving the blockers at zero.
	 */
	TEST_FUNCTION ("event_ref");
	event = event_new (NULL, "test", NULL, NULL);
	event->refs = 4;

	event_ref (event);

	TEST_EQ (event->refs, 5);
	TEST_EQ (event->blockers, 0);

	nih_list_free (&event->entry);
}

void
test_unref (void)
{
	Event *event;

	/* Check that calling event_unref decrements the number of references
	 * that the event has, while leaving the blockers at zero.
	 */
	TEST_FUNCTION ("event_unref");
	event = event_new (NULL, "test", NULL, NULL);
	event->refs = 4;

	event_unref (event);

	TEST_EQ (event->refs, 3);
	TEST_EQ (event->blockers, 0);

	nih_list_free (&event->entry);
}

void
test_block (void)
{
	Event *event;

	/* Check that calling event_block increments the number of blockers
	 * that the event has, while leaving the references at zero.
	 */
	TEST_FUNCTION ("event_block");
	event = event_new (NULL, "test", NULL, NULL);
	event->blockers = 4;

	event_block (event);

	TEST_EQ (event->blockers, 5);
	TEST_EQ (event->refs, 0);

	nih_list_free (&event->entry);
}

void
test_unblock (void)
{
	Event *event;

	/* Check that calling event_unblock increments the number of blockers
	 * that the event has, while leaving the references at zero.
	 */
	TEST_FUNCTION ("event_unblock");
	event = event_new (NULL, "test", NULL, NULL);
	event->blockers = 4;

	event_unblock (event);

	TEST_EQ (event->blockers, 3);
	TEST_EQ (event->refs, 0);

	nih_list_free (&event->entry);
}


static int destructor_called = 0;

static int
my_destructor (void *ptr)
{
	destructor_called++;

	return 0;
}

void
test_poll (void)
{
	Event         *event;
	EventOperator *oper;
	Job           *job;
	int            status;

	TEST_FUNCTION ("event_poll");
	job_init ();
	event_init ();


	/* Check that a pending event in the queue is handled, with any
	 * jobs started or stopped as a result.  The event should remain
	 * in the handling state while the job is blocking it.
	 */
	TEST_FEATURE ("with pending event");
	job = job_new (NULL, "test");
	job->process[PROCESS_MAIN] = job_process_new (job->process);
	job->process[PROCESS_MAIN]->command = "echo";

	job->start_on = event_operator_new (job, EVENT_MATCH, "test", NULL);

	event = event_new (NULL, "test", NULL, NULL);
	event->id = 0xdeafbeef;

	event_poll ();

	TEST_EQ (event->progress, EVENT_HANDLING);
	TEST_EQ (event->refs, 1);
	TEST_EQ (event->blockers, 1);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_GT (job->process[PROCESS_MAIN]->pid, 0);

	waitpid (job->process[PROCESS_MAIN]->pid, NULL, 0);

	nih_list_free (&job->entry);
	nih_list_free (&event->entry);


	/* Check that having a handling event in the queue which has blockers
	 * doesn't cause any problem.
	 */
	TEST_FEATURE ("with blocked handling event");
	TEST_ALLOC_FAIL {
		event = event_new (NULL, "test", NULL, NULL);
		event->progress = EVENT_HANDLING;
		event->blockers = 1;

		event_poll ();

		TEST_LIST_NOT_EMPTY (&event->entry);
		nih_list_free (&event->entry);
	}


	/* Check that we finish unblocked handling events, leaving the list
	 * empty.  Blocked jobs should be released and the event should be
	 * freed.
	 */
	TEST_FEATURE ("with finished event");
	event = event_new (NULL, "test", NULL, NULL);
	event->id = 0xdeafbeef;
	event->progress = EVENT_HANDLING;

	job = job_new (NULL, "test");
	job->goal = JOB_START;
	job->state = JOB_STARTING;
	job->blocked = event;
	job->process[PROCESS_MAIN] = job_process_new (job->process);
	job->process[PROCESS_MAIN]->command = "echo";

	event_ref (job->blocked);

	destructor_called = 0;
	nih_alloc_set_destructor (event, my_destructor);

	event_poll ();

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_GT (job->process[PROCESS_MAIN]->pid, 0);

	waitpid (job->process[PROCESS_MAIN]->pid, &status, 0);
	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_EQ_P (job->blocked, NULL);

	TEST_TRUE (destructor_called);


	/* Check that a finished event with remaining references is held
	 * in the done state, and not freed immediately.
	 */
	TEST_FEATURE ("with referenced event");
	TEST_ALLOC_FAIL {
		event = event_new (NULL, "test", NULL, NULL);
		event->progress = EVENT_DONE;
		event->refs = 1;

		event_poll ();

		TEST_LIST_NOT_EMPTY (&event->entry);
		nih_list_free (&event->entry);
	}


	/* Check that a pending event which doesn't cause any jobs to be
	 * changed goes straight into the done state, thus getting
	 * destroyed.
	 */
	TEST_FEATURE ("with no-op pending event");
	TEST_ALLOC_FAIL {
		event = event_new (NULL, "test", NULL, NULL);

		destructor_called = 0;
		nih_alloc_set_destructor (event, my_destructor);

		event_poll ();

		TEST_TRUE (destructor_called);
	}


	/* Check that a failed event causes another event to be emitted
	 * that has "/failed" appended on the end.  We can obtain the
	 * failed event by hooking a job on it, and using the
	 * cause.
	 */
	TEST_FEATURE ("with failed event");
	event = event_new (NULL, "test", NULL, NULL);
	event->failed = TRUE;
	event->progress = EVENT_FINISHED;

	job = job_new (NULL, "test");
	job->process[PROCESS_MAIN] = job_process_new (job->process);
	job->process[PROCESS_MAIN]->command = "echo";

	job->start_on = event_operator_new (job, EVENT_MATCH,
					    "test/failed", NULL);

	destructor_called = 0;
	nih_alloc_set_destructor (event, my_destructor);

	event_poll ();

	TEST_TRUE (destructor_called);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_GT (job->process[PROCESS_MAIN]->pid, 0);

	waitpid (job->process[PROCESS_MAIN]->pid, NULL, 0);

	TEST_EQ_STR (job->start_on->event->name, "test/failed");

	event_poll ();

	nih_list_free (&job->entry);


	/* Check that failed events do not, themselves, emit new failed
	 * events (otherwise we could be there all night :p)
	 */
	TEST_FEATURE ("with failed failed event");
	event = event_new (NULL, "test/failed", NULL, NULL);
	event->failed = TRUE;
	event->progress = EVENT_FINISHED;

	job = job_new (NULL, "test");
	job->process[PROCESS_MAIN] = job_process_new (job->process);
	job->process[PROCESS_MAIN]->command = "echo";

	job->start_on = event_operator_new (job, EVENT_OR, NULL, NULL);

	oper = event_operator_new (job, EVENT_MATCH,
				   "test/failed", NULL);
	nih_tree_add (&job->start_on->node, &oper->node, NIH_TREE_LEFT);

	oper = event_operator_new (job, EVENT_MATCH,
				   "test/failed/failed", NULL);
	nih_tree_add (&job->start_on->node, &oper->node, NIH_TREE_RIGHT);

	destructor_called = 0;
	nih_alloc_set_destructor (event, my_destructor);

	event_poll ();

	TEST_TRUE (destructor_called);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_WAITING);
	TEST_EQ (job->process[PROCESS_MAIN]->pid, 0);

	nih_list_free (&job->entry);
}


void
test_operator_new (void)
{
	EventOperator  *oper;
	char          **args;

	TEST_FUNCTION ("event_operator_new");


	/* Check that we can create a new EventOperator structure to match
	 * an event, and have the details filled in and returned.  It
	 * should not be placed into any tree structure.
	 */
	TEST_FEATURE ("with EVENT_MATCH");
	TEST_ALLOC_FAIL {
		oper = event_operator_new (NULL, EVENT_MATCH, "test", NULL);

		if (test_alloc_failed) {
			TEST_EQ_P (oper, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (oper, sizeof (EventOperator));
		TEST_EQ_P (oper->node.parent, NULL);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_STR (oper->name, "test");
		TEST_ALLOC_PARENT (oper->name, oper);

		TEST_EQ_P (oper->args, NULL);
		TEST_EQ_P (oper->event, NULL);
		TEST_EQ (oper->blocked, FALSE);

		nih_free (oper);
	}


	/* Check that arguments passed to event_operator_new are reparented
	 * to belong to the structure itself.
	 */
	TEST_FEATURE ("with EVENT_MATCH and arguments");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			args = nih_str_array_new (NULL);
			NIH_MUST (nih_str_array_add (&args, NULL,
						     NULL, "foo"));
			NIH_MUST (nih_str_array_add (&args, NULL,
						     NULL, "bar"));
		}

		oper = event_operator_new (NULL, EVENT_MATCH, "test", args);

		if (test_alloc_failed) {
			TEST_EQ_P (oper, NULL);
			TEST_ALLOC_PARENT (args, NULL);
			nih_free (args);
			continue;
		}

		TEST_ALLOC_SIZE (oper, sizeof (EventOperator));
		TEST_EQ_P (oper->node.parent, NULL);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_STR (oper->name, "test");
		TEST_ALLOC_PARENT (oper->name, oper);

		TEST_EQ_P (oper->args, args);
		TEST_ALLOC_PARENT (oper->args, oper);

		TEST_EQ_P (oper->event, NULL);
		TEST_EQ (oper->blocked, FALSE);

		nih_free (oper);
	}


	/* Check that an ordinary operator needs no name attached. */
	TEST_FEATURE ("with EVENT_OR");
	TEST_ALLOC_FAIL {
		oper = event_operator_new (NULL, EVENT_OR, NULL, NULL);

		if (test_alloc_failed) {
			TEST_EQ_P (oper, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (oper, sizeof (EventOperator));
		TEST_EQ_P (oper->node.parent, NULL);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->name, NULL);
		TEST_EQ_P (oper->args, NULL);
		TEST_EQ_P (oper->event, NULL);
		TEST_EQ (oper->blocked, FALSE);

		nih_free (oper);
	}
}

void
test_operator_copy (void)
{
	EventOperator *oper, *copy;
	EventOperator *oper1, *oper2, *copy1, *copy2;

	TEST_FUNCTION ("event_operator_copy");
	event_init ();

	/* Check that we can copy a plain event operator, the value should
	 * be copied as well, and the other fields left as NULL.
	 */
	TEST_FEATURE ("with EVENT_OR");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			oper = event_operator_new (NULL, EVENT_OR, NULL, NULL);
			oper->value = TRUE;
		}

		copy = event_operator_copy (NULL, oper);

		if (test_alloc_failed) {
			TEST_EQ_P (copy, NULL);
			nih_free (oper);
			continue;
		}

		TEST_ALLOC_SIZE (copy, sizeof (EventOperator));
		TEST_EQ_P (copy->node.parent, NULL);
		TEST_EQ_P (copy->node.left, NULL);
		TEST_EQ_P (copy->node.right, NULL);
		TEST_EQ (copy->type, EVENT_OR);
		TEST_EQ (copy->value, TRUE);
		TEST_EQ_P (copy->name, NULL);
		TEST_EQ_P (copy->args, NULL);
		TEST_EQ_P (copy->event, NULL);
		TEST_EQ (copy->blocked, FALSE);

		nih_free (copy);
		nih_free (oper);
	}


	/* Check that we can copy and EVENT_MATCH operator which does not
	 * have any arguments or matched event.
	 */
	TEST_FEATURE ("with EVENT_MATCH and no arguments or event");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			oper = event_operator_new (NULL, EVENT_MATCH,
						   "test", NULL);
			oper->value = TRUE;
		}

		copy = event_operator_copy (NULL, oper);

		if (test_alloc_failed) {
			TEST_EQ_P (copy, NULL);
			nih_free (oper);
			continue;
		}

		TEST_ALLOC_SIZE (copy, sizeof (EventOperator));
		TEST_EQ_P (copy->node.parent, NULL);
		TEST_EQ_P (copy->node.left, NULL);
		TEST_EQ_P (copy->node.right, NULL);
		TEST_EQ (copy->type, EVENT_MATCH);
		TEST_EQ (copy->value, TRUE);
		TEST_EQ_STR (copy->name, "test");
		TEST_ALLOC_PARENT (copy->name, copy);
		TEST_EQ_P (copy->args, NULL);
		TEST_EQ_P (copy->event, NULL);
		TEST_EQ (copy->blocked, FALSE);

		nih_free (copy);
		nih_free (oper);
	}


	/* Check that arguments to an EVENT_MATCH operator are also copied,
	 * and each argument within the array copied too.
	 */
	TEST_FEATURE ("with EVENT_MATCH and arguments");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			oper = event_operator_new (NULL, EVENT_MATCH,
						   "test", NULL);
			oper->value = TRUE;

			NIH_MUST (nih_str_array_add (&oper->args, oper,
						     NULL, "FOO=foo"));
			NIH_MUST (nih_str_array_add (&oper->args, oper,
						     NULL, "BAR=bar"));
		}

		copy = event_operator_copy (NULL, oper);

		if (test_alloc_failed) {
			TEST_EQ_P (copy, NULL);
			nih_free (oper);
			continue;
		}

		TEST_ALLOC_SIZE (copy, sizeof (EventOperator));
		TEST_EQ_P (copy->node.parent, NULL);
		TEST_EQ_P (copy->node.left, NULL);
		TEST_EQ_P (copy->node.right, NULL);
		TEST_EQ (copy->type, EVENT_MATCH);
		TEST_EQ (copy->value, TRUE);
		TEST_EQ_STR (copy->name, "test");
		TEST_ALLOC_PARENT (copy->name, copy);

		TEST_ALLOC_PARENT (copy->args, copy);
		TEST_ALLOC_SIZE (copy->args, sizeof (char *) * 3);
		TEST_ALLOC_PARENT (copy->args[0], copy->args);
		TEST_ALLOC_PARENT (copy->args[1], copy->args);
		TEST_EQ_STR (copy->args[0], "FOO=foo");
		TEST_EQ_STR (copy->args[1], "BAR=bar");
		TEST_EQ_P (copy->args[2], NULL);

		TEST_EQ_P (copy->event, NULL);
		TEST_EQ (copy->blocked, FALSE);

		nih_free (copy);
		nih_free (oper);
	}


	/* Check that if the EVENT_MATCH operator has a referenced event,
	 * the event is copied and referenced a second time.
	 */
	TEST_FEATURE ("with EVENT_MATCH and referenced event");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			oper = event_operator_new (NULL, EVENT_MATCH,
						   "test", NULL);
			oper->value = TRUE;

			oper->event = event_new (oper, "test", NULL, NULL);
			event_ref (oper->event);
		}

		copy = event_operator_copy (NULL, oper);

		if (test_alloc_failed) {
			TEST_EQ_P (copy, NULL);
			nih_free (oper);
			continue;
		}

		TEST_ALLOC_SIZE (copy, sizeof (EventOperator));
		TEST_EQ_P (copy->node.parent, NULL);
		TEST_EQ_P (copy->node.left, NULL);
		TEST_EQ_P (copy->node.right, NULL);
		TEST_EQ (copy->type, EVENT_MATCH);
		TEST_EQ (copy->value, TRUE);
		TEST_EQ_STR (copy->name, "test");
		TEST_ALLOC_PARENT (copy->name, copy);
		TEST_EQ_P (copy->args, NULL);

		TEST_EQ_P (copy->event, oper->event);
		TEST_EQ (copy->event->refs, 2);
		TEST_EQ (copy->event->blockers, 0);
		TEST_EQ (copy->blocked, FALSE);

		event_unref (copy->event);
		nih_free (copy);

		event_unref (oper->event);
		nih_free (oper);
	}


	/* Check that if the EVENT_MATCH operator has a blocked event,
	 * the event is copied and referenced and blocked a second time.
	 */
	TEST_FEATURE ("with EVENT_MATCH and blocked event");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			oper = event_operator_new (NULL, EVENT_MATCH,
						   "test", NULL);
			oper->value = TRUE;

			oper->event = event_new (oper, "test", NULL, NULL);
			event_ref (oper->event);

			event_block (oper->event);
			oper->blocked = TRUE;
		}

		copy = event_operator_copy (NULL, oper);

		if (test_alloc_failed) {
			TEST_EQ_P (copy, NULL);
			TEST_EQ (oper->event->refs, 1);
			TEST_EQ (oper->event->blockers, 1);
			event_unblock (oper->event);
			event_unref (oper->event);
			nih_free (oper);
			continue;
		}

		TEST_ALLOC_SIZE (copy, sizeof (EventOperator));
		TEST_EQ_P (copy->node.parent, NULL);
		TEST_EQ_P (copy->node.left, NULL);
		TEST_EQ_P (copy->node.right, NULL);
		TEST_EQ (copy->type, EVENT_MATCH);
		TEST_EQ (copy->value, TRUE);
		TEST_EQ_STR (copy->name, "test");
		TEST_ALLOC_PARENT (copy->name, copy);
		TEST_EQ_P (copy->args, NULL);

		TEST_EQ_P (copy->event, oper->event);
		TEST_EQ (copy->event->refs, 2);
		TEST_EQ (copy->event->blockers, 2);
		TEST_EQ (copy->blocked, TRUE);

		event_unblock (copy->event);
		event_unref (copy->event);
		nih_free (copy);

		event_unblock (oper->event);
		event_unref (oper->event);
		nih_free (oper);
	}

	/* Check that if the operator has children, these are copied as
	 * well, including their state.
	 */
	TEST_FEATURE ("with children");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			oper = event_operator_new (NULL, EVENT_OR, NULL, NULL);
			oper->value = TRUE;

			oper1 = event_operator_new (NULL, EVENT_MATCH,
						    "foo", NULL);
			oper1->value = TRUE;
			oper1->event = event_new (oper1, "foo", NULL, NULL);
			event_ref (oper1->event);

			event_block (oper1->event);
			oper1->blocked = TRUE;
			nih_tree_add (&oper->node, &oper1->node,
				      NIH_TREE_LEFT);

			oper2 = event_operator_new (NULL, EVENT_MATCH,
						    "bar", NULL);
			oper2->value = TRUE;
			oper2->event = event_new (oper2, "foo", NULL, NULL);
			event_ref (oper2->event);

			event_block (oper2->event);
			oper2->blocked = TRUE;
			nih_tree_add (&oper->node, &oper2->node,
				      NIH_TREE_RIGHT);
		}

		copy = event_operator_copy (NULL, oper);

		if (test_alloc_failed) {
			TEST_EQ_P (copy, NULL);
			nih_free (oper);
			TEST_EQ (oper1->event->refs, 1);
			TEST_EQ (oper1->event->blockers, 1);
			nih_free (oper1);
			TEST_EQ (oper2->event->refs, 1);
			TEST_EQ (oper2->event->blockers, 1);
			nih_free (oper2);
			continue;
		}

		TEST_ALLOC_SIZE (copy, sizeof (EventOperator));
		TEST_ALLOC_PARENT (copy, NULL);
		TEST_EQ_P (copy->node.parent, NULL);
		TEST_NE_P (copy->node.left, NULL);
		TEST_NE_P (copy->node.right, NULL);
		TEST_EQ (copy->type, EVENT_OR);
		TEST_EQ (copy->value, TRUE);
		TEST_EQ_P (copy->name, NULL);
		TEST_EQ_P (copy->args, NULL);

		copy1 = (EventOperator *)copy->node.left;
		TEST_ALLOC_SIZE (copy1, sizeof (EventOperator));
		TEST_ALLOC_PARENT (copy1, copy);
		TEST_EQ_P (copy1->node.parent, &copy->node);
		TEST_EQ_P (copy1->node.left, NULL);
		TEST_EQ_P (copy1->node.right, NULL);
		TEST_EQ (copy1->type, EVENT_MATCH);
		TEST_EQ (copy1->value, TRUE);
		TEST_EQ_STR (copy1->name, "foo");
		TEST_ALLOC_PARENT (copy1->name, copy1);
		TEST_EQ_P (copy1->args, NULL);

		TEST_EQ_P (copy1->event, oper1->event);
		TEST_EQ (copy1->event->refs, 2);
		TEST_EQ (copy1->event->blockers, 2);
		TEST_EQ (copy1->blocked, TRUE);

		event_unblock (copy1->event);
		event_unref (copy1->event);
		nih_free (copy1);

		copy2 = (EventOperator *)copy->node.right;
		TEST_ALLOC_SIZE (copy2, sizeof (EventOperator));
		TEST_ALLOC_PARENT (copy2, copy);
		TEST_EQ_P (copy2->node.parent, &copy->node);
		TEST_EQ_P (copy2->node.left, NULL);
		TEST_EQ_P (copy2->node.right, NULL);
		TEST_EQ (copy2->type, EVENT_MATCH);
		TEST_EQ (copy2->value, TRUE);
		TEST_EQ_STR (copy2->name, "bar");
		TEST_ALLOC_PARENT (copy2->name, copy2);
		TEST_EQ_P (copy2->args, NULL);

		TEST_EQ_P (copy2->event, oper2->event);
		TEST_EQ (copy2->event->refs, 2);
		TEST_EQ (copy2->event->blockers, 2);
		TEST_EQ (copy2->blocked, TRUE);

		event_unblock (copy2->event);
		event_unref (copy2->event);
		nih_free (copy2);

		nih_free (copy);

		event_unblock (oper1->event);
		event_unref (oper1->event);
		nih_free (oper1);

		event_unblock (oper2->event);
		event_unref (oper2->event);
		nih_free (oper2);

		nih_free (oper);
	}

	event_poll ();
}


void
test_operator_update (void)
{
	EventOperator *oper1, *oper2, *oper3;

	TEST_FUNCTION ("event_operator_update");
	oper1 = event_operator_new (NULL, EVENT_OR, NULL, NULL);
	oper2 = event_operator_new (NULL, EVENT_MATCH, "foo", NULL);
	oper3 = event_operator_new (NULL, EVENT_MATCH, "bar", NULL);

	nih_tree_add (&oper1->node, &oper2->node, NIH_TREE_LEFT);
	nih_tree_add (&oper1->node, &oper3->node, NIH_TREE_RIGHT);


	/* Check that EVENT_OR is FALSE if both children are FALSE. */
	TEST_FEATURE ("with EVENT_OR and both children FALSE");
	oper1->value = oper2->value = oper3->value = FALSE;

	event_operator_update (oper1);

	TEST_EQ (oper1->value, FALSE);


	/* Check that EVENT_OR is TRUE if only the left child is TRUE. */
	TEST_FEATURE ("with EVENT_OR and only left child TRUE");
	oper1->value = oper3->value = FALSE;
	oper2->value = TRUE;

	event_operator_update (oper1);

	TEST_EQ (oper1->value, TRUE);


	/* Check that EVENT_OR is TRUE if only the right child is TRUE. */
	TEST_FEATURE ("with EVENT_OR and only right child TRUE");
	oper1->value = oper2->value = FALSE;
	oper3->value = TRUE;

	event_operator_update (oper1);

	TEST_EQ (oper1->value, TRUE);


	/* Check that EVENT_OR is TRUE if both children are TRUE. */
	TEST_FEATURE ("with EVENT_OR and both children TRUE");
	oper1->value = FALSE;
	oper2->value = oper3->value = TRUE;

	event_operator_update (oper1);

	TEST_EQ (oper1->value, TRUE);


	/* Check that EVENT_AND is FALSE if both children are FALSE. */
	TEST_FEATURE ("with EVENT_AND and both children FALSE");
	oper1->type = EVENT_AND;
	oper1->value = oper2->value = oper3->value = FALSE;

	event_operator_update (oper1);

	TEST_EQ (oper1->value, FALSE);


	/* Check that EVENT_AND is FALSE if only the left child is TRUE. */
	TEST_FEATURE ("with EVENT_AND and only left child TRUE");
	oper1->value = oper3->value = FALSE;
	oper2->value = TRUE;

	event_operator_update (oper1);

	TEST_EQ (oper1->value, FALSE);


	/* Check that EVENT_AND is FALSE if only the right child is TRUE. */
	TEST_FEATURE ("with EVENT_AND and only right child TRUE");
	oper1->value = oper2->value = FALSE;
	oper3->value = TRUE;

	event_operator_update (oper1);

	TEST_EQ (oper1->value, FALSE);


	/* Check that EVENT_AND is TRUE if both children are TRUE. */
	TEST_FEATURE ("with EVENT_AND and both children TRUE");
	oper1->value = FALSE;
	oper2->value = oper3->value = TRUE;

	event_operator_update (oper1);

	TEST_EQ (oper1->value, TRUE);


	nih_free (oper1);
	nih_free (oper2);
	nih_free (oper3);
}

void
test_operator_match (void)
{
	EventOperator *oper;
	Event         *event;
	char          *args1[] = { "foo", "bar", "baz", NULL };
	char          *args2[] = { "foo", "bar", "baz", NULL };

	TEST_FUNCTION ("event_operator_match");
	event = event_new (NULL, "foo", NULL, NULL);

	/* Check that two events with different names do not match. */
	TEST_FEATURE ("with different name events");
	oper = event_operator_new (NULL, EVENT_MATCH, "bar", NULL);

	TEST_FALSE (event_operator_match (oper, event));

	nih_free (oper);


	/* Check that two events with the same names match. */
	TEST_FEATURE ("with same name events");
	oper = event_operator_new (NULL, EVENT_MATCH, "foo", NULL);

	TEST_TRUE (event_operator_match (oper, event));


	/* Check that two events with the same arguments lists match. */
	TEST_FEATURE ("with same argument lists");
	oper->args = args1;
	event->args = args2;

	TEST_TRUE (event_operator_match (oper, event));


	/* Check that the argument list in the operator may be shorter. */
	TEST_FEATURE ("with shorter list in operator");
	args1[2] = NULL;

	TEST_TRUE (event_operator_match (oper, event));


	/* Check that the argument list in event may not be shorter. */
	TEST_FEATURE ("with shorter list in event");
	args1[2] = args2[2];
	args2[2] = NULL;

	TEST_FALSE (event_operator_match (oper, event));


	/* Check that the opeartor argument lists may be globs. */
	TEST_FEATURE ("with globs in operator arguments");
	args2[2] = args1[2];
	args1[2] = "b?z*";

	TEST_TRUE (event_operator_match (oper, event));


	nih_free (oper);
	nih_list_free (&event->entry);
}


void
test_operator_handle (void)
{
	EventOperator *oper1, *oper2, *oper3, *oper4, *oper5;
	Event         *event;
	int            ret;

	TEST_FUNCTION ("event_operator_handle");
	oper1 = event_operator_new (NULL, EVENT_OR, NULL, NULL);
	oper2 = event_operator_new (NULL, EVENT_AND, NULL, NULL);
	oper3 = event_operator_new (NULL, EVENT_MATCH, "foo", NULL);
	oper4 = event_operator_new (NULL, EVENT_MATCH, "bar", NULL);
	oper5 = event_operator_new (NULL, EVENT_MATCH, "baz", NULL);

	nih_tree_add (&oper1->node, &oper2->node, NIH_TREE_LEFT);
	nih_tree_add (&oper2->node, &oper3->node, NIH_TREE_LEFT);
	nih_tree_add (&oper2->node, &oper4->node, NIH_TREE_RIGHT);
	nih_tree_add (&oper1->node, &oper5->node, NIH_TREE_RIGHT);


	/* Check that a non-matching event doesn't touch the tree. */
	TEST_FEATURE ("with non-matching event");
	event = event_new (NULL, "frodo", NULL, NULL);
	ret = event_operator_handle (oper1, event);

	TEST_EQ (ret, FALSE);
	TEST_EQ (oper1->value, FALSE);
	TEST_EQ (oper2->value, FALSE);
	TEST_EQ (oper3->value, FALSE);
	TEST_EQ_P (oper3->event, NULL);
	TEST_EQ (oper3->blocked, FALSE);
	TEST_EQ (oper4->value, FALSE);
	TEST_EQ_P (oper4->event, NULL);
	TEST_EQ (oper4->blocked, FALSE);
	TEST_EQ (oper5->value, FALSE);
	TEST_EQ_P (oper5->event, NULL);
	TEST_EQ (oper5->blocked, FALSE);

	TEST_EQ (event->refs, 0);
	TEST_EQ (event->blockers, 0);


	/* Check that matching an event in the tree results in the event
	 * being referenced and blocked, and stored in the operator.
	 * The tree value should not be updated since the expression is not
	 * TRUE.
	 */
	TEST_FEATURE ("with matching event");
	event = event_new (NULL, "foo", NULL, NULL);
	ret = event_operator_handle (oper1, event);

	TEST_EQ (ret, TRUE);
	TEST_EQ (oper1->value, FALSE);
	TEST_EQ (oper2->value, FALSE);
	TEST_EQ (oper3->value, TRUE);
	TEST_EQ_P (oper3->event, event);
	TEST_EQ (oper3->blocked, TRUE);
	TEST_EQ (oper4->value, FALSE);
	TEST_EQ_P (oper4->event, NULL);
	TEST_EQ (oper4->blocked, FALSE);
	TEST_EQ (oper5->value, FALSE);
	TEST_EQ_P (oper5->event, NULL);
	TEST_EQ (oper5->blocked, FALSE);

	TEST_EQ (event->refs, 1);
	TEST_EQ (event->blockers, 1);


	/* Check that matching an event in the tree results in the event
	 * being referenced and blocked, and stored in the operator.
	 * Since the event tips the balance, it should update the expression.
	 */
	TEST_FEATURE ("with matching event and complete expression");
	event = event_new (NULL, "bar", NULL, NULL);
	ret = event_operator_handle (oper1, event);

	TEST_EQ (ret, TRUE);
	TEST_EQ (oper1->value, TRUE);
	TEST_EQ (oper2->value, TRUE);
	TEST_EQ (oper3->value, TRUE);
	TEST_NE_P (oper3->event, NULL);
	TEST_EQ (oper3->blocked, TRUE);
	TEST_EQ (oper4->value, TRUE);
	TEST_EQ_P (oper4->event, event);
	TEST_EQ (oper4->blocked, TRUE);
	TEST_EQ (oper5->value, FALSE);
	TEST_EQ_P (oper5->event, NULL);
	TEST_EQ (oper5->blocked, FALSE);

	TEST_EQ (event->refs, 1);
	TEST_EQ (event->blockers, 1);


	event_operator_reset (oper1);

	nih_free (oper1);
	nih_free (oper2);
	nih_free (oper3);
	nih_free (oper4);
	nih_free (oper5);

	event_poll ();
}

void
test_operator_unblock (void)
{
	EventOperator *oper1, *oper2, *oper3, *oper4, *oper5;
	Event         *event1, *event2;

	/* Check that we can unblock all of the events in the tree, but that
	 * the references remain.
	 */
	TEST_FUNCTION ("event_operator_unblock");
	oper1 = event_operator_new (NULL, EVENT_OR, NULL, NULL);
	oper2 = event_operator_new (NULL, EVENT_AND, NULL, NULL);
	oper3 = event_operator_new (NULL, EVENT_MATCH, "foo", NULL);
	oper4 = event_operator_new (NULL, EVENT_MATCH, "bar", NULL);
	oper5 = event_operator_new (NULL, EVENT_MATCH, "baz", NULL);

	nih_tree_add (&oper1->node, &oper2->node, NIH_TREE_LEFT);
	nih_tree_add (&oper2->node, &oper3->node, NIH_TREE_LEFT);
	nih_tree_add (&oper2->node, &oper4->node, NIH_TREE_RIGHT);
	nih_tree_add (&oper1->node, &oper5->node, NIH_TREE_RIGHT);

	event1 = event_new (NULL, "foo", NULL, NULL);
	event2 = event_new (NULL, "bar", NULL, NULL);

	event_operator_handle (oper1, event1);
	event_operator_handle (oper1, event2);

	TEST_EQ (oper1->value, TRUE);
	TEST_EQ (oper2->value, TRUE);
	TEST_EQ (oper3->value, TRUE);
	TEST_EQ_P (oper3->event, event1);
	TEST_EQ (oper3->blocked, TRUE);
	TEST_EQ (oper4->value, TRUE);
	TEST_EQ (oper4->event, event2);
	TEST_EQ (oper4->blocked, TRUE);
	TEST_EQ (oper5->value, FALSE);
	TEST_EQ (oper5->blocked, FALSE);

	TEST_EQ (event1->refs, 1);
	TEST_EQ (event1->blockers, 1);
	TEST_EQ (event2->refs, 1);
	TEST_EQ (event2->blockers, 1);

	event_operator_unblock (oper1);

	TEST_EQ (oper1->value, TRUE);
	TEST_EQ (oper2->value, TRUE);
	TEST_EQ (oper3->value, TRUE);
	TEST_EQ_P (oper3->event, event1);
	TEST_EQ (oper3->blocked, FALSE);
	TEST_EQ (oper4->value, TRUE);
	TEST_EQ (oper4->event, event2);
	TEST_EQ (oper4->blocked, FALSE);
	TEST_EQ (oper5->value, FALSE);
	TEST_EQ (oper5->blocked, FALSE);

	TEST_EQ (event1->refs, 1);
	TEST_EQ (event1->blockers, 0);
	TEST_EQ (event2->refs, 1);
	TEST_EQ (event2->blockers, 0);

	event_operator_reset (oper1);

	nih_free (oper1);
	nih_free (oper2);
	nih_free (oper3);
	nih_free (oper4);
	nih_free (oper5);

	event_poll ();
}

void
test_operator_reset (void)
{
	EventOperator *oper1, *oper2, *oper3, *oper4, *oper5;
	Event         *event1, *event2;

	/* Check that we can reset all of the operators in the tree,
	 * discarding any events that were referenced or blocked and setting
	 * all the values back to FALSE.
	 */
	TEST_FUNCTION ("event_operator_reset");
	oper1 = event_operator_new (NULL, EVENT_OR, NULL, NULL);
	oper2 = event_operator_new (NULL, EVENT_AND, NULL, NULL);
	oper3 = event_operator_new (NULL, EVENT_MATCH, "foo", NULL);
	oper4 = event_operator_new (NULL, EVENT_MATCH, "bar", NULL);
	oper5 = event_operator_new (NULL, EVENT_MATCH, "baz", NULL);

	nih_tree_add (&oper1->node, &oper2->node, NIH_TREE_LEFT);
	nih_tree_add (&oper2->node, &oper3->node, NIH_TREE_LEFT);
	nih_tree_add (&oper2->node, &oper4->node, NIH_TREE_RIGHT);
	nih_tree_add (&oper1->node, &oper5->node, NIH_TREE_RIGHT);

	event1 = event_new (NULL, "foo", NULL, NULL);
	event2 = event_new (NULL, "bar", NULL, NULL);

	event_operator_handle (oper1, event1);
	event_operator_handle (oper1, event2);

	TEST_EQ (oper1->value, TRUE);
	TEST_EQ (oper2->value, TRUE);
	TEST_EQ (oper3->value, TRUE);
	TEST_EQ_P (oper3->event, event1);
	TEST_EQ (oper3->blocked, TRUE);
	TEST_EQ (oper4->value, TRUE);
	TEST_EQ (oper4->event, event2);
	TEST_EQ (oper4->blocked, TRUE);
	TEST_EQ (oper5->value, FALSE);
	TEST_EQ (oper5->blocked, FALSE);

	TEST_EQ (event1->refs, 1);
	TEST_EQ (event1->blockers, 1);
	TEST_EQ (event2->refs, 1);
	TEST_EQ (event2->blockers, 1);

	event_operator_reset (oper1);

	TEST_EQ (oper1->value, FALSE);
	TEST_EQ (oper2->value, FALSE);
	TEST_EQ (oper3->value, FALSE);
	TEST_EQ_P (oper3->event, NULL);
	TEST_EQ (oper3->blocked, FALSE);
	TEST_EQ (oper4->value, FALSE);
	TEST_EQ (oper4->event, NULL);
	TEST_EQ (oper4->blocked, FALSE);
	TEST_EQ (oper5->value, FALSE);
	TEST_EQ (oper5->blocked, FALSE);

	TEST_EQ (event1->refs, 0);
	TEST_EQ (event1->blockers, 0);
	TEST_EQ (event2->refs, 0);
	TEST_EQ (event2->blockers, 0);

	nih_free (oper1);
	nih_free (oper2);
	nih_free (oper3);
	nih_free (oper4);
	nih_free (oper5);

	event_poll ();
}


int
main (int   argc,
      char *argv[])
{
	test_new ();
	test_find_by_id ();
	test_ref ();
	test_unref ();
	test_block ();
	test_unblock ();
	test_poll ();

	test_operator_new ();
	test_operator_copy ();
	test_operator_update ();
	test_operator_match ();
	test_operator_handle ();
	test_operator_unblock ();
	test_operator_reset ();

	return 0;
}
