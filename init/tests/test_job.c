/* upstart
 *
 * test_job.c - test suite for init/job.c
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

#if HAVE_VALGRIND_VALGRIND_H
#include <valgrind/valgrind.h>
#endif /* HAVE_VALGRIND_VALGRIND_H */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
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
#include "conf.h"


static char *argv0;

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

		nih_free (process);
	}
}


void
test_config_new (void)
{
	JobConfig *config;
	int        i;

	/* Check that we can create a new JobConfig structure; the
	 * structure should be allocated with nih_alloc but not placed in
	 * the jobs hash.
	 */
	TEST_FUNCTION ("job_config_new");
	job_init ();
	TEST_ALLOC_FAIL {
		config = job_config_new (NULL, "test");

		if (test_alloc_failed) {
			TEST_EQ_P (config, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (config, sizeof (JobConfig));
		TEST_LIST_EMPTY (&config->entry);

		TEST_ALLOC_PARENT (config->name, config);
		TEST_EQ_STR (config->name, "test");
		TEST_EQ_P (config->description, NULL);
		TEST_EQ_P (config->author, NULL);
		TEST_EQ_P (config->version, NULL);

		TEST_EQ_P (config->start_on, NULL);
		TEST_EQ_P (config->stop_on, NULL);

		TEST_LIST_EMPTY (&config->emits);

		TEST_NE_P (config->process, NULL);
		TEST_ALLOC_PARENT (config->process, config);
		TEST_ALLOC_SIZE (config->process,
				 sizeof (JobProcess *) * PROCESS_LAST);

		for (i = 0; i < PROCESS_LAST; i++)
			TEST_EQ_P (config->process[i], NULL);

		TEST_EQ (config->wait_for, JOB_WAIT_NONE);

		TEST_EQ (config->kill_timeout, JOB_DEFAULT_KILL_TIMEOUT);

		TEST_EQ (config->instance, FALSE);
		TEST_EQ (config->service, FALSE);
		TEST_EQ (config->respawn, FALSE);
		TEST_EQ (config->respawn_limit, JOB_DEFAULT_RESPAWN_LIMIT);
		TEST_EQ (config->respawn_interval, JOB_DEFAULT_RESPAWN_INTERVAL);

		TEST_EQ_P (config->normalexit, NULL);
		TEST_EQ (config->normalexit_len, 0);

		TEST_EQ (config->console, CONSOLE_NONE);
		TEST_EQ_P (config->env, NULL);

		TEST_EQ (config->umask, JOB_DEFAULT_UMASK);
		TEST_EQ (config->nice, 0);

		for (i = 0; i < RLIMIT_NLIMITS; i++)
			TEST_EQ_P (config->limits[i], NULL);

		TEST_EQ_P (config->chroot, NULL);
		TEST_EQ_P (config->chdir, NULL);
		TEST_FALSE (config->deleted);

		nih_free (config);
	}
}

void
test_config_replace (void)
{
	ConfSource *source1, *source2, *source3;
	ConfFile   *file1, *file2, *file3;
	JobConfig  *config1, *config2, *config3, *ptr;
	Job        *job;

	TEST_FUNCTION ("job_config_replace");
	source1 = conf_source_new (NULL, "/tmp/foo", CONF_DIR);

	source2 = conf_source_new (NULL, "/tmp/bar", CONF_JOB_DIR);

	file1 = conf_file_new (source2, "/tmp/bar/frodo");
	config1 = file1->job = job_config_new (NULL, "frodo");

	file2 = conf_file_new (source2, "/tmp/bar/bilbo");
	config2 = file2->job = job_config_new (NULL, "bilbo");

	source3 = conf_source_new (NULL, "/tmp/baz", CONF_JOB_DIR);

	file3 = conf_file_new (source3, "/tmp/baz/frodo");
	config3 = file3->job = job_config_new (NULL, "frodo");

	job = job_new (config3);
	job->goal = JOB_START;
	job->state = JOB_RUNNING;

	nih_hash_add (jobs, &config3->entry);


	/* Check that the current job will not be replaced if it has
	 * instances.
	 */
	TEST_FEATURE ("with job with instances");
	ptr = job_config_replace (config3);
	TEST_EQ (ptr, config3);

	ptr = (JobConfig *)nih_hash_lookup (jobs, "frodo");
	TEST_EQ (ptr, config3);


	/* Check that the current job can be replaced by another job if it
	 * does not have instances, and that the new job is returned.
	 */
	TEST_FEATURE ("with job without instances");
	nih_free (job);

	ptr = job_config_replace (config3);
	TEST_EQ (ptr, config1);

	ptr = (JobConfig *)nih_hash_lookup (jobs, "frodo");
	TEST_EQ (ptr, config1);

	TEST_LIST_EMPTY (&config3->entry);


	/* Check that replacing a job which is the current and highest
	 * priority job leaves it as the current one.
	 */
	TEST_FEATURE ("with current job already best");
	ptr = job_config_replace (config1);
	TEST_EQ (ptr, config1);

	ptr = (JobConfig *)nih_hash_lookup (jobs, "frodo");
	TEST_EQ (ptr, config1);


	/* Check that if there is no job, it is removed from the hash table.
	 */
	TEST_FEATURE ("with no job left");
	file1->job = file3->job = NULL;

	ptr = job_config_replace (config1);
	TEST_EQ (ptr, NULL);

	ptr = (JobConfig *)nih_hash_lookup (jobs, "frodo");
	TEST_EQ (ptr, NULL);

	TEST_LIST_EMPTY (&config1->entry);


	file2->job = NULL;
	nih_free (source3);
	nih_free (source2);
	nih_free (source1);

	nih_free (config3);
	nih_free (config2);
	nih_free (config1);
}

void
test_config_environment (void)
{
	JobConfig  *config;
	char      **env;
	size_t      len;

	TEST_FUNCTION ("job_config_environment");

	/* Check that a job created with an empty environment will just have
	 * the built-ins in the returned environment.
	 */
	TEST_FEATURE ("with no configured environment");
	config = job_config_new (NULL, "test");

	TEST_ALLOC_FAIL {
		env = job_config_environment (NULL, config, &len);

		if (test_alloc_failed) {
			TEST_EQ_P (env, NULL);
			continue;
		}

		TEST_NE_P (env, NULL);
		TEST_EQ (len, 2);
		TEST_ALLOC_SIZE (env, sizeof (char *) * 3);

		TEST_ALLOC_PARENT (env[0], env);
		TEST_EQ_STRN (env[0], "PATH=");
		TEST_ALLOC_PARENT (env[1], env);
		TEST_EQ_STRN (env[1], "TERM=");
		TEST_EQ_P (env[2], NULL);

		nih_free (env);
	}

	nih_free (config);


	/* Check that a job created with defined environment variables will
	 * have those appended to the environment as well as the builtins.
	 */
	TEST_FEATURE ("with configured environment");
	config = job_config_new (NULL, "test");
	config->env = nih_str_array_new (config);
	assert (nih_str_array_add (&(config->env), config, NULL, "FOO=BAR"));
	assert (nih_str_array_add (&(config->env), config, NULL, "BAR=BAZ"));

	TEST_ALLOC_FAIL {
		env = job_config_environment (NULL, config, &len);

		if (test_alloc_failed) {
			TEST_EQ_P (env, NULL);
			continue;
		}

		TEST_NE_P (env, NULL);
		TEST_EQ (len, 4);
		TEST_ALLOC_SIZE (env, sizeof (char *) * 5);

		TEST_ALLOC_PARENT (env[0], env);
		TEST_EQ_STRN (env[0], "PATH=");
		TEST_ALLOC_PARENT (env[1], env);
		TEST_EQ_STRN (env[1], "TERM=");
		TEST_ALLOC_PARENT (env[2], env);
		TEST_EQ_STR (env[2], "FOO=BAR");
		TEST_ALLOC_PARENT (env[3], env);
		TEST_EQ_STR (env[3], "BAR=BAZ");
		TEST_EQ_P (env[4], NULL);

		nih_free (env);
	}

	nih_free (config);


	/* Check that configured environment override built-ins.
	 */
	TEST_FEATURE ("with configuration overriding built-ins");
	config = job_config_new (NULL, "test");
	config->env = nih_str_array_new (config);
	assert (nih_str_array_add (&(config->env), config, NULL, "FOO=BAR"));
	assert (nih_str_array_add (&(config->env), config, NULL, "BAR=BAZ"));
	assert (nih_str_array_add (&(config->env), config, NULL, "TERM=elmo"));

	TEST_ALLOC_FAIL {
		env = job_config_environment (NULL, config, &len);

		if (test_alloc_failed) {
			TEST_EQ_P (env, NULL);
			continue;
		}

		TEST_NE_P (env, NULL);
		TEST_EQ (len, 4);
		TEST_ALLOC_SIZE (env, sizeof (char *) * 5);

		TEST_ALLOC_PARENT (env[0], env);
		TEST_EQ_STRN (env[0], "PATH=");
		TEST_ALLOC_PARENT (env[1], env);
		TEST_EQ_STR (env[1], "TERM=elmo");
		TEST_ALLOC_PARENT (env[2], env);
		TEST_EQ_STR (env[2], "FOO=BAR");
		TEST_ALLOC_PARENT (env[3], env);
		TEST_EQ_STR (env[3], "BAR=BAZ");
		TEST_EQ_P (env[4], NULL);

		nih_free (env);
	}

	nih_free (config);
}


void
test_new (void)
{
	JobConfig     *config;
	Job           *job;
	EventOperator *oper;
	int            i;

	/* Check that we can create a new job structure; the structure
	 * should be allocated with nih_alloc, placed in the instances
	 * list of the config and have sensible defaults.
	 */
	TEST_FUNCTION ("job_new");
	job_init ();

	config = job_config_new (NULL, "test");

	config->stop_on = event_operator_new (config, EVENT_MATCH,
					      "baz", NULL);


	TEST_ALLOC_FAIL {
		job_instances = 0;
		job_id = 99;

		job = job_new (config);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);
			TEST_EQ (job_instances, 0);
			continue;
		}

		TEST_ALLOC_PARENT (job, config);
		TEST_ALLOC_SIZE (job, sizeof (Job));
		TEST_LIST_NOT_EMPTY (&job->entry);

		TEST_EQ (job_instances, 1);

		TEST_EQ (job->id, 99);

		oper = (EventOperator *)job->stop_on;
		TEST_ALLOC_PARENT (oper, job);
		TEST_ALLOC_SIZE (oper, sizeof (EventOperator));
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "baz");
		TEST_EQ_P (oper->env, NULL);
		TEST_EQ (oper->value, FALSE);
		TEST_EQ (oper->blocked, FALSE);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);

		TEST_EQ_P (job->blocked, NULL);
		TEST_EQ_P (job->blocking, NULL);

		TEST_EQ_P (job->env, NULL);
		TEST_EQ_P (job->start_env, NULL);
		TEST_EQ_P (job->stop_env, NULL);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_NE_P (job->pid, NULL);
		TEST_ALLOC_PARENT (job->pid, job);
		TEST_ALLOC_SIZE (job->pid, sizeof (pid_t) * PROCESS_LAST);

		for (i = 0; i < PROCESS_LAST; i++)
			TEST_EQ (job->pid[i], 0);

		TEST_EQ_P (job->kill_timer, NULL);

		TEST_EQ (job->respawn_count, 0);
		TEST_EQ (job->respawn_time, 0);

		TEST_EQ (job->trace_forks, 0);
		TEST_EQ (job->trace_state, TRACE_NONE);

		event_operator_reset (job->stop_on);

		nih_free (job);
	}

	event_operator_reset (config->stop_on);

	nih_free (config);
}


void
test_find_by_pid (void)
{
	JobConfig   *config1, *config2, *config3;
	Job         *job1, *job2, *job3, *job4, *job5, *ptr;
	ProcessType  process;

	TEST_FUNCTION ("job_find_by_pid");
	config1 = job_config_new (NULL, "foo");
	config1->process[PROCESS_MAIN] = job_process_new (config1);
	config1->process[PROCESS_POST_START] = job_process_new (config1);
	config1->instance = TRUE;
	nih_hash_add (jobs, &config1->entry);

	config2 = job_config_new (NULL, "bar");
	config2->process[PROCESS_PRE_START] = job_process_new (config2);
	config2->process[PROCESS_MAIN] = job_process_new (config2);
	config2->process[PROCESS_PRE_STOP] = job_process_new (config2);
	config2->instance = TRUE;
	nih_hash_add (jobs, &config2->entry);

	config3 = job_config_new (NULL, "baz");
	config3->process[PROCESS_POST_STOP] = job_process_new (config3);
	nih_hash_add (jobs, &config3->entry);

	job1 = job_new (config1);
	job1->pid[PROCESS_MAIN] = 10;
	job1->pid[PROCESS_POST_START] = 15;

	job2 = job_new (config1);

	job3 = job_new (config2);
	job3->pid[PROCESS_PRE_START] = 20;

	job4 = job_new (config2);
	job4->pid[PROCESS_MAIN] = 25;
	job4->pid[PROCESS_PRE_STOP] = 30;

	job5 = job_new (config3);
	job5->pid[PROCESS_POST_STOP] = 35;

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
	nih_free (job5);
	nih_free (job4);
	nih_free (job3);
	nih_free (job1);
	ptr = job_find_by_pid (20, NULL);

	TEST_EQ_P (ptr, NULL);


	/* Check that we get NULL if there are no instances running. */
	TEST_FEATURE ("with no instances");
	nih_free (job2);
	ptr = job_find_by_pid (20, NULL);

	TEST_EQ_P (ptr, NULL);


	/* Check that we get NULL if there are no jobs in the hash. */
	TEST_FEATURE ("with empty job table");
	nih_free (config1);
	nih_free (config2);
	nih_free (config3);
	ptr = job_find_by_pid (20, NULL);

	TEST_EQ_P (ptr, NULL);
}

void
test_find_by_id (void)
{
	JobConfig    *config1, *config2;
	Job          *job1, *job2, *job3, *ptr;
	unsigned int  id;

	TEST_FUNCTION ("job_find_by_id");
	config1 = job_config_new (NULL, "foo");
	nih_hash_add (jobs, &config1->entry);

	config2 = job_config_new (NULL, "bar");
	nih_hash_add (jobs, &config2->entry);

	job1 = job_new (config1);
	job2 = job_new (config1);
	job3 = job_new (config2);


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

	nih_free (config1);
	nih_free (config2);
	ptr = job_find_by_id (id);

	TEST_EQ_P (ptr, NULL);
}


void
test_instance (void)
{
	JobConfig     *config;
	Job           *job, *ptr;

	TEST_FUNCTION ("job_instance");

	config = job_config_new (NULL, "test");
	config->process[PROCESS_MAIN] = job_process_new (config);
	config->process[PROCESS_MAIN]->command = "echo";


	/* Check that NULL is returned for an inactive single instance job,
	 * which should indicate a new instance should be created.
	 */
	TEST_FEATURE ("with inactive single-instance job");
	TEST_ALLOC_FAIL {
		job = job_instance (config);

		TEST_EQ_P (job, NULL);
	}


	/* Check that the active instance of a single-instance job is
	 * returned.
	 */
	TEST_FEATURE ("with active single-instance job");
	job = job_new (config);

	TEST_ALLOC_FAIL {
		ptr = job_instance (config);

		TEST_EQ_P (ptr, job);
	}

	nih_free (job);


	/* Check that NULL is returned for an inactive multi-instance job,
	 * indicating that a new instance should be created (which is
	 * always true in this case).
	 */
	TEST_FEATURE ("with inactive multi-instance job");
	config->instance = TRUE;

	TEST_ALLOC_FAIL {
		job = job_instance (config);

		TEST_EQ_P (job, NULL);
	}

	config->instance = FALSE;


	/* Check that NULL is still returned for an active multi-instance job,
	 * since we always want to create a new instance so none can match.
	 */
	TEST_FEATURE ("with active single-instance job");
	config->instance = TRUE;
	job = job_new (config);

	TEST_ALLOC_FAIL {
		ptr = job_instance (config);

		TEST_EQ_P (ptr, NULL);
	}

	config->instance = FALSE;
	nih_free (job);


	nih_free (config);
	event_poll ();
}

void
test_change_goal (void)
{
	JobConfig *config;
	Job       *job = NULL;

	TEST_FUNCTION ("job_change_goal");
	program_name = "test";

	config = job_config_new (NULL, "test");
	config->process[PROCESS_MAIN] = job_process_new (config);
	config->process[PROCESS_MAIN]->command = "echo";
	config->process[PROCESS_PRE_START] = job_process_new (config);
	config->process[PROCESS_PRE_START]->command = "echo";
	config->process[PROCESS_POST_STOP] = job_process_new (config);
	config->process[PROCESS_POST_STOP]->command = "echo";


	/* Check that an attempt to start a waiting job results in the
	 * goal being changed to start, and the state transitioned to
	 * starting.
	 */
	TEST_FEATURE ("with waiting job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
		}

		job->goal = JOB_STOP;
		job->state = JOB_WAITING;
		job->blocked = NULL;

		job_change_goal (job, JOB_START);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STARTING);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		nih_free (job);
	}


	/* Check that an attempt to start a job that's in the process of
	 * stopping changes only the goal, and leaves the rest of the
	 * state transition up to the normal process.
	 */
	TEST_FEATURE ("with stopping job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
		}

		job->goal = JOB_STOP;
		job->state = JOB_KILLED;
		job->pid[PROCESS_MAIN] = 1;
		job->blocked = NULL;

		job_change_goal (job, JOB_START);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_KILLED);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);

		TEST_EQ_P (job->blocked, NULL);

		nih_free (job);
	}


	/* Check that an attempt to start a job that's running and still
	 * with a start goal does nothing.
	 */
	TEST_FEATURE ("with running job and start");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
		}

		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;
		job->blocked = NULL;

		job_change_goal (job, JOB_START);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);

		TEST_EQ_P (job->blocked, NULL);

		nih_free (job);
	}


	/* Check that an attempt to stop a running job results in the goal
	 * and the state being changed.
	 */
	TEST_FEATURE ("with running job and stop");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
		}

		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;
		job->blocked = NULL;

		job_change_goal (job, JOB_STOP);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		nih_free (job);
	}


	/* Check that an attempt to stop a running job without any process
	 * also results in the state being changed.
	 */
	TEST_FEATURE ("with running job and no process");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
		}

		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->blocked = NULL;

		job_change_goal (job, JOB_STOP);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		nih_free (job);
	}


	/* Check that an attempt to stop a starting job only results in the
	 * goal being changed, the state should not be changed.
	 */
	TEST_FEATURE ("with starting job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
		}

		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->pid[PROCESS_PRE_START] = 1;
		job->blocked = NULL;

		job_change_goal (job, JOB_STOP);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_PRE_START);
		TEST_EQ (job->pid[PROCESS_PRE_START], 1);

		TEST_EQ_P (job->blocked, NULL);

		nih_free (job);
	}


	/* Check that an attempt to stop a waiting job does nothing. */
	TEST_FEATURE ("with waiting job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
		}

		job->goal = JOB_STOP;
		job->state = JOB_WAITING;
		job->blocked = NULL;

		job_change_goal (job, JOB_STOP);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_WAITING);

		TEST_EQ_P (job->blocked, NULL);

		nih_free (job);
	}


	nih_free (config);
	event_poll ();
}


void
test_change_state (void)
{
	FILE         *output;
	ConfSource   *source = NULL;
	ConfFile     *file = NULL;
	JobConfig    *config, *replacement = NULL, *ptr;
	Job          *job = NULL;
	NihList      *list;
	NihListEntry *entry;
	Event        *cause, *event;
	struct stat   statbuf;
	char          dirname[PATH_MAX], filename[PATH_MAX], **env1, **env2;
	JobProcess   *tmp, *fail;
	pid_t         pid;
	int           status;

	TEST_FUNCTION ("job_change_state");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (dirname);
	mkdir (dirname, 0700);

	config = job_config_new (NULL, "test");
	config->process[PROCESS_MAIN] = job_process_new (config);
	config->process[PROCESS_MAIN]->command = nih_sprintf (
		config->process[PROCESS_MAIN], "touch %s/run", dirname);
	config->process[PROCESS_PRE_START] = job_process_new (config);
	config->process[PROCESS_PRE_START]->command = nih_sprintf (
		config->process[PROCESS_PRE_START], "touch %s/start", dirname);
	config->process[PROCESS_POST_STOP] = job_process_new (config);
	config->process[PROCESS_POST_STOP]->command = nih_sprintf (
		config->process[PROCESS_POST_STOP], "touch %s/stop", dirname);
	config->respawn_limit = 0;

	config->start_on = event_operator_new (config, EVENT_MATCH,
					       "wibble", NULL);
	config->stop_on = event_operator_new (config, EVENT_MATCH,
					      "wibble", NULL);

	fail = job_process_new (config);
	fail->command = nih_sprintf (fail, "%s/no/such/file", dirname);

	event_init ();

	cause = event_new (NULL, "wibble", NULL);
	nih_list_remove (&cause->entry);


	/* Check that a job can move from waiting to starting.  This
	 * should emit the starting event and block on it and copy the
	 * environment from start_env.
	 */
	TEST_FEATURE ("waiting to starting");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);

			assert (nih_str_array_add (&(job->start_env), job,
						   NULL, "FOO=BAR"));
			assert (nih_str_array_add (&(job->start_env), job,
						   NULL, "BAZ=BAZ"));

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_ref (cause);
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_WAITING;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		env1 = job->start_env;

		job->failed = TRUE;
		job->failed_process = PROCESS_POST_STOP;
		job->exit_status = 1;

		job_change_state (job, JOB_STARTING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STARTING);

		TEST_EQ (cause->refs, 1);
		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->env, env1);
		TEST_EQ_P (job->start_env, NULL);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);
		event_unref (cause);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "starting");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_P (event->env[1], NULL);
		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 0);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that if a job tries to move from waiting to starting too
	 * many times in a given period then it is caught, a message is
	 * output and the job deleted again.  We get a stopped event, and
	 * the cause is cleared.  The failed information should reflect
	 * that we were stopped by the starting state.
	 */
	TEST_FEATURE ("waiting to starting too fast");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 99;

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_ref (cause);
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_WAITING;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		config->respawn_limit = 10;
		config->respawn_interval = 1000;
		job->respawn_time = time (NULL);
		job->respawn_count = 10;

		TEST_FREE_TAG (job);

		TEST_DIVERT_STDERR (output) {
			job_change_state (job, JOB_STARTING);
		}
		rewind (output);

		TEST_FREE (job);

		TEST_EQ (cause->refs, 0);
		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, TRUE);

		TEST_FREE (list);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopped");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=respawn");
		TEST_EQ_P (event->env[3], NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_FILE_EQ (output, ("test: test (#99) respawning too fast, "
				       "stopped\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	config->respawn_limit = 0;
	config->respawn_interval = JOB_DEFAULT_RESPAWN_INTERVAL;


	/* Check that a job with a start process can move from starting
	 * to pre-start, and have the process run.
	 */
	TEST_FEATURE ("starting to pre-start");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_ref (cause);
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_STARTING;
		job->pid[PROCESS_PRE_START] = 0;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_PRE_START);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_PRE_START);
		TEST_NE (job->pid[PROCESS_PRE_START], 0);

		waitpid (job->pid[PROCESS_PRE_START], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/start");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ (cause->refs, 1);
		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);
		event_unref (cause);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that a job without a start process can move from starting
	 * to pre-start, skipping over that state, and instead going all
	 * the way through to the running state.  Because we get there,
	 * we should get a started event emitted.
	 */
	TEST_FEATURE ("starting to pre-start without process");
	tmp = config->process[PROCESS_PRE_START];
	config->process[PROCESS_PRE_START] = NULL;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_ref (cause);
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_STARTING;
		job->pid[PROCESS_MAIN] = 0;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_PRE_START);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_NE (job->pid[PROCESS_MAIN], 0);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/run");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ (cause->refs, 1);
		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);
		event_unref (cause);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_P (event->env[1], NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	config->process[PROCESS_PRE_START] = tmp;


	/* Check that a job with a start process that fails to run moves
	 * from starting to pre-start, the goal gets changed to stop, the
	 * status to stopping and the failed information set correctly.
	 */
	TEST_FEATURE ("starting to pre-start for failed process");
	tmp = config->process[PROCESS_PRE_START];
	config->process[PROCESS_PRE_START] = fail;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 99;

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_ref (cause);
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_STARTING;
		job->pid[PROCESS_PRE_START] = 0;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_change_state (job, JOB_PRE_START);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_PRE_START], 0);

		TEST_EQ (cause->refs, 0);
		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, TRUE);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_FREE (list);
		TEST_EQ_P (job->blocking, NULL);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopping");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=pre-start");
		TEST_EQ_P (event->env[3], NULL);
		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 0);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_PRE_START);
		TEST_EQ (job->exit_status, -1);

		TEST_FILE_EQ (output, ("test: Failed to spawn test (#99) "
				       "pre-start process: unable to execute: "
				       "No such file or directory\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	config->process[PROCESS_PRE_START] = tmp;


	/* Check that a job with a main process can move from pre-start to
	 * spawned and have the process run, and as it's not going to wait,
	 * the state will be skipped forwards to running and the started
	 * event emitted.
	 */
	TEST_FEATURE ("pre-start to spawned");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_ref (cause);
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->pid[PROCESS_MAIN] = 0;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_SPAWNED);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_NE (job->pid[PROCESS_MAIN], 0);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/run");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ (cause->refs, 1);
		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);
		event_unref (cause);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_P (event->env[1], NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


 	/* Check that a job without a main process can move from pre-start
	 * straight to running skipping the interim steps, and has the
	 * started event emitted.
	 */
	TEST_FEATURE ("pre-start to spawned without process");
	tmp = config->process[PROCESS_MAIN];
	config->process[PROCESS_MAIN] = NULL;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_ref (cause);
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_PRE_START;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_SPAWNED);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);

		TEST_EQ (cause->refs, 1);
		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);
		event_unref (cause);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_P (event->env[1], NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	config->process[PROCESS_MAIN] = tmp;


	/* Check that a job with a main process that fails has its goal
	 * changed to stop, the state changed to stopping and failed
	 * information filled in.
	 */
	TEST_FEATURE ("pre-start to spawned for failed process");
	tmp = config->process[PROCESS_MAIN];
	config->process[PROCESS_MAIN] = fail;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 99;

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_ref (cause);
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->pid[PROCESS_MAIN] = 0;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_change_state (job, JOB_SPAWNED);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_EQ (cause->refs, 0);
		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, TRUE);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_FREE (list);
		TEST_EQ_P (job->blocking, NULL);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopping");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=main");
		TEST_EQ_P (event->env[3], NULL);
		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 0);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, -1);

		TEST_FILE_EQ (output, ("test: Failed to spawn test (#99) "
				       "main process: unable to execute: "
				       "No such file or directory\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	config->process[PROCESS_MAIN] = tmp;


	/* Check that a job which has a main process that needs to wait for
	 * an event can move from pre-start to spawned and have the process
	 * run.  The state will remain in spawned until whatever we're
	 * waiting for happens.
	 */
	TEST_FEATURE ("pre-start to spawned for waiting job");
	config->wait_for = JOB_WAIT_STOP;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_ref (cause);
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->pid[PROCESS_MAIN] = 0;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_SPAWNED);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_SPAWNED);
		TEST_NE (job->pid[PROCESS_MAIN], 0);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/run");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ (cause->refs, 1);
		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);
		event_unref (cause);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	config->wait_for = JOB_WAIT_NONE;


	/* Check that a job with a post-start process can move from spawned
	 * to post-start, and have the process run.
	 */
	TEST_FEATURE ("spawned to post-start");
	config->process[PROCESS_POST_START] = job_process_new (config);
	config->process[PROCESS_POST_START]->command = nih_sprintf (
		config->process[PROCESS_POST_START],
		"touch %s/post-start", dirname);

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_ref (cause);
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = 1;
		job->pid[PROCESS_POST_START] = 0;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_POST_START);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_POST_START);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);
		TEST_NE (job->pid[PROCESS_POST_START], 0);

		waitpid (job->pid[PROCESS_POST_START], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/post-start");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ (cause->refs, 1);
		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);
		event_unref (cause);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	nih_free (config->process[PROCESS_POST_START]);
	config->process[PROCESS_POST_START] = NULL;


	/* Check that a job without a post-start process can move from
	 * spawned to post-start, skipping over that state, and instead
	 * going to the running state.  Because we get there, we should
	 * get a started event emitted.
	 */
	TEST_FEATURE ("spawned to post-start without process");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_ref (cause);
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = 1;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_POST_START);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);

		TEST_EQ (cause->refs, 1);
		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);
		event_unref (cause);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_P (event->env[1], NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that a job with a post-start process ignores the failure
	 * of that process and can move from spawned to post-start, skipping
	 * over that state, and instead going to the running state.  Because
	 * we get there, we should get a started event emitted.
	 */
	TEST_FEATURE ("spawned to post-start for failed process");
	config->process[PROCESS_POST_START] = fail;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 99;

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_ref (cause);
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = 1;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_change_state (job, JOB_POST_START);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);

		TEST_EQ (cause->refs, 1);
		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);
		event_unref (cause);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_P (event->env[1], NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: Failed to spawn test (#99) "
				       "post-start process: unable to execute: "
				       "No such file or directory\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	config->process[PROCESS_POST_START] = NULL;


	/* Check that a task can move from post-start to running, which will
	 * emit the started event but leave events blocked and referenced.
	 */
	TEST_FEATURE ("post-start to running");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_ref (cause);
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_POST_START;
		job->pid[PROCESS_MAIN] = 1;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_RUNNING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);

		TEST_EQ (cause->refs, 1);
		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);
		event_unref (cause);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_P (event->env[1], NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that a service can move from post-start to running, which
	 * will emit the started event and unblock the events that caused
	 * us to start since the job has reached the desired state.
	 */
	TEST_FEATURE ("post-start to running for service");
	config->service = TRUE;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_ref (cause);
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_POST_START;
		job->pid[PROCESS_MAIN] = 1;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_RUNNING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);

		TEST_EQ (cause->refs, 0);
		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_FREE (list);
		TEST_EQ_P (job->blocking, NULL);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "started");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_P (event->env[1], NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	config->service = FALSE;


	/* Check that a job with a pre-stop process can move from running
	 * to pre-stop, and have the process run.
	 */
	TEST_FEATURE ("running to pre-stop");
	config->process[PROCESS_PRE_STOP] = job_process_new (config);
	config->process[PROCESS_PRE_STOP]->command = nih_sprintf (
		config->process[PROCESS_PRE_STOP],
		"touch %s/pre-stop", dirname);

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
		}

		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;
		job->pid[PROCESS_PRE_STOP] = 0;

		job->blocked = NULL;
		cause->failed = FALSE;

		job->stop_on->value = TRUE;
		job->stop_on->event = cause;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_PRE_STOP);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_PRE_STOP);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);
		TEST_NE (job->pid[PROCESS_PRE_STOP], 0);

		waitpid (job->pid[PROCESS_PRE_STOP], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/pre-stop");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ (cause->refs, 1);
		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (job->stop_on->value, TRUE);
		TEST_EQ_P (job->stop_on->event, cause);
		TEST_EQ (job->stop_on->blocked, TRUE);

		event_operator_reset (job->stop_on);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	nih_free (config->process[PROCESS_PRE_STOP]);
	config->process[PROCESS_PRE_STOP] = NULL;


	/* Check that a job without a pre-stop process can move from
	 * running to pre-stop, skipping over that state, and instead
	 * going to the stopping state.  Because we get there, we should
	 * get a stopping event emitted.
	 */
	TEST_FEATURE ("running to pre-stop without process");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
		}

		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;

		job->blocked = NULL;
		cause->failed = FALSE;

		job->stop_on->value = TRUE;
		job->stop_on->event = cause;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_PRE_STOP);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);

		TEST_EQ (cause->refs, 1);
		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_EQ (job->stop_on->value, TRUE);
		TEST_EQ_P (job->stop_on->event, cause);
		TEST_EQ (job->stop_on->blocked, TRUE);

		event_operator_reset (job->stop_on);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopping");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=ok");
		TEST_EQ_P (event->env[2], NULL);
		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 0);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that a job with a pre-stop process ignores any failure and
	 * moves from running to pre-stop, and then straight into the stopping
	 * state, emitting that event.
	 */
	TEST_FEATURE ("running to pre-stop for failed process");
	config->process[PROCESS_PRE_STOP] = fail;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 99;
		}

		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;

		job->blocked = NULL;
		cause->failed = FALSE;

		job->stop_on->value = TRUE;
		job->stop_on->event = cause;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_change_state (job, JOB_PRE_STOP);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);

		TEST_EQ (cause->refs, 1);
		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_EQ (job->stop_on->value, TRUE);
		TEST_EQ_P (job->stop_on->event, cause);
		TEST_EQ (job->stop_on->blocked, TRUE);

		event_operator_reset (job->stop_on);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopping");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=ok");
		TEST_EQ_P (event->env[2], NULL);
		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 0);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: Failed to spawn test (#99) "
				       "pre-stop process: unable to execute: "
				       "No such file or directory\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	config->process[PROCESS_PRE_STOP] = NULL;


	/* Check that a job can move from running to stopping, by-passing
	 * pre-stop.  This should emit the stopping event, containing the
	 * failed information including the exit status, and block on it.
	 */
	TEST_FEATURE ("running to stopping");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
		}

		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;

		job->blocked = NULL;
		cause->failed = FALSE;

		job->stop_on->value = TRUE;
		job->stop_on->event = cause;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 1;

		job_change_state (job, JOB_STOPPING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);

		TEST_EQ (cause->refs, 1);
		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_EQ (job->stop_on->value, TRUE);
		TEST_EQ_P (job->stop_on->event, cause);
		TEST_EQ (job->stop_on->blocked, TRUE);

		event_operator_reset (job->stop_on);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopping");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=main");
		TEST_EQ_STR (event->env[3], "EXIT_STATUS=1");
		TEST_EQ_P (event->env[4], NULL);
		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 0);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, 1);

		nih_free (job);
	}


	/* Check that a job killed by a signal can move from running to
	 * stopping, by-passing pre-stop.  This should emit the stopping
	 * event, containing the failed information including the exit
	 * signal, and block on it.
	 */
	TEST_FEATURE ("running to stopping for killed process");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
		}

		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;

		job->blocked = NULL;
		cause->failed = FALSE;

		job->stop_on->value = TRUE;
		job->stop_on->event = cause;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = SIGSEGV << 8;

		job_change_state (job, JOB_STOPPING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);

		TEST_EQ (cause->refs, 1);
		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_EQ (job->stop_on->value, TRUE);
		TEST_EQ_P (job->stop_on->event, cause);
		TEST_EQ (job->stop_on->blocked, TRUE);

		event_operator_reset (job->stop_on);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopping");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=main");
		TEST_EQ_STR (event->env[3], "EXIT_SIGNAL=SEGV");
		TEST_EQ_P (event->env[4], NULL);
		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 0);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, SIGSEGV << 8);

		nih_free (job);
	}


	/* Check that a job killed by an unknown signal can move from
	 * running to stopping, by-passing pre-stop.  This should emit
	 * the stopping event, containing the failed information
	 * including the exit signal number, and block on it.
	 */
	TEST_FEATURE ("running to stopping for unknown signal");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
		}

		job->goal = JOB_STOP;
		job->state = JOB_RUNNING;

		job->blocked = NULL;
		cause->failed = FALSE;

		job->stop_on->value = TRUE;
		job->stop_on->event = cause;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 33 << 8;

		job_change_state (job, JOB_STOPPING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);

		TEST_EQ (cause->refs, 1);
		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_EQ (job->stop_on->value, TRUE);
		TEST_EQ_P (job->stop_on->event, cause);
		TEST_EQ (job->stop_on->blocked, TRUE);

		event_operator_reset (job->stop_on);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopping");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=main");
		TEST_EQ_STR (event->env[3], "EXIT_SIGNAL=33");
		TEST_EQ_P (event->env[4], NULL);
		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 0);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, 33 << 8);

		nih_free (job);
	}


	/* Check that a job can move from pre-stop back to running again;
	 * clearing the block and reference on the events that stopped it.
	 */
	TEST_FEATURE ("pre-stop to running");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
		}

		job->goal = JOB_STOP;
		job->state = JOB_PRE_STOP;

		job->blocked = NULL;
		cause->failed = FALSE;

		job->stop_on->value = TRUE;
		job->stop_on->event = cause;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_goal (job, JOB_START);
		job_change_state (job, JOB_RUNNING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);

		TEST_EQ (cause->refs, 0);
		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (job->stop_on->value, FALSE);
		TEST_EQ_P (job->stop_on->event, NULL);
		TEST_EQ (job->stop_on->blocked, FALSE);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that a job can move from pre-stop to stopping.  This
	 * should emit the stopping event, containing the failed information,
	 * and block on it.
	 */
	TEST_FEATURE ("pre-stop to stopping");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
		}

		job->goal = JOB_STOP;
		job->state = JOB_PRE_STOP;

		job->blocked = NULL;
		cause->failed = FALSE;

		job->stop_on->value = TRUE;
		job->stop_on->event = cause;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_STOPPING);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);

		TEST_EQ (cause->refs, 1);
		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_EQ (job->stop_on->value, TRUE);
		TEST_EQ_P (job->stop_on->event, cause);
		TEST_EQ (job->stop_on->blocked, TRUE);

		event_operator_reset (job->stop_on);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopping");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=ok");
		TEST_EQ_P (event->env[2], NULL);
		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 0);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that a job with an active process can move from stopping
	 * to killed, the process should be sent the TERM signal and a
	 * kill timer put in place to check up on it.
	 */
	TEST_FEATURE ("stopping to killed");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
		}

		job->goal = JOB_STOP;
		job->state = JOB_STOPPING;
		TEST_CHILD (job->pid[PROCESS_MAIN]) {
			pause ();
		}
		pid = job->pid[PROCESS_MAIN];
		setpgid (pid, pid);

		job->blocked = NULL;
		cause->failed = FALSE;

		job->stop_on->value = TRUE;
		job->stop_on->event = cause;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_KILLED);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_KILLED);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFSIGNALED (status));
		TEST_EQ (WTERMSIG (status), SIGTERM);

		TEST_EQ (cause->refs, 1);
		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (job->stop_on->value, TRUE);
		TEST_EQ_P (job->stop_on->event, cause);
		TEST_EQ (job->stop_on->blocked, TRUE);

		event_operator_reset (job->stop_on);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_NE_P (job->kill_timer, NULL);

		nih_free (job->kill_timer);
		job->kill_timer = NULL;

		nih_free (job);
	}


	/* Check that a job with no running process can move from stopping
	 * to killed, skipping over that state and ending up in post-stop
	 * instead.
	 */
	TEST_FEATURE ("stopping to killed without process");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
		}

		job->goal = JOB_STOP;
		job->state = JOB_STOPPING;
		job->pid[PROCESS_POST_STOP] = 0;

		job->blocked = NULL;
		cause->failed = FALSE;

		job->stop_on->value = TRUE;
		job->stop_on->event = cause;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_KILLED);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_POST_STOP);
		TEST_NE (job->pid[PROCESS_POST_STOP], 0);

		waitpid (job->pid[PROCESS_POST_STOP], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/stop");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ (cause->refs, 1);
		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (job->stop_on->value, TRUE);
		TEST_EQ_P (job->stop_on->event, cause);
		TEST_EQ (job->stop_on->blocked, TRUE);

		event_operator_reset (job->stop_on);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_EQ_P (job->kill_timer, NULL);

		nih_free (job);
	}


	/* Check that a job with a stop process can move from killed
	 * to post-stop, and have the process run.
	 */
	TEST_FEATURE ("killed to post-stop");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
		}

		job->goal = JOB_STOP;
		job->state = JOB_KILLED;
		job->pid[PROCESS_POST_STOP] = 0;

		job->blocked = NULL;
		cause->failed = FALSE;

		job->stop_on->value = TRUE;
		job->stop_on->event = cause;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_change_state (job, JOB_POST_STOP);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_POST_STOP);
		TEST_NE (job->pid[PROCESS_POST_STOP], 0);

		waitpid (job->pid[PROCESS_POST_STOP], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		strcpy (filename, dirname);
		strcat (filename, "/stop");
		TEST_EQ (stat (filename, &statbuf), 0);
		unlink (filename);

		TEST_EQ (cause->refs, 1);
		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (job->stop_on->value, TRUE);
		TEST_EQ_P (job->stop_on->event, cause);
		TEST_EQ (job->stop_on->blocked, TRUE);

		event_operator_reset (job->stop_on);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that a job without a stop process can move from killed
	 * to post-stop, skipping over that state, and instead going all
	 * the way through to being deleted.  Because we get there,
	 * we should get a stopped event emitted, and both the events
	 * that started and stopped the job forgotten.
	 */
	TEST_FEATURE ("killed to post-stop without process");
	tmp = config->process[PROCESS_POST_STOP];
	config->process[PROCESS_POST_STOP] = NULL;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
		}

		job->goal = JOB_STOP;
		job->state = JOB_KILLED;

		job->blocked = NULL;
		cause->failed = FALSE;

		job->stop_on->value = TRUE;
		job->stop_on->event = cause;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 1;

		TEST_FREE_TAG (job);

		job_change_state (job, JOB_POST_STOP);

		TEST_FREE (job);

		TEST_EQ (cause->refs, 0);
		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, FALSE);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopped");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=main");
		TEST_EQ_STR (event->env[3], "EXIT_STATUS=1");
		TEST_EQ_P (event->env[4], NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_free (event);

		TEST_LIST_EMPTY (events);
	}

	config->process[PROCESS_POST_STOP] = tmp;


	/* Check that a job with a stop process that fails to run moves
	 * from killed to post-start, the goal gets changed to stop, the
	 * status to stopped (and thus through to being deleted) and the
	 * failed information set correctly.
	 */
	TEST_FEATURE ("killed to post-stop for failed process");
	tmp = config->process[PROCESS_POST_STOP];
	config->process[PROCESS_POST_STOP] = fail;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 99;
		}

		job->goal = JOB_START;
		job->state = JOB_KILLED;

		job->blocked = NULL;
		cause->failed = FALSE;

		job->stop_on->value = TRUE;
		job->stop_on->event = cause;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_FREE_TAG (job);

		TEST_DIVERT_STDERR (output) {
			job_change_state (job, JOB_POST_STOP);
		}
		rewind (output);

		TEST_FREE (job);

		TEST_EQ (cause->refs, 0);
		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, TRUE);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopped");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=post-stop");
		TEST_EQ_P (event->env[3], NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_FILE_EQ (output, ("test: Failed to spawn test (#99) "
				       "post-stop process: unable to execute: "
				       "No such file or directory\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	config->process[PROCESS_POST_STOP] = tmp;


	/* Check that a job can move from post-stop to being deleted.  This
	 * should emit the stopped event and clear the cause.
	 */
	TEST_FEATURE ("post-stop to waiting");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
		}

		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;

		job->blocked = NULL;
		cause->failed = FALSE;

		job->stop_on->value = TRUE;
		job->stop_on->event = cause;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 1;

		TEST_FREE_TAG (job);

		job_change_state (job, JOB_WAITING);

		TEST_FREE (job);

		TEST_EQ (cause->refs, 0);
		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, FALSE);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopped");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=main");
		TEST_EQ_STR (event->env[3], "EXIT_STATUS=1");
		TEST_EQ_P (event->env[4], NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_free (event);

		TEST_LIST_EMPTY (events);
	}


	/* Check that if the last active instance is deleted that the
	 * stalled event is emitted.
	 */
	TEST_FEATURE ("post-stop to waiting for last instance");
	TEST_ALLOC_FAIL {
		job_instances = 0;

		TEST_ALLOC_SAFE {
			job = job_new (config);
		}

		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;

		job->blocked = NULL;
		cause->failed = FALSE;

		job->stop_on->value = TRUE;
		job->stop_on->event = cause;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 1;

		TEST_FREE_TAG (job);

		job_change_state (job, JOB_WAITING);

		TEST_FREE (job);

		TEST_EQ (cause->refs, 0);
		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, FALSE);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopped");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=main");
		TEST_EQ_STR (event->env[3], "EXIT_STATUS=1");
		TEST_EQ_P (event->env[4], NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_free (event);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stalled");
		TEST_EQ_P (event->env, NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_free (event);

		TEST_LIST_EMPTY (events);
	}


	/* Check that a job can move from post-stop to starting.  This
	 * should emit the starting event and block on it, as well as clear
	 * any failed state information; but only unblock and unreference the
	 * stop events, the start events should remain referenced while the
	 * environment should be replaced with the new one.
	 */
	TEST_FEATURE ("post-stop to starting");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);

			assert (nih_str_array_add (&(job->env), job,
						   NULL, "FOO=TEA"));
			assert (nih_str_array_add (&(job->env), job,
						   NULL, "BAZ=COFFEE"));

			assert (nih_str_array_add (&(job->start_env), job,
						   NULL, "FOO=BAR"));
			assert (nih_str_array_add (&(job->start_env), job,
						   NULL, "BAZ=BAZ"));

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_ref (cause);
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_POST_STOP;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->stop_on->value = TRUE;
		job->stop_on->event = cause;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		env1 = job->env;
		env2 = job->start_env;

		TEST_FREE_TAG (env1);
		TEST_FREE_TAG (env2);

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 1;

		job_change_state (job, JOB_STARTING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STARTING);

		TEST_EQ (cause->refs, 1);
		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_FREE (env1);

		TEST_NOT_FREE (env2);
		TEST_EQ_P (job->env, env2);
		TEST_EQ_P (job->start_env, NULL);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_EQ (job->stop_on->value, FALSE);
		TEST_EQ_P (job->stop_on->event, NULL);
		TEST_EQ (job->stop_on->blocked, FALSE);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);
		event_unref (cause);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "starting");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_P (event->env[1], NULL);
		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 0);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that when there is no new environment, the old one is left
	 * intact when the job moves from post-stop to starting.
	 */
	TEST_FEATURE ("post-stop to starting without new environment");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);

			assert (nih_str_array_add (&(job->env), job,
						   NULL, "FOO=TEA"));
			assert (nih_str_array_add (&(job->env), job,
						   NULL, "BAZ=COFFEE"));

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_ref (cause);
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_POST_STOP;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->stop_on->value = TRUE;
		job->stop_on->event = cause;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		env1 = job->env;

		TEST_FREE_TAG (env1);

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 1;

		job_change_state (job, JOB_STARTING);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STARTING);

		TEST_EQ (cause->refs, 1);
		TEST_EQ (cause->blockers, 1);
		TEST_EQ (cause->failed, FALSE);

		TEST_NOT_FREE (env1);
		TEST_EQ_P (job->env, env1);
		TEST_EQ_P (job->start_env, NULL);

		TEST_EQ_P (job->blocked, (Event *)events->next);

		TEST_EQ (job->stop_on->value, FALSE);
		TEST_EQ_P (job->stop_on->event, NULL);
		TEST_EQ (job->stop_on->blocked, FALSE);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, cause);
		event_unblock (cause);
		event_unref (cause);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "starting");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_P (event->env[1], NULL);
		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 0);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that if a job tries to move from post-stop to starting too
	 * many times in a given period then it is caught, a message is
	 * output and the job deleted.  We get a stopped event, and any
	 * blocks and references on start or stop events are cleared.
	 */
	TEST_FEATURE ("post-stop to starting too fast");
	config->respawn_limit = 10;
	config->respawn_interval = 1000;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 99;

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = cause;
			event_ref (cause);
			event_block (cause);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_POST_STOP;

		job->blocked = NULL;
		cause->failed = FALSE;

		TEST_FREE_TAG (list);

		job->stop_on->value = TRUE;
		job->stop_on->event = cause;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job->respawn_time = time (NULL);
		job->respawn_count = 10;

		TEST_FREE_TAG (job);

		TEST_DIVERT_STDERR (output) {
			job_change_state (job, JOB_STARTING);
		}
		rewind (output);

		TEST_FREE (job);

		TEST_EQ (cause->refs, 0);
		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, TRUE);

		TEST_FREE (list);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopped");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=respawn");
		TEST_EQ_P (event->env[3], NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		TEST_FILE_EQ (output, ("test: test (#99) respawning too fast, "
				       "stopped\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	config->respawn_limit = 0;
	config->respawn_interval = JOB_DEFAULT_RESPAWN_INTERVAL;


	/* Check that a job which has a better replacement can move from
	 * post-stop to waiting, and be removed from the jobs hash table
	 * and replaced by the better one.
	 */
	TEST_FEATURE ("post-stop to waiting for replaced job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			source = conf_source_new (NULL, "/tmp", CONF_JOB_DIR);
			file = conf_file_new (source, "/tmp/test");
			file->job = job_config_new (NULL, "test");
			replacement = file->job;

			job = job_new (config);
		}

		nih_hash_add (jobs, &config->entry);

		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;

		job->blocked = NULL;
		cause->failed = FALSE;

		job->stop_on->value = TRUE;
		job->stop_on->event = cause;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = 1;

		TEST_FREE_TAG (job);

		job_change_state (job, JOB_WAITING);

		TEST_FREE (job);

		TEST_EQ (cause->refs, 0);
		TEST_EQ (cause->blockers, 0);
		TEST_EQ (cause->failed, FALSE);

		event = (Event *)events->next;
		TEST_ALLOC_SIZE (event, sizeof (Event));
		TEST_EQ_STR (event->name, "stopped");
		TEST_EQ_STR (event->env[0], "JOB=test");
		TEST_EQ_STR (event->env[1], "RESULT=failed");
		TEST_EQ_STR (event->env[2], "PROCESS=main");
		TEST_EQ_STR (event->env[3], "EXIT_STATUS=1");
		TEST_EQ_P (event->env[4], NULL);
		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		nih_free (event);

		TEST_LIST_EMPTY (events);

		ptr = (JobConfig *)nih_hash_lookup (jobs, "test");
		TEST_EQ (ptr, replacement);

		file->job = NULL;
		nih_free (replacement);
		nih_free (source);
	}


	/* Check that a job with a deleted source can move from post-stop
	 * to waiting, be removed from the jobs hash table, replaced by
	 * a better one, then freed.
	 */
	TEST_FEATURE ("post-stop to waiting for deleted job");
	source = conf_source_new (NULL, "/tmp", CONF_JOB_DIR);
	file = conf_file_new (source, "/tmp/test");
	file->job = job_config_new (NULL, "test");
	replacement = file->job;

	config->deleted = TRUE;
	job = job_new (config);

	nih_hash_add (jobs, &config->entry);

	job->goal = JOB_STOP;
	job->state = JOB_POST_STOP;

	job->blocked = NULL;
	cause->failed = FALSE;

	job->stop_on->value = TRUE;
	job->stop_on->event = cause;
	event_ref (job->stop_on->event);
	job->stop_on->blocked = TRUE;
	event_block (job->stop_on->event);

	job->failed = TRUE;
	job->failed_process = PROCESS_MAIN;
	job->exit_status = 1;

	TEST_FREE_TAG (config);
	TEST_FREE_TAG (job);

	job_change_state (job, JOB_WAITING);

	TEST_FREE (config);
	TEST_FREE (job);

	TEST_EQ (cause->refs, 0);
	TEST_EQ (cause->blockers, 0);
	TEST_EQ (cause->failed, FALSE);

	event = (Event *)events->next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "stopped");
	TEST_EQ_STR (event->env[0], "JOB=test");
	TEST_EQ_STR (event->env[1], "RESULT=failed");
	TEST_EQ_STR (event->env[2], "PROCESS=main");
	TEST_EQ_STR (event->env[3], "EXIT_STATUS=1");
	TEST_EQ_P (event->env[4], NULL);
	TEST_EQ (event->refs, 0);
	TEST_EQ (event->blockers, 0);
	nih_free (event);

	TEST_LIST_EMPTY (events);

	ptr = (JobConfig *)nih_hash_lookup (jobs, "test");
	TEST_EQ (ptr, replacement);

	file->job = NULL;
	nih_free (replacement);
	nih_free (source);


	fclose (output);
	rmdir (dirname);

	nih_free (cause);
	event_poll ();
}

void
test_next_state (void)
{
	JobConfig *config;
	Job       *job;

	TEST_FUNCTION ("job_next_state");
	config = job_config_new (NULL, "test");
	config->process[PROCESS_MAIN] = job_process_new (config);
	config->process[PROCESS_MAIN]->command = "echo";

	job = job_new (config);

	/* Check that the next state if we're starting a waiting job is
	 * starting.
	 */
	TEST_FEATURE ("with waiting job and a goal of start");
	job->goal = JOB_START;
	job->state = JOB_WAITING;

	TEST_EQ (job_next_state (job), JOB_STARTING);


	/* Check that the next state if we're stopping a starting job is
	 * stpoping.
	 */
	TEST_FEATURE ("with starting job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_STARTING;

	TEST_EQ (job_next_state (job), JOB_STOPPING);


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
	job->pid[PROCESS_MAIN] = 1;

	TEST_EQ (job_next_state (job), JOB_PRE_STOP);


	/* Check that the next state if we're stopping a running job that
	 * has no process is stopping.  This is the stop process if the
	 * process goes away on its own, as called from the child reaper.
	 */
	TEST_FEATURE ("with dead running job and a goal of stop");
	job->goal = JOB_STOP;
	job->state = JOB_RUNNING;
	job->pid[PROCESS_MAIN] = 0;

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


	nih_free (config);
}


void
test_run_process (void)
{
	JobConfig   *config = NULL;
	Job         *job = NULL;
	FILE        *output;
	struct stat  statbuf;
	char         filename[PATH_MAX], buf[80];
	int          ret, status, first;
	siginfo_t    info;

	TEST_FUNCTION ("job_run_process");
	TEST_FILENAME (filename);

	/* Check that we can run a simple command, and have the process id
	 * and state filled in.  We should be able to wait for the pid to
	 * finish and see that it has been run as expected.
	 */
	TEST_FEATURE ("with simple command");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			config = job_config_new (NULL, "test");
			config->process[PROCESS_MAIN] = job_process_new (config);
			config->process[PROCESS_MAIN]->command = nih_sprintf (
				config->process[PROCESS_MAIN],
				"touch %s", filename);

			job = job_new (config);
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
		}

		ret = job_run_process (job, PROCESS_MAIN);
		TEST_EQ (ret, 0);

		TEST_NE (job->pid[PROCESS_MAIN], 0);

		waitpid (job->pid[PROCESS_MAIN], NULL, 0);
		TEST_EQ (stat (filename, &statbuf), 0);

		unlink (filename);
		nih_free (config);
	}


	/* Check that we can run a command that requires a shell to be
	 * intepreted correctly, a shell should automatically be used to
	 * make this work.  Check the contents of a file we'll create to
	 * check that a shell really was used.
	 */
	TEST_FEATURE ("with shell command");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			config = job_config_new (NULL, "test");
			config->process[PROCESS_MAIN] = job_process_new (config);
			config->process[PROCESS_MAIN]->command = nih_sprintf (
				config->process[PROCESS_MAIN],
				"echo $$ > %s", filename);

			job = job_new (config);
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
		}

		ret = job_run_process (job, PROCESS_MAIN);
		TEST_EQ (ret, 0);

		TEST_NE (job->pid[PROCESS_MAIN], 0);

		waitpid (job->pid[PROCESS_MAIN], NULL, 0);
		TEST_EQ (stat (filename, &statbuf), 0);

		/* Filename should contain the pid */
		output = fopen (filename, "r");
		sprintf (buf, "%d\n", job->pid[PROCESS_MAIN]);
		TEST_FILE_EQ (output, buf);
		TEST_FILE_END (output);
		fclose (output);
		unlink (filename);

		nih_free (config);
	}

	/* Check that we can run a small shell script, and that it's run
	 * by using the shell directly and passing the script in on the
	 * command-line.
	 */
	TEST_FEATURE ("with small script");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			config = job_config_new (NULL, "test");
			config->process[PROCESS_MAIN] = job_process_new (config);
			config->process[PROCESS_MAIN]->script = TRUE;
			config->process[PROCESS_MAIN]->command = nih_sprintf (
				config->process[PROCESS_MAIN],
				"exec > %s\necho $0\necho $@", filename);

			job = job_new (config);
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
		}

		ret = job_run_process (job, PROCESS_MAIN);
		TEST_EQ (ret, 0);

		TEST_NE (job->pid[PROCESS_MAIN], 0);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		output = fopen (filename, "r");
		TEST_FILE_EQ (output, "/bin/sh\n");
		TEST_FILE_EQ (output, "\n");
		TEST_FILE_END (output);
		fclose (output);
		unlink (filename);

		nih_free (config);
	}


	/* Check that shell scripts are run with the -e option set, so that
	 * any failing command causes the entire script to fail.
	 */
	TEST_FEATURE ("with script that will fail");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			config = job_config_new (NULL, "test");
			config->process[PROCESS_MAIN] = job_process_new (config);
			config->process[PROCESS_MAIN]->script = TRUE;
			config->process[PROCESS_MAIN]->command = nih_sprintf (
				config->process[PROCESS_MAIN],
				"exec > %s\ntest -d %s\necho oops",
				filename, filename);

			job = job_new (config);
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
		}

		ret = job_run_process (job, PROCESS_MAIN);
		TEST_EQ (ret, 0);

		TEST_NE (job->pid[PROCESS_MAIN], 0);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 1);

		output = fopen (filename, "r");
		TEST_FILE_END (output);
		fclose (output);
		unlink (filename);

		nih_free (config);
	}


	/* Check that a job is run with the environment from its env member,
	 * with the job name and id appended to it.
	 */
	TEST_FEATURE ("with environment");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			config = job_config_new (NULL, "test");
			config->process[PROCESS_MAIN] = job_process_new (config);
			config->process[PROCESS_MAIN]->command = nih_sprintf (
				config->process[PROCESS_MAIN],
				"%s %s", argv0, filename);

			job = job_new (config);
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;

			assert (nih_str_array_add (&job->env, job, NULL, "FOO=BAR"));
			assert (nih_str_array_add (&job->env, job, NULL, "BAR=BAZ"));
		}

		ret = job_run_process (job, PROCESS_MAIN);
		TEST_EQ (ret, 0);

		TEST_NE (job->pid[PROCESS_MAIN], 0);

		waitpid (job->pid[PROCESS_MAIN], NULL, 0);
		TEST_EQ (stat (filename, &statbuf), 0);

		/* Read back the environment to make sure it matched that from
		 * the job.
		 */
		output = fopen (filename, "r");
		TEST_FILE_EQ (output, "FOO=BAR\n");
		TEST_FILE_EQ (output, "BAR=BAZ\n");
		TEST_FILE_EQ (output, "UPSTART_JOB=test\n");
		TEST_FILE_EQ_N (output, "UPSTART_JOB_ID=");
		fclose (output);
		unlink (filename);

		nih_free (config);
	}


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
			config = job_config_new (NULL, "test");
			config->process[PROCESS_MAIN] = job_process_new (config);
			config->process[PROCESS_MAIN]->script = TRUE;
			config->process[PROCESS_MAIN]->command = nih_alloc (
				config->process[PROCESS_MAIN], 4096);
			sprintf (config->process[PROCESS_MAIN]->command,
				 "exec > %s\necho $0\necho $@\n", filename);
			while (strlen (config->process[PROCESS_MAIN]->command) < 4000)
				strcat (config->process[PROCESS_MAIN]->command,
					"# this just bulks it out a bit");

			job = job_new (config);
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
		}

		ret = job_run_process (job, PROCESS_MAIN);
		TEST_EQ (ret, 0);

		TEST_NE (job->pid[PROCESS_MAIN], 0);

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

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		output = fopen (filename, "r");
		TEST_FILE_EQ_N (output, "/dev/fd/");
		TEST_FILE_EQ (output, "\n");
		TEST_FILE_END (output);
		fclose (output);
		unlink (filename);

		nih_free (config);
	}


no_devfd:
	/* Check that if we're running a non-daemon job, the trace state
	 * is reset and no process trace is established.
	 */
	TEST_FEATURE ("with non-daemon job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			config = job_config_new (NULL, "test");
			config->process[PROCESS_MAIN] = job_process_new (config);
			config->process[PROCESS_MAIN]->command = "true";

			job = job_new (config);
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;

			job->trace_forks = 2;
			job->trace_state = TRACE_NORMAL;
		}

		ret = job_run_process (job, PROCESS_MAIN);
		TEST_EQ (ret, 0);

		TEST_EQ (job->trace_forks, 0);
		TEST_EQ (job->trace_state, TRACE_NONE);

		TEST_NE (job->pid[PROCESS_MAIN], 0);

		assert0 (waitid (P_PID, job->pid[PROCESS_MAIN], &info,
				 WEXITED | WSTOPPED));
		TEST_EQ (info.si_pid, job->pid[PROCESS_MAIN]);
		TEST_EQ (info.si_code, CLD_EXITED);
		TEST_EQ (info.si_status, 0);

		nih_free (config);
	}


	/* Check that if we're running a script for a daemon job, the
	 * trace state is reset and no process trace is established.
	 */
	TEST_FEATURE ("with script for daemon job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			config = job_config_new (NULL, "test");
			config->process[PROCESS_PRE_START] = job_process_new (config);
			config->process[PROCESS_PRE_START]->command = "true";

			job = job_new (config);
			job->goal = JOB_START;
			job->state = JOB_PRE_START;

			job->trace_forks = 2;
			job->trace_state = TRACE_NORMAL;
		}

		ret = job_run_process (job, PROCESS_PRE_START);
		TEST_EQ (ret, 0);

		TEST_EQ (job->trace_forks, 0);
		TEST_EQ (job->trace_state, TRACE_NONE);

		TEST_NE (job->pid[PROCESS_PRE_START], 0);

		assert0 (waitid (P_PID, job->pid[PROCESS_PRE_START], &info,
				 WEXITED | WSTOPPED));
		TEST_EQ (info.si_pid, job->pid[PROCESS_PRE_START]);
		TEST_EQ (info.si_code, CLD_EXITED);
		TEST_EQ (info.si_status, 0);

		nih_free (config);
	}


	/* Check that if we're running a daemon job, the trace state
	 * is reset and a process trace is established so that we can
	 * follow the forks.
	 */
	TEST_FEATURE ("with daemon job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			config = job_config_new (NULL, "test");
			config->wait_for = JOB_WAIT_DAEMON;
			config->process[PROCESS_MAIN] = job_process_new (config);
			config->process[PROCESS_MAIN]->command = "true";

			job = job_new (config);
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;

			job->trace_forks = 2;
			job->trace_state = TRACE_NORMAL;
		}

		ret = job_run_process (job, PROCESS_MAIN);
		TEST_EQ (ret, 0);

		TEST_EQ (job->trace_forks, 0);
		TEST_EQ (job->trace_state, TRACE_NEW);

		TEST_NE (job->pid[PROCESS_MAIN], 0);

		assert0 (waitid (P_PID, job->pid[PROCESS_MAIN], &info,
				 WEXITED | WSTOPPED));
		TEST_EQ (info.si_pid, job->pid[PROCESS_MAIN]);
		TEST_EQ (info.si_code, CLD_TRAPPED);
		TEST_EQ (info.si_status, SIGTRAP);

		assert0 (ptrace (PTRACE_DETACH, job->pid[PROCESS_MAIN],
				 NULL, 0));

		assert0 (waitid (P_PID, job->pid[PROCESS_MAIN], &info,
				 WEXITED | WSTOPPED));
		TEST_EQ (info.si_pid, job->pid[PROCESS_MAIN]);
		TEST_EQ (info.si_code, CLD_EXITED);
		TEST_EQ (info.si_status, 0);

		nih_free (config);
	}


	/* Check that if we're running a forking job, the trace state
	 * is reset and a process trace is established so that we can
	 * follow the fork.
	 */
	TEST_FEATURE ("with forking job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			config = job_config_new (NULL, "test");
			config->wait_for = JOB_WAIT_FORK;
			config->process[PROCESS_MAIN] = job_process_new (config);
			config->process[PROCESS_MAIN]->command = "true";

			job = job_new (config);
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;

			job->trace_forks = 2;
			job->trace_state = TRACE_NORMAL;
		}

		ret = job_run_process (job, PROCESS_MAIN);
		TEST_EQ (ret, 0);

		TEST_EQ (job->trace_forks, 0);
		TEST_EQ (job->trace_state, TRACE_NEW);

		TEST_NE (job->pid[PROCESS_MAIN], 0);

		assert0 (waitid (P_PID, job->pid[PROCESS_MAIN], &info,
				 WEXITED | WSTOPPED));
		TEST_EQ (info.si_pid, job->pid[PROCESS_MAIN]);
		TEST_EQ (info.si_code, CLD_TRAPPED);
		TEST_EQ (info.si_status, SIGTRAP);

		assert0 (ptrace (PTRACE_DETACH, job->pid[PROCESS_MAIN],
				 NULL, 0));

		assert0 (waitid (P_PID, job->pid[PROCESS_MAIN], &info,
				 WEXITED | WSTOPPED));
		TEST_EQ (info.si_pid, job->pid[PROCESS_MAIN]);
		TEST_EQ (info.si_code, CLD_EXITED);
		TEST_EQ (info.si_status, 0);

		nih_free (config);
	}


	/* Check that if we try and run a command that doesn't exist,
	 * job_run_process() raises a ProcessError and the command doesn't
	 * have any stored process id for it.
	 */
	TEST_FEATURE ("with no such file");
	output = tmpfile ();

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			config = job_config_new (NULL, "test");
			config->process[PROCESS_MAIN] = job_process_new (config);
			config->process[PROCESS_MAIN]->command = filename;

			job = job_new (config);
			job->id = 1;
			job->goal = JOB_START;
			job->state = JOB_SPAWNED;
		}

		TEST_DIVERT_STDERR (output) {
			ret = job_run_process (job, PROCESS_MAIN);
		}
		rewind (output);
		TEST_LT (ret, 0);

		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_FILE_EQ (output, ("test: Failed to spawn test (#1) main "
				       "process: unable to execute: "
				       "No such file or directory\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (config);
	}
}


void
test_kill_process (void)
{
	JobConfig *config;
	Job       *job = NULL;
	NihTimer  *timer;
	pid_t      pid;
	int        status;

	TEST_FUNCTION ("job_kill_process");
	config = job_config_new (NULL, "test");
	config->kill_timeout = 1000;
	config->respawn_limit = 0;

	config->process[PROCESS_MAIN] = job_process_new (config);
	config->process[PROCESS_MAIN]->command = nih_strdup (
		config->process[PROCESS_MAIN], "echo");


	/* Check that an easily killed process goes away with just a single
	 * call to job_kill_process, having received the TERM signal.
	 * A kill timer should be set to handle the case where the child
	 * doesn't get reaped.
	 */
	TEST_FEATURE ("with easily killed process");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
		}

		job->goal = JOB_STOP;
		job->state = JOB_KILLED;
		TEST_CHILD (job->pid[PROCESS_MAIN]) {
			pause ();
		}
		pid = job->pid[PROCESS_MAIN];
		setpgid (pid, pid);

		job_kill_process (job, PROCESS_MAIN);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_KILLED);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFSIGNALED (status));
		TEST_EQ (WTERMSIG (status), SIGTERM);

		TEST_NE_P (job->kill_timer, NULL);
		TEST_ALLOC_SIZE (job->kill_timer, sizeof (NihTimer));
		TEST_ALLOC_PARENT (job->kill_timer, job);
		TEST_GE (job->kill_timer->due, time (NULL) + 950);
		TEST_LE (job->kill_timer->due, time (NULL) + 1000);

		nih_free (job->kill_timer);
		job->kill_timer = NULL;

		nih_free (job);

		event_poll ();
	}


	/* Check that a process that's hard to kill doesn't go away, but
	 * that the kill timer sends the KILL signal which should finally
	 * get rid of it.
	 */
	TEST_FEATURE ("with hard to kill process");
	TEST_ALLOC_FAIL {
		int wait_fd = 0;

		TEST_ALLOC_SAFE {
			job = job_new (config);
		}

		job->goal = JOB_STOP;
		job->state = JOB_KILLED;
		TEST_CHILD_WAIT (job->pid[PROCESS_MAIN], wait_fd) {
			struct sigaction act;

			act.sa_handler = SIG_IGN;
			act.sa_flags = 0;
			sigemptyset (&act.sa_mask);
			sigaction (SIGTERM, &act, NULL);

			TEST_CHILD_RELEASE (wait_fd);

			for (;;)
				pause ();
		}
		pid = job->pid[PROCESS_MAIN];
		setpgid (pid, pid);

		job_kill_process (job, PROCESS_MAIN);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_KILLED);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);

		TEST_EQ (kill (job->pid[PROCESS_MAIN], 0), 0);

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
		TEST_EQ (job->pid[PROCESS_MAIN], pid);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFSIGNALED (status));
		TEST_EQ (WTERMSIG (status), SIGKILL);

		TEST_EQ_P (job->kill_timer, NULL);

		nih_free (job);

		event_poll ();
	}

	nih_free (config);
}


void
test_child_handler (void)
{
	ConfSource   *source;
	ConfFile     *file;
	JobConfig    *config;
	Job          *job = NULL;
	NihList      *list;
	NihListEntry *entry;
	Event        *event;
	FILE         *output;
	int           exitcodes[2] = { 100, SIGINT << 8 }, status;
	pid_t         pid;
	siginfo_t     info;

	TEST_FUNCTION ("job_child_handler");
	program_name = "test";
	output = tmpfile ();

	source = conf_source_new (NULL, "/tmp", CONF_JOB_DIR);
	file = conf_file_new (source, "/tmp/test");
	file->job = config = job_config_new (NULL, "test");
	config->process[PROCESS_MAIN] = job_process_new (config);
	config->process[PROCESS_MAIN]->command = "echo";

	config->start_on = event_operator_new (config, EVENT_MATCH,
					       "foo", NULL);
	config->stop_on = event_operator_new (config, EVENT_MATCH,
					      "foo", NULL);
	nih_hash_add (jobs, &config->entry);

	event = event_new (NULL, "foo", NULL);


	/* Check that the child handler can be called with a pid that doesn't
	 * match the job, and that the job state doesn't change.
	 */
	TEST_FEATURE ("with unknown pid");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = event;
			event_ref (event);
			event_block (event);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;

		TEST_FREE_TAG (list);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_child_handler (NULL, 999, NIH_CHILD_EXITED, 0);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);

		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, event);
		event_unblock (event);
		event_unref (event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that we can handle the running task of the job terminating,
	 * which should set the goal to stop and transition a state change
	 * into the stopping state.  This should not be considered a failure.
	 */
	TEST_FEATURE ("with running process");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = event;
			event_ref (event);
			event_block (event);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;

		TEST_FREE_TAG (list);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_child_handler (NULL, 1, NIH_CHILD_EXITED, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, event);
		event_unblock (event);
		event_unref (event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that we can handle a running task of the job after it's been
	 * sent the TERM signal and a kill timer set.  The kill timer should
	 * be cancelled and freed, and since we killed it, the job should
	 * still not be considered failed.
	 */
	TEST_FEATURE ("with kill timer");
	TEST_ALLOC_FAIL {
		NihTimer *timer = NULL;

		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;
		}

		job->goal = JOB_STOP;
		job->state = JOB_KILLED;
		job->pid[PROCESS_MAIN] = 1;

		job->stop_on->value = TRUE;
		job->stop_on->event = event;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_ALLOC_SAFE {
			timer = (void *) nih_strdup (job, "test");
		}

		TEST_FREE_TAG (timer);
		job->kill_timer = timer;

		TEST_FREE_TAG (job);

		job_child_handler (NULL, 1, NIH_CHILD_EXITED, 0);

		TEST_FREE (timer);
		TEST_FREE (job);

		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->failed, FALSE);
	}


	/* Check that we can handle the pre-start process of the job exiting,
	 * and if it terminates with a good error code, end up in the running
	 * state.
	 */
	TEST_FEATURE ("with pre-start process");
	config->process[PROCESS_PRE_START] = job_process_new (config);
	config->process[PROCESS_PRE_START]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = event;
			event_ref (event);
			event_block (event);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->pid[PROCESS_MAIN] = 0;
		job->pid[PROCESS_PRE_START] = 1;

		TEST_FREE_TAG (list);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_child_handler (NULL, 1, NIH_CHILD_EXITED, 0);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid[PROCESS_PRE_START], 0);
		TEST_GT (job->pid[PROCESS_MAIN], 0);

		waitpid (job->pid[PROCESS_MAIN], NULL, 0);

		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, event);
		event_unblock (event);
		event_unref (event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	nih_free (config->process[PROCESS_PRE_START]);
	config->process[PROCESS_PRE_START] = NULL;


	/* Check that we can handle a failing pre-start process of the job,
	 * which changes the goal to stop and transitions a state change in
	 * that direction to the stopping state.  An error should be emitted
	 * and the job and event should be marked as failed.
	 */
	TEST_FEATURE ("with failed pre-start process");
	config->process[PROCESS_PRE_START] = job_process_new (config);
	config->process[PROCESS_PRE_START]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = event;
			event_ref (event);
			event_block (event);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->pid[PROCESS_PRE_START] = 1;

		TEST_FREE_TAG (list);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_handler (NULL, 1, NIH_CHILD_EXITED, 1);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_PRE_START], 0);

		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->failed, TRUE);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_FREE (list);
		TEST_EQ_P (job->blocking, NULL);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_PRE_START);
		TEST_EQ (job->exit_status, 1);

		TEST_FILE_EQ (output, ("test: test (#1) pre-start process (1) "
				       "terminated with status 1\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	nih_free (config->process[PROCESS_PRE_START]);
	config->process[PROCESS_PRE_START] = NULL;


	/* Check that we can handle a killed starting task, which should
	 * act as if it failed.  A different error should be output and
	 * the failed exit status should contain the signal and the high bit.
	 */
	TEST_FEATURE ("with killed pre-start process");
	config->process[PROCESS_PRE_START] = job_process_new (config);
	config->process[PROCESS_PRE_START]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = event;
			event_ref (event);
			event_block (event);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_PRE_START;
		job->pid[PROCESS_PRE_START] = 1;

		TEST_FREE_TAG (list);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_handler (NULL, 1, NIH_CHILD_KILLED, SIGTERM);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_PRE_START], 0);

		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->failed, TRUE);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_FREE (list);
		TEST_EQ_P (job->blocking, NULL);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_PRE_START);
		TEST_EQ (job->exit_status, SIGTERM << 8);

		TEST_FILE_EQ (output, ("test: test (#1) pre-start process (1) "
				       "killed by TERM signal\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	nih_free (config->process[PROCESS_PRE_START]);
	config->process[PROCESS_PRE_START] = NULL;


	/* Check that we can catch the running task failing, and if the job
	 * is to be respawned, go into the stopping state but don't change
	 * the goal to stop.
	 *
	 * This should also emit a warning, but should not set the failed
	 * state since we're dealing with it.
	 */
	TEST_FEATURE ("with respawn of running process");
	config->respawn = TRUE;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = event;
			event_ref (event);
			event_block (event);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;

		TEST_FREE_TAG (list);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_handler (NULL, 1, NIH_CHILD_EXITED, 0);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, event);
		event_unblock (event);
		event_unref (event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test (#1) main process ended, "
				       "respawning\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	config->respawn = FALSE;


	/* Check that we can catch a running task exiting with a "normal"
	 * exit code, and even if it's marked respawn, set the goal to
	 * stop and transition into the stopping state.
	 */
	TEST_FEATURE ("with normal exit of running respawn process");
	config->respawn = TRUE;
	config->normalexit = exitcodes;
	config->normalexit_len = 1;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = event;
			event_ref (event);
			event_block (event);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;

		TEST_FREE_TAG (list);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_handler (NULL, 1, NIH_CHILD_EXITED, 100);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, event);
		event_unblock (event);
		event_unref (event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test (#1) main process (1) "
				       "terminated with status 100\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	config->respawn = FALSE;
	config->normalexit = NULL;
	config->normalexit_len = 0;


	/* Check that a running task that fails with an exit status not
	 * listed in normalexit causes the job to be marked as failed.
	 */
	TEST_FEATURE ("with abnormal exit of running process");
	config->normalexit = exitcodes;
	config->normalexit_len = 2;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = event;
			event_ref (event);
			event_block (event);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;

		TEST_FREE_TAG (list);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_handler (NULL, 1, NIH_CHILD_EXITED, 99);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->failed, TRUE);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_FREE (list);
		TEST_EQ_P (job->blocking, NULL);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, 99);

		TEST_FILE_EQ (output, ("test: test (#1) main process (1) "
				       "terminated with status 99\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	config->normalexit = NULL;
	config->normalexit_len = 0;


	/* Check that a running task that fails doesn't mark the job or
	 * event as failed if the goal was already to stop the job (since
	 * it's probably failed because of the TERM or KILL signal).
	 */
	TEST_FEATURE ("with killed running process");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;
		}

		job->goal = JOB_STOP;
		job->state = JOB_KILLED;
		job->pid[PROCESS_MAIN] = 1;

		job->stop_on->value = TRUE;
		job->stop_on->event = event;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_FREE_TAG (job);

		TEST_DIVERT_STDERR (output) {
			job_child_handler (NULL, 1, NIH_CHILD_KILLED, SIGTERM);
		}
		rewind (output);

		TEST_FREE (job);

		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->failed, FALSE);

		TEST_FILE_EQ (output, ("test: test (#1) main process (1) "
				       "killed by TERM signal\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}


	/* Check that a running task that fails with an exit status
	 * listed in normalexit does not cause the job to be marked as
	 * failed, but instead just stops it normally.
	 */
	TEST_FEATURE ("with normal exit of running process");
	config->normalexit = exitcodes;
	config->normalexit_len = 2;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = event;
			event_ref (event);
			event_block (event);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;

		TEST_FREE_TAG (list);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_handler (NULL, 1, NIH_CHILD_EXITED, 100);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, event);
		event_unblock (event);
		event_unref (event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test (#1) main process (1) "
				       "terminated with status 100\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	config->normalexit = NULL;
	config->normalexit_len = 0;


	/* Check that a running task that fails with an signal
	 * listed in normalexit does not cause the job to be marked as
	 * failed, but instead just stops it normally.
	 */
	TEST_FEATURE ("with normal signal killed running process");
	config->normalexit = exitcodes;
	config->normalexit_len = 2;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = event;
			event_ref (event);
			event_block (event);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;

		TEST_FREE_TAG (list);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_handler (NULL, 1, NIH_CHILD_KILLED, SIGINT);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, event);
		event_unblock (event);
		event_unref (event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test (#1) main process (1) "
				       "killed by INT signal\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	config->normalexit = NULL;
	config->normalexit_len = 0;


	/* A running task exiting with the zero exit code is considered
	 * a normal termination if not marked respawn.
	 */
	TEST_FEATURE ("with running task and zero exit");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = event;
			event_ref (event);
			event_block (event);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_RUNNING;
		job->pid[PROCESS_MAIN] = 1;

		TEST_FREE_TAG (list);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_child_handler (NULL, 1, NIH_CHILD_EXITED, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, event);
		event_unblock (event);
		event_unref (event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that we can handle the post-stop process of the job exiting,
	 * and end up in the waiting state.
	 */
	TEST_FEATURE ("with post-stop process");
	config->process[PROCESS_POST_STOP] = job_process_new (config);
	config->process[PROCESS_POST_STOP]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;
		}

		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;
		job->pid[PROCESS_POST_STOP] = 1;

		job->stop_on->value = TRUE;
		job->stop_on->event = event;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_FREE_TAG (job);

		job_child_handler (NULL, 1, NIH_CHILD_EXITED, 0);

		TEST_FREE (job);

		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->failed, FALSE);
	}

	nih_free (config->process[PROCESS_POST_STOP]);
	config->process[PROCESS_POST_STOP] = NULL;


	/* Check that we can handle a failing post-stop process of the job,
	 * which should get marked as failed if the job hasn't been already.
	 */
	TEST_FEATURE ("with failed post-stop process");
	config->process[PROCESS_POST_STOP] = job_process_new (config);
	config->process[PROCESS_POST_STOP]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;
		}

		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;
		job->pid[PROCESS_POST_STOP] = 1;

		job->stop_on->value = TRUE;
		job->stop_on->event = event;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_FREE_TAG (job);

		TEST_DIVERT_STDERR (output) {
			job_child_handler (NULL, 1, NIH_CHILD_EXITED, 1);
		}
		rewind (output);

		TEST_FREE (job);

		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->failed, TRUE);

		TEST_FILE_EQ (output, ("test: test (#1) post-stop process (1) "
				       "terminated with status 1\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	nih_free (config->process[PROCESS_POST_STOP]);
	config->process[PROCESS_POST_STOP] = NULL;


	/* Check that a failing stopping task doesn't overwrite the record
	 * of a failing earlier task.
	 */
	TEST_FEATURE ("with stopping task failure after failure");
	config->process[PROCESS_POST_STOP] = job_process_new (config);
	config->process[PROCESS_POST_STOP]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;
		}

		job->goal = JOB_STOP;
		job->state = JOB_POST_STOP;
		job->pid[PROCESS_POST_STOP] = 1;

		job->stop_on->value = TRUE;
		job->stop_on->event = event;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		job->blocked = NULL;
		event->failed = TRUE;

		job->failed = TRUE;
		job->failed_process = PROCESS_MAIN;
		job->exit_status = SIGSEGV << 8;

		TEST_FREE_TAG (job);

		TEST_DIVERT_STDERR (output) {
			job_child_handler (NULL, 1, NIH_CHILD_EXITED, 1);
		}
		rewind (output);

		TEST_FREE (job);

		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->failed, TRUE);

		TEST_FILE_EQ (output, ("test: test (#1) post-stop process (1) "
				       "terminated with status 1\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);
	}

	nih_free (config->process[PROCESS_POST_STOP]);
	config->process[PROCESS_POST_STOP] = NULL;


	/* Check that we can handle the post-start task of the job exiting,
	 * the exit status should be ignored and the job transitioned into
	 * the running state.  The pid of the job shouldn't be cleared,
	 * but the aux pid should be.
	 */
	TEST_FEATURE ("with post-start process");
	config->process[PROCESS_POST_START] = job_process_new (config);
	config->process[PROCESS_POST_START]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = event;
			event_ref (event);
			event_block (event);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_POST_START;
		job->pid[PROCESS_MAIN] = 1;
		job->pid[PROCESS_POST_START] = 2;

		TEST_FREE_TAG (list);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_handler (NULL, 2, NIH_CHILD_EXITED, 1);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);
		TEST_EQ (job->pid[PROCESS_POST_START], 0);

		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, event);
		event_unblock (event);
		event_unref (event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test (#1) post-start process (2) "
				       "terminated with status 1\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	nih_free (config->process[PROCESS_POST_START]);
	config->process[PROCESS_POST_START] = NULL;


	/* Check that we can handle the running task of the job exiting, even
	 * if it dies during the post-start state, which should set the goal to
	 * stop and transition a state change into the stopping state.
	 */
	TEST_FEATURE ("with running process in post-start state");
	config->process[PROCESS_POST_START] = job_process_new (config);
	config->process[PROCESS_POST_START]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = event;
			event_ref (event);
			event_block (event);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_POST_START;
		job->pid[PROCESS_MAIN] = 1;

		TEST_FREE_TAG (list);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_child_handler (NULL, 1, NIH_CHILD_EXITED, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, event);
		event_unblock (event);
		event_unref (event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	nih_free (config->process[PROCESS_POST_START]);
	config->process[PROCESS_POST_START] = NULL;


	/* Check that we can handle the running task of the job exiting while
	 * there is a post-start script running; this should only set the goal
	 * to stop since we also have to wait for the post-start script to
	 * stop.
	 */
	TEST_FEATURE ("with running process while post-start running");
	config->process[PROCESS_POST_START] = job_process_new (config);
	config->process[PROCESS_POST_START]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = event;
			event_ref (event);
			event_block (event);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_POST_START;
		job->pid[PROCESS_MAIN] = 1;
		job->pid[PROCESS_POST_START] = 2;

		TEST_FREE_TAG (list);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_child_handler (NULL, 1, NIH_CHILD_EXITED, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_POST_START);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);
		TEST_EQ (job->pid[PROCESS_POST_START], 2);

		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, event);
		event_unblock (event);
		event_unref (event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	nih_free (config->process[PROCESS_POST_START]);
	config->process[PROCESS_POST_START] = NULL;


	/* Check that we can handle the running process exiting before the
	 * post-start process finishes.  This should mark the job to be
	 * stopped, but not change the state, handling the post-start process
	 * exiting afterwards should change the state.
	 */
	TEST_FEATURE ("with running then post-start process");
	config->process[PROCESS_POST_START] = job_process_new (config);
	config->process[PROCESS_POST_START]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = event;
			event_ref (event);
			event_block (event);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_POST_START;
		job->pid[PROCESS_MAIN] = 1;
		job->pid[PROCESS_POST_START] = 2;

		TEST_FREE_TAG (list);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_child_handler (NULL, 1, NIH_CHILD_EXITED, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_POST_START);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);
		TEST_EQ (job->pid[PROCESS_POST_START], 2);

		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		job_child_handler (NULL, 2, NIH_CHILD_EXITED, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);
		TEST_EQ (job->pid[PROCESS_POST_START], 0);

		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, event);
		event_unblock (event);
		event_unref (event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	nih_free (config->process[PROCESS_POST_START]);
	config->process[PROCESS_POST_START] = NULL;


	/* Check that we can handle a failed running process before the
	 * post-start process finishes.  This should mark the job to be
	 * stopped, but not change the state, then handling the post-start
	 * process exiting afterwards should change the state.
	 */
	TEST_FEATURE ("with failed running then post-start process");
	config->process[PROCESS_POST_START] = job_process_new (config);
	config->process[PROCESS_POST_START]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = event;
			event_ref (event);
			event_block (event);
			nih_list_add (job->blocking, &entry->entry);
		}

		job->goal = JOB_START;
		job->state = JOB_POST_START;
		job->pid[PROCESS_MAIN] = 1;
		job->pid[PROCESS_POST_START] = 2;

		TEST_FREE_TAG (list);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_handler (NULL, 1, NIH_CHILD_KILLED, SIGSEGV);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_POST_START);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);
		TEST_EQ (job->pid[PROCESS_POST_START], 2);

		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->failed, TRUE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_FREE (list);
		TEST_EQ_P (job->blocking, NULL);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, SIGSEGV << 8);

		TEST_FILE_EQ (output, ("test: test (#1) main process (1) "
				       "killed by SEGV signal\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		job_child_handler (NULL, 2, NIH_CHILD_EXITED, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);
		TEST_EQ (job->pid[PROCESS_POST_START], 0);

		TEST_EQ (event->refs, 0);
		TEST_EQ (event->blockers, 0);
		TEST_EQ (event->failed, TRUE);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_FREE (list);
		TEST_EQ_P (job->blocking, NULL);

		TEST_EQ (job->failed, TRUE);
		TEST_EQ (job->failed_process, PROCESS_MAIN);
		TEST_EQ (job->exit_status, SIGSEGV << 8);

		nih_free (job);
	}

	nih_free (config->process[PROCESS_POST_START]);
	config->process[PROCESS_POST_START] = NULL;


	/* Check that we can handle the pre-stop task of the job exiting, the
	 * exit status should be ignored and the job transitioned into
	 * the stopping state.  The pid of the job shouldn't be cleared,
	 * but the aux pid should be.
	 */
	TEST_FEATURE ("with pre-stop process");
	config->process[PROCESS_PRE_STOP] = job_process_new (config);
	config->process[PROCESS_PRE_STOP]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;
		}

		job->goal = JOB_STOP;
		job->state = JOB_PRE_STOP;
		job->pid[PROCESS_MAIN] = 1;
		job->pid[PROCESS_PRE_STOP] = 2;

		job->stop_on->value = TRUE;
		job->stop_on->event = event;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_handler (NULL, 2, NIH_CHILD_EXITED, 1);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);
		TEST_EQ (job->pid[PROCESS_PRE_STOP], 0);

		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_EQ (job->stop_on->value, TRUE);
		TEST_EQ_P (job->stop_on->event, event);
		TEST_EQ (job->stop_on->blocked, TRUE);

		event_operator_reset (job->stop_on);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		TEST_FILE_EQ (output, ("test: test (#1) pre-stop process (2) "
				       "terminated with status 1\n"));
		TEST_FILE_END (output);
		TEST_FILE_RESET (output);

		nih_free (job);
	}

	nih_free (config->process[PROCESS_PRE_STOP]);
	config->process[PROCESS_PRE_STOP] = NULL;


	/* Check that we can handle the running task of the job exiting, even
	 * if it dies during the pre-stop state, which transition a state
	 * change into the stopping state.
	 */
	TEST_FEATURE ("with running process in pre-stop state");
	config->process[PROCESS_PRE_STOP] = job_process_new (config);
	config->process[PROCESS_PRE_STOP]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;
		}

		job->goal = JOB_STOP;
		job->state = JOB_PRE_STOP;
		job->pid[PROCESS_MAIN] = 1;

		job->stop_on->value = TRUE;
		job->stop_on->event = event;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_child_handler (NULL, 1, NIH_CHILD_EXITED, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_STOPPING);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);

		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_NE_P (job->blocked, NULL);
		TEST_EQ (job->blocked->refs, 1);
		event_unref (job->blocked);

		TEST_EQ (job->stop_on->value, TRUE);
		TEST_EQ_P (job->stop_on->event, event);
		TEST_EQ (job->stop_on->blocked, TRUE);

		event_operator_reset (job->stop_on);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	nih_free (config->process[PROCESS_PRE_STOP]);
	config->process[PROCESS_PRE_STOP] = NULL;


	/* Check that we can handle the running task of the job exiting while
	 * there is a pre-stop script running; this should have no other effect
	 * since we also have to wait for the pre-stop script to stop.
	 */
	TEST_FEATURE ("with running process while pre-stop running");
	config->process[PROCESS_PRE_STOP] = job_process_new (config);
	config->process[PROCESS_PRE_STOP]->command = "echo";

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;
		}

		job->goal = JOB_STOP;
		job->state = JOB_PRE_STOP;
		job->pid[PROCESS_MAIN] = 1;
		job->pid[PROCESS_PRE_STOP] = 2;

		job->stop_on->value = TRUE;
		job->stop_on->event = event;
		event_ref (job->stop_on->event);
		job->stop_on->blocked = TRUE;
		event_block (job->stop_on->event);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		job_child_handler (NULL, 1, NIH_CHILD_EXITED, 0);

		TEST_EQ (job->goal, JOB_STOP);
		TEST_EQ (job->state, JOB_PRE_STOP);
		TEST_EQ (job->pid[PROCESS_MAIN], 0);
		TEST_EQ (job->pid[PROCESS_PRE_STOP], 2);

		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_EQ (job->stop_on->value, TRUE);
		TEST_EQ_P (job->stop_on->event, event);
		TEST_EQ (job->stop_on->blocked, TRUE);

		event_operator_reset (job->stop_on);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	nih_free (config->process[PROCESS_PRE_STOP]);
	config->process[PROCESS_PRE_STOP] = NULL;


#if HAVE_VALGRIND_VALGRIND_H
	/* These tests fail when running under valgrind.
	 */
	if (! RUNNING_ON_VALGRIND) {
#endif
	/* Check that we ignore a process stopping on a signal if it isn't
	 * the main process of the job.
	 */
	TEST_FEATURE ("with stopped non-main process");
	config->wait_for = JOB_WAIT_STOP;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = event;
			event_ref (event);
			event_block (event);
			nih_list_add (job->blocking, &entry->entry);
		}

		TEST_CHILD (pid) {
			raise (SIGSTOP);
			exit (0);
		}

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = 1;
		job->pid[PROCESS_POST_START] = pid;

		TEST_FREE_TAG (list);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_handler (NULL, pid,
					   NIH_CHILD_STOPPED, SIGSTOP);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_SPAWNED);
		TEST_EQ (job->pid[PROCESS_MAIN], 1);
		TEST_EQ (job->pid[PROCESS_POST_START], pid);

		TEST_EQ (kill (pid, SIGCONT), 0);

		waitpid (job->pid[PROCESS_POST_START], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, event);
		event_unblock (event);
		event_unref (event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	config->wait_for = JOB_WAIT_NONE;


	/* Check that we ignore the main process stopping on a signal if the
	 * job isn't in the spawned state.
	 */
	TEST_FEATURE ("with stopped main process outside of spawned");
	config->wait_for = JOB_WAIT_STOP;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = event;
			event_ref (event);
			event_block (event);
			nih_list_add (job->blocking, &entry->entry);
		}

		TEST_CHILD (pid) {
			raise (SIGSTOP);
			exit (0);
		}

		job->goal = JOB_START;
		job->state = JOB_POST_START;
		job->pid[PROCESS_MAIN] = pid;
		job->pid[PROCESS_POST_START] = 1;

		TEST_FREE_TAG (list);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_handler (NULL, pid,
					   NIH_CHILD_STOPPED, SIGSTOP);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_POST_START);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);
		TEST_EQ (job->pid[PROCESS_POST_START], 1);

		TEST_EQ (kill (pid, SIGCONT), 0);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, event);
		event_unblock (event);
		event_unref (event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	config->wait_for = JOB_WAIT_NONE;


	/* Check that we ignore the main process stopping on a signal in
	 * the spawned state if we're not waiting for it to do so.
	 */
	TEST_FEATURE ("with stopped main process for non-wait job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = event;
			event_ref (event);
			event_block (event);
			nih_list_add (job->blocking, &entry->entry);
		}

		TEST_CHILD (pid) {
			raise (SIGSTOP);
			exit (0);
		}

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = pid;

		TEST_FREE_TAG (list);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_handler (NULL, pid,
					   NIH_CHILD_STOPPED, SIGSTOP);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_SPAWNED);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);

		TEST_EQ (kill (pid, SIGCONT), 0);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, event);
		event_unblock (event);
		event_unref (event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}


	/* Check that we ignore the main process stopping on the wrong
	 * signal.
	 */
	TEST_FEATURE ("with stopped main process but wrong signal");
	config->wait_for = JOB_WAIT_STOP;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = event;
			event_ref (event);
			event_block (event);
			nih_list_add (job->blocking, &entry->entry);
		}

		TEST_CHILD (pid) {
			raise (SIGTSTP);
			exit (0);
		}

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = pid;

		TEST_FREE_TAG (list);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_handler (NULL, pid,
					   NIH_CHILD_STOPPED, SIGTSTP);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_SPAWNED);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);

		TEST_EQ (kill (pid, SIGCONT), 0);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, event);
		event_unblock (event);
		event_unref (event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	config->wait_for = JOB_WAIT_NONE;


	/* Check that if we're waiting in spawned for the main process to
	 * stop, and it does so, the process is continued and the job state
	 * changed to running.
	 */
	TEST_FEATURE ("with stopped main process waiting in spawned");
	config->wait_for = JOB_WAIT_STOP;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;

			job->blocking = nih_list_new (job);
			list = job->blocking;

			entry = nih_list_entry_new (job->blocking);
			entry->data = event;
			event_ref (event);
			event_block (event);
			nih_list_add (job->blocking, &entry->entry);
		}

		TEST_CHILD (pid) {
			raise (SIGSTOP);
			exit (0);
		}

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = pid;

		TEST_FREE_TAG (list);

		job->blocked = NULL;
		event->failed = FALSE;

		job->failed = FALSE;
		job->failed_process = -1;
		job->exit_status = 0;

		TEST_DIVERT_STDERR (output) {
			job_child_handler (NULL, pid,
					   NIH_CHILD_STOPPED, SIGSTOP);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 1);
		TEST_EQ (event->failed, FALSE);

		TEST_EQ_P (job->blocked, NULL);

		TEST_NOT_FREE (list);
		TEST_EQ_P (job->blocking, list);
		TEST_EQ_P (entry->data, event);
		event_unblock (event);
		event_unref (event);

		TEST_EQ (job->failed, FALSE);
		TEST_EQ (job->failed_process, -1);
		TEST_EQ (job->exit_status, 0);

		nih_free (job);
	}

	config->wait_for = JOB_WAIT_NONE;


	/* Check that a traced process has a signal delivered to it
	 * unchanged.
	 */
	TEST_FEATURE ("with signal delivered to traced process");
	config->wait_for = JOB_WAIT_DAEMON;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;
			job->trace_state = TRACE_NORMAL;
		}

		TEST_CHILD (pid) {
			assert0 (ptrace (PTRACE_TRACEME, 0, NULL, 0));
			signal (SIGTERM, SIG_IGN);
			raise (SIGTERM);
			exit (0);
		}

		assert0 (waitid (P_PID, pid, &info, WSTOPPED | WNOWAIT));

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = pid;

		TEST_DIVERT_STDERR (output) {
			job_child_handler (NULL, pid,
					   NIH_CHILD_TRAPPED, SIGTERM);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_SPAWNED);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);

		TEST_EQ (job->trace_forks, 0);
		TEST_EQ (job->trace_state, TRACE_NORMAL);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		nih_free (job);
	}

	config->wait_for = JOB_WAIT_NONE;


	/* Check that a new traced process which receives SIGTRAP doesn't
	 * have it delivered, and instead has its options set.
	 */
	TEST_FEATURE ("with trapped new traced process");
	config->wait_for = JOB_WAIT_DAEMON;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;
			job->trace_state = TRACE_NEW;
		}

		TEST_CHILD (pid) {
			assert0 (ptrace (PTRACE_TRACEME, 0, NULL, 0));
			raise (SIGTRAP);
			exit (0);
		}

		assert0 (waitid (P_PID, pid, &info, WSTOPPED | WNOWAIT));

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = pid;

		TEST_DIVERT_STDERR (output) {
			job_child_handler (NULL, pid,
					   NIH_CHILD_TRAPPED, SIGTRAP);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_SPAWNED);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);

		TEST_EQ (job->trace_forks, 0);
		TEST_EQ (job->trace_state, TRACE_NORMAL);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		nih_free (job);
	}

	config->wait_for = JOB_WAIT_NONE;


	/* Check that a new traced process child which receives SIGSTOP
	 * doesn't have it delivered, and instead has its fork count
	 * incremented and its options set.
	 */
	TEST_FEATURE ("with trapped new traced process");
	config->wait_for = JOB_WAIT_DAEMON;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;
			job->trace_state = TRACE_NEW_CHILD;
		}

		TEST_CHILD (pid) {
			assert0 (ptrace (PTRACE_TRACEME, 0, NULL, 0));
			raise (SIGSTOP);
			exit (0);
		}

		assert0 (waitid (P_PID, pid, &info, WSTOPPED | WNOWAIT));

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = pid;

		TEST_DIVERT_STDERR (output) {
			job_child_handler (NULL, pid,
					   NIH_CHILD_TRAPPED, SIGSTOP);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_SPAWNED);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);

		TEST_EQ (job->trace_forks, 1);
		TEST_EQ (job->trace_state, TRACE_NORMAL);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		nih_free (job);
	}

	config->wait_for = JOB_WAIT_NONE;


	/* Check that the second child of a daemon process is detached
	 * and ends the trace, moving the job into the running state.
	 */
	TEST_FEATURE ("with second child of daemon process");
	config->wait_for = JOB_WAIT_DAEMON;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;
			job->trace_forks = 1;
			job->trace_state = TRACE_NEW_CHILD;
		}

		TEST_CHILD (pid) {
			assert0 (ptrace (PTRACE_TRACEME, 0, NULL, 0));
			raise (SIGSTOP);
			pause ();
			exit (0);
		}

		assert0 (waitid (P_PID, pid, &info, WSTOPPED | WNOWAIT));

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = pid;

		TEST_DIVERT_STDERR (output) {
			job_child_handler (NULL, pid,
					   NIH_CHILD_TRAPPED, SIGSTOP);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);

		TEST_EQ (job->trace_forks, 2);
		TEST_EQ (job->trace_state, TRACE_NONE);

		kill (job->pid[PROCESS_MAIN], SIGTERM);
		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFSIGNALED (status));
		TEST_EQ (WTERMSIG (status), SIGTERM);

		nih_free (job);
	}

	config->wait_for = JOB_WAIT_NONE;


	/* Check that the first child of a forking process is detached
	 * and ends the trace, moving the job into the running state.
	 */
	TEST_FEATURE ("with first child of forking process");
	config->wait_for = JOB_WAIT_FORK;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;
			job->trace_forks = 0;
			job->trace_state = TRACE_NEW_CHILD;
		}

		TEST_CHILD (pid) {
			assert0 (ptrace (PTRACE_TRACEME, 0, NULL, 0));
			raise (SIGSTOP);
			pause ();
			exit (0);
		}

		assert0 (waitid (P_PID, pid, &info, WSTOPPED | WNOWAIT));

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = pid;

		TEST_DIVERT_STDERR (output) {
			job_child_handler (NULL, pid,
					   NIH_CHILD_TRAPPED, SIGSTOP);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);

		TEST_EQ (job->trace_forks, 1);
		TEST_EQ (job->trace_state, TRACE_NONE);

		kill (job->pid[PROCESS_MAIN], SIGTERM);
		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFSIGNALED (status));
		TEST_EQ (WTERMSIG (status), SIGTERM);

		nih_free (job);
	}

	config->wait_for = JOB_WAIT_NONE;


	/* Check that when a process forks, the trace state is set to expect
	 * a new child, the job is updated to the new child and the old
	 * parent is detached.
	 */
	TEST_FEATURE ("with forked process");
	config->wait_for = JOB_WAIT_DAEMON;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;
			job->trace_state = TRACE_NORMAL;
		}

		TEST_CHILD (pid) {
			assert0 (ptrace (PTRACE_TRACEME, 0, NULL, 0));
			raise (SIGSTOP);
			fork ();
			exit (0);
		}

		assert0 (waitid (P_PID, pid, &info, WSTOPPED | WNOWAIT));
		assert0 (ptrace (PTRACE_SETOPTIONS, pid, NULL,
				 PTRACE_O_TRACEFORK | PTRACE_O_TRACEEXEC));
		assert0 (ptrace (PTRACE_CONT, pid, NULL, 0));

		assert0 (waitid (P_PID, pid, &info, WSTOPPED | WNOWAIT));

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = pid;

		TEST_DIVERT_STDERR (output) {
			job_child_handler (NULL, pid, NIH_CHILD_PTRACE,
					   PTRACE_EVENT_FORK);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_SPAWNED);
		TEST_NE (job->pid[PROCESS_MAIN], pid);

		TEST_EQ (job->trace_forks, 0);
		TEST_EQ (job->trace_state, TRACE_NEW_CHILD);

		waitpid (pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		assert0 (waitid (P_PID, job->pid[PROCESS_MAIN],
				 &info, WSTOPPED | WNOWAIT));
		TEST_EQ (info.si_pid, job->pid[PROCESS_MAIN]);
		TEST_EQ (info.si_code, CLD_TRAPPED);
		TEST_EQ (info.si_status, SIGSTOP);

		assert0 (ptrace (PTRACE_DETACH, job->pid[PROCESS_MAIN],
				 NULL, 0));

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		nih_free (job);
	}

	config->wait_for = JOB_WAIT_NONE;


	/* Check that should the process call exec() it ends the tracing
	 * even if we haven't had enough forks yet and moves the job into
	 * the running state.
	 */
	TEST_FEATURE ("with exec call by process");
	config->wait_for = JOB_WAIT_DAEMON;

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			job = job_new (config);
			job->id = 1;
			job->trace_forks = 1;
			job->trace_state = TRACE_NORMAL;
		}

		TEST_CHILD (pid) {
			assert0 (ptrace (PTRACE_TRACEME, 0, NULL, 0));
			raise (SIGSTOP);
			execl ("/bin/true", "true", NULL);
			exit (15);
		}

		assert0 (waitid (P_PID, pid, &info, WSTOPPED | WNOWAIT));
		assert0 (ptrace (PTRACE_SETOPTIONS, pid, NULL,
				 PTRACE_O_TRACEFORK | PTRACE_O_TRACEEXEC));
		assert0 (ptrace (PTRACE_CONT, pid, NULL, 0));

		assert0 (waitid (P_PID, pid, &info, WSTOPPED | WNOWAIT));

		job->goal = JOB_START;
		job->state = JOB_SPAWNED;
		job->pid[PROCESS_MAIN] = pid;

		TEST_DIVERT_STDERR (output) {
			job_child_handler (NULL, pid, NIH_CHILD_PTRACE,
					   PTRACE_EVENT_EXEC);
		}
		rewind (output);

		TEST_EQ (job->goal, JOB_START);
		TEST_EQ (job->state, JOB_RUNNING);
		TEST_EQ (job->pid[PROCESS_MAIN], pid);

		TEST_EQ (job->trace_forks, 1);
		TEST_EQ (job->trace_state, TRACE_NONE);

		waitpid (job->pid[PROCESS_MAIN], &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		nih_free (job);
	}

	config->wait_for = JOB_WAIT_NONE;
#if HAVE_VALGRIND_VALGRIND_H
	}
#endif


	fclose (output);

	nih_free (config);
	file->job = NULL;
	nih_free (source);

	nih_free (event);
	event_poll ();
}


void
test_handle_event (void)
{
	JobConfig      *config1, *config2;
	Job            *job1, *job2, *ptr;
	Event          *event, *event1, *event2, *event3, *event4;
	EventOperator  *oper;
	NihList        *list;
	NihListEntry   *entry;
	char          **env1, **env2;

	TEST_FUNCTION ("job_handle_event");
	config1 = job_config_new (NULL, "foo");
	config1->respawn_limit = 0;

	assert (nih_str_array_add (&(config1->env), config1, NULL, "FOO=BAR"));
	assert (nih_str_array_add (&(config1->env), config1, NULL, "BAR=BAZ"));

	config1->start_on = event_operator_new (config1, EVENT_AND,
						NULL, NULL);

	oper = event_operator_new (config1->start_on, EVENT_MATCH,
				   "wibble", NULL);
	nih_tree_add (&config1->start_on->node, &oper->node, NIH_TREE_LEFT);

	oper = event_operator_new (config1->start_on, EVENT_MATCH,
				   "wobble", NULL);
	nih_tree_add (&config1->start_on->node, &oper->node, NIH_TREE_RIGHT);

	nih_hash_add (jobs, &config1->entry);


	config2 = job_config_new (NULL, "bar");
	config2->respawn_limit = 0;

	config2->stop_on = event_operator_new (config2, EVENT_OR, NULL, NULL);

	oper = event_operator_new (config2->stop_on, EVENT_MATCH,
				   "foo", NULL);
	nih_tree_add (&config2->stop_on->node, &oper->node, NIH_TREE_LEFT);

	oper = event_operator_new (config2->stop_on, EVENT_MATCH,
				   "bar", NULL);
	nih_tree_add (&config2->stop_on->node, &oper->node, NIH_TREE_RIGHT);

	nih_hash_add (jobs, &config2->entry);


	/* Check that a non matching event has no effect on either job,
	 * and doesn't result in the event being given any jobs.
	 */
	TEST_FEATURE ("with non-matching event");
	event1 = event_new (NULL, "biscuit", NULL);

	TEST_ALLOC_FAIL {
		event1->blockers = 0;
		event1->refs = 0;

		TEST_ALLOC_SAFE {
			job2 = job_new (config2);
		}

		job2->goal = JOB_START;
		job2->state = JOB_RUNNING;
		job2->blocked = NULL;

		job_handle_event (event1);

		TEST_EQ (event1->refs, 0);
		TEST_EQ (event1->blockers, 0);

		TEST_LIST_EMPTY (&config1->instances);

		oper = config1->start_on;
		TEST_EQ (oper->value, FALSE);

		oper = (EventOperator *)config1->start_on->node.left;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);
		TEST_EQ (oper->blocked, FALSE);

		oper = (EventOperator *)config1->start_on->node.right;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);
		TEST_EQ (oper->blocked, FALSE);

		TEST_EQ (job2->goal, JOB_START);
		TEST_EQ (job2->state, JOB_RUNNING);
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

		nih_free (job2);
	}

	nih_free (event1);


	/* Check that a second event can complete an expression and affect
	 * the job, spawning a new instance.  The environment from the config,
	 * plus the job-unique variables should be in the instances's
	 * environment, since they would have been copied out of start_env
	 * on starting.
	 */
	TEST_FEATURE ("with matching event to start");
	event1 = event_new (NULL, "wibble", NULL);
	event2 = event_new (NULL, "wobble", NULL);

	TEST_ALLOC_FAIL {
		event1->blockers = 0;
		event1->refs = 0;

		event2->blockers = 0;
		event2->refs = 0;

		job_handle_event (event1);
		job_handle_event (event2);

		TEST_EQ (event1->refs, 1);
		TEST_EQ (event1->blockers, 1);

		TEST_EQ (event2->refs, 1);
		TEST_EQ (event2->blockers, 1);

		TEST_LIST_NOT_EMPTY (&config1->instances);
		job1 = (Job *)config1->instances.next;

		TEST_EQ (job1->goal, JOB_START);
		TEST_EQ (job1->state, JOB_STARTING);
		TEST_NE_P (job1->blocked, NULL);
		TEST_EQ (job1->blocked->refs, 1);
		event_unref (job1->blocked);

		TEST_NE_P (job1->env, NULL);
		TEST_ALLOC_PARENT (job1->env, job1);
		TEST_ALLOC_SIZE (job1->env, sizeof (char *) * 8);
		TEST_ALLOC_PARENT (job1->env[0], job1->env);
		TEST_EQ_STRN (job1->env[0], "PATH=");
		TEST_ALLOC_PARENT (job1->env[1], job1->env);
		TEST_EQ_STRN (job1->env[1], "TERM=");
		TEST_ALLOC_PARENT (job1->env[2], job1->env);
		TEST_EQ_STR (job1->env[2], "FOO=BAR");
		TEST_ALLOC_PARENT (job1->env[3], job1->env);
		TEST_EQ_STR (job1->env[3], "BAR=BAZ");
		TEST_ALLOC_PARENT (job1->env[4], job1->env);
		TEST_EQ_STR (job1->env[4], "UPSTART_EVENTS=wibble wobble");
		TEST_ALLOC_PARENT (job1->env[5], job1->env);
		TEST_EQ_STR (job1->env[5], "UPSTART_JOB=foo");
		TEST_ALLOC_PARENT (job1->env[6], job1->env);
		TEST_EQ_STRN (job1->env[6], "UPSTART_JOB_ID=");
		TEST_EQ_P (job1->env[7], NULL);

		TEST_EQ_P (job1->start_env, NULL);

		oper = config1->start_on;
		TEST_EQ (oper->value, FALSE);

		oper = (EventOperator *)config1->start_on->node.left;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ (oper->blocked, FALSE);
		TEST_EQ_P (oper->event, NULL);

		oper = (EventOperator *)config1->start_on->node.right;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ (oper->blocked, FALSE);
		TEST_EQ_P (oper->event, NULL);

		TEST_NE_P (job1->blocking, NULL);
		TEST_ALLOC_SIZE (job1->blocking, sizeof (NihList));
		TEST_ALLOC_PARENT (job1->blocking, job1);

		TEST_LIST_NOT_EMPTY (job1->blocking);

		entry = (NihListEntry *)job1->blocking->next;
		TEST_ALLOC_SIZE (entry, sizeof (NihListEntry));
		TEST_ALLOC_PARENT (entry, job1->blocking);
		event = (Event *)entry->data;
		TEST_EQ_P (event, event1);
		event_unblock (event);
		event_unref (event);
		nih_free (entry);

		entry = (NihListEntry *)job1->blocking->next;
		TEST_ALLOC_SIZE (entry, sizeof (NihListEntry));
		TEST_ALLOC_PARENT (entry, job1->blocking);
		event = (Event *)entry->data;
		TEST_EQ_P (event, event2);
		event_unblock (event);
		event_unref (event);
		nih_free (entry);

		TEST_LIST_EMPTY (job1->blocking);

		nih_free (job1);
	}

	nih_free (event1);
	nih_free (event2);


	/* Check that the environment variables from the event are also copied
	 * into the job's environment.
	 */
	TEST_FEATURE ("with environment in start event");
	event1 = event_new (NULL, "wibble", NULL);
	assert (nih_str_array_add (&(event1->env), event1,
				   NULL, "FRODO=baggins"));
	assert (nih_str_array_add (&(event1->env), event1,
				   NULL, "BILBO=took"));

	event2 = event_new (NULL, "wobble", NULL);
	assert (nih_str_array_add (&(event2->env), event2,
				   NULL, "FRODO=brandybuck"));
	assert (nih_str_array_add (&(event2->env), event2,
				   NULL, "TEA=MILK"));

	TEST_ALLOC_FAIL {
		event1->blockers = 0;
		event1->refs = 0;

		event2->blockers = 0;
		event2->refs = 0;

		job_handle_event (event1);
		job_handle_event (event2);

		TEST_EQ (event1->refs, 1);
		TEST_EQ (event1->blockers, 1);

		TEST_EQ (event2->refs, 1);
		TEST_EQ (event2->blockers, 1);

		TEST_LIST_NOT_EMPTY (&config1->instances);
		job1 = (Job *)config1->instances.next;

		TEST_EQ (job1->goal, JOB_START);
		TEST_EQ (job1->state, JOB_STARTING);
		TEST_NE_P (job1->blocked, NULL);
		TEST_EQ (job1->blocked->refs, 1);
		event_unref (job1->blocked);

		TEST_NE_P (job1->env, NULL);
		TEST_ALLOC_PARENT (job1->env, job1);
		TEST_ALLOC_SIZE (job1->env, sizeof (char *) * 11);
		TEST_ALLOC_PARENT (job1->env[0], job1->env);
		TEST_EQ_STRN (job1->env[0], "PATH=");
		TEST_ALLOC_PARENT (job1->env[1], job1->env);
		TEST_EQ_STRN (job1->env[1], "TERM=");
		TEST_ALLOC_PARENT (job1->env[2], job1->env);
		TEST_EQ_STR (job1->env[2], "FOO=BAR");
		TEST_ALLOC_PARENT (job1->env[3], job1->env);
		TEST_EQ_STR (job1->env[3], "BAR=BAZ");
		TEST_ALLOC_PARENT (job1->env[4], job1->env);
		TEST_EQ_STR (job1->env[4], "FRODO=brandybuck");
		TEST_ALLOC_PARENT (job1->env[5], job1->env);
		TEST_EQ_STR (job1->env[5], "BILBO=took");
		TEST_ALLOC_PARENT (job1->env[6], job1->env);
		TEST_EQ_STR (job1->env[6], "TEA=MILK");
		TEST_ALLOC_PARENT (job1->env[7], job1->env);
		TEST_EQ_STR (job1->env[7], "UPSTART_EVENTS=wibble wobble");
		TEST_ALLOC_PARENT (job1->env[8], job1->env);
		TEST_EQ_STR (job1->env[8], "UPSTART_JOB=foo");
		TEST_ALLOC_PARENT (job1->env[9], job1->env);
		TEST_EQ_STRN (job1->env[9], "UPSTART_JOB_ID=");
		TEST_EQ_P (job1->env[10], NULL);

		TEST_EQ_P (job1->start_env, NULL);

		oper = config1->start_on;
		TEST_EQ (oper->value, FALSE);

		oper = (EventOperator *)config1->start_on->node.left;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ (oper->blocked, FALSE);
		TEST_EQ_P (oper->event, NULL);

		oper = (EventOperator *)config1->start_on->node.right;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ (oper->blocked, FALSE);
		TEST_EQ_P (oper->event, NULL);

		TEST_NE_P (job1->blocking, NULL);
		TEST_ALLOC_SIZE (job1->blocking, sizeof (NihList));
		TEST_ALLOC_PARENT (job1->blocking, job1);

		TEST_LIST_NOT_EMPTY (job1->blocking);

		entry = (NihListEntry *)job1->blocking->next;
		TEST_ALLOC_SIZE (entry, sizeof (NihListEntry));
		TEST_ALLOC_PARENT (entry, job1->blocking);
		event = (Event *)entry->data;
		TEST_EQ_P (event, event1);
		event_unblock (event);
		event_unref (event);
		nih_free (entry);

		entry = (NihListEntry *)job1->blocking->next;
		TEST_ALLOC_SIZE (entry, sizeof (NihListEntry));
		TEST_ALLOC_PARENT (entry, job1->blocking);
		event = (Event *)entry->data;
		TEST_EQ_P (event, event2);
		event_unblock (event);
		event_unref (event);
		nih_free (entry);

		TEST_LIST_EMPTY (job1->blocking);

		nih_free (job1);
	}

	nih_free (event1);
	nih_free (event2);


	/* Check that the event can restart an instance that is stopping,
	 * storing the environment in the start_env member since it should
	 * not overwrite the previous environment until it actually restarts.
	 */
	TEST_FEATURE ("with restart of stopping job");
	event1 = event_new (NULL, "wibble", NULL);
	assert (nih_str_array_add (&(event1->env), event1,
				   NULL, "FRODO=baggins"));
	assert (nih_str_array_add (&(event1->env), event1,
				   NULL, "BILBO=took"));

	event2 = event_new (NULL, "wobble", NULL);
	assert (nih_str_array_add (&(event2->env), event2,
				   NULL, "FRODO=brandybuck"));
	assert (nih_str_array_add (&(event2->env), event2,
				   NULL, "TEA=MILK"));

	TEST_ALLOC_FAIL {
		event1->blockers = 0;
		event1->refs = 0;

		event2->blockers = 0;
		event2->refs = 0;

		TEST_ALLOC_SAFE {
			job1 = job_new (config1);

			assert (nih_str_array_add (&(job1->env), job1,
						   NULL, "FOO=wibble"));
			assert (nih_str_array_add (&(job1->env), job1,
						   NULL, "BAR=wobble"));

			assert (nih_str_array_add (&(job1->start_env), job1,
						   NULL, "FOO=tea"));
			assert (nih_str_array_add (&(job1->start_env), job1,
						   NULL, "BAR=coffee"));

			job1->blocking = nih_list_new (job1);

			entry = nih_list_entry_new (job1->blocking);
			entry->data = event_new (NULL, "flibble", NULL);
			nih_list_add (job1->blocking, &entry->entry);
			event3 = entry->data;
			event_ref (event3);
			event_block (event3);

			entry = nih_list_entry_new (job1->blocking);
			entry->data = event_new (NULL, "flobble", NULL);
			nih_list_add (job1->blocking, &entry->entry);
			event4 = entry->data;
			event_ref (event4);
			event_block (event4);
		}

		job1->goal = JOB_STOP;
		job1->state = JOB_STOPPING;
		job1->blocked = NULL;

		env1 = job1->env;
		TEST_FREE_TAG (env1);

		env2 = job1->start_env;
		TEST_FREE_TAG (env2);

		list = job1->blocking;
		TEST_FREE_TAG (list);

		job_handle_event (event1);
		job_handle_event (event2);

		TEST_EQ (event1->refs, 1);
		TEST_EQ (event1->blockers, 1);

		TEST_EQ (event2->refs, 1);
		TEST_EQ (event2->blockers, 1);

		TEST_EQ (event3->refs, 0);
		TEST_EQ (event3->blockers, 0);

		TEST_EQ (event4->refs, 0);
		TEST_EQ (event4->blockers, 0);

		TEST_LIST_NOT_EMPTY (&config1->instances);
		ptr = (Job *)config1->instances.next;

		TEST_EQ_P (ptr, job1);

		TEST_EQ (job1->goal, JOB_START);
		TEST_EQ (job1->state, JOB_STOPPING);
		TEST_EQ_P (job1->blocked, NULL);

		TEST_NOT_FREE (env1);
		TEST_EQ_P (job1->env, env1);

		TEST_FREE (env2);

		TEST_NE_P (job1->start_env, NULL);
		TEST_ALLOC_PARENT (job1->start_env, job1);
		TEST_ALLOC_SIZE (job1->start_env, sizeof (char *) * 11);
		TEST_ALLOC_PARENT (job1->start_env[0], job1->start_env);
		TEST_EQ_STRN (job1->start_env[0], "PATH=");
		TEST_ALLOC_PARENT (job1->start_env[1], job1->start_env);
		TEST_EQ_STRN (job1->start_env[1], "TERM=");
		TEST_ALLOC_PARENT (job1->start_env[2], job1->start_env);
		TEST_EQ_STR (job1->start_env[2], "FOO=BAR");
		TEST_ALLOC_PARENT (job1->start_env[3], job1->start_env);
		TEST_EQ_STR (job1->start_env[3], "BAR=BAZ");
		TEST_ALLOC_PARENT (job1->start_env[4], job1->start_env);
		TEST_EQ_STR (job1->start_env[4], "FRODO=brandybuck");
		TEST_ALLOC_PARENT (job1->start_env[5], job1->start_env);
		TEST_EQ_STR (job1->start_env[5], "BILBO=took");
		TEST_ALLOC_PARENT (job1->start_env[6], job1->start_env);
		TEST_EQ_STR (job1->start_env[6], "TEA=MILK");
		TEST_ALLOC_PARENT (job1->start_env[7], job1->start_env);
		TEST_EQ_STR (job1->start_env[7], "UPSTART_EVENTS=wibble wobble");
		TEST_ALLOC_PARENT (job1->start_env[8], job1->start_env);
		TEST_EQ_STR (job1->start_env[8], "UPSTART_JOB=foo");
		TEST_ALLOC_PARENT (job1->start_env[9], job1->start_env);
		TEST_EQ_STRN (job1->start_env[9], "UPSTART_JOB_ID=");
		TEST_EQ_P (job1->start_env[10], NULL);

		oper = config1->start_on;
		TEST_EQ (oper->value, FALSE);

		oper = (EventOperator *)config1->start_on->node.left;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ (oper->blocked, FALSE);
		TEST_EQ_P (oper->event, NULL);

		oper = (EventOperator *)config1->start_on->node.right;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ (oper->blocked, FALSE);
		TEST_EQ_P (oper->event, NULL);

		TEST_FREE (list);

		TEST_NE_P (job1->blocking, NULL);
		TEST_ALLOC_SIZE (job1->blocking, sizeof (NihList));
		TEST_ALLOC_PARENT (job1->blocking, job1);

		TEST_LIST_NOT_EMPTY (job1->blocking);

		entry = (NihListEntry *)job1->blocking->next;
		TEST_ALLOC_SIZE (entry, sizeof (NihListEntry));
		TEST_ALLOC_PARENT (entry, job1->blocking);
		event = (Event *)entry->data;
		TEST_EQ_P (event, event1);
		event_unblock (event);
		event_unref (event);
		nih_free (entry);

		entry = (NihListEntry *)job1->blocking->next;
		TEST_ALLOC_SIZE (entry, sizeof (NihListEntry));
		TEST_ALLOC_PARENT (entry, job1->blocking);
		event = (Event *)entry->data;
		TEST_EQ_P (event, event2);
		event_unblock (event);
		event_unref (event);
		nih_free (entry);

		TEST_LIST_EMPTY (job1->blocking);

		nih_free (job1);
		nih_free (event3);
		nih_free (event4);
	}

	nih_free (event1);
	nih_free (event2);


	/* Check that a job that is already running is not affected by the
	 * start events happening again.
	 */
	TEST_FEATURE ("with already running job");
	event1 = event_new (NULL, "wibble", NULL);
	event2 = event_new (NULL, "wobble", NULL);

	TEST_ALLOC_FAIL {
		event1->blockers = 0;
		event1->refs = 0;

		event2->blockers = 0;
		event2->refs = 0;

		TEST_ALLOC_SAFE {
			job1 = job_new (config1);

			assert (nih_str_array_add (&(job1->env), job1,
						   NULL, "FOO=wibble"));
			assert (nih_str_array_add (&(job1->env), job1,
						   NULL, "BAR=wobble"));

			assert (nih_str_array_add (&(job1->start_env), job1,
						   NULL, "FOO=tea"));
			assert (nih_str_array_add (&(job1->start_env), job1,
						   NULL, "BAR=coffee"));

			job1->blocking = nih_list_new (job1);

			entry = nih_list_entry_new (job1->blocking);
			entry->data = event_new (NULL, "flibble", NULL);
			nih_list_add (job1->blocking, &entry->entry);
			event3 = entry->data;
			event_ref (event3);
			event_block (event3);

			entry = nih_list_entry_new (job1->blocking);
			entry->data = event_new (NULL, "flpbble", NULL);
			nih_list_add (job1->blocking, &entry->entry);
			event4 = entry->data;
			event_ref (event4);
			event_block (event4);
		}

		job1->goal = JOB_START;
		job1->state = JOB_RUNNING;
		job1->blocked = NULL;

		env1 = job1->env;
		TEST_FREE_TAG (env1);

		env2 = job1->start_env;
		TEST_FREE_TAG (env2);

		list = job1->blocking;
		TEST_FREE_TAG (list);

		job_handle_event (event1);
		job_handle_event (event2);

		TEST_EQ (event1->refs, 0);
		TEST_EQ (event1->blockers, 0);

		TEST_EQ (event2->refs, 0);
		TEST_EQ (event2->blockers, 0);

		TEST_EQ (event3->refs, 1);
		TEST_EQ (event3->blockers, 1);

		TEST_EQ (event4->refs, 1);
		TEST_EQ (event4->blockers, 1);

		TEST_LIST_NOT_EMPTY (&config1->instances);
		ptr = (Job *)config1->instances.next;

		TEST_EQ_P (ptr, job1);

		TEST_EQ (job1->goal, JOB_START);
		TEST_EQ (job1->state, JOB_RUNNING);
		TEST_EQ_P (job1->blocked, NULL);

		TEST_NOT_FREE (env1);
		TEST_EQ_P (job1->env, env1);

		TEST_NOT_FREE (env2);
		TEST_EQ_P (job1->start_env, env2);

		oper = config1->start_on;
		TEST_EQ (oper->value, FALSE);

		oper = (EventOperator *)config1->start_on->node.left;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ (oper->blocked, FALSE);
		TEST_EQ_P (oper->event, NULL);

		oper = (EventOperator *)config1->start_on->node.right;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ (oper->blocked, FALSE);
		TEST_EQ_P (oper->event, NULL);

		event_unblock (event3);
		event_unref (event3);

		event_unblock (event4);
		event_unref (event4);

		nih_free (job1);
		nih_free (event3);
		nih_free (event4);
	}

	nih_free (event1);
	nih_free (event2);


	/* Check that a matching event is recorded against the operator that
	 * matches it, but only affects the job if it completes the
	 * expression.  The name of the event should be added to the stop_env
	 * member of the job, used for pre-stop later.
	 */
	TEST_FEATURE ("with matching event to stop");
	event1 = event_new (NULL, "foo", NULL);

	TEST_ALLOC_FAIL {
		event1->blockers = 0;
		event1->refs = 0;

		TEST_ALLOC_SAFE {
			job2 = job_new (config2);
		}

		job2->goal = JOB_START;
		job2->state = JOB_RUNNING;
		job2->blocked = NULL;

		job_handle_event (event1);

		TEST_EQ (event1->refs, 1);
		TEST_EQ (event1->blockers, 1);

		TEST_LIST_NOT_EMPTY (&config2->instances);
		TEST_EQ_P ((Job *)config2->instances.next, job2);

		TEST_EQ (job2->goal, JOB_STOP);
		TEST_EQ (job2->state, JOB_STOPPING);
		TEST_NE_P (job2->blocked, NULL);
		TEST_EQ (job2->blocked->refs, 1);
		event_unref (job2->blocked);

		TEST_NE_P (job2->stop_env, NULL);
		TEST_ALLOC_PARENT (job2->stop_env, job2);
		TEST_ALLOC_SIZE (job2->stop_env, sizeof (char *) * 2);
		TEST_ALLOC_PARENT (job2->stop_env[0], job2->stop_env);
		TEST_EQ_STR (job2->stop_env[0], "UPSTART_STOP_EVENTS=foo");
		TEST_EQ_P (job2->stop_env[1], NULL);

		oper = job2->stop_on;
		TEST_EQ (oper->value, TRUE);

		oper = (EventOperator *)job2->stop_on->node.left;
		TEST_EQ (oper->value, TRUE);
		TEST_EQ_P (oper->event, event1);
		TEST_EQ (oper->blocked, TRUE);

		oper = (EventOperator *)job2->stop_on->node.right;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);
		TEST_EQ (oper->blocked, FALSE);

		event_operator_reset (job2->stop_on);

		nih_free (job2);
	}

	nih_free (event1);


	/* Check that the environment variables from the event are also copied
	 * into the job's stop_env member.
	 */
	TEST_FEATURE ("with environment in stop event");
	event1 = event_new (NULL, "foo", NULL);
	assert (nih_str_array_add (&(event1->env), event1,
				   NULL, "FOO=foo"));
	assert (nih_str_array_add (&(event1->env), event1,
				   NULL, "BAR=bar"));

	TEST_ALLOC_FAIL {
		event1->blockers = 0;
		event1->refs = 0;

		TEST_ALLOC_SAFE {
			job2 = job_new (config2);
		}

		job2->goal = JOB_START;
		job2->state = JOB_RUNNING;
		job2->blocked = NULL;

		job_handle_event (event1);

		TEST_EQ (event1->refs, 1);
		TEST_EQ (event1->blockers, 1);

		TEST_LIST_NOT_EMPTY (&config2->instances);
		TEST_EQ_P ((Job *)config2->instances.next, job2);

		TEST_EQ (job2->goal, JOB_STOP);
		TEST_EQ (job2->state, JOB_STOPPING);
		TEST_NE_P (job2->blocked, NULL);
		TEST_EQ (job2->blocked->refs, 1);
		event_unref (job2->blocked);

		TEST_NE_P (job2->stop_env, NULL);
		TEST_ALLOC_PARENT (job2->stop_env, job2);
		TEST_ALLOC_SIZE (job2->stop_env, sizeof (char *) * 4);
		TEST_ALLOC_PARENT (job2->stop_env[0], job2->stop_env);
		TEST_EQ_STR (job2->stop_env[0], "FOO=foo");
		TEST_ALLOC_PARENT (job2->stop_env[1], job2->stop_env);
		TEST_EQ_STR (job2->stop_env[1], "BAR=bar");
		TEST_ALLOC_PARENT (job2->stop_env[2], job2->stop_env);
		TEST_EQ_STR (job2->stop_env[2], "UPSTART_STOP_EVENTS=foo");
		TEST_EQ_P (job2->stop_env[3], NULL);

		oper = job2->stop_on;
		TEST_EQ (oper->value, TRUE);

		oper = (EventOperator *)job2->stop_on->node.left;
		TEST_EQ (oper->value, TRUE);
		TEST_EQ_P (oper->event, event1);
		TEST_EQ (oper->blocked, TRUE);

		oper = (EventOperator *)job2->stop_on->node.right;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);
		TEST_EQ (oper->blocked, FALSE);

		event_operator_reset (job2->stop_on);

		nih_free (job2);
	}

	nih_free (event1);


	/* Check that the event can resume stopping a job that's stopping
	 * but previously was marked for restarting.
	 */
	TEST_FEATURE ("with stop of restarting job");
	event1 = event_new (NULL, "foo", NULL);
	assert (nih_str_array_add (&(event1->env), event1,
				   NULL, "FOO=foo"));
	assert (nih_str_array_add (&(event1->env), event1,
				   NULL, "BAR=bar"));

	TEST_ALLOC_FAIL {
		event1->blockers = 0;
		event1->refs = 0;

		TEST_ALLOC_SAFE {
			job2 = job_new (config2);

			assert (nih_str_array_add (&(job2->stop_env), job2,
						   NULL, "FOO=wibble"));
			assert (nih_str_array_add (&(job2->stop_env), job2,
						   NULL, "BAR=wobble"));
		}

		job2->goal = JOB_START;
		job2->state = JOB_STOPPING;
		job2->blocked = NULL;

		env1 = job2->stop_env;
		TEST_FREE_TAG (env1);

		job_handle_event (event1);

		TEST_EQ (event1->refs, 1);
		TEST_EQ (event1->blockers, 1);

		TEST_LIST_NOT_EMPTY (&config2->instances);
		TEST_EQ_P ((Job *)config2->instances.next, job2);

		TEST_EQ (job2->goal, JOB_STOP);
		TEST_EQ (job2->state, JOB_STOPPING);
		TEST_EQ_P (job2->blocked, NULL);

		TEST_FREE (env1);

		TEST_NE_P (job2->stop_env, NULL);
		TEST_ALLOC_PARENT (job2->stop_env, job2);
		TEST_ALLOC_SIZE (job2->stop_env, sizeof (char *) * 4);
		TEST_ALLOC_PARENT (job2->stop_env[0], job2->stop_env);
		TEST_EQ_STR (job2->stop_env[0], "FOO=foo");
		TEST_ALLOC_PARENT (job2->stop_env[1], job2->stop_env);
		TEST_EQ_STR (job2->stop_env[1], "BAR=bar");
		TEST_ALLOC_PARENT (job2->stop_env[2], job2->stop_env);
		TEST_EQ_STR (job2->stop_env[2], "UPSTART_STOP_EVENTS=foo");
		TEST_EQ_P (job2->stop_env[3], NULL);

		oper = job2->stop_on;
		TEST_EQ (oper->value, TRUE);

		oper = (EventOperator *)job2->stop_on->node.left;
		TEST_EQ (oper->value, TRUE);
		TEST_EQ_P (oper->event, event1);
		TEST_EQ (oper->blocked, TRUE);

		oper = (EventOperator *)job2->stop_on->node.right;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);
		TEST_EQ (oper->blocked, FALSE);

		event_operator_reset (job2->stop_on);

		nih_free (job2);
	}

	nih_free (event1);


	/* Check that a job that is already stopping is not affected by the
	 * stop events happening again.
	 */
	TEST_FEATURE ("with already stopping job");
	event1 = event_new (NULL, "foo", NULL);

	TEST_ALLOC_FAIL {
		event1->blockers = 0;
		event1->refs = 0;

		TEST_ALLOC_SAFE {
			job2 = job_new (config2);

			assert (nih_str_array_add (&(job2->stop_env), job2,
						   NULL, "FOO=wibble"));
			assert (nih_str_array_add (&(job2->stop_env), job2,
						   NULL, "BAR=wobble"));
		}

		job2->goal = JOB_STOP;
		job2->state = JOB_STOPPING;
		job2->blocked = NULL;

		env1 = job2->stop_env;
		TEST_FREE_TAG (env1);

		job_handle_event (event1);

		TEST_EQ (event1->refs, 1);
		TEST_EQ (event1->blockers, 1);

		TEST_LIST_NOT_EMPTY (&config2->instances);
		TEST_EQ_P ((Job *)config2->instances.next, job2);

		TEST_EQ (job2->goal, JOB_STOP);
		TEST_EQ (job2->state, JOB_STOPPING);
		TEST_EQ_P (job2->blocked, NULL);

		TEST_NOT_FREE (env1);
		TEST_EQ_P (job2->stop_env, env1);

		oper = job2->stop_on;
		TEST_EQ (oper->value, TRUE);

		oper = (EventOperator *)job2->stop_on->node.left;
		TEST_EQ (oper->value, TRUE);
		TEST_EQ_P (oper->event, event1);
		TEST_EQ (oper->blocked, TRUE);

		oper = (EventOperator *)job2->stop_on->node.right;
		TEST_EQ (oper->value, FALSE);
		TEST_EQ_P (oper->event, NULL);
		TEST_EQ (oper->blocked, FALSE);

		event_operator_reset (job2->stop_on);

		nih_free (job2);
	}

	nih_free (event1);


	nih_free (config1);
	nih_free (config2);

	event_poll ();
}

void
test_handle_event_finished (void)
{
	JobConfig *config1, *config2;
	Job       *job1, *job2;
	Event     *event;

	TEST_FUNCTION ("job_handle_event_finished");
	config1 = job_config_new (NULL, "foo");
	config1->respawn_limit = 0;
	config1->process[PROCESS_PRE_START] = job_process_new (config1);
	config1->process[PROCESS_PRE_START]->command = "echo";
	config1->process[PROCESS_POST_STOP] = job_process_new (config1);
	config1->process[PROCESS_POST_STOP]->command = "echo";

	config1->start_on = event_operator_new (config1, EVENT_MATCH,
						"wibble", NULL);

	job1 = job_new (config1);

	nih_hash_add (jobs, &config1->entry);

	config2 = job_config_new (NULL, "bar");
	config2->respawn_limit = 0;
	config2->process[PROCESS_PRE_START] = job_process_new (config2);
	config2->process[PROCESS_PRE_START]->command = "echo";
	config2->process[PROCESS_POST_STOP] = job_process_new (config2);
	config2->process[PROCESS_POST_STOP]->command = "echo";

	config2->stop_on = event_operator_new (config2, EVENT_MATCH,
					       "wibble", NULL);

	job2 = job_new (config2);

	nih_hash_add (jobs, &config2->entry);


	/* Check that a non matching event has no effect on either job.
	 */
	TEST_FEATURE ("with non-matching event");
	event = event_new (NULL, "biscuit", NULL);

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

	nih_free (event);


	/* Check that a matching event results in the jobs being unblocked
	 * and then started or stopped as appropriate.
	 */
	TEST_FEATURE ("with matching event");
	event = event_new (NULL, "wibble", NULL);

	TEST_ALLOC_FAIL {
		job1->goal = JOB_STOP;
		job1->state = JOB_STOPPING;
		job1->pid[PROCESS_POST_STOP] = 0;
		job1->blocked = event;

		job2->goal = JOB_START;
		job2->state = JOB_STARTING;
		job2->pid[PROCESS_PRE_START] = 0;
		job2->blocked = event;
		event->refs = 3;
		event->blockers = 1;

		job_handle_event_finished (event);

		TEST_EQ (job1->goal, JOB_STOP);
		TEST_EQ (job1->state, JOB_POST_STOP);
		TEST_GT (job1->pid[PROCESS_POST_STOP], 0);
		TEST_EQ_P (job1->blocked, NULL);

		waitpid (job1->pid[PROCESS_POST_STOP], NULL, 0);

		TEST_EQ (job2->goal, JOB_START);
		TEST_EQ (job2->state, JOB_PRE_START);
		TEST_GT (job2->pid[PROCESS_PRE_START], 0);
		TEST_EQ_P (job2->blocked, NULL);

		TEST_EQ (event->refs, 1);
		TEST_EQ (event->blockers, 1);

		waitpid (job2->pid[PROCESS_PRE_START], NULL, 0);
	}

	nih_free (event);


	nih_free (config2);
	nih_free (config1);

	event_poll ();
}


int
main (int   argc,
      char *argv[])
{
	/* We re-exec this binary to test various children features.  To
	 * do that, we need to know the full path to the program.
	 */
	argv0 = argv[0];
	if (argv0[0] != '/') {
		char path[PATH_MAX];

		getcwd (path, sizeof (path));
		strcat (path, "/");
		strcat (path, argv0);

		argv0 = path;
	}

	/* If an argument is given, it's a filename to write the environment to
	 */
	if (argc == 2) {
		FILE *out;

		out = fopen (argv[1], "w");
		for (char **env = environ; *env; env++)
			fprintf (out, "%s\n", *env);
		exit (0);
	}

	/* Otherwise run the tests as normal */
	test_process_new ();

	test_config_new ();
	test_config_replace ();
	test_config_environment ();

	test_new ();
	test_find_by_pid ();
	test_find_by_id ();
	test_instance ();
	test_change_goal ();
	test_change_state ();
	test_next_state ();
	test_run_process ();
	test_kill_process ();
	test_child_handler ();
	test_handle_event ();
	test_handle_event_finished ();

	return 0;
}
