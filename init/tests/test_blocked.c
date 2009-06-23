/* upstart
 *
 * test_blocked.c - test suite for init/blocked.c
 *
 * Copyright © 2009 Canonical Ltd.
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

#include <nih-dbus/dbus_message.h>

#include "job_class.h"
#include "job.h"
#include "event.h"
#include "blocked.h"



void
test_new (void)
{
	Blocked        *blocked;
	JobClass       *class;
	Job            *job;
	Event          *event;
	NihDBusMessage *message = NULL;

	TEST_FUNCTION ("blocked_new");

	/* Check that we can create a new blocked record for a job, with the
	 * details filled in correctly.  The returned structure should not be
	 * in a list.
	 */
	TEST_FEATURE ("with job");
	class = job_class_new (NULL, "test");
	job = job_new (class, "");

	TEST_ALLOC_FAIL {
		blocked = blocked_new (NULL, BLOCKED_JOB, job);

		if (test_alloc_failed) {
			TEST_EQ_P (blocked, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_LIST_EMPTY (&blocked->entry);
		TEST_EQ (blocked->type, BLOCKED_JOB);
		TEST_EQ_P (blocked->job, job);

		nih_free (blocked);
	}

	nih_free (class);


	/* Check that we can create a new blocked record for an event, with
	 * the details filled in correctly.  The returned structure should
	 * not be in a list.
	 */
	TEST_FEATURE ("with event");
	event = event_new (NULL, "foo", NULL);

	TEST_ALLOC_FAIL {
		blocked = blocked_new (NULL, BLOCKED_EVENT, event);

		if (test_alloc_failed) {
			TEST_EQ_P (blocked, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_LIST_EMPTY (&blocked->entry);
		TEST_EQ (blocked->type, BLOCKED_EVENT);
		TEST_EQ_P (blocked->event, event);

		nih_free (blocked);
	}

	nih_free (event);


	/* Check that we can create a new blocked record for a D-Bus message,
	 * with the details filled in correctly and the D-Bus message
	 * referenced.  The returned structure should not be in a list.
	 */
	TEST_FEATURE ("with D-Bus EmitEvent method");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}
		TEST_FREE_TAG (message);

		blocked = blocked_new (NULL, BLOCKED_EMIT_METHOD, message);

		if (test_alloc_failed) {
			TEST_EQ_P (blocked, NULL);

			TEST_NOT_FREE (message);
			nih_discard (message);
			TEST_FREE (message);
			continue;
		}

		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_LIST_EMPTY (&blocked->entry);
		TEST_EQ (blocked->type, BLOCKED_EMIT_METHOD);
		TEST_EQ_P (blocked->message, message);

		nih_discard (message);
		TEST_NOT_FREE (message);

		nih_free (blocked);
		TEST_FREE (message);
	}


	/* Check that we can create a new blocked record for a D-Bus message,
	 * with the details filled in correctly and the D-Bus message
	 * referenced.  The returned structure should not be in a list.
	 */
	TEST_FEATURE ("with D-Bus Instance.Start method");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}
		TEST_FREE_TAG (message);

		blocked = blocked_new (NULL, BLOCKED_INSTANCE_START_METHOD,
				       message);

		if (test_alloc_failed) {
			TEST_EQ_P (blocked, NULL);

			TEST_NOT_FREE (message);
			nih_discard (message);
			TEST_FREE (message);
			continue;
		}

		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_LIST_EMPTY (&blocked->entry);
		TEST_EQ (blocked->type, BLOCKED_INSTANCE_START_METHOD);
		TEST_EQ_P (blocked->message, message);

		nih_discard (message);
		TEST_NOT_FREE (message);

		nih_free (blocked);
		TEST_FREE (message);
	}


	/* Check that we can create a new blocked record for a D-Bus message,
	 * with the details filled in correctly and the D-Bus message
	 * referenced.  The returned structure should not be in a list.
	 */
	TEST_FEATURE ("with D-Bus Instance.Stop method");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}
		TEST_FREE_TAG (message);

		blocked = blocked_new (NULL, BLOCKED_INSTANCE_STOP_METHOD,
				       message);

		if (test_alloc_failed) {
			TEST_EQ_P (blocked, NULL);

			TEST_NOT_FREE (message);
			nih_discard (message);
			TEST_FREE (message);
			continue;
		}

		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_LIST_EMPTY (&blocked->entry);
		TEST_EQ (blocked->type, BLOCKED_INSTANCE_STOP_METHOD);
		TEST_EQ_P (blocked->message, message);

		nih_discard (message);
		TEST_NOT_FREE (message);

		nih_free (blocked);
		TEST_FREE (message);
	}


	/* Check that we can create a new blocked record for a D-Bus message,
	 * with the details filled in correctly and the D-Bus message
	 * referenced.  The returned structure should not be in a list.
	 */
	TEST_FEATURE ("with D-Bus Instance.Restart method");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}
		TEST_FREE_TAG (message);

		blocked = blocked_new (NULL, BLOCKED_INSTANCE_RESTART_METHOD,
				       message);

		if (test_alloc_failed) {
			TEST_EQ_P (blocked, NULL);

			TEST_NOT_FREE (message);
			nih_discard (message);
			TEST_FREE (message);
			continue;
		}

		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_LIST_EMPTY (&blocked->entry);
		TEST_EQ (blocked->type, BLOCKED_INSTANCE_RESTART_METHOD);
		TEST_EQ_P (blocked->message, message);

		nih_discard (message);
		TEST_NOT_FREE (message);

		nih_free (blocked);
		TEST_FREE (message);
	}


	/* Check that we can create a new blocked record for a D-Bus message,
	 * with the details filled in correctly and the D-Bus message
	 * referenced.  The returned structure should not be in a list.
	 */
	TEST_FEATURE ("with D-Bus Job.Start method");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}
		TEST_FREE_TAG (message);

		blocked = blocked_new (NULL, BLOCKED_JOB_START_METHOD,
				       message);

		if (test_alloc_failed) {
			TEST_EQ_P (blocked, NULL);

			TEST_NOT_FREE (message);
			nih_discard (message);
			TEST_FREE (message);
			continue;
		}

		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_LIST_EMPTY (&blocked->entry);
		TEST_EQ (blocked->type, BLOCKED_JOB_START_METHOD);
		TEST_EQ_P (blocked->message, message);

		nih_discard (message);
		TEST_NOT_FREE (message);

		nih_free (blocked);
		TEST_FREE (message);
	}


	/* Check that we can create a new blocked record for a D-Bus message,
	 * with the details filled in correctly and the D-Bus message
	 * referenced.  The returned structure should not be in a list.
	 */
	TEST_FEATURE ("with D-Bus Job.Stop method");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}
		TEST_FREE_TAG (message);

		blocked = blocked_new (NULL, BLOCKED_JOB_STOP_METHOD,
				       message);

		if (test_alloc_failed) {
			TEST_EQ_P (blocked, NULL);

			TEST_NOT_FREE (message);
			nih_discard (message);
			TEST_FREE (message);
			continue;
		}

		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_LIST_EMPTY (&blocked->entry);
		TEST_EQ (blocked->type, BLOCKED_JOB_STOP_METHOD);
		TEST_EQ_P (blocked->message, message);

		nih_discard (message);
		TEST_NOT_FREE (message);

		nih_free (blocked);
		TEST_FREE (message);
	}


	/* Check that we can create a new blocked record for a D-Bus message,
	 * with the details filled in correctly and the D-Bus message
	 * referenced.  The returned structure should not be in a list.
	 */
	TEST_FEATURE ("with D-Bus Job.Restart method");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}
		TEST_FREE_TAG (message);

		blocked = blocked_new (NULL, BLOCKED_JOB_RESTART_METHOD,
				       message);

		if (test_alloc_failed) {
			TEST_EQ_P (blocked, NULL);

			TEST_NOT_FREE (message);
			nih_discard (message);
			TEST_FREE (message);
			continue;
		}

		TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
		TEST_LIST_EMPTY (&blocked->entry);
		TEST_EQ (blocked->type, BLOCKED_JOB_RESTART_METHOD);
		TEST_EQ_P (blocked->message, message);

		nih_discard (message);
		TEST_NOT_FREE (message);

		nih_free (blocked);
		TEST_FREE (message);
	}
}


int
main (int   argc,
      char *argv[])
{
	test_new ();

	return 0;
}
