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
		item = conf_item_new (file, CONF_JOB);

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
test_source_reload_job_dir (void)
{
	ConfSource *source;
	ConfFile   *file;
	ConfItem   *item;
	Job        *job, *old_job;
	FILE       *f;
	int         ret, fd[4096], i = 0, nfds;
	char        dirname[PATH_MAX];
	char        tmpname[PATH_MAX], filename[PATH_MAX];
	fd_set      readfds, writefds, exceptfds;
	NihError   *err;

	TEST_FUNCTION ("conf_source_reload");
	program_name = "test";
	nih_log_set_priority (NIH_LOG_FATAL);

	TEST_FILENAME (dirname);
	mkdir (dirname, 0755);

	strcpy (filename, dirname);
	strcat (filename, "/foo");

	f = fopen (filename, "w");
	fprintf (f, "exec /sbin/daemon\n");
	fprintf (f, "respawn\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/bar");

	f = fopen (filename, "w");
	fprintf (f, "script\n");
	fprintf (f, "  echo\n");
	fprintf (f, "end script\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/frodo");

	mkdir (filename, 0755);

	strcpy (filename, dirname);
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
	source = conf_source_new (NULL, dirname, CONF_JOB_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_ALLOC_SIZE (source->watch, sizeof (NihWatch));
	TEST_EQ_STR (source->watch->path, dirname);
	TEST_EQ_P (source->watch->data, source);

	TEST_TRUE (fcntl (source->watch->fd, F_GETFD) & FD_CLOEXEC);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, dirname);
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


	strcpy (filename, dirname);
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


	strcpy (filename, dirname);
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
	job_free_deleted ();


	/* Check that if we create a new file in the directory, using the
	 * direct writing technique, it will be automatically parsed and
	 * loaded.
	 */
	TEST_FEATURE ("with new file in directory (direct write)");
	source = conf_source_new (NULL, dirname, CONF_JOB_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);

	strcpy (filename, dirname);
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
	strcpy (filename, dirname);
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
	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar");

	strcpy (tmpname, dirname);
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
	strcpy (filename, dirname);
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


	/* Check that if a running job is modified, it is not immediately
	 * replaced, and instead is marked as the future replacement.
	 */
	TEST_FEATURE ("with modification to running job");
	strcpy (filename, dirname);
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

	job = job_find_by_name ("frodo/bar");
	TEST_NE_P (job, NULL);

	job->goal = JOB_START;
	job->state = JOB_RUNNING;

	old_job = job;

	f = fopen (filename, "w");
	fprintf (f, "respawn\n");
	fprintf (f, "script\n");
	fprintf (f, "  sleep 15\n");
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
	TEST_EQ_P (job, old_job);
	TEST_NE_P (job, item->job);

	TEST_TRUE (item->job->respawn);
	TEST_NE_P (item->job->process[PROCESS_MAIN], NULL);
	TEST_EQ (item->job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (item->job->process[PROCESS_MAIN]->command, "sleep 15\n");

	TEST_EQ_P (item->job->replacement, NULL);
	TEST_EQ_P (item->job->replacement_for, job);

	TEST_EQ_P (job->replacement, item->job);
	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);

	TEST_EQ_P (item->entry.next, &file->items);

	old_job = item->job;


	/* Check that if we modify a job that is a replacement for a running
	 * job, the new job is marked as a replacement for the running job
	 * itself rather than having two levels of replacement.  The previous
	 * replacement should be marked for deletion.
	 */
	TEST_FEATURE ("with modification to replacement for running job");
	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar");

	f = fopen (filename, "w");
	fprintf (f, "respawn\n");
	fprintf (f, "script\n");
	fprintf (f, "  sleep 10\n");
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
	TEST_NE_P (job, item->job);

	TEST_TRUE (item->job->respawn);
	TEST_NE_P (item->job->process[PROCESS_MAIN], NULL);
	TEST_EQ (item->job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (item->job->process[PROCESS_MAIN]->command, "sleep 10\n");

	TEST_EQ_P (item->job->replacement, NULL);
	TEST_EQ_P (item->job->replacement_for, job);

	TEST_EQ_P (job->replacement, item->job);
	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);

	TEST_EQ (old_job->replacement, (void *)-1);
	TEST_EQ (old_job->replacement_for, job);

	TEST_EQ_P (item->entry.next, &file->items);

	old_job = item->job;


	/* Check that if we delete a job that is a replacement for a running
	 * job, the running job itself is marked for deletion rather than
	 * having two levels of replacement.  The previous replacement should
	 * be marked for deletion.
	 */
	TEST_FEATURE ("with deletion of replacement for running job");
	strcpy (filename, dirname);
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
	TEST_NE_P (job, NULL);

	TEST_EQ_P (job->replacement, (void *)-1);
	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);

	TEST_EQ (old_job->replacement, (void *)-1);
	TEST_EQ (old_job->replacement_for, job);

	job->goal = JOB_STOP;
	job->state = JOB_DELETED;


	/* Check that if a running job is deleted, it is not immediately
	 * freed, and instead is marked to be when stopped.
	 */
	TEST_FEATURE ("with deletion of running job");
	strcpy (filename, dirname);
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

	job = job_find_by_name ("frodo/bar");
	TEST_NE_P (job, NULL);

	job->goal = JOB_START;
	job->state = JOB_RUNNING;

	old_job = job;

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
	TEST_EQ_P (job, old_job);

	TEST_EQ_P (job->replacement, (void *)-1);
	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);

	job->goal = JOB_STOP;
	job->state = JOB_DELETED;


	/* Check that a physical error when re-parsing a job is caught,
	 * and any items in that file are lost without losing the file.
	 */
	TEST_FEATURE ("with error after modification of job");
	strcpy (filename, dirname);
	strcat (filename, "/foo");

	chmod (filename, 0200);

	f = fopen (filename, "w");
	fprintf (f, "exec /bin/tool\n");
	fprintf (f, "respawn\n");
	fclose (f);

	old_job = job_find_by_name ("foo");

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
	TEST_LIST_EMPTY (&file->items);

	job = job_find_by_name ("foo");
	TEST_EQ_P (job, NULL);

	TEST_EQ_P (old_job->replacement, (void *)-1);
	TEST_EQ (old_job->goal, JOB_STOP);
	TEST_EQ (old_job->state, JOB_DELETED);

	strcpy (filename, dirname);
	strcat (filename, "/foo");

	chmod (filename, 0644);


	/* Check that a parse error when re-parsing a job is caught,
	 * and any items in that file are lost without losing the file.
	 */
	TEST_FEATURE ("with parse error after modification of job");
	strcpy (filename, dirname);
	strcat (filename, "/foo");

	f = fopen (filename, "w");
	fprintf (f, "exec /bin/tool\n");
	fprintf (f, "respawn\n");
	fclose (f);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	old_job = job_find_by_name ("foo");

	f = fopen (filename, "w");
	fprintf (f, "respin\n");
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
	TEST_LIST_EMPTY (&file->items);

	job = job_find_by_name ("foo");
	TEST_EQ_P (job, NULL);

	TEST_EQ_P (old_job->replacement, (void *)-1);
	TEST_EQ (old_job->goal, JOB_STOP);
	TEST_EQ (old_job->state, JOB_DELETED);

	strcpy (filename, dirname);
	strcat (filename, "/foo");


	conf_source_free (source);
	job_free_deleted ();


	/* Check that a physical error parsing a file initially is caught,
	 * and doesn't affect later jobs.  The file item should remain,
	 * and only the item within it should be missing.
	 */
	TEST_FEATURE ("with error parsing job");
	strcpy (filename, dirname);
	strcat (filename, "/foo");

	chmod (filename, 0000);

	source = conf_source_new (NULL, dirname, CONF_JOB_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_ALLOC_SIZE (source->watch, sizeof (NihWatch));
	TEST_EQ_STR (source->watch->path, dirname);
	TEST_EQ_P (source->watch->data, source);

	TEST_TRUE (fcntl (source->watch->fd, F_GETFD) & FD_CLOEXEC);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, dirname);
	strcat (filename, "/foo");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);


	strcpy (filename, dirname);
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


	strcpy (filename, dirname);
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
	job_free_deleted ();

	strcpy (filename, dirname);
	strcat (filename, "/foo");
	chmod (filename, 0644);


	/* Check that a parsing error with a file is ignored and doesn't
	 * affect later jobs.
	 */
	TEST_FEATURE ("with job parse error");
	strcpy (filename, dirname);
	strcat (filename, "/foo");

	f = fopen (filename, "w");
	fprintf (f, "respin\n");
	fclose (f);

	source = conf_source_new (NULL, dirname, CONF_JOB_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_ALLOC_SIZE (source->watch, sizeof (NihWatch));
	TEST_EQ_STR (source->watch->path, dirname);
	TEST_EQ_P (source->watch->data, source);

	TEST_TRUE (fcntl (source->watch->fd, F_GETFD) & FD_CLOEXEC);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, dirname);
	strcat (filename, "/foo");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);


	strcpy (filename, dirname);
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


	strcpy (filename, dirname);
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
	job_free_deleted ();


	/* Check that we catch errors attempting to watch and walk a
	 * directory that doesn't exist.
	 */
	TEST_FEATURE ("with non-existant directory");
	strcpy (filename, dirname);
	strcat (filename, "/wibble");

	source = conf_source_new (NULL, filename, CONF_JOB_DIR);
	ret = conf_source_reload (source);

	TEST_LT (ret, 0);

	err = nih_error_get ();
	TEST_EQ (err->number, ENOENT);
	nih_free (err);

	TEST_EQ_P (source->watch, NULL);

	TEST_HASH_EMPTY (source->files);

	conf_source_free (source);
	job_free_deleted ();


	/* Check that we can catch the deletion of the top-level directory,
	 * which results in the files and directories themselves being
	 * deleted and the watch being removed from the source structure.
	 */
	TEST_FEATURE ("with deletion of top-level directory");
	source = conf_source_new (NULL, dirname, CONF_JOB_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo");
	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/frodo");
	rmdir (filename);

	strcpy (filename, dirname);
	strcat (filename, "/bar");
	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/foo");
	unlink (filename);

	rmdir (dirname);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	TEST_EQ_P (source->watch, NULL);

	TEST_HASH_EMPTY (source->files);

	job = job_find_by_name ("foo");
	TEST_EQ (job, NULL);

	job = job_find_by_name ("bar");
	TEST_EQ (job, NULL);

	job = job_find_by_name ("frodo/foo");
	TEST_EQ (job, NULL);

	conf_source_free (source);
	job_free_deleted ();


	/* Consume all available inotify instances so that the following
	 * tests run without inotify.
	 */
	for (i = 0; i < 4096; i++)
		if ((fd[i] = inotify_init ()) < 0)
			break;
no_inotify:


	TEST_FILENAME (dirname);
	mkdir (dirname, 0755);

	strcpy (filename, dirname);
	strcat (filename, "/foo");

	f = fopen (filename, "w");
	fprintf (f, "exec /sbin/daemon\n");
	fprintf (f, "respawn\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/bar");

	f = fopen (filename, "w");
	fprintf (f, "script\n");
	fprintf (f, "  echo\n");
	fprintf (f, "end script\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/frodo");

	mkdir (filename, 0755);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo");

	f = fopen (filename, "w");
	fprintf (f, "exec /bin/tool\n");
	fclose (f);


	/* Check that we can load a job directory source for the first time.
	 * Even though we don't have inotify, all entries in the directory
	 * should still be parsed.
	 */
	TEST_FEATURE ("with new job directory but no inotify");
	source = conf_source_new (NULL, dirname, CONF_JOB_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, dirname);
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


	strcpy (filename, dirname);
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


	strcpy (filename, dirname);
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
	job_free_deleted ();


	/* Check that we can perform a mandatory reload of the directory,
	 * having made some changes in between.  Entries that were added
	 * should be parsed into the tree, and entries that were deleted
	 * should have been lost.
	 */
	TEST_FEATURE ("with reload of job directory");
	source = conf_source_new (NULL, dirname, CONF_JOB_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, dirname);
	strcat (filename, "/foo");

	f = fopen (filename, "w");
	fprintf (f, "exec /sbin/daemon --foo\n");
	fprintf (f, "respawn\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo");

	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar");

	f = fopen (filename, "w");
	fprintf (f, "exec /bin/tool --foo\n");
	fclose (f);

	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, FALSE);

	strcpy (filename, dirname);
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


	strcpy (filename, dirname);
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


	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);
	TEST_EQ_P (file, NULL);

	job = job_find_by_name ("frodo/foo");
	TEST_EQ_P (job, NULL);


	strcpy (filename, dirname);
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
	job_free_deleted ();


	/* Check that a physical error parsing a file initially is caught,
	 * and doesn't affect later jobs.  The file item should remain,
	 * and only the item within it should be missing.
	 */
	TEST_FEATURE ("with error parsing job without inotify");
	strcpy (filename, dirname);
	strcat (filename, "/foo");

	chmod (filename, 0000);

	source = conf_source_new (NULL, dirname, CONF_JOB_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, dirname);
	strcat (filename, "/foo");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);


	strcpy (filename, dirname);
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


	strcpy (filename, dirname);
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
	job_free_deleted ();

	strcpy (filename, dirname);
	strcat (filename, "/foo");
	chmod (filename, 0644);


	/* Check that a parse error is ignored and doesn't affect later jobs.
	 * The file item should remain.
	 */
	TEST_FEATURE ("with job parse error without inotify");
	strcpy (filename, dirname);
	strcat (filename, "/foo");

	f = fopen (filename, "w");
	fprintf (f, "respin\n");
	fclose (f);

	source = conf_source_new (NULL, dirname, CONF_JOB_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, dirname);
	strcat (filename, "/foo");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);


	strcpy (filename, dirname);
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


	strcpy (filename, dirname);
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
	job_free_deleted ();


	/* Check that we catch errors attempting to walk a directory that
	 * doesn't exist.
	 */
	TEST_FEATURE ("with non-existant directory and no inotify");
	strcpy (filename, dirname);
	strcat (filename, "/wibble");

	source = conf_source_new (NULL, filename, CONF_JOB_DIR);
	ret = conf_source_reload (source);

	TEST_LT (ret, 0);

	err = nih_error_get ();
	TEST_EQ (err->number, ENOENT);
	nih_free (err);

	TEST_EQ_P (source->watch, NULL);

	TEST_HASH_EMPTY (source->files);

	conf_source_free (source);
	job_free_deleted ();


	/* Check that if we mandatory reload a non-existant directory, all
	 * files, items and jobs are deleted.
	 */
	TEST_FEATURE ("with reload of deleted directory");
	source = conf_source_new (NULL, dirname, CONF_JOB_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar");
	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/frodo");
	rmdir (filename);

	strcpy (filename, dirname);
	strcat (filename, "/bar");
	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/foo");
	unlink (filename);

	rmdir (dirname);

	ret = conf_source_reload (source);

	TEST_LT (ret, 0);

	err = nih_error_get ();
	TEST_EQ (err->number, ENOENT);
	nih_free (err);

	TEST_EQ_P (source->watch, NULL);

	TEST_HASH_EMPTY (source->files);

	job = job_find_by_name ("foo");
	TEST_EQ (job, NULL);

	job = job_find_by_name ("bar");
	TEST_EQ (job, NULL);

	job = job_find_by_name ("frodo/foo");
	TEST_EQ (job, NULL);

	conf_source_free (source);
	job_free_deleted ();


	nih_log_set_priority (NIH_LOG_MESSAGE);

	/* Release consumed instances */
	for (i = 0; i < 4096; i++) {
		if (fd[i] < 0)
			break;

		close (fd[i]);
	}
}


void
test_source_reload_conf_dir (void)
{
	ConfSource *source;
	ConfFile   *file;
	ConfItem   *item;
	Job        *job, *old_job;
	FILE       *f;
	int         ret, fd[4096], i = 0, nfds;
	char        dirname[PATH_MAX];
	char        filename[PATH_MAX];
	fd_set      readfds, writefds, exceptfds;
	NihError   *err;

	TEST_FUNCTION ("conf_source_reload");
	program_name = "test";
	nih_log_set_priority (NIH_LOG_FATAL);

	TEST_FILENAME (dirname);
	mkdir (dirname, 0755);

	strcpy (filename, dirname);
	strcat (filename, "/foo");

	f = fopen (filename, "w");
	fprintf (f, "job wibble\n");
	fprintf (f, "  exec /sbin/daemon\n");
	fprintf (f, "  respawn\n");
	fprintf (f, "end job\n");
	fprintf (f, "job wobble\n");
	fprintf (f, "  script\n");
	fprintf (f, "    echo\n");
	fprintf (f, "  end script\n");
	fprintf (f, "end job\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/bar");

	f = fopen (filename, "w");
	fprintf (f, "job waggle\n");
	fprintf (f, "  script\n");
	fprintf (f, "    echo\n");
	fprintf (f, "  end script\n");
	fprintf (f, "end job\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/frodo");

	mkdir (filename, 0755);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo");

	f = fopen (filename, "w");
	fprintf (f, "job woggle\n");
	fprintf (f, "  exec /bin/tool\n");
	fprintf (f, "end job\n");
	fclose (f);

	/* Make sure that we have inotify before performing some tests... */
	if ((fd[0] = inotify_init ()) < 0) {
		printf ("SKIP: inotify not available\n");
		goto no_inotify;
	}
	close (fd[0]);


	/* Check that we can load a conf directory source for the first time.
	 * An inotify watch should be established on the directory, the
	 * descriptor set to be closed-on-exec, and all entries in the
	 * directory parsed.
	 */
	TEST_FEATURE ("with new conf directory");
	source = conf_source_new (NULL, dirname, CONF_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_ALLOC_SIZE (source->watch, sizeof (NihWatch));
	TEST_EQ_STR (source->watch->path, dirname);
	TEST_EQ_P (source->watch->data, source);

	TEST_TRUE (fcntl (source->watch->fd, F_GETFD) & FD_CLOEXEC);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, dirname);
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

	job = job_find_by_name ("wibble");
	TEST_EQ_P (item->job, job);

	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "/sbin/daemon");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);

	item = (ConfItem *)file->items.next;

	TEST_ALLOC_SIZE (item, sizeof (ConfItem));
	TEST_ALLOC_PARENT (item, file);
	TEST_NE_P (item->job, NULL);

	job = job_find_by_name ("wobble");
	TEST_EQ_P (item->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);

	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);


	strcpy (filename, dirname);
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

	job = job_find_by_name ("waggle");
	TEST_EQ_P (item->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);

	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);


	strcpy (filename, dirname);
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

	job = job_find_by_name ("woggle");
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
	job_free_deleted ();


	/* Check that if we create a new file in the directory, it will
	 * be automatically parsed and loaded.
	 */
	TEST_FEATURE ("with new file in directory");
	source = conf_source_new (NULL, dirname, CONF_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar");

	f = fopen (filename, "w");
	fprintf (f, "job wiggle\n");
	fprintf (f, "  respawn\n");
	fprintf (f, "  script\n");
	fprintf (f, "    echo\n");
	fprintf (f, "  end script\n");
	fprintf (f, "end job\n");
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

	job = job_find_by_name ("wiggle");
	TEST_EQ_P (item->job, job);

	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	TEST_EQ_P (job->replacement, NULL);
	TEST_EQ_P (job->replacement_for, NULL);

	TEST_EQ_P (item->entry.next, &file->items);

	old_job = job;


	/* Check that a file in the directory we're watching can be modified;
	 * it should be parsed and the previous items marked for deletion.
	 */
	TEST_FEATURE ("with modified file in directory");
	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar");

	f = fopen (filename, "w");
	fprintf (f, "job wiggle\n");
	fprintf (f, "  respawn\n");
	fprintf (f, "  script\n");
	fprintf (f, "    sleep 5\n");
	fprintf (f, "  end script\n");
	fprintf (f, "end job\n");
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

	job = job_find_by_name ("wiggle");
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


	/* Check that we can delete a file from the directory, the metadata
	 * for it should be lost and the items should be queued for deletion.
	 */
	TEST_FEATURE ("with deleted file");
	strcpy (filename, dirname);
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

	job = job_find_by_name ("wiggle");
	TEST_EQ_P (job, NULL);

	TEST_EQ_P (old_job->replacement, (void *)-1);
	TEST_EQ (old_job->state, JOB_DELETED);


	/* Check that a physical error when re-parsing a file is caught,
	 * and any items in that file are lost without losing the file.
	 */
	TEST_FEATURE ("with error after modification of file");
	strcpy (filename, dirname);
	strcat (filename, "/foo");

	chmod (filename, 0200);

	f = fopen (filename, "w");
	fprintf (f, "job wibble\n");
	fprintf (f, "  exec /sbin/tool\n");
	fprintf (f, "  respawn\n");
	fprintf (f, "end job\n");
	fprintf (f, "job wobble\n");
	fprintf (f, "  script\n");
	fprintf (f, "    echo\n");
	fprintf (f, "  end script\n");
	fprintf (f, "end job\n");
	fclose (f);

	old_job = job_find_by_name ("wibble");

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
	TEST_LIST_EMPTY (&file->items);

	job = job_find_by_name ("wibble");
	TEST_EQ_P (job, NULL);

	job = job_find_by_name ("wobble");
	TEST_EQ_P (job, NULL);

	TEST_EQ_P (old_job->replacement, (void *)-1);
	TEST_EQ (old_job->goal, JOB_STOP);
	TEST_EQ (old_job->state, JOB_DELETED);

	strcpy (filename, dirname);
	strcat (filename, "/foo");

	chmod (filename, 0644);


	/* Check that a parse error when re-parsing a file is caught,
	 * and any items in that file are lost without losing the file.
	 */
	TEST_FEATURE ("with parse error after modification of job");
	strcpy (filename, dirname);
	strcat (filename, "/foo");

	f = fopen (filename, "w");
	fprintf (f, "job wibble\n");
	fprintf (f, "  exec /sbin/tool\n");
	fprintf (f, "  respawn\n");
	fprintf (f, "end job\n");
	fprintf (f, "job wobble\n");
	fprintf (f, "  script\n");
	fprintf (f, "    echo\n");
	fprintf (f, "  end script\n");
	fprintf (f, "end job\n");
	fclose (f);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	old_job = job_find_by_name ("wibble");

	f = fopen (filename, "w");
	fprintf (f, "job wibble\n");
	fprintf (f, "  exec /sbin/tool\n");
	fprintf (f, "  respin\n");
	fprintf (f, "end job\n");
	fprintf (f, "job wobble\n");
	fprintf (f, "  script\n");
	fprintf (f, "    echo\n");
	fprintf (f, "  end script\n");
	fprintf (f, "end job\n");
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
	TEST_LIST_EMPTY (&file->items);

	job = job_find_by_name ("wibble");
	TEST_EQ_P (job, NULL);

	job = job_find_by_name ("wobble");
	TEST_EQ_P (job, NULL);

	TEST_EQ_P (old_job->replacement, (void *)-1);
	TEST_EQ (old_job->goal, JOB_STOP);
	TEST_EQ (old_job->state, JOB_DELETED);

	strcpy (filename, dirname);
	strcat (filename, "/foo");


	conf_source_free (source);
	job_free_deleted ();


	/* Check that a physical error parsing a file initially is caught,
	 * and doesn't affect later files.  The file item should remain,
	 * and only the item within it should be missing.
	 */
	TEST_FEATURE ("with error parsing file");
	strcpy (filename, dirname);
	strcat (filename, "/foo");

	chmod (filename, 0000);

	source = conf_source_new (NULL, dirname, CONF_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_ALLOC_SIZE (source->watch, sizeof (NihWatch));
	TEST_EQ_STR (source->watch->path, dirname);
	TEST_EQ_P (source->watch->data, source);

	TEST_TRUE (fcntl (source->watch->fd, F_GETFD) & FD_CLOEXEC);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, dirname);
	strcat (filename, "/foo");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);


	strcpy (filename, dirname);
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

	job = job_find_by_name ("waggle");
	TEST_EQ_P (item->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);

	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);


	strcpy (filename, dirname);
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

	job = job_find_by_name ("woggle");
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
	job_free_deleted ();

	strcpy (filename, dirname);
	strcat (filename, "/foo");
	chmod (filename, 0644);


	/* Check that a parsing error with a file is ignored and doesn't
	 * affect later items.
	 */
	TEST_FEATURE ("with parse error");
	strcpy (filename, dirname);
	strcat (filename, "/foo");

	f = fopen (filename, "w");
	fprintf (f, "job wibble\n");
	fprintf (f, "  exec /sbin/tool\n");
	fprintf (f, "  respin\n");
	fprintf (f, "end job\n");
	fprintf (f, "job wobble\n");
	fprintf (f, "  script\n");
	fprintf (f, "    echo\n");
	fprintf (f, "  end script\n");
	fprintf (f, "end job\n");
	fclose (f);

	source = conf_source_new (NULL, dirname, CONF_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_ALLOC_SIZE (source->watch, sizeof (NihWatch));
	TEST_EQ_STR (source->watch->path, dirname);
	TEST_EQ_P (source->watch->data, source);

	TEST_TRUE (fcntl (source->watch->fd, F_GETFD) & FD_CLOEXEC);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, dirname);
	strcat (filename, "/foo");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);


	strcpy (filename, dirname);
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

	job = job_find_by_name ("waggle");
	TEST_EQ_P (item->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);

	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);


	strcpy (filename, dirname);
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

	job = job_find_by_name ("woggle");
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
	job_free_deleted ();


	/* Check that we catch errors attempting to watch and walk a
	 * directory that doesn't exist.
	 */
	TEST_FEATURE ("with non-existant directory");
	strcpy (filename, dirname);
	strcat (filename, "/wibble");

	source = conf_source_new (NULL, filename, CONF_DIR);
	ret = conf_source_reload (source);

	TEST_LT (ret, 0);

	err = nih_error_get ();
	TEST_EQ (err->number, ENOENT);
	nih_free (err);

	TEST_EQ_P (source->watch, NULL);

	TEST_HASH_EMPTY (source->files);

	conf_source_free (source);
	job_free_deleted ();


	/* Check that we can catch the deletion of the top-level directory,
	 * which results in the files and directories themselves being
	 * deleted and the watch being removed from the source structure.
	 */
	TEST_FEATURE ("with deletion of top-level directory");
	source = conf_source_new (NULL, dirname, CONF_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo");
	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/frodo");
	rmdir (filename);

	strcpy (filename, dirname);
	strcat (filename, "/bar");
	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/foo");
	unlink (filename);

	rmdir (dirname);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	TEST_EQ_P (source->watch, NULL);

	TEST_HASH_EMPTY (source->files);

	job = job_find_by_name ("wibble");
	TEST_EQ (job, NULL);

	job = job_find_by_name ("wobble");
	TEST_EQ (job, NULL);

	job = job_find_by_name ("waggle");
	TEST_EQ (job, NULL);

	job = job_find_by_name ("woggle");
	TEST_EQ (job, NULL);

	conf_source_free (source);
	job_free_deleted ();


	/* Consume all available inotify instances so that the following
	 * tests run without inotify.
	 */
	for (i = 0; i < 4096; i++)
		if ((fd[i] = inotify_init ()) < 0)
			break;
no_inotify:


	TEST_FILENAME (dirname);
	mkdir (dirname, 0755);

	strcpy (filename, dirname);
	strcat (filename, "/foo");

	f = fopen (filename, "w");
	fprintf (f, "job wibble\n");
	fprintf (f, "  exec /sbin/daemon\n");
	fprintf (f, "  respawn\n");
	fprintf (f, "end job\n");
	fprintf (f, "job wobble\n");
	fprintf (f, "  script\n");
	fprintf (f, "    echo\n");
	fprintf (f, "  end script\n");
	fprintf (f, "end job\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/bar");

	f = fopen (filename, "w");
	fprintf (f, "job waggle\n");
	fprintf (f, "  script\n");
	fprintf (f, "    echo\n");
	fprintf (f, "  end script\n");
	fprintf (f, "end job\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/frodo");

	mkdir (filename, 0755);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo");

	f = fopen (filename, "w");
	fprintf (f, "job woggle\n");
	fprintf (f, "  exec /bin/tool\n");
	fprintf (f, "end job\n");
	fclose (f);


	/* Check that we can load a conf directory source for the first time.
	 * Even though we don't have inotify, all entries in the directory
	 * should still be parsed.
	 */
	TEST_FEATURE ("with new conf directory but no inotify");
	source = conf_source_new (NULL, dirname, CONF_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, dirname);
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

	job = job_find_by_name ("wibble");
	TEST_EQ_P (item->job, job);

	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "/sbin/daemon");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);

	item = (ConfItem *)file->items.next;

	TEST_ALLOC_SIZE (item, sizeof (ConfItem));
	TEST_ALLOC_PARENT (item, file);
	TEST_NE_P (item->job, NULL);

	job = job_find_by_name ("wobble");
	TEST_EQ_P (item->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);

	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);


	strcpy (filename, dirname);
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

	job = job_find_by_name ("waggle");
	TEST_EQ_P (item->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);

	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);


	strcpy (filename, dirname);
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

	job = job_find_by_name ("woggle");
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
	job_free_deleted ();


	/* Check that we can perform a mandatory reload of the directory,
	 * having made some changes in between.  Entries that were added
	 * should be parsed into the tree, and entries that were deleted
	 * should have been lost.
	 */
	TEST_FEATURE ("with reload of conf directory");
	source = conf_source_new (NULL, dirname, CONF_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, dirname);
	strcat (filename, "/foo");

	f = fopen (filename, "w");
	fprintf (f, "job wibble\n");
	fprintf (f, "  exec /sbin/daemon --foo\n");
	fprintf (f, "  respawn\n");
	fprintf (f, "end job\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo");

	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar");

	f = fopen (filename, "w");
	fprintf (f, "job wubble\n");
	fprintf (f, "  exec /bin/tool --foo\n");
	fprintf (f, "end job\n");
	fclose (f);

	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, FALSE);

	strcpy (filename, dirname);
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

	job = job_find_by_name ("wibble");
	TEST_EQ_P (item->job, job);

	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command,
		     "/sbin/daemon --foo");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);

	TEST_LIST_EMPTY (&file->items);

	job = job_find_by_name ("wobble");
	TEST_EQ_P (job, NULL);

	nih_list_free (&file->entry);


	strcpy (filename, dirname);
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

	job = job_find_by_name ("waggle");
	TEST_EQ_P (item->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);

	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);


	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);
	TEST_EQ_P (file, NULL);

	job = job_find_by_name ("woggle");
	TEST_EQ_P (job, NULL);


	strcpy (filename, dirname);
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

	job = job_find_by_name ("wubble");
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
	job_free_deleted ();


	/* Check that a physical error parsing a file initially is caught,
	 * and doesn't affect later items.  The file item should remain,
	 * and only the item within it should be missing.
	 */
	TEST_FEATURE ("with error parsing file without inotify");
	strcpy (filename, dirname);
	strcat (filename, "/foo");

	chmod (filename, 0000);

	source = conf_source_new (NULL, dirname, CONF_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, dirname);
	strcat (filename, "/foo");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);


	strcpy (filename, dirname);
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

	job = job_find_by_name ("waggle");
	TEST_EQ_P (item->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);

	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);


	strcpy (filename, dirname);
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

	job = job_find_by_name ("wubble");
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
	job_free_deleted ();

	strcpy (filename, dirname);
	strcat (filename, "/foo");
	chmod (filename, 0644);


	/* Check that a parse error is ignored and doesn't affect later items.
	 * The file item should remain.
	 */
	TEST_FEATURE ("with parse error without inotify");
	strcpy (filename, dirname);
	strcat (filename, "/foo");

	f = fopen (filename, "w");
	fprintf (f, "job wibble\n");
	fprintf (f, "  respin\n");
	fprintf (f, "end job\n");
	fclose (f);

	source = conf_source_new (NULL, dirname, CONF_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, dirname);
	strcat (filename, "/foo");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);


	strcpy (filename, dirname);
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

	job = job_find_by_name ("waggle");
	TEST_EQ_P (item->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);

	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);


	strcpy (filename, dirname);
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

	job = job_find_by_name ("wubble");
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
	job_free_deleted ();


	/* Check that we catch errors attempting to walk a directory that
	 * doesn't exist.
	 */
	TEST_FEATURE ("with non-existant directory and no inotify");
	strcpy (filename, dirname);
	strcat (filename, "/wibble");

	source = conf_source_new (NULL, filename, CONF_DIR);
	ret = conf_source_reload (source);

	TEST_LT (ret, 0);

	err = nih_error_get ();
	TEST_EQ (err->number, ENOENT);
	nih_free (err);

	TEST_EQ_P (source->watch, NULL);

	TEST_HASH_EMPTY (source->files);

	conf_source_free (source);
	job_free_deleted ();


	/* Check that if we mandatory reload a non-existant directory, all
	 * files, items and jobs are deleted.
	 */
	TEST_FEATURE ("with reload of deleted directory");
	source = conf_source_new (NULL, dirname, CONF_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar");
	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/frodo");
	rmdir (filename);

	strcpy (filename, dirname);
	strcat (filename, "/bar");
	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/foo");
	unlink (filename);

	rmdir (dirname);

	ret = conf_source_reload (source);

	TEST_LT (ret, 0);

	err = nih_error_get ();
	TEST_EQ (err->number, ENOENT);
	nih_free (err);

	TEST_EQ_P (source->watch, NULL);

	TEST_HASH_EMPTY (source->files);

	job = job_find_by_name ("wibble");
	TEST_EQ (job, NULL);

	job = job_find_by_name ("wobble");
	TEST_EQ (job, NULL);

	job = job_find_by_name ("waggle");
	TEST_EQ (job, NULL);

	job = job_find_by_name ("wuggle");
	TEST_EQ (job, NULL);

	conf_source_free (source);
	job_free_deleted ();


	nih_log_set_priority (NIH_LOG_MESSAGE);

	/* Release consumed instances */
	for (i = 0; i < 4096; i++) {
		if (fd[i] < 0)
			break;

		close (fd[i]);
	}
}


void
test_source_reload_file (void)
{
	ConfSource *source;
	ConfFile   *file;
	ConfItem   *item;
	Job        *job, *old_job;
	FILE       *f;
	int         ret, fd[4096], i = 0, nfds;
	char        dirname[PATH_MAX];
	char        tmpname[PATH_MAX], filename[PATH_MAX];
	fd_set      readfds, writefds, exceptfds;
	NihError   *err;

	TEST_FUNCTION ("conf_source_reload");
	program_name = "test";
	nih_log_set_priority (NIH_LOG_FATAL);

	TEST_FILENAME (dirname);
	mkdir (dirname, 0755);

	strcpy (filename, dirname);
	strcat (filename, "/foo");

	f = fopen (filename, "w");
	fprintf (f, "job wibble\n");
	fprintf (f, "  exec /sbin/daemon\n");
	fprintf (f, "  respawn\n");
	fprintf (f, "end job\n");
	fprintf (f, "job wobble\n");
	fprintf (f, "  script\n");
	fprintf (f, "    echo\n");
	fprintf (f, "  end script\n");
	fprintf (f, "end job\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/bar");

	f = fopen (filename, "w");
	fprintf (f, "job waggle\n");
	fprintf (f, "  exec /bin/tool\n");
	fprintf (f, "end job\n");
	fclose (f);

	/* Make sure that we have inotify before performing some tests... */
	if ((fd[0] = inotify_init ()) < 0) {
		printf ("SKIP: inotify not available\n");
		goto no_inotify;
	}
	close (fd[0]);


	/* Check that we can load a file source for the first time.  An
	 * inotify watch should be established on the parent directory,
	 * the descriptor set to be closed-on-exec, but only that single
	 * file parsed.
	 */
	TEST_FEATURE ("with new conf file");
	strcpy (filename, dirname);
	strcat (filename, "/foo");

	source = conf_source_new (NULL, filename, CONF_FILE);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_ALLOC_SIZE (source->watch, sizeof (NihWatch));
	TEST_EQ_STR (source->watch->path, dirname);
	TEST_EQ_P (source->watch->data, source);

	TEST_TRUE (fcntl (source->watch->fd, F_GETFD) & FD_CLOEXEC);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, dirname);
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

	job = job_find_by_name ("wibble");
	TEST_EQ_P (item->job, job);

	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "/sbin/daemon");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);


	item = (ConfItem *)file->items.next;

	TEST_ALLOC_SIZE (item, sizeof (ConfItem));
	TEST_ALLOC_PARENT (item, file);
	TEST_NE_P (item->job, NULL);

	job = job_find_by_name ("wobble");
	TEST_EQ_P (item->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);

	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);


	strcpy (filename, dirname);
	strcat (filename, "/bar");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_EQ_P (file, NULL);

	job = job_find_by_name ("waggle");
	TEST_EQ_P (job, NULL);

	TEST_HASH_EMPTY (source->files);

	conf_source_free (source);
	job_free_deleted ();


	/* Check that if we create a new file in the directory, alongside
	 * the one we are watching, it is ignored.
	 */
	TEST_FEATURE ("with new file alongside conf file");
	strcpy (filename, dirname);
	strcat (filename, "/foo");

	source = conf_source_new (NULL, filename, CONF_FILE);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);

	strcpy (filename, dirname);
	strcat (filename, "/baz");

	f = fopen (filename, "w");
	fprintf (f, "job woggle\n");
	fprintf (f, "  respawn\n");
	fprintf (f, "  script\n");
	fprintf (f, "    echo\n");
	fprintf (f, "  end script\n");
	fprintf (f, "end job\n");
	fclose (f);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_EQ_P (file, NULL);


	/* Check that if we modify a file in the directory, alongside
	 * the one we are watching, it is ignored.
	 */
	TEST_FEATURE ("with modification to file alongside conf file");
	strcpy (filename, dirname);
	strcat (filename, "/bar");

	f = fopen (filename, "w");
	fprintf (f, "job waggle\n");
	fprintf (f, "  respawn\n");
	fprintf (f, "  script\n");
	fprintf (f, "    echo\n");
	fprintf (f, "  end script\n");
	fprintf (f, "end job\n");
	fclose (f);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_EQ_P (file, NULL);


	/* Check that the configuration file we're watching can be modified
	 * using the direct writing technique; it should be parsed and the
	 * previous job marked for deletion.
	 */
	TEST_FEATURE ("with modification (direct write)");
	old_job = job_find_by_name ("wibble");

	strcpy (filename, dirname);
	strcat (filename, "/foo");

	f = fopen (filename, "w");
	fprintf (f, "job wibble\n");
	fprintf (f, "  script\n");
	fprintf (f, "    sleep 5\n");
	fprintf (f, "  end script\n");
	fprintf (f, "  respawn\n");
	fprintf (f, "end job\n");
	fprintf (f, "job wobble\n");
	fprintf (f, "  script\n");
	fprintf (f, "    echo\n");
	fprintf (f, "  end script\n");
	fprintf (f, "end job\n");
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

	job = job_find_by_name ("wibble");
	TEST_EQ_P (item->job, job);

	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "sleep 5\n");

	TEST_EQ_P (job->replacement, NULL);
	TEST_EQ_P (job->replacement_for, NULL);

	TEST_EQ_P (old_job->replacement, job);
	TEST_EQ (old_job->state, JOB_DELETED);

	TEST_EQ_P (item->entry.next, file->items.prev);

	item = (ConfItem *)file->items.prev;

	TEST_ALLOC_SIZE (item, sizeof (ConfItem));
	TEST_ALLOC_PARENT (item, file);
	TEST_NE_P (item->job, NULL);

	job = job_find_by_name ("wobble");
	TEST_EQ_P (item->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	TEST_EQ_P (item->entry.next, &file->items);


	/* Check that the configuration file we're watching can be modified
	 * using the write and then rename technique; it should be parsed
	 * and the previous job marked for deletion.
	 */
	TEST_FEATURE ("with modification (atomic rename)");
	old_job = job_find_by_name ("wibble");

	strcpy (tmpname, dirname);
	strcat (tmpname, "/.foo.tmp");

	f = fopen (tmpname, "w");
	fprintf (f, "job wibble\n");
	fprintf (f, "  script\n");
	fprintf (f, "    sleep 15\n");
	fprintf (f, "  end script\n");
	fprintf (f, "  respawn\n");
	fprintf (f, "end job\n");
	fprintf (f, "job wobble\n");
	fprintf (f, "  script\n");
	fprintf (f, "    echo\n");
	fprintf (f, "  end script\n");
	fprintf (f, "end job\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/foo");

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

	job = job_find_by_name ("wibble");
	TEST_EQ_P (item->job, job);

	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "sleep 15\n");

	TEST_EQ_P (job->replacement, NULL);
	TEST_EQ_P (job->replacement_for, NULL);

	TEST_EQ_P (old_job->replacement, job);
	TEST_EQ (old_job->state, JOB_DELETED);

	TEST_EQ_P (item->entry.next, file->items.prev);

	item = (ConfItem *)file->items.prev;

	TEST_ALLOC_SIZE (item, sizeof (ConfItem));
	TEST_ALLOC_PARENT (item, file);
	TEST_NE_P (item->job, NULL);

	job = job_find_by_name ("wobble");
	TEST_EQ_P (item->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	TEST_EQ_P (item->entry.next, &file->items);


	/* Check that we can delete the configuration file that we're
	 * watching, the metadata for it should be lost and the job should
	 * be queued for deletion.
	 */
	TEST_FEATURE ("with deletion");
	old_job = job_find_by_name ("wibble");

	strcpy (filename, dirname);
	strcat (filename, "/foo");

	unlink (filename);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_EQ_P (file, NULL);

	job = job_find_by_name ("wibble");
	TEST_EQ_P (job, NULL);

	TEST_EQ_P (old_job->replacement, (void *)-1);
	TEST_EQ (old_job->state, JOB_DELETED);

	job = job_find_by_name ("wobble");
	TEST_EQ_P (job, NULL);


	/* Check that the watch allows us to see if the file we want is
	 * created using the direct writing technique, and thus parsed.
	 */
	TEST_FEATURE ("with creation (direct write)");
	strcpy (filename, dirname);
	strcat (filename, "/foo");

	f = fopen (filename, "w");
	fprintf (f, "job wibble\n");
	fprintf (f, "  exec /sbin/daemon\n");
	fprintf (f, "  respawn\n");
	fprintf (f, "end job\n");
	fprintf (f, "job wobble\n");
	fprintf (f, "  script\n");
	fprintf (f, "    echo\n");
	fprintf (f, "  end script\n");
	fprintf (f, "end job\n");
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

	job = job_find_by_name ("wibble");
	TEST_EQ_P (item->job, job);

	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "/sbin/daemon");

	TEST_EQ_P (item->entry.next, file->items.prev);

	item = (ConfItem *)file->items.prev;

	TEST_ALLOC_SIZE (item, sizeof (ConfItem));
	TEST_ALLOC_PARENT (item, file);
	TEST_NE_P (item->job, NULL);

	job = job_find_by_name ("wobble");
	TEST_EQ_P (item->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	TEST_EQ_P (item->entry.next, &file->items);


	/* Check that the watch allows us to see if the file we want is
	 * created using the write and rename, and thus parsed.
	 */
	TEST_FEATURE ("with creation (atomic rename)");
	strcpy (filename, dirname);
	strcat (filename, "/foo");

	unlink (filename);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_EQ_P (file, NULL);

	strcpy (tmpname, dirname);
	strcat (tmpname, "/.foo.tmp");

	f = fopen (tmpname, "w");
	fprintf (f, "job wibble\n");
	fprintf (f, "  exec /sbin/daemon\n");
	fprintf (f, "  respawn\n");
	fprintf (f, "end job\n");
	fprintf (f, "job wobble\n");
	fprintf (f, "  script\n");
	fprintf (f, "    echo\n");
	fprintf (f, "  end script\n");
	fprintf (f, "end job\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/foo");

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

	job = job_find_by_name ("wibble");
	TEST_EQ_P (item->job, job);

	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "/sbin/daemon");

	TEST_EQ_P (item->entry.next, file->items.prev);

	item = (ConfItem *)file->items.prev;

	TEST_ALLOC_SIZE (item, sizeof (ConfItem));
	TEST_ALLOC_PARENT (item, file);
	TEST_NE_P (item->job, NULL);

	job = job_find_by_name ("wobble");
	TEST_EQ_P (item->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	TEST_EQ_P (item->entry.next, &file->items);


	/* Check that a physical error when re-parsing a job is caught,
	 * and any items in that file are lost without losing the file.
	 */
	TEST_FEATURE ("with error after modification");
	old_job = job_find_by_name ("wibble");

	strcpy (filename, dirname);
	strcat (filename, "/foo");

	chmod (filename, 0200);

	f = fopen (filename, "w");
	fprintf (f, "job wibble\n");
	fprintf (f, "  exec /bin/tool\n");
	fprintf (f, "  respawn\n");
	fprintf (f, "end job\n");
	fprintf (f, "job wobble\n");
	fprintf (f, "  script\n");
	fprintf (f, "    echo\n");
	fprintf (f, "  end script\n");
	fprintf (f, "end job\n");
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
	TEST_LIST_EMPTY (&file->items);

	job = job_find_by_name ("wibble");
	TEST_EQ_P (job, NULL);

	TEST_EQ_P (old_job->replacement, (void *)-1);
	TEST_EQ (old_job->goal, JOB_STOP);
	TEST_EQ (old_job->state, JOB_DELETED);

	job = job_find_by_name ("wobble");
	TEST_EQ_P (job, NULL);

	strcpy (filename, dirname);
	strcat (filename, "/foo");

	chmod (filename, 0644);


	/* Check that a parse error when re-parsing a file is caught,
	 * and any items in that file are lost without losing the file.
	 */
	TEST_FEATURE ("with parse error after modification");
	strcpy (filename, dirname);
	strcat (filename, "/foo");

	f = fopen (filename, "w");
	fprintf (f, "job wibble\n");
	fprintf (f, "  exec /bin/tool\n");
	fprintf (f, "  respawn\n");
	fprintf (f, "end job\n");
	fprintf (f, "job wobble\n");
	fprintf (f, "  script\n");
	fprintf (f, "    echo\n");
	fprintf (f, "  end script\n");
	fprintf (f, "end job\n");
	fclose (f);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	old_job = job_find_by_name ("wibble");

	f = fopen (filename, "w");
	fprintf (f, "job wibble\n");
	fprintf (f, "  exec /bin/tool\n");
	fprintf (f, "  respin\n");
	fprintf (f, "end job\n");
	fprintf (f, "job wobble\n");
	fprintf (f, "  script\n");
	fprintf (f, "    echo\n");
	fprintf (f, "  end script\n");
	fprintf (f, "end job\n");
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
	TEST_LIST_EMPTY (&file->items);

	job = job_find_by_name ("wibble");
	TEST_EQ_P (job, NULL);

	TEST_EQ_P (old_job->replacement, (void *)-1);
	TEST_EQ (old_job->goal, JOB_STOP);
	TEST_EQ (old_job->state, JOB_DELETED);

	conf_source_free (source);
	job_free_deleted ();


	/* Check that a physical error parsing a file initially is caught
	 * and returned as an error.
	 */
	TEST_FEATURE ("with physical error parsing file");
	strcpy (filename, dirname);
	strcat (filename, "/foo");

	f = fopen (filename, "w");
	fprintf (f, "job wibble\n");
	fprintf (f, "  exec /bin/tool\n");
	fprintf (f, "  respawn\n");
	fprintf (f, "end job\n");
	fprintf (f, "job wobble\n");
	fprintf (f, "  script\n");
	fprintf (f, "    echo\n");
	fprintf (f, "  end script\n");
	fprintf (f, "end job\n");
	fclose (f);

	chmod (filename, 0000);

	source = conf_source_new (NULL, filename, CONF_FILE);
	ret = conf_source_reload (source);

	TEST_LT (ret, 0);

	err = nih_error_get ();
	TEST_EQ (err->number, EACCES);
	nih_free (err);

	TEST_ALLOC_SIZE (source->watch, sizeof (NihWatch));
	TEST_EQ_STR (source->watch->path, dirname);
	TEST_EQ_P (source->watch->data, source);

	TEST_TRUE (fcntl (source->watch->fd, F_GETFD) & FD_CLOEXEC);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, dirname);
	strcat (filename, "/foo");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);

	TEST_HASH_EMPTY (source->files);

	conf_source_free (source);
	job_free_deleted ();

	strcpy (filename, dirname);
	strcat (filename, "/foo");
	chmod (filename, 0644);


	/* Check that a parsing error with a file doesn't return an error
	 * code, since that's no different to an empty file.
	 */
	TEST_FEATURE ("with parse error");
	strcpy (filename, dirname);
	strcat (filename, "/foo");

	f = fopen (filename, "w");
	fprintf (f, "job wibble\n");
	fprintf (f, "  exec /bin/tool\n");
	fprintf (f, "  respin\n");
	fprintf (f, "end job\n");
	fprintf (f, "job wobble\n");
	fprintf (f, "  script\n");
	fprintf (f, "    echo\n");
	fprintf (f, "  end script\n");
	fprintf (f, "end job\n");
	fclose (f);

	source = conf_source_new (NULL, filename, CONF_FILE);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_ALLOC_SIZE (source->watch, sizeof (NihWatch));
	TEST_EQ_STR (source->watch->path, dirname);
	TEST_EQ_P (source->watch->data, source);

	TEST_TRUE (fcntl (source->watch->fd, F_GETFD) & FD_CLOEXEC);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, dirname);
	strcat (filename, "/foo");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);

	TEST_HASH_EMPTY (source->files);

	conf_source_free (source);
	job_free_deleted ();


	/* Check that we catch errors attempting to parse a file that
	 * doesn't exist, however we should have a watch on it, so that if
	 * it is created in the future it will be automatically parsed.
	 */
	TEST_FEATURE ("with non-existant file");
	strcpy (filename, dirname);
	strcat (filename, "/wibble");

	source = conf_source_new (NULL, filename, CONF_FILE);
	ret = conf_source_reload (source);

	TEST_LT (ret, 0);

	err = nih_error_get ();
	TEST_EQ (err->number, ENOENT);
	nih_free (err);

	TEST_ALLOC_SIZE (source->watch, sizeof (NihWatch));
	TEST_EQ_STR (source->watch->path, dirname);
	TEST_EQ_P (source->watch->data, source);

	TEST_TRUE (fcntl (source->watch->fd, F_GETFD) & FD_CLOEXEC);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);

	TEST_HASH_EMPTY (source->files);

	conf_source_free (source);
	job_free_deleted ();


	/* Check that we can catch the deletion of the parent directory,
	 * which results in the file being deleted and the watch removed.
	 */
	TEST_FEATURE ("with deletion of parent directory");
	strcpy (filename, dirname);
	strcat (filename, "/foo");

	source = conf_source_new (NULL, filename, CONF_FILE);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);

	strcpy (filename, dirname);
	strcat (filename, "/baz");
	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/bar");
	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/foo");
	unlink (filename);

	rmdir (dirname);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	TEST_EQ_P (source->watch, NULL);

	TEST_HASH_EMPTY (source->files);

	job = job_find_by_name ("wibble");
	TEST_EQ (job, NULL);

	job = job_find_by_name ("wobble");
	TEST_EQ (job, NULL);

	conf_source_free (source);
	job_free_deleted ();


	/* Consume all available inotify instances so that the following
	 * tests run without inotify.
	 */
	for (i = 0; i < 4096; i++)
		if ((fd[i] = inotify_init ()) < 0)
			break;
no_inotify:


	TEST_FILENAME (dirname);
	mkdir (dirname, 0755);

	strcpy (filename, dirname);
	strcat (filename, "/foo");

	f = fopen (filename, "w");
	fprintf (f, "job wibble\n");
	fprintf (f, "  exec /sbin/daemon\n");
	fprintf (f, "  respawn\n");
	fprintf (f, "end job\n");
	fprintf (f, "job wobble\n");
	fprintf (f, "  script\n");
	fprintf (f, "    echo\n");
	fprintf (f, "  end script\n");
	fprintf (f, "end job\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/bar");

	f = fopen (filename, "w");
	fprintf (f, "job waggle\n");
	fprintf (f, "  exec /bin/tool\n");
	fprintf (f, "end job\n");
	fclose (f);


	/* Check that we can load a conf file source for the first time.
	 * Even though we don't have inotify, the file should still be parsed.
	 */
	TEST_FEATURE ("with new conf file but no inotify");
	strcpy (filename, dirname);
	strcat (filename, "/foo");

	source = conf_source_new (NULL, filename, CONF_FILE);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, dirname);
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

	job = job_find_by_name ("wibble");
	TEST_EQ_P (item->job, job);

	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "/sbin/daemon");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);

	item = (ConfItem *)file->items.next;

	TEST_ALLOC_SIZE (item, sizeof (ConfItem));
	TEST_ALLOC_PARENT (item, file);
	TEST_NE_P (item->job, NULL);

	job = job_find_by_name ("wobble");
	TEST_EQ_P (item->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);

	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);

	job = job_find_by_name ("waggle");
	TEST_EQ_P (job, NULL);

	TEST_HASH_EMPTY (source->files);

	conf_source_free (source);
	job_free_deleted ();


	/* Check that we can perform a mandatory reload of the file,
	 * having made some changes in between.  Items that were added
	 * should be parsed and items that were removed should be deleted.
	 */
	TEST_FEATURE ("with reload of conf file");
	strcpy (filename, dirname);
	strcat (filename, "/foo");

	source = conf_source_new (NULL, filename, CONF_FILE);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, dirname);
	strcat (filename, "/foo");

	f = fopen (filename, "w");
	fprintf (f, "job wibble\n");
	fprintf (f, "  exec /sbin/daemon --foo\n");
	fprintf (f, "  respawn\n");
	fprintf (f, "end job\n");
	fprintf (f, "job wiggle\n");
	fprintf (f, "  script\n");
	fprintf (f, "    echo\n");
	fprintf (f, "  end script\n");
	fprintf (f, "end job\n");
	fclose (f);

	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, FALSE);

	strcpy (filename, dirname);
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

	job = job_find_by_name ("wibble");
	TEST_EQ_P (item->job, job);

	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command,
		     "/sbin/daemon --foo");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);

	item = (ConfItem *)file->items.next;

	TEST_ALLOC_SIZE (item, sizeof (ConfItem));
	TEST_ALLOC_PARENT (item, file);
	TEST_NE_P (item->job, NULL);

	job = job_find_by_name ("wiggle");
	TEST_EQ_P (item->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	nih_list_free (&job->entry);
	nih_list_free (&item->entry);

	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);

	job = job_find_by_name ("wobble");
	TEST_EQ_P (job, NULL);

	TEST_HASH_EMPTY (source->files);

	conf_source_free (source);
	job_free_deleted ();


	/* Check that a physical error parsing a file initially is caught
	 * and returned.
	 */
	TEST_FEATURE ("with error parsing file without inotify");
	strcpy (filename, dirname);
	strcat (filename, "/foo");

	chmod (filename, 0000);

	source = conf_source_new (NULL, filename, CONF_FILE);
	ret = conf_source_reload (source);

	TEST_LT (ret, 0);

	err = nih_error_get ();
	TEST_EQ (err->number, EACCES);
	nih_free (err);

	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, TRUE);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);

	TEST_HASH_EMPTY (source->files);

	conf_source_free (source);
	job_free_deleted ();

	chmod (filename, 0644);


	/* Check that a parse error is ignored and doesn't return an error,
	 * being treated equivalent to an empty file.
	 */
	TEST_FEATURE ("with parse error without inotify");
	strcpy (filename, dirname);
	strcat (filename, "/foo");

	f = fopen (filename, "w");
	fprintf (f, "job wibble\n");
	fprintf (f, "  exec /sbin/daemon --foo\n");
	fprintf (f, "  respin\n");
	fprintf (f, "end job\n");
	fprintf (f, "job wiggle\n");
	fprintf (f, "  script\n");
	fprintf (f, "    echo\n");
	fprintf (f, "  end script\n");
	fprintf (f, "end job\n");
	fclose (f);

	source = conf_source_new (NULL, filename, CONF_FILE);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, TRUE);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);

	TEST_HASH_EMPTY (source->files);

	conf_source_free (source);
	job_free_deleted ();


	/* Check that trying to parse a file that doesn't exist returns
	 * an error.
	 */
	TEST_FEATURE ("with non-existant conf file and no inotify");
	strcpy (filename, dirname);
	strcat (filename, "/wibble");

	source = conf_source_new (NULL, filename, CONF_FILE);
	ret = conf_source_reload (source);

	TEST_LT (ret, 0);

	err = nih_error_get ();
	TEST_EQ (err->number, ENOENT);
	nih_free (err);

	TEST_EQ_P (source->watch, NULL);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);

	TEST_HASH_EMPTY (source->files);

	conf_source_free (source);
	job_free_deleted ();


	/* Check that if we mandatory reload a non-existant file, all items
	 * are deleted.
	 */
	TEST_FEATURE ("with reload of deleted conf file");
	strcpy (filename, dirname);
	strcat (filename, "/foo");

	source = conf_source_new (NULL, filename, CONF_FILE);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);

	unlink (filename);

	ret = conf_source_reload (source);

	TEST_LT (ret, 0);

	err = nih_error_get ();
	TEST_EQ (err->number, ENOENT);
	nih_free (err);

	TEST_EQ_P (source->watch, NULL);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_LIST_EMPTY (&file->items);

	nih_list_free (&file->entry);

	TEST_HASH_EMPTY (source->files);

	job = job_find_by_name ("wibble");
	TEST_EQ (job, NULL);

	job = job_find_by_name ("wobble");
	TEST_EQ (job, NULL);

	conf_source_free (source);
	job_free_deleted ();


	strcpy (filename, dirname);
	strcat (filename, "/bar");
	unlink (filename);

	rmdir (dirname);

	nih_log_set_priority (NIH_LOG_MESSAGE);

	/* Release consumed instances */
	for (i = 0; i < 4096; i++) {
		if (fd[i] < 0)
			break;

		close (fd[i]);
	}
}


static int destructor_called = 0;

static int
my_destructor (void *ptr)
{
	destructor_called++;

	return 100;
}

void
test_source_free (void)
{
	ConfSource *source;
	ConfFile   *file;
	char        dirname[PATH_MAX];
	int         ret, fd;

	TEST_FUNCTION ("conf_source_free");
	TEST_FILENAME (dirname);
	mkdir (dirname, 0755);


	/* Make sure that we have inotify before performing some tests... */
	if ((fd = inotify_init ()) < 0) {
		printf ("SKIP: inotify not available\n");
		goto no_inotify;
	}
	close (fd);


	/* Check that when the source is freed, the associated watch structure
	 * is freed along with its io destructor, and the return value of the
	 * destructors are returned.
	 */
	TEST_FEATURE ("with attached watch");
	source = conf_source_new (NULL, dirname, CONF_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);

	destructor_called = 0;
	nih_alloc_set_destructor (source, my_destructor);
	nih_alloc_set_destructor (source->watch, my_destructor);
	nih_alloc_set_destructor (source->watch->io, my_destructor);

	ret = conf_source_free (source);

	TEST_EQ (ret, 100);
	TEST_EQ (destructor_called, 3);


no_inotify:
	/* Check that we can free a source that has no attached watch.
	 * The destructor should be called, and the return value returned.
	 */
	TEST_FEATURE ("with no watch");
	source = conf_source_new (NULL, dirname, CONF_DIR);

	destructor_called = 0;
	nih_alloc_set_destructor (source, my_destructor);

	ret = conf_source_free (source);

	TEST_EQ (ret, 100);
	TEST_EQ (destructor_called, 1);


	/* Check that files attached to the source are freed as well. */
	TEST_FEATURE ("with attached files");
	source = conf_source_new (NULL, dirname, CONF_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);

	destructor_called = 0;

	file = conf_file_get (source, "/path/to/file1");
	nih_alloc_set_destructor (file, my_destructor);

	file = conf_file_get (source, "/path/to/file2");
	nih_alloc_set_destructor (file, my_destructor);

	ret = conf_source_free (source);

	TEST_EQ (ret, 0);
	TEST_EQ (destructor_called, 2);


	rmdir (dirname);
}

void
test_file_free (void)
{
	ConfSource *source;
	ConfFile   *file;
	ConfItem   *item;
	int         ret;

	/* Check that when a file is freed, the attached items are also
	 * freed and the return value from the destructor is returned.
	 * The file should have been removed from its containing list.
	 */
	TEST_FUNCTION ("conf_file_free");
	source = conf_source_new (NULL, "/path", CONF_JOB_DIR);
	file = conf_file_get (source, "/path/to/file");

	destructor_called = 0;
	nih_alloc_set_destructor (file, my_destructor);

	item = conf_item_new (file, CONF_JOB);
	item->job = job_new (NULL, "foo");
	nih_alloc_set_destructor (item, my_destructor);

	item = conf_item_new (file, CONF_JOB);
	item->job = job_new (NULL, "bar");
	nih_alloc_set_destructor (item, my_destructor);

	ret = conf_file_free (file);

	TEST_EQ (ret, 100);
	TEST_EQ (destructor_called, 3);

	TEST_EQ_P (nih_hash_lookup (source->files, "/path/to/file"), NULL);

	conf_source_free (source);
	job_free_deleted ();
}

void
test_item_free (void)
{
	ConfSource *source;
	ConfFile   *file;
	ConfItem   *item;
	Job        *job, *old_job;
	int         ret;

	TEST_FUNCTION ("conf_item_free");
	source = conf_source_new (NULL, "/path", CONF_JOB_DIR);
	file = conf_file_get (source, "/path/to/file");


	/* Check that when a job item is freed, the attached job is
	 * marked to be deleted and the state change invoked.
	 */
	TEST_FEATURE ("with stopped job");
	item = conf_item_new (file, CONF_JOB);
	item->job = job_new (NULL, "foo");

	destructor_called = 0;
	nih_alloc_set_destructor (item, my_destructor);

	job = item->job;

	ret = conf_item_free (item);

	TEST_EQ (ret, 100);
	TEST_TRUE (destructor_called);

	TEST_EQ_P (job->replacement, (void *)-1);
	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_DELETED);


	/* Check that a job that already has a replacement is not altered
	 * and instead just has its state change pushed.
	 */
	TEST_FEATURE ("with stopped job with replacement");
	item = conf_item_new (file, CONF_JOB);
	item->job = job_new (NULL, "foo");

	destructor_called = 0;
	nih_alloc_set_destructor (item, my_destructor);

	job = item->job;
	job->replacement = job_new (NULL, "foo");
	old_job = job->replacement;
	old_job->replacement_for = job;

	ret = conf_item_free (item);

	TEST_EQ (ret, 100);
	TEST_TRUE (destructor_called);

	TEST_EQ_P (job->replacement, old_job);
	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_DELETED);

	TEST_EQ_P (old_job->replacement_for, NULL);

	nih_list_free (&old_job->entry);


	/* Check that when a job item is freed, if the attached job is running
	 * then it is only marked for deletion and the state is left alone.
	 */
	TEST_FEATURE ("with running job");
	item = conf_item_new (file, CONF_JOB);
	item->job = job_new (NULL, "foo");

	destructor_called = 0;
	nih_alloc_set_destructor (item, my_destructor);

	job = item->job;
	job->goal = JOB_START;
	job->state = JOB_RUNNING;

	ret = conf_item_free (item);

	TEST_EQ (ret, 100);
	TEST_TRUE (destructor_called);

	TEST_EQ_P (job->replacement, (void *)-1);
	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);

	job->goal = JOB_STOP;
	job->state = JOB_DELETED;


	/* Check that a running job that already has a replacement is not
	 * altered at all.
	 */
	TEST_FEATURE ("with running job with replacement");
	item = conf_item_new (file, CONF_JOB);
	item->job = job_new (NULL, "foo");

	destructor_called = 0;
	nih_alloc_set_destructor (item, my_destructor);

	job = item->job;
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->replacement = job_new (NULL, "foo");
	old_job = job->replacement;
	old_job->replacement_for = job;

	ret = conf_item_free (item);

	TEST_EQ (ret, 100);
	TEST_TRUE (destructor_called);

	TEST_EQ_P (job->replacement, old_job);
	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);

	TEST_EQ_P (old_job->replacement_for, job);

	nih_list_free (&old_job->entry);


	/* If the job attached to the item is actually a replacement for
	 * another already, then cut out the middle man and mark it to
	 * be replaced by our replacement, and change its state.
	 */
	TEST_FEATURE ("with replacement job");
	old_job = job_new (NULL, "foo");

	item = conf_item_new (file, CONF_JOB);
	item->job = job_new (NULL, "foo");

	destructor_called = 0;
	nih_alloc_set_destructor (item, my_destructor);

	job = item->job;
	job->replacement_for = old_job;
	old_job->replacement = job;

	ret = conf_item_free (item);

	TEST_EQ (ret, 100);
	TEST_TRUE (destructor_called);

	TEST_EQ_P (job->replacement, (void *)-1);
	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_DELETED);

	TEST_EQ_P (old_job->replacement, (void *)-1);
	TEST_EQ (old_job->goal, JOB_STOP);
	TEST_EQ (old_job->state, JOB_DELETED);

	nih_list_free (&old_job->entry);


	conf_source_free (source);
	job_free_deleted ();
}


int
main (int   argc,
      char *argv[])
{
	test_source_new ();
	test_file_get ();
	test_item_new ();
	test_source_reload_job_dir ();
	test_source_reload_conf_dir ();
	test_source_reload_file ();
	test_source_free ();
	test_file_free ();
	test_item_free ();

	return 0;
}
