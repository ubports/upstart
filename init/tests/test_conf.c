/* upstart
 *
 * test_conf.c - test suite for init/conf.c
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

#ifdef HAVE_SYS_INOTIFY_H
# include <sys/inotify.h>
#else
# include <nih/inotify.h>
#endif /* HAVE_SYS_INOTIFY_H */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>

#include <fcntl.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/hash.h>
#include <nih/io.h>
#include <nih/watch.h>
#include <nih/main.h>
#include <nih/logging.h>
#include <nih/error.h>

#include "conf.h"


void
test_source_new (void)
{
	ConfSource *source;

	conf_init ();

	/* Check that we can request a new ConfSource structure, it should be
	 * allocated with nih_alloc and placed into the conf_sources hash
	 * table.
	 */
	TEST_FUNCTION ("conf_source_new");
	TEST_ALLOC_FAIL {
		source = conf_source_new (NULL, "/tmp", CONF_JOB_DIR);

		if (test_alloc_failed) {
			TEST_EQ_P (source, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (source, sizeof (ConfSource));
		TEST_LIST_NOT_EMPTY (&source->entry);
		TEST_ALLOC_PARENT (source->path, source);
		TEST_EQ_STR (source->path, "/tmp");
		TEST_EQ (source->type, CONF_JOB_DIR);
		TEST_EQ_P (source->watch, NULL);
		TEST_EQ (source->flag, FALSE);
		TEST_NE_P (source->files, NULL);

		TEST_EQ_P ((void *)nih_hash_lookup (conf_sources, "/tmp"),
			   source);

		nih_list_free (&source->entry);
	}
}

void
test_file_get (void)
{
	ConfSource *source;
	ConfFile   *file, *ptr;

	TEST_FUNCTION ("conf_file_get");
	source = conf_source_new (NULL, "/tmp", CONF_JOB_DIR);

	/* Check that we can request a new ConfFile structure, it should be
	 * allocated with nih_alloc and placed into the files hash table of
	 * the source, with the flag copied.
	 */
	TEST_FEATURE ("with new path");
	TEST_ALLOC_FAIL {
		file = conf_file_get (source, "/tmp/foo");

		if (test_alloc_failed) {
			TEST_EQ_P (file, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (file, sizeof (ConfFile));
		TEST_ALLOC_PARENT (file, source);
		TEST_LIST_NOT_EMPTY (&file->entry);
		TEST_ALLOC_PARENT (file->path, file);
		TEST_EQ_STR (file->path, "/tmp/foo");
		TEST_EQ (file->flag, source->flag);
		TEST_LIST_EMPTY (&file->items);

		TEST_EQ_P ((void *)nih_hash_lookup (source->files, "/tmp/foo"),
			   file);

		nih_list_free (&file->entry);
	}


	/* Check that we can retrieve an existing ConfFile entry for a
	 * source, and that the flag is updated to the new value.
	 */
	TEST_FEATURE ("with existing path");
	file = conf_file_get (source, "/tmp/foo");
	source->flag = (! source->flag);

	TEST_ALLOC_FAIL {
		ptr = conf_file_get (source, "/tmp/foo");

		if (test_alloc_failed) {
			TEST_EQ_P (ptr, NULL);
			continue;
		}

		TEST_EQ_P (ptr, file);

		TEST_ALLOC_SIZE (file, sizeof (ConfFile));
		TEST_ALLOC_PARENT (file, source);
		TEST_LIST_NOT_EMPTY (&file->entry);
		TEST_ALLOC_PARENT (file->path, file);
		TEST_EQ_STR (file->path, "/tmp/foo");
		TEST_EQ (file->flag, source->flag);
		TEST_LIST_EMPTY (&file->items);

		TEST_EQ_P ((void *)nih_hash_lookup (source->files, "/tmp/foo"),
			   file);
	}

	nih_list_free (&file->entry);
	nih_list_free (&source->entry);
}

void
test_item_new (void)
{
	ConfSource *source;
	ConfFile   *file;
	ConfItem   *item;

	TEST_FUNCTION ("conf_item_new");
	source = conf_source_new (NULL, "/tmp", CONF_JOB_DIR);
	file = conf_file_get (source, "/tmp/foo");

	/* Check that we can request a new Confitem structure, it should be
	 * allocated with nih_alloc and placed into the items list of
	 * the file, with the flag copied.
	 */
	TEST_FEATURE ("with new item");
	TEST_ALLOC_FAIL {
		item = conf_item_new (source, file, CONF_JOB);

		if (test_alloc_failed) {
			TEST_EQ_P (item, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (item, sizeof (ConfItem));
		TEST_ALLOC_PARENT (item, file);
		TEST_LIST_NOT_EMPTY (&item->entry);
		TEST_EQ (item->type, CONF_JOB);
		TEST_EQ_P (item->data, NULL);

		nih_list_free (&item->entry);
	}

	nih_list_free (&file->entry);
	nih_list_free (&source->entry);
}


void
test_source_reload (void)
{
	ConfSource *source;
	ConfFile   *file;
	ConfItem   *item;
	Job        *job, *old_job;
	FILE       *f;
	int         ret, fd[4096], i = 0, nfds;
	char        job_dirname[PATH_MAX];
	char        tmpname[PATH_MAX], filename[PATH_MAX];
	fd_set      readfds, writefds, exceptfds;

	TEST_FUNCTION ("conf_source_reload");
	program_name = "test";
	nih_log_set_priority (NIH_LOG_FATAL);

	TEST_FILENAME (job_dirname);
	mkdir (job_dirname, 0755);

	strcpy (filename, job_dirname);
	strcat (filename, "/foo");

	f = fopen (filename, "w");
	fprintf (f, "exec /sbin/daemon\n");
	fprintf (f, "respawn\n");
	fclose (f);

	strcpy (filename, job_dirname);
	strcat (filename, "/bar");

	f = fopen (filename, "w");
	fprintf (f, "script\n");
	fprintf (f, "  echo\n");
	fprintf (f, "end script\n");
	fclose (f);

	strcpy (filename, job_dirname);
	strcat (filename, "/frodo");

	mkdir (filename, 0755);

	strcpy (filename, job_dirname);
	strcat (filename, "/frodo/foo");

	f = fopen (filename, "w");
	fprintf (f, "exec /bin/tool\n");
	fclose (f);

	/* Make sure that we have inotify before performing some tests... */
	if ((fd[0] = inotify_init ()) < 0) {
		printf ("SKIP: inotify not available\n");
		goto no_inotify;
	}
	close (fd[0]);


	/* Check that we can load a job directory source for the first time.
	 * An inotify watch should be established on the directory, the
	 * descriptor set to be closed-on-exec, and all entries in the
	 * directory parsed.
	 */
	TEST_FEATURE ("with new job directory");
	source = conf_source_new (NULL, job_dirname, CONF_JOB_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_ALLOC_SIZE (source->watch, sizeof (NihWatch));
	TEST_EQ_STR (source->watch->path, job_dirname);
	TEST_EQ_P (source->watch->data, source);

	TEST_TRUE (fcntl (source->watch->fd, F_GETFD) & FD_CLOEXEC);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, job_dirname);
	strcat (filename, "/foo");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_NOT_EMPTY (&file->items);

	item = (ConfItem *)file->items.next;

	TEST_ALLOC_SIZE (item, sizeof (ConfItem));
	TEST_ALLOC_PARENT (item, file);
	TEST_NE_P (item->job, NULL);

	job = job_find_by_name ("foo");
	TEST_EQ_P (item->job, job);

	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "/sbin/daemon");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);

	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);


	strcpy (filename, job_dirname);
	strcat (filename, "/bar");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_NOT_EMPTY (&file->items);

	item = (ConfItem *)file->items.next;

	TEST_ALLOC_SIZE (item, sizeof (ConfItem));
	TEST_ALLOC_PARENT (item, file);
	TEST_NE_P (item->job, NULL);

	job = job_find_by_name ("bar");
	TEST_EQ_P (item->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);

	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);


	strcpy (filename, job_dirname);
	strcat (filename, "/frodo/foo");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_NOT_EMPTY (&file->items);

	item = (ConfItem *)file->items.next;

	TEST_ALLOC_SIZE (item, sizeof (ConfItem));
	TEST_ALLOC_PARENT (item, file);
	TEST_NE_P (item->job, NULL);

	job = job_find_by_name ("frodo/foo");
	TEST_EQ_P (item->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "/bin/tool");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);

	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);

	TEST_HASH_EMPTY (source->files);

	conf_source_free (source);


	/* Check that if we create a new file in the directory, using the
	 * direct writing technique, it will be automatically parsed and
	 * loaded.
	 */
	TEST_FEATURE ("with new file in directory (direct write)");
	source = conf_source_new (NULL, job_dirname, CONF_JOB_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);

	strcpy (filename, job_dirname);
	strcat (filename, "/frodo/bar");

	f = fopen (filename, "w");
	fprintf (f, "respawn\n");
	fprintf (f, "script\n");
	fprintf (f, "  echo\n");
	fprintf (f, "end script\n");
	fclose (f);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_NOT_EMPTY (&file->items);

	item = (ConfItem *)file->items.next;

	TEST_ALLOC_SIZE (item, sizeof (ConfItem));
	TEST_ALLOC_PARENT (item, file);
	TEST_NE_P (item->job, NULL);

	job = job_find_by_name ("frodo/bar");
	TEST_EQ_P (item->job, job);

	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	TEST_EQ_P (job->replacement, NULL);
	TEST_EQ_P (job->replacement_for, NULL);

	TEST_EQ_P (item->entry.next, &file->items);

	old_job = job;


	/* Check that a file in the directory we're watching can be modified
	 * using the direct writing technique; it should be parsed and the
	 * previous job marked for deletion.
	 */
	TEST_FEATURE ("with modified job (direct write)");
	strcpy (filename, job_dirname);
	strcat (filename, "/frodo/bar");

	f = fopen (filename, "w");
	fprintf (f, "respawn\n");
	fprintf (f, "script\n");
	fprintf (f, "  sleep 5\n");
	fprintf (f, "end script\n");
	fclose (f);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_NOT_EMPTY (&file->items);

	item = (ConfItem *)file->items.next;

	TEST_ALLOC_SIZE (item, sizeof (ConfItem));
	TEST_ALLOC_PARENT (item, file);
	TEST_NE_P (item->job, NULL);

	job = job_find_by_name ("frodo/bar");
	TEST_EQ_P (item->job, job);

	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "sleep 5\n");

	TEST_EQ_P (job->replacement, NULL);
	TEST_EQ_P (job->replacement_for, NULL);

	TEST_EQ_P (old_job->replacement, job);
	TEST_EQ (old_job->state, JOB_DELETED);

	TEST_EQ_P (item->entry.next, &file->items);

	old_job = job;


	/* Check that a file in the directory we're watching can be modified
	 * using the write and then rename technique; it should be parsed and
	 * the previous job marked for deletion.
	 */
	TEST_FEATURE ("with modified job (atomic rename)");
	strcpy (filename, job_dirname);
	strcat (filename, "/frodo/bar");

	strcpy (tmpname, job_dirname);
	strcat (tmpname, "/frodo/.bar.swp");

	f = fopen (tmpname, "w");
	fprintf (f, "respawn\n");
	fprintf (f, "script\n");
	fprintf (f, "  sleep 15\n");
	fprintf (f, "end script\n");
	fclose (f);

	rename (tmpname, filename);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_NOT_EMPTY (&file->items);

	item = (ConfItem *)file->items.next;

	TEST_ALLOC_SIZE (item, sizeof (ConfItem));
	TEST_ALLOC_PARENT (item, file);
	TEST_NE_P (item->job, NULL);

	job = job_find_by_name ("frodo/bar");
	TEST_EQ_P (item->job, job);

	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "sleep 15\n");

	TEST_EQ_P (job->replacement, NULL);
	TEST_EQ_P (job->replacement_for, NULL);

	TEST_EQ_P (old_job->replacement, job);
	TEST_EQ (old_job->state, JOB_DELETED);

	TEST_EQ_P (item->entry.next, &file->items);

	old_job = job;


	/* Check that we can delete a file from the directory, the metadata
	 * for it should be lost and the job should be queued for deletion.
	 */
	TEST_FEATURE ("with deleted job");
	strcpy (filename, job_dirname);
	strcat (filename, "/frodo/bar");

	unlink (filename);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_EQ_P (file, NULL);

	job = job_find_by_name ("frodo/bar");
	TEST_EQ_P (job, NULL);

	TEST_EQ_P (old_job->replacement, (void *)-1);
	TEST_EQ (old_job->state, JOB_DELETED);


	conf_source_free (source);

	/* Consume all available inotify instances so that the following
	 * tests run without inotify.
	 */
	for (i = 0; i < 4096; i++)
		if ((fd[i] = inotify_init ()) < 0)
			break;
no_inotify:


	/* Check that we can load a job directory source for the first time.
	 * Even though we don't have inotify, all entries in the directory
	 * should still be parsed.
	 */
	TEST_FEATURE ("with new job directory but no inotify");
	source = conf_source_new (NULL, job_dirname, CONF_JOB_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, job_dirname);
	strcat (filename, "/foo");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_NOT_EMPTY (&file->items);

	item = (ConfItem *)file->items.next;

	TEST_ALLOC_SIZE (item, sizeof (ConfItem));
	TEST_ALLOC_PARENT (item, file);
	TEST_NE_P (item->job, NULL);

	job = job_find_by_name ("foo");
	TEST_EQ_P (item->job, job);

	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "/sbin/daemon");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);

	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);


	strcpy (filename, job_dirname);
	strcat (filename, "/bar");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_NOT_EMPTY (&file->items);

	item = (ConfItem *)file->items.next;

	TEST_ALLOC_SIZE (item, sizeof (ConfItem));
	TEST_ALLOC_PARENT (item, file);
	TEST_NE_P (item->job, NULL);

	job = job_find_by_name ("bar");
	TEST_EQ_P (item->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);

	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);


	strcpy (filename, job_dirname);
	strcat (filename, "/frodo/foo");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_NOT_EMPTY (&file->items);

	item = (ConfItem *)file->items.next;

	TEST_ALLOC_SIZE (item, sizeof (ConfItem));
	TEST_ALLOC_PARENT (item, file);
	TEST_NE_P (item->job, NULL);

	job = job_find_by_name ("frodo/foo");
	TEST_EQ_P (item->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "/bin/tool");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);

	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);


	TEST_HASH_EMPTY (source->files);

	conf_source_free (source);


	/* Check that we can perform a mandatory reload of the directory,
	 * having made some changes in between.  Entries that were added
	 * should be parsed into the tree, and entries that were deleted
	 * should have been lost.
	 */
	TEST_FEATURE ("with reload of job directory");
	source = conf_source_new (NULL, job_dirname, CONF_JOB_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, job_dirname);
	strcat (filename, "/foo");

	f = fopen (filename, "w");
	fprintf (f, "exec /sbin/daemon --foo\n");
	fprintf (f, "respawn\n");
	fclose (f);

	strcpy (filename, job_dirname);
	strcat (filename, "/frodo/foo");

	unlink (filename);

	strcpy (filename, job_dirname);
	strcat (filename, "/frodo/bar");

	f = fopen (filename, "w");
	fprintf (f, "exec /bin/tool --foo\n");
	fclose (f);

	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, FALSE);

	strcpy (filename, job_dirname);
	strcat (filename, "/foo");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_NOT_EMPTY (&file->items);

	item = (ConfItem *)file->items.next;

	TEST_ALLOC_SIZE (item, sizeof (ConfItem));
	TEST_ALLOC_PARENT (item, file);
	TEST_NE_P (item->job, NULL);

	job = job_find_by_name ("foo");
	TEST_EQ_P (item->job, job);

	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command,
		     "/sbin/daemon --foo");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);

	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);


	strcpy (filename, job_dirname);
	strcat (filename, "/bar");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_NOT_EMPTY (&file->items);

	item = (ConfItem *)file->items.next;

	TEST_ALLOC_SIZE (item, sizeof (ConfItem));
	TEST_ALLOC_PARENT (item, file);
	TEST_NE_P (item->job, NULL);

	job = job_find_by_name ("bar");
	TEST_EQ_P (item->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);

	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);


	strcpy (filename, job_dirname);
	strcat (filename, "/frodo/foo");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);
	TEST_EQ_P (file, NULL);

	job = job_find_by_name ("frodo/foo");
	TEST_EQ_P (job, NULL);


	strcpy (filename, job_dirname);
	strcat (filename, "/frodo/bar");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_NOT_EMPTY (&file->items);

	item = (ConfItem *)file->items.next;

	TEST_ALLOC_SIZE (item, sizeof (ConfItem));
	TEST_ALLOC_PARENT (item, file);
	TEST_NE_P (item->job, NULL);

	job = job_find_by_name ("frodo/bar");
	TEST_EQ_P (item->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "/bin/tool --foo");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);

	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);


	TEST_HASH_EMPTY (source->files);

	conf_source_free (source);


	strcpy (filename, job_dirname);
	strcat (filename, "/frodo/bar");
	unlink (filename);

	strcpy (filename, job_dirname);
	strcat (filename, "/frodo");
	rmdir (filename);

	strcpy (filename, job_dirname);
	strcat (filename, "/bar");
	unlink (filename);

	strcpy (filename, job_dirname);
	strcat (filename, "/foo");
	unlink (filename);

	rmdir (job_dirname);
}


int
main (int   argc,
      char *argv[])
{
	test_source_new ();
	test_file_get ();
	test_item_new ();

	test_source_reload ();

	return 0;
}
