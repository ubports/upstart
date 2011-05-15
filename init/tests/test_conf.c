/* upstart
 *
 * test_conf.c - test suite for init/conf.c
 *
 * Copyright © 2011 Google Inc.
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

#include <sys/inotify.h>
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

#include "job_class.h"
#include "job.h"
#include "conf.h"


void
test_source_new (void)
{
	ConfSource *source;

	conf_init ();

	/* Check that we can request a new ConfSource structure, it should be
	 * allocated with nih_alloc and placed into the conf_sources list.
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

		nih_free (source);
	}
}

void
test_file_new (void)
{
	ConfSource *source;
	ConfFile   *file;

	/* Check that we can request a new ConfFile structure, it should be
	 * allocated with nih_alloc and placed into the files hash table of
	 * the source, with the flag copied.
	 */
	TEST_FUNCTION ("conf_file_new");
	source = conf_source_new (NULL, "/tmp", CONF_JOB_DIR);

	TEST_ALLOC_FAIL {
		file = conf_file_new (source, "/tmp/foo");

		if (test_alloc_failed) {
			TEST_EQ_P (file, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (file, sizeof (ConfFile));
		TEST_ALLOC_PARENT (file, source);
		TEST_LIST_NOT_EMPTY (&file->entry);
		TEST_EQ_P (file->source, source);
		TEST_ALLOC_PARENT (file->path, file);
		TEST_EQ_STR (file->path, "/tmp/foo");
		TEST_EQ (file->flag, source->flag);
		TEST_EQ_P (file->data, NULL);

		TEST_EQ_P ((void *)nih_hash_lookup (source->files, "/tmp/foo"),
			   file);

		nih_free (file);
	}

	nih_free (source);
}

void
test_source_reload_job_dir (void)
{
	ConfSource *source;
	ConfFile   *file, *old_file;
	JobClass   *job, *old_job;
	Job        *instance;
	FILE       *f;
	int         ret, fd[4096], i = 0, nfds;
	char        dirname[PATH_MAX];
	char        tmpname[PATH_MAX], filename[PATH_MAX];
	fd_set      readfds, writefds, exceptfds;
	NihError   *err;

	TEST_FUNCTION_FEATURE ("conf_source_reload",
			       "with job directory");
	program_name = "test";
	nih_log_set_priority (NIH_LOG_FATAL);

	TEST_FILENAME (dirname);
	mkdir (dirname, 0755);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

	f = fopen (filename, "w");
	fprintf (f, "exec /sbin/daemon\n");
	fprintf (f, "respawn\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/bar.conf");

	f = fopen (filename, "w");
	fprintf (f, "script\n");
	fprintf (f, "  echo\n");
	fprintf (f, "end script\n");
	fclose (f);

	/* FIXME sub-directories are broken */
	strcpy (filename, dirname);
	strcat (filename, "/frodo");

	mkdir (filename, 0755);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo.conf");

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
	strcat (filename, "/foo.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_NE_P (file->job, NULL);

	job = (JobClass *)nih_hash_lookup (job_classes, "foo");
	TEST_EQ_P (file->job, job);

	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "/sbin/daemon");

	nih_free (file);


	strcpy (filename, dirname);
	strcat (filename, "/bar.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_NE_P (file->job, NULL);

	job = (JobClass *)nih_hash_lookup (job_classes, "bar");
	TEST_EQ_P (file->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	nih_free (file);


	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_NE_P (file->job , NULL);

	job = (JobClass *)nih_hash_lookup (job_classes, "frodo/foo");
	TEST_EQ_P (file->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "/bin/tool");

	nih_free (file);

	TEST_HASH_EMPTY (source->files);

	nih_free (source);


	/* Check that if we create a new file in the directory, using the
	 * direct writing technique, it will be automatically parsed and
	 * loaded.
	 */
	TEST_FEATURE ("with new file in directory (direct write)");
	source = conf_source_new (NULL, dirname, CONF_JOB_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar.conf");

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
	TEST_NE_P (file->job, NULL);

	job = (JobClass *)nih_hash_lookup (job_classes, "frodo/bar");
	TEST_EQ_P (file->job, job);

	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");
	TEST_EQ (job->deleted, FALSE);

	old_file = file;
	old_job = job;


	/* Check that a file in the directory we're watching can be modified
	 * using the direct writing technique; it should be parsed and the
	 * previous job marked for deletion.
	 */
	TEST_FEATURE ("with modified job (direct write)");
	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar.conf");

	f = fopen (filename, "w");
	fprintf (f, "respawn\n");
	fprintf (f, "script\n");
	fprintf (f, "  sleep 5\n");
	fprintf (f, "end script\n");
	fclose (f);

	TEST_FREE_TAG (old_file);
	TEST_FREE_TAG (old_job);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	TEST_FREE (old_file);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_NE_P (file->job, NULL);

	TEST_FREE (old_job);

	job = (JobClass *)nih_hash_lookup (job_classes, "frodo/bar");
	TEST_EQ_P (file->job, job);

	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "sleep 5\n");
	TEST_EQ (job->deleted, FALSE);

	old_file = file;
	old_job = job;


	/* Check that a file in the directory we're watching can be modified
	 * using the write and then rename technique; it should be parsed and
	 * the previous job marked for deletion.
	 */
	TEST_FEATURE ("with modified job (atomic rename)");
	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar.conf");

	strcpy (tmpname, dirname);
	strcat (tmpname, "/frodo/.bar.conf.swp");

	f = fopen (tmpname, "w");
	fprintf (f, "respawn\n");
	fprintf (f, "script\n");
	fprintf (f, "  sleep 15\n");
	fprintf (f, "end script\n");
	fclose (f);

	rename (tmpname, filename);

	TEST_FREE_TAG (old_file);
	TEST_FREE_TAG (old_job);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	TEST_FREE (old_file);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_NE_P (file->job, NULL);

	TEST_FREE (old_job);

	job = (JobClass *)nih_hash_lookup (job_classes, "frodo/bar");
	TEST_EQ_P (file->job, job);

	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "sleep 15\n");
	TEST_EQ (job->deleted, FALSE);

	old_file = file;
	old_job = job;


	/* Check that we can delete a file from the directory, the metadata
	 * for it should be lost and the job should be immediately freed
	 * since it is not running.
	 */
	TEST_FEATURE ("with deleted job");
	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar.conf");

	unlink (filename);

	TEST_FREE_TAG (old_file);
	TEST_FREE_TAG (old_job);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	TEST_FREE (old_file);
	TEST_FREE (old_job);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);
	TEST_EQ_P (file, NULL);

	job = (JobClass *)nih_hash_lookup (job_classes, "frodo/bar");
	TEST_EQ_P (job, NULL);


	/* Check that if a running job is modified, it is not immediately
	 * replaced, and is instead marked as deleted with the new job in
	 * the sources list ready for action.
	 */
	TEST_FEATURE ("with modification to running job");
	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar.conf");

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

	job = (JobClass *)nih_hash_lookup (job_classes, "frodo/bar");
	TEST_NE_P (job, NULL);

	instance = job_new (job, "");
	instance->goal = JOB_START;
	instance->state = JOB_RUNNING;

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
	TEST_NE_P (file->job, NULL);

	job = (JobClass *)nih_hash_lookup (job_classes, "frodo/bar");
	TEST_EQ_P (job, old_job);
	TEST_NE_P (job, file->job);

	TEST_TRUE (file->job->respawn);
	TEST_NE_P (file->job->process[PROCESS_MAIN], NULL);
	TEST_EQ (file->job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (file->job->process[PROCESS_MAIN]->command, "sleep 15\n");
	TEST_FALSE (file->job->deleted);

	TEST_TRUE (job->deleted);
	TEST_HASH_NOT_EMPTY (job->instances);
	instance = (Job *)nih_hash_lookup (job->instances, "");

	TEST_EQ (instance->goal, JOB_START);
	TEST_EQ (instance->state, JOB_RUNNING);

	old_file = file;
	old_job = file->job;


	/* Check that if we modify a job that is a replacement for a running
	 * job, the replacement is freed immediately and the new replacement
	 * is kept instead.
	 */
	TEST_FEATURE ("with modification to replacement for running job");
	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar.conf");

	f = fopen (filename, "w");
	fprintf (f, "respawn\n");
	fprintf (f, "script\n");
	fprintf (f, "  sleep 10\n");
	fprintf (f, "end script\n");
	fclose (f);

	TEST_FREE_TAG (old_file);
	TEST_FREE_TAG (old_job);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	TEST_FREE (old_file);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_NE_P (file->job, NULL);

	TEST_FREE (old_job);

	job = (JobClass *)nih_hash_lookup (job_classes, "frodo/bar");
	TEST_NE_P (job, file->job);

	TEST_TRUE (file->job->respawn);
	TEST_NE_P (file->job->process[PROCESS_MAIN], NULL);
	TEST_EQ (file->job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (file->job->process[PROCESS_MAIN]->command, "sleep 10\n");
	TEST_FALSE (file->job->deleted);

	TEST_TRUE (job->deleted);
	TEST_HASH_NOT_EMPTY (job->instances);
	instance = (Job *)nih_hash_lookup (job->instances, "");

	TEST_EQ (instance->goal, JOB_START);
	TEST_EQ (instance->state, JOB_RUNNING);

	old_file = file;
	old_job = file->job;


	/* Check that if we delete a job that is a replacement for a running
	 * job, the replacement is lost and there is no replacement for the
	 * running job anymore.
	 */
	TEST_FEATURE ("with deletion of replacement for running job");
	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar.conf");

	unlink (filename);

	TEST_FREE_TAG (old_file);
	TEST_FREE_TAG (old_job);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	TEST_FREE (old_file);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);
	TEST_EQ_P (file, NULL);

	TEST_FREE (old_job);

	job = (JobClass *)nih_hash_lookup (job_classes, "frodo/bar");
	TEST_NE_P (job, NULL);

	TEST_TRUE (job->deleted);
	TEST_HASH_NOT_EMPTY (job->instances);
	instance = (Job *)nih_hash_lookup (job->instances, "");

	TEST_EQ (instance->goal, JOB_START);
	TEST_EQ (instance->state, JOB_RUNNING);

	nih_free (job);


	/* Check that if a running job is deleted, it is not immediately
	 * freed, and instead is marked to be when stopped.
	 */
	TEST_FEATURE ("with deletion of running job");
	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar.conf");

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

	job = (JobClass *)nih_hash_lookup (job_classes, "frodo/bar");
	TEST_NE_P (job, NULL);

	instance = job_new (job, "");
	instance->goal = JOB_START;
	instance->state = JOB_RUNNING;

	old_job = job;

	unlink (filename);

	TEST_FREE_TAG (old_job);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);
	TEST_EQ_P (file, NULL);

	TEST_NOT_FREE (old_job);

	job = (JobClass *)nih_hash_lookup (job_classes, "frodo/bar");
	TEST_EQ_P (job, old_job);
	TEST_TRUE (job->deleted);

	TEST_HASH_NOT_EMPTY (job->instances);
	instance = (Job *)nih_hash_lookup (job->instances, "");

	TEST_EQ (instance->goal, JOB_START);
	TEST_EQ (instance->state, JOB_RUNNING);

	nih_free (job);


	/* Check that a physical error when re-parsing a job is caught
	 * and the file is lost.
	 */
	TEST_FEATURE ("with error after modification of job");
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

	chmod (filename, 0200);

	f = fopen (filename, "w");
	fprintf (f, "exec /bin/tool\n");
	fprintf (f, "respawn\n");
	fclose (f);

	old_job = (JobClass *)nih_hash_lookup (job_classes, "foo");

	TEST_FREE_TAG (old_job);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);
	TEST_EQ_P (file, NULL);

	TEST_FREE (old_job);

	job = (JobClass *)nih_hash_lookup (job_classes, "foo");
	TEST_EQ_P (job, NULL);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

	chmod (filename, 0644);


	/* Check that a parse error when re-parsing a job is caught
	 * and the file lost.
	 */
	TEST_FEATURE ("with parse error after modification of job");
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

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

	old_file = (ConfFile *)nih_hash_lookup (source->files, filename);
	TEST_FREE_TAG (old_file);

	old_job = (JobClass *)nih_hash_lookup (job_classes, "foo");
	TEST_FREE_TAG (old_job);

	f = fopen (filename, "w");
	fprintf (f, "respin\n");
	fclose (f);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	TEST_FREE (old_file);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->job, NULL);

	TEST_FREE (old_job);

	job = (JobClass *)nih_hash_lookup (job_classes, "foo");
	TEST_EQ_P (job, NULL);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");


	nih_free (source);


	/* Check that a physical error parsing a file initially is caught,
	 * and doesn't affect later jobs.
	 */
	TEST_FEATURE ("with error parsing job");
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

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
	strcat (filename, "/foo.conf");

	file = (ConfFile *)nih_hash_lookup (source->files, filename);
	TEST_EQ_P (file, NULL);


	strcpy (filename, dirname);
	strcat (filename, "/bar.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_NE_P (file, NULL);

	job = (JobClass *)nih_hash_lookup (job_classes, "bar");
	TEST_EQ_P (file->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	nih_free (file);


	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_NE_P (file->job, NULL);

	job = (JobClass *)nih_hash_lookup (job_classes, "frodo/foo");
	TEST_EQ_P (file->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "/bin/tool");

	nih_free (file);

	TEST_HASH_EMPTY (source->files);

	nih_free (source);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");
	chmod (filename, 0644);


	/* Check that a parsing error with a file is ignored and doesn't
	 * affect later jobs.
	 */
	TEST_FEATURE ("with job parse error");
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

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
	strcat (filename, "/foo.conf");

	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->job, NULL);

	nih_free (file);


	strcpy (filename, dirname);
	strcat (filename, "/bar.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_NE_P (file->job, NULL);

	job = (JobClass *)nih_hash_lookup (job_classes, "bar");
	TEST_EQ_P (file->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	nih_free (file);


	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_NE_P (file->job, NULL);

	job = (JobClass *)nih_hash_lookup (job_classes, "frodo/foo");
	TEST_EQ_P (file->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "/bin/tool");

	nih_free (file);

	TEST_HASH_EMPTY (source->files);

	nih_free (source);


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

	nih_free (source);


	/* Check that we can catch the deletion of the top-level directory,
	 * which results in the files and directories themselves being
	 * deleted and the watch being removed from the source structure.
	 */
	TEST_FEATURE ("with deletion of top-level directory");
	source = conf_source_new (NULL, dirname, CONF_JOB_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo.conf");
	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/frodo");
	rmdir (filename);

	strcpy (filename, dirname);
	strcat (filename, "/bar.conf");
	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");
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

	job = (JobClass *)nih_hash_lookup (job_classes, "foo");
	TEST_EQ_P (job, NULL);

	job = (JobClass *)nih_hash_lookup (job_classes, "bar");
	TEST_EQ_P (job, NULL);

	job = (JobClass *)nih_hash_lookup (job_classes, "frodo/foo");
	TEST_EQ_P (job, NULL);

	nih_free (source);


	/* Check that a file without the ".conf" extension is ignored.
	 */
	TEST_FEATURE ("without .conf extension only");

	TEST_FILENAME (dirname);
	mkdir (dirname, 0755);

	source = conf_source_new (NULL, dirname, CONF_JOB_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_HASH_EMPTY (source->files);
	TEST_HASH_EMPTY (job_classes);

	strcpy (filename, dirname);
	strcat (filename, "/munchkin");

	f = fopen (filename, "w");
	fprintf (f, "exec echo\n");
	fclose (f);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	TEST_HASH_EMPTY (source->files);
	TEST_HASH_EMPTY (job_classes);

	nih_free (source);

	unlink (filename);
	rmdir (dirname);


	/* Check that a file named just ".conf" is ignored.
	 */
	TEST_FEATURE ("with literal .conf file");

	TEST_FILENAME (dirname);
	mkdir (dirname, 0755);

	source = conf_source_new (NULL, dirname, CONF_JOB_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_HASH_EMPTY (source->files);
	TEST_HASH_EMPTY (job_classes);

	strcpy (filename, dirname);
	strcat (filename, "/.conf");

	f = fopen (filename, "w");
	fprintf (f, "exec echo\n");
	fclose (f);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	TEST_HASH_EMPTY (source->files);
	TEST_HASH_EMPTY (job_classes);

	nih_free (source);

	unlink (filename);
	rmdir (dirname);


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
	strcat (filename, "/foo.conf");

	f = fopen (filename, "w");
	fprintf (f, "exec /sbin/daemon\n");
	fprintf (f, "respawn\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/bar.conf");

	f = fopen (filename, "w");
	fprintf (f, "script\n");
	fprintf (f, "  echo\n");
	fprintf (f, "end script\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/frodo");

	mkdir (filename, 0755);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo.conf");

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
	strcat (filename, "/foo.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_NE_P (file->job, NULL);

	job = (JobClass *)nih_hash_lookup (job_classes, "foo");
	TEST_EQ_P (file->job, job);

	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "/sbin/daemon");

	nih_free (file);


	strcpy (filename, dirname);
	strcat (filename, "/bar.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_NE_P (file->job, NULL);

	job = (JobClass *)nih_hash_lookup (job_classes, "bar");
	TEST_EQ_P (file->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	nih_free (file);


	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_NE_P (file->job, NULL);

	job = (JobClass *)nih_hash_lookup (job_classes, "frodo/foo");
	TEST_EQ_P (file->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "/bin/tool");

	nih_free (file);


	TEST_HASH_EMPTY (source->files);

	nih_free (source);


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
	strcat (filename, "/foo.conf");

	f = fopen (filename, "w");
	fprintf (f, "exec /sbin/daemon --foo\n");
	fprintf (f, "respawn\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo.conf");

	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar.conf");

	f = fopen (filename, "w");
	fprintf (f, "exec /bin/tool --foo\n");
	fclose (f);

	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, FALSE);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_NE_P (file->job, NULL);

	job = (JobClass *)nih_hash_lookup (job_classes, "foo");
	TEST_EQ_P (file->job, job);

	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command,
		     "/sbin/daemon --foo");

	nih_free (file);


	strcpy (filename, dirname);
	strcat (filename, "/bar.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_NE_P (file->job, NULL);

	job = (JobClass *)nih_hash_lookup (job_classes, "bar");
	TEST_EQ_P (file->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	nih_free (file);


	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);
	TEST_EQ_P (file, NULL);

	job = (JobClass *)nih_hash_lookup (job_classes, "frodo/foo");
	TEST_EQ_P (job, NULL);


	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_NE_P (file->job, NULL);

	job = (JobClass *)nih_hash_lookup (job_classes, "frodo/bar");
	TEST_EQ_P (file->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "/bin/tool --foo");

	nih_free (file);


	TEST_HASH_EMPTY (source->files);

	nih_free (source);


	/* Check that a physical error parsing a file initially is caught,
	 * and doesn't affect later jobs.
	 */
	TEST_FEATURE ("with error parsing job without inotify");
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

	chmod (filename, 0000);

	source = conf_source_new (NULL, dirname, CONF_JOB_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_EQ_P (file, NULL);


	strcpy (filename, dirname);
	strcat (filename, "/bar.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_NE_P (file->job, NULL);

	job = (JobClass *)nih_hash_lookup (job_classes, "bar");
	TEST_EQ_P (file->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	nih_free (file);


	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_NE_P (file->job, NULL);

	job = (JobClass *)nih_hash_lookup (job_classes, "frodo/bar");
	TEST_EQ_P (file->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "/bin/tool --foo");

	nih_free (file);

	TEST_HASH_EMPTY (source->files);

	nih_free (source);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");
	chmod (filename, 0644);


	/* Check that a parse error is ignored and doesn't affect later jobs.
	 * The file item should remain.
	 */
	TEST_FEATURE ("with job parse error without inotify");
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

	f = fopen (filename, "w");
	fprintf (f, "respin\n");
	fclose (f);

	source = conf_source_new (NULL, dirname, CONF_JOB_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);

	nih_free (file);


	strcpy (filename, dirname);
	strcat (filename, "/bar.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_NE_P (file->job, NULL);

	job = (JobClass *)nih_hash_lookup (job_classes, "bar");
	TEST_EQ_P (file->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	nih_free (file);


	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_NE_P (file->job, NULL);

	job = (JobClass *)nih_hash_lookup (job_classes, "frodo/bar");
	TEST_EQ_P (file->job, job);

	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "/bin/tool --foo");

	nih_free (file);

	TEST_HASH_EMPTY (source->files);

	nih_free (source);


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

	nih_free (source);


	/* Check that if we mandatory reload a non-existant directory, all
	 * files, items and jobs are deleted.
	 */
	TEST_FEATURE ("with reload of deleted directory");
	source = conf_source_new (NULL, dirname, CONF_JOB_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar.conf");
	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/frodo");
	rmdir (filename);

	strcpy (filename, dirname);
	strcat (filename, "/bar.conf");
	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");
	unlink (filename);

	rmdir (dirname);

	ret = conf_source_reload (source);

	TEST_LT (ret, 0);

	err = nih_error_get ();
	TEST_EQ (err->number, ENOENT);
	nih_free (err);

	TEST_EQ_P (source->watch, NULL);

	TEST_HASH_EMPTY (source->files);

	job = (JobClass *)nih_hash_lookup (job_classes, "foo");
	TEST_EQ (job, NULL);

	job = (JobClass *)nih_hash_lookup (job_classes, "bar");
	TEST_EQ (job, NULL);

	job = (JobClass *)nih_hash_lookup (job_classes, "frodo/foo");
	TEST_EQ (job, NULL);

	nih_free (source);


	/* Check that a file without the ".conf" extension is ignored
	 * when it exists at reload time.
	 */
	TEST_FEATURE ("without .conf extension only");

	TEST_FILENAME (dirname);
	mkdir (dirname, 0755);

	source = conf_source_new (NULL, dirname, CONF_JOB_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_HASH_EMPTY (source->files);
	TEST_HASH_EMPTY (job_classes);

	strcpy (filename, dirname);
	strcat (filename, "/munchkin");

	f = fopen (filename, "w");
	fprintf (f, "exec echo\n");
	fclose (f);

	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);

	TEST_HASH_EMPTY (source->files);
	TEST_HASH_EMPTY (job_classes);

	nih_free (source);

	unlink (filename);
	rmdir (dirname);


	/* Check that a file named just ".conf" is ignored when it exists
	 * at reload time.
	 */
	TEST_FEATURE ("with literal .conf file");

	TEST_FILENAME (dirname);
	mkdir (dirname, 0755);

	source = conf_source_new (NULL, dirname, CONF_JOB_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_HASH_EMPTY (source->files);
	TEST_HASH_EMPTY (job_classes);

	strcpy (filename, dirname);
	strcat (filename, "/.conf");

	f = fopen (filename, "w");
	fprintf (f, "exec echo\n");
	fclose (f);

	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);

	TEST_HASH_EMPTY (source->files);
	TEST_HASH_EMPTY (job_classes);

	nih_free (source);

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


void
test_source_reload_conf_dir (void)
{
	ConfSource *source;
	ConfFile   *file, *old_file;
	FILE       *f;
	int         ret, fd[4096], i = 0, nfds;
	char        dirname[PATH_MAX];
	char        filename[PATH_MAX];
	fd_set      readfds, writefds, exceptfds;
	NihError   *err;

	TEST_FUNCTION_FEATURE ("conf_source_reload",
			       "with config directory");
	program_name = "test";
	nih_log_set_priority (NIH_LOG_FATAL);

	TEST_FILENAME (dirname);
	mkdir (dirname, 0755);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

	f = fopen (filename, "w");
	fprintf (f, "#nothing to test\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/bar.conf");

	f = fopen (filename, "w");
	fprintf (f, "#nothing to test\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/frodo");

	mkdir (filename, 0755);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo.conf");

	f = fopen (filename, "w");
	fprintf (f, "#nothing to test\n");
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
	strcat (filename, "/foo.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->data, NULL);

	nih_free (file);


	strcpy (filename, dirname);
	strcat (filename, "/bar.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->data, NULL);

	nih_free (file);


	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->data, NULL);

	nih_free (file);

	TEST_HASH_EMPTY (source->files);

	nih_free (source);


	/* Check that if we create a new file in the directory, it will
	 * be automatically parsed and loaded.
	 */
	TEST_FEATURE ("with new file in directory");
	source = conf_source_new (NULL, dirname, CONF_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar.conf");

	f = fopen (filename, "w");
	fprintf (f, "#nothing to test\n");
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
	TEST_EQ_P (file->data, NULL);

	old_file = file;


	/* Check that a file in the directory we're watching can be modified;
	 * it should be parsed and the previous items marked for deletion.
	 */
	TEST_FEATURE ("with modified file in directory");
	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar.conf");

	f = fopen (filename, "w");
	fprintf (f, "#still nothing to test\n");
	fclose (f);

	TEST_FREE_TAG (old_file);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	TEST_FREE (old_file);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->data, NULL);

	old_file = file;


	/* Check that we can delete a file from the directory, the metadata
	 * for it should be lost and the items should be queued for deletion.
	 */
	TEST_FEATURE ("with deleted file");
	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar.conf");

	unlink (filename);

	TEST_FREE_TAG (old_file);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	TEST_FREE (old_file);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_EQ_P (file, NULL);


	/* Check that a physical error when re-parsing a file is caught,
	 * and that the file is lost.
	 */
	TEST_FEATURE ("with error after modification of file");
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

	f = fopen (filename, "w");
	fprintf (f, "#nothing to test\n");
	fclose (f);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	old_file = (ConfFile *)nih_hash_lookup (source->files, filename);
	TEST_FREE_TAG (old_file);

	chmod (filename, 0200);

	f = fopen (filename, "w");
	fprintf (f, "#still nothing to test\n");
	fclose (f);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	TEST_FREE (old_file);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);
	TEST_EQ_P (file, NULL);

	chmod (filename, 0644);


	/* Check that a parse error when re-parsing a file is caught,
	 * and that the file is lost.
	 */
	TEST_FEATURE ("with parse error after modification of file");
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

	f = fopen (filename, "w");
	fprintf (f, "#nothing to test\n");
	fclose (f);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	old_file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_FREE_TAG (old_file);

	f = fopen (filename, "w");
	fprintf (f, "oops\n");
	fclose (f);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	TEST_FREE (old_file);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->data, NULL);

	nih_free (file);

	nih_free (source);


	/* Check that a physical error parsing a file initially is caught,
	 * and doesn't affect later files.
	 */
	TEST_FEATURE ("with error parsing file");
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

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
	strcat (filename, "/foo.conf");

	file = (ConfFile *)nih_hash_lookup (source->files, filename);
	TEST_EQ_P (file, NULL);


	strcpy (filename, dirname);
	strcat (filename, "/bar.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->data, NULL);

	nih_free (file);


	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->data, NULL);

	nih_free (file);

	TEST_HASH_EMPTY (source->files);

	nih_free (source);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");
	chmod (filename, 0644);


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

	nih_free (source);


	/* Check that we can catch the deletion of the top-level directory,
	 * which results in the files and directories themselves being
	 * deleted and the watch being removed from the source structure.
	 */
	TEST_FEATURE ("with deletion of top-level directory");
	source = conf_source_new (NULL, dirname, CONF_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo.conf");
	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/frodo");
	rmdir (filename);

	strcpy (filename, dirname);
	strcat (filename, "/bar.conf");
	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");
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

	nih_free (source);


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
	strcat (filename, "/foo.conf");

	f = fopen (filename, "w");
	fprintf (f, "#nothing to test\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/bar.conf");

	f = fopen (filename, "w");
	fprintf (f, "#nothing to test\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/frodo");

	mkdir (filename, 0755);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo.conf");

	f = fopen (filename, "w");
	fprintf (f, "#nothing to test\n");
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
	strcat (filename, "/foo.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->data, NULL);

	nih_free (file);


	strcpy (filename, dirname);
	strcat (filename, "/bar.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->data, NULL);

	nih_free (file);


	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->data, NULL);

	nih_free (file);


	TEST_HASH_EMPTY (source->files);

	nih_free (source);


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
	strcat (filename, "/foo.conf");

	f = fopen (filename, "w");
	fprintf (f, "#nothing to test\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo.conf");

	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar.conf");

	f = fopen (filename, "w");
	fprintf (f, "#nothing to test\n");
	fclose (f);

	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, FALSE);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->data, NULL);

	nih_free (file);


	strcpy (filename, dirname);
	strcat (filename, "/bar.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->data, NULL);

	nih_free (file);


	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);
	TEST_EQ_P (file, NULL);


	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->data, NULL);

	nih_free (file);


	TEST_HASH_EMPTY (source->files);

	nih_free (source);


	/* Check that a physical error parsing a file initially is caught,
	 * and doesn't affect later items.
	 */
	TEST_FEATURE ("with error parsing file without inotify");
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

	chmod (filename, 0000);

	source = conf_source_new (NULL, dirname, CONF_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);
	TEST_EQ_P (file, NULL);


	strcpy (filename, dirname);
	strcat (filename, "/bar.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->data, NULL);

	nih_free (file);


	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->data, NULL);

	nih_free (file);

	TEST_HASH_EMPTY (source->files);

	nih_free (source);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");
	chmod (filename, 0644);


	/* Check that a parse error is ignored and doesn't affect later items.
	 * The file item should remain.
	 */
	TEST_FEATURE ("with parse error without inotify");
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

	f = fopen (filename, "w");
	fprintf (f, "oops\n");
	fclose (f);

	source = conf_source_new (NULL, dirname, CONF_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->data, NULL);

	nih_free (file);


	strcpy (filename, dirname);
	strcat (filename, "/bar.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->data, NULL);

	nih_free (file);


	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->data, NULL);

	nih_free (file);

	TEST_HASH_EMPTY (source->files);

	nih_free (source);


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

	nih_free (source);


	/* Check that if we mandatory reload a non-existant directory, all
	 * files, items and jobs are deleted.
	 */
	TEST_FEATURE ("with reload of deleted directory");
	source = conf_source_new (NULL, dirname, CONF_DIR);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar.conf");
	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/frodo");
	rmdir (filename);

	strcpy (filename, dirname);
	strcat (filename, "/bar.conf");
	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");
	unlink (filename);

	rmdir (dirname);

	ret = conf_source_reload (source);

	TEST_LT (ret, 0);

	err = nih_error_get ();
	TEST_EQ (err->number, ENOENT);
	nih_free (err);

	TEST_EQ_P (source->watch, NULL);

	TEST_HASH_EMPTY (source->files);

	nih_free (source);


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
	ConfFile   *file, *old_file;
	FILE       *f;
	int         ret, fd[4096], i = 0, nfds;
	char        dirname[PATH_MAX];
	char        tmpname[PATH_MAX], filename[PATH_MAX];
	fd_set      readfds, writefds, exceptfds;
	NihError   *err;

	TEST_FUNCTION_FEATURE ("conf_source_reload",
			       "with configuration file");
	program_name = "test";
	nih_log_set_priority (NIH_LOG_FATAL);

	TEST_FILENAME (dirname);
	mkdir (dirname, 0755);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

	f = fopen (filename, "w");
	fprintf (f, "#nothing to test\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/bar.conf");

	f = fopen (filename, "w");
	fprintf (f, "#nothing to test\n");
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
	strcat (filename, "/foo.conf");

	source = conf_source_new (NULL, filename, CONF_FILE);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_ALLOC_SIZE (source->watch, sizeof (NihWatch));
	TEST_EQ_STR (source->watch->path, dirname);
	TEST_EQ_P (source->watch->data, source);

	TEST_TRUE (fcntl (source->watch->fd, F_GETFD) & FD_CLOEXEC);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->data, NULL);

	nih_free (file);


	strcpy (filename, dirname);
	strcat (filename, "/bar.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_EQ_P (file, NULL);

	TEST_HASH_EMPTY (source->files);

	nih_free (source);


	/* Check that if we create a new file in the directory, alongside
	 * the one we are watching, it is ignored.
	 */
	TEST_FEATURE ("with new file alongside conf file");
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

	source = conf_source_new (NULL, filename, CONF_FILE);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);

	strcpy (filename, dirname);
	strcat (filename, "/baz.conf");

	f = fopen (filename, "w");
	fprintf (f, "#nothing to test\n");
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
	strcat (filename, "/bar.conf");

	f = fopen (filename, "w");
	fprintf (f, "#nothing to test\n");
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
	 * using the direct writing technique.
	 */
	TEST_FEATURE ("with modification (direct write)");
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

	old_file = (ConfFile *)nih_hash_lookup (source->files, filename);
	TEST_FREE_TAG (old_file);

	f = fopen (filename, "w");
	fprintf (f, "#still nothing to test\n");
	fclose (f);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	TEST_FREE (old_file);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->data, NULL);

	old_file = file;


	/* Check that the configuration file we're watching can be modified
	 * using the write and then rename technique; it should be parsed
	 * and the previous file marked for deletion.
	 */
	TEST_FEATURE ("with modification (atomic rename)");
	strcpy (tmpname, dirname);
	strcat (tmpname, "/.foo.tmp");

	f = fopen (tmpname, "w");
	fprintf (f, "#nothing to test\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

	rename (tmpname, filename);

	TEST_FREE_TAG (old_file);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	TEST_FREE (old_file);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->data, NULL);

	old_file = file;


	/* Check that we can delete the configuration file that we're
	 * watching, the metadata for it should be lost.
	 */
	TEST_FEATURE ("with deletion");
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

	unlink (filename);

	TEST_FREE_TAG (old_file);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	TEST_FREE (old_file);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_EQ_P (file, NULL);


	/* Check that the watch allows us to see if the file we want is
	 * created using the direct writing technique, and thus parsed.
	 */
	TEST_FEATURE ("with creation (direct write)");
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

	f = fopen (filename, "w");
	fprintf (f, "#nothing to test\n");
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
	TEST_EQ_P (file->data, NULL);

	nih_free (file);


	/* Check that the watch allows us to see if the file we want is
	 * created using the write and rename, and thus parsed.
	 */
	TEST_FEATURE ("with creation (atomic rename)");
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

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
	fprintf (f, "#nothing to test\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

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
	TEST_EQ_P (file->data, NULL);

	old_file = file;


	/* Check that a physical error when re-parsing a job is caught.
	 */
	TEST_FEATURE ("with error after modification");
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

	chmod (filename, 0200);

	TEST_FREE_TAG (old_file);

	f = fopen (filename, "w");
	fprintf (f, "#still nothing to test\n");
	fclose (f);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	TEST_FREE (old_file);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);
	TEST_EQ_P (file, NULL);

	chmod (filename, 0644);


	/* Check that a parse error when re-parsing a file is caught.
	 */
	TEST_FEATURE ("with parse error after modification");
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

	f = fopen (filename, "w");
	fprintf (f, "#nothing to test\n");
	fclose (f);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	old_file = (ConfFile *)nih_hash_lookup (source->files, filename);
	TEST_FREE_TAG (old_file);

	f = fopen (filename, "w");
	fprintf (f, "oops\n");
	fclose (f);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	TEST_FREE (old_file);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->data, NULL);

	nih_free (file);

	nih_free (source);


	/* Check that a physical error parsing a file initially is caught
	 * and returned as an error.
	 */
	TEST_FEATURE ("with physical error parsing file");
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

	f = fopen (filename, "w");
	fprintf (f, "#nothing to test\n");
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
	strcat (filename, "/foo.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);
	TEST_EQ_P (file, NULL);

	TEST_HASH_EMPTY (source->files);

	nih_free (source);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");
	chmod (filename, 0644);


	/* Check that a parsing error with a file doesn't return an error
	 * code, since that's no different to an empty file.
	 */
	TEST_FEATURE ("with parse error");
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

	f = fopen (filename, "w");
	fprintf (f, "oops\n");
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
	strcat (filename, "/foo.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->data, NULL);

	nih_free (file);

	TEST_HASH_EMPTY (source->files);

	nih_free (source);


	/* Check that we catch errors attempting to parse a file that
	 * doesn't exist, however we should have a watch on it, so that if
	 * it is created in the future it will be automatically parsed.
	 */
	TEST_FEATURE ("with non-existant file");
	strcpy (filename, dirname);
	strcat (filename, "/wibble.conf");

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
	TEST_EQ_P (file, NULL);

	TEST_HASH_EMPTY (source->files);

	nih_free (source);


	/* Check that we can catch the deletion of the parent directory,
	 * which results in the file being deleted and the watch removed.
	 */
	TEST_FEATURE ("with deletion of parent directory");
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

	source = conf_source_new (NULL, filename, CONF_FILE);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);

	strcpy (filename, dirname);
	strcat (filename, "/baz.conf");
	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/bar.conf");
	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");
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

	nih_free (source);


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
	strcat (filename, "/foo.conf");

	f = fopen (filename, "w");
	fprintf (f, "#nothing to test\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/bar.conf");

	f = fopen (filename, "w");
	fprintf (f, "#nothing to test\n");
	fclose (f);


	/* Check that we can load a conf file source for the first time.
	 * Even though we don't have inotify, the file should still be parsed.
	 */
	TEST_FEATURE ("with new conf file but no inotify");
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

	source = conf_source_new (NULL, filename, CONF_FILE);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->data, NULL);

	nih_free (file);

	TEST_HASH_EMPTY (source->files);

	nih_free (source);


	/* Check that we can perform a mandatory reload of the file,
	 * having made some changes in between.  Items that were added
	 * should be parsed and items that were removed should be deleted.
	 */
	TEST_FEATURE ("with reload of conf file");
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

	source = conf_source_new (NULL, filename, CONF_FILE);
	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, TRUE);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

	f = fopen (filename, "w");
	fprintf (f, "#nothing to test\n");
	fclose (f);

	ret = conf_source_reload (source);

	TEST_EQ (ret, 0);
	TEST_EQ_P (source->watch, NULL);

	TEST_EQ (source->flag, FALSE);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");
	file = (ConfFile *)nih_hash_lookup (source->files, filename);

	TEST_ALLOC_SIZE (file, sizeof (ConfFile));
	TEST_ALLOC_PARENT (file, source);
	TEST_EQ (file->flag, source->flag);
	TEST_EQ_P (file->data, NULL);

	nih_free (file);

	TEST_HASH_EMPTY (source->files);

	nih_free (source);


	/* Check that a physical error parsing a file initially is caught
	 * and returned.
	 */
	TEST_FEATURE ("with error parsing file without inotify");
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

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
	TEST_EQ_P (file, NULL);

	TEST_HASH_EMPTY (source->files);

	nih_free (source);

	chmod (filename, 0644);


	/* Check that a parse error is ignored and doesn't return an error,
	 * being treated equivalent to an empty file.
	 */
	TEST_FEATURE ("with parse error without inotify");
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

	f = fopen (filename, "w");
	fprintf (f, "oops\n");
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
	TEST_EQ_P (file->data, NULL);

	nih_free (file);

	TEST_HASH_EMPTY (source->files);

	nih_free (source);


	/* Check that trying to parse a file that doesn't exist returns
	 * an error.
	 */
	TEST_FEATURE ("with non-existant conf file and no inotify");
	strcpy (filename, dirname);
	strcat (filename, "/wibble.conf");

	source = conf_source_new (NULL, filename, CONF_FILE);
	ret = conf_source_reload (source);

	TEST_LT (ret, 0);

	err = nih_error_get ();
	TEST_EQ (err->number, ENOENT);
	nih_free (err);

	TEST_EQ_P (source->watch, NULL);

	file = (ConfFile *)nih_hash_lookup (source->files, filename);
	TEST_EQ_P (file, NULL);

	TEST_HASH_EMPTY (source->files);

	nih_free (source);


	/* Check that if we mandatory reload a non-existant file, all items
	 * are deleted.
	 */
	TEST_FEATURE ("with reload of deleted conf file");
	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");

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
	TEST_EQ_P (file, NULL);

	TEST_HASH_EMPTY (source->files);

	nih_free (source);


	strcpy (filename, dirname);
	strcat (filename, "/bar.conf");
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


void
test_source_reload (void)
{
	FILE       *f;
	ConfSource *source1, *source2, *source3;
	char        dirname[PATH_MAX], filename[PATH_MAX];

	/* Check that we can reload all sources, and that errors are warned
	 * about and not returned.
	 */
	TEST_FUNCTION ("conf_source_reload");
	nih_log_set_priority (NIH_LOG_FATAL);

	TEST_FILENAME (dirname);
	mkdir (dirname, 0755);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");
	source1 = conf_source_new (NULL, filename, CONF_FILE);

	f = fopen (filename, "w");
	fprintf (f, "job foo\n");
	fprintf (f, "  respawn\n");
	fprintf (f, "  exec /sbin/daemon\n");
	fprintf (f, "end job\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/bar");
	mkdir (filename, 0755);

	source2 = conf_source_new (NULL, filename, CONF_JOB_DIR);

	strcpy (filename, dirname);
	strcat (filename, "/bar/bar.conf");

	f = fopen (filename, "w");
	fprintf (f, "script\n");
	fprintf (f, "  echo\n");
	fprintf (f, "end script\n");
	fclose (f);

	strcpy (filename, dirname);
	strcat (filename, "/baz");
	source3 = conf_source_new (NULL, filename, CONF_DIR);

	conf_reload ();

	TEST_HASH_NOT_EMPTY (source1->files);

	TEST_HASH_NOT_EMPTY (source2->files);

	TEST_HASH_EMPTY (source3->files);

	nih_free (source1);
	nih_free (source2);
	nih_free (source3);

	strcpy (filename, dirname);
	strcat (filename, "/foo.conf");
	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/bar/bar.conf");
	unlink (filename);

	strcpy (filename, dirname);
	strcat (filename, "/bar");
	rmdir (filename);

	rmdir (dirname);

	nih_log_set_priority (NIH_LOG_MESSAGE);
}


void
test_file_destroy (void)
{
	ConfSource *source;
	ConfFile   *file;
	JobClass   *job, *other, *ptr;
	Job        *instance;

	TEST_FUNCTION ("conf_file_destroy");
	source = conf_source_new (NULL, "/path", CONF_JOB_DIR);


	/* Check that when a ConfFile for a job is freed, the attached
	 * job is also freed if it is not the current job.
	 */
	TEST_FEATURE ("with not-current job");
	file = conf_file_new (source, "/path/to/file");
	job = file->job = job_class_new (NULL, "foo", NULL);

	other = job_class_new (NULL, "foo", NULL);
	nih_hash_add (job_classes, &other->entry);

	TEST_FREE_TAG (job);

	nih_free (file);

	TEST_FREE (job);

	ptr = (JobClass *)nih_hash_lookup (job_classes, "foo");
	TEST_EQ_P (ptr, other);

	nih_free (other);


	/* Check that when a ConfFile for a job is freed, the attached
	 * job is also freed if it is the current job but is not running.
	 */
	TEST_FEATURE ("with stopped job");
	file = conf_file_new (source, "/path/to/file");
	job = file->job = job_class_new (NULL, "foo", NULL);

	nih_hash_add (job_classes, &job->entry);

	TEST_FREE_TAG (job);

	nih_free (file);

	TEST_FREE (job);

	ptr = (JobClass *)nih_hash_lookup (job_classes, "foo");
	TEST_EQ_P (ptr, NULL);


	/* Check that when a ConfFile for a job is freed, the attached job
	 * is not freed if it is the current job and has active instances;
	 * instead it is marked as deleted so job_change_state() can free it.
	 */
	TEST_FEATURE ("with running job");
	file = conf_file_new (source, "/path/to/file");
	job = file->job = job_class_new (NULL, "foo", NULL);

	nih_hash_add (job_classes, &job->entry);

	TEST_FREE_TAG (job);

	instance = job_new (job, "");
	instance->goal = JOB_START;
	instance->state = JOB_RUNNING;

	nih_free (file);

	TEST_NOT_FREE (job);

	TEST_TRUE (job->deleted);
	TEST_EQ (instance->goal, JOB_START);
	TEST_EQ (instance->state, JOB_RUNNING);

	ptr = (JobClass *)nih_hash_lookup (job_classes, "foo");
	TEST_EQ_P (ptr, job);

	nih_free (job);

	nih_free (source);
}


void
test_select_job (void)
{
	ConfSource *source1, *source2, *source3;
	ConfFile   *file1, *file2, *file3, *file4, *file5;
	JobClass   *class1, *class2, *class3, *class4, *ptr;

	TEST_FUNCTION ("conf_select_job");
	source1 = conf_source_new (NULL, "/tmp/foo", CONF_DIR);

	source2 = conf_source_new (NULL, "/tmp/bar", CONF_JOB_DIR);

	file1 = conf_file_new (source2, "/tmp/bar/frodo");
	class1 = file1->job = job_class_new (NULL, "frodo", NULL);

	file2 = conf_file_new (source2, "/tmp/bar/bilbo");

	file3 = conf_file_new (source2, "/tmp/bar/drogo");
	class2 = file3->job = job_class_new (NULL, "drogo", NULL);

	source3 = conf_source_new (NULL, "/tmp/baz", CONF_JOB_DIR);

	file4 = conf_file_new (source3, "/tmp/baz/frodo");
	class3 = file4->job = job_class_new (NULL, "frodo", NULL);

	file5 = conf_file_new (source2, "/tmp/bar/bilbo");
	class4 = file5->job = job_class_new (NULL, "bilbo", NULL);


	/* Check that a job with only one file is returned.
	 */
	TEST_FEATURE ("with one file");
	ptr = conf_select_job ("drogo", NULL);

	TEST_EQ_P (ptr, class2);


	/* Check that the first job of multiple available files is
	 * returned.
	 */
	TEST_FEATURE ("with multiple files");
	ptr = conf_select_job ("frodo", NULL);

	TEST_EQ_P (ptr, class1);


	/* Check that files with no attached job are ignored.
	 */
	TEST_FEATURE ("with file but no attached job");
	ptr = conf_select_job ("bilbo", NULL);

	TEST_EQ_P (ptr, class4);


	/* Check that when there is no match, NULL is returned.
	 */
	TEST_FEATURE ("with no match");
	ptr = conf_select_job ("meep", NULL);

	TEST_EQ_P (ptr, NULL);


	nih_free (source3);
	nih_free (source2);
	nih_free (source1);
}


int
main (int   argc,
      char *argv[])
{
	/* run tests in legacy (pre-session support) mode */
	setenv ("UPSTART_NO_SESSIONS", "1", 1);

	test_source_new ();
	test_file_new ();
	test_source_reload_job_dir ();
	test_source_reload_conf_dir ();
	test_source_reload_file ();
	test_source_reload ();
	test_file_destroy ();
	test_select_job ();

	return 0;
}
