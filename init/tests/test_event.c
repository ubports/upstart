/* upstart
 *
 * test_event.c - test suite for init/event.c
 *
 * Copyright © 2010 Canonical Ltd.
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

#include <nih/test.h>

#include <sys/types.h>
#include <sys/wait.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/io.h>
#include <nih/main.h>
#include <nih/error.h>

#include "control.h"
#include "job.h"
#include "event.h"
#include "blocked.h"


void
test_new (void)
{
	Event  *event;
	char  **env;

	/* Check that we can create a new event; the structure should
	 * be allocated with nih_alloc(), placed in a list and all of the
	 * details filled in.
	 */
	TEST_FUNCTION ("event_new");
	event_init ();
	nih_main_loop_init ();

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			env = nih_str_array_new (NULL);
			NIH_MUST (nih_str_array_add (&env, NULL, NULL,
						     "FOO=BAR"));
			NIH_MUST (nih_str_array_add (&env, NULL, NULL,
						     "BAR=FRODO"));
		}

		event = event_new (NULL, "test", env);

		if (test_alloc_failed) {
			TEST_EQ_P (event, NULL);
			TEST_ALLOC_PARENT (env, NULL);
			nih_free (env);
			continue;
		}

		nih_discard (env);

		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_LIST_NOT_EMPTY (&event->entry);

		TEST_EQ (event->progress, EVENT_PENDING);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ (event->blockers, 0);
		TEST_LIST_EMPTY (&event->blocking);

		TEST_EQ_STR (event->name, "test");
		TEST_ALLOC_PARENT (event->name, event);

		TEST_EQ_P (event->env, env);
		TEST_ALLOC_PARENT (event->env, event);

		nih_free (event);
	}
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
	Event *event = NULL;

	TEST_FUNCTION ("event_poll");
	job_class_init ();
	control_init ();


	/* Check that a pending event which does not get blocked goes
	 * straight though and gets freed.
	 */
	TEST_FEATURE ("with unblocked pending event");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event = event_new (NULL, "test", NULL);
		}

		TEST_FREE_TAG (event);

		event_poll ();

		TEST_FREE (event);
	}


	/* Check that a handling event which is not blocked goes
	 * straight though and gets freed.
	 */
	TEST_FEATURE ("with unblocked handling event");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event = event_new (NULL, "test", NULL);
			event->progress = EVENT_HANDLING;
		}

		TEST_FREE_TAG (event);

		event_poll ();

		TEST_FREE (event);
	}


	/* Check that a handling event which is blocked stays in the queue
	 * in the handling state, but does not prevent the loop from exiting.
	 */
	TEST_FEATURE ("with blocked handling event");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event = event_new (NULL, "test", NULL);
			event->progress = EVENT_HANDLING;
			event->blockers = 1;
		}

		TEST_FREE_TAG (event);

		event_poll ();

		TEST_NOT_FREE (event);
		TEST_LIST_NOT_EMPTY (&event->entry);

		nih_free (event);
	}


	/* Check that a finished event is freed.
	 */
	TEST_FEATURE ("with finished event");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event = event_new (NULL, "test", NULL);
			event->progress = EVENT_FINISHED;
		}

		TEST_FREE_TAG (event);

		event_poll ();

		TEST_FREE (event);
	}
}


void
test_pending (void)
{
	JobClass *class = NULL;
	Job      *job;
	Event    *event = NULL;

	/* Check that a pending event in the queue results in jobs being
	 * started and/or stopped and gets moved into the handling state.
	 */
	TEST_FUNCTION ("event_pending");
	nih_error_init ();

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event = event_new (NULL, "test", NULL);

			class = job_class_new (NULL, "test");
			class->task = TRUE;
			class->process[PROCESS_MAIN] = process_new (class->process);
			class->process[PROCESS_MAIN]->command = "echo";

			class->start_on = event_operator_new (
				class, EVENT_MATCH, "test", NULL);

			nih_hash_add (job_classes, &class->entry);
		}

		event_poll ();

		TEST_EQ (event->progress, EVENT_HANDLING);
		TEST_EQ (event->blockers, 1);

		TEST_HASH_NOT_EMPTY (class->instances);

		job = (Job *)nih_hash_lookup (class->instances, "");

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_GT (job->pid[PROCESS_MAIN], 0);

		waitpid (job->pid[PROCESS_MAIN], NULL, 0);

		nih_free (class);
		nih_free (event);
	}
}

void
test_pending_handle_jobs (void)
{
	FILE           *output;
	JobClass       *class = NULL;
	Job            *job = NULL, *ptr;
	Event          *event1 = NULL, *event2 = NULL;
	Event          *event3 = NULL, *event4 = NULL;
	EventOperator  *oper;
	Blocked        *blocked = NULL, *blocked1 = NULL, *blocked2 = NULL;
	char          **env1 = NULL, **env2 = NULL;

	TEST_FUNCTION ("event_pending_handle_jobs");
	program_name = "test";
	output = tmpfile ();


	/* Check that an event that does not match the start operator of
	 * a job does not get blocked and passes straight through the
	 * loop.
	 */
	TEST_FEATURE ("with non-matching event for start");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event1 = event_new (NULL, "biscuit", NULL);

			TEST_FREE_TAG (event1);

			class = job_class_new (NULL, "test");
			class->task = TRUE;

			class->start_on = event_operator_new (
				class, EVENT_MATCH, "wibble", NULL);

			nih_hash_add (job_classes, &class->entry);
		}

		event_poll ();

		TEST_FREE (event1);

		TEST_HASH_EMPTY (class->instances);

		oper = class->start_on;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);

		nih_free (class);
	}


	/* Check that an event that only partially matches an operator
	 * marks the individual node as true, but does not result in the
	 * job being changed yet.  The event should now be blocked on the
	 * job.
	 */
	TEST_FEATURE ("with partial matching event to start");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event1 = event_new (NULL, "wibble", NULL);

			TEST_FREE_TAG (event1);

			class = job_class_new (NULL, "test");
			class->task = TRUE;

			class->start_on = event_operator_new (
				class, EVENT_AND, NULL, NULL);

			oper = event_operator_new (
				class->start_on, EVENT_MATCH, "wibble", NULL);
			nih_tree_add (&class->start_on->node, &oper->node,
				      NIH_TREE_LEFT);

			oper = event_operator_new (
				class->start_on, EVENT_MATCH, "wobble", NULL);
			nih_tree_add (&class->start_on->node, &oper->node,
				      NIH_TREE_RIGHT);

			nih_hash_add (job_classes, &class->entry);
		}

		event_poll ();

		TEST_NOT_FREE (event1);

		TEST_EQ (event1->blockers, 1);

		TEST_HASH_EMPTY (class->instances);

		oper = class->start_on;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);

		oper = (EventOperator *)class->start_on->node.left;
		TEST_EQ (oper->value, TRUE);
		TEST_EQ_P (oper->event, event1);

		oper = (EventOperator *)class->start_on->node.right;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);

		nih_free (class);
		nih_free (event1);
	}


	/* Check that multiple events can complete an operator match and
	 * result in the job being started and the start operator in the
	 * class reset.  The environment from the class, plus the job-unique
	 * variables should be in the instances's environment, since they
	 * would have been copied out of start_env on starting.
	 */
	TEST_FEATURE ("with matching events to start");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event1 = event_new (NULL, "wibble", NULL);
			event2 = event_new (NULL, "wobble", NULL);

			TEST_FREE_TAG (event1);
			TEST_FREE_TAG (event2);

			class = job_class_new (NULL, "test");
			class->task = TRUE;

			assert (nih_str_array_add (&(class->env), class,
						   NULL, "FOO=BAR"));
			assert (nih_str_array_add (&(class->env), class,
						   NULL, "BAR=BAZ"));

			class->start_on = event_operator_new (
				class, EVENT_AND, NULL, NULL);

			oper = event_operator_new (
				class->start_on, EVENT_MATCH, "wibble", NULL);
			nih_tree_add (&class->start_on->node, &oper->node,
				      NIH_TREE_LEFT);

			oper = event_operator_new (
				class->start_on, EVENT_MATCH, "wobble", NULL);
			nih_tree_add (&class->start_on->node, &oper->node,
				      NIH_TREE_RIGHT);

			nih_hash_add (job_classes, &class->entry);
		}

		event_poll ();

		TEST_NOT_FREE (event1);
		TEST_NOT_FREE (event2);

		TEST_EQ (event1->blockers, 1);
		TEST_EQ (event2->blockers, 1);

		TEST_HASH_NOT_EMPTY (class->instances);

		job = (Job *)nih_hash_lookup (class->instances, "");

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);

		TEST_NE_P (job->env, NULL);
		TEST_ALLOC_PARENT (job->env, job);
		TEST_ALLOC_SIZE (job->env, sizeof (char *) * 6);
		TEST_ALLOC_PARENT (job->env[0], job->env);
		TEST_EQ_STRN (job->env[0], "PATH=");
		TEST_ALLOC_PARENT (job->env[1], job->env);
		TEST_EQ_STRN (job->env[1], "TERM=");
		TEST_ALLOC_PARENT (job->env[2], job->env);
		TEST_EQ_STR (job->env[2], "FOO=BAR");
		TEST_ALLOC_PARENT (job->env[3], job->env);
		TEST_EQ_STR (job->env[3], "BAR=BAZ");
		TEST_ALLOC_PARENT (job->env[4], job->env);
		TEST_EQ_STR (job->env[4], "UPSTART_EVENTS=wibble wobble");
		TEST_EQ_P (job->env[5], NULL);

		TEST_EQ_P (job->start_env, NULL);


		oper = class->start_on;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);

		oper = (EventOperator *)class->start_on->node.left;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);

		oper = (EventOperator *)class->start_on->node.right;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);


		TEST_LIST_NOT_EMPTY (&job->blocking);

		blocked = (Blocked *)job->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job);
		TEST_EQ (blocked->type, BLOCKED_EVENT);
		TEST_EQ_P (blocked->event, event1);
		nih_free (blocked);

		blocked = (Blocked *)job->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job);
		TEST_EQ (blocked->type, BLOCKED_EVENT);
		TEST_EQ_P (blocked->event, event2);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocking);

		nih_free (class);
		nih_free (event1);
		nih_free (event2);
	}


	/* Check that the environment variables from the event are also copied
	 * into the job's environment.
	 */
	TEST_FEATURE ("with environment in start event");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event1 = event_new (NULL, "wibble", NULL);
			assert (nih_str_array_add (&(event1->env), event1,
						   NULL, "FRODO=baggins"));
			assert (nih_str_array_add (&(event1->env), event1,
						   NULL, "BILBO=took"));

			TEST_FREE_TAG (event1);

			event2 = event_new (NULL, "wobble", NULL);
			assert (nih_str_array_add (&(event2->env), event2,
						   NULL, "FRODO=brandybuck"));
			assert (nih_str_array_add (&(event2->env), event2,
						   NULL, "TEA=MILK"));

			TEST_FREE_TAG (event2);

			class = job_class_new (NULL, "test");
			class->task = TRUE;

			assert (nih_str_array_add (&(class->env), class,
						   NULL, "FOO=BAR"));
			assert (nih_str_array_add (&(class->env), class,
						   NULL, "BAR=BAZ"));

			class->start_on = event_operator_new (
				class, EVENT_AND, NULL, NULL);

			oper = event_operator_new (
				class->start_on, EVENT_MATCH, "wibble", NULL);
			nih_tree_add (&class->start_on->node, &oper->node,
				      NIH_TREE_LEFT);

			oper = event_operator_new (
				class->start_on, EVENT_MATCH, "wobble", NULL);
			nih_tree_add (&class->start_on->node, &oper->node,
				      NIH_TREE_RIGHT);

			nih_hash_add (job_classes, &class->entry);
		}


		event_poll ();

		TEST_NOT_FREE (event1);
		TEST_NOT_FREE (event2);

		TEST_EQ (event1->blockers, 1);
		TEST_EQ (event2->blockers, 1);

		TEST_HASH_NOT_EMPTY (class->instances);

		job = (Job *)nih_hash_lookup (class->instances, "");

		TEST_ALLOC_PARENT (job->name, job);
		TEST_EQ_STR (job->name, "");

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);

		TEST_NE_P (job->env, NULL);
		TEST_ALLOC_PARENT (job->env, job);
		TEST_ALLOC_SIZE (job->env, sizeof (char *) * 9);
		TEST_ALLOC_PARENT (job->env[0], job->env);
		TEST_EQ_STRN (job->env[0], "PATH=");
		TEST_ALLOC_PARENT (job->env[1], job->env);
		TEST_EQ_STRN (job->env[1], "TERM=");
		TEST_ALLOC_PARENT (job->env[2], job->env);
		TEST_EQ_STR (job->env[2], "FOO=BAR");
		TEST_ALLOC_PARENT (job->env[3], job->env);
		TEST_EQ_STR (job->env[3], "BAR=BAZ");
		TEST_ALLOC_PARENT (job->env[4], job->env);
		TEST_EQ_STR (job->env[4], "FRODO=brandybuck");
		TEST_ALLOC_PARENT (job->env[5], job->env);
		TEST_EQ_STR (job->env[5], "BILBO=took");
		TEST_ALLOC_PARENT (job->env[6], job->env);
		TEST_EQ_STR (job->env[6], "TEA=MILK");
		TEST_ALLOC_PARENT (job->env[7], job->env);
		TEST_EQ_STR (job->env[7], "UPSTART_EVENTS=wibble wobble");
		TEST_EQ_P (job->env[8], NULL);

		TEST_EQ_P (job->start_env, NULL);


		oper = class->start_on;
		TEST_EQ (oper->value, FALSE);

		oper = (EventOperator *)class->start_on->node.left;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);

		oper = (EventOperator *)class->start_on->node.right;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);


		TEST_LIST_NOT_EMPTY (&job->blocking);

		blocked = (Blocked *)job->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job);
		TEST_EQ (blocked->type, BLOCKED_EVENT);
		TEST_EQ_P (blocked->event, event1);
		nih_free (blocked);

		blocked = (Blocked *)job->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job);
		TEST_EQ (blocked->type, BLOCKED_EVENT);
		TEST_EQ_P (blocked->event, event2);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocking);

		nih_free (class);
		nih_free (event1);
		nih_free (event2);
	}


	/* Check that the event can restart an instance that is stopping,
	 * storing the environment in the start_env member since it should
	 * not overwrite the previous environment until it actually restarts.
	 */
	TEST_FEATURE ("with restart of stopping job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event1 = event_new (NULL, "wibble", NULL);
			assert (nih_str_array_add (&(event1->env), event1,
						   NULL, "FRODO=baggins"));
			assert (nih_str_array_add (&(event1->env), event1,
						   NULL, "BILBO=took"));

			TEST_FREE_TAG (event1);

			event2 = event_new (NULL, "wobble", NULL);
			assert (nih_str_array_add (&(event2->env), event2,
						   NULL, "FRODO=brandybuck"));
			assert (nih_str_array_add (&(event2->env), event2,
						   NULL, "TEA=MILK"));

			TEST_FREE_TAG (event2);

			class = job_class_new (NULL, "test");
			class->task = TRUE;

			assert (nih_str_array_add (&(class->env), class,
						   NULL, "FOO=BAR"));
			assert (nih_str_array_add (&(class->env), class,
						   NULL, "BAR=BAZ"));

			class->start_on = event_operator_new (
				class, EVENT_AND, NULL, NULL);

			oper = event_operator_new (
				class->start_on, EVENT_MATCH, "wibble", NULL);
			nih_tree_add (&class->start_on->node, &oper->node,
				      NIH_TREE_LEFT);

			oper = event_operator_new (
				class->start_on, EVENT_MATCH, "wobble", NULL);
			nih_tree_add (&class->start_on->node, &oper->node,
				      NIH_TREE_RIGHT);

			nih_hash_add (job_classes, &class->entry);

			job = job_new (class, "");
			job->goal = JOB_STOP;
			job->state = JOB_STOPPING;

			assert (nih_str_array_add (&(job->env), job,
						   NULL, "FOO=wibble"));
			assert (nih_str_array_add (&(job->env), job,
						   NULL, "BAR=wobble"));

			env1 = job->env;
			TEST_FREE_TAG (env1);

			assert (nih_str_array_add (&(job->start_env), job,
						   NULL, "FOO=tea"));
			assert (nih_str_array_add (&(job->start_env), job,
						   NULL, "BAR=coffee"));

			env2 = job->start_env;
			TEST_FREE_TAG (env2);

			event3 = event_new (NULL, "flibble", NULL);
			blocked1 = blocked_new (job, BLOCKED_EVENT, event3);
			event_block (blocked1->event);
			nih_list_add (&job->blocking, &blocked1->entry);

			TEST_FREE_TAG (blocked1);
			TEST_FREE_TAG (event3);

			event4 = event_new (NULL, "flobble", NULL);
			blocked2 = blocked_new (job, BLOCKED_EVENT, event4);
			event_block (blocked2->event);
			nih_list_add (&job->blocking, &blocked2->entry);

			TEST_FREE_TAG (blocked2);
			TEST_FREE_TAG (event4);
		}

		event_poll ();

		TEST_NOT_FREE (event1);
		TEST_NOT_FREE (event2);
		TEST_FREE (event3);
		TEST_FREE (event4);

		TEST_EQ (event1->blockers, 1);
		TEST_EQ (event2->blockers, 1);

		TEST_HASH_NOT_EMPTY (class->instances);

		ptr = (Job *)nih_hash_lookup (class->instances, "");

		TEST_EQ_P (ptr, job);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STOPPING);

		TEST_NOT_FREE (env1);
		TEST_EQ_P (job->env, env1);

		TEST_FREE (env2);

		TEST_NE_P (job->start_env, NULL);
		TEST_ALLOC_PARENT (job->start_env, job);
		TEST_ALLOC_SIZE (job->start_env, sizeof (char *) * 9);
		TEST_ALLOC_PARENT (job->start_env[0], job->start_env);
		TEST_EQ_STRN (job->start_env[0], "PATH=");
		TEST_ALLOC_PARENT (job->start_env[1], job->start_env);
		TEST_EQ_STRN (job->start_env[1], "TERM=");
		TEST_ALLOC_PARENT (job->start_env[2], job->start_env);
		TEST_EQ_STR (job->start_env[2], "FOO=BAR");
		TEST_ALLOC_PARENT (job->start_env[3], job->start_env);
		TEST_EQ_STR (job->start_env[3], "BAR=BAZ");
		TEST_ALLOC_PARENT (job->start_env[4], job->start_env);
		TEST_EQ_STR (job->start_env[4], "FRODO=brandybuck");
		TEST_ALLOC_PARENT (job->start_env[5], job->start_env);
		TEST_EQ_STR (job->start_env[5], "BILBO=took");
		TEST_ALLOC_PARENT (job->start_env[6], job->start_env);
		TEST_EQ_STR (job->start_env[6], "TEA=MILK");
		TEST_ALLOC_PARENT (job->start_env[7], job->start_env);
		TEST_EQ_STR (job->start_env[7], "UPSTART_EVENTS=wibble wobble");
		TEST_EQ_P (job->start_env[8], NULL);

		oper = class->start_on;
		TEST_EQ (oper->value, FALSE);

		oper = (EventOperator *)class->start_on->node.left;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);

		oper = (EventOperator *)class->start_on->node.right;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);

		TEST_FREE (blocked1);
		TEST_FREE (blocked2);

		TEST_LIST_NOT_EMPTY (&job->blocking);

		blocked = (Blocked *)job->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job);
		TEST_EQ (blocked->type, BLOCKED_EVENT);
		TEST_EQ_P (blocked->event, event1);
		nih_free (blocked);

		blocked = (Blocked *)job->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job);
		TEST_EQ (blocked->type, BLOCKED_EVENT);
		TEST_EQ_P (blocked->event, event2);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocking);

		nih_free (class);
		nih_free (event1);
		nih_free (event2);
	}


	/* Check that a job that is already running is not affected by the
	 * start events happening again.
	 */
	TEST_FEATURE ("with already running job");
	event1 = event_new (NULL, "wibble", NULL);
	event2 = event_new (NULL, "wobble", NULL);

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event1 = event_new (NULL, "wibble", NULL);
			assert (nih_str_array_add (&(event1->env), event1,
						   NULL, "FRODO=baggins"));
			assert (nih_str_array_add (&(event1->env), event1,
						   NULL, "BILBO=took"));

			TEST_FREE_TAG (event1);

			event2 = event_new (NULL, "wobble", NULL);
			assert (nih_str_array_add (&(event2->env), event2,
						   NULL, "FRODO=brandybuck"));
			assert (nih_str_array_add (&(event2->env), event2,
						   NULL, "TEA=MILK"));

			TEST_FREE_TAG (event2);

			class = job_class_new (NULL, "test");
			class->task = TRUE;

			assert (nih_str_array_add (&(class->env), class,
						   NULL, "FOO=BAR"));
			assert (nih_str_array_add (&(class->env), class,
						   NULL, "BAR=BAZ"));

			class->start_on = event_operator_new (
				class, EVENT_AND, NULL, NULL);

			oper = event_operator_new (
				class->start_on, EVENT_MATCH, "wibble", NULL);
			nih_tree_add (&class->start_on->node, &oper->node,
				      NIH_TREE_LEFT);

			oper = event_operator_new (
				class->start_on, EVENT_MATCH, "wobble", NULL);
			nih_tree_add (&class->start_on->node, &oper->node,
				      NIH_TREE_RIGHT);

			nih_hash_add (job_classes, &class->entry);

			job = job_new (class, "");
			job->goal = JOB_START;
			job->state = JOB_RUNNING;

			assert (nih_str_array_add (&(job->env), job,
						   NULL, "FOO=wibble"));
			assert (nih_str_array_add (&(job->env), job,
						   NULL, "BAR=wobble"));

			env1 = job->env;
			TEST_FREE_TAG (env1);

			assert (nih_str_array_add (&(job->start_env), job,
						   NULL, "FOO=tea"));
			assert (nih_str_array_add (&(job->start_env), job,
						   NULL, "BAR=coffee"));

			env2 = job->start_env;
			TEST_FREE_TAG (env2);

			event3 = event_new (NULL, "flibble", NULL);
			blocked1 = blocked_new (job, BLOCKED_EVENT, event3);
			event_block (blocked1->event);
			nih_list_add (&job->blocking, &blocked1->entry);

			TEST_FREE_TAG (blocked1);
			TEST_FREE_TAG (event3);

			event4 = event_new (NULL, "flobble", NULL);
			blocked2 = blocked_new (job, BLOCKED_EVENT, event4);
			event_block (blocked2->event);
			nih_list_add (&job->blocking, &blocked2->entry);

			TEST_FREE_TAG (blocked2);
			TEST_FREE_TAG (event4);
		}

		event_poll ();

		TEST_FREE (event1);
		TEST_FREE (event2);
		TEST_NOT_FREE (event3);
		TEST_NOT_FREE (event4);

		event_poll ();

		TEST_EQ (event3->blockers, 1);
		TEST_EQ (event4->blockers, 1);

		TEST_HASH_NOT_EMPTY (class->instances);

		ptr = (Job *)nih_hash_lookup (class->instances, "");

		TEST_EQ_P (ptr, job);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);

		TEST_NOT_FREE (env1);
		TEST_EQ_P (job->env, env1);

		TEST_NOT_FREE (env2);
		TEST_EQ_P (job->start_env, env2);

		oper = class->start_on;
		TEST_EQ (oper->value, FALSE);

		oper = (EventOperator *)class->start_on->node.left;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);

		oper = (EventOperator *)class->start_on->node.right;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked1);
		TEST_NOT_FREE (blocked2);
		nih_free (blocked1);
		nih_free (blocked2);
		TEST_LIST_EMPTY (&job->blocking);

		nih_free (class);
		nih_free (event3);
		nih_free (event4);
	}


	/* Check that the class's instance name undergoes expansion against
	 * the events, and is used to name the resulting job.
	 */
	TEST_FEATURE ("with instance name");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event1 = event_new (NULL, "wibble", NULL);
			assert (nih_str_array_add (&(event1->env), event1,
						   NULL, "FRODO=baggins"));
			assert (nih_str_array_add (&(event1->env), event1,
						   NULL, "BILBO=took"));

			TEST_FREE_TAG (event1);

			class = job_class_new (NULL, "test");
			class->instance = "$FRODO";
			class->task = TRUE;

			class->start_on = event_operator_new (
				class, EVENT_MATCH, "wibble", NULL);

			nih_hash_add (job_classes, &class->entry);
		}

		event_poll ();

		TEST_NOT_FREE (event1);

		TEST_EQ (event1->blockers, 1);

		TEST_HASH_NOT_EMPTY (class->instances);

		job = (Job *)nih_hash_lookup (class->instances, "baggins");

		TEST_ALLOC_PARENT (job->name, job);
		TEST_EQ_STR (job->name, "baggins");

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);

		TEST_NE_P (job->env, NULL);
		TEST_ALLOC_PARENT (job->env, job);
		TEST_ALLOC_SIZE (job->env, sizeof (char *) * 6);
		TEST_ALLOC_PARENT (job->env[0], job->env);
		TEST_EQ_STRN (job->env[0], "PATH=");
		TEST_ALLOC_PARENT (job->env[1], job->env);
		TEST_EQ_STRN (job->env[1], "TERM=");
		TEST_ALLOC_PARENT (job->env[2], job->env);
		TEST_EQ_STR (job->env[2], "FRODO=baggins");
		TEST_ALLOC_PARENT (job->env[3], job->env);
		TEST_EQ_STR (job->env[3], "BILBO=took");
		TEST_ALLOC_PARENT (job->env[4], job->env);
		TEST_EQ_STR (job->env[4], "UPSTART_EVENTS=wibble");
		TEST_EQ_P (job->env[5], NULL);

		TEST_EQ_P (job->start_env, NULL);


		oper = class->start_on;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);


		TEST_LIST_NOT_EMPTY (&job->blocking);

		blocked = (Blocked *)job->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job);
		TEST_EQ (blocked->type, BLOCKED_EVENT);
		TEST_EQ_P (blocked->event, event1);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocking);

		nih_free (class);
		nih_free (event1);
	}


	/* Check that if an instance with that name already exists, it is
	 * restarted itself instead of a new one being created.
	 */
	TEST_FEATURE ("with restart of existing instance");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event1 = event_new (NULL, "wibble", NULL);
			assert (nih_str_array_add (&(event1->env), event1,
						   NULL, "FRODO=brandybuck"));
			assert (nih_str_array_add (&(event1->env), event1,
						   NULL, "BILBO=took"));

			TEST_FREE_TAG (event1);

			class = job_class_new (NULL, "test");
			class->instance = "$FRODO";
			class->task = TRUE;

			class->start_on = event_operator_new (
				class, EVENT_MATCH, "wibble", NULL);

			nih_hash_add (job_classes, &class->entry);

			job = job_new (class, "brandybuck");
			job->goal = JOB_STOP;
			job->state = JOB_STOPPING;

			event3 = event_new (NULL, "flibble", NULL);
			blocked1 = blocked_new (job, BLOCKED_EVENT, event3);
			event_block (blocked1->event);
			nih_list_add (&job->blocking, &blocked1->entry);

			TEST_FREE_TAG (blocked1);
			TEST_FREE_TAG (event3);

			event4 = event_new (NULL, "flobble", NULL);
			blocked2 = blocked_new (job, BLOCKED_EVENT, event4);
			event_block (blocked2->event);
			nih_list_add (&job->blocking, &blocked2->entry);

			TEST_FREE_TAG (blocked2);
			TEST_FREE_TAG (event4);
		}

		event_poll ();

		TEST_NOT_FREE (event1);

		TEST_EQ (event1->blockers, 1);

		TEST_HASH_NOT_EMPTY (class->instances);

		ptr = (Job *)nih_hash_lookup (class->instances, "brandybuck");

		TEST_EQ_P (ptr, job);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STOPPING);

		oper = class->start_on;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);

		TEST_FREE (event3);
		TEST_FREE (event4);
		TEST_FREE (blocked1);
		TEST_FREE (blocked2);

		TEST_LIST_NOT_EMPTY (&job->blocking);

		blocked = (Blocked *)job->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job);
		TEST_EQ (blocked->type, BLOCKED_EVENT);
		TEST_EQ_P (blocked->event, event1);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocking);

		nih_free (class);
		nih_free (event1);
	}


	/* Check that errors with the instance name are caught and prevent
	 * the job from being started.
	 */
	TEST_FEATURE ("with error in instance name");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event1 = event_new (NULL, "wibble", NULL);
			assert (nih_str_array_add (&(event1->env), event1,
						   NULL, "FRODO=baggins"));
			assert (nih_str_array_add (&(event1->env), event1,
						   NULL, "BILBO=took"));

			TEST_FREE_TAG (event1);

			class = job_class_new (NULL, "test");
			class->instance = "$TIPPLE";
			class->task = TRUE;

			class->start_on = event_operator_new (
				class, EVENT_MATCH, "wibble", NULL);

			nih_hash_add (job_classes, &class->entry);
		}

		TEST_DIVERT_STDERR (output) {
			event_poll ();
		}
		rewind (output);

		TEST_FREE (event1);

		TEST_HASH_EMPTY (class->instances);

		oper = class->start_on;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);

		TEST_FILE_EQ (output, ("test: Failed to obtain test instance: "
				       "Unknown parameter: TIPPLE\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (class);
	}


	/* Check that an event that does not match the stop operator of
	 * a job does not get blocked and passes straight through the
	 * loop.
	 */
	TEST_FEATURE ("with non-matching event for stop");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event1 = event_new (NULL, "biscuit", NULL);

			TEST_FREE_TAG (event1);

			class = job_class_new (NULL, "test");
			class->task = TRUE;

			class->process[PROCESS_POST_STOP] = process_new (class);
			class->process[PROCESS_POST_STOP]->command = "echo";

			class->stop_on = event_operator_new (
				class, EVENT_MATCH, "wibble", NULL);

			job = job_new (class, "");
			job->goal = JOB_START;
			job->state = JOB_RUNNING;

			nih_hash_add (job_classes, &class->entry);
		}

		event_poll ();

		TEST_FREE (event1);

		TEST_HASH_NOT_EMPTY (class->instances);

		job = (Job *)nih_hash_lookup (class->instances, "");

		oper = job->stop_on;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);

		oper = class->stop_on;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);

		nih_free (class);
	}


	/* Check that a matching event is recorded against the operator that
	 * matches it, but only affects the job if it completes the
	 * expression.  The name of the event should be added to the stop_env
	 * member of the job, used for pre-stop later.
	 */
	TEST_FEATURE ("with matching event to stop");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event1 = event_new (NULL, "wibble", NULL);

			TEST_FREE_TAG (event1);

			class = job_class_new (NULL, "test");
			class->task = TRUE;

			class->process[PROCESS_POST_STOP] = process_new (class);
			class->process[PROCESS_POST_STOP]->command = "echo";

			class->stop_on = event_operator_new (
				class, EVENT_MATCH, "wibble", NULL);

			job = job_new (class, "");
			job->goal = JOB_START;
			job->state = JOB_RUNNING;

			nih_hash_add (job_classes, &class->entry);
		}

		event_poll ();

		TEST_NOT_FREE (event1);

		TEST_EQ (event1->blockers, 1);

		TEST_HASH_NOT_EMPTY (class->instances);

		job = (Job *)nih_hash_lookup (class->instances, "");

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_POST_STOP);

		TEST_GT (job->pid[PROCESS_POST_STOP], 0);
		waitpid (job->pid[PROCESS_POST_STOP], NULL, 0);

		TEST_NE_P (job->stop_env, NULL);
		TEST_ALLOC_PARENT (job->stop_env, job);
		TEST_ALLOC_SIZE (job->stop_env, sizeof (char *) * 2);
		TEST_ALLOC_PARENT (job->stop_env[0], job->stop_env);
		TEST_EQ_STR (job->stop_env[0], "UPSTART_STOP_EVENTS=wibble");
		TEST_EQ_P (job->stop_env[1], NULL);


		oper = job->stop_on;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);


		TEST_LIST_NOT_EMPTY (&job->blocking);

		blocked = (Blocked *)job->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job);
		TEST_EQ (blocked->type, BLOCKED_EVENT);
		TEST_EQ_P (blocked->event, event1);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocking);

		nih_free (class);
		nih_free (event1);
	}


	/* Check that the environment variables from the event are also copied
	 * into the job's stop_env member.
	 */
	TEST_FEATURE ("with environment in stop event");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event1 = event_new (NULL, "wibble", NULL);
			assert (nih_str_array_add (&(event1->env), event1,
						   NULL, "FOO=foo"));
			assert (nih_str_array_add (&(event1->env), event1,
						   NULL, "BAR=bar"));

			TEST_FREE_TAG (event1);

			class = job_class_new (NULL, "test");
			class->task = TRUE;

			class->process[PROCESS_POST_STOP] = process_new (class);
			class->process[PROCESS_POST_STOP]->command = "echo";

			class->stop_on = event_operator_new (
				class, EVENT_MATCH, "wibble", NULL);

			job = job_new (class, "");
			job->goal = JOB_START;
			job->state = JOB_RUNNING;

			nih_hash_add (job_classes, &class->entry);
		}

		event_poll ();

		TEST_NOT_FREE (event1);

		TEST_EQ (event1->blockers, 1);

		TEST_HASH_NOT_EMPTY (class->instances);

		job = (Job *)nih_hash_lookup (class->instances, "");

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_POST_STOP);

		TEST_GT (job->pid[PROCESS_POST_STOP], 0);
		waitpid (job->pid[PROCESS_POST_STOP], NULL, 0);

		TEST_NE_P (job->stop_env, NULL);
		TEST_ALLOC_PARENT (job->stop_env, job);
		TEST_ALLOC_SIZE (job->stop_env, sizeof (char *) * 4);
		TEST_ALLOC_PARENT (job->stop_env[0], job->stop_env);
		TEST_EQ_STR (job->stop_env[0], "FOO=foo");
		TEST_ALLOC_PARENT (job->stop_env[1], job->stop_env);
		TEST_EQ_STR (job->stop_env[1], "BAR=bar");
		TEST_ALLOC_PARENT (job->stop_env[2], job->stop_env);
		TEST_EQ_STR (job->stop_env[2], "UPSTART_STOP_EVENTS=wibble");
		TEST_EQ_P (job->stop_env[3], NULL);


		oper = job->stop_on;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);


		TEST_LIST_NOT_EMPTY (&job->blocking);

		blocked = (Blocked *)job->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job);
		TEST_EQ (blocked->type, BLOCKED_EVENT);
		TEST_EQ_P (blocked->event, event1);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocking);

		nih_free (class);
		nih_free (event1);
	}


	/* Check that the event can resume stopping a job that's stopping
	 * but previously was marked for restarting.
	 */
	TEST_FEATURE ("with stop of restarting job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event1 = event_new (NULL, "wibble", NULL);
			assert (nih_str_array_add (&(event1->env), event1,
						   NULL, "FOO=foo"));
			assert (nih_str_array_add (&(event1->env), event1,
						   NULL, "BAR=bar"));

			TEST_FREE_TAG (event1);

			class = job_class_new (NULL, "test");
			class->task = TRUE;

			class->process[PROCESS_POST_STOP] = process_new (class);
			class->process[PROCESS_POST_STOP]->command = "echo";

			class->stop_on = event_operator_new (
				class, EVENT_MATCH, "wibble", NULL);

			job = job_new (class, "");
			job->goal = JOB_START;
			job->state = JOB_RUNNING;

			assert (nih_str_array_add (&(job->stop_env), job,
						   NULL, "FOO=biscuit"));
			assert (nih_str_array_add (&(job->stop_env), job,
						   NULL, "BAR=beer"));

			env1 = job->stop_env;
			TEST_FREE_TAG (env1);

			event3 = event_new (NULL, "flibble", NULL);
			blocked1 = blocked_new (job, BLOCKED_EVENT, event3);
			event_block (blocked1->event);
			nih_list_add (&job->blocking, &blocked1->entry);

			TEST_FREE_TAG (blocked1);
			TEST_FREE_TAG (event3);

			event4 = event_new (NULL, "flobble", NULL);
			blocked2 = blocked_new (job, BLOCKED_EVENT, event4);
			event_block (blocked2->event);
			nih_list_add (&job->blocking, &blocked2->entry);

			TEST_FREE_TAG (blocked2);
			TEST_FREE_TAG (event4);

			nih_hash_add (job_classes, &class->entry);
		}

		event_poll ();

		TEST_NOT_FREE (event1);

		TEST_EQ (event1->blockers, 1);

		TEST_HASH_NOT_EMPTY (class->instances);

		job = (Job *)nih_hash_lookup (class->instances, "");

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_POST_STOP);

		TEST_GT (job->pid[PROCESS_POST_STOP], 0);
		waitpid (job->pid[PROCESS_POST_STOP], NULL, 0);

		TEST_FREE (env1);

		TEST_NE_P (job->stop_env, NULL);
		TEST_ALLOC_PARENT (job->stop_env, job);
		TEST_ALLOC_SIZE (job->stop_env, sizeof (char *) * 4);
		TEST_ALLOC_PARENT (job->stop_env[0], job->stop_env);
		TEST_EQ_STR (job->stop_env[0], "FOO=foo");
		TEST_ALLOC_PARENT (job->stop_env[1], job->stop_env);
		TEST_EQ_STR (job->stop_env[1], "BAR=bar");
		TEST_ALLOC_PARENT (job->stop_env[2], job->stop_env);
		TEST_EQ_STR (job->stop_env[2], "UPSTART_STOP_EVENTS=wibble");
		TEST_EQ_P (job->stop_env[3], NULL);


		oper = job->stop_on;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);


		TEST_FREE (event3);
		TEST_FREE (event4);
		TEST_FREE (blocked1);
		TEST_FREE (blocked2);

		TEST_LIST_NOT_EMPTY (&job->blocking);

		blocked = (Blocked *)job->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job);
		TEST_EQ (blocked->type, BLOCKED_EVENT);
		TEST_EQ_P (blocked->event, event1);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocking);

		nih_free (class);
		nih_free (event1);
	}


	/* Check that a job that is already stopping is not affected by the
	 * stop events happening again.
	 */
	TEST_FEATURE ("with already stopping job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event1 = event_new (NULL, "wibble", NULL);
			assert (nih_str_array_add (&(event1->env), event1,
						   NULL, "FOO=foo"));
			assert (nih_str_array_add (&(event1->env), event1,
						   NULL, "BAR=bar"));

			TEST_FREE_TAG (event1);

			class = job_class_new (NULL, "test");
			class->task = TRUE;

			class->process[PROCESS_POST_STOP] = process_new (class);
			class->process[PROCESS_POST_STOP]->command = "echo";

			class->stop_on = event_operator_new (
				class, EVENT_MATCH, "wibble", NULL);

			job = job_new (class, "");
			job->goal = JOB_STOP;
			job->state = JOB_STOPPING;

			assert (nih_str_array_add (&(job->stop_env), job,
						   NULL, "FOO=biscuit"));
			assert (nih_str_array_add (&(job->stop_env), job,
						   NULL, "BAR=beer"));

			env1 = job->stop_env;
			TEST_FREE_TAG (env1);

			event3 = event_new (NULL, "flibble", NULL);
			blocked1 = blocked_new (job, BLOCKED_EVENT, event3);
			event_block (blocked1->event);
			nih_list_add (&job->blocking, &blocked1->entry);

			TEST_FREE_TAG (blocked1);
			TEST_FREE_TAG (event3);

			event4 = event_new (NULL, "flobble", NULL);
			blocked2 = blocked_new (job, BLOCKED_EVENT, event4);
			event_block (blocked2->event);
			nih_list_add (&job->blocking, &blocked2->entry);

			TEST_FREE_TAG (blocked2);
			TEST_FREE_TAG (event4);

			nih_hash_add (job_classes, &class->entry);
		}

		event_poll ();

		TEST_FREE (event1);

		TEST_HASH_NOT_EMPTY (class->instances);

		job = (Job *)nih_hash_lookup (class->instances, "");

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);

		TEST_NOT_FREE (env1);
		TEST_EQ_P (job->stop_env, env1);

		oper = job->stop_on;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);

		TEST_NOT_FREE (event3);
		TEST_NOT_FREE (event4);

		TEST_LIST_NOT_EMPTY (&job->blocking);
		TEST_NOT_FREE (blocked1);
		TEST_NOT_FREE (blocked2);
		nih_free (blocked1);
		nih_free (blocked2);
		TEST_LIST_EMPTY (&job->blocking);

		nih_free (class);
		nih_free (event3);
		nih_free (event4);
	}


	/* Check that the operator for the stop event can match against
	 * environment variables expanded from the job's env member.
	 */
	TEST_FEATURE ("with environment expansion in stop event");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event1 = event_new (NULL, "wibble", NULL);
			assert (nih_str_array_add (&(event1->env), event1,
						   NULL, "SNITCH=GOLD"));
			assert (nih_str_array_add (&(event1->env), event1,
						   NULL, "SEAKER=WIZARD"));

			TEST_FREE_TAG (event1);

			class = job_class_new (NULL, "test");
			class->task = TRUE;

			class->process[PROCESS_POST_STOP] = process_new (class);
			class->process[PROCESS_POST_STOP]->command = "echo";

			class->stop_on = event_operator_new (
				class, EVENT_MATCH, "wibble", NULL);

			assert (nih_str_array_add (&(class->stop_on->env), class->stop_on,
						   NULL, "SNITCH=$COLOUR"));

			job = job_new (class, "");
			job->goal = JOB_START;
			job->state = JOB_RUNNING;

			assert (nih_str_array_add (&(job->env), job,
						   NULL, "COLOUR=GOLD"));

			nih_hash_add (job_classes, &class->entry);
		}

		event_poll ();

		TEST_NOT_FREE (event1);

		TEST_EQ (event1->blockers, 1);

		TEST_HASH_NOT_EMPTY (class->instances);

		job = (Job *)nih_hash_lookup (class->instances, "");

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_POST_STOP);

		TEST_GT (job->pid[PROCESS_POST_STOP], 0);
		waitpid (job->pid[PROCESS_POST_STOP], NULL, 0);

		TEST_NE_P (job->stop_env, NULL);
		TEST_ALLOC_PARENT (job->stop_env, job);
		TEST_ALLOC_SIZE (job->stop_env, sizeof (char *) * 4);
		TEST_ALLOC_PARENT (job->stop_env[0], job->stop_env);
		TEST_EQ_STR (job->stop_env[0], "SNITCH=GOLD");
		TEST_ALLOC_PARENT (job->stop_env[1], job->stop_env);
		TEST_EQ_STR (job->stop_env[1], "SEAKER=WIZARD");
		TEST_ALLOC_PARENT (job->stop_env[2], job->stop_env);
		TEST_EQ_STR (job->stop_env[2], "UPSTART_STOP_EVENTS=wibble");
		TEST_EQ_P (job->stop_env[3], NULL);


		oper = job->stop_on;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);


		TEST_LIST_NOT_EMPTY (&job->blocking);

		blocked = (Blocked *)job->blocking.next;
		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_ALLOC_PARENT (blocked, job);
		TEST_EQ (blocked->type, BLOCKED_EVENT);
		TEST_EQ_P (blocked->event, event1);
		nih_free (blocked);

		TEST_LIST_EMPTY (&job->blocking);

		nih_free (class);
		nih_free (event1);
	}

	fclose (output);
}


void
test_finished (void)
{
	JobClass      *class = NULL;
	Job           *job = NULL;
	Event         *event = NULL, *bevent = NULL;
	Blocked       *blocked = NULL;
	EventOperator *oper;

	TEST_FUNCTION ("event_finished");

	/* Check that when a non-failed event is finished, a failed event
	 * is not generated.
	 */
	TEST_FEATURE ("with non-failed event");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event = event_new (NULL, "test", NULL);
			event->progress = EVENT_FINISHED;

			TEST_FREE_TAG (event);

			class = job_class_new (NULL, "test");
			class->process[PROCESS_MAIN] = process_new (class->process);
			class->process[PROCESS_MAIN]->command = "echo";

			class->start_on = event_operator_new (
				class, EVENT_MATCH, "test/failed", NULL);

			nih_hash_add (job_classes, &class->entry);
		}

		event_poll ();

		TEST_FREE (event);

		TEST_HASH_EMPTY (class->instances);

		nih_free (class);
	}


	/* Check that a failed event causes another event to be emitted
	 * that has "/failed" appended on the end.  We can obtain the
	 * failed event by hooking a job on it, and using the
	 * cause.
	 */
	TEST_FEATURE ("with failed event");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event = event_new (NULL, "test", NULL);
			event->failed = TRUE;
			event->progress = EVENT_FINISHED;

			TEST_FREE_TAG (event);

			class = job_class_new (NULL, "test");
			class->process[PROCESS_MAIN] = process_new (class->process);
			class->process[PROCESS_MAIN]->command = "echo";

			class->start_on = event_operator_new (
				class, EVENT_MATCH, "test/failed", NULL);

			nih_hash_add (job_classes, &class->entry);
		}

		event_poll ();

		TEST_FREE (event);

		TEST_HASH_NOT_EMPTY (class->instances);

		job = (Job *)nih_hash_lookup (class->instances, "");

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_GT (job->pid[PROCESS_MAIN], 0);

		waitpid (job->pid[PROCESS_MAIN], NULL, 0);

		nih_free (class);
	}

	event_poll ();


	/* Check that failed events do not, themselves, emit new failed
	 * events (otherwise we could be there all night :p)
	 */
	TEST_FEATURE ("with failed failed event");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event = event_new (NULL, "test/failed", NULL);
			event->failed = TRUE;
			event->progress = EVENT_FINISHED;

			TEST_FREE_TAG (event);

			class = job_class_new (NULL, "test");
			class->process[PROCESS_MAIN] = process_new (class->process);
			class->process[PROCESS_MAIN]->command = "echo";

			class->start_on = event_operator_new (
				class, EVENT_OR, NULL, NULL);

			oper = event_operator_new (class, EVENT_MATCH,
						   "test/failed", NULL);
			nih_tree_add (&class->start_on->node, &oper->node,
				      NIH_TREE_LEFT);

			oper = event_operator_new (class, EVENT_MATCH,
						   "test/failed/failed", NULL);
			nih_tree_add (&class->start_on->node, &oper->node,
				      NIH_TREE_RIGHT);

			nih_hash_add (job_classes, &class->entry);
		}

		event_poll ();

		TEST_FREE (event);

		TEST_HASH_EMPTY (class->instances);

		nih_free (class);
	}


	/* Check that a finishing event has no effect on a stopping job
	 * that is no longer blocked (shouldn't ever happen really, but
	 * pays to check).
	 */
	TEST_FEATURE ("with non-blocked stopping job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event = event_new (NULL, "biscuit", NULL);

			TEST_FREE_TAG (event);

			class = job_class_new (NULL, "foo");

			job = job_new (class, "");
			job->goal = JOB_STOP;
			job->state = JOB_STOPPING;
			job->blocker = NULL;

			nih_hash_add (job_classes, &class->entry);
		}

		event_poll ();

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ_P (job->blocker, NULL);

		TEST_FREE (event);

		nih_free (class);
	}


	/* Check that a finishing event has no effect on a starting job
	 * that is no longer blocked (shouldn't ever happen really, but
	 * pays to check).
	 */
	TEST_FEATURE ("with non-blocked starting job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event = event_new (NULL, "biscuit", NULL);

			TEST_FREE_TAG (event);

			class = job_class_new (NULL, "foo");

			job = job_new (class, "");
			job->goal = JOB_START;
			job->state = JOB_STARTING;
			job->blocker = NULL;

			nih_hash_add (job_classes, &class->entry);
		}

		event_poll ();

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STARTING);
		TEST_EQ_P (job->blocker, NULL);

		TEST_FREE (event);

		nih_free (class);
	}


	/* Check that the wrong event does not unblock a stopping job.
	 */
	TEST_FEATURE ("with stopping job but wrong event");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event = event_new (NULL, "biscuit", NULL);

			TEST_FREE_TAG (event);

			class = job_class_new (NULL, "foo");

			job = job_new (class, "");
			job->goal = JOB_STOP;
			job->state = JOB_STOPPING;

			bevent = event_new (job, "wibble", NULL);
			blocked = blocked_new (bevent, BLOCKED_JOB, job);
			nih_list_add (&bevent->blocking, &blocked->entry);
			event_block (bevent);

			job->blocker = bevent;

			TEST_FREE_TAG (blocked);

			nih_hash_add (job_classes, &class->entry);
		}

		event_poll ();

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ_P (job->blocker, bevent);

		TEST_NOT_FREE (blocked);
		TEST_EQ (bevent->blockers, 1);

		TEST_FREE (event);

		nih_free (class);
	}


	/* Check that the wrong event does not unblock a starting job.
	 */
	TEST_FEATURE ("with starting job but wrong event");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event = event_new (NULL, "biscuit", NULL);

			TEST_FREE_TAG (event);

			class = job_class_new (NULL, "foo");

			job = job_new (class, "");
			job->goal = JOB_START;
			job->state = JOB_STARTING;

			bevent = event_new (job, "wibble", NULL);
			blocked = blocked_new (bevent, BLOCKED_JOB, job);
			nih_list_add (&bevent->blocking, &blocked->entry);
			event_block (bevent);

			job->blocker = bevent;

			TEST_FREE_TAG (blocked);

			nih_hash_add (job_classes, &class->entry);
		}

		event_poll ();

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STARTING);
		TEST_EQ_P (job->blocker, bevent);

		TEST_NOT_FREE (blocked);
		TEST_EQ (bevent->blockers, 1);

		TEST_FREE (event);

		nih_free (class);
	}


	/* Check that a matching event unblocks a stopping job and moves
	 * it into the next state.
	 */
	TEST_FEATURE ("with stopping job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event = event_new (NULL, "wibble", NULL);

			TEST_FREE_TAG (event);

			class = job_class_new (NULL, "foo");
			class->process[PROCESS_POST_STOP] = process_new (class);
			class->process[PROCESS_POST_STOP]->command = "echo";

			job = job_new (class, "");
			job->goal = JOB_STOP;
			job->state = JOB_STOPPING;
			job->pid[PROCESS_POST_STOP] = 0;

			job->blocker = event;

			blocked = blocked_new (event, BLOCKED_JOB, job);
			nih_list_add (&event->blocking, &blocked->entry);

			TEST_FREE_TAG (blocked);

			nih_hash_add (job_classes, &class->entry);
		}

		event_poll ();

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_POST_STOP);
		TEST_GT (job->pid[PROCESS_POST_STOP], 0);
		TEST_EQ_P (job->blocker, NULL);

		waitpid (job->pid[PROCESS_POST_STOP], NULL, 0);

		TEST_FREE (event);
		TEST_FREE (blocked);

		nih_free (class);
	}


	/* Check that a matching event unblocks a starting job and moves
	 * it into the next state.
	 */
	TEST_FEATURE ("with starting job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			event = event_new (NULL, "wibble", NULL);

			TEST_FREE_TAG (event);

			class = job_class_new (NULL, "foo");
			class->process[PROCESS_PRE_START] = process_new (class);
			class->process[PROCESS_PRE_START]->command = "echo";

			job = job_new (class, "");
			job->goal = JOB_START;
			job->state = JOB_STARTING;
			job->pid[PROCESS_PRE_START] = 0;

			job->blocker = event;

			blocked = blocked_new (event, BLOCKED_JOB, job);
			nih_list_add (&event->blocking, &blocked->entry);

			TEST_FREE_TAG (blocked);

			nih_hash_add (job_classes, &class->entry);
		}

		event_poll ();

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_PRE_START);
		TEST_GT (job->pid[PROCESS_PRE_START], 0);
		TEST_EQ_P (job->blocker, NULL);

		waitpid (job->pid[PROCESS_PRE_START], NULL, 0);

		TEST_FREE (event);
		TEST_FREE (blocked);

		nih_free (class);
	}
}


int
main (int   argc,
      char *argv[])
{
	test_new ();
	test_block ();
	test_unblock ();
	test_poll ();

	test_pending ();
	test_pending_handle_jobs ();
	test_finished ();

	return 0;
}
