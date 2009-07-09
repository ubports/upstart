/* upstart
 *
 * test_job_class.c - test suite for init/job_class.c
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
#include <nih-dbus/test_dbus.h>

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

#include <nih-dbus/dbus_error.h>
#include <nih-dbus/dbus_message.h>
#include <nih-dbus/dbus_object.h>
#include <nih-dbus/errors.h>

#include "dbus/upstart.h"

#include "blocked.h"
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
		TEST_EQ_STR (class->path, DBUS_PATH_UPSTART "/jobs/test");

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
	TEST_DBUS_OPEN (conn);
	TEST_DBUS_OPEN (client_conn);

	dbus_bus_add_match (client_conn, "type='signal'", &dbus_error);
	assert (! dbus_error_is_set (&dbus_error));


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

	TEST_DBUS_MESSAGE (client_conn, message);
	TEST_TRUE (dbus_message_is_signal (message, DBUS_INTERFACE_UPSTART,
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

	TEST_DBUS_MESSAGE (client_conn, message);
	TEST_TRUE (dbus_message_is_signal (message, DBUS_INTERFACE_UPSTART,
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

	TEST_DBUS_MESSAGE (client_conn, message);
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

	TEST_DBUS_MESSAGE (client_conn, message);
	TEST_TRUE (dbus_message_is_signal (message, DBUS_INTERFACE_UPSTART,
					   "JobRemoved"));

	TEST_TRUE (dbus_message_get_args (message, NULL,
					  DBUS_TYPE_OBJECT_PATH, &path,
					  DBUS_TYPE_INVALID));

	TEST_EQ_STR (path, class3->path);

	dbus_message_unref (message);

	TEST_DBUS_MESSAGE (client_conn, message);
	TEST_TRUE (dbus_message_is_signal (message, DBUS_INTERFACE_UPSTART,
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

	TEST_DBUS_MESSAGE (client_conn, message);
	TEST_TRUE (dbus_message_is_signal (message, DBUS_INTERFACE_UPSTART,
					   "JobRemoved"));

	TEST_TRUE (dbus_message_get_args (message, NULL,
					  DBUS_TYPE_OBJECT_PATH, &path,
					  DBUS_TYPE_INVALID));

	TEST_EQ_STR (path, class4->path);

	dbus_message_unref (message);

	TEST_DBUS_MESSAGE (client_conn, message);
	TEST_TRUE (dbus_message_is_signal (message, DBUS_INTERFACE_UPSTART,
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

	TEST_DBUS_CLOSE (conn);
	TEST_DBUS_CLOSE (client_conn);
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
	TEST_DBUS_OPEN (conn);
	TEST_DBUS_OPEN (client_conn);

	dbus_bus_add_match (client_conn, "type='signal'", &dbus_error);
	assert (! dbus_error_is_set (&dbus_error));


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

	TEST_DBUS_MESSAGE (client_conn, message);
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

	TEST_DBUS_MESSAGE (client_conn, message);
	TEST_TRUE (dbus_message_is_signal (message, DBUS_INTERFACE_UPSTART,
					   "JobRemoved"));

	TEST_TRUE (dbus_message_get_args (message, NULL,
					  DBUS_TYPE_OBJECT_PATH, &path,
					  DBUS_TYPE_INVALID));

	TEST_EQ_STR (path, class3->path);

	dbus_message_unref (message);

	TEST_DBUS_MESSAGE (client_conn, message);
	TEST_TRUE (dbus_message_is_signal (message, DBUS_INTERFACE_UPSTART,
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

	TEST_DBUS_MESSAGE (client_conn, message);
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

	TEST_DBUS_MESSAGE (client_conn, message);
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

	TEST_DBUS_MESSAGE (client_conn, message);
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

	TEST_DBUS_MESSAGE (client_conn, message);
	TEST_TRUE (dbus_message_is_signal (message, DBUS_INTERFACE_UPSTART,
					   "JobRemoved"));

	TEST_TRUE (dbus_message_get_args (message, NULL,
					  DBUS_TYPE_OBJECT_PATH, &path,
					  DBUS_TYPE_INVALID));

	TEST_EQ_STR (path, class4->path);

	dbus_message_unref (message);

	nih_free (class4);


	nih_free (entry);

	TEST_DBUS_CLOSE (conn);
	TEST_DBUS_CLOSE (client_conn);
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
	TEST_DBUS_OPEN (conn);
	TEST_DBUS_OPEN (client_conn);

	dbus_bus_add_match (client_conn, "type='signal'", &dbus_error);
	assert (! dbus_error_is_set (&dbus_error));


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

	TEST_DBUS_MESSAGE (client_conn, message);
	TEST_TRUE (dbus_message_is_signal (message, DBUS_INTERFACE_UPSTART,
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

	TEST_DBUS_MESSAGE (client_conn, message);
	TEST_TRUE (dbus_message_is_signal (message, "com.ubuntu.Upstart.Test",
					   "TestPassed"));

	dbus_message_unref (message);

	nih_free (class);


	TEST_DBUS_CLOSE (conn);
	TEST_DBUS_CLOSE (client_conn);
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
	TEST_DBUS_OPEN (conn);
	TEST_DBUS_OPEN (client_conn);

	dbus_bus_add_match (client_conn, "type='signal'", &dbus_error);
	assert (! dbus_error_is_set (&dbus_error));


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

	TEST_DBUS_MESSAGE (client_conn, message);
	TEST_TRUE (dbus_message_is_signal (message, DBUS_INTERFACE_UPSTART,
					   "JobRemoved"));

	TEST_TRUE (dbus_message_get_args (message, NULL,
					  DBUS_TYPE_OBJECT_PATH, &path,
					  DBUS_TYPE_INVALID));

	TEST_EQ_STR (path, class->path);

	dbus_message_unref (message);

	nih_free (class);


	TEST_DBUS_CLOSE (conn);
	TEST_DBUS_CLOSE (client_conn);
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
test_get_instance (void)
{
	NihDBusMessage  *message = NULL;
	char           **env;
	JobClass        *class = NULL;
	Job             *job = NULL;
	char            *path;
	int              ret;
	NihError        *error;
	NihDBusError    *dbus_error;


	TEST_FUNCTION ("job_class_get_instance");
	nih_error_init ();


	/* Check that we can obtain the path of an existing instance, and
	 * that a copy is returned in the pointer given.
	 */
	TEST_FEATURE ("with running job");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			class = job_class_new (NULL, "test");
			job = job_new (class, "");

			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;

			env = nih_str_array_new (message);
		}

		ret = job_class_get_instance (class, message, env, &path);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);
			nih_free (class);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_ALLOC_PARENT (path, message);
		TEST_EQ_STR (path, job->path);

		nih_free (message);
		nih_free (class);
	}


	/* Check that if there's no such instance, a D-Bus error is raised.
	 */
	TEST_FEATURE ("with unknown job");
	class = job_class_new (NULL, "test");

	message = nih_new (NULL, NihDBusMessage);
	message->connection = NULL;
	message->message = NULL;

	env = nih_str_array_new (message);

	ret = job_class_get_instance (class, message, env, &path);

	TEST_LT (ret, 0);

	error = nih_error_get ();
	TEST_EQ (error->number, NIH_DBUS_ERROR);
	TEST_ALLOC_SIZE (error, sizeof (NihDBusError));

	dbus_error = (NihDBusError *)error;
	TEST_EQ_STR (dbus_error->name,
		     DBUS_INTERFACE_UPSTART ".Error.UnknownInstance");

	nih_free (dbus_error);

	nih_free (message);
	nih_free (class);


	/* Check that the environment parameter is used to locate instances.
	 */
	TEST_FEATURE ("with environment");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			class = job_class_new (NULL, "test");
			class->instance = "$FOO";

			job = job_new (class, "wibble");

			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;

			env = nih_str_array_new (message);
			assert (nih_str_array_add (&env, message, NULL,
						   "FOO=wibble"));
			assert (nih_str_array_add (&env, message, NULL,
						   "BAR=wobble"));
		}

		ret = job_class_get_instance (class, message, env, &path);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);
			nih_free (class);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_ALLOC_PARENT (path, message);
		TEST_EQ_STR (path, job->path);

		nih_free (message);
		nih_free (class);
	}


	/* Check that if the environment table is not valid, an error
	 * is returned.
	 */
	TEST_FEATURE ("with invalid environment");
	class = job_class_new (NULL, "test");
	class->instance = "$FOO";

	job = job_new (class, "wibble");

	message = nih_new (NULL, NihDBusMessage);
	message->connection = NULL;
	message->message = NULL;

	env = nih_str_array_new (message);
	assert (nih_str_array_add (&env, message, NULL, "FOO BAR=wibble"));

	ret = job_class_get_instance (class, message, env, &path);

	TEST_LT (ret, 0);

	error = nih_error_get ();
	TEST_EQ (error->number, NIH_DBUS_ERROR);
	TEST_ALLOC_SIZE (error, sizeof (NihDBusError));

	dbus_error = (NihDBusError *)error;
	TEST_EQ_STR (dbus_error->name, DBUS_ERROR_INVALID_ARGS);

	nih_free (dbus_error);

	nih_free (message);
	nih_free (class);
}

void
test_get_instance_by_name (void)
{
	NihDBusMessage *message = NULL;
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
			message->connection = NULL;
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
			message->connection = NULL;
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
			message->connection = NULL;
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
			     DBUS_INTERFACE_UPSTART ".Error.UnknownInstance");

		nih_free (error);

		nih_free (message);
	}


	nih_free (class);
}

void
test_get_all_instances (void)
{
	NihDBusMessage  *message = NULL;
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
			message->connection = NULL;
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
			message->connection = NULL;
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


void
test_start (void)
{
	DBusConnection  *conn, *client_conn;
	pid_t            dbus_pid;
	DBusMessage     *method, *reply;
	NihDBusMessage  *message;
	char           **env;
	char            *path;
	dbus_uint32_t    serial;
	JobClass        *class;
	Job             *job;
	Blocked *        blocked;
	int              ret;
	NihError        *error;
	NihDBusError    *dbus_error;


	TEST_FUNCTION ("job_class_start");
	nih_error_init ();
	nih_main_loop_init ();
	event_init ();

	TEST_DBUS (dbus_pid);
	TEST_DBUS_OPEN (conn);
	TEST_DBUS_OPEN (client_conn);


	/* Check that we can start a new instance of a job, and that it's
	 * goal should be start.  If we then hurry it though to running,
	 * the reply will be sent to the sender.
	 */
	TEST_FEATURE ("with new job");
	class = job_class_new (NULL, "test");

	method = dbus_message_new_method_call (
		dbus_bus_get_unique_name (conn),
		class->path,
		DBUS_INTERFACE_UPSTART_JOB,
		"Start");

	dbus_connection_send (client_conn, method, &serial);
	dbus_connection_flush (client_conn);
	dbus_message_unref (method);

	TEST_DBUS_MESSAGE (conn, method);
	assert (dbus_message_get_serial (method) == serial);

	message = nih_new (NULL, NihDBusMessage);
	message->connection = conn;
	message->message = method;

	TEST_FREE_TAG (message);

	env = nih_str_array_new (message);

	ret = job_class_start (class, message, env, TRUE);

	TEST_EQ (ret, 0);

	nih_discard (message);
	TEST_NOT_FREE (message);

	TEST_HASH_NOT_EMPTY (class->instances);

	job = (Job *)nih_hash_lookup (class->instances, "");

	TEST_NE_P (job, NULL);
	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_ALLOC_PARENT (job, class);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_STARTING);

	TEST_LIST_NOT_EMPTY (&job->blocking);

	blocked = (Blocked *)job->blocking.next;
	TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
	TEST_ALLOC_PARENT (blocked, job);
	TEST_EQ (blocked->type, BLOCKED_JOB_START_METHOD);
	TEST_EQ_P (blocked->message, message);

	TEST_ALLOC_PARENT (blocked->message, blocked);

	TEST_FREE_TAG (blocked);

	nih_free (job->blocker);
	job->blocker = NULL;

	job_change_state (job, job_next_state (job));

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);

	TEST_LIST_EMPTY (&job->blocking);
	TEST_FREE (blocked);

	TEST_FREE (message);
	dbus_message_unref (method);

	dbus_connection_flush (conn);

	TEST_DBUS_MESSAGE (client_conn, reply);

	TEST_EQ (dbus_message_get_type (reply),
		 DBUS_MESSAGE_TYPE_METHOD_RETURN);
	TEST_EQ (dbus_message_get_reply_serial (reply), serial);

	TEST_TRUE (dbus_message_get_args (reply, NULL,
					  DBUS_TYPE_OBJECT_PATH, &path,
					  DBUS_TYPE_INVALID));

	TEST_EQ_STR (path, job->path);

	dbus_message_unref (reply);

	nih_free (class);


	/* Check that we can start a new instance of a job without waiting
	 * for it to complete, the reply should be sent to the sender
	 * immediately and the job not blocked.
	 */
	TEST_FEATURE ("with no wait");
	class = job_class_new (NULL, "test");

	method = dbus_message_new_method_call (
		dbus_bus_get_unique_name (conn),
		class->path,
		DBUS_INTERFACE_UPSTART_JOB,
		"Start");

	dbus_connection_send (client_conn, method, &serial);
	dbus_connection_flush (client_conn);
	dbus_message_unref (method);

	TEST_DBUS_MESSAGE (conn, method);
	assert (dbus_message_get_serial (method) == serial);

	message = nih_new (NULL, NihDBusMessage);
	message->connection = conn;
	message->message = method;

	TEST_FREE_TAG (message);

	env = nih_str_array_new (message);

	ret = job_class_start (class, message, env, FALSE);

	TEST_EQ (ret, 0);

	nih_discard (message);
	TEST_FREE (message);
	dbus_message_unref (method);

	TEST_HASH_NOT_EMPTY (class->instances);

	job = (Job *)nih_hash_lookup (class->instances, "");

	TEST_NE_P (job, NULL);
	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_ALLOC_PARENT (job, class);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_STARTING);

	TEST_LIST_EMPTY (&job->blocking);


	dbus_connection_flush (conn);

	TEST_DBUS_MESSAGE (client_conn, reply);

	TEST_EQ (dbus_message_get_type (reply),
		 DBUS_MESSAGE_TYPE_METHOD_RETURN);
	TEST_EQ (dbus_message_get_reply_serial (reply), serial);

	TEST_TRUE (dbus_message_get_args (reply, NULL,
					  DBUS_TYPE_OBJECT_PATH, &path,
					  DBUS_TYPE_INVALID));

	TEST_EQ_STR (path, job->path);

	dbus_message_unref (reply);


	nih_free (job->blocker);
	job->blocker = NULL;

	job_change_state (job, job_next_state (job));

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);

	TEST_LIST_EMPTY (&job->blocking);

	nih_free (class);


	/* Check that start can be used to restart an existing instance of
	 * a job and that the goal gets reset to start.  If we then hurry
	 * it through to running, the reply will be sent to the sender.
	 */
	TEST_FEATURE ("with stopping job");
	class = job_class_new (NULL, "test");
	job = job_new (class, "");

	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;

	method = dbus_message_new_method_call (
		dbus_bus_get_unique_name (conn),
		class->path,
		DBUS_INTERFACE_UPSTART_JOB,
		"Start");

	dbus_connection_send (client_conn, method, &serial);
	dbus_connection_flush (client_conn);
	dbus_message_unref (method);

	TEST_DBUS_MESSAGE (conn, method);
	assert (dbus_message_get_serial (method) == serial);

	message = nih_new (NULL, NihDBusMessage);
	message->connection = conn;
	message->message = method;

	TEST_FREE_TAG (message);

	env = nih_str_array_new (message);

	ret = job_class_start (class, message, env, TRUE);

	TEST_EQ (ret, 0);

	nih_discard (message);
	TEST_NOT_FREE (message);

	TEST_HASH_NOT_EMPTY (class->instances);

	job = (Job *)nih_hash_lookup (class->instances, "");

	TEST_NE_P (job, NULL);
	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_ALLOC_PARENT (job, class);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_STOPPING);

	TEST_LIST_NOT_EMPTY (&job->blocking);

	blocked = (Blocked *)job->blocking.next;
	TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
	TEST_ALLOC_PARENT (blocked, job);
	TEST_EQ (blocked->type, BLOCKED_JOB_START_METHOD);
	TEST_EQ_P (blocked->message, message);

	TEST_ALLOC_PARENT (blocked->message, blocked);

	TEST_FREE_TAG (blocked);

	job_change_state (job, job_next_state (job));

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_STARTING);

	TEST_NOT_FREE (blocked);

	nih_free (job->blocker);
	job->blocker = NULL;

	job_change_state (job, job_next_state (job));

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);

	TEST_LIST_EMPTY (&job->blocking);
	TEST_FREE (blocked);

	TEST_FREE (message);
	dbus_message_unref (method);

	dbus_connection_flush (conn);

	TEST_DBUS_MESSAGE (client_conn, reply);

	TEST_EQ (dbus_message_get_type (reply),
		 DBUS_MESSAGE_TYPE_METHOD_RETURN);
	TEST_EQ (dbus_message_get_reply_serial (reply), serial);

	TEST_TRUE (dbus_message_get_args (reply, NULL,
					  DBUS_TYPE_OBJECT_PATH, &path,
					  DBUS_TYPE_INVALID));

	TEST_EQ_STR (path, job->path);

	dbus_message_unref (reply);

	nih_free (class);


	/* Check that if we attempt to start a job that's already started,
	 * a D-Bus error is raised immediately.
	 */
	TEST_FEATURE ("with starting job");
	class = job_class_new (NULL, "test");
	job = job_new (class, "");

	job->goal = JOB_START;
	job->state = JOB_STARTING;

	method = dbus_message_new_method_call (
		dbus_bus_get_unique_name (conn),
		class->path,
		DBUS_INTERFACE_UPSTART_JOB,
		"Start");

	dbus_connection_send (client_conn, method, &serial);
	dbus_connection_flush (client_conn);
	dbus_message_unref (method);

	TEST_DBUS_MESSAGE (conn, method);
	assert (dbus_message_get_serial (method) == serial);

	message = nih_new (NULL, NihDBusMessage);
	message->connection = conn;
	message->message = method;

	TEST_FREE_TAG (message);

	env = nih_str_array_new (message);

	ret = job_class_start (class, message, env, TRUE);

	TEST_LT (ret, 0);

	TEST_NOT_FREE (message);
	nih_discard (message);
	TEST_FREE (message);
	dbus_message_unref (method);

	error = nih_error_get ();
	TEST_EQ (error->number, NIH_DBUS_ERROR);
	TEST_ALLOC_SIZE (error, sizeof (NihDBusError));

	dbus_error = (NihDBusError *)error;
	TEST_EQ_STR (dbus_error->name, DBUS_INTERFACE_UPSTART ".Error.AlreadyStarted");

	nih_free (dbus_error);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_STARTING);

	nih_free (class);


	/* Check that the environment parameter is used to locate and
	 * name instances, and is then placed in the job as the environment
	 * when it's starting.
	 */
	TEST_FEATURE ("with environment");
	class = job_class_new (NULL, "test");
	class->instance = "$FOO";

	method = dbus_message_new_method_call (
		dbus_bus_get_unique_name (conn),
		class->path,
		DBUS_INTERFACE_UPSTART_JOB,
		"Start");

	dbus_connection_send (client_conn, method, &serial);
	dbus_connection_flush (client_conn);
	dbus_message_unref (method);

	TEST_DBUS_MESSAGE (conn, method);
	assert (dbus_message_get_serial (method) == serial);

	message = nih_new (NULL, NihDBusMessage);
	message->connection = conn;
	message->message = method;

	TEST_FREE_TAG (message);

	env = nih_str_array_new (message);
	assert (nih_str_array_add (&env, message, NULL, "FOO=wibble"));
	assert (nih_str_array_add (&env, message, NULL, "BAR=wobble"));

	ret = job_class_start (class, message, env, TRUE);

	TEST_EQ (ret, 0);

	nih_discard (message);
	TEST_NOT_FREE (message);

	TEST_HASH_NOT_EMPTY (class->instances);

	job = (Job *)nih_hash_lookup (class->instances, "wibble");

	TEST_NE_P (job, NULL);
	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_ALLOC_PARENT (job, class);

	TEST_EQ_STR (job->name, "wibble");

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_STARTING);

	TEST_EQ_STRN (job->env[0], "PATH=");
	TEST_EQ_STRN (job->env[1], "TERM=");
	TEST_EQ_STR (job->env[2], "FOO=wibble");
	TEST_EQ_STR (job->env[3], "BAR=wobble");
	TEST_EQ_P (job->env[4], NULL);

	TEST_LIST_NOT_EMPTY (&job->blocking);

	blocked = (Blocked *)job->blocking.next;
	TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
	TEST_ALLOC_PARENT (blocked, job);
	TEST_EQ (blocked->type, BLOCKED_JOB_START_METHOD);
	TEST_EQ_P (blocked->message, message);

	TEST_ALLOC_PARENT (blocked->message, blocked);

	TEST_FREE_TAG (blocked);

	nih_free (job->blocker);
	job->blocker = NULL;

	job_change_state (job, job_next_state (job));

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);

	TEST_LIST_EMPTY (&job->blocking);
	TEST_FREE (blocked);

	TEST_FREE (message);
	dbus_message_unref (method);

	dbus_connection_flush (conn);

	TEST_DBUS_MESSAGE (client_conn, reply);

	TEST_EQ (dbus_message_get_type (reply),
		 DBUS_MESSAGE_TYPE_METHOD_RETURN);
	TEST_EQ (dbus_message_get_reply_serial (reply), serial);

	TEST_TRUE (dbus_message_get_args (reply, NULL,
					  DBUS_TYPE_OBJECT_PATH, &path,
					  DBUS_TYPE_INVALID));

	TEST_EQ_STR (path, job->path);

	dbus_message_unref (reply);

	nih_free (class);


	/* Check that if the environment table is not valid, an error
	 * is returned.
	 */
	TEST_FEATURE ("with invalid environment");
	class = job_class_new (NULL, "test");

	method = dbus_message_new_method_call (
		dbus_bus_get_unique_name (conn),
		class->path,
		DBUS_INTERFACE_UPSTART_JOB,
		"Start");

	dbus_connection_send (client_conn, method, &serial);
	dbus_connection_flush (client_conn);
	dbus_message_unref (method);

	TEST_DBUS_MESSAGE (conn, method);
	assert (dbus_message_get_serial (method) == serial);

	message = nih_new (NULL, NihDBusMessage);
	message->connection = conn;
	message->message = method;

	TEST_FREE_TAG (message);

	env = nih_str_array_new (message);
	assert (nih_str_array_add (&env, message, NULL, "FOO BAR=wibble"));

	ret = job_class_start (class, message, env, TRUE);

	TEST_LT (ret, 0);

	TEST_NOT_FREE (message);
	nih_discard (message);
	TEST_FREE (message);
	dbus_message_unref (method);

	error = nih_error_get ();
	TEST_EQ (error->number, NIH_DBUS_ERROR);
	TEST_ALLOC_SIZE (error, sizeof (NihDBusError));

	dbus_error = (NihDBusError *)error;
	TEST_EQ_STR (dbus_error->name, DBUS_ERROR_INVALID_ARGS);

	nih_free (dbus_error);

	TEST_HASH_EMPTY (class->instances);

	nih_free (class);


	TEST_DBUS_CLOSE (conn);
	TEST_DBUS_CLOSE (client_conn);
	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();

	event_poll ();
}

void
test_stop (void)
{
	DBusConnection  *conn, *client_conn;
	pid_t            dbus_pid;
	DBusMessage     *method, *reply;
	NihDBusMessage  *message;
	char           **env;
	dbus_uint32_t    serial;
	JobClass        *class;
	Job             *job;
	Blocked *        blocked;
	int              ret;
	NihError        *error;
	NihDBusError    *dbus_error;


	TEST_FUNCTION ("job_class_stop");
	nih_error_init ();
	nih_main_loop_init ();
	event_init ();

	TEST_DBUS (dbus_pid);
	TEST_DBUS_OPEN (conn);
	TEST_DBUS_OPEN (client_conn);


	/* Check that stop can be used on an existing instance to set the
	 * goal to stop.  If we then hurry it through to waiting, the reply
	 * will be sent to the sender.
	 */
	TEST_FEATURE ("with running job");
	class = job_class_new (NULL, "test");
	job = job_new (class, "");

	job->goal = JOB_START;
	job->state = JOB_RUNNING;

	method = dbus_message_new_method_call (
		dbus_bus_get_unique_name (conn),
		class->path,
		DBUS_INTERFACE_UPSTART_JOB,
		"Stop");

	dbus_connection_send (client_conn, method, &serial);
	dbus_connection_flush (client_conn);
	dbus_message_unref (method);

	TEST_DBUS_MESSAGE (conn, method);
	assert (dbus_message_get_serial (method) == serial);

	message = nih_new (NULL, NihDBusMessage);
	message->connection = conn;
	message->message = method;

	TEST_FREE_TAG (message);

	env = nih_str_array_new (message);

	TEST_FREE_TAG (job);

	ret = job_class_stop (class, message, env, TRUE);

	TEST_EQ (ret, 0);

	nih_discard (message);
	TEST_NOT_FREE (message);

	TEST_NOT_FREE (job);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_STOPPING);

	TEST_LIST_NOT_EMPTY (&job->blocking);

	blocked = (Blocked *)job->blocking.next;
	TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
	TEST_ALLOC_PARENT (blocked, job);
	TEST_EQ (blocked->type, BLOCKED_JOB_STOP_METHOD);
	TEST_EQ_P (blocked->message, message);

	TEST_ALLOC_PARENT (blocked->message, blocked);

	TEST_FREE_TAG (blocked);

	nih_free (job->blocker);
	job->blocker = NULL;

	job_change_state (job, job_next_state (job));

	TEST_FREE (job);

	TEST_FREE (blocked);

	TEST_FREE (message);
	dbus_message_unref (method);

	dbus_connection_flush (conn);

	TEST_DBUS_MESSAGE (client_conn, reply);

	TEST_EQ (dbus_message_get_type (reply),
		 DBUS_MESSAGE_TYPE_METHOD_RETURN);
	TEST_EQ (dbus_message_get_reply_serial (reply), serial);

	dbus_message_unref (reply);

	nih_free (class);


	/* Check that we can stop a job without waiting for the command
	 * to finish, the reply should be sent to the sender immediately
	 * and no blocking entry created.
	 */
	TEST_FEATURE ("with no wait");
	class = job_class_new (NULL, "test");
	job = job_new (class, "");

	job->goal = JOB_START;
	job->state = JOB_RUNNING;

	method = dbus_message_new_method_call (
		dbus_bus_get_unique_name (conn),
		class->path,
		DBUS_INTERFACE_UPSTART_JOB,
		"Stop");

	dbus_connection_send (client_conn, method, &serial);
	dbus_connection_flush (client_conn);
	dbus_message_unref (method);

	TEST_DBUS_MESSAGE (conn, method);
	assert (dbus_message_get_serial (method) == serial);

	message = nih_new (NULL, NihDBusMessage);
	message->connection = conn;
	message->message = method;

	TEST_FREE_TAG (message);

	env = nih_str_array_new (message);

	TEST_FREE_TAG (job);

	ret = job_class_stop (class, message, env, FALSE);

	TEST_EQ (ret, 0);

	nih_discard (message);
	TEST_FREE (message);
	dbus_message_unref (method);

	TEST_NOT_FREE (job);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_STOPPING);

	TEST_LIST_EMPTY (&job->blocking);


	dbus_connection_flush (conn);

	TEST_DBUS_MESSAGE (client_conn, reply);

	TEST_EQ (dbus_message_get_type (reply),
		 DBUS_MESSAGE_TYPE_METHOD_RETURN);
	TEST_EQ (dbus_message_get_reply_serial (reply), serial);

	dbus_message_unref (reply);


	nih_free (job->blocker);
	job->blocker = NULL;

	job_change_state (job, job_next_state (job));

	TEST_FREE (job);

	nih_free (class);


	/* Check that if we attempt to stop a job that's already stopped,
	 * a D-Bus error is raised immediately.
	 */
	TEST_FEATURE ("with stopping job");
	class = job_class_new (NULL, "test");
	job = job_new (class, "");

	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;

	method = dbus_message_new_method_call (
		dbus_bus_get_unique_name (conn),
		class->path,
		DBUS_INTERFACE_UPSTART_JOB,
		"Stop");

	dbus_connection_send (client_conn, method, &serial);
	dbus_connection_flush (client_conn);
	dbus_message_unref (method);

	TEST_DBUS_MESSAGE (conn, method);
	assert (dbus_message_get_serial (method) == serial);

	message = nih_new (NULL, NihDBusMessage);
	message->connection = conn;
	message->message = method;

	TEST_FREE_TAG (message);

	env = nih_str_array_new (message);

	ret = job_class_stop (class, message, env, TRUE);

	TEST_LT (ret, 0);

	TEST_NOT_FREE (message);
	nih_discard (message);
	TEST_FREE (message);
	dbus_message_unref (method);

	error = nih_error_get ();
	TEST_EQ (error->number, NIH_DBUS_ERROR);
	TEST_ALLOC_SIZE (error, sizeof (NihDBusError));

	dbus_error = (NihDBusError *)error;
	TEST_EQ_STR (dbus_error->name, DBUS_INTERFACE_UPSTART ".Error.AlreadyStopped");

	nih_free (dbus_error);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_STOPPING);

	nih_free (class);


	/* Check that if there's no such instance, a D-Bus error is raised
	 * immediately.
	 */
	TEST_FEATURE ("with unknown job");
	class = job_class_new (NULL, "test");

	method = dbus_message_new_method_call (
		dbus_bus_get_unique_name (conn),
		class->path,
		DBUS_INTERFACE_UPSTART_JOB,
		"Stop");

	dbus_connection_send (client_conn, method, &serial);
	dbus_connection_flush (client_conn);
	dbus_message_unref (method);

	TEST_DBUS_MESSAGE (conn, method);
	assert (dbus_message_get_serial (method) == serial);

	message = nih_new (NULL, NihDBusMessage);
	message->connection = conn;
	message->message = method;

	TEST_FREE_TAG (message);

	env = nih_str_array_new (message);

	ret = job_class_stop (class, message, env, TRUE);

	TEST_LT (ret, 0);

	TEST_NOT_FREE (message);
	nih_discard (message);
	TEST_FREE (message);
	dbus_message_unref (method);

	error = nih_error_get ();
	TEST_EQ (error->number, NIH_DBUS_ERROR);
	TEST_ALLOC_SIZE (error, sizeof (NihDBusError));

	dbus_error = (NihDBusError *)error;
	TEST_EQ_STR (dbus_error->name, DBUS_INTERFACE_UPSTART ".Error.UnknownInstance");

	nih_free (dbus_error);

	nih_free (class);


	/* Check that the environment parameter is used to locate and
	 * name instances, and is then placed in the job as the environment
	 * for the pre-stop script.
	 */
	TEST_FEATURE ("with environment");
	class = job_class_new (NULL, "test");
	class->instance = "$FOO";

	job = job_new (class, "wibble");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;

	method = dbus_message_new_method_call (
		dbus_bus_get_unique_name (conn),
		class->path,
		DBUS_INTERFACE_UPSTART_JOB,
		"Stop");

	dbus_connection_send (client_conn, method, &serial);
	dbus_connection_flush (client_conn);
	dbus_message_unref (method);

	TEST_DBUS_MESSAGE (conn, method);
	assert (dbus_message_get_serial (method) == serial);

	message = nih_new (NULL, NihDBusMessage);
	message->connection = conn;
	message->message = method;

	TEST_FREE_TAG (message);

	env = nih_str_array_new (message);
	assert (nih_str_array_add (&env, message, NULL, "FOO=wibble"));
	assert (nih_str_array_add (&env, message, NULL, "BAR=wobble"));

	TEST_FREE_TAG (job);

	ret = job_class_stop (class, message, env, TRUE);

	TEST_EQ (ret, 0);

	nih_discard (message);
	TEST_NOT_FREE (message);

	TEST_NOT_FREE (job);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_STOPPING);

	TEST_EQ_STR (job->stop_env[0], "FOO=wibble");
	TEST_EQ_STR (job->stop_env[1], "BAR=wobble");
	TEST_EQ_P (job->stop_env[2], NULL);

	TEST_LIST_NOT_EMPTY (&job->blocking);

	blocked = (Blocked *)job->blocking.next;
	TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
	TEST_ALLOC_PARENT (blocked, job);
	TEST_EQ (blocked->type, BLOCKED_JOB_STOP_METHOD);
	TEST_EQ_P (blocked->message, message);

	TEST_ALLOC_PARENT (blocked->message, blocked);

	TEST_FREE_TAG (blocked);

	nih_free (job->blocker);
	job->blocker = NULL;

	job_change_state (job, job_next_state (job));

	TEST_FREE (job);

	TEST_FREE (blocked);

	TEST_FREE (message);
	dbus_message_unref (method);

	dbus_connection_flush (conn);

	TEST_DBUS_MESSAGE (client_conn, reply);

	TEST_EQ (dbus_message_get_type (reply),
		 DBUS_MESSAGE_TYPE_METHOD_RETURN);
	TEST_EQ (dbus_message_get_reply_serial (reply), serial);

	dbus_message_unref (reply);

	nih_free (class);


	/* Check that if the environment table is not valid, an error
	 * is returned.
	 */
	TEST_FEATURE ("with invalid environment");
	class = job_class_new (NULL, "test");
	job = job_new (class, "");

	job->goal = JOB_START;
	job->state = JOB_RUNNING;

	method = dbus_message_new_method_call (
		dbus_bus_get_unique_name (conn),
		class->path,
		DBUS_INTERFACE_UPSTART_JOB,
		"Stop");

	dbus_connection_send (client_conn, method, &serial);
	dbus_connection_flush (client_conn);
	dbus_message_unref (method);

	TEST_DBUS_MESSAGE (conn, method);
	assert (dbus_message_get_serial (method) == serial);

	message = nih_new (NULL, NihDBusMessage);
	message->connection = conn;
	message->message = method;

	TEST_FREE_TAG (message);

	env = nih_str_array_new (message);
	assert (nih_str_array_add (&env, message, NULL, "FOO BAR=wibble"));

	ret = job_class_stop (class, message, env, TRUE);

	TEST_LT (ret, 0);

	TEST_NOT_FREE (message);
	nih_discard (message);
	TEST_FREE (message);
	dbus_message_unref (method);

	error = nih_error_get ();
	TEST_EQ (error->number, NIH_DBUS_ERROR);
	TEST_ALLOC_SIZE (error, sizeof (NihDBusError));

	dbus_error = (NihDBusError *)error;
	TEST_EQ_STR (dbus_error->name, DBUS_ERROR_INVALID_ARGS);

	nih_free (dbus_error);

	nih_free (class);


	TEST_DBUS_CLOSE (conn);
	TEST_DBUS_CLOSE (client_conn);
	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();

	event_poll ();
}

void
test_restart (void)
{
	DBusConnection  *conn, *client_conn;
	pid_t            dbus_pid;
	DBusMessage     *method, *reply;
	NihDBusMessage  *message;
	char           **env;
	char            *path;
	dbus_uint32_t    serial;
	JobClass        *class;
	Job             *job;
	Blocked *        blocked;
	int              ret;
	NihError        *error;
	NihDBusError    *dbus_error;


	TEST_FUNCTION ("job_class_restart");
	nih_error_init ();
	nih_main_loop_init ();
	event_init ();

	TEST_DBUS (dbus_pid);
	TEST_DBUS_OPEN (conn);
	TEST_DBUS_OPEN (client_conn);


	/* Check that restart can be used on an existing instance to set the
	 * goal to start while stopping the job first.  If we then hurry it
	 * through to waiting, the reply will be sent to the sender.
	 */
	TEST_FEATURE ("with running job");
	class = job_class_new (NULL, "test");
	job = job_new (class, "");

	job->goal = JOB_START;
	job->state = JOB_RUNNING;

	method = dbus_message_new_method_call (
		dbus_bus_get_unique_name (conn),
		class->path,
		DBUS_INTERFACE_UPSTART_JOB,
		"Restart");

	dbus_connection_send (client_conn, method, &serial);
	dbus_connection_flush (client_conn);
	dbus_message_unref (method);

	TEST_DBUS_MESSAGE (conn, method);
	assert (dbus_message_get_serial (method) == serial);

	message = nih_new (NULL, NihDBusMessage);
	message->connection = conn;
	message->message = method;

	TEST_FREE_TAG (message);

	env = nih_str_array_new (message);

	ret = job_class_restart (class, message, env, TRUE);

	TEST_EQ (ret, 0);

	nih_discard (message);
	TEST_NOT_FREE (message);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_STOPPING);

	TEST_LIST_NOT_EMPTY (&job->blocking);

	blocked = (Blocked *)job->blocking.next;
	TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
	TEST_ALLOC_PARENT (blocked, job);
	TEST_EQ (blocked->type, BLOCKED_JOB_RESTART_METHOD);
	TEST_EQ_P (blocked->message, message);

	TEST_ALLOC_PARENT (blocked->message, blocked);

	TEST_FREE_TAG (blocked);

	nih_free (job->blocker);
	job->blocker = NULL;

	job_change_state (job, job_next_state (job));

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_STARTING);

	TEST_NOT_FREE (blocked);

	nih_free (job->blocker);
	job->blocker = NULL;

	job_change_state (job, job_next_state (job));

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);

	TEST_LIST_EMPTY (&job->blocking);
	TEST_FREE (blocked);

	TEST_FREE (message);
	dbus_message_unref (method);

	dbus_connection_flush (conn);

	TEST_DBUS_MESSAGE (client_conn, reply);

	TEST_EQ (dbus_message_get_type (reply),
		 DBUS_MESSAGE_TYPE_METHOD_RETURN);
	TEST_EQ (dbus_message_get_reply_serial (reply), serial);

	TEST_TRUE (dbus_message_get_args (reply, NULL,
					  DBUS_TYPE_OBJECT_PATH, &path,
					  DBUS_TYPE_INVALID));

	TEST_EQ_STR (path, job->path);

	dbus_message_unref (reply);

	nih_free (class);


	/* Check that we can restart the job without waiting for the command
	 * to finish, the reply should be sent immediately and no blocking
	 * entry created.
	 */
	TEST_FEATURE ("with no wait");
	class = job_class_new (NULL, "test");
	job = job_new (class, "");

	job->goal = JOB_START;
	job->state = JOB_RUNNING;

	method = dbus_message_new_method_call (
		dbus_bus_get_unique_name (conn),
		class->path,
		DBUS_INTERFACE_UPSTART_JOB,
		"Restart");

	dbus_connection_send (client_conn, method, &serial);
	dbus_connection_flush (client_conn);
	dbus_message_unref (method);

	TEST_DBUS_MESSAGE (conn, method);
	assert (dbus_message_get_serial (method) == serial);

	message = nih_new (NULL, NihDBusMessage);
	message->connection = conn;
	message->message = method;

	TEST_FREE_TAG (message);

	env = nih_str_array_new (message);

	ret = job_class_restart (class, message, env, FALSE);

	TEST_EQ (ret, 0);

	nih_discard (message);
	TEST_FREE (message);
	dbus_message_unref (method);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_STOPPING);

	TEST_LIST_EMPTY (&job->blocking);


	dbus_connection_flush (conn);

	TEST_DBUS_MESSAGE (client_conn, reply);

	TEST_EQ (dbus_message_get_type (reply),
		 DBUS_MESSAGE_TYPE_METHOD_RETURN);
	TEST_EQ (dbus_message_get_reply_serial (reply), serial);

	TEST_TRUE (dbus_message_get_args (reply, NULL,
					  DBUS_TYPE_OBJECT_PATH, &path,
					  DBUS_TYPE_INVALID));

	TEST_EQ_STR (path, job->path);

	dbus_message_unref (reply);


	nih_free (job->blocker);
	job->blocker = NULL;

	job_change_state (job, job_next_state (job));

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_STARTING);

	nih_free (job->blocker);
	job->blocker = NULL;

	job_change_state (job, job_next_state (job));

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);

	nih_free (class);


	/* Check that if we attempt to restart a job that's already stopped,
	 * a D-Bus error is raised immediately.
	 */
	TEST_FEATURE ("with stopping job");
	class = job_class_new (NULL, "test");
	job = job_new (class, "");

	job->goal = JOB_STOP;
	job->state = JOB_STOPPING;

	method = dbus_message_new_method_call (
		dbus_bus_get_unique_name (conn),
		class->path,
		DBUS_INTERFACE_UPSTART_JOB,
		"Restart");

	dbus_connection_send (client_conn, method, &serial);
	dbus_connection_flush (client_conn);
	dbus_message_unref (method);

	TEST_DBUS_MESSAGE (conn, method);
	assert (dbus_message_get_serial (method) == serial);

	message = nih_new (NULL, NihDBusMessage);
	message->connection = conn;
	message->message = method;

	TEST_FREE_TAG (message);

	env = nih_str_array_new (message);

	ret = job_class_restart (class, message, env, TRUE);

	TEST_LT (ret, 0);

	TEST_NOT_FREE (message);
	nih_discard (message);
	TEST_FREE (message);
	dbus_message_unref (method);

	error = nih_error_get ();
	TEST_EQ (error->number, NIH_DBUS_ERROR);
	TEST_ALLOC_SIZE (error, sizeof (NihDBusError));

	dbus_error = (NihDBusError *)error;
	TEST_EQ_STR (dbus_error->name, DBUS_INTERFACE_UPSTART ".Error.AlreadyStopped");

	nih_free (dbus_error);

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_STOPPING);

	nih_free (class);


	/* Check that if there's no such instance, a D-Bus error is raised
	 * immediately.
	 */
	TEST_FEATURE ("with unknown job");
	class = job_class_new (NULL, "test");

	method = dbus_message_new_method_call (
		dbus_bus_get_unique_name (conn),
		class->path,
		DBUS_INTERFACE_UPSTART_JOB,
		"Restart");

	dbus_connection_send (client_conn, method, &serial);
	dbus_connection_flush (client_conn);
	dbus_message_unref (method);

	TEST_DBUS_MESSAGE (conn, method);
	assert (dbus_message_get_serial (method) == serial);

	message = nih_new (NULL, NihDBusMessage);
	message->connection = conn;
	message->message = method;

	TEST_FREE_TAG (message);

	env = nih_str_array_new (message);

	ret = job_class_restart (class, message, env, TRUE);

	TEST_LT (ret, 0);

	TEST_NOT_FREE (message);
	nih_discard (message);
	TEST_FREE (message);
	dbus_message_unref (method);

	error = nih_error_get ();
	TEST_EQ (error->number, NIH_DBUS_ERROR);
	TEST_ALLOC_SIZE (error, sizeof (NihDBusError));

	dbus_error = (NihDBusError *)error;
	TEST_EQ_STR (dbus_error->name, DBUS_INTERFACE_UPSTART ".Error.UnknownInstance");

	nih_free (dbus_error);

	nih_free (class);


	/* Check that the environment parameter is used to locate and
	 * name instances, and is then placed in the job as the environment
	 * when it's starting again.
	 */
	TEST_FEATURE ("with environment");
	class = job_class_new (NULL, "test");
	class->instance = "$FOO";

	job = job_new (class, "wibble");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;

	method = dbus_message_new_method_call (
		dbus_bus_get_unique_name (conn),
		class->path,
		DBUS_INTERFACE_UPSTART_JOB,
		"Restart");

	dbus_connection_send (client_conn, method, &serial);
	dbus_connection_flush (client_conn);
	dbus_message_unref (method);

	TEST_DBUS_MESSAGE (conn, method);
	assert (dbus_message_get_serial (method) == serial);

	message = nih_new (NULL, NihDBusMessage);
	message->connection = conn;
	message->message = method;

	TEST_FREE_TAG (message);

	env = nih_str_array_new (message);
	assert (nih_str_array_add (&env, message, NULL, "FOO=wibble"));
	assert (nih_str_array_add (&env, message, NULL, "BAR=wobble"));

	ret = job_class_restart (class, message, env, TRUE);

	TEST_EQ (ret, 0);

	nih_discard (message);
	TEST_NOT_FREE (message);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_STOPPING);

	TEST_EQ_P (job->stop_env, NULL);

	TEST_EQ_STRN (job->start_env[0], "PATH=");
	TEST_EQ_STRN (job->start_env[1], "TERM=");
	TEST_EQ_STR (job->start_env[2], "FOO=wibble");
	TEST_EQ_STR (job->start_env[3], "BAR=wobble");
	TEST_EQ_P (job->start_env[4], NULL);

	TEST_LIST_NOT_EMPTY (&job->blocking);

	blocked = (Blocked *)job->blocking.next;
	TEST_ALLOC_SIZE (blocked, sizeof (Blocked));
	TEST_ALLOC_PARENT (blocked, job);
	TEST_EQ (blocked->type, BLOCKED_JOB_RESTART_METHOD);
	TEST_EQ_P (blocked->message, message);

	TEST_ALLOC_PARENT (blocked->message, blocked);

	TEST_FREE_TAG (blocked);

	nih_free (job->blocker);
	job->blocker = NULL;

	job_change_state (job, job_next_state (job));

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_STARTING);

	TEST_EQ_STRN (job->env[0], "PATH=");
	TEST_EQ_STRN (job->env[1], "TERM=");
	TEST_EQ_STR (job->env[2], "FOO=wibble");
	TEST_EQ_STR (job->env[3], "BAR=wobble");
	TEST_EQ_P (job->env[4], NULL);

	TEST_NOT_FREE (blocked);

	nih_free (job->blocker);
	job->blocker = NULL;

	job_change_state (job, job_next_state (job));

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);

	TEST_LIST_EMPTY (&job->blocking);
	TEST_FREE (blocked);

	TEST_FREE (message);
	dbus_message_unref (method);

	dbus_connection_flush (conn);

	TEST_DBUS_MESSAGE (client_conn, reply);

	TEST_EQ (dbus_message_get_type (reply),
		 DBUS_MESSAGE_TYPE_METHOD_RETURN);
	TEST_EQ (dbus_message_get_reply_serial (reply), serial);

	TEST_TRUE (dbus_message_get_args (reply, NULL,
					  DBUS_TYPE_OBJECT_PATH, &path,
					  DBUS_TYPE_INVALID));

	TEST_EQ_STR (path, job->path);

	dbus_message_unref (reply);

	nih_free (class);


	/* Check that if the environment table is not valid, an error
	 * is returned.
	 */
	TEST_FEATURE ("with invalid environment");
	class = job_class_new (NULL, "test");
	job = job_new (class, "");

	job->goal = JOB_START;
	job->state = JOB_RUNNING;

	method = dbus_message_new_method_call (
		dbus_bus_get_unique_name (conn),
		class->path,
		DBUS_INTERFACE_UPSTART_JOB,
		"Restart");

	dbus_connection_send (client_conn, method, &serial);
	dbus_connection_flush (client_conn);
	dbus_message_unref (method);

	TEST_DBUS_MESSAGE (conn, method);
	assert (dbus_message_get_serial (method) == serial);

	message = nih_new (NULL, NihDBusMessage);
	message->connection = conn;
	message->message = method;

	TEST_FREE_TAG (message);

	env = nih_str_array_new (message);
	assert (nih_str_array_add (&env, message, NULL, "FOO BAR=wibble"));

	ret = job_class_restart (class, message, env, TRUE);

	TEST_LT (ret, 0);

	TEST_NOT_FREE (message);
	nih_discard (message);
	TEST_FREE (message);
	dbus_message_unref (method);

	error = nih_error_get ();
	TEST_EQ (error->number, NIH_DBUS_ERROR);
	TEST_ALLOC_SIZE (error, sizeof (NihDBusError));

	dbus_error = (NihDBusError *)error;
	TEST_EQ_STR (dbus_error->name, DBUS_ERROR_INVALID_ARGS);

	nih_free (dbus_error);

	nih_free (class);


	TEST_DBUS_CLOSE (conn);
	TEST_DBUS_CLOSE (client_conn);
	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();

	event_poll ();
}


void
test_get_name (void)
{
	NihDBusMessage *message = NULL;
	JobClass       *class;
	NihError       *error;
	char           *name;
	int             ret;

	/* Check that the name of the job class is returned from the
	 * property, as a child of the message.
	 */
	TEST_FUNCTION ("job_class_get_name");
	nih_error_init ();
	job_class_init ();

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			class = job_class_new (NULL, "test");

			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}

		name = NULL;

		ret = job_class_get_name (class, message, &name);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);
			nih_free (class);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_ALLOC_PARENT (name, message);
		TEST_EQ_STR (name, "test");

		nih_free (message);
		nih_free (class);
	}
}

void
test_get_description (void)
{
	NihDBusMessage *message = NULL;
	JobClass       *class;
	NihError       *error;
	char           *description;
	int             ret;

	TEST_FUNCTION ("job_class_get_description");
	nih_error_init ();
	job_class_init ();

	/* Check that the description of the job class is returned from the
	 * property, as a child of the message.
	 */
	TEST_FEATURE ("with description");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			class = job_class_new (NULL, "test");
			class->description = nih_strdup (class, "a test job");

			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}

		description = NULL;

		ret = job_class_get_description (class, message, &description);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);
			nih_free (class);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_ALLOC_PARENT (description, message);
		TEST_EQ_STR (description, "a test job");

		nih_free (message);
		nih_free (class);
	}


	/* Check that when there is no description, the empty string is
	 * returned instead.
	 */
	TEST_FEATURE ("with no description");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			class = job_class_new (NULL, "test");

			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}

		description = NULL;

		ret = job_class_get_description (class, message, &description);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);
			nih_free (class);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_ALLOC_PARENT (description, message);
		TEST_EQ_STR (description, "");

		nih_free (message);
		nih_free (class);
	}
}

void
test_get_author (void)
{
	NihDBusMessage *message = NULL;
	JobClass       *class;
	NihError       *error;
	char           *author;
	int             ret;

	TEST_FUNCTION ("job_class_get_author");
	nih_error_init ();
	job_class_init ();

	/* Check that the author of the job class is returned from the
	 * property, as a child of the message.
	 */
	TEST_FEATURE ("with author");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			class = job_class_new (NULL, "test");
			class->author = nih_strdup (class, "a test job");

			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}

		author = NULL;

		ret = job_class_get_author (class, message, &author);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);
			nih_free (class);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_ALLOC_PARENT (author, message);
		TEST_EQ_STR (author, "a test job");

		nih_free (message);
		nih_free (class);
	}


	/* Check that when there is no author, the empty string is
	 * returned instead.
	 */
	TEST_FEATURE ("with no author");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			class = job_class_new (NULL, "test");

			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}

		author = NULL;

		ret = job_class_get_author (class, message, &author);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);
			nih_free (class);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_ALLOC_PARENT (author, message);
		TEST_EQ_STR (author, "");

		nih_free (message);
		nih_free (class);
	}
}

void
test_get_version (void)
{
	NihDBusMessage *message = NULL;
	JobClass       *class;
	NihError       *error;
	char           *version;
	int             ret;

	TEST_FUNCTION ("job_class_get_version");
	nih_error_init ();
	job_class_init ();

	/* Check that the version of the job class is returned from the
	 * property, as a child of the message.
	 */
	TEST_FEATURE ("with version");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			class = job_class_new (NULL, "test");
			class->version = nih_strdup (class, "a test job");

			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}

		version = NULL;

		ret = job_class_get_version (class, message, &version);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);
			nih_free (class);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_ALLOC_PARENT (version, message);
		TEST_EQ_STR (version, "a test job");

		nih_free (message);
		nih_free (class);
	}


	/* Check that when there is no version, the empty string is
	 * returned instead.
	 */
	TEST_FEATURE ("with no version");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			class = job_class_new (NULL, "test");

			message = nih_new (NULL, NihDBusMessage);
			message->connection = NULL;
			message->message = NULL;
		}

		version = NULL;

		ret = job_class_get_version (class, message, &version);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			error = nih_error_get ();
			TEST_EQ (error->number, ENOMEM);
			nih_free (error);

			nih_free (message);
			nih_free (class);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_ALLOC_PARENT (version, message);
		TEST_EQ_STR (version, "");

		nih_free (message);
		nih_free (class);
	}
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

	test_get_instance ();
	test_get_instance_by_name ();
	test_get_all_instances ();

	test_start ();
	test_stop ();
	test_restart ();

	test_get_name ();
	test_get_description ();
	test_get_author ();
	test_get_version ();

	return 0;
}
