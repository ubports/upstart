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

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/hash.h>

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
	source->flag = TRUE;

	TEST_ALLOC_FAIL {
		file = conf_file_new (NULL, source, "/tmp/foo");

		if (test_alloc_failed) {
			TEST_EQ_P (file, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (file, sizeof (ConfFile));
		TEST_LIST_NOT_EMPTY (&file->entry);
		TEST_ALLOC_PARENT (file->path, file);
		TEST_EQ_STR (file->path, "/tmp/foo");
		TEST_EQ (file->flag, TRUE);
		TEST_NE_P (file->items, NULL);

		TEST_EQ_P ((void *)nih_hash_lookup (source->files, "/tmp/foo"),
			   file);

		nih_list_free (&file->entry);
	}

	nih_list_free (&source->entry);
}

void
test_item_new (void)
{
	ConfSource *source;
	ConfFile   *file;
	ConfItem   *item;

	/* Check that we can request a new Confitem structure, it should be
	 * allocated with nih_alloc and placed into the items hash table of
	 * the file, with the flag copied.
	 */
	TEST_FUNCTION ("conf_item_new");
	source = conf_source_new (NULL, "/tmp", CONF_JOB_DIR);
	source->flag = TRUE;

	file = conf_file_new (NULL, source, "/tmp/foo");

	TEST_ALLOC_FAIL {
		item = conf_item_new (NULL, file, "foo");

		if (test_alloc_failed) {
			TEST_EQ_P (item, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (item, sizeof (ConfItem));
		TEST_LIST_NOT_EMPTY (&item->entry);
		TEST_ALLOC_PARENT (item->name, item);
		TEST_EQ_STR (item->name, "foo");
		TEST_EQ (item->flag, TRUE);
		TEST_EQ_P (item->data, NULL);

		TEST_EQ_P ((void *)nih_hash_lookup (file->items, "foo"),
			   item);

		nih_list_free (&item->entry);
	}

	nih_list_free (&file->entry);
	nih_list_free (&source->entry);
}


int
main (int   argc,
      char *argv[])
{
	test_source_new ();
	test_file_new ();
	test_item_new ();

	return 0;
}
