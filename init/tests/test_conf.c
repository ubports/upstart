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

#include <fcntl.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/hash.h>
#include <nih/watch.h>
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
	ConfFile   *conf_file, *ptr;

	TEST_FUNCTION ("conf_file_get");
	source = conf_source_new (NULL, "/tmp", CONF_JOB_DIR);

	/* Check that we can request a new ConfFile structure, it should be
	 * allocated with nih_alloc and placed into the files hash table of
	 * the source, with the flag copied.
	 */
	TEST_FEATURE ("with new path");
	TEST_ALLOC_FAIL {
		conf_file = conf_file_get (source, "/tmp/foo");

		if (test_alloc_failed) {
			TEST_EQ_P (conf_file, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (conf_file, sizeof (ConfFile));
		TEST_ALLOC_PARENT (conf_file, source);
		TEST_LIST_NOT_EMPTY (&conf_file->entry);
		TEST_ALLOC_PARENT (conf_file->path, conf_file);
		TEST_EQ_STR (conf_file->path, "/tmp/foo");
		TEST_EQ (conf_file->flag, source->flag);
		TEST_NE_P (conf_file->items, NULL);

		TEST_EQ_P ((void *)nih_hash_lookup (source->files, "/tmp/foo"),
			   conf_file);

		nih_list_free (&conf_file->entry);
	}


	/* Check that we can retrieve an existing ConfFile entry for a
	 * source, and that the flag is updated to the new value.
	 */
	TEST_FEATURE ("with existing path");
	conf_file = conf_file_get (source, "/tmp/foo");
	source->flag = (! source->flag);

	TEST_ALLOC_FAIL {
		ptr = conf_file_get (source, "/tmp/foo");

		if (test_alloc_failed) {
			TEST_EQ_P (ptr, NULL);
			continue;
		}

		TEST_EQ_P (ptr, conf_file);

		TEST_ALLOC_SIZE (conf_file, sizeof (ConfFile));
		TEST_ALLOC_PARENT (conf_file, source);
		TEST_LIST_NOT_EMPTY (&conf_file->entry);
		TEST_ALLOC_PARENT (conf_file->path, conf_file);
		TEST_EQ_STR (conf_file->path, "/tmp/foo");
		TEST_EQ (conf_file->flag, source->flag);
		TEST_NE_P (conf_file->items, NULL);

		TEST_EQ_P ((void *)nih_hash_lookup (source->files, "/tmp/foo"),
			   conf_file);
	}

	nih_list_free (&conf_file->entry);
	nih_list_free (&source->entry);
}

void
test_item_set (void)
{
	ConfSource *source;
	ConfFile   *conf_file;
	ConfItem   *item, *ptr;
	Job        *job1, *job2;

	TEST_FUNCTION ("conf_item_set");
	source = conf_source_new (NULL, "/tmp", CONF_JOB_DIR);
	conf_file = conf_file_get (source, "/tmp/foo");

	/* Check that we can request a new Confitem structure, it should be
	 * allocated with nih_alloc and placed into the items hash table of
	 * the file, with the flag copied.
	 */
	TEST_FEATURE ("with new item");
	job1 = job_new (NULL, "foo");

	TEST_ALLOC_FAIL {
		item = conf_item_set (source, conf_file, "foo", job1);

		if (test_alloc_failed) {
			TEST_EQ_P (item, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (item, sizeof (ConfItem));
		TEST_ALLOC_PARENT (item, conf_file);
		TEST_LIST_NOT_EMPTY (&item->entry);
		TEST_ALLOC_PARENT (item->name, item);
		TEST_EQ_STR (item->name, "foo");
		TEST_EQ (item->flag, conf_file->flag);
		TEST_EQ_P (item->data, job1);

		TEST_EQ_P ((void *)nih_hash_lookup (conf_file->items, "foo"),
			   item);

		nih_list_free (&item->entry);
	}

	/* Check that we can retrieve an existing ConfItem for a given name,
	 * and that the flag and data are both updated to the new value.
	 */
	TEST_FEATURE ("with existing item");
	item = conf_item_set (source, conf_file, "foo", job1);
	source->flag = (! source->flag);

	job2 = job_new (NULL, "foo");

	TEST_ALLOC_FAIL {
		ptr = conf_item_set (source, conf_file, "foo", job2);

		if (test_alloc_failed) {
			TEST_EQ_P (ptr, NULL);
			continue;
		}

		TEST_EQ_P (ptr, item);

		TEST_ALLOC_SIZE (item, sizeof (ConfItem));
		TEST_ALLOC_PARENT (item, conf_file);
		TEST_LIST_NOT_EMPTY (&item->entry);
		TEST_ALLOC_PARENT (item->name, item);
		TEST_EQ_STR (item->name, "foo");
		TEST_EQ (item->flag, conf_file->flag);
		TEST_EQ_P (item->data, job2);

		TEST_EQ_P ((void *)nih_hash_lookup (conf_file->items, "foo"),
			   item);
	}

	nih_list_free (&conf_file->entry);
	nih_list_free (&source->entry);
}


void
test_source_reload (void)
{
	ConfSource *source;
	FILE       *f;
	int         ret, fd[4096], i = 0;
	char        dirname[PATH_MAX], filename[PATH_MAX];

	TEST_FUNCTION ("conf_source_reload");
	TEST_FILENAME (dirname);
	mkdir (dirname, 0755);

	strcpy (filename, dirname);
	strcat (filename, "/foo");

	f = fopen (filename, "w");
	fprintf (f, "exec /sbin/daemon\n");
	fprintf (f, "respawn\n");
	fclose (f);

	/* Make sure that we have inotify before performing some tests... */
	if ((fd[0] = inotify_init ()) < 0) {
		printf ("SKIP: inotify not available\n");
		goto no_inotify;
	}
	close (fd[0]);


	/* Check that we can reload a new file source.  An inotify watch
	 * should be established on the parent directory, and its file
	 * descriptor set to be closed on exec.
	 */
	TEST_FEATURE ("with new file source");
	strcpy (filename, dirname);
	strcat (filename, "/foo");

	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			source = conf_source_new (NULL, filename, CONF_FILE);
		}

		ret = conf_source_reload (source);

		TEST_EQ (ret, 0);
		TEST_ALLOC_SIZE (source->watch, sizeof (NihWatch));
		TEST_EQ_STR (source->watch->path, dirname);
		TEST_EQ_P (source->watch->data, source);

		TEST_TRUE (fcntl (source->watch->fd, F_GETFD) & FD_CLOEXEC);

		nih_list_free (&source->entry);
	}


	/* Consume all available inotify instances so that the following
	 * tests run without inotify.
	 */
	for (i = 0; i < 4096; i++)
		if ((fd[i] = inotify_init ()) < 0)
			break;
no_inotify:


	strcpy (filename, dirname);
	strcat (filename, "/foo");
	unlink (filename);

	rmdir (dirname);
}


int
main (int   argc,
      char *argv[])
{
	test_source_new ();
	test_file_get ();
	test_item_set ();
	test_source_reload ();

	return 0;
}
