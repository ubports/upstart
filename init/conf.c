/* upstart
 *
 * conf.c - configuration management
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/hash.h>
#include <nih/string.h>
#include <nih/logging.h>

#include "conf.h"


/**
 * conf_sources:
 *
 * This hash table holds the list of known sources of configurations,
 * indexed by their path.  Each entry is a ConfSource structure, multiple
 * entries for the same path may not exist.
 **/
NihHash *conf_sources = NULL;


/**
 * conf_init:
 *
 * Initialise the conf_sources hash table.
 **/
void
conf_init (void)
{
	if (! conf_sources)
		NIH_MUST (conf_sources = nih_hash_new (NULL, 0,
						       (NihKeyFunction)nih_hash_string_key));
}


/**
 * conf_source_new:
 * @parent: parent of new block,
 * @path: path to source,
 * @type: type of source.
 *
 * Allocates and returns a new ConfSource structure for the given @path;
 * @type indicates whether this @path is a file or directory and what type
 * of files are within the directory.
 *
 * The returned structure is automatically placed in the conf_sources hash
 * table, indexed by @path.
 *
 * Configuration is not parsed immediately, instead you must call
 * conf_reload() to set up any watches and load the configuration.  Normally
 * this is called once after setting up all sources.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * Returns: newly allocated ConfSource structure or NULL if
 * insufficient memory.
 **/
ConfSource *
conf_source_new (const void     *parent,
		 const char     *path,
		 ConfSourceType  type)
{
	ConfSource *source;

	nih_assert (path != NULL);

	source = nih_new (parent, ConfSource);
	if (! source)
		return NULL;

	nih_list_init (&source->entry);

	source->path = nih_strdup (source, path);
	if (! source->path) {
		nih_free (source);
		return NULL;
	}

	source->type = type;

	source->watch = NULL;

	source->flag = FALSE;
	source->files = nih_hash_new (source, 0,
				      (NihKeyFunction)nih_hash_string_key);
	if (! source->files) {
		nih_free (source);
		return NULL;
	}

	nih_hash_add (conf_sources, &source->entry);

	return source;
}

/**
 * conf_file_new:
 * @parent: parent of new block,
 * @source: source to attach to,
 * @path: path to file.
 *
 * Allocates and returns a new ConfFile structure for the given @path and
 * places it in the files hash table of the given @source.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * Returns: newly allocated ConfFile structure or NULL if insufficient memory.
 **/
ConfFile *
conf_file_new (const void *parent,
	       ConfSource *source,
	       const char *path)
{
	ConfFile *file;

	nih_assert (source != NULL);
	nih_assert (path != NULL);

	file = nih_new (parent, ConfFile);
	if (! file)
		return NULL;

	nih_list_init (&file->entry);

	file->path = nih_strdup (file, path);
	if (! file->path) {
		nih_free (file);
		return NULL;
	}

	file->flag = source->flag;
	file->items = nih_hash_new (source, 0,
				    (NihKeyFunction)nih_hash_string_key);
	if (! file->items) {
		nih_free (file);
		return NULL;
	}

	nih_hash_add (source->files, &file->entry);

	return file;
}

/**
 * conf_item_new:
 * @parent: parent of new block,
 * @file: file to attach to,
 * @name: name of item.
 *
 * Allocates and returns a new ConfItem structure for the given @name and
 * places it in the items hash table of the given @file.  It is up to the
 * caller to set the pointer to the actual item.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * Returns: newly allocated ConfItem structure or NULL if insufficient memory.
 **/
ConfItem *
conf_item_new (const void *parent,
	       ConfFile   *file,
	       const char *name)
{
	ConfItem *item;

	nih_assert (file != NULL);
	nih_assert (name != NULL);

	item = nih_new (parent, ConfItem);
	if (! item)
		return NULL;

	nih_list_init (&item->entry);

	item->name = nih_strdup (item, name);
	if (! item->name) {
		nih_free (item);
		return NULL;
	}

	item->flag = file->flag;
	item->data = NULL;

	nih_hash_add (file->items, &item->entry);

	return item;
}
