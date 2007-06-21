/* upstart
 *
 * test_job.c - test suite for init/job.c
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
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/select.h>

#include <time.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/hash.h>
#include <nih/tree.h>
#include <nih/io.h>
#include <nih/main.h>

#include "event.h"
#include "job.h"
#include "control.h"


void
test_process_new (void)
{
	JobProcess *process;

	/* Check that we can create a new JobProcess structure; the structure
	 * should be allocated with nih_alloc and have sensible defaults.
	 */
	TEST_FUNCTION ("job_process_new");
	TEST_ALLOC_FAIL {
		process = job_process_new (NULL);

		if (test_alloc_failed) {
			TEST_EQ_P (process, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (process, sizeof (JobProcess));

		TEST_EQ (process->script, FALSE);
		TEST_EQ_P (process->command, NULL);
		TEST_EQ (process->pid, 0);

		nih_free (process);
	}
}

void
test_process_copy (void)
{
	JobProcess *process, *copy;

	TEST_FUNCTION ("job_process_copy");

	/* Check that we can create a copy of a fresh structure, with most
	 * fields left unset.
	 */
	TEST_FEATURE ("with unconfigured job process");
	process = job_process_new (NULL);

	TEST_ALLOC_FAIL {
		copy = job_process_copy (NULL, process);

		if (test_alloc_failed) {
			TEST_EQ_P (copy, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (copy, sizeof (JobProcess));

		TEST_EQ (process->script, FALSE);
		TEST_EQ_P (process->command, NULL);
		TEST_EQ (process->pid, 0);

		nih_free (copy);
	}

	nih_free (process);


	/* Check that we can create a copy of an existing structure which
	 * has the same configured details, but a clean state.
	 */
	TEST_FEATURE ("with configured job process");
	process = job_process_new (NULL);
	process->script = TRUE;
	process->command = nih_strdup (process, "/usr/sbin/daemon");
	process->pid = 1000;

	TEST_ALLOC_FAIL {
		copy = job_process_copy (NULL, process);

		if (test_alloc_failed) {
			TEST_EQ_P (copy, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (copy, sizeof (JobProcess));

		TEST_EQ (copy->script, process->script);
		TEST_ALLOC_PARENT (copy->command, copy);
		TEST_EQ_STR (copy->command, process->command);
		TEST_EQ (copy->pid, 0);

		nih_free (copy);
	}

	nih_free (process);
}


void
test_new (void)
{
	Job *job;
	int  i;

	/* Check that we can create a new job structure; the structure
	 * should be allocated with nih_alloc, placed in the jobs hash
	 * and have sensible defaults.
	 */
	TEST_FUNCTION ("job_new");
	job_init ();
	TEST_ALLOC_FAIL {
		job = job_new (NULL, "test");

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (job, sizeof (Job));
		TEST_LIST_NOT_EMPTY (&job->entry);

		TEST_ALLOC_PARENT (job->name, job);
		TEST_EQ_STR (job->name, "test");
		TEST_EQ_P (job->description, NULL);
		TEST_EQ_P (job->author, NULL);
		TEST_EQ_P (job->version, NULL);

		TEST_EQ_P (job->replacement, NULL);
		TEST_EQ_P (job->replacement_for, NULL);
		TEST_EQ_P (job->instance_of, NULL);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_EQ_P (job->start_on, NULL);
		TEST_EQ_P (job->stop_on, NULL);

		TEST_LIST_EMPTY (&job->emits);

		TEST_NE_P (job->process, NULL);
		TEST_ALLOC_PARENT (job->process, job);
		TEST_ALLOC_SIZE (job->process,
				 sizeof (JobProcess *) * PROCESS_LAST);

		for (i = 0; i < PROCESS_LAST; i++)
			TEST_EQ_P (job->process[i], NULL);

		TEST_EQ_P (job->normalexit, NULL);
		TEST_EQ (job->normalexit_len, 0);

		TEST_EQ (job->kill_timeout, JOB_DEFAULT_KILL_TIMEOUT);
		TEST_EQ_P (job->kill_timer, NULL);

		TEST_EQ (job->instance, FALSE);
		TEST_EQ (job->service, FALSE);
		TEST_EQ (job->respawn, FALSE);
		TEST_EQ (job->respawn_limit, JOB_DEFAULT_RESPAWN_LIMIT);
		TEST_EQ (job->respawn_interval, JOB_DEFAULT_RESPAWN_INTERVAL);
		TEST_EQ (job->respawn_count, 0);
		TEST_EQ (job->respawn_time, 0);

		TEST_EQ (job->daemon, FALSE);
		TEST_EQ_P (job->pid_file, NULL);
		TEST_EQ_P (job->pid_binary, NULL);
		TEST_EQ (job->pid_timeout, JOB_DEFAULT_PID_TIMEOUT);
		TEST_EQ_P (job->pid_timer, NULL);

		TEST_EQ (job->console, CONSOLE_NONE);
		TEST_EQ_P (job->env, NULL);

		TEST_EQ (job->umask, JOB_DEFAULT_UMASK);
		TEST_EQ (job->nice, 0);

		for (i = 0; i < RLIMIT_NLIMITS; i++)
			TEST_EQ_P (job->limits[i], NULL);

		TEST_EQ_P (job->chroot, NULL);
		TEST_EQ_P (job->chdir, NULL);

		nih_list_free (&job->entry);
	}
}

void
test_copy (void)
{
	Job           *job, *copy;
	JobProcess    *process;
	EventOperator *oper;
	NihListEntry  *emits;
	int            i;

	TEST_FUNCTION ("job_copy");

	/* Check that we can create a copy of a fresh structure, with most
	 * fields left unset.
	 */
	TEST_FEATURE ("with unconfigured job");
	job = job_new (NULL, "test");

	TEST_ALLOC_FAIL {
		copy = job_copy (NULL, job);

		if (test_alloc_failed) {
			TEST_EQ_P (copy, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (copy, sizeof (Job));
		TEST_LIST_NOT_EMPTY (&copy->entry);

		TEST_ALLOC_PARENT (copy->name, copy);
		TEST_EQ_STR (copy->name, "test");
		TEST_EQ_P (copy->description, NULL);
		TEST_EQ_P (copy->author, NULL);
		TEST_EQ_P (copy->version, NULL);

		TEST_EQ_P (copy->replacement, NULL);
		TEST_EQ_P (copy->replacement_for, NULL);
		TEST_EQ_P (copy->instance_of, NULL);

		TEST_EQ (copy->goal, JOB_STOP);
		TEST_EQ (copy->state, JOB_WAITING);

		TEST_EQ_P (copy->cause, NULL);
		TEST_EQ_P (copy->blocked, NULL);

		TEST_EQ (copy->failed, FALSE);
		TEST_EQ (copy->failed_process, -1);
		TEST_EQ (copy->exit_status, 0);

		TEST_EQ_P (copy->start_on, NULL);
		TEST_EQ_P (copy->stop_on, NULL);

		TEST_LIST_EMPTY (&copy->emits);

		TEST_NE_P (copy->process, NULL);
		TEST_ALLOC_PARENT (copy->process, copy);
		TEST_ALLOC_SIZE (copy->process,
				 sizeof (JobProcess *) * PROCESS_LAST);

		for (i = 0; i < PROCESS_LAST; i++)
			TEST_EQ_P (copy->process[i], NULL);

		TEST_EQ_P (copy->normalexit, NULL);
		TEST_EQ (copy->normalexit_len, 0);

		TEST_EQ (copy->kill_timeout, JOB_DEFAULT_KILL_TIMEOUT);
		TEST_EQ_P (copy->kill_timer, NULL);

		TEST_EQ (copy->instance, FALSE);
		TEST_EQ (copy->service, FALSE);
		TEST_EQ (copy->respawn, FALSE);
		TEST_EQ (copy->respawn_limit, JOB_DEFAULT_RESPAWN_LIMIT);
		TEST_EQ (copy->respawn_interval, JOB_DEFAULT_RESPAWN_INTERVAL);
		TEST_EQ (copy->respawn_count, 0);
		TEST_EQ (copy->respawn_time, 0);

		TEST_EQ (copy->daemon, FALSE);
		TEST_EQ_P (copy->pid_file, NULL);
		TEST_EQ_P (copy->pid_binary, NULL);
		TEST_EQ (copy->pid_timeout, JOB_DEFAULT_PID_TIMEOUT);
		TEST_EQ_P (copy->pid_timer, NULL);

		TEST_EQ (copy->console, CONSOLE_NONE);
		TEST_EQ_P (copy->env, NULL);

		TEST_EQ (copy->umask, JOB_DEFAULT_UMASK);
		TEST_EQ (copy->nice, 0);

		for (i = 0; i < RLIMIT_NLIMITS; i++)
			TEST_EQ_P (copy->limits[i], NULL);

		TEST_EQ_P (copy->chroot, NULL);
		TEST_EQ_P (copy->chdir, NULL);

		nih_list_free (&copy->entry);
	}

	nih_list_free (&job->entry);


	/* Check that we can create a copy of an existing structure which
	 * has the same configured details, but a clean state except for
	 * the event expressions which should be copied.
	 */
	TEST_FEATURE ("with configured");
	job = job_new (NULL, "test");
	job->description = nih_strdup (job, "an example job");
	job->author = nih_strdup (job, "joe bloggs");
	job->version = nih_strdup (job, "1.0");

	job->replacement = (void *)-1;
	job->replacement_for = (void *)-1;
	job->instance_of = (void *)-1;

	job->goal = JOB_STOP;
	job->state = JOB_POST_STOP;

	job->cause = (void *)-1;
	job->blocked = (void *)-1;

	job->failed = TRUE;
	job->failed_process = PROCESS_MAIN;
	job->exit_status = SIGSEGV << 8;

	job->start_on = event_operator_new (job, EVENT_OR, NULL, NULL);

	oper = event_operator_new (job, EVENT_MATCH, "foo", NULL);
	oper->value = TRUE;
	oper->event = event_new (oper, "foo", NULL, NULL);
	event_ref (oper->event);
	oper->blocked = TRUE;
	event_block (oper->event);
	nih_tree_add (&job->start_on->node, &oper->node, NIH_TREE_LEFT);

	oper = event_operator_new (job, EVENT_MATCH, "bar", NULL);
	NIH_MUST (nih_str_array_add (&oper->args, oper, NULL, "frodo"));
	NIH_MUST (nih_str_array_add (&oper->args, oper, NULL, "bilbo"));
	nih_tree_add (&job->start_on->node, &oper->node, NIH_TREE_RIGHT);

	job->stop_on = event_operator_new (job, EVENT_MATCH, "baz", NULL);

	emits = nih_list_entry_new (job);
	emits->str = nih_strdup (emits, "wibble");
	nih_list_add (&job->emits, &emits->entry);

	job->normalexit = nih_alloc (job, sizeof (int) * 2);
	job->normalexit[0] = 99;
	job->normalexit[1] = 100;
	job->normalexit_len = 2;

	job->kill_timeout = 10;
	job->kill_timer = (void *)-1;

	job->instance = TRUE;
	job->service = TRUE;
	job->respawn = TRUE;
	job->respawn_limit = 20;
	job->respawn_interval = 100;
	job->respawn_count = 8;
	job->respawn_time = time (NULL) - 20;

	job->daemon = TRUE;
	job->pid_file = nih_strdup (job, "/var/run/job.pid");
	job->pid_binary = nih_strdup (job, "/usr/lib/daemon");
	job->pid_timeout = 30;
	job->pid_timer = (void *)-1;

	process = job_process_new (job->process);
	process->script = FALSE;
	process->command = nih_strdup (process, "/usr/sbin/daemon");
	process->pid = 1000;
	job->process[PROCESS_MAIN] = process;

	process = job_process_new (job);
	process->script = TRUE;
	process->command = nih_strdup (process, "mkdir /var/run/daemon\n");
	job->process[PROCESS_PRE_START] = process;

	process = job_process_new (job);
	process->script = TRUE;
	process->command = nih_strdup (process,
				       "echo start | nc -q0 127.0.0.1 80\n");
	job->process[PROCESS_POST_START] = process;

	process = job_process_new (job);
	process->script = TRUE;
	process->command = nih_strdup (process,
				       "echo stop | nc -q0 127.0.0.1 80\n");
	process->pid = 1010;
	job->process[PROCESS_PRE_STOP] = process;

	process = job_process_new (job);
	process->script = TRUE;
	process->command = nih_strdup (process, "rm -rf /var/run/daemon\n");
	job->process[PROCESS_POST_STOP] = process;

	job->console = CONSOLE_OUTPUT;

	job->env = nih_str_array_new (job);
	NIH_MUST (nih_str_array_add (&job->env, job, NULL, "EH=OH"));
	NIH_MUST (nih_str_array_add (&job->env, job, NULL, "LA=LA"));

	job->umask = 002;
	job->nice = -5;

	job->limits[RLIMIT_CORE] = nih_new (job, struct rlimit);
	job->limits[RLIMIT_CORE]->rlim_cur = RLIM_INFINITY;
	job->limits[RLIMIT_CORE]->rlim_max = RLIM_INFINITY;

	job->limits[RLIMIT_CPU] = nih_new (job, struct rlimit);
	job->limits[RLIMIT_CPU]->rlim_cur = 120;
	job->limits[RLIMIT_CPU]->rlim_max = 180;

	job->chroot = nih_strdup (job, "/var/run/daemon");
	job->chdir = nih_strdup (job, "/etc");

	TEST_ALLOC_FAIL {
		copy = job_copy (NULL, job);

		if (test_alloc_failed) {
			TEST_EQ_P (copy, NULL);

			oper = (EventOperator *)job->start_on->node.left;
			TEST_EQ (oper->event->refs, 1);
			TEST_EQ (oper->event->blockers, 1);
			continue;
		}

		TEST_ALLOC_SIZE (copy, sizeof (Job));
		TEST_LIST_NOT_EMPTY (&copy->entry);

		TEST_ALLOC_PARENT (copy->name, copy);
		TEST_EQ_STR (copy->name, "test");
		TEST_ALLOC_PARENT (copy->description, copy);
		TEST_EQ_STR (copy->description, job->description);
		TEST_ALLOC_PARENT (copy->author, copy);
		TEST_EQ_STR (copy->author, job->author);
		TEST_ALLOC_PARENT (copy->version, copy);
		TEST_EQ_STR (copy->version, job->version);

		TEST_EQ_P (copy->replacement, NULL);
		TEST_EQ_P (copy->replacement_for, NULL);
		TEST_EQ_P (copy->instance_of, NULL);

		TEST_EQ (copy->goal, JOB_STOP);
		TEST_EQ (copy->state, JOB_WAITING);

		TEST_EQ_P (copy->cause, NULL);
		TEST_EQ_P (copy->blocked, NULL);

		TEST_EQ (copy->failed, FALSE);
		TEST_EQ (copy->failed_process, -1);
		TEST_EQ (copy->exit_status, 0);

		oper = (EventOperator *)copy->start_on;
		TEST_ALLOC_PARENT (oper, copy);
		TEST_ALLOC_SIZE (oper, sizeof (EventOperator));
		TEST_EQ (oper->type, EVENT_OR);

		oper = (EventOperator *)copy->start_on->node.left;
		TEST_ALLOC_PARENT (oper, copy->start_on);
		TEST_ALLOC_SIZE (oper, sizeof (EventOperator));
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "foo");
		TEST_EQ_P (oper->args, NULL);
		TEST_EQ (oper->value, TRUE);
		TEST_EQ (oper->blocked, TRUE);
		TEST_EQ (oper->event->refs, 2);
		TEST_EQ (oper->event->blockers, 2);

		oper = (EventOperator *)copy->start_on->node.right;
		TEST_ALLOC_PARENT (oper, copy->start_on);
		TEST_ALLOC_SIZE (oper, sizeof (EventOperator));
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "bar");
		TEST_ALLOC_PARENT (oper->args, oper);
		TEST_ALLOC_SIZE (oper->args, sizeof (char *) * 3);
		TEST_ALLOC_PARENT (oper->args[0], oper->args);
		TEST_ALLOC_PARENT (oper->args[1], oper->args);
		TEST_EQ_STR (oper->args[0], "frodo");
		TEST_EQ_STR (oper->args[1], "bilbo");
		TEST_EQ_P (oper->args[2], NULL);

		oper = (EventOperator *)copy->stop_on;
		TEST_ALLOC_PARENT (oper, copy);
		TEST_ALLOC_SIZE (oper, sizeof (EventOperator));
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "baz");
		TEST_EQ_P (oper->args, NULL);

		TEST_LIST_NOT_EMPTY (&copy->emits);

		emits = (NihListEntry *)copy->emits.next;
		TEST_ALLOC_PARENT (emits, copy);
		TEST_ALLOC_SIZE (emits, sizeof (NihListEntry));
		TEST_ALLOC_PARENT (emits->str, emits);
		TEST_EQ_STR (emits->str, "wibble");

		TEST_EQ_P (emits->entry.next, &copy->emits);

		TEST_NE_P (copy->process, NULL);
		TEST_ALLOC_PARENT (copy->process, copy);
		TEST_ALLOC_SIZE (copy->process,
				 sizeof (JobProcess *) * PROCESS_LAST);

		for (i = 0; i < PROCESS_LAST; i++) {
			TEST_ALLOC_PARENT (copy->process[i], copy->process);
			TEST_ALLOC_SIZE (copy->process[i],
					 sizeof (JobProcess));
			TEST_EQ (copy->process[i]->script,
				 job->process[i]->script);
			TEST_EQ_STR (copy->process[i]->command,
				     job->process[i]->command);
			TEST_EQ (copy->process[i]->pid, 0);
		}

		TEST_ALLOC_PARENT (copy->normalexit, copy);
		TEST_ALLOC_SIZE (copy->normalexit, sizeof (int) * 2);
		TEST_EQ (copy->normalexit[0], 99);
		TEST_EQ (copy->normalexit[1], 100);
		TEST_EQ (copy->normalexit_len, 2);

		TEST_EQ (copy->kill_timeout, job->kill_timeout);
		TEST_EQ_P (copy->kill_timer, NULL);

		TEST_EQ (copy->instance, job->instance);
		TEST_EQ (copy->service, job->service);
		TEST_EQ (copy->respawn, job->respawn);
		TEST_EQ (copy->respawn_limit, job->respawn_limit);
		TEST_EQ (copy->respawn_interval, job->respawn_interval);
		TEST_EQ (copy->respawn_count, 0);
		TEST_EQ (copy->respawn_time, 0);

		TEST_EQ (copy->daemon, job->daemon);
		TEST_ALLOC_PARENT (copy->pid_file, copy);
		TEST_EQ_STR (copy->pid_file, job->pid_file);
		TEST_ALLOC_PARENT (copy->pid_binary, copy);
		TEST_EQ_STR (copy->pid_binary, job->pid_binary);
		TEST_EQ (copy->pid_timeout, job->pid_timeout);
		TEST_EQ_P (copy->pid_timer, NULL);

		TEST_EQ (copy->console, job->console);
		TEST_ALLOC_PARENT (copy->env, copy);
		TEST_ALLOC_SIZE (copy->env, sizeof (char *) * 3);
		TEST_EQ_STR (copy->env[0], "EH=OH");
		TEST_EQ_STR (copy->env[1], "LA=LA");
		TEST_EQ_P (copy->env[2], NULL);

		TEST_EQ (copy->umask, job->umask);
		TEST_EQ (copy->nice, job->nice);

		for (i = 0; i < RLIMIT_NLIMITS; i++) {
			if (job->limits[i]) {
				TEST_ALLOC_PARENT (copy->limits[i], copy);
				TEST_ALLOC_SIZE (copy->limits[i],
						 sizeof (struct rlimit));
				TEST_EQ (copy->limits[i]->rlim_cur,
					 job->limits[i]->rlim_cur);
				TEST_EQ (copy->limits[i]->rlim_max,
					 job->limits[i]->rlim_max);
			} else {
				TEST_EQ_P (copy->limits[i], NULL);
			}
		}

		TEST_ALLOC_PARENT (copy->chroot, copy);
		TEST_EQ_STR (copy->chroot, job->chroot);
		TEST_ALLOC_PARENT (copy->chdir, copy);
		TEST_EQ_STR (copy->chdir, job->chdir);

		event_operator_reset (copy->start_on);
		nih_list_free (&copy->entry);
	}

	event_operator_reset (job->start_on);
	nih_list_free (&job->entry);

	event_poll ();
}


void
test_find_by_name (void)
{
	Job *job1, *job2, *job3, *ptr;

	TEST_FUNCTION ("job_find_by_name");
	job1 = job_new (NULL, "foo");
	job2 = job_new (NULL, "bar");
	job3 = job_new (NULL, "baz");

	/* Check that we can find a job that exists by its name. */
	TEST_FEATURE ("with name we expect to find");
	ptr = job_find_by_name ("foo");

	TEST_EQ_P (ptr, job1);


	/* Check that we get NULL if the job doesn't exist. */
	TEST_FEATURE ("with name we do not expect to find");
	ptr = job_find_by_name ("frodo");

	TEST_EQ_P (ptr, NULL);


	/* Check that we ignore jobs about to be deleted. */
	TEST_FEATURE ("with deleted job");
	job2->state = JOB_DELETED;
	ptr = job_find_by_name ("bar");

	TEST_EQ_P (ptr, NULL);


	/* Check that if an entry is an instance, we get the real job. */
	TEST_FEATURE ("with instance");
	job2->state = JOB_WAITING;
	job2->instance_of = job3;
	ptr = job_find_by_name ("bar");

	TEST_EQ (ptr, job3);


	/* Check that if an entry is an replacement, we get the current job. */
	TEST_FEATURE ("with replacement");
	job2->instance_of = NULL;
	job2->replacement_for = job3;
	ptr = job_find_by_name ("bar");

	TEST_EQ (ptr, job3);


	/* Check that we get NULL if the job list is empty, and nothing
	 * bad happens.
	 */
	TEST_FEATURE ("with empty job list");
	nih_list_free (&job3->entry);
	nih_list_free (&job2->entry);
	nih_list_free (&job1->entry);
	ptr = job_find_by_name ("foo");

	TEST_EQ_P (ptr, NULL);
}

void
test_find_by_pid (void)
{
	Job         *job1, *job2, *job3, *job4, *job5, *ptr;
	ProcessType  process;

	TEST_FUNCTION ("job_find_by_pid");
	job1 = job_new (NULL, "foo");
	job1->process[PROCESS_MAIN] = job_process_new (job1);
	job1->process[PROCESS_MAIN]->pid = 10;
	job1->process[PROCESS_POST_START] = job_process_new (job1);
	job1->process[PROCESS_POST_START]->pid = 15;
	job2 = job_new (NULL, "bar");
	job3 = job_new (NULL, "baz");
	job3->process[PROCESS_PRE_START] = job_process_new (job3);
	job3->process[PROCESS_PRE_START]->pid = 20;
	job4 = job_new (NULL, "frodo");
	job4->process[PROCESS_MAIN] = job_process_new (job4);
	job4->process[PROCESS_MAIN]->pid = 25;
	job4->process[PROCESS_PRE_STOP] = job_process_new (job4);
	job4->process[PROCESS_PRE_STOP]->pid = 30;
	job5 = job_new (NULL, "bilbo");
	job5->process[PROCESS_POST_STOP] = job_process_new (job5);
	job5->process[PROCESS_POST_STOP]->pid = 35;

	/* Check that we can find a job that exists by the pid of its
	 * primary process.
	 */
	TEST_FEATURE ("with pid we expect to find");
	ptr = job_find_by_pid (10, &process);

	TEST_EQ_P (ptr, job1);
	TEST_EQ (process, PROCESS_MAIN);


	/* Check that we can find a job that exists by the pid of its
	 * pre-start process.
	 */
	TEST_FEATURE ("with pre-start pid we expect to find");
	ptr = job_find_by_pid (20, &process);

	TEST_EQ_P (ptr, job3);
	TEST_EQ (process, PROCESS_PRE_START);


	/* Check that we can find a job that exists by the pid of its
	 * post-start process.
	 */
	TEST_FEATURE ("with post-start pid we expect to find");
	ptr = job_find_by_pid (15, &process);

	TEST_EQ_P (ptr, job1);
	TEST_EQ (process, PROCESS_POST_START);


	/* Check that we can find a job that exists by the pid of its
	 * pre-stop process.
	 */
	TEST_FEATURE ("with pre-stop pid we expect to find");
	ptr = job_find_by_pid (30, &process);

	TEST_EQ_P (ptr, job4);
	TEST_EQ (process, PROCESS_PRE_STOP);


	/* Check that we can find a job that exists by the pid of its
	 * pre-stop process.
	 */
	TEST_FEATURE ("with post-stop pid we expect to find");
	ptr = job_find_by_pid (35, &process);

	TEST_EQ_P (ptr, job5);
	TEST_EQ (process, PROCESS_POST_STOP);


	/* Check that we get NULL if no job has a process with that pid. */
	TEST_FEATURE ("with pid we do not expect to find");
	ptr = job_find_by_pid (100, NULL);

	TEST_EQ_P (ptr, NULL);


	/* Check that we get NULL if there are jobs in the hash, but none
	 * have pids.
	 */
	TEST_FEATURE ("with no pids in job table");
	nih_list_free (&job5->entry);
	nih_list_free (&job4->entry);
	nih_list_free (&job3->entry);
	nih_list_free (&job1->entry);
	ptr = job_find_by_pid (20, NULL);

	TEST_EQ_P (ptr, NULL);


	/* Check that we get NULL if there are no jobs in the hash. */
	TEST_FEATURE ("with empty job table");
	nih_list_free (&job2->entry);
	ptr = job_find_by_pid (20, NULL);

	TEST_EQ_P (ptr, NULL);
}

void
test_find_by_id (void)
{
	Job          *job1, *job2, *job3, *ptr;
	unsigned int  id;

	TEST_FUNCTION ("job_find_by_id");
	job1 = job_new (NULL, "foo");
	job2 = job_new (NULL, "bar");
	job3 = job_new (NULL, "bar");

	/* Check that we can find a job by its id. */
	TEST_FEATURE ("with id we expect to find");
	ptr = job_find_by_id (job2->id);

	TEST_EQ_P (ptr, job2);


	/* Check that we get NULL if the id doesn't exist. */
	TEST_FEATURE ("with id we do not expect to find");
	id = job3->id;
	while ((id == job1->id) || (id == job2->id) || (id == job3->id))
		id++;

	ptr = job_find_by_id (id);

	TEST_EQ_P (ptr, NULL);


	/* Check that we get NULL if the job list is empty, and nothing
	 * bad happens.
	 */
	TEST_FEATURE ("with empty job table");
	id = job1->id;

	nih_list_free (&job3->entry);
	nih_list_free (&job2->entry);
	nih_list_free (&job1->entry);
	ptr = job_find_by_id (id);

	TEST_EQ_P (ptr, NULL);
}


void
test_instance (void)
{
	Job           *job, *ptr;
	EventOperator *oper;

	TEST_FUNCTION ("job_instance");

	job = job_new (NULL, "test");
	job->process[PROCESS_MAIN] = job_process_new (job);
	job->process[PROCESS_MAIN]->command = "echo";

	job->start_on = event_operator_new (job, EVENT_AND, NULL, NULL);

	oper = event_operator_new (job->start_on, EVENT_MATCH, "foo", NULL);
	nih_tree_add (&job->start_on->node, &oper->node, NIH_TREE_LEFT);

	oper = event_operator_new (job->start_on, EVENT_MATCH, "bar", NULL);
	nih_tree_add (&job->start_on->node, &oper->node, NIH_TREE_RIGHT);


	/* Check that we get an ordinary job returned as-is; with the event
	 * expression remaining correctly referenced.
	 */
	TEST_FEATURE ("with non-instance job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job->start_on->value = TRUE;

			oper = (EventOperator *)job->start_on->node.left;
			oper->value = TRUE;
			oper->event = event_new (oper, "foo", NULL, NULL);
			event_ref (oper->event);
			oper->blocked = TRUE;
			event_block (oper->event);

			oper = (EventOperator *)job->start_on->node.right;
			oper->value = TRUE;
			oper->event = event_new (oper, "bar", NULL, NULL);
			event_ref (oper->event);
			oper->blocked = TRUE;
			event_block (oper->event);
		}

		ptr = job_instance (job);

		TEST_EQ_P (ptr, job);

		oper = job->start_on;
		TEST_EQ (oper->value, TRUE);

		oper = (EventOperator *)job->start_on->node.left;
		TEST_EQ (oper->value, TRUE);
		TEST_EQ (oper->blocked, TRUE);
		TEST_NE_P (oper->event, NULL);
		TEST_EQ (oper->event->refs, 1);
		TEST_EQ (oper->event->blockers, 1);

		oper = (EventOperator *)job->start_on->node.right;
		TEST_EQ (oper->value, TRUE);
		TEST_EQ (oper->blocked, TRUE);
		TEST_NE_P (oper->event, NULL);
		TEST_EQ (oper->event->refs, 1);
		TEST_EQ (oper->event->blockers, 1);

		event_operator_reset (job->start_on);
	}


	/* Check that we get a new instance for an instance master. */
	TEST_FEATURE ("with instance master job");
	job->instance = TRUE;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job->start_on->value = TRUE;

			oper = (EventOperator *)job->start_on->node.left;
			oper->value = TRUE;
			oper->event = event_new (oper, "foo", NULL, NULL);
			event_ref (oper->event);
			oper->blocked = TRUE;
			event_block (oper->event);

			oper = (EventOperator *)job->start_on->node.right;
			oper->value = TRUE;
			oper->event = event_new (oper, "bar", NULL, NULL);
			event_ref (oper->event);
			oper->blocked = TRUE;
			event_block (oper->event);
		}

		ptr = job_instance (job);

		TEST_NE_P (ptr, job);
		TEST_EQ (ptr->instance, TRUE);
		TEST_EQ_P (ptr->instance_of, job);

		oper = job->start_on;
		TEST_EQ (oper->value, FALSE);

		oper = (EventOperator *)job->start_on->node.left;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ (oper->blocked, FALSE);
		TEST_EQ_P (oper->event, NULL);

		oper = (EventOperator *)job->start_on->node.right;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ (oper->blocked, FALSE);
		TEST_EQ_P (oper->event, NULL);

		oper = ptr->start_on;
		TEST_EQ (oper->value, TRUE);

		oper = (EventOperator *)ptr->start_on->node.left;
		TEST_EQ (oper->value, TRUE);
		TEST_EQ (oper->blocked, TRUE);
		TEST_NE_P (oper->event, NULL);
		TEST_EQ (oper->event->refs, 1);
		TEST_EQ (oper->event->blockers, 1);

		oper = (EventOperator *)ptr->start_on->node.right;
		TEST_EQ (oper->value, TRUE);
		TEST_EQ (oper->blocked, TRUE);
		TEST_NE_P (oper->event, NULL);
		TEST_EQ (oper->event->refs, 1);
		TEST_EQ (oper->event->blockers, 1);

		event_operator_reset (ptr->start_on);

		nih_list_free (&ptr->entry);
	}


	nih_list_free (&job->entry);
	event_poll ();
}

void
test_change_goal (void)
{
	Event *event;
	Job   *job;

	TEST_FUNCTION ("job_change_goal");
	program_name = "test";

	job = job_new (NULL, "test");
	job->process[PROCESS_MAIN] = job_process_new (job);
	job->process[PROCESS_MAIN]->command = "echo";
	job->process[PROCESS_PRE_START] = job_process_new (job);
	job->process[PROCESS_PRE_START]->command = "echo";
	job->process[PROCESS_POST_STOP] = job_process_new (job);
	job->process[PROCESS_POST_STOP]->command = "echo";


	/* Check that an attempt to start a waiting job results in the
	 * goal being changed to start, and the state transitioned to
	 * starting.
	 */
	TEST_FEATURE ("with waiting job");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_WAITING;
		job->blocked = NULL;

		job_change_goal (job, JOB_START, NULL);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STARTING);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);
	}


	/* Check that an attempt to start a job that's in the process of
	 * stopping changes only the goal, and leaves the rest of the
	 * state transition up to the normal process.
	 */
	TEST_FEATURE ("with stopping job");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_KILLED;
		job->process[PROCESS_MAIN]->pid = 1;
		job->blocked = NULL;

		job_change_goal (job, JOB_START, NULL);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_KILLED);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 1);

		TEST_EQ_P (job->blocked, NULL);
	}

	job->process[PROCESS_MAIN]->pid = 0;


	/* Check that starting a job with a cause set unreferences it
	 * and sets it to NULL in the job.
	 */
	TEST_FEATURE ("with existing cause");
	event = event_new (NULL, "foo", NULL, NULL);

	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_STOPPING;
		job->process[PROCESS_MAIN]->pid = 1;
		job->cause = event;
		event->progress = EVENT_HANDLING;
		event->blockers = 1;
		event->refs = 1;
		job->blocked = NULL;

		job_change_goal (job, JOB_START, NULL);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 1);

		TEST_EQ_P (job->cause, NULL);

		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->refs, 0);

		TEST_EQ_P (job->blocked, NULL);
	}

	nih_list_free (&event->entry);
	job->cause = NULL;
	job->process[PROCESS_MAIN]->pid = 0;


	/* Check that starting a job with a cause passed references that event
	 * and sets the cause in the job.
	 */
	TEST_FEATURE ("with new cause");
	event = event_new (NULL, "foo", NULL, NULL);

	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;
		job->process[PROCESS_POST_STOP]->pid = 1;
		job->cause = NULL;
		event->blockers = 0;
		event->refs = 0;
		job->blocked = NULL;

		job_change_goal (job, JOB_START, event);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_POST_STOP);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 0);
		TEST_EQ (job->process[PROCESS_POST_STOP]->pid, 1);

		TEST_EQ_P (job->cause, event);

		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->refs, 1);

		TEST_EQ_P (job->blocked, NULL);
	}

	nih_list_free (&event->entry);
	job->cause = NULL;
	job->process[PROCESS_POST_STOP]->pid = 0;


	/* Check that an attempt to start a job that's running and still
	 * with a start goal does nothing.
	 */
	TEST_FEATURE ("with running job and start");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->process[PROCESS_MAIN]->pid = 1;
		job->blocked = NULL;

		job_change_goal (job, JOB_START, NULL);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 1);

		TEST_EQ_P (job->blocked, NULL);
	}

	job->process[PROCESS_MAIN]->pid = 0;


	/* Check that an attempt to stop a running job results in the goal
	 * and the state being changed.
	 */
	TEST_FEATURE ("with running job and stop");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->process[PROCESS_MAIN]->pid = 1;
		job->blocked = NULL;

		job_change_goal (job, JOB_STOP, NULL);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 1);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);
	}

	job->process[PROCESS_MAIN]->pid = 0;


	/* Check that an attempt to stop a running job without any process
	 * also results in the state being changed.
	 */
	TEST_FEATURE ("with running job and no process");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->blocked = NULL;

		job_change_goal (job, JOB_STOP, NULL);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);
	}


	/* Check that an attempt to stop a starting job only results in the
	 * goal being changed, the state should not be changed.
	 */
	TEST_FEATURE ("with starting job");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->process[PROCESS_PRE_START]->pid = 1;
		job->blocked = NULL;

		job_change_goal (job, JOB_STOP, NULL);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_PRE_START);
		TEST_EQ (job->process[PROCESS_PRE_START]->pid, 1);

		TEST_EQ_P (job->blocked, NULL);
	}

	job->process[PROCESS_PRE_START]->pid = 0;


	/* Check that stopping a job with a cause event set unreferences the
	 * event and sets it to NULL.
	 */
	TEST_FEATURE ("with existing cause");
	event = event_new (NULL, "foo", NULL, NULL);

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->process[PROCESS_MAIN]->pid = 1;
		job->cause = event;
		event->blockers = 1;
		event->refs = 1;
		event->progress = EVENT_HANDLING;
		job->blocked = NULL;

		job_change_goal (job, JOB_STOP, NULL);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_SPAWNED);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 1);
		TEST_EQ_P (job->cause, NULL);

		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->refs, 0);

		TEST_EQ_P (job->blocked, NULL);
	}

	nih_list_free (&event->entry);
	job->cause = NULL;
	job->process[PROCESS_MAIN]->pid = 0;


	/* Check that stopping a job passing a cause references that event
	 * and sets the job's cause.
	 */
	TEST_FEATURE ("with existing cause");
	event = event_new (NULL, "foo", NULL, NULL);

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_STARTING;
		job->cause = NULL;
		event->blockers = 0;
		event->refs = 0;
		job->blocked = NULL;

		job_change_goal (job, JOB_STOP, event);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STARTING);

		TEST_EQ_P (job->cause, event);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->refs, 1);

		TEST_EQ_P (job->blocked, NULL);
	}

	nih_list_free (&event->entry);
	job->cause = NULL;


	/* Check that an attempt to stop a waiting job does nothing. */
	TEST_FEATURE ("with waiting job");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_WAITING;
		job->blocked = NULL;

		job_change_goal (job, JOB_STOP, NULL);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);

		TEST_EQ_P (job->blocked, NULL);
	}


	nih_list_free (&job->entry);
	event_poll ();
}


void
test_change_state (void)
{
	FILE        *output;
	Job         *job;
	Event       *cause, *event;
	struct stat  statbuf;
	char         dirname[PATH_MAX], filename[PATH_MAX];
	JobProcess  *tmp;
	pid_t        pid;
	int          status;

	TEST_FUNCTION ("job_change_state");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (dirname);
	mkdir (dirname, 0700);

	job = job_new (NULL, "test");
	job->process[PROCESS_MAIN] = job_process_new (job);
	job->process[PROCESS_MAIN]->command = nih_sprintf (
		job->process[PROCESS_MAIN], "touch %s/run", dirname);
	job->process[PROCESS_PRE_START] = job_process_new (job);
	job->process[PROCESS_PRE_START]->command = nih_sprintf (
		job->process[PROCESS_PRE_START], "touch %s/start", dirname);
	job->process[PROCESS_POST_STOP] = job_process_new (job);
	job->process[PROCESS_POST_STOP]->command = nih_sprintf (
		job->process[PROCESS_POST_STOP], "touch %s/stop", dirname);
	job->respawn_limit = 0;

	event_init ();

	cause = event_new (NULL, "wibble", NULL, NULL);
	nih_list_remove (&cause->entry);


	/* Check that a job can move from waiting to starting.  This
	 * should emit the starting event and block on it after clearing
	 * out any failed information from the last time the job was run.
	 */
	TEST_FEATURE ("waiting to starting");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_WAITING;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = TRUE;
		job->failed_process = PROCESS_POST_STOP;
		job->exit_status = 1;

		job_change_state (job, JOB_STARTING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STARTING);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->refs, 1);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "starting");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_P (event->args[1], NULL);
		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 0);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);
	}


	/* Check that if a job tries to move from waiting to starting too
	 * many times in a given period then it is caught, a message is
	 * output and the job sent back to stopped and waiting.  We get
	 * a stopped event, and the cause is cleared.  The failed information
	 * should reflect that we were stopped by the starting state.
	 */
	TEST_FEATURE ("waiting to starting too fast");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_WAITING;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job->respawn_limit = 10;
		job->respawn_interval = 1000;
		job->respawn_time = time (NULL);
		job->respawn_count = 10;

		TEST_DIVERT_STDERR (output) {
			job_change_state (job, JOB_STARTING);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->refs, 0);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopped");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_STR (event->args[1], "failed");
		TEST_EQ_STR (event->args[2], "respawn");
		TEST_EQ_P (event->args[3], NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output,
			      "test: test respawning too fast, stopped\n");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	job->respawn_limit = 0;
	job->respawn_interval = JOB_DEFAULT_RESPAWN_INTERVAL;
	job->respawn_time = 0;
	job->respawn_count = 0;


	/* Check that a job with a start process can move from starting
	 * to pre-start, and have the process run.
	 */
	TEST_FEATURE ("starting to pre-start");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_STARTING;
		job->process[PROCESS_PRE_START]->pid = 0;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_PRE_START);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_PRE_START);
		TEST_NE (job->process[PROCESS_PRE_START]->pid, 0);

		waitpid (job->process[PROCESS_PRE_START]->pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/start");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->refs, 1);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);
	}

	job->process[PROCESS_PRE_START]->pid = 0;


	/* Check that a job without a start process can move from starting
	 * to pre-start, skipping over that state, and instead going all
	 * the way through to the running state.  Because we get there,
	 * we should get a started event emitted.
	 */
	TEST_FEATURE ("starting to pre-start without process");
	tmp = job->process[PROCESS_PRE_START];
	job->process[PROCESS_PRE_START] = NULL;

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_STARTING;
		job->process[PROCESS_MAIN]->pid = 0;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_PRE_START);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_NE (job->process[PROCESS_MAIN]->pid, 0);

		waitpid (job->process[PROCESS_MAIN]->pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/run");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->refs, 1);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_P (event->args[1], NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);
	}

	job->process[PROCESS_PRE_START] = tmp;
	job->process[PROCESS_MAIN]->pid = 0;


	/* Check that a job with a main process can move from pre-start to
	 * spawned and have the process run, and as it's not a daemon,
	 * the state will be skipped forwards to running and the started
	 * event emitted.
	 */
	TEST_FEATURE ("pre-start to spawned");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->process[PROCESS_MAIN]->pid = 0;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_SPAWNED);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_NE (job->process[PROCESS_MAIN]->pid, 0);

		waitpid (job->process[PROCESS_MAIN]->pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/run");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->refs, 1);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_P (event->args[1], NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);
	}

	job->process[PROCESS_MAIN]->pid = 0;


 	/* Check that a job without a main process can move from pre-start
	 * straight to running skipping the interim steps, and has the
	 * started event emitted.
	 */
	TEST_FEATURE ("pre-start to spawned without process");
	tmp = job->process[PROCESS_MAIN];
	job->process[PROCESS_MAIN] = NULL;

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_PRE_START;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_SPAWNED);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->refs, 1);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_P (event->args[1], NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);
	}

	job->process[PROCESS_MAIN] = tmp;


	/* Check that a job which has a main process that becomes a daemon
	 * can move from pre-start to spawned and have the process run.
	 * The state will remain in spawned until that process dies, and
	 * we have a better pid.
	 */
	TEST_FEATURE ("pre-start to spawned for daemon");
	job->daemon = TRUE;

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->process[PROCESS_MAIN]->pid = 0;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_SPAWNED);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_SPAWNED);
		TEST_NE (job->process[PROCESS_MAIN]->pid, 0);

		waitpid (job->process[PROCESS_MAIN]->pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/run");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->refs, 1);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);
	}

	job->daemon = FALSE;
	job->process[PROCESS_MAIN]->pid = 0;


	/* Check that a job with a post-start process can move from spawned
	 * to post-start, and have the process run.
	 */
	TEST_FEATURE ("spawned to post-start");
	job->process[PROCESS_POST_START] = job_process_new (job);
	job->process[PROCESS_POST_START]->command = nih_sprintf (
		job->process[PROCESS_POST_START],
		"touch %s/post-start", dirname);

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->process[PROCESS_MAIN]->pid = 1;
		job->process[PROCESS_POST_START]->pid = 0;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_POST_START);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_POST_START);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 1);
		TEST_NE (job->process[PROCESS_POST_START]->pid, 0);

		waitpid (job->process[PROCESS_POST_START]->pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/post-start");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->refs, 1);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);
	}

	nih_free (job->process[PROCESS_POST_START]);
	job->process[PROCESS_POST_START] = NULL;
	job->process[PROCESS_MAIN]->pid = 0;


	/* Check that a job without a post-start process can move from
	 * spawned to post-start, skipping over that state, and instead
	 * going to the running state.  Because we get there, we should
	 * get a started event emitted.
	 */
	TEST_FEATURE ("spawned to post-start without process");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->process[PROCESS_MAIN]->pid = 1;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_POST_START);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 1);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->refs, 1);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_P (event->args[1], NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);
	}

	job->process[PROCESS_MAIN]->pid = 0;


	/* Check that a task can move from post-start to running, which will
	 * emit the started event but leave the cause alone.
	 */
	TEST_FEATURE ("post-start to running");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_POST_START;
		job->process[PROCESS_MAIN]->pid = 1;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_RUNNING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 1);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->refs, 1);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_P (event->args[1], NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);
	}

	job->process[PROCESS_MAIN]->pid = 0;


	/* Check that a service can move from post-start to running, which
	 * will emit the started event and clear the cause since the job
	 * has reached the desired state.
	 */
	TEST_FEATURE ("post-start to running for service");
	job->service = TRUE;

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_POST_START;
		job->process[PROCESS_MAIN]->pid = 1;

		cause->blockers = 2;
		cause->refs = 2;
		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_RUNNING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 1);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->refs, 0);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_P (event->args[1], NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);
	}

	job->service = FALSE;
	job->process[PROCESS_MAIN]->pid = 0;


	/* Check that a job with a pre-stop process can move from running
	 * to pre-stop, and have the process run.
	 */
	TEST_FEATURE ("running to pre-stop");
	job->process[PROCESS_PRE_STOP] = job_process_new (job);
	job->process[PROCESS_PRE_STOP]->command = nih_sprintf (
		job->process[PROCESS_PRE_STOP],
		"touch %s/pre-stop", dirname);

	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;
		job->process[PROCESS_MAIN]->pid = 1;
		job->process[PROCESS_PRE_STOP]->pid = 0;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_PRE_STOP);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_PRE_STOP);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 1);
		TEST_NE (job->process[PROCESS_PRE_STOP]->pid, 0);

		waitpid (job->process[PROCESS_PRE_STOP]->pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/pre-stop");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->refs, 1);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);
	}

	nih_free (job->process[PROCESS_PRE_STOP]);
	job->process[PROCESS_PRE_STOP] = NULL;
	job->process[PROCESS_MAIN]->pid = 0;


	/* Check that a job without a pre-stop process can move from
	 * running to pre-stop, skipping over that state, and instead
	 * going to the stopping state.  Because we get there, we should
	 * get a stopping event emitted.
	 */
	TEST_FEATURE ("running to pre-stop without process");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;
		job->process[PROCESS_MAIN]->pid = 1;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_PRE_STOP);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 1);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->refs, 1);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopping");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_STR (event->args[1], "ok");
		TEST_EQ_P (event->args[2], NULL);
		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 0);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);
	}

	job->process[PROCESS_MAIN]->pid = 0;


	/* Check that a job can move from running to stopping, by-passing
	 * pre-stop.  This should emit the stopping event, containing the
	 * failed information including the exit status, and block on it.
	 */
	TEST_FEATURE ("running to stopping");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 1;

		job_change_state (job, JOB_STOPPING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->refs, 1);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopping");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_STR (event->args[1], "failed");
		TEST_EQ_STR (event->args[2], "main");
		TEST_EQ_P (event->args[3], NULL);
		TEST_EQ_STR (event->env[0], "EXIT_STATUS=1");
		TEST_EQ_P (event->env[1], NULL);
		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 0);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, 1);
	}


	/* Check that a job killed by a signal can move from running to
	 * stopping, by-passing pre-stop.  This should emit the stopping
	 * event, containing the failed information including the exit
	 * signal, and block on it.
	 */
	TEST_FEATURE ("running to stopping for killed process");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = SIGSEGV << 8;

		job_change_state (job, JOB_STOPPING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->refs, 1);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopping");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_STR (event->args[1], "failed");
		TEST_EQ_STR (event->args[2], "main");
		TEST_EQ_P (event->args[3], NULL);
		TEST_EQ_STR (event->env[0], "EXIT_SIGNAL=SEGV");
		TEST_EQ_P (event->env[1], NULL);
		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 0);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, SIGSEGV << 8);
	}


	/* Check that a job killed by an unknown signal can move from
	 * running to stopping, by-passing pre-stop.  This should emit
	 * the stopping event, containing the failed information
	 * including the exit signal number, and block on it.
	 */
	TEST_FEATURE ("running to stopping for unknown signal");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 33 << 8;

		job_change_state (job, JOB_STOPPING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->refs, 1);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopping");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_STR (event->args[1], "failed");
		TEST_EQ_STR (event->args[2], "main");
		TEST_EQ_P (event->args[3], NULL);
		TEST_EQ_STR (event->env[0], "EXIT_SIGNAL=33");
		TEST_EQ_P (event->env[1], NULL);
		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 0);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, 33 << 8);
	}


	/* Check that a job can move from pre-stop back to starting again,
	 * which should only clear the cause.
	 */
	TEST_FEATURE ("pre-stop to stopping");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_PRE_STOP;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_goal (job, JOB_START, NULL);
		job_change_state (job, JOB_RUNNING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->refs, 0);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);
	}


	/* Check that a job can move from pre-stop to stopping.  This
	 * should emit the stopping event, containing the failed information,
	 * and block on it.
	 */
	TEST_FEATURE ("pre-stop to stopping");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_PRE_STOP;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_STOPPING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->refs, 1);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopping");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_STR (event->args[1], "ok");
		TEST_EQ_P (event->args[2], NULL);
		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 0);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);
	}


	/* Check that a job with an active process can move from stopping
	 * to killed, the process should be sent the TERM signal and a
	 * kill timer put in place to check up on it.
	 */
	TEST_FEATURE ("stopping to killed");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_STOPPING;
		TEST_CHILD (job->process[PROCESS_MAIN]->pid) {
			pause ();
		}
		pid = job->process[PROCESS_MAIN]->pid;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_KILLED);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_KILLED);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, pid);

		waitpid (job->process[PROCESS_MAIN]->pid, &status, 0);
		TEST_TRUE (WIFSIGNALED (status));
		TEST_EQ (WTERMSIG (status), SIGTERM);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->refs, 1);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_NE_P (job->kill_timer, NULL);

		nih_list_free (&job->kill_timer->entry);
		job->kill_timer = NULL;
	}

	job->process[PROCESS_MAIN]->pid = 0;


	/* Check that a job with no running process can move from stopping
	 * to killed, skipping over that state and ending up in post-stop
	 * instead.
	 */
	TEST_FEATURE ("stopping to killed without process");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_STOPPING;
		job->process[PROCESS_POST_STOP]->pid = 0;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_KILLED);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_POST_STOP);
		TEST_NE (job->process[PROCESS_POST_STOP]->pid, 0);

		waitpid (job->process[PROCESS_POST_STOP]->pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/stop");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->refs, 1);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_EQ_P (job->kill_timer, NULL);
	}

	job->process[PROCESS_POST_STOP]->pid = 0;


	/* Check that a job with a stop process can move from killed
	 * to post-stop, and have the process run.
	 */
	TEST_FEATURE ("killed to post-stop");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_KILLED;
		job->process[PROCESS_POST_STOP]->pid = 0;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_POST_STOP);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_POST_STOP);
		TEST_NE (job->process[PROCESS_POST_STOP]->pid, 0);

		waitpid (job->process[PROCESS_POST_STOP]->pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/stop");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->refs, 1);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);
	}


	/* Check that a job without a stop process can move from killed
	 * to post-stop, skipping over that state, and instead going all
	 * the way through to the waiting state.  Because we get there,
	 * we should get a stopped event emitted, and the cause forgotten.
	 */
	TEST_FEATURE ("killed to post-stop without process");
	tmp = job->process[PROCESS_POST_STOP];
	job->process[PROCESS_POST_STOP] = NULL;

	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_KILLED;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 1;

		job_change_state (job, JOB_POST_STOP);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->refs, 0);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopped");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_STR (event->args[1], "failed");
		TEST_EQ_STR (event->args[2], "main");
		TEST_EQ_P (event->args[3], NULL);
		TEST_EQ_STR (event->env[0], "EXIT_STATUS=1");
		TEST_EQ_P (event->env[1], NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, 1);
	}

	job->process[PROCESS_POST_STOP] = tmp;


	/* Check that a job can move from post-stop to waiting.  This
	 * should emit the stopped event and clear the cause, but not the
	 * failed information.
	 */
	TEST_FEATURE ("post-stop to waiting");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 1;

		job_change_state (job, JOB_WAITING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->refs, 0);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopped");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_STR (event->args[1], "failed");
		TEST_EQ_STR (event->args[2], "main");
		TEST_EQ_P (event->args[3], NULL);
		TEST_EQ_STR (event->env[0], "EXIT_STATUS=1");
		TEST_EQ_P (event->env[1], NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, 1);
	}


	/* Check that a job can move from post-stop to starting.  This
	 * should emit the starting event and block on it, as well as clear
	 * any failed state information; but not the cause.
	 */
	TEST_FEATURE ("post-stop to starting");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_POST_STOP;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 1;

		job_change_state (job, JOB_STARTING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STARTING);

		TEST_EQ_P (job->cause, cause);
		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->refs, 1);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "starting");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_P (event->args[1], NULL);
		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 0);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);
	}


	/* Check that if a job tries to move from post-stop to starting too
	 * many times in a given period then it is caught, a message is
	 * output and the job sent back to stopped and waiting.  We get
	 * a stopped event, and the cause is cleared.
	 */
	TEST_FEATURE ("post-stop to starting too fast");
	job->respawn_limit = 10;
	job->respawn_interval = 1000;

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_POST_STOP;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 1;

		job->respawn_time = time (NULL);
		job->respawn_count = 10;

		TEST_DIVERT_STDERR (output) {
			job_change_state (job, JOB_STARTING);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->refs, 0);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopped");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_STR (event->args[1], "failed");
		TEST_EQ_STR (event->args[2], "main");
		TEST_EQ_P (event->args[3], NULL);
		TEST_EQ_STR (event->env[0], "EXIT_STATUS=1");
		TEST_EQ_P (event->env[1], NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, 1);

		TEST_FILE_EQ (output,
			      "test: test respawning too fast, stopped\n");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	job->respawn_limit = 0;
	job->respawn_interval = JOB_DEFAULT_RESPAWN_INTERVAL;
	job->respawn_time = 0;
	job->respawn_count = 0;


	/* Check that an instance job can move from post-stop to waiting,
	 * going through that state and ending up in deleted.
	 */
	TEST_FEATURE ("post-stop to waiting for instance");
	job->instance_of = job_new (NULL, "wibble");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 1;

		job_change_state (job, JOB_WAITING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_DELETED);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->refs, 0);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopped");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_STR (event->args[1], "failed");
		TEST_EQ_STR (event->args[2], "main");
		TEST_EQ_P (event->args[3], NULL);
		TEST_EQ_STR (event->env[0], "EXIT_STATUS=1");
		TEST_EQ_P (event->env[1], NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, 1);
	}

	nih_list_free (&job->instance_of->entry);
	job->instance_of = NULL;


	/* Check that a job with a replacement can move from post-stop to
	 * waiting, going through that state and ending up in deleted where
	 * the replacement takes over and becomes a real job.
	 */
	TEST_FEATURE ("post-stop to waiting for replaced job");
	job->replacement = job_new (NULL, "wibble");
	job->replacement->replacement_for = job;
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 1;

		job_change_state (job, JOB_WAITING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_DELETED);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->refs, 0);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopped");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_STR (event->args[1], "failed");
		TEST_EQ_STR (event->args[2], "main");
		TEST_EQ_P (event->args[3], NULL);
		TEST_EQ_STR (event->env[0], "EXIT_STATUS=1");
		TEST_EQ_P (event->env[1], NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, 1);

		TEST_EQ_P (job->replacement->replacement_for, NULL);
	}

	nih_list_free (&job->replacement->entry);
	job->replacement = NULL;


	/* Check that an instance of a replaced job can move from post-stop
	 * to deleted via waiting, and cause its own instance parent to
	 * become deleted and replaced as well.
	 */
	TEST_FEATURE ("post-stop to waiting for instance of replaced job");
	job->instance_of = job_new (NULL, "wibble");
	job->instance_of->replacement = job_new (NULL, "wibble");
	job->instance_of->replacement->replacement_for = job->instance_of;
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;

		job->cause = cause;
		job->blocked = NULL;
		cause->refs = 1;
		cause->blockers = 1;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_WAITING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_DELETED);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->refs, 0);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopped");
		TEST_EQ_STR (event->args[0], "test");
		TEST_EQ_STR (event->args[1], "ok");
		TEST_EQ_P (event->args[2], NULL);
		TEST_EQ_P (event->env, NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_list_free (&event->entry);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_EQ (job->instance_of->goal, JOB_STOP);
		TEST_EQ (job->instance_of->state, JOB_DELETED);

		TEST_EQ (job->instance_of->replacement->replacement_for, NULL);
	}

	nih_list_free (&job->instance_of->replacement->entry);
	nih_list_free (&job->instance_of->entry);
	job->instance_of = NULL;


	fclose (output);
	rmdir (dirname);

	nih_list_free (&job->entry);

	nih_list_free (&cause->entry);
	event_poll ();
}

void
test_next_state (void)
{
	Job *job;

	TEST_FUNCTION ("job_next_state");
	job = job_new (NULL, "test");
	job->process[PROCESS_MAIN] = job_process_new (job);
	job->process[PROCESS_MAIN]->command = "echo";

	/* Check that the next state if we're stopping a waiting job is
	 * deleted.  The only place this can happen is from the job loop
	 * for jobs that should be deleted or replaced; so this is the
	 * logical next state.
	 */
	TEST_FEATURE ("with waiting job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;

	TEST_EQ (job_next_state (job), JOB_DELETED);


	/* Check that the next state if we're starting a waiting job is
	 * starting.
	 */
	TEST_FEATURE ("with waiting job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_WAITING;

	TEST_EQ (job_next_state (job), JOB_STARTING);


	/* Check that the next state if we're stopping a starting job is
	 * waiting.
	 */
	TEST_FEATURE ("with starting job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_STARTING;

	TEST_EQ (job_next_state (job), JOB_WAITING);


	/* Check that the next state if we're starting a starting job is
	 * pre-start.
	 */
	TEST_FEATURE ("with starting job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_STARTING;

	TEST_EQ (job_next_state (job), JOB_PRE_START);


	/* Check that the next state if we're stopping a pre-start job is
	 * stopping.
	 */
	TEST_FEATURE ("with pre-start job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_PRE_START;

	TEST_EQ (job_next_state (job), JOB_STOPPING);


	/* Check that the next state if we're starting a pre-start job is
	 * spawned.
	 */
	TEST_FEATURE ("with pre-start job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_PRE_START;

	TEST_EQ (job_next_state (job), JOB_SPAWNED);


	/* Check that the next state if we're stopping a spawned job is
	 * stopping.
	 */
	TEST_FEATURE ("with spawned job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_SPAWNED;

	TEST_EQ (job_next_state (job), JOB_STOPPING);


	/* Check that the next state if we're starting a spawned job is
	 * post-start.
	 */
	TEST_FEATURE ("with spawned job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_SPAWNED;

	TEST_EQ (job_next_state (job), JOB_POST_START);


	/* Check that the next state if we're stopping a post-start job is
	 * stopping.
	 */
	TEST_FEATURE ("with post-start job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_POST_START;

	TEST_EQ (job_next_state (job), JOB_STOPPING);


	/* Check that the next state if we're starting a post-start job is
	 * running.
	 */
	TEST_FEATURE ("with post-start job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_POST_START;

	TEST_EQ (job_next_state (job), JOB_RUNNING);


	/* Check that the next state if we're stopping a running job is
	 * pre-stop.  This is the "normal" stop process, as called from the
	 * goal change event.
	 */
	TEST_FEATURE ("with running job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_RUNNING;
	job->process[PROCESS_MAIN]->pid = 1;

	TEST_EQ (job_next_state (job), JOB_PRE_STOP);


	/* Check that the next state if we're stopping a running job that
	 * has no process is stopping.  This is the stop process if the
	 * process goes away on its own, as called from the child reaper.
	 */
	TEST_FEATURE ("with dead running job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_RUNNING;
	job->process[PROCESS_MAIN]->pid = 0;

	TEST_EQ (job_next_state (job), JOB_STOPPING);


	/* Check that the next state if we're starting a running job is
	 * stopping.  This assumes that the job has exited, but we didn't
	 * change the goal, so it should be respawned.
	 */
	TEST_FEATURE ("with running job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;

	TEST_EQ (job_next_state (job), JOB_STOPPING);


	/* Check that the next state if we're starting a pre-stop job is
	 * running.  This assumes that the pre-stop job decided that the
	 * job should not stop.
	 */
	TEST_FEATURE ("with pre-stop job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_PRE_STOP;

	TEST_EQ (job_next_state (job), JOB_RUNNING);


	/* Check that the next state if we're stopping a pre-stop job is
	 * stopping.
	 */
	TEST_FEATURE ("with pre-stop job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_PRE_STOP;

	TEST_EQ (job_next_state (job), JOB_STOPPING);


	/* Check that the next state if we're starting a stopping job is
	 * killed.  This is because we need to clean up before we can start
	 * again.
	 */
	TEST_FEATURE ("with stopping job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_STOPPING;

	TEST_EQ (job_next_state (job), JOB_KILLED);


	/* Check that the next state if we're stopping a stopping job is
	 * killed.
	 */
	TEST_FEATURE ("with stopping job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;

	TEST_EQ (job_next_state (job), JOB_KILLED);


	/* Check that the next state if we're starting a killed job is
	 * post-stop.  This is because we need to clean up before we can
	 * start again.
	 */
	TEST_FEATURE ("with killed job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_KILLED;

	TEST_EQ (job_next_state (job), JOB_POST_STOP);


	/* Check that the next state if we're stopping a killed job is
	 * post-stop.
	 */
	TEST_FEATURE ("with killed job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_KILLED;

	TEST_EQ (job_next_state (job), JOB_POST_STOP);


	/* Check that the next state if we're starting a post-stop job is
	 * starting.
	 */
	TEST_FEATURE ("with post-stop job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_POST_STOP;

	TEST_EQ (job_next_state (job), JOB_STARTING);


	/* Check that the next state if we're stopping a post-stop job is
	 * waiting.
	 */
	TEST_FEATURE ("with post-stop job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_POST_STOP;

	TEST_EQ (job_next_state (job), JOB_WAITING);


	nih_list_free (&job->entry);
}


void
test_should_replace (void)
{
	Job *job1, *job2, *job3;
	int  replace;

	TEST_FUNCTION ("job_should_replace");
	job1 = job_new (NULL, "foo");
	job1->goal = JOB_STOP;
	job1->state = JOB_WAITING;

	job2 = job_new (NULL, "bar");
	job2->goal = JOB_START;
	job2->state = JOB_RUNNING;
	job2->replacement = job1;

	job3 = job_new (NULL, "bar");
	job3->goal = JOB_START;
	job3->state = JOB_RUNNING;
	job3->instance_of = job2;


	/* Check that we should not replace a job that doesn't have a
	 * replacement at this point.
	 */
	TEST_FEATURE ("with replacement-less job");
	replace = job_should_replace (job1);

	TEST_FALSE (replace);


	/* Check that we should not replace a job that has a replacement,
	 * but is still running.
	 */
	TEST_FEATURE ("with running job");
	replace = job_should_replace (job2);

	TEST_FALSE (replace);


	/* Check that we should replace a job that has a replacement and
	 * is no longer running.
	 */
	TEST_FEATURE ("with stopped job");
	job2->goal = JOB_STOP;
	job2->state = JOB_WAITING;

	replace = job_should_replace (job2);

	TEST_TRUE (replace);


	/* Check that we should not replace a job that has current instances.
	 */
	TEST_FEATURE ("with job that has instances");
	job2->instance = TRUE;

	replace = job_should_replace (job2);

	TEST_FALSE (replace);


	/* Check that we should replace a job that has no current instances.
	 */
	TEST_FEATURE ("with instance-less job");
	job3->instance_of = NULL;

	replace = job_should_replace (job2);

	TEST_TRUE (replace);


	nih_list_free (&job3->entry);
	nih_list_free (&job2->entry);
	nih_list_free (&job1->entry);
}


void
test_run_process (void)
{
	Job           *job = NULL;
	Event         *event;
	FILE          *output;
	struct stat    statbuf;
	char           filename[PATH_MAX], buf[80], **args;
	int            status, first;

	TEST_FUNCTION ("job_run_process");
	TEST_FILENAME (filename);

	/* Check that we can run a simple command, and have the process id
	 * and state filled in.  We should be able to wait for the pid to
	 * finish and see that it has been run as expected.
	 */
	TEST_FEATURE ("with simple command");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (NULL, "test");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
			job->process[PROCESS_MAIN] = job_process_new (job);
			job->process[PROCESS_MAIN]->command = nih_sprintf (
				job->process[PROCESS_MAIN],
				"touch %s", filename);
		}

		job_run_process (job, PROCESS_MAIN);

		TEST_NE (job->process[PROCESS_MAIN]->pid, 0);

		waitpid (job->process[PROCESS_MAIN]->pid, NULL, 0);
		TEST_EQ (stat (filename, &statbuf), 0);

		unlink (filename);
		nih_list_free (&job->entry);
	}


	/* Check that we can run a command that requires a shell to be
	 * intepreted correctly, a shell should automatically be used to
	 * make this work.  Check the contents of a file we'll create to
	 * check that a shell really was used.
	 */
	TEST_FEATURE ("with shell command");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (NULL, "test");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
			job->process[PROCESS_MAIN] = job_process_new (job);
			job->process[PROCESS_MAIN]->command = nih_sprintf (
				job->process[PROCESS_MAIN],
				"echo $$ > %s", filename);
		}

		job_run_process (job, PROCESS_MAIN);

		TEST_NE (job->process[PROCESS_MAIN]->pid, 0);

		waitpid (job->process[PROCESS_MAIN]->pid, NULL, 0);
		TEST_EQ (stat (filename, &statbuf), 0);

		/* Filename should contain the pid */
		output = fopen (filename, "r");
		sprintf (buf, "%d\n", job->process[PROCESS_MAIN]->pid);
		TEST_FILE_EQ (output, buf);
		TEST_FILE_END (output);
		fclose (output);
		unlink (filename);

		nih_list_free (&job->entry);
	}

	/* Check that we can run a small shell script, and that it's run
	 * by using the shell directly and passing the script in on the
	 * command-line.
	 */
	TEST_FEATURE ("with small script");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (NULL, "test");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
			job->process[PROCESS_MAIN] = job_process_new (job);
			job->process[PROCESS_MAIN]->script = TRUE;
			job->process[PROCESS_MAIN]->command = nih_sprintf (
				job->process[PROCESS_MAIN],
				"exec > %s\necho $0\necho $@", filename);
		}

		job_run_process (job, PROCESS_MAIN);

		TEST_NE (job->process[PROCESS_MAIN]->pid, 0);

		waitpid (job->process[PROCESS_MAIN]->pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		output = fopen (filename, "r");
		TEST_FILE_EQ (output, "/bin/sh\n");
		TEST_FILE_EQ (output, "\n");
		TEST_FILE_END (output);
		fclose (output);
		unlink (filename);

		nih_list_free (&job->entry);
	}


	/* Check that shell scripts are run with the -e option set, so that
	 * any failing command causes the entire script to fail.
	 */
	TEST_FEATURE ("with script that will fail");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (NULL, "test");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
			job->process[PROCESS_MAIN] = job_process_new (job);
			job->process[PROCESS_MAIN]->script = TRUE;
			job->process[PROCESS_MAIN]->command = nih_sprintf (
				job->process[PROCESS_MAIN],
				"exec > %s\ntest -d %s\necho oops",
				filename, filename);
		}

		job_run_process (job, PROCESS_MAIN);

		TEST_NE (job->process[PROCESS_MAIN]->pid, 0);

		waitpid (job->process[PROCESS_MAIN]->pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 1);

		output = fopen (filename, "r");
		TEST_FILE_END (output);
		fclose (output);
		unlink (filename);

		nih_list_free (&job->entry);
	}


	/* Check that a small shell script will have arguments from the
	 * cause passed to it, if one exists.
	 */
	TEST_FEATURE ("with small script and cause");
	args = nih_str_array_new (NULL);
	NIH_MUST (nih_str_array_add (&args, NULL, NULL, "foo"));
	NIH_MUST (nih_str_array_add (&args, NULL, NULL, "bar"));
	event = event_new (NULL, "test", args, NULL);

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (NULL, "test");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
			job->cause = event;
			job->process[PROCESS_MAIN] = job_process_new (job);
			job->process[PROCESS_MAIN]->script = TRUE;
			job->process[PROCESS_MAIN]->command = nih_sprintf (
				job->process[PROCESS_MAIN],
				"exec > %s\necho $0\necho $@", filename);
		}

		job_run_process (job, PROCESS_MAIN);

		TEST_NE (job->process[PROCESS_MAIN]->pid, 0);

		waitpid (job->process[PROCESS_MAIN]->pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		output = fopen (filename, "r");
		TEST_FILE_EQ (output, "/bin/sh\n");
		TEST_FILE_EQ (output, "foo bar\n");
		TEST_FILE_END (output);
		fclose (output);
		unlink (filename);

		nih_list_free (&job->entry);
	}

	nih_list_free (&event->entry);


	/* Check that an event without arguments doesn't cause any problems */
	TEST_FEATURE ("with cause without arguments");
	event = event_new (NULL, "test", NULL, NULL);

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (NULL, "test");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
			job->cause = event;
			job->process[PROCESS_MAIN] = job_process_new (job);
			job->process[PROCESS_MAIN]->script = TRUE;
			job->process[PROCESS_MAIN]->command = nih_sprintf (
				job->process[PROCESS_MAIN],
				"exec > %s\necho $0\necho $@", filename);
		}

		job_run_process (job, PROCESS_MAIN);

		TEST_NE (job->process[PROCESS_MAIN]->pid, 0);

		waitpid (job->process[PROCESS_MAIN]->pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		output = fopen (filename, "r");
		TEST_FILE_EQ (output, "/bin/sh\n");
		TEST_FILE_EQ (output, "\n");
		TEST_FILE_END (output);
		fclose (output);
		unlink (filename);

		nih_list_free (&job->entry);
	}

	nih_list_free (&event->entry);


	if (stat ("/dev/fd", &statbuf) < 0) {
		printf ("SKIP: no /dev/fd\n");
		goto no_devfd;
	}

	/* Check that a particularly long script is instead invoked by
	 * using the /dev/fd feature, with the shell script fed to the
	 * child process by an NihIo structure.
	 */
	TEST_FEATURE ("with long script");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (NULL, "test");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
			job->process[PROCESS_MAIN] = job_process_new (job);
			job->process[PROCESS_MAIN]->script = TRUE;
			job->process[PROCESS_MAIN]->command = nih_alloc (
				job->process[PROCESS_MAIN], 4096);
			sprintf (job->process[PROCESS_MAIN]->command,
				 "exec > %s\necho $0\necho $@\n", filename);
			while (strlen (job->process[PROCESS_MAIN]->command) < 4000)
				strcat (job->process[PROCESS_MAIN]->command,
					"# this just bulks it out a bit");
		}

		job_run_process (job, PROCESS_MAIN);

		TEST_NE (job->process[PROCESS_MAIN]->pid, 0);

		/* Loop until we've fed all of the data. */
		first = TRUE;
		for (;;) {
			fd_set readfds, writefds, exceptfds;
			int    nfds;

			nfds = 0;
			FD_ZERO (&readfds);
			FD_ZERO (&writefds);
			FD_ZERO (&exceptfds);

			nih_io_select_fds (&nfds, &readfds,
					   &writefds, &exceptfds);
			if (! nfds) {
				if (first)
					TEST_FAILED ("expected to have "
						     "data to feed.");
				break;
			}
			first = FALSE;

			select (nfds, &readfds, &writefds, &exceptfds, NULL);

			nih_io_handle_fds (&readfds, &writefds, &exceptfds);
		}

		waitpid (job->process[PROCESS_MAIN]->pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		output = fopen (filename, "r");
		TEST_FILE_EQ_N (output, "/dev/fd/");
		TEST_FILE_EQ (output, "\n");
		TEST_FILE_END (output);
		fclose (output);
		unlink (filename);

		nih_list_free (&job->entry);
	}


	/* Check that a long shell script will have arguments from the
	 * cause passed to it, if one exists.
	 */
	TEST_FEATURE ("with long script and cause");
	args = nih_str_array_new (NULL);
	NIH_MUST (nih_str_array_add (&args, NULL, NULL, "foo"));
	NIH_MUST (nih_str_array_add (&args, NULL, NULL, "bar"));
	event = event_new (NULL, "test", args, NULL);

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (NULL, "test");
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
			job->cause = event;
			job->process[PROCESS_MAIN] = job_process_new (job);
			job->process[PROCESS_MAIN]->script = TRUE;
			job->process[PROCESS_MAIN]->command = nih_alloc (
				job->process[PROCESS_MAIN], 4096);
			sprintf (job->process[PROCESS_MAIN]->command,
				 "exec > %s\necho $0\necho $@\n", filename);
			while (strlen (job->process[PROCESS_MAIN]->command) < 4000)
				strcat (job->process[PROCESS_MAIN]->command,
					"# this just bulks it out a bit");
		}

		job_run_process (job, PROCESS_MAIN);

		TEST_NE (job->process[PROCESS_MAIN]->pid, 0);

		/* Loop until we've fed all of the data. */
		first = TRUE;
		for (;;) {
			fd_set readfds, writefds, exceptfds;
			int    nfds;

			nfds = 0;
			FD_ZERO (&readfds);
			FD_ZERO (&writefds);
			FD_ZERO (&exceptfds);

			nih_io_select_fds (&nfds, &readfds,
					   &writefds, &exceptfds);
			if (! nfds) {
				if (first)
					TEST_FAILED ("expected to have "
						     "data to feed.");
				break;
			}
			first = FALSE;

			select (nfds, &readfds, &writefds, &exceptfds, NULL);

			nih_io_handle_fds (&readfds, &writefds, &exceptfds);
		}

		waitpid (job->process[PROCESS_MAIN]->pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		output = fopen (filename, "r");
		TEST_FILE_EQ_N (output, "/dev/fd/");
		TEST_FILE_EQ (output, "foo bar\n");
		TEST_FILE_END (output);
		fclose (output);
		unlink (filename);

		nih_list_free (&job->entry);
	}

	nih_list_free (&event->entry);
no_devfd:
	;
}


void
test_kill_process (void)
{
	Job      *job;
	NihTimer *timer;
	pid_t     pid;
	int       status;

	TEST_FUNCTION ("job_kill_process");
	job = job_new (NULL, "test");
	job->kill_timeout = 1000;
	job->respawn_limit = 0;

	job->process[PROCESS_MAIN] = job_process_new (job);
	job->process[PROCESS_MAIN]->command = nih_strdup (
		job->process[PROCESS_MAIN], "echo");


	/* Check that an easily killed process goes away with just a single
	 * call to job_kill_process, having received the TERM signal.
	 * A kill timer should be set to handle the case where the child
	 * doesn't get reaped.
	 */
	TEST_FEATURE ("with easily killed process");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_KILLED;
		TEST_CHILD (job->process[PROCESS_MAIN]->pid) {
			pause ();
		}
		pid = job->process[PROCESS_MAIN]->pid;

		job_kill_process (job, PROCESS_MAIN);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_KILLED);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, pid);

		waitpid (job->process[PROCESS_MAIN]->pid, &status, 0);
		TEST_TRUE (WIFSIGNALED (status));
		TEST_EQ (WTERMSIG (status), SIGTERM);

		TEST_NE_P (job->kill_timer, NULL);
		TEST_ALLOC_SIZE (job->kill_timer, sizeof (NihTimer));
		TEST_ALLOC_PARENT (job->kill_timer, job);
		TEST_GE (job->kill_timer->due, time (NULL) + 950);
		TEST_LE (job->kill_timer->due, time (NULL) + 1000);

		nih_free (job->kill_timer);
		job->kill_timer = NULL;

		event_poll ();
	}


	/* Check that a process that's hard to kill doesn't go away, but
	 * that the kill timer sends the KILL signal which should finally
	 * get rid of it.
	 */
	TEST_FEATURE ("with hard to kill process");
	TEST_ALLOC_FAIL {
		int wait_fd = 0;

		job->goal = JOB_STOP;
		job->state = JOB_KILLED;
		TEST_CHILD_WAIT (job->process[PROCESS_MAIN]->pid, wait_fd) {
			struct sigaction act;

			act.sa_handler = SIG_IGN;
			act.sa_flags = 0;
			sigemptyset (&act.sa_mask);
			sigaction (SIGTERM, &act, NULL);

			TEST_CHILD_RELEASE (wait_fd);

			for (;;)
				pause ();
		}
		pid = job->process[PROCESS_MAIN]->pid;

		job_kill_process (job, PROCESS_MAIN);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_KILLED);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, pid);

		TEST_EQ (kill (job->process[PROCESS_MAIN]->pid, 0), 0);

		TEST_NE_P (job->kill_timer, NULL);
		TEST_ALLOC_SIZE (job->kill_timer, sizeof (NihTimer));
		TEST_ALLOC_PARENT (job->kill_timer, job);
		TEST_GE (job->kill_timer->due, time (NULL) + 950);
		TEST_LE (job->kill_timer->due, time (NULL) + 1000);

		/* Run the kill timer */
		timer = job->kill_timer;
		timer->callback (timer->data, timer);
		nih_free (timer);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_KILLED);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, pid);

		waitpid (job->process[PROCESS_MAIN]->pid, &status, 0);
		TEST_TRUE (WIFSIGNALED (status));
		TEST_EQ (WTERMSIG (status), SIGKILL);

		TEST_EQ_P (job->kill_timer, NULL);

		event_poll ();
	}

	nih_list_free (&job->entry);
}


static int destructor_called = 0;

static int
my_destructor (void *ptr)
{
	destructor_called++;
	return 0;
}

void
test_child_reaper (void)
{
	Job   *job;
	Event *event;
	FILE  *output;
	int    exitcodes[2] = { 100, SIGINT << 8 };

	TEST_FUNCTION ("job_child_reaper");
	program_name = "test";
	output = tmpfile ();

	job = job_new (NULL, "test");
	job->process[PROCESS_MAIN] = job_process_new (job);
	job->process[PROCESS_MAIN]->command = "echo";

	event = event_new (NULL, "foo", NULL, NULL);


	/* Check that the child reaper can be called with a pid that doesn't
	 * match the job, and that the job state doesn't change.
	 */
	TEST_FEATURE ("with unknown pid");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->process[PROCESS_MAIN]->pid = 1;
		job->blocked = NULL;

		job_child_reaper (NULL, 999, FALSE, 0);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 1);

		TEST_EQ_P (job->blocked, NULL);
	}

	job->process[PROCESS_MAIN]->pid = 0;


	/* Check that we can reap the running task of the job, which should
	 * set the goal to stop and transition a state change into the
	 * stopping state.  This should not be considered a failure.
	 */
	TEST_FEATURE ("with running process");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->process[PROCESS_MAIN]->pid = 1;

		job->cause = event;
		event->failed = FALSE;
		event->refs = 1;
		event->blockers = 1;

		job->blocked = NULL;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_child_reaper (NULL, 1, FALSE, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 0);

		TEST_EQ_P (job->cause, event);
		TEST_EQ (event->failed, FALSE);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->refs, 1);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);
	}

	job->process[PROCESS_MAIN]->pid = 0;


	/* Check that we can reap a running task of the job after it's been
	 * sent the TERM signal and a kill timer set.  The kill timer should
	 * be cancelled and freed, and since we killed it, the job should
	 * still not be considered failed.
	 */
	TEST_FEATURE ("with kill timer");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_KILLED;
		job->process[PROCESS_MAIN]->pid = 1;

		job->cause = event;
		event->failed = FALSE;
		event->refs = 1;
		event->blockers = 1;

		job->blocked = NULL;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_ALLOC_SAFE {
			destructor_called = 0;
			job->kill_timer = (void *) nih_strdup (job, "test");
			nih_alloc_set_destructor (job->kill_timer,
						  my_destructor);
		}

		job_child_reaper (NULL, 1, FALSE, 0);

		TEST_TRUE (destructor_called);
		TEST_EQ_P (job->kill_timer, NULL);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 0);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ (event->failed, FALSE);
		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->refs, 0);

		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);
	}

	job->process[PROCESS_MAIN]->pid = 0;


	/* Check that we can reap the pre-start process of the job, and if it
	 * terminates with a good error code, end up in the running state.
	 */
	TEST_FEATURE ("with pre-start process");
	job->process[PROCESS_PRE_START] = job_process_new (job);
	job->process[PROCESS_PRE_START]->command = "echo";

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->process[PROCESS_MAIN]->pid = 0;
		job->process[PROCESS_PRE_START]->pid = 1;

		job->cause = event;
		event->failed = FALSE;
		event->refs = 1;
		event->blockers = 1;

		job->blocked = NULL;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_child_reaper (NULL, 1, FALSE, 0);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->process[PROCESS_PRE_START]->pid, 0);
		TEST_GT (job->process[PROCESS_MAIN]->pid, 0);

		waitpid (job->process[PROCESS_MAIN]->pid, NULL, 0);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_EQ_P (job->cause, event);
		TEST_EQ (event->failed, FALSE);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->refs, 1);

		TEST_EQ_P (job->blocked, NULL);
	}

	job->process[PROCESS_PRE_START]->pid = 0;
	job->process[PROCESS_MAIN]->pid = 0;


	/* Check that we can reap a failing pre-start process of the job, which
	 * changes the goal to stop and transitions a state change in that
	 * direction to the stopping state.  An error should be emitted
	 * and the job and event should be marked as failed.
	 */
	TEST_FEATURE ("with failed pre-start process");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->process[PROCESS_PRE_START]->pid = 1;

		job->cause = event;
		event->failed = FALSE;
		event->refs = 1;
		event->blockers = 1;

		job->blocked = NULL;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, FALSE, 1);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process[PROCESS_PRE_START]->pid, 0);

		TEST_EQ_P (job->cause, event);
		TEST_EQ (event->failed, TRUE);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->refs, 1);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_PRE_START);
		TEST_EQ (job->exit_status, 1);

		TEST_FILE_EQ (output, ("test: test pre-start process (1) "
				       "terminated with status 1\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	job->process[PROCESS_PRE_START]->pid = 0;


	/* Check that we can reap a killed starting task, which should
	 * act as if it failed.  A different error should be output and
	 * the failed exit status should contain the signal and the high bit.
	 */
	TEST_FEATURE ("with killed pre-start process");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->process[PROCESS_PRE_START]->pid = 1;

		job->cause = event;
		event->failed = FALSE;
		event->refs = 1;
		event->blockers = 1;

		job->blocked = NULL;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, TRUE, SIGTERM);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process[PROCESS_PRE_START]->pid, 0);

		TEST_EQ_P (job->cause, event);
		TEST_EQ (event->failed, TRUE);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->refs, 1);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_PRE_START);
		TEST_EQ (job->exit_status, SIGTERM << 8);

		TEST_FILE_EQ (output, ("test: test pre-start process (1) "
				       "killed by TERM signal\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	job->process[PROCESS_PRE_START]->pid = 0;


	/* Check that we can catch the running task failing, and if the job
	 * is to be respawned, go into the stopping state but don't change
	 * the goal to stop.
	 *
	 * This should also emit a warning, but should not set the failed
	 * state since we're dealing with it.
	 */
	TEST_FEATURE ("with respawn of running process");
	job->respawn = TRUE;

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->process[PROCESS_MAIN]->pid = 1;

		job->cause = event;
		event->failed = FALSE;
		event->refs = 1;
		event->blockers = 1;

		job->blocked = NULL;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, FALSE, 0);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 0);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_EQ_P (job->cause, event);
		TEST_EQ (event->failed, FALSE);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->refs, 1);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_FILE_EQ (output,
			      "test: test main process ended, respawning\n");
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	job->respawn = FALSE;
	job->process[PROCESS_MAIN]->pid = 0;


	/* Check that we can catch a running task exiting with a "normal"
	 * exit code, and even if it's marked respawn, set the goal to
	 * stop and transition into the stopping state.
	 */
	TEST_FEATURE ("with normal exit of running respawn process");
	job->respawn = TRUE;
	job->normalexit = exitcodes;
	job->normalexit_len = 1;

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->process[PROCESS_MAIN]->pid = 1;

		job->cause = event;
		event->failed = FALSE;
		event->refs = 1;
		event->blockers = 1;

		job->blocked = NULL;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, FALSE, 100);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 0);

		TEST_EQ_P (job->cause, event);
		TEST_EQ (event->failed, FALSE);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->refs, 1);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test main process (1) "
				       "terminated with status 100\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	job->respawn = FALSE;
	job->normalexit = NULL;
	job->normalexit_len = 0;
	job->process[PROCESS_MAIN]->pid = 0;


	/* Check that a running task that fails with an exit status not
	 * listed in normalexit causes the job to be marked as failed.
	 */
	TEST_FEATURE ("with abnormal exit of running process");
	job->normalexit = exitcodes;
	job->normalexit_len = 2;

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->process[PROCESS_MAIN]->pid = 1;

		job->cause = event;
		event->failed = FALSE;
		event->refs = 1;
		event->blockers = 1;

		job->blocked = NULL;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, FALSE, 99);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 0);

		TEST_EQ_P (job->cause, event);
		TEST_EQ (event->failed, TRUE);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->refs, 1);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, 99);

		TEST_FILE_EQ (output, ("test: test main process (1) "
				       "terminated with status 99\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	job->normalexit = NULL;
	job->normalexit_len = 0;
	job->process[PROCESS_MAIN]->pid = 0;


	/* Check that a running task that fails doesn't mark the job or
	 * event as failed if the goal was already to stop the job (since
	 * it's probably failed because of the TERM or KILL signal).
	 */
	TEST_FEATURE ("with killed running process");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_KILLED;
		job->process[PROCESS_MAIN]->pid = 1;

		job->cause = event;
		event->failed = FALSE;
		event->refs = 1;
		event->blockers = 1;

		job->blocked = NULL;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, TRUE, SIGTERM);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 0);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ (event->failed, FALSE);
		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->refs, 0);

		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test main process (1) killed "
				       "by TERM signal\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	job->process[PROCESS_MAIN]->pid = 0;


	/* Check that a running task that fails with an exit status
	 * listed in normalexit does not cause the job to be marked as
	 * failed, but instead just stops it normally.
	 */
	TEST_FEATURE ("with normal exit of running process");
	job->normalexit = exitcodes;
	job->normalexit_len = 2;

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->process[PROCESS_MAIN]->pid = 1;

		job->cause = event;
		event->failed = FALSE;
		event->refs = 1;
		event->blockers = 1;

		job->blocked = NULL;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, FALSE, 100);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 0);

		TEST_EQ_P (job->cause, event);
		TEST_EQ (event->failed, FALSE);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->refs, 1);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test main process (1) "
				       "terminated with status 100\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	job->normalexit = NULL;
	job->normalexit_len = 0;
	job->process[PROCESS_MAIN]->pid = 0;


	/* Check that a running task that fails with an signal
	 * listed in normalexit does not cause the job to be marked as
	 * failed, but instead just stops it normally.
	 */
	TEST_FEATURE ("with normal signal killed running process");
	job->normalexit = exitcodes;
	job->normalexit_len = 2;

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->process[PROCESS_MAIN]->pid = 1;

		job->cause = event;
		event->failed = FALSE;
		event->refs = 1;
		event->blockers = 1;

		job->blocked = NULL;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, TRUE, SIGINT);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 0);

		TEST_EQ_P (job->cause, event);
		TEST_EQ (event->failed, FALSE);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->refs, 1);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test main process (1) killed "
				       "by INT signal\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	job->normalexit = NULL;
	job->normalexit_len = 0;
	job->process[PROCESS_MAIN]->pid = 0;


	/* A running task exiting with the zero exit code is considered
	 * a normal termination if not marked respawn.
	 */
	TEST_FEATURE ("with running task and zero exit");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->process[PROCESS_MAIN]->pid = 1;

		job->cause = event;
		event->failed = FALSE;
		event->refs = 1;
		event->blockers = 1;

		job->blocked = NULL;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_child_reaper (NULL, 1, FALSE, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 0);

		TEST_EQ_P (job->cause, event);
		TEST_EQ (event->failed, FALSE);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->refs, 1);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);
	}

	job->process[PROCESS_MAIN]->pid = 0;


	/* Check that we can reap the post-stop process of the job, and end up
	 * in the waiting state.
	 */
	TEST_FEATURE ("with post-stop process");
	job->process[PROCESS_POST_STOP] = job_process_new (job);
	job->process[PROCESS_POST_STOP]->command = "echo";

	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;
		job->process[PROCESS_POST_STOP]->pid = 1;

		job->cause = event;
		event->failed = FALSE;
		event->refs = 1;
		event->blockers = 1;

		job->blocked = NULL;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_child_reaper (NULL, 1, FALSE, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ (job->process[PROCESS_POST_STOP]->pid, 0);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ (event->failed, FALSE);
		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->refs, 0);

		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);
	}

	job->process[PROCESS_POST_STOP]->pid = 0;


	/* Check that we can reap a failing post-stop process of the job, which
	 * should get marked as failed if the job hasn't been already.
	 */
	TEST_FEATURE ("with failed post-stop process");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;
		job->process[PROCESS_POST_STOP]->pid = 1;

		job->cause = event;
		event->failed = FALSE;
		event->refs = 1;
		event->blockers = 1;

		job->blocked = NULL;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, FALSE, 1);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ (job->process[PROCESS_POST_STOP]->pid, 0);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ (event->failed, TRUE);
		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->refs, 0);

		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_POST_STOP);
		TEST_EQ (job->exit_status, 1);

		TEST_FILE_EQ (output, ("test: test post-stop process (1) "
				       "terminated with status 1\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	job->process[PROCESS_POST_STOP]->pid = 0;


	/* Check that a failing stopping task doesn't overwrite the record
	 * of a failing earlier task.
	 */
	TEST_FEATURE ("with stopping task failure after failure");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;
		job->process[PROCESS_POST_STOP]->pid = 1;

		job->cause = event;
		event->failed = TRUE;
		event->refs = 1;
		event->blockers = 1;

		job->blocked = NULL;

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = SIGSEGV << 8;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, FALSE, 1);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);
		TEST_EQ (job->process[PROCESS_POST_STOP]->pid, 0);

		TEST_EQ_P (job->cause, NULL);
		TEST_EQ (event->failed, TRUE);
		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->refs, 0);

		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, SIGSEGV << 8);

		TEST_FILE_EQ (output, ("test: test post-stop process (1) "
				       "terminated with status 1\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	job->process[PROCESS_POST_STOP]->pid = 0;


	/* Check that we can reap the post-start task of the job, the
	 * exit status should be ignored and the job transitioned into
	 * the running state.  The pid of the job shouldn't be cleared,
	 * but the aux pid should be.
	 */
	TEST_FEATURE ("with post-start process");
	job->process[PROCESS_POST_START] = job_process_new (job);
	job->process[PROCESS_POST_START]->command = "echo";

	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_POST_START;
		job->process[PROCESS_MAIN]->pid = 1;
		job->process[PROCESS_POST_START]->pid = 2;

		job->cause = event;
		event->failed = FALSE;
		event->refs = 1;
		event->blockers = 1;

		job->blocked = NULL;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 2, FALSE, 1);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 1);
		TEST_EQ (job->process[PROCESS_POST_START]->pid, 0);

		TEST_EQ_P (job->cause, event);
		TEST_EQ (event->failed, FALSE);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->refs, 1);

		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test post-start process (2) "
				       "terminated with status 1\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	job->process[PROCESS_MAIN]->pid = 0;
	job->process[PROCESS_POST_START]->pid = 0;


	/* Check that we can reap the running task of the job, even if it
	 * dies during the post-start state, which should set the goal to
	 * stop and transition a state change into the stopping state.
	 */
	TEST_FEATURE ("with running process in post-start state");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_POST_START;
		job->process[PROCESS_MAIN]->pid = 1;

		job->cause = event;
		event->failed = FALSE;
		event->refs = 1;
		event->blockers = 1;

		job->blocked = NULL;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_child_reaper (NULL, 1, FALSE, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 0);

		TEST_EQ_P (job->cause, event);
		TEST_EQ (event->failed, FALSE);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->refs, 1);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);
	}

	job->process[PROCESS_MAIN]->pid = 0;


	/* Check that we can reap the running task of the job, while there
	 * is a post-start script running; this should only set the goal to
	 * stop since we also have to wait for the post-start script to stop.
	 */
	TEST_FEATURE ("with running process while post-start running");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_POST_START;
		job->process[PROCESS_MAIN]->pid = 1;
		job->process[PROCESS_POST_START]->pid = 2;

		job->cause = event;
		event->failed = FALSE;
		event->refs = 1;
		event->blockers = 1;

		job->blocked = NULL;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_child_reaper (NULL, 1, FALSE, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_POST_START);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 0);
		TEST_EQ (job->process[PROCESS_POST_START]->pid, 2);

		TEST_EQ_P (job->cause, event);
		TEST_EQ (event->failed, FALSE);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->refs, 1);

		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);
	}

	job->process[PROCESS_MAIN]->pid = 0;
	job->process[PROCESS_POST_START]->pid = 0;


	/* Check that we can reap the running process before the post-start
	 * process finishes.  Reaping the running process should mark the job
	 * to be stopped, but not change the state, then reaping the post-start
	 * process should change the state.
	 */
	TEST_FEATURE ("with running then post-start process");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_POST_START;
		job->process[PROCESS_MAIN]->pid = 1;
		job->process[PROCESS_POST_START]->pid = 2;

		job->cause = event;
		event->failed = FALSE;
		event->refs = 1;
		event->blockers = 1;

		job->blocked = NULL;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_child_reaper (NULL, 1, FALSE, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_POST_START);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 0);
		TEST_EQ (job->process[PROCESS_POST_START]->pid, 2);

		TEST_EQ_P (job->cause, event);
		TEST_EQ (event->failed, FALSE);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->refs, 1);

		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		job_child_reaper (NULL, 2, FALSE, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 0);
		TEST_EQ (job->process[PROCESS_POST_START]->pid, 0);

		TEST_EQ_P (job->cause, event);
		TEST_EQ (event->failed, FALSE);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->refs, 1);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);
	}

	job->process[PROCESS_MAIN]->pid = 0;
	job->process[PROCESS_POST_START]->pid = 0;


	/* Check that we can reap a failed running process before the
	 * post-start process finishes.  Reaping the running process
	 * should mark the job to be stopped, but not change the state,
	 * then reaping the post-start process should change the state.
	 */
	TEST_FEATURE ("with failed running then post-start process");
	TEST_ALLOC_FAIL {
		job->goal = JOB_START;
		job->state = JOB_POST_START;
		job->process[PROCESS_MAIN]->pid = 1;
		job->process[PROCESS_POST_START]->pid = 2;

		job->cause = event;
		event->failed = FALSE;
		event->refs = 1;
		event->blockers = 1;

		job->blocked = NULL;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 1, TRUE, SIGSEGV);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_POST_START);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 0);
		TEST_EQ (job->process[PROCESS_POST_START]->pid, 2);

		TEST_EQ_P (job->cause, event);
		TEST_EQ (event->failed, TRUE);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->refs, 1);

		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, SIGSEGV << 8);

		TEST_FILE_EQ (output, ("test: test main process (1) "
				       "killed by SEGV signal\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		job_child_reaper (NULL, 2, FALSE, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 0);
		TEST_EQ (job->process[PROCESS_POST_START]->pid, 0);

		TEST_EQ_P (job->cause, event);
		TEST_EQ (event->failed, TRUE);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->refs, 1);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, SIGSEGV << 8);
	}

	job->process[PROCESS_MAIN]->pid = 0;
	job->process[PROCESS_POST_START]->pid = 0;


	/* Check that we can reap the pre-stop task of the job, the
	 * exit status should be ignored and the job transitioned into
	 * the stopping state.  The pid of the job shouldn't be cleared,
	 * but the aux pid should be.
	 */
	TEST_FEATURE ("with pre-stop process");
	job->process[PROCESS_PRE_STOP] = job_process_new (job);
	job->process[PROCESS_PRE_STOP]->command = "echo";

	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_PRE_STOP;
		job->process[PROCESS_MAIN]->pid = 1;
		job->process[PROCESS_PRE_STOP]->pid = 2;

		job->cause = event;
		event->failed = FALSE;
		event->refs = 1;
		event->blockers = 1;

		job->blocked = NULL;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_reaper (NULL, 2, FALSE, 1);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 1);
		TEST_EQ (job->process[PROCESS_PRE_STOP]->pid, 0);

		TEST_EQ_P (job->cause, event);
		TEST_EQ (event->failed, FALSE);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->refs, 1);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test pre-stop process (2) "
				       "terminated with status 1\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	job->process[PROCESS_MAIN]->pid = 0;
	job->process[PROCESS_PRE_STOP]->pid = 0;


	/* Check that we can reap the running task of the job, even if it
	 * dies during the pre-stop state, which transition a state change
	 * into the stopping state.
	 */
	TEST_FEATURE ("with running process in pre-stop state");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_PRE_STOP;
		job->process[PROCESS_MAIN]->pid = 1;

		job->cause = event;
		event->failed = FALSE;
		event->refs = 1;
		event->blockers = 1;

		job->blocked = NULL;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_child_reaper (NULL, 1, FALSE, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 0);

		TEST_EQ_P (job->cause, event);
		TEST_EQ (event->failed, FALSE);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->refs, 1);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);
	}

	job->process[PROCESS_MAIN]->pid = 0;


	/* Check that we can reap the running task of the job, while there
	 * is a pre-stop script running; this should have no other effect
	 * since we also have to wait for the pre-stop script to stop.
	 */
	TEST_FEATURE ("with running process while pre-stop running");
	TEST_ALLOC_FAIL {
		job->goal = JOB_STOP;
		job->state = JOB_PRE_STOP;
		job->process[PROCESS_MAIN]->pid = 1;
		job->process[PROCESS_PRE_STOP]->pid = 2;

		job->cause = event;
		event->failed = FALSE;
		event->refs = 1;
		event->blockers = 1;

		job->blocked = NULL;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_child_reaper (NULL, 1, FALSE, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_PRE_STOP);
		TEST_EQ (job->process[PROCESS_MAIN]->pid, 0);
		TEST_EQ (job->process[PROCESS_PRE_STOP]->pid, 2);

		TEST_EQ_P (job->cause, event);
		TEST_EQ (event->failed, FALSE);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->refs, 1);

		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);
	}

	job->process[PROCESS_MAIN]->pid = 0;
	job->process[PROCESS_PRE_STOP]->pid = 0;


	fclose (output);

	nih_list_free (&job->entry);

	nih_list_free (&event->entry);
	event_poll ();
}


void
test_handle_event (void)
{
	Job           *job1, *job2;
	Event         *event, *event2;
	EventOperator *oper;

	TEST_FUNCTION ("job_handle_event");
	job1 = job_new (NULL, "foo");
	job1->respawn_limit = 0;

	job1->start_on = event_operator_new (job1, EVENT_AND, NULL, NULL);

	oper = event_operator_new (job1->start_on, EVENT_MATCH,
				   "wibble", NULL);
	nih_tree_add (&job1->start_on->node, &oper->node, NIH_TREE_LEFT);

	oper = event_operator_new (job1->start_on, EVENT_MATCH,
				   "wobble", NULL);
	nih_tree_add (&job1->start_on->node, &oper->node, NIH_TREE_RIGHT);

	job2 = job_new (NULL, "bar");
	job2->respawn_limit = 0;

	job2->stop_on = event_operator_new (job2, EVENT_OR, NULL, NULL);

	oper = event_operator_new (job2->stop_on, EVENT_MATCH,
				   "wibble", NULL);
	nih_tree_add (&job2->stop_on->node, &oper->node, NIH_TREE_LEFT);

	oper = event_operator_new (job2->stop_on, EVENT_MATCH,
				   "wobble", NULL);
	nih_tree_add (&job2->stop_on->node, &oper->node, NIH_TREE_RIGHT);


	/* Check that a non matching event has no effect on either job,
	 * and doesn't result in the event being given any jobs.
	 */
	TEST_FEATURE ("with non-matching event");
	event = event_new (NULL, "biscuit", NULL, NULL);

	TEST_ALLOC_FAIL {
		event->blockers = 0;
		event->refs = 0;

		job1->goal = JOB_STOP;
		job1->state = JOB_WAITING;
		job1->cause = NULL;
		job1->blocked = NULL;

		job2->goal = JOB_START;
		job2->state = JOB_RUNNING;
		job2->cause = NULL;
		job2->blocked = NULL;

		job_handle_event (event);

		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->refs, 0);

		TEST_EQ (job1->goal, JOB_STOP);
		TEST_EQ (job1->state, JOB_WAITING);
		TEST_EQ_P (job1->cause, NULL);
		TEST_EQ_P (job1->blocked, NULL);

		oper = job1->start_on;
		TEST_EQ (oper->value, FALSE);

		oper = (EventOperator *)job1->start_on->node.left;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);
		TEST_EQ (oper->blocked, FALSE);

		oper = (EventOperator *)job1->start_on->node.right;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);
		TEST_EQ (oper->blocked, FALSE);

		TEST_EQ (job2->goal, JOB_START);
		TEST_EQ (job2->state, JOB_RUNNING);
		TEST_EQ_P (job2->cause, NULL);
		TEST_EQ_P (job2->blocked, NULL);

		oper = job2->stop_on;
		TEST_EQ (oper->value, FALSE);

		oper = (EventOperator *)job2->stop_on->node.left;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);
		TEST_EQ (oper->blocked, FALSE);

		oper = (EventOperator *)job2->stop_on->node.right;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);
		TEST_EQ (oper->blocked, FALSE);
	}

	nih_list_free (&event->entry);


	/* Check that a matching event is recorded against the operator that
	 * matches it, but only affects the job if it completes the
	 * expression.
	 */
	TEST_FEATURE ("with matching event to stop");
	event = event_new (NULL, "wibble", NULL, NULL);

	TEST_ALLOC_FAIL {
		event->blockers = 0;
		event->refs = 0;

		job1->goal = JOB_STOP;
		job1->state = JOB_WAITING;
		job1->cause = NULL;
		job1->blocked = NULL;

		job2->goal = JOB_START;
		job2->state = JOB_RUNNING;
		job2->cause = NULL;
		job2->blocked = NULL;

		job_handle_event (event);

		TEST_EQ (event->blockers, 3); /* 2 */
		TEST_EQ (event->refs, 3); /* 2 */

		TEST_EQ (job1->goal, JOB_STOP);
		TEST_EQ (job1->state, JOB_WAITING);
		TEST_EQ_P (job1->cause, NULL);
		TEST_EQ_P (job1->blocked, NULL);

		oper = job1->start_on;
		TEST_EQ (oper->value, FALSE);

		oper = (EventOperator *)job1->start_on->node.left;
		TEST_EQ (oper->value, TRUE);
		TEST_EQ_P (oper->event, event);
		TEST_EQ (oper->blocked, TRUE);

		oper = (EventOperator *)job1->start_on->node.right;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);
		TEST_EQ (oper->blocked, FALSE);

		TEST_EQ (job2->goal, JOB_STOP);
		TEST_EQ (job2->state, JOB_STOPPING);
		TEST_EQ_P (job2->cause, event);
		TEST_NE_P (job2->blocked, NULL);
		TEST_EQ (job2->blocked->refs, 1);
		event_unref (job2->blocked);

		oper = job2->stop_on;
		TEST_EQ (oper->value, TRUE);

		oper = (EventOperator *)job2->stop_on->node.left;
		TEST_EQ (oper->value, TRUE);
		TEST_EQ_P (oper->event, event);
		TEST_EQ (oper->blocked, TRUE);

		oper = (EventOperator *)job2->stop_on->node.right;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);
		TEST_EQ (oper->blocked, FALSE);

		event_operator_reset (job1->start_on);
		event_operator_reset (job2->stop_on);
	}

	nih_list_free (&event->entry);


	/* Check that a second event can complete an expression and affect
	 * the job.
	 */
	TEST_FEATURE ("with matching event to start");
	event = event_new (NULL, "wibble", NULL, NULL);
	event2 = event_new (NULL, "wobble", NULL, NULL);

	TEST_ALLOC_FAIL {
		event->blockers = 0;
		event->refs = 0;

		event2->blockers = 0;
		event2->refs = 0;

		job1->goal = JOB_STOP;
		job1->state = JOB_WAITING;
		job1->cause = NULL;
		job1->blocked = NULL;

		job2->goal = JOB_START;
		job2->state = JOB_RUNNING;
		job2->cause = NULL;
		job2->blocked = NULL;

		job_handle_event (event);
		job_handle_event (event2);

		TEST_EQ (event->blockers, 3); /* 2 */
		TEST_EQ (event->refs, 3); /* 2 */

		TEST_EQ (event2->blockers, 3); /* 2 */
		TEST_EQ (event2->refs, 3); /* 2 */

		TEST_EQ (job1->goal, JOB_START);
		TEST_EQ (job1->state, JOB_STARTING);
		TEST_EQ_P (job1->cause, event2);
		TEST_NE_P (job1->blocked, NULL);
		TEST_EQ (job1->blocked->refs, 1);
		event_unref (job1->blocked);

		oper = job1->start_on;
		TEST_EQ (oper->value, TRUE);

		oper = (EventOperator *)job1->start_on->node.left;
		TEST_EQ (oper->value, TRUE);
		TEST_EQ_P (oper->event, event);
		TEST_EQ (oper->blocked, TRUE);

		oper = (EventOperator *)job1->start_on->node.right;
		TEST_EQ (oper->value, TRUE);
		TEST_EQ_P (oper->event, event2);
		TEST_EQ (oper->blocked, TRUE);

		TEST_EQ (job2->goal, JOB_STOP);
		TEST_EQ (job2->state, JOB_STOPPING);
		TEST_EQ_P (job2->cause, event);
		TEST_NE_P (job2->blocked, NULL);
		TEST_EQ (job2->blocked->refs, 1);
		event_unref (job2->blocked);

		oper = job2->stop_on;
		TEST_EQ (oper->value, TRUE);

		oper = (EventOperator *)job2->stop_on->node.left;
		TEST_EQ (oper->value, TRUE);
		TEST_EQ_P (oper->event, event);
		TEST_EQ (oper->blocked, TRUE);

		oper = (EventOperator *)job2->stop_on->node.right;
		TEST_EQ (oper->value, TRUE);
		TEST_EQ_P (oper->event, event2);
		TEST_EQ (oper->blocked, TRUE);

		event_operator_reset (job1->start_on);
		event_operator_reset (job2->stop_on);
	}

	nih_list_free (&event->entry);
	nih_list_free (&event2->entry);

	nih_list_free (&job2->entry);


	/* Check that a matching event for an instance job results in the
	 * job itself being unchanged, but a new job being created that's
	 * an instance of the first and that one being started.  Only the
	 * new job should be referencing the event.
	 */
	TEST_FEATURE ("with matching event for instance job");
	job1->instance = TRUE;

	event = event_new (NULL, "wibble", NULL, NULL);
	event2 = event_new (NULL, "wobble", NULL, NULL);

	TEST_ALLOC_FAIL {
		event->blockers = 0;
		event->refs = 0;

		event2->blockers = 0;
		event2->refs = 0;

		job1->goal = JOB_STOP;
		job1->state = JOB_WAITING;
		job1->cause = NULL;
		job1->blocked = NULL;

		job_handle_event (event);
		job_handle_event (event2);

		TEST_EQ (event->blockers, 1); /* 1 */
		TEST_EQ (event->refs, 1); /* 1 */

		TEST_EQ (event2->blockers, 2); /* 1 */
		TEST_EQ (event2->refs, 2); /* 1 */

		TEST_EQ (job1->goal, JOB_STOP);
		TEST_EQ (job1->state, JOB_WAITING);
		TEST_EQ_P (job1->cause, NULL);
		TEST_EQ_P (job1->blocked, NULL);

		oper = job1->start_on;
		TEST_EQ (oper->value, FALSE);

		oper = (EventOperator *)job1->start_on->node.left;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);
		TEST_EQ (oper->blocked, FALSE);

		oper = (EventOperator *)job1->start_on->node.right;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);
		TEST_EQ (oper->blocked, FALSE);

		job2 = NULL;
		NIH_HASH_FOREACH (jobs, iter) {
			Job *instance = (Job *)iter;

			if (instance->instance_of == job1) {
				job2 = instance;
				break;
			}
		}

		TEST_NE_P (job2, NULL);
		TEST_EQ (job2->instance, TRUE);
		TEST_EQ_P (job2->instance_of, job1);

		TEST_EQ (job2->goal, JOB_START);
		TEST_EQ (job2->state, JOB_STARTING);
		TEST_EQ_P (job2->cause, event2);
		TEST_NE_P (job2->blocked, NULL);
		TEST_EQ (job2->blocked->refs, 1);
		event_unref (job2->blocked);

		oper = job2->start_on;
		TEST_EQ (oper->value, TRUE);

		oper = (EventOperator *)job2->start_on->node.left;
		TEST_EQ (oper->value, TRUE);
		TEST_EQ_P (oper->event, event);
		TEST_EQ (oper->blocked, TRUE);

		oper = (EventOperator *)job2->start_on->node.right;
		TEST_EQ (oper->value, TRUE);
		TEST_EQ_P (oper->event, event2);
		TEST_EQ (oper->blocked, TRUE);

		event_operator_reset (job2->start_on);

		nih_list_free (&job2->entry);
	}

	nih_list_free (&event->entry);
	nih_list_free (&event2->entry);


	nih_list_free (&job1->entry);

	event_poll ();
}

void
test_handle_event_finished (void)
{
	Job   *job1, *job2;
	Event *event;

	TEST_FUNCTION ("job_handle_event_finished");
	job1 = job_new (NULL, "foo");
	job1->respawn_limit = 0;
	job1->process[PROCESS_PRE_START] = job_process_new (job1);
	job1->process[PROCESS_PRE_START]->command = "echo";
	job1->process[PROCESS_POST_STOP] = job_process_new (job1);
	job1->process[PROCESS_POST_STOP]->command = "echo";

	job1->start_on = event_operator_new (job1, EVENT_MATCH,
					     "wibble", NULL);

	job2 = job_new (NULL, "bar");
	job2->respawn_limit = 0;
	job2->process[PROCESS_PRE_START] = job_process_new (job2);
	job2->process[PROCESS_PRE_START]->command = "echo";
	job2->process[PROCESS_POST_STOP] = job_process_new (job2);
	job2->process[PROCESS_POST_STOP]->command = "echo";

	job2->stop_on = event_operator_new (job2, EVENT_MATCH, "wibble", NULL);


	/* Check that a non matching event has no effect on either job.
	 */
	TEST_FEATURE ("with non-matching event");
	event = event_new (NULL, "biscuit", NULL, NULL);

	TEST_ALLOC_FAIL {
		job1->goal = JOB_STOP;
		job1->state = JOB_STOPPING;
		job1->blocked = NULL;

		job2->goal = JOB_START;
		job2->state = JOB_STARTING;
		job2->blocked = NULL;

		job_handle_event_finished (event);

		TEST_EQ (job1->goal, JOB_STOP);
		TEST_EQ (job1->state, JOB_STOPPING);
		TEST_EQ_P (job1->blocked, NULL);

		TEST_EQ (job2->goal, JOB_START);
		TEST_EQ (job2->state, JOB_STARTING);
		TEST_EQ_P (job2->blocked, NULL);
	}

	nih_list_free (&event->entry);


	/* Check that a matching event results in the jobs being unblocked
	 * and then started or stopped as appropriate.
	 */
	TEST_FEATURE ("with matching event");
	event = event_new (NULL, "wibble", NULL, NULL);

	TEST_ALLOC_FAIL {
		job1->goal = JOB_STOP;
		job1->state = JOB_STOPPING;
		job1->process[PROCESS_POST_STOP]->pid = 0;
		job1->blocked = event;

		job2->goal = JOB_START;
		job2->state = JOB_STARTING;
		job2->process[PROCESS_PRE_START]->pid = 0;
		job2->blocked = event;
		event->refs = 3;
		event->blockers = 1;

		job_handle_event_finished (event);

		TEST_EQ (job1->goal, JOB_STOP);
		TEST_EQ (job1->state, JOB_POST_STOP);
		TEST_GT (job1->process[PROCESS_POST_STOP]->pid, 0);
		TEST_EQ_P (job1->blocked, NULL);

		waitpid (job1->process[PROCESS_POST_STOP]->pid, NULL, 0);

		TEST_EQ (job2->goal, JOB_START);
		TEST_EQ (job2->state, JOB_PRE_START);
		TEST_GT (job2->process[PROCESS_PRE_START]->pid, 0);
		TEST_EQ_P (job2->blocked, NULL);

		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 1);

		waitpid (job2->process[PROCESS_PRE_START]->pid, NULL, 0);
	}

	nih_list_free (&event->entry);


	nih_list_free (&job2->entry);
	nih_list_free (&job1->entry);

	event_poll ();
}


void
test_detect_stalled (void)
{
	Job   *job1, *job2;
	Event *event;

	TEST_FUNCTION ("job_detect_stalled");

	event_init ();

	job1 = job_new (NULL, "foo");
	job1->goal = JOB_STOP;
	job1->state = JOB_WAITING;

	job2 = job_new (NULL, "bar");
	job2->goal = JOB_STOP;
	job2->state = JOB_WAITING;


	/* Check that even if we detect the stalled state, we do nothing
	 * if there's no handled for it.
	 */
	TEST_FEATURE ("with stalled state and no handler");
	job_detect_stalled ();

	TEST_LIST_EMPTY (events);


	/* Check that we can detect the stalled state, when all jobs are
	 * stopped, which results in the stalled event being queued.
	 */
	TEST_FEATURE ("with stalled state");
	job1->start_on = event_operator_new (job1, EVENT_MATCH,
					     "stalled", NULL);

	job_detect_stalled ();

	event = (Event *)events->prev;
	TEST_EQ_STR (event->name, "stalled");
	nih_list_free (&event->entry);

	TEST_LIST_EMPTY (events);


	/* Check that we don't detect the stalled state if one of the jobs
	 * is waiting to be started.
	 */
	TEST_FEATURE ("with waiting job");
	job1->goal = JOB_START;

	job_detect_stalled ();

	TEST_LIST_EMPTY (events);


	/* Check that we don't detect the stalled state if one of the jobs
	 * is starting.
	 */
	TEST_FEATURE ("with starting job");
	job1->state = JOB_PRE_START;

	job_detect_stalled ();

	TEST_LIST_EMPTY (events);


	/* Check that we don't detect the stalled state if one of the jobs
	 * is running.
	 */
	TEST_FEATURE ("with running job");
	job1->state = JOB_RUNNING;

	job_detect_stalled ();

	TEST_LIST_EMPTY (events);


	/* Check that we don't detect the stalled if one of the jobs is
	 * stopping.
	 */
	TEST_FEATURE ("with stopping job");
	job1->goal = JOB_STOP;
	job1->state = JOB_POST_STOP;

	job_detect_stalled ();

	TEST_LIST_EMPTY (events);


	nih_list_free (&job1->entry);
	nih_list_free (&job2->entry);
	event_poll ();
}

void
test_free_deleted ()
{
	Job *job1, *job2, *job3;

	TEST_FUNCTION ("job_free_deleted");
	job1 = job_new (NULL, "frodo");
	job1->goal = JOB_START;
	job1->state = JOB_RUNNING;

	job2 = job_new (NULL, "bilbo");
	job2->goal = JOB_STOP;
	job2->state = JOB_DELETED;

	job3 = job_new (NULL, "drogo");
	job3->goal = JOB_STOP;
	job3->state = JOB_STOPPING;

	nih_alloc_set_destructor (job1, my_destructor);
	nih_alloc_set_destructor (job2, my_destructor);
	nih_alloc_set_destructor (job3, my_destructor);


	/* Check that only those jobs in the deleted state are removed from
	 * the list and freed.
	 */
	TEST_FEATURE ("with single deleted job");
	destructor_called = 0;

	job_free_deleted ();

	TEST_EQ (destructor_called, 1);


	/* Check that if there are no jobs to be deleted, nothing happens. */
	TEST_FEATURE ("with no deleted jobs");
	destructor_called = 0;

	job_free_deleted ();

	TEST_EQ (destructor_called, 0);

	nih_list_free (&job1->entry);
	nih_list_free (&job3->entry);
}


int
main (int   argc,
      char *argv[])
{
	test_process_new ();
	test_process_copy ();
	test_new ();
	test_copy ();
	test_find_by_name ();
	test_find_by_pid ();
	test_find_by_id ();
	test_instance ();
	test_change_goal ();
	test_change_state ();
	test_next_state ();
	test_should_replace ();
	test_run_process ();
	test_kill_process ();
	test_child_reaper ();
	test_handle_event ();
	test_handle_event_finished ();
	test_detect_stalled ();
	test_free_deleted ();

	return 0;
}
