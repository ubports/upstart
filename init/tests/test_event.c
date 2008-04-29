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
	Event  *event;
	char  **env;

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
	JobClass      *class;
	Job           *job;
	int            status;

	TEST_FUNCTION ("event_poll");
	job_class_init ();
	event_init ();


	/* Check that a pending event in the queue is handled, with any
	 * jobs started or stopped as a result.  The event should remain
	 * in the handling state while the job is blocking it.
	 */
	TEST_FEATURE ("with pending event");
	class = job_class_new (NULL, "test");
	class->leader = TRUE;
	class->task = TRUE;
	class->process[PROCESS_MAIN] = process_new (class->process);
	class->process[PROCESS_MAIN]->command = "echo";

	class->start_on = event_operator_new (class, EVENT_MATCH,
					      "test", NULL);

	nih_hash_add (job_classes, &class->entry);

	event = event_new (NULL, "test", NULL);

	event_poll ();

	TEST_EQ (event->progress, EVENT_HANDLING);
	TEST_EQ (event->blockers, 1);

	TEST_LIST_NOT_EMPTY (&class->instances);

	job = (Job *)class->instances.next;

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_GT (job->pid[PROCESS_MAIN], 0);

	waitpid (job->pid[PROCESS_MAIN], NULL, 0);

	nih_free (class);
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
	event->progress = EVENT_HANDLING;

	class = job_class_new (NULL, "test");
	class->leader = TRUE;
	class->process[PROCESS_MAIN] = process_new (class->process);
	class->process[PROCESS_MAIN]->command = "echo";

	nih_hash_add (job_classes, &class->entry);

	job = job_new (class, NULL);
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

	nih_free (class);


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

	class = job_class_new (NULL, "test");
	class->leader = TRUE;
	class->process[PROCESS_MAIN] = process_new (class->process);
	class->process[PROCESS_MAIN]->command = "echo";

	nih_hash_add (job_classes, &class->entry);

	class->start_on = event_operator_new (class, EVENT_MATCH,
					      "test/failed", NULL);

	TEST_FREE_TAG (event);

	event_poll ();

	TEST_FREE (event);

	TEST_LIST_NOT_EMPTY (&class->instances);

	job = (Job *)class->instances.next;

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_GT (job->pid[PROCESS_MAIN], 0);

	waitpid (job->pid[PROCESS_MAIN], NULL, 0);

	event_poll ();

	nih_free (class);


	/* Check that failed events do not, themselves, emit new failed
	 * events (otherwise we could be there all night :p)
	 */
	TEST_FEATURE ("with failed failed event");
	event = event_new (NULL, "test/failed", NULL);
	event->failed = TRUE;
	event->progress = EVENT_FINISHED;

	class = job_class_new (NULL, "test");
	class->leader = TRUE;
	class->process[PROCESS_MAIN] = process_new (class->process);
	class->process[PROCESS_MAIN]->command = "echo";

	nih_hash_add (job_classes, &class->entry);

	class->start_on = event_operator_new (class, EVENT_OR, NULL, NULL);

	oper = event_operator_new (class, EVENT_MATCH,
				   "test/failed", NULL);
	nih_tree_add (&class->start_on->node, &oper->node, NIH_TREE_LEFT);

	oper = event_operator_new (class, EVENT_MATCH,
				   "test/failed/failed", NULL);
	nih_tree_add (&class->start_on->node, &oper->node, NIH_TREE_RIGHT);

	TEST_FREE_TAG (event);

	event_poll ();

	TEST_FREE (event);

	TEST_LIST_EMPTY (&class->instances);

	nih_free (class);
}


int
main (int   argc,
      char *argv[])
{
	test_new ();
	test_block ();
	test_unblock ();
	test_poll ();

	return 0;
}
