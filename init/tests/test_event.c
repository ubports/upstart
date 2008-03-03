/* upstart
 *
 * test_event.c - test suite for init/event.c
 *
 * Copyright © 2008 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
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
	char         **env;
	unsigned int   last_id = -1;

	/* Check that we can create a new event; the structure should
	 * be allocated with nih_alloc(), placed in a list and all of the
	 * details filled in.
	 */
	TEST_FUNCTION ("event_new");
	event_init ();

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			env = nih_str_array_new (NULL);
			NIH_MUST (nih_str_array_add (&env, NULL, NULL,
						     "FOO=BAR"));
			NIH_MUST (nih_str_array_add (&env, NULL, NULL,
						     "BAR=FRODO"));
		}

		event = event_new (NULL, "test", env);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_NE (event->id, last_id);
		last_id = event->id;

		TEST_EQ (event->progress, EVENT_PENDING);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ (event->blockers, 0);

		TEST_EQ_STR (event->name, "test");
		TEST_ALLOC_PARENT (event->name, event);

		TEST_EQ_P (event->env, env);
		TEST_ALLOC_PARENT (event->env, event);

		nih_free (event);
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
	event = event_new (NULL, "test", NULL);

	ret = event_find_by_id (event->id);

	TEST_EQ_P (ret, event);

	id = event->id;
	nih_free (event);


	/* Check that we get NULL if the id isn't in either queue. */
	TEST_FEATURE ("with id not in either queue");
	ret = event_find_by_id (id);

	TEST_EQ_P (ret, NULL);
}


void
test_block (void)
{
	Event *event;

	/* Check that calling event_block increments the number of blockers
	 * that the event has.
	 */
	TEST_FUNCTION ("event_block");
	event = event_new (NULL, "test", NULL);
	event->blockers = 4;

	event_block (event);

	TEST_EQ (event->blockers, 5);

	nih_free (event);
}

void
test_unblock (void)
{
	Event *event;

	/* Check that calling event_unblock increments the number of blockers
	 * that the event has.
	 */
	TEST_FUNCTION ("event_unblock");
	event = event_new (NULL, "test", NULL);
	event->blockers = 4;

	event_unblock (event);

	TEST_EQ (event->blockers, 3);

	nih_free (event);
}

void
test_poll (void)
{
	Event         *event;
	EventOperator *oper;
	JobConfig     *config;
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
	config = job_config_new (NULL, "test");
	config->process[PROCESS_MAIN] = job_process_new (config->process);
	config->process[PROCESS_MAIN]->command = "echo";

	config->start_on = event_operator_new (config, EVENT_MATCH,
					       "test", NULL);

	nih_hash_add (jobs, &config->entry);

	event = event_new (NULL, "test", NULL);
	event->id = 0xdeafbeef;

	event_poll ();

	TEST_EQ (event->progress, EVENT_HANDLING);
	TEST_EQ (event->blockers, 1);

	TEST_LIST_NOT_EMPTY (&config->instances);

	job = (Job *)config->instances.next;

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_GT (job->pid[PROCESS_MAIN], 0);

	waitpid (job->pid[PROCESS_MAIN], NULL, 0);

	nih_free (config);
	nih_free (event);


	/* Check that having a handling event in the queue which has blockers
	 * doesn't cause any problem.
	 */
	TEST_FEATURE ("with blocked handling event");
	TEST_ALLOC_FAIL {
		event = event_new (NULL, "test", NULL);
		event->progress = EVENT_HANDLING;
		event->blockers = 1;

		event_poll ();

		TEST_LIST_NOT_EMPTY (&event->entry);
		nih_free (event);
	}


	/* Check that we finish unblocked handling events, leaving the list
	 * empty.  Blocked jobs should be released and the event should be
	 * freed.
	 */
	TEST_FEATURE ("with finished event");
	event = event_new (NULL, "test", NULL);
	event->id = 0xdeafbeef;
	event->progress = EVENT_HANDLING;

	config = job_config_new (NULL, "test");
	config->process[PROCESS_MAIN] = job_process_new (config->process);
	config->process[PROCESS_MAIN]->command = "echo";

	nih_hash_add (jobs, &config->entry);

	job = job_new (config);
	job->goal = JOB_START;
	job->state = JOB_STARTING;
	job->blocked = event;

	TEST_FREE_TAG (event);

	event_poll ();

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_GT (job->pid[PROCESS_MAIN], 0);

	waitpid (job->pid[PROCESS_MAIN], &status, 0);
	TEST_TRUE (WIFEXITED (status));
	TEST_EQ (WEXITSTATUS (status), 0);

	TEST_EQ_P (job->blocked, NULL);

	TEST_FREE (event);

	nih_free (config);


	/* Check that a pending event which doesn't cause any jobs to be
	 * changed goes straight into the done state, thus getting
	 * destroyed.
	 */
	TEST_FEATURE ("with no-op pending event");
	TEST_ALLOC_FAIL {
		event = event_new (NULL, "test", NULL);

		TEST_FREE_TAG (event);

		event_poll ();

		TEST_FREE (event);
	}


	/* Check that a failed event causes another event to be emitted
	 * that has "/failed" appended on the end.  We can obtain the
	 * failed event by hooking a job on it, and using the
	 * cause.
	 */
	TEST_FEATURE ("with failed event");
	event = event_new (NULL, "test", NULL);
	event->failed = TRUE;
	event->progress = EVENT_FINISHED;

	config = job_config_new (NULL, "test");
	config->process[PROCESS_MAIN] = job_process_new (config->process);
	config->process[PROCESS_MAIN]->command = "echo";

	nih_hash_add (jobs, &config->entry);

	config->start_on = event_operator_new (config, EVENT_MATCH,
					       "test/failed", NULL);

	TEST_FREE_TAG (event);

	event_poll ();

	TEST_FREE (event);

	TEST_LIST_NOT_EMPTY (&config->instances);

	job = (Job *)config->instances.next;

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_GT (job->pid[PROCESS_MAIN], 0);

	waitpid (job->pid[PROCESS_MAIN], NULL, 0);

	event_poll ();

	nih_free (config);


	/* Check that failed events do not, themselves, emit new failed
	 * events (otherwise we could be there all night :p)
	 */
	TEST_FEATURE ("with failed failed event");
	event = event_new (NULL, "test/failed", NULL);
	event->failed = TRUE;
	event->progress = EVENT_FINISHED;

	config = job_config_new (NULL, "test");
	config->process[PROCESS_MAIN] = job_process_new (config->process);
	config->process[PROCESS_MAIN]->command = "echo";

	nih_hash_add (jobs, &config->entry);

	config->start_on = event_operator_new (config, EVENT_OR, NULL, NULL);

	oper = event_operator_new (config, EVENT_MATCH,
				   "test/failed", NULL);
	nih_tree_add (&config->start_on->node, &oper->node, NIH_TREE_LEFT);

	oper = event_operator_new (config, EVENT_MATCH,
				   "test/failed/failed", NULL);
	nih_tree_add (&config->start_on->node, &oper->node, NIH_TREE_RIGHT);

	TEST_FREE_TAG (event);

	event_poll ();

	TEST_FREE (event);

	TEST_LIST_EMPTY (&config->instances);

	nih_free (config);
}


void
test_operator_new (void)
{
	EventOperator  *oper;
	char          **env;

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

		TEST_EQ_P (oper->env, NULL);
		TEST_EQ_P (oper->event, NULL);

		nih_free (oper);
	}


	/* Check that environment passed to event_operator_new is reparented
	 * to belong to the structure itself.
	 */
	TEST_FEATURE ("with EVENT_MATCH and environment");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			env = nih_str_array_new (NULL);
			NIH_MUST (nih_str_array_add (&env, NULL,
						     NULL, "foo"));
			NIH_MUST (nih_str_array_add (&env, NULL,
						     NULL, "BAR=frodo"));
		}

		oper = event_operator_new (NULL, EVENT_MATCH, "test", env);

		if (test_alloc_failed) {
			TEST_EQ_P (oper, NULL);
			TEST_ALLOC_PARENT (env, NULL);
			nih_free (env);
			continue;
		}

		TEST_ALLOC_SIZE (oper, sizeof (EventOperator));
		TEST_EQ_P (oper->node.parent, NULL);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_STR (oper->name, "test");
		TEST_ALLOC_PARENT (oper->name, oper);

		TEST_EQ_P (oper->env, env);
		TEST_ALLOC_PARENT (oper->env, oper);

		TEST_EQ_P (oper->event, NULL);

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
		TEST_EQ_P (oper->env, NULL);
		TEST_EQ_P (oper->event, NULL);

		nih_free (oper);
	}
}

void
test_operator_copy (void)
{
	EventOperator *oper = NULL, *copy;
	EventOperator *oper1 = NULL, *oper2 = NULL, *copy1, *copy2;

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
		TEST_EQ_P (copy->env, NULL);
		TEST_EQ_P (copy->event, NULL);

		nih_free (copy);
		nih_free (oper);
	}


	/* Check that we can copy and EVENT_MATCH operator which does not
	 * have any environment or matched event.
	 */
	TEST_FEATURE ("with EVENT_MATCH and no environment or event");
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
		TEST_EQ_P (copy->env, NULL);
		TEST_EQ_P (copy->event, NULL);

		nih_free (copy);
		nih_free (oper);
	}


	/* Check that environment to an EVENT_MATCH operator are also copied,
	 * and each entry within the array copied too.
	 */
	TEST_FEATURE ("with EVENT_MATCH and environment");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			oper = event_operator_new (NULL, EVENT_MATCH,
						   "test", NULL);
			oper->value = TRUE;

			NIH_MUST (nih_str_array_add (&oper->env, oper,
						     NULL, "FOO=foo"));
			NIH_MUST (nih_str_array_add (&oper->env, oper,
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

		TEST_ALLOC_PARENT (copy->env, copy);
		TEST_ALLOC_SIZE (copy->env, sizeof (char *) * 3);
		TEST_ALLOC_PARENT (copy->env[0], copy->env);
		TEST_ALLOC_PARENT (copy->env[1], copy->env);
		TEST_EQ_STR (copy->env[0], "FOO=foo");
		TEST_EQ_STR (copy->env[1], "BAR=bar");
		TEST_EQ_P (copy->env[2], NULL);

		TEST_EQ_P (copy->event, NULL);

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

			oper->event = event_new (oper, "test", NULL);
			event_block (oper->event);
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
		TEST_EQ_P (copy->env, NULL);

		TEST_EQ_P (copy->event, oper->event);
		TEST_EQ (copy->event->blockers, 2);

		nih_free (copy);
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
			oper1->event = event_new (oper1, "foo", NULL);
			event_block (oper1->event);
			nih_tree_add (&oper->node, &oper1->node,
				      NIH_TREE_LEFT);

			oper2 = event_operator_new (NULL, EVENT_MATCH,
						    "bar", NULL);
			oper2->value = TRUE;
			oper2->event = event_new (oper2, "foo", NULL);
			event_block (oper2->event);
			nih_tree_add (&oper->node, &oper2->node,
				      NIH_TREE_RIGHT);
		}

		copy = event_operator_copy (NULL, oper);

		if (test_alloc_failed) {
			TEST_EQ_P (copy, NULL);
			nih_free (oper);
			TEST_EQ (oper1->event->blockers, 1);
			nih_free (oper1);
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
		TEST_EQ_P (copy->env, NULL);

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
		TEST_EQ_P (copy1->env, NULL);

		TEST_EQ_P (copy1->event, oper1->event);
		TEST_EQ (copy1->event->blockers, 2);

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
		TEST_EQ_P (copy2->env, NULL);

		TEST_EQ_P (copy2->event, oper2->event);
		TEST_EQ (copy2->event->blockers, 2);

		nih_free (copy2);
		nih_free (copy);
		nih_free (oper1);
		nih_free (oper2);
		nih_free (oper);
	}

	event_poll ();
}

void
test_operator_destroy (void)
{
	EventOperator *oper;
	Event         *event;

	TEST_FUNCTION ("event_operator_destroy");


	/* Check that when an event operator is destroyed, the referenced
	 * event is unblocked and unreferenced.
	 */
	TEST_FEATURE ("with referenced event");
	oper = event_operator_new (NULL, EVENT_MATCH, "foo", NULL);
	oper->value = TRUE;

	event = event_new (NULL, "foo", NULL);
	oper->event = event;
	event_block (event);

	nih_free (oper);

	TEST_EQ (event->blockers, 0);

	nih_free (event);


	/* Check that when an event operator without an event is destroyed,
	 * everything works.
	 */
	TEST_FEATURE ("without referenced event");
	oper = event_operator_new (NULL, EVENT_MATCH, "foo", NULL);
	nih_free (oper);


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
	char          *env1[5], *env2[5];

	TEST_FUNCTION ("event_operator_match");
	event = event_new (NULL, "foo", NULL);

	/* Check that two events with different names do not match. */
	TEST_FEATURE ("with different name events");
	oper = event_operator_new (NULL, EVENT_MATCH, "bar", NULL);

	TEST_FALSE (event_operator_match (oper, event));

	nih_free (oper);


	/* Check that two events with the same names match. */
	TEST_FEATURE ("with same name events");
	oper = event_operator_new (NULL, EVENT_MATCH, "foo", NULL);

	TEST_TRUE (event_operator_match (oper, event));


	/* Check that two events with the same environment lists match. */
	TEST_FEATURE ("with same environment lists");
	event->env = env1;
	event->env[0] = "FRODO=foo";
	event->env[1] = "BILBO=bar";
	event->env[2] = "MERRY=baz";
	event->env[3] = NULL;

	oper->env = env2;
	oper->env[0] = "FRODO=foo";
	oper->env[1] = "BILBO=bar";
	oper->env[2] = "MERRY=baz";
	oper->env[3] = NULL;

	TEST_TRUE (event_operator_match (oper, event));


	/* Check that two events with the same environment lists but wrong
	 * values do not match.
	 */
	TEST_FEATURE ("with environment lists and wrong values");
	event->env = env1;
	event->env[0] = "FRODO=foo";
	event->env[1] = "BILBO=bar";
	event->env[2] = "MERRY=baz";
	event->env[3] = NULL;

	oper->env = env2;
	oper->env[0] = "FRODO=foo";
	oper->env[1] = "BILBO=baz";
	oper->env[2] = "MERRY=bar";
	oper->env[3] = NULL;

	TEST_FALSE (event_operator_match (oper, event));


	/* Check that the environment list in the operator may be shorter. */
	TEST_FEATURE ("with shorter environment list in operator");
	event->env = env1;
	event->env[0] = "FRODO=foo";
	event->env[1] = "BILBO=bar";
	event->env[2] = "MERRY=baz";
	event->env[3] = NULL;

	oper->env = env2;
	oper->env[0] = "FRODO=foo";
	oper->env[1] = "BILBO=bar";
	oper->env[2] = NULL;

	TEST_TRUE (event_operator_match (oper, event));


	/* Check that the operator may match the event's environment list
	 * by value only.
	 */
	TEST_FEATURE ("with matching value lists");
	event->env = env1;
	event->env[0] = "FRODO=foo";
	event->env[1] = "BILBO=bar";
	event->env[2] = "MERRY=baz";
	event->env[3] = NULL;

	oper->env = env2;
	oper->env[0] = "foo";
	oper->env[1] = "bar";
	oper->env[2] = "baz";
	oper->env[3] = NULL;

	TEST_TRUE (event_operator_match (oper, event));


	/* Check that the operator may not match the event's environment list
	 * by value if a value is wrong.
	 */
	TEST_FEATURE ("with non-matching value lists");
	event->env = env1;
	event->env[0] = "FRODO=foo";
	event->env[1] = "BILBO=bar";
	event->env[2] = "MERRY=baz";
	event->env[3] = NULL;

	oper->env = env2;
	oper->env[0] = "foo";
	oper->env[1] = "baz";
	oper->env[2] = "bar";
	oper->env[3] = NULL;

	TEST_FALSE (event_operator_match (oper, event));


	/* Check that when the operator matches by value, the list of values
	 * may be smaller than the actual environment.
	 */
	TEST_FEATURE ("with shorter value list in operator");
	event->env = env1;
	event->env[0] = "FRODO=foo";
	event->env[1] = "BILBO=bar";
	event->env[2] = "MERRY=baz";
	event->env[3] = NULL;

	oper->env = env2;
	oper->env[0] = "foo";
	oper->env[1] = "bar";
	oper->env[2] = NULL;

	TEST_TRUE (event_operator_match (oper, event));


	/* Check that the environment list in event may not be shorter. */
	TEST_FEATURE ("with shorter list in event");
	event->env = env1;
	event->env[0] = "FRODO=foo";
	event->env[1] = "BILBO=bar";
	event->env[2] = NULL;

	oper->env = env2;
	oper->env[0] = "foo";
	oper->env[1] = "bar";
	oper->env[2] = "baz";
	oper->env[3] = NULL;

	TEST_FALSE (event_operator_match (oper, event));


	/* Check that two events with the same environment lists match
	 * in any order.
	 */
	TEST_FEATURE ("with different order environment lists");
	event->env = env1;
	event->env[0] = "FRODO=foo";
	event->env[1] = "BILBO=bar";
	event->env[2] = "MERRY=baz";
	event->env[3] = NULL;

	oper->env = env2;
	oper->env[0] = "BILBO=bar";
	oper->env[1] = "FRODO=foo";
	oper->env[2] = "MERRY=baz";
	oper->env[3] = NULL;

	TEST_TRUE (event_operator_match (oper, event));


	/* Check that the operator may match by value until it first matches
	 * by name.
	 */
	TEST_FEATURE ("with value list and variable list");
	event->env = env1;
	event->env[0] = "FRODO=foo";
	event->env[1] = "BILBO=bar";
	event->env[2] = "MERRY=baz";
	event->env[3] = "PIPPIN=quux";
	event->env[4] = NULL;

	oper->env = env2;
	oper->env[0] = "foo";
	oper->env[1] = "bar";
	oper->env[2] = "PIPPIN=quux";
	oper->env[3] = NULL;

	TEST_TRUE (event_operator_match (oper, event));


	/* Check that unknown variable names never match. */
	TEST_FEATURE ("with unknown variable in operator");
	event->env = env1;
	event->env[0] = "FRODO=foo";
	event->env[1] = "BILBO=bar";
	event->env[2] = NULL;

	oper->env = env2;
	oper->env[0] = "MERRY=baz";
	oper->env[1] = NULL;

	TEST_FALSE (event_operator_match (oper, event));


	/* Check that the operator environment may be globs. */
	TEST_FEATURE ("with globs in operator environment");
	event->env = env1;
	event->env[0] = "FRODO=foo";
	event->env[1] = "BILBO=bar";
	event->env[2] = NULL;

	oper->env = env2;
	oper->env[0] = "BILBO=b?r";
	oper->env[1] = NULL;

	TEST_TRUE (event_operator_match (oper, event));


	/* Check that the operator values may be globs. */
	TEST_FEATURE ("with globs in operator value");
	event->env = env1;
	event->env[0] = "FRODO=foo";
	event->env[1] = "BILBO=bar";
	event->env[2] = NULL;

	oper->env = env2;
	oper->env[0] = "f*";
	oper->env[1] = NULL;

	TEST_TRUE (event_operator_match (oper, event));


	nih_free (oper);
	nih_free (event);
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
	event = event_new (NULL, "frodo", NULL);
	ret = event_operator_handle (oper1, event);

	TEST_EQ (ret, FALSE);
	TEST_EQ (oper1->value, FALSE);
	TEST_EQ (oper2->value, FALSE);
	TEST_EQ (oper3->value, FALSE);
	TEST_EQ_P (oper3->event, NULL);
	TEST_EQ (oper4->value, FALSE);
	TEST_EQ_P (oper4->event, NULL);
	TEST_EQ (oper5->value, FALSE);
	TEST_EQ_P (oper5->event, NULL);

	TEST_EQ (event->blockers, 0);


	/* Check that matching an event in the tree results in the event
	 * being referenced and blocked, and stored in the operator.
	 * The tree value should not be updated since the expression is not
	 * TRUE.
	 */
	TEST_FEATURE ("with matching event");
	event = event_new (NULL, "foo", NULL);
	ret = event_operator_handle (oper1, event);

	TEST_EQ (ret, TRUE);
	TEST_EQ (oper1->value, FALSE);
	TEST_EQ (oper2->value, FALSE);
	TEST_EQ (oper3->value, TRUE);
	TEST_EQ_P (oper3->event, event);
	TEST_EQ (oper4->value, FALSE);
	TEST_EQ_P (oper4->event, NULL);
	TEST_EQ (oper5->value, FALSE);
	TEST_EQ_P (oper5->event, NULL);

	TEST_EQ (event->blockers, 1);


	/* Check that matching an event in the tree results in the event
	 * being referenced and blocked, and stored in the operator.
	 * Since the event tips the balance, it should update the expression.
	 */
	TEST_FEATURE ("with matching event and complete expression");
	event = event_new (NULL, "bar", NULL);
	ret = event_operator_handle (oper1, event);

	TEST_EQ (ret, TRUE);
	TEST_EQ (oper1->value, TRUE);
	TEST_EQ (oper2->value, TRUE);
	TEST_EQ (oper3->value, TRUE);
	TEST_NE_P (oper3->event, NULL);
	TEST_EQ (oper4->value, TRUE);
	TEST_EQ_P (oper4->event, event);
	TEST_EQ (oper5->value, FALSE);
	TEST_EQ_P (oper5->event, NULL);

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
test_operator_collect (void)
{
	EventOperator  *root, *oper1, *oper2, *oper3, *oper4, *oper5, *oper6;
	Event          *event1, *event2, *event3;
	NihList        *list;
	NihListEntry   *entry;
	char          **env;
	size_t          len;

	TEST_FUNCTION ("event_operator_collect");
	root = event_operator_new (NULL, EVENT_OR, NULL, NULL);
	oper1 = event_operator_new (root, EVENT_AND, NULL, NULL);
	oper2 = event_operator_new (root, EVENT_AND, NULL, NULL);
	oper3 = event_operator_new (root, EVENT_MATCH, "foo", NULL);
	oper4 = event_operator_new (root, EVENT_MATCH, "bar", NULL);
	oper5 = event_operator_new (root, EVENT_MATCH, "frodo", NULL);
	oper6 = event_operator_new (root, EVENT_MATCH, "bilbo", NULL);

	nih_tree_add (&root->node, &oper1->node, NIH_TREE_LEFT);
	nih_tree_add (&root->node, &oper2->node, NIH_TREE_RIGHT);
	nih_tree_add (&oper1->node, &oper3->node, NIH_TREE_LEFT);
	nih_tree_add (&oper1->node, &oper4->node, NIH_TREE_RIGHT);
	nih_tree_add (&oper2->node, &oper5->node, NIH_TREE_LEFT);
	nih_tree_add (&oper2->node, &oper6->node, NIH_TREE_RIGHT);

	root->value = TRUE;

	oper1->value = TRUE;

	oper3->value = TRUE;
	oper3->event = event1 = event_new (NULL, "foo", NULL);
	event_block (oper3->event);

	NIH_MUST (nih_str_array_add (&oper3->event->env, oper3->event,
				     NULL, "FOO=APPLE"));
	NIH_MUST (nih_str_array_add (&oper3->event->env, oper3->event,
				     NULL, "TEA=YES"));

	oper4->value = TRUE;
	oper4->event = event2 = event_new (NULL, "bar", NULL);
	event_block (oper4->event);

	NIH_MUST (nih_str_array_add (&oper4->event->env, oper4->event,
				     NULL, "BAR=ORANGE"));
	NIH_MUST (nih_str_array_add (&oper4->event->env, oper4->event,
				     NULL, "COFFEE=NO"));

	oper6->value = TRUE;
	oper6->event = event3 = event_new (NULL, "bilbo", NULL);
	event_block (oper6->event);

	NIH_MUST (nih_str_array_add (&oper6->event->env, oper6->event,
				     NULL, "FRODO=BAGGINS"));
	NIH_MUST (nih_str_array_add (&oper6->event->env, oper6->event,
				     NULL, "BILBO=WIBBLE"));


	/* Check that if we iterate without giving any pointers, nothing
	 * happens (a good test of the logic, but utterly pointless).
	 */
	TEST_FEATURE ("with no arguments");
	TEST_ALLOC_FAIL {
		event_operator_collect (root, NULL, NULL, NULL, NULL, NULL);

		TEST_EQ (oper3->event->blockers, 1);
		TEST_EQ (oper4->event->blockers, 1);
		TEST_EQ (oper6->event->blockers, 1);
	}


	/* Check that if we give an environment table, the environment from
	 * each of the events is appended to it; except for the event that
	 * was matched but not in the true tree.
	 */
	TEST_FEATURE ("with environment table");
	TEST_ALLOC_FAIL {
		env = NULL;
		len = 0;

		event_operator_collect (root, &env, NULL, &len, NULL, NULL);

		TEST_NE_P (env, NULL);
		TEST_ALLOC_SIZE (env, sizeof (char *) * 5);
		TEST_EQ (len, 4);

		TEST_ALLOC_PARENT (env[0], env);
		TEST_EQ_STR (env[0], "FOO=APPLE");
		TEST_ALLOC_PARENT (env[1], env);
		TEST_EQ_STR (env[1], "TEA=YES");
		TEST_ALLOC_PARENT (env[2], env);
		TEST_EQ_STR (env[2], "BAR=ORANGE");
		TEST_ALLOC_PARENT (env[3], env);
		TEST_EQ_STR (env[3], "COFFEE=NO");
		TEST_EQ_P (env[4], NULL);

		TEST_EQ (oper3->event->blockers, 1);
		TEST_EQ (oper4->event->blockers, 1);
		TEST_EQ (oper6->event->blockers, 1);

		nih_free (env);
	}


	/* Check that if we also give the name of an environment variable,
	 * this will contain a space-separated list of the event names.
	 */
	TEST_FEATURE ("with environment variable for event list");
	TEST_ALLOC_FAIL {
		env = NULL;
		len = 0;

		event_operator_collect (root, &env, NULL, &len,
					"UPSTART_EVENTS", NULL);

		TEST_NE_P (env, NULL);
		TEST_ALLOC_SIZE (env, sizeof (char *) * 6);
		TEST_EQ (len, 5);

		TEST_ALLOC_PARENT (env[0], env);
		TEST_EQ_STR (env[0], "FOO=APPLE");
		TEST_ALLOC_PARENT (env[1], env);
		TEST_EQ_STR (env[1], "TEA=YES");
		TEST_ALLOC_PARENT (env[2], env);
		TEST_EQ_STR (env[2], "BAR=ORANGE");
		TEST_ALLOC_PARENT (env[3], env);
		TEST_EQ_STR (env[3], "COFFEE=NO");
		TEST_ALLOC_PARENT (env[4], env);
		TEST_EQ_STR (env[4], "UPSTART_EVENTS=foo bar");
		TEST_EQ_P (env[5], NULL);

		TEST_EQ (oper3->event->blockers, 1);
		TEST_EQ (oper4->event->blockers, 1);
		TEST_EQ (oper6->event->blockers, 1);

		nih_free (env);
	}


	/* Check that if we get a list to append to, the events are appended
	 * in tree order and each event is referenced and blocked; the
	 * event that was matched, but not in the operator tree, should
	 * not be added.
	 */
	TEST_FEATURE ("with list to append to");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			list = nih_list_new (NULL);
		}

		event_operator_collect (root, NULL, NULL, NULL, NULL, list);

		TEST_LIST_NOT_EMPTY (list);

		entry = (NihListEntry *)list->next;
		TEST_ALLOC_SIZE (entry, sizeof (NihListEntry));
		TEST_ALLOC_PARENT (entry, list);
		TEST_EQ_P (entry->data, oper3->event);
		TEST_EQ (oper3->event->blockers, 2);
		event_unblock (oper3->event);
		nih_free (entry);

		entry = (NihListEntry *)list->next;
		TEST_ALLOC_SIZE (entry, sizeof (NihListEntry));
		TEST_ALLOC_PARENT (entry, list);
		TEST_EQ_P (entry->data, oper4->event);
		TEST_EQ (oper4->event->blockers, 2);
		event_unblock (oper4->event);
		nih_free (entry);

		TEST_LIST_EMPTY (list);

		TEST_EQ (oper6->event->blockers, 1);

		nih_free (list);
	}


	/* Check that if we give all the arguments, all of the side-effects
	 * are combined.
	 */
	TEST_FEATURE ("with all arguments");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			list = nih_list_new (NULL);
		}

		env = NULL;
		len = 0;

		event_operator_collect (root, &env, NULL, &len,
					"UPSTART_EVENTS", list);

		TEST_LIST_NOT_EMPTY (list);

		entry = (NihListEntry *)list->next;
		TEST_ALLOC_SIZE (entry, sizeof (NihListEntry));
		TEST_ALLOC_PARENT (entry, list);
		TEST_EQ_P (entry->data, oper3->event);
		TEST_EQ (oper3->event->blockers, 2);
		event_unblock (oper3->event);
		nih_free (entry);

		entry = (NihListEntry *)list->next;
		TEST_ALLOC_SIZE (entry, sizeof (NihListEntry));
		TEST_ALLOC_PARENT (entry, list);
		TEST_EQ_P (entry->data, oper4->event);
		TEST_EQ (oper4->event->blockers, 2);
		event_unblock (oper4->event);
		nih_free (entry);

		TEST_LIST_EMPTY (list);

		TEST_EQ (oper6->event->blockers, 1);

		TEST_NE_P (env, NULL);
		TEST_ALLOC_SIZE (env, sizeof (char *) * 6);
		TEST_EQ (len, 5);

		TEST_ALLOC_PARENT (env[0], env);
		TEST_EQ_STR (env[0], "FOO=APPLE");
		TEST_ALLOC_PARENT (env[1], env);
		TEST_EQ_STR (env[1], "TEA=YES");
		TEST_ALLOC_PARENT (env[2], env);
		TEST_EQ_STR (env[2], "BAR=ORANGE");
		TEST_ALLOC_PARENT (env[3], env);
		TEST_EQ_STR (env[3], "COFFEE=NO");
		TEST_ALLOC_PARENT (env[4], env);
		TEST_EQ_STR (env[4], "UPSTART_EVENTS=foo bar");
		TEST_EQ_P (env[5], NULL);

		nih_free (env);
		nih_free (list);
	}


	/* Check that if no events are matched, the list remains empty and
	 * the environment table only has an empty events list.
	 */
	TEST_FEATURE ("with all arguments but no matches");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			list = nih_list_new (NULL);
		}

		env = NULL;
		len = 0;

		event_operator_collect (oper5, &env, NULL, &len,
					"UPSTART_EVENTS", list);

		TEST_LIST_EMPTY (list);

		TEST_EQ (oper3->event->blockers, 1);
		TEST_EQ (oper4->event->blockers, 1);
		TEST_EQ (oper6->event->blockers, 1);

		TEST_NE_P (env, NULL);
		TEST_ALLOC_SIZE (env, sizeof (char *) * 2);
		TEST_EQ (len, 1);

		TEST_ALLOC_PARENT (env[0], env);
		TEST_EQ_STR (env[0], "UPSTART_EVENTS=");
		TEST_EQ_P (env[1], NULL);

		nih_free (env);
		nih_free (list);
	}


	nih_free (root);
	nih_free (event1);
	nih_free (event2);
	nih_free (event3);
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

	event1 = event_new (NULL, "foo", NULL);
	event2 = event_new (NULL, "bar", NULL);

	event_operator_handle (oper1, event1);
	event_operator_handle (oper1, event2);

	TEST_EQ (oper1->value, TRUE);
	TEST_EQ (oper2->value, TRUE);
	TEST_EQ (oper3->value, TRUE);
	TEST_EQ_P (oper3->event, event1);
	TEST_EQ (oper4->value, TRUE);
	TEST_EQ (oper4->event, event2);
	TEST_EQ (oper5->value, FALSE);

	TEST_EQ (event1->blockers, 1);
	TEST_EQ (event2->blockers, 1);

	event_operator_reset (oper1);

	TEST_EQ (oper1->value, FALSE);
	TEST_EQ (oper2->value, FALSE);
	TEST_EQ (oper3->value, FALSE);
	TEST_EQ_P (oper3->event, NULL);
	TEST_EQ (oper4->value, FALSE);
	TEST_EQ (oper4->event, NULL);
	TEST_EQ (oper5->value, FALSE);

	TEST_EQ (event1->blockers, 0);
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
	test_block ();
	test_unblock ();
	test_poll ();

	test_operator_new ();
	test_operator_copy ();
	test_operator_destroy ();
	test_operator_update ();
	test_operator_match ();
	test_operator_handle ();
	test_operator_collect ();
	test_operator_reset ();

	return 0;
}
