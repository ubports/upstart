/* upstart
 *
 * test_job_class.c - test suite for init/job_class.c
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


void
test_new (void)
{
	JobClass *class;
	int       i;

	/* Check that we can create a new JobClass structure; the structure
	 * should be allocated with nih_alloc but not placed in the jobs
	 * hash.
	 */
	TEST_FUNCTION ("job_class_new");
	job_class_init ();

	TEST_ALLOC_FAIL {
		class = job_class_new (NULL, "test");

		if (test_alloc_failed) {
			TEST_EQ_P (class, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (class, sizeof (JobClass));
		TEST_LIST_EMPTY (&class->entry);

		TEST_ALLOC_PARENT (class->name, class);
		TEST_EQ_STR (class->name, "test");

		TEST_ALLOC_PARENT (class->path, class);
		TEST_EQ_STR (class->path, "/com/ubuntu/Upstart/jobs/test");

		TEST_EQ_P (class->instance, NULL);
		TEST_LIST_EMPTY (&class->instances);

		TEST_EQ_P (class->description, NULL);
		TEST_EQ_P (class->author, NULL);
		TEST_EQ_P (class->version, NULL);

		TEST_EQ_P (class->env, NULL);
		TEST_EQ_P (class->export, NULL);

		TEST_EQ_P (class->start_on, NULL);
		TEST_EQ_P (class->stop_on, NULL);
		TEST_EQ_P (class->emits, NULL);

		TEST_NE_P (class->process, NULL);
		TEST_ALLOC_PARENT (class->process, class);
		TEST_ALLOC_SIZE (class->process,
				 sizeof (Process *) * PROCESS_LAST);

		for (i = 0; i < PROCESS_LAST; i++)
			TEST_EQ_P (class->process[i], NULL);

		TEST_EQ (class->expect, EXPECT_NONE);
		TEST_EQ (class->task, FALSE);

		TEST_EQ (class->kill_timeout, 5);

		TEST_EQ (class->respawn, FALSE);
		TEST_EQ (class->respawn_limit, 10);
		TEST_EQ (class->respawn_interval, 5);

		TEST_EQ_P (class->normalexit, NULL);
		TEST_EQ (class->normalexit_len, 0);

		TEST_EQ (class->leader, FALSE);
		TEST_EQ (class->console, CONSOLE_NONE);

		TEST_EQ (class->umask, 022);
		TEST_EQ (class->nice, 0);
		TEST_EQ (class->oom_adj, 0);

		for (i = 0; i < RLIMIT_NLIMITS; i++)
			TEST_EQ_P (class->limits[i], NULL);

		TEST_EQ_P (class->chroot, NULL);
		TEST_EQ_P (class->chdir, NULL);
		TEST_FALSE (class->deleted);

		nih_free (class);
	}
}


void
test_consider (void)
{
	ConfSource *source1, *source2, *source3;
	ConfFile   *file1, *file2, *file3;
	JobClass   *class1, *class2, *class3, *class4, *ptr;
	Job        *job;
	int         ret;

	TEST_FUNCTION ("job_class_consider");
	source1 = conf_source_new (NULL, "/tmp/foo", CONF_DIR);

	source2 = conf_source_new (NULL, "/tmp/bar", CONF_JOB_DIR);

	file1 = conf_file_new (source2, "/tmp/bar/frodo");
	class1 = file1->job = job_class_new (NULL, "frodo");

	file2 = conf_file_new (source2, "/tmp/bar/bilbo");
	class2 = file2->job = job_class_new (NULL, "bilbo");

	source3 = conf_source_new (NULL, "/tmp/baz", CONF_JOB_DIR);

	file3 = conf_file_new (source3, "/tmp/baz/frodo");
	class3 = file3->job = job_class_new (NULL, "frodo");


	/* Check that when there is no registered class and we consider the
	 * best class to use, it becomes the registered class.
	 */
	TEST_FEATURE ("with no registered class and best class");
	ret = job_class_consider (class1);
	ptr = (JobClass *)nih_hash_lookup (job_classes, "frodo");

	TEST_TRUE (ret);
	TEST_EQ_P (ptr, class1);

	nih_list_remove (&class1->entry);


	/* Check that when there is no registered class and we consider a
	 * class that is not the best to use, what should be the best
	 * becomes the registered class.  In practice, this eventuality
	 * should obviously never happen.
	 */
	TEST_FEATURE ("with no registered class and not best class");
	ret = job_class_consider (class3);
	ptr = (JobClass *)nih_hash_lookup (job_classes, "frodo");

	TEST_FALSE (ret);
	TEST_EQ_P (ptr, class1);

	nih_list_remove (&class1->entry);


	/* Check that when there is a registered class that cannot be
	 * replaced because it has an active job, it is not replaced, even
	 * if our class is better.
	 */
	TEST_FEATURE ("with registered class that cannot be replaced");
	job = job_new (class3, NULL);
	job->goal = JOB_START;
	job->state = JOB_RUNNING;

	nih_hash_add (job_classes, &class3->entry);

	ret = job_class_consider (class1);
	ptr = (JobClass *)nih_hash_lookup (job_classes, "frodo");

	TEST_FALSE (ret);
	TEST_EQ_P (ptr, class3);

	nih_free (job);
	nih_list_remove (&class3->entry);


	/* Check that when there is a registered class that can be
	 * replaced, and our class is the best replacement, our class
	 * becomes the hash table member.
	 */
	TEST_FEATURE ("with replacable registered class and best class");
	nih_hash_add (job_classes, &class3->entry);

	ret = job_class_consider (class1);
	ptr = (JobClass *)nih_hash_lookup (job_classes, "frodo");

	TEST_TRUE (ret);
	TEST_EQ_P (ptr, class1);

	TEST_LIST_EMPTY (&class3->entry);

	nih_list_remove (&class1->entry);


	/* Check that when there is a registered class that can be
	 * replaced, and our class is not the best replacement, the best
	 * becomes the hash table member.
	 */
	TEST_FEATURE ("with replacable registered class and not best class");
	class4 = job_class_new (NULL, "frodo");
	nih_hash_add (job_classes, &class4->entry);

	ret = job_class_consider (class3);
	ptr = (JobClass *)nih_hash_lookup (job_classes, "frodo");

	TEST_FALSE (ret);
	TEST_EQ_P (ptr, class1);

	TEST_LIST_EMPTY (&class4->entry);

	nih_list_remove (&class1->entry);

	nih_free (class4);


	nih_free (source3);
	nih_free (source2);
	nih_free (source1);
}

void
test_reconsider (void)
{
	ConfSource *source1, *source2, *source3;
	ConfFile   *file1, *file2, *file3;
	JobClass   *class1, *class2, *class3, *class4, *ptr;
	Job        *job;
	int         ret;

	TEST_FUNCTION ("job_class_reconsider");
	source1 = conf_source_new (NULL, "/tmp/foo", CONF_DIR);

	source2 = conf_source_new (NULL, "/tmp/bar", CONF_JOB_DIR);

	file1 = conf_file_new (source2, "/tmp/bar/frodo");
	class1 = file1->job = job_class_new (NULL, "frodo");

	file2 = conf_file_new (source2, "/tmp/bar/bilbo");
	class2 = file2->job = job_class_new (NULL, "bilbo");

	source3 = conf_source_new (NULL, "/tmp/baz", CONF_JOB_DIR);

	file3 = conf_file_new (source3, "/tmp/baz/frodo");
	class3 = file3->job = job_class_new (NULL, "frodo");


	/* Check that when we reconsider the registered class and it is
	 * still the best class, it remains the registered class.
	 */
	TEST_FEATURE ("with registered best class");
	nih_hash_add (job_classes, &class1->entry);

	ret = job_class_reconsider (class1);
	ptr = (JobClass *)nih_hash_lookup (job_classes, "frodo");

	TEST_FALSE (ret);
	TEST_EQ_P (ptr, class1);

	nih_list_remove (&class1->entry);


	/* Check that when we reconsider the registered class and it is
	 * no longer the best class, it is replaced by the best.
	 */
	TEST_FEATURE ("with registered not best class");
	nih_hash_add (job_classes, &class3->entry);

	ret = job_class_reconsider (class3);
	ptr = (JobClass *)nih_hash_lookup (job_classes, "frodo");

	TEST_TRUE (ret);
	TEST_EQ_P (ptr, class1);

	nih_list_remove (&class1->entry);


	/* Check that when we reconsider a class that cannot be replaced,
	 * it is not, even if there is a better.
	 */
	TEST_FEATURE ("with registered not best class that can't be replaced");
	job = job_new (class3, NULL);
	job->goal = JOB_START;
	job->state = JOB_RUNNING;

	nih_hash_add (job_classes, &class3->entry);

	ret = job_class_reconsider (class3);
	ptr = (JobClass *)nih_hash_lookup (job_classes, "frodo");

	TEST_FALSE (ret);
	TEST_EQ_P (ptr, class3);

	nih_free (job);
	nih_list_remove (&class3->entry);


	/* Check that if the class we reconsidered is not the registered
	 * class, an election is not forced.
	 */
	TEST_FEATURE ("with unregistered class");
	nih_hash_add (job_classes, &class3->entry);

	ret = job_class_reconsider (class1);
	ptr = (JobClass *)nih_hash_lookup (job_classes, "frodo");

	TEST_TRUE (ret);
	TEST_EQ_P (ptr, class3);

	nih_list_remove (&class3->entry);


	/* Check that if there is no registered class, an election is
	 * not forced.
	 */
	TEST_FEATURE ("with no registered class");
	ret = job_class_reconsider (class1);
	ptr = (JobClass *)nih_hash_lookup (job_classes, "frodo");

	TEST_TRUE (ret);
	TEST_EQ_P (ptr, NULL);


	/* Check that when there are no more classes left to consider,
	 * the registered class is simply removed.
	 */
	TEST_FEATURE ("with no classes to replace it");
	nih_free (source3);
	nih_free (source2);
	nih_free (source1);

	class4 = job_class_new (NULL, "frodo");
	nih_hash_add (job_classes, &class4->entry);

	ret = job_class_reconsider (class4);
	ptr = (JobClass *)nih_hash_lookup (job_classes, "frodo");

	TEST_TRUE (ret);
	TEST_EQ_P (ptr, NULL);

	nih_free (class4);
}


void
test_environment (void)
{
	JobClass  *class;
	char     **env;
	size_t     len;

	TEST_FUNCTION ("job_class_environment");

	/* Check that a job class created with an empty environment will
	 * just have the built-ins in the returned environment.
	 */
	TEST_FEATURE ("with no configured environment");
	class = job_class_new (NULL, "test");

	TEST_ALLOC_FAIL {
		env = job_class_environment (NULL, class, &len);

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

	nih_free (class);


	/* Check that a job class created with defined environment variables
	 * will have those appended to the environment as well as the builtins.
	 */
	TEST_FEATURE ("with configured environment");
	class = job_class_new (NULL, "test");

	class->env = nih_str_array_new (class);
	assert (nih_str_array_add (&(class->env), class, NULL, "FOO=BAR"));
	assert (nih_str_array_add (&(class->env), class, NULL, "BAR=BAZ"));

	TEST_ALLOC_FAIL {
		env = job_class_environment (NULL, class, &len);

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

	nih_free (class);


	/* Check that configured environment override built-ins.
	 */
	TEST_FEATURE ("with configuration overriding built-ins");
	class = job_class_new (NULL, "test");

	class->env = nih_str_array_new (class);
	assert (nih_str_array_add (&(class->env), class, NULL, "FOO=BAR"));
	assert (nih_str_array_add (&(class->env), class, NULL, "BAR=BAZ"));
	assert (nih_str_array_add (&(class->env), class, NULL, "TERM=elmo"));

	TEST_ALLOC_FAIL {
		env = job_class_environment (NULL, class, &len);

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

	nih_free (class);
}


int
main (int   argc,
      char *argv[])
{
	test_new ();
	test_consider ();
	test_reconsider ();
	test_environment ();

	return 0;
}
