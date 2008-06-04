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
#include <nih/error.h>
#include <nih/errors.h>

#include <nih/dbus.h>

#include "event.h"
#include "job.h"
#include "conf.h"
#include "control.h"


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

		TEST_ALLOC_PARENT (class->instance, class);
		TEST_EQ_STR (class->instance, "");

		TEST_ALLOC_PARENT (class->instances, class);
		TEST_ALLOC_SIZE (class->instances, sizeof (NihHash));
		TEST_HASH_EMPTY (class->instances);

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
	pid_t           dbus_pid;
	DBusError       dbus_error;
	DBusConnection *conn, *client_conn;
	DBusMessage    *message;
	NihListEntry   *entry;
	NihDBusObject  *object;
	ConfSource     *source1, *source2, *source3;
	ConfFile       *file1, *file2, *file3;
	JobClass       *class1, *class2, *class3, *class4, *ptr;
	Job            *job;
	char           *path;
	int             ret;

	TEST_FUNCTION ("job_class_consider");
	dbus_error_init (&dbus_error);

	TEST_DBUS (dbus_pid);

	client_conn = dbus_bus_get (DBUS_BUS_SYSTEM, NULL);
	assert (client_conn != NULL);
	dbus_connection_set_exit_on_disconnect (client_conn, FALSE);

	dbus_bus_add_match (client_conn, "type='signal'", &dbus_error);
	assert (! dbus_error_is_set (&dbus_error));


	assert (conn = nih_dbus_bus (DBUS_BUS_SYSTEM, NULL));

	while (! (message = dbus_connection_pop_message (client_conn)))
		dbus_connection_read_write (client_conn, -1);

	assert (dbus_message_is_signal (message, DBUS_INTERFACE_DBUS,
					"NameAcquired"));

	dbus_message_unref (message);


	source1 = conf_source_new (NULL, "/tmp/foo", CONF_DIR);

	source2 = conf_source_new (NULL, "/tmp/bar", CONF_JOB_DIR);

	file1 = conf_file_new (source2, "/tmp/bar/frodo");
	class1 = file1->job = job_class_new (NULL, "frodo");

	file2 = conf_file_new (source2, "/tmp/bar/bilbo");
	class2 = file2->job = job_class_new (NULL, "bilbo");

	source3 = conf_source_new (NULL, "/tmp/baz", CONF_JOB_DIR);

	file3 = conf_file_new (source3, "/tmp/baz/frodo");
	class3 = file3->job = job_class_new (NULL, "frodo");


	control_init ();

	entry = nih_list_entry_new (NULL);
	entry->data = conn;
	nih_list_add (control_conns, &entry->entry);


	/* Check that when there is no registered class and we consider the
	 * best class to use, it becomes the registered class.
	 */
	TEST_FEATURE ("with no registered class and best class");
	ret = job_class_consider (class1);
	ptr = (JobClass *)nih_hash_lookup (job_classes, "frodo");

	TEST_TRUE (ret);
	TEST_EQ_P (ptr, class1);

	TEST_TRUE (dbus_connection_get_object_path_data (conn,
							 class1->path,
							 (void **)&object));

	TEST_ALLOC_SIZE (object, sizeof (NihDBusObject));
	TEST_EQ_STR (object->path, class1->path);
	TEST_EQ_P (object->data, class1);

	dbus_connection_flush (conn);

	while (! (message = dbus_connection_pop_message (client_conn)))
		dbus_connection_read_write (client_conn, -1);

	TEST_TRUE (dbus_message_is_signal (message, "com.ubuntu.Upstart",
					   "JobAdded"));

	TEST_TRUE (dbus_message_get_args (message, NULL,
					  DBUS_TYPE_OBJECT_PATH, &path,
					  DBUS_TYPE_INVALID));

	TEST_EQ_STR (path, class1->path);

	dbus_message_unref (message);

	nih_list_remove (&class1->entry);
	dbus_connection_unregister_object_path (conn, class1->path);


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

	TEST_TRUE (dbus_connection_get_object_path_data (conn,
							 class1->path,
							 (void **)&object));

	TEST_ALLOC_SIZE (object, sizeof (NihDBusObject));
	TEST_EQ_STR (object->path, class1->path);
	TEST_EQ_P (object->data, class1);

	dbus_connection_flush (conn);

	while (! (message = dbus_connection_pop_message (client_conn)))
		dbus_connection_read_write (client_conn, -1);

	TEST_TRUE (dbus_message_is_signal (message, "com.ubuntu.Upstart",
					   "JobAdded"));

	TEST_TRUE (dbus_message_get_args (message, NULL,
					  DBUS_TYPE_OBJECT_PATH, &path,
					  DBUS_TYPE_INVALID));

	TEST_EQ_STR (path, class1->path);

	dbus_message_unref (message);

	nih_list_remove (&class1->entry);
	dbus_connection_unregister_object_path (conn, class1->path);


	/* Check that when there is a registered class that cannot be
	 * replaced because it has an active job, it is not replaced, even
	 * if our class is better.
	 */
	TEST_FEATURE ("with registered class that cannot be replaced");
	nih_list_remove (&entry->entry);

	job = job_new (class3, "");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;

	nih_list_add (control_conns, &entry->entry);

	nih_hash_add (job_classes, &class3->entry);
	job_class_register (class3, conn, FALSE);

	ret = job_class_consider (class1);
	ptr = (JobClass *)nih_hash_lookup (job_classes, "frodo");

	TEST_FALSE (ret);
	TEST_EQ_P (ptr, class3);

	TEST_TRUE (dbus_connection_get_object_path_data (conn,
							 class3->path,
							 (void **)&object));
	TEST_ALLOC_SIZE (object, sizeof (NihDBusObject));
	TEST_EQ_STR (object->path, class3->path);
	TEST_EQ_P (object->data, class3);

	message = dbus_message_new_signal ("/", "com.ubuntu.Upstart.Test",
					   "TestPassed");
	assert (message != NULL);

	dbus_connection_send (conn, message, NULL);

	dbus_message_unref (message);

	dbus_connection_flush (conn);

	while (! (message = dbus_connection_pop_message (client_conn)))
		dbus_connection_read_write (client_conn, -1);

	TEST_TRUE (dbus_message_is_signal (message, "com.ubuntu.Upstart.Test",
					   "TestPassed"));

	dbus_message_unref (message);

	nih_free (job);
	nih_list_remove (&class3->entry);
	dbus_connection_unregister_object_path (conn, class3->path);


	/* Check that when there is a registered class that can be
	 * replaced, and our class is the best replacement, our class
	 * becomes the hash table member.
	 */
	TEST_FEATURE ("with replacable registered class and best class");
	nih_hash_add (job_classes, &class3->entry);
	job_class_register (class3, conn, FALSE);

	ret = job_class_consider (class1);
	ptr = (JobClass *)nih_hash_lookup (job_classes, "frodo");

	TEST_TRUE (ret);
	TEST_EQ_P (ptr, class1);

	TEST_TRUE (dbus_connection_get_object_path_data (conn,
							 class1->path,
							 (void **)&object));

	TEST_ALLOC_SIZE (object, sizeof (NihDBusObject));
	TEST_EQ_STR (object->path, class1->path);
	TEST_EQ_P (object->data, class1);

	TEST_LIST_EMPTY (&class3->entry);

	dbus_connection_flush (conn);

	while (! (message = dbus_connection_pop_message (client_conn)))
		dbus_connection_read_write (client_conn, -1);

	TEST_TRUE (dbus_message_is_signal (message, "com.ubuntu.Upstart",
					   "JobRemoved"));

	TEST_TRUE (dbus_message_get_args (message, NULL,
					  DBUS_TYPE_OBJECT_PATH, &path,
					  DBUS_TYPE_INVALID));

	TEST_EQ_STR (path, class3->path);

	dbus_message_unref (message);

	while (! (message = dbus_connection_pop_message (client_conn)))
		dbus_connection_read_write (client_conn, -1);

	TEST_TRUE (dbus_message_is_signal (message, "com.ubuntu.Upstart",
					   "JobAdded"));

	TEST_TRUE (dbus_message_get_args (message, NULL,
					  DBUS_TYPE_OBJECT_PATH, &path,
					  DBUS_TYPE_INVALID));

	TEST_EQ_STR (path, class1->path);

	dbus_message_unref (message);

	nih_list_remove (&class1->entry);
	dbus_connection_unregister_object_path (conn, class1->path);


	/* Check that when there is a registered class that can be
	 * replaced, and our class is not the best replacement, the best
	 * becomes the hash table member.
	 */
	TEST_FEATURE ("with replacable registered class and not best class");
	class4 = job_class_new (NULL, "frodo");
	nih_hash_add (job_classes, &class4->entry);
	job_class_register (class4, conn, FALSE);

	ret = job_class_consider (class3);
	ptr = (JobClass *)nih_hash_lookup (job_classes, "frodo");

	TEST_FALSE (ret);
	TEST_EQ_P (ptr, class1);

	TEST_TRUE (dbus_connection_get_object_path_data (conn,
							 class1->path,
							 (void **)&object));

	TEST_ALLOC_SIZE (object, sizeof (NihDBusObject));
	TEST_EQ_STR (object->path, class1->path);
	TEST_EQ_P (object->data, class1);

	TEST_LIST_EMPTY (&class4->entry);

	dbus_connection_flush (conn);

	while (! (message = dbus_connection_pop_message (client_conn)))
		dbus_connection_read_write (client_conn, -1);

	TEST_TRUE (dbus_message_is_signal (message, "com.ubuntu.Upstart",
					   "JobRemoved"));

	TEST_TRUE (dbus_message_get_args (message, NULL,
					  DBUS_TYPE_OBJECT_PATH, &path,
					  DBUS_TYPE_INVALID));

	TEST_EQ_STR (path, class4->path);

	dbus_message_unref (message);

	while (! (message = dbus_connection_pop_message (client_conn)))
		dbus_connection_read_write (client_conn, -1);

	TEST_TRUE (dbus_message_is_signal (message, "com.ubuntu.Upstart",
					   "JobAdded"));

	TEST_TRUE (dbus_message_get_args (message, NULL,
					  DBUS_TYPE_OBJECT_PATH, &path,
					  DBUS_TYPE_INVALID));

	TEST_EQ_STR (path, class1->path);

	dbus_message_unref (message);

	nih_list_remove (&class1->entry);
	dbus_connection_unregister_object_path (conn, class1->path);

	nih_free (class4);


	nih_free (source3);
	nih_free (source2);
	nih_free (source1);


	nih_free (entry);

	dbus_connection_unref (conn);

	dbus_connection_unref (client_conn);

	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}

void
test_reconsider (void)
{
	pid_t           dbus_pid;
	DBusError       dbus_error;
	DBusConnection *conn, *client_conn;
	DBusMessage    *message;
	NihListEntry   *entry;
	NihDBusObject  *object;
	ConfSource     *source1, *source2, *source3;
	ConfFile       *file1, *file2, *file3;
	JobClass       *class1, *class2, *class3, *class4, *ptr;
	Job            *job;
	char           *path;
	int             ret;

	TEST_FUNCTION ("job_class_reconsider");
	dbus_error_init (&dbus_error);

	TEST_DBUS (dbus_pid);

	client_conn = dbus_bus_get (DBUS_BUS_SYSTEM, NULL);
	assert (client_conn != NULL);
	dbus_connection_set_exit_on_disconnect (client_conn, FALSE);

	dbus_bus_add_match (client_conn, "type='signal'", &dbus_error);
	assert (! dbus_error_is_set (&dbus_error));


	assert (conn = nih_dbus_bus (DBUS_BUS_SYSTEM, NULL));

	while (! (message = dbus_connection_pop_message (client_conn)))
		dbus_connection_read_write (client_conn, -1);

	assert (dbus_message_is_signal (message, DBUS_INTERFACE_DBUS,
					"NameAcquired"));

	dbus_message_unref (message);


	source1 = conf_source_new (NULL, "/tmp/foo", CONF_DIR);

	source2 = conf_source_new (NULL, "/tmp/bar", CONF_JOB_DIR);

	file1 = conf_file_new (source2, "/tmp/bar/frodo");
	class1 = file1->job = job_class_new (NULL, "frodo");

	file2 = conf_file_new (source2, "/tmp/bar/bilbo");
	class2 = file2->job = job_class_new (NULL, "bilbo");

	source3 = conf_source_new (NULL, "/tmp/baz", CONF_JOB_DIR);

	file3 = conf_file_new (source3, "/tmp/baz/frodo");
	class3 = file3->job = job_class_new (NULL, "frodo");


	control_init ();

	entry = nih_list_entry_new (NULL);
	entry->data = conn;
	nih_list_add (control_conns, &entry->entry);


	/* Check that when we reconsider the registered class and it is
	 * still the best class, it remains the registered class.
	 */
	TEST_FEATURE ("with registered best class");
	nih_hash_add (job_classes, &class1->entry);
	job_class_register (class1, conn, FALSE);

	ret = job_class_reconsider (class1);
	ptr = (JobClass *)nih_hash_lookup (job_classes, "frodo");

	TEST_FALSE (ret);
	TEST_EQ_P (ptr, class1);

	TEST_TRUE (dbus_connection_get_object_path_data (conn,
							 class1->path,
							 (void **)&object));

	TEST_ALLOC_SIZE (object, sizeof (NihDBusObject));
	TEST_EQ_STR (object->path, class1->path);
	TEST_EQ_P (object->data, class1);

	message = dbus_message_new_signal ("/", "com.ubuntu.Upstart.Test",
					   "TestPassed");
	assert (message != NULL);

	dbus_connection_send (conn, message, NULL);

	dbus_message_unref (message);

	dbus_connection_flush (conn);

	while (! (message = dbus_connection_pop_message (client_conn)))
		dbus_connection_read_write (client_conn, -1);

	TEST_TRUE (dbus_message_is_signal (message, "com.ubuntu.Upstart.Test",
					   "TestPassed"));

	dbus_message_unref (message);

	nih_list_remove (&class1->entry);
	dbus_connection_unregister_object_path (conn, class1->path);


	/* Check that when we reconsider the registered class and it is
	 * no longer the best class, it is replaced by the best.
	 */
	TEST_FEATURE ("with registered not best class");
	nih_hash_add (job_classes, &class3->entry);
	job_class_register (class3, conn, FALSE);

	ret = job_class_reconsider (class3);
	ptr = (JobClass *)nih_hash_lookup (job_classes, "frodo");

	TEST_TRUE (ret);
	TEST_EQ_P (ptr, class1);

	TEST_TRUE (dbus_connection_get_object_path_data (conn,
							 class1->path,
							 (void **)&object));

	TEST_ALLOC_SIZE (object, sizeof (NihDBusObject));
	TEST_EQ_STR (object->path, class1->path);
	TEST_EQ_P (object->data, class1);

	dbus_connection_flush (conn);

	while (! (message = dbus_connection_pop_message (client_conn)))
		dbus_connection_read_write (client_conn, -1);

	TEST_TRUE (dbus_message_is_signal (message, "com.ubuntu.Upstart",
					   "JobRemoved"));

	TEST_TRUE (dbus_message_get_args (message, NULL,
					  DBUS_TYPE_OBJECT_PATH, &path,
					  DBUS_TYPE_INVALID));

	TEST_EQ_STR (path, class3->path);

	dbus_message_unref (message);

	while (! (message = dbus_connection_pop_message (client_conn)))
		dbus_connection_read_write (client_conn, -1);

	TEST_TRUE (dbus_message_is_signal (message, "com.ubuntu.Upstart",
					   "JobAdded"));

	TEST_TRUE (dbus_message_get_args (message, NULL,
					  DBUS_TYPE_OBJECT_PATH, &path,
					  DBUS_TYPE_INVALID));

	TEST_EQ_STR (path, class1->path);

	dbus_message_unref (message);

	nih_list_remove (&class1->entry);
	dbus_connection_unregister_object_path (conn, class1->path);


	/* Check that when we reconsider a class that cannot be replaced,
	 * it is not, even if there is a better.
	 */
	TEST_FEATURE ("with registered not best class that can't be replaced");
	nih_list_remove (&entry->entry);

	job = job_new (class3, "");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;

	nih_list_add (control_conns, &entry->entry);

	nih_hash_add (job_classes, &class3->entry);
	job_class_register (class3, conn, FALSE);

	ret = job_class_reconsider (class3);
	ptr = (JobClass *)nih_hash_lookup (job_classes, "frodo");

	TEST_FALSE (ret);
	TEST_EQ_P (ptr, class3);

	TEST_TRUE (dbus_connection_get_object_path_data (conn,
							 class3->path,
							 (void **)&object));

	TEST_ALLOC_SIZE (object, sizeof (NihDBusObject));
	TEST_EQ_STR (object->path, class3->path);
	TEST_EQ_P (object->data, class3);

	message = dbus_message_new_signal ("/", "com.ubuntu.Upstart.Test",
					   "TestPassed");
	assert (message != NULL);

	dbus_connection_send (conn, message, NULL);

	dbus_message_unref (message);

	dbus_connection_flush (conn);

	while (! (message = dbus_connection_pop_message (client_conn)))
		dbus_connection_read_write (client_conn, -1);

	TEST_TRUE (dbus_message_is_signal (message, "com.ubuntu.Upstart.Test",
					   "TestPassed"));

	dbus_message_unref (message);

	nih_free (job);
	nih_list_remove (&class3->entry);
	dbus_connection_unregister_object_path (conn, class3->path);


	/* Check that if the class we reconsidered is not the registered
	 * class, an election is not forced.
	 */
	TEST_FEATURE ("with unregistered class");
	nih_hash_add (job_classes, &class3->entry);
	job_class_register (class3, conn, FALSE);

	ret = job_class_reconsider (class1);
	ptr = (JobClass *)nih_hash_lookup (job_classes, "frodo");

	TEST_TRUE (ret);
	TEST_EQ_P (ptr, class3);

	TEST_TRUE (dbus_connection_get_object_path_data (conn,
							 class3->path,
							 (void **)&object));

	TEST_ALLOC_SIZE (object, sizeof (NihDBusObject));
	TEST_EQ_STR (object->path, class3->path);
	TEST_EQ_P (object->data, class3);

	message = dbus_message_new_signal ("/", "com.ubuntu.Upstart.Test",
					   "TestPassed");
	assert (message != NULL);

	dbus_connection_send (conn, message, NULL);

	dbus_message_unref (message);

	dbus_connection_flush (conn);

	while (! (message = dbus_connection_pop_message (client_conn)))
		dbus_connection_read_write (client_conn, -1);

	TEST_TRUE (dbus_message_is_signal (message, "com.ubuntu.Upstart.Test",
					   "TestPassed"));

	dbus_message_unref (message);

	nih_list_remove (&class3->entry);
	dbus_connection_unregister_object_path (conn, class3->path);


	/* Check that if there is no registered class, an election is
	 * not forced.
	 */
	TEST_FEATURE ("with no registered class");
	ret = job_class_reconsider (class1);
	ptr = (JobClass *)nih_hash_lookup (job_classes, "frodo");

	TEST_TRUE (ret);
	TEST_EQ_P (ptr, NULL);

	TEST_TRUE (dbus_connection_get_object_path_data (conn,
							 class1->path,
							 (void **)&object));
	TEST_EQ_P (object, NULL);

	message = dbus_message_new_signal ("/", "com.ubuntu.Upstart.Test",
					   "TestPassed");
	assert (message != NULL);

	dbus_connection_send (conn, message, NULL);

	dbus_message_unref (message);

	dbus_connection_flush (conn);

	while (! (message = dbus_connection_pop_message (client_conn)))
		dbus_connection_read_write (client_conn, -1);

	TEST_TRUE (dbus_message_is_signal (message, "com.ubuntu.Upstart.Test",
					   "TestPassed"));

	dbus_message_unref (message);


	/* Check that when there are no more classes left to consider,
	 * the registered class is simply removed.
	 */
	TEST_FEATURE ("with no classes to replace it");
	nih_free (source3);
	nih_free (source2);
	nih_free (source1);

	class4 = job_class_new (NULL, "frodo");
	nih_hash_add (job_classes, &class4->entry);
	job_class_register (class4, conn, FALSE);

	ret = job_class_reconsider (class4);
	ptr = (JobClass *)nih_hash_lookup (job_classes, "frodo");

	TEST_TRUE (ret);
	TEST_EQ_P (ptr, NULL);

	TEST_TRUE (dbus_connection_get_object_path_data (conn,
							 class4->path,
							 (void **)&object));
	TEST_EQ_P (object, NULL);

	dbus_connection_flush (conn);

	while (! (message = dbus_connection_pop_message (client_conn)))
		dbus_connection_read_write (client_conn, -1);

	TEST_TRUE (dbus_message_is_signal (message, "com.ubuntu.Upstart",
					   "JobRemoved"));

	TEST_TRUE (dbus_message_get_args (message, NULL,
					  DBUS_TYPE_OBJECT_PATH, &path,
					  DBUS_TYPE_INVALID));

	TEST_EQ_STR (path, class4->path);

	dbus_message_unref (message);

	nih_free (class4);


	nih_free (entry);

	dbus_connection_unref (conn);

	dbus_connection_unref (client_conn);

	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}


void
test_register (void)
{
	pid_t           dbus_pid;
	DBusError       dbus_error;
	DBusConnection *conn, *client_conn;
	DBusMessage    *message;
	JobClass       *class;
	NihDBusObject  *object;
	char           *path;

	TEST_FUNCTION ("job_class_register");
	dbus_error_init (&dbus_error);

	TEST_DBUS (dbus_pid);

	client_conn = dbus_bus_get (DBUS_BUS_SYSTEM, NULL);
	assert (client_conn != NULL);
	dbus_connection_set_exit_on_disconnect (client_conn, FALSE);

	dbus_bus_add_match (client_conn, "type='signal'", &dbus_error);
	assert (! dbus_error_is_set (&dbus_error));


	assert (conn = nih_dbus_bus (DBUS_BUS_SYSTEM, NULL));

	while (! (message = dbus_connection_pop_message (client_conn)))
		dbus_connection_read_write (client_conn, -1);

	assert (dbus_message_is_signal (message, DBUS_INTERFACE_DBUS,
					"NameAcquired"));

	dbus_message_unref (message);


	/* Check that we can register an existing job class on the bus
	 * using its path and that the JobAdded signal is emitted to
	 * announce it.
	 */
	TEST_FEATURE ("with signal emission");
	class = job_class_new (NULL, "test");

	assert (dbus_connection_get_object_path_data (conn, class->path,
						      (void **)&object));
	assert (object == NULL);

	job_class_register (class, conn, TRUE);

	TEST_TRUE (dbus_connection_get_object_path_data (conn,
							 class->path,
							 (void **)&object));

	TEST_ALLOC_SIZE (object, sizeof (NihDBusObject));
	TEST_EQ_STR (object->path, class->path);
	TEST_EQ_P (object->data, class);

	dbus_connection_flush (conn);

	while (! (message = dbus_connection_pop_message (client_conn)))
		dbus_connection_read_write (client_conn, -1);

	TEST_TRUE (dbus_message_is_signal (message, "com.ubuntu.Upstart",
					   "JobAdded"));

	TEST_TRUE (dbus_message_get_args (message, NULL,
					  DBUS_TYPE_OBJECT_PATH, &path,
					  DBUS_TYPE_INVALID));

	TEST_EQ_STR (path, class->path);

	dbus_message_unref (message);

	nih_free (class);


	/* Check that we can register the job without emitting the signal
	 * by emitting a signal immediately afterwards.
	 */
	TEST_FEATURE ("without signal emission");
	class = job_class_new (NULL, "test");

	assert (dbus_connection_get_object_path_data (conn, class->path,
						      (void **)&object));
	assert (object == NULL);

	job_class_register (class, conn, FALSE);

	TEST_TRUE (dbus_connection_get_object_path_data (conn,
							 class->path,
							 (void **)&object));

	TEST_ALLOC_SIZE (object, sizeof (NihDBusObject));
	TEST_EQ_STR (object->path, class->path);
	TEST_EQ_P (object->data, class);

	message = dbus_message_new_signal ("/", "com.ubuntu.Upstart.Test",
					   "TestPassed");
	assert (message != NULL);

	dbus_connection_send (conn, message, NULL);

	dbus_message_unref (message);

	dbus_connection_flush (conn);

	while (! (message = dbus_connection_pop_message (client_conn)))
		dbus_connection_read_write (client_conn, -1);

	TEST_TRUE (dbus_message_is_signal (message, "com.ubuntu.Upstart.Test",
					   "TestPassed"));

	dbus_message_unref (message);

	nih_free (class);


	dbus_connection_unref (conn);

	dbus_connection_unref (client_conn);

	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}

void
test_unregister (void)
{
	pid_t           dbus_pid;
	DBusError       dbus_error;
	DBusConnection *conn, *client_conn;
	DBusMessage    *message;
	JobClass       *class;
	NihDBusObject  *object;
	char           *path;

	/* Check that we can unregister an object for a job class from
	 * the bus and that the JobRemoved signal is emitted as a result.
	 * Don't worry about its instances, we can never unregister while
	 * it has them.
	 */
	TEST_FUNCTION ("job_class_unregister");
	dbus_error_init (&dbus_error);

	TEST_DBUS (dbus_pid);

	client_conn = dbus_bus_get (DBUS_BUS_SYSTEM, NULL);
	assert (client_conn != NULL);
	dbus_connection_set_exit_on_disconnect (client_conn, FALSE);

	dbus_bus_add_match (client_conn, "type='signal'", &dbus_error);
	assert (! dbus_error_is_set (&dbus_error));


	assert (conn = nih_dbus_bus (DBUS_BUS_SYSTEM, NULL));

	while (! (message = dbus_connection_pop_message (client_conn)))
		dbus_connection_read_write (client_conn, -1);

	assert (dbus_message_is_signal (message, DBUS_INTERFACE_DBUS,
					"NameAcquired"));

	dbus_message_unref (message);


	class = job_class_new (NULL, "test");

	assert (dbus_connection_get_object_path_data (conn, class->path,
						      (void **)&object));
	assert (object == NULL);

	job_class_register (class, conn, FALSE);

	assert (dbus_connection_get_object_path_data (conn, class->path,
						      (void **)&object));
	assert (object != NULL);
	assert (object->data == class);

	job_class_unregister (class, conn);

	TEST_TRUE (dbus_connection_get_object_path_data (conn,
							 class->path,
							 (void **)&object));
	TEST_EQ_P (object, NULL);

	dbus_connection_flush (conn);

	while (! (message = dbus_connection_pop_message (client_conn)))
		dbus_connection_read_write (client_conn, -1);

	TEST_TRUE (dbus_message_is_signal (message, "com.ubuntu.Upstart",
					   "JobRemoved"));

	TEST_TRUE (dbus_message_get_args (message, NULL,
					  DBUS_TYPE_OBJECT_PATH, &path,
					  DBUS_TYPE_INVALID));

	TEST_EQ_STR (path, class->path);

	dbus_message_unref (message);

	nih_free (class);


	dbus_connection_unref (conn);

	dbus_connection_unref (client_conn);

	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
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


void
test_get_instance_by_name (void)
{
	NihDBusMessage *message;
	JobClass       *class;
	Job            *job;
	char           *path;
	NihError       *error;
	NihDBusError   *dbus_error;
	int             ret;

	TEST_FUNCTION ("job_class_get_instance_by_name");
	nih_error_init ();

	class = job_class_new (NULL, "test");


	/* Check that when given a known instance name, the path to that
	 * instance is returned as a duplicate child of the message
	 * structure.
	 */
	TEST_FEATURE ("with known job");
	job = job_new (class, "foo");

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->conn = NULL;
			message->message = NULL;
		}

		ret = job_class_get_instance_by_name (class, message,
						      "foo", &path);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);

			continue;
		}

		TEST_EQ (ret, 0);

		TEST_ALLOC_PARENT (path, message);
		TEST_EQ_STR (path, job->path);

		nih_free (message);
	}

	nih_free (job);


	/* Check that when given the name of the singleton instance, the
	 * path to that instance is returned as a duplicate child of the
	 * message structure.
	 */
	TEST_FEATURE ("with singleton job");
	job = job_new (class, "");

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->conn = NULL;
			message->message = NULL;
		}

		ret = job_class_get_instance_by_name (class, message,
						      "", &path);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);

			continue;
		}

		TEST_EQ (ret, 0);

		TEST_ALLOC_PARENT (path, message);
		TEST_EQ_STR (path, job->path);

		nih_free (message);
	}

	nih_free (job);


	/* Check that when given an unknown instance name, an unknown
	 * instance D-Bus error is raised and an error returned.
	 */
	TEST_FEATURE ("with unknown instance");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->conn = NULL;
			message->message = NULL;
		}

		ret = job_class_get_instance_by_name (class, message,
						      "foo", &path);

		TEST_LT (ret, 0);

		error = nih_error_get ();
		TEST_EQ (error->number, NIH_DBUS_ERROR);
		TEST_ALLOC_SIZE (error, sizeof (NihDBusError));

		dbus_error = (NihDBusError *)error;
		TEST_EQ_STR (dbus_error->name,
			     "com.ubuntu.Upstart.Error.UnknownInstance");

		nih_free (error);

		nih_free (message);
	}


	nih_free (class);
}

void
test_get_all_instances (void)
{
	NihDBusMessage  *message;
	JobClass        *class;
	Job             *job1, *job2, *job3;
	NihError        *error;
	char           **paths;
	int              ret;

	TEST_FUNCTION ("job_class_get_all_instances");
	nih_error_init ();
	job_class_init ();

	class = job_class_new (NULL, "test");


	/* Check that paths for each of the active instances are returned
	 * in an array allocated as a child of the message structure.
	 */
	TEST_FEATURE ("with active instances");
	job1 = job_new (class, "frodo");
	job2 = job_new (class, "bilbo");
	job3 = job_new (class, "sauron");

	TEST_ALLOC_FAIL {
		int found1 = FALSE, found2 = FALSE, found3 = FALSE, i;

		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->conn = NULL;
			message->message = NULL;
		}

		ret = job_class_get_all_instances (class, message, &paths);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);

			continue;
		}

		TEST_EQ (ret, 0);

		TEST_ALLOC_PARENT (paths, message);
		TEST_ALLOC_SIZE (paths, sizeof (char *) * 4);
		TEST_EQ_P (paths[3], NULL);

		for (i = 0; i < 3; i++) {
			TEST_ALLOC_PARENT (paths[i], paths);

			if (! strcmp (paths[i], job1->path))
				found1 = TRUE;
			if (! strcmp (paths[i], job2->path))
				found2 = TRUE;
			if (! strcmp (paths[i], job3->path))
				found3 = TRUE;
		}

		TEST_TRUE (found1);
		TEST_TRUE (found2);
		TEST_TRUE (found3);

		nih_free (message);
	}

	nih_free (job3);
	nih_free (job2);
	nih_free (job1);


	/* Check that when no instances exist for the given class, an empty
	 * array is returned instead of an error.
	 */
	TEST_FEATURE ("with no instances");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->conn = NULL;
			message->message = NULL;
		}

		ret = job_class_get_all_instances (class, message, &paths);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);

			continue;
		}

		TEST_EQ (ret, 0);

		TEST_ALLOC_PARENT (paths, message);
		TEST_ALLOC_SIZE (paths, sizeof (char *) * 1);
		TEST_EQ_P (paths[0], NULL);

		nih_free (message);
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
	test_register ();
	test_unregister ();
	test_environment ();

	test_get_instance_by_name ();
	test_get_all_instances ();

	return 0;
}
