/* upstart
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

#ifndef INIT_CONF_H
#define INIT_CONF_H

#include <nih/macros.h>

#include <nih/hash.h>
#include <nih/list.h>
#include <nih/watch.h>

#include "job.h"


/**
 * ConfSourceType:
 *
 * We support various different types of configuration source, both solitary
 * files and directory trees.  Files within a directory tree may define any
 * single top-level type, or may be as top-level configuration files and name
 * what they define within themselves.
 **/
typedef enum conf_source_type {
	CONF_FILE,
	CONF_DIR,
	CONF_JOB_DIR,
} ConfSourceType;


/**
 * ConfSource:
 * @entry: list header,
 * @path: path to source,
 * @type: type of source,
 * @watch: NihWatch structure for automatic change notification,
 * @flag: reload flag,
 * @files; hash table of files.
 *
 * This structure represents a single source of configuration, which may be
 * a single file or a directory of files of various types, depending on @type.
 *
 * Normally inotify is used to watch the source for changes and load them
 * automatically, however mandatory reloading is also supported; for this
 * the @flag member is toggled, and copied to all files and items reloaded;
 * any that are in the old state are deleted.
 **/
typedef struct conf_source {
	NihList         entry;
	char           *path;
	ConfSourceType  type;

	NihWatch       *watch;

	int             flag;
	NihHash        *files;
} ConfSource;

/**
 * ConfFile:
 * @entry: list header,
 * @path: path to file,
 * @flag: reload flag,
 * @items: hash table of items.
 *
 * This structure represents a file within a source, either the file itself
 * or a single file in the directory tree.  These are tracked independantly
 * to make handling file deletion easier.
 *
 * The @flag member is used to support mandatory reloading; when the file is
 * created and parsed, it is set to the same value as the source's.  Then
 * the source can trivially see which files have been lost, since they have
 * the wrong flag value.  We use the same method for the @items hash.
 **/
typedef struct conf_file {
	NihList  entry;
	char    *path;

	int      flag;
	NihHash *items;
} ConfFile;

/**
 * ConfItem:
 * @entry: list header,
 * @name: item name,
 * @flag: reload flag,
 * @data: pointer to actual item.
 *
 * This structure represents an item defined within a configuration file,
 * the @data pointer points to the actual item structure itself.
 *
 * The @flag member is used to support mandatory reloading; when the item
 * is first created, or reparsed, it is set to the same value as the file's.
 * Then the file can trivially see which items defined within it have been
 * lost, since they have the wrong flag value.
 **/
typedef struct conf_item {
	NihList  entry;
	char    *name;

	int      flag;
	union {
		void *data;
		Job  *job;
	};
} ConfItem;


NIH_BEGIN_EXTERN

NihHash *conf_sources;

void        conf_init       (void);

ConfSource *conf_source_new (const void *parent, const char *path,
			     ConfSourceType type)
	__attribute__ ((warn_unused_result, malloc));
ConfFile *  conf_file_new   (const void *parent, ConfSource *source,
			     const char *path)
	__attribute__ ((warn_unused_result, malloc));
ConfItem *  conf_item_new   (const void *parent, ConfFile *file,
			     const char *name)
	__attribute__ ((warn_unused_result, malloc));

NIH_END_EXTERN

#endif /* INIT_CONF_H */
