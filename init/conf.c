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


#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <libgen.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/hash.h>
#include <nih/string.h>
#include <nih/io.h>
#include <nih/file.h>
#include <nih/watch.h>
#include <nih/logging.h>
#include <nih/error.h>
#include <nih/errors.h>

#include "parse_job.h"
#include "conf.h"
#include "errors.h"


/* Prototypes for static functions */
static int  conf_source_reload_file    (ConfSource *source)
	__attribute__ ((warn_unused_result));
static int  conf_source_reload_dir     (ConfSource *source)
	__attribute__ ((warn_unused_result));

static int  conf_file_filter           (ConfSource *source, const char *path);
static void conf_create_modify_handler (ConfSource *source, NihWatch *watch,
					const char *path,
					struct stat *statbuf);
static void conf_delete_handler        (ConfSource *source, NihWatch *watch,
					const char *path);
static int  conf_file_visitor          (ConfSource *source,
					const char *dirname, const char *path,
					struct stat *statbuf)
	__attribute__ ((warn_unused_result));

static int  conf_reload_path           (ConfSource *source, const char *path)
	__attribute__ ((warn_unused_result));


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
 * conf_source_reload() on this source to set up any watches and load the
 * current configuration.  Normally you would set up all of the sources and
 * then call conf_reload() which will load them all.
 *
 * Since a source has attached files, items and inotify watches, you should
 * use conf_source_free() to free it and not attempt to free it directly.
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
 * conf_file_get:
 * @source: configuration source,
 * @path: path to file.
 *
 * Looks up the ConfFile entry in @source for @path, or allocates a new
 * structure and places it in the files hash table before returning it.
 *
 * The flag of the returned ConfFile will be set to that of the @source.
 *
 * Returns: existing or newly allocated ConfFile structure,
 * or NULL if insufficient memory.
 **/
ConfFile *
conf_file_get (ConfSource *source,
	       const char *path)
{
	ConfFile *file;

	nih_assert (source != NULL);
	nih_assert (path != NULL);

	file = (ConfFile *)nih_hash_lookup (source->files, path);
	if (! file) {
		file = nih_new (source, ConfFile);
		if (! file)
			return NULL;

		nih_list_init (&file->entry);

		file->path = nih_strdup (file, path);
		if (! file->path) {
			nih_free (file);
			return NULL;
		}

		nih_list_init (&file->items);

		nih_hash_add (source->files, &file->entry);
	}

	file->flag = source->flag;

	return file;
}

/**
 * conf_item_new:
 * @source: configuration source,
 * @file: file to attach to,
 * @type: type of item.
 *
 * Allocates and returns a new ConfItem structure for the given @source
 * and @file, with @type indicating what kind of data will be attached
 * to this item.  Setting the data pointer is the job of the caller.
 *
 * The returned structure is automatically placed in the @file items list.
 *
 * Returns: newly allocated ConfItem structure or NULL if insufficient memory.
 **/
ConfItem *
conf_item_new (ConfSource   *source,
	       ConfFile     *file,
	       ConfItemType  type)
{
	ConfItem *item;

	nih_assert (source != NULL);
	nih_assert (file != NULL);

	item = nih_new (file, ConfItem);
	if (! item)
		return NULL;

	nih_list_init (&item->entry);

	item->type = type;
	item->data = NULL;

	nih_list_add (&file->items, &item->entry);

	return item;
}


/**
 * conf_reload:
 *
 * Reloads all configuration sources.
 *
 * Watches on new configuration sources are established so that future
 * changes will be automatically detected with inotify.  Then for both
 * new and existing sources, the current state is parsed.
 *
 * Any errors are logged through the usual mechanism, and not returned,
 * since some configuration may have been parsed; and it's possible to
 * parse no configuration without error.
 **/
void
conf_reload (void)
{
	conf_init ();

	NIH_HASH_FOREACH (conf_sources, iter) {
		ConfSource *source = (ConfSource *)iter;

		if (conf_source_reload (source) < 0) {
			NihError *err;

			err = nih_error_get ();
			nih_error ("%s: %s: %s", source->path,
				   _("Unable to load configuration"),
				   err->message);
			nih_free (err);
		}
	}
}

/**
 * conf_source_reload:
 * @source: configuration source to reload.
 *
 * Reloads the given configuration @source.
 *
 * If not already established, an inotify watch is created so that future
 * changes to this source are automatically detected and parsed.  For files,
 * this watch is actually on the parent directory, since we need to watch
 * out for editors that rename over the top, etc.
 *
 * We then parse the current state of the source.  The flag member is
 * toggled first, and this is propogated to all new and modified files and
 * items that we find as a result of parsing.  Once done, we scan for
 * anything with the wrong flag, and delete them.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
conf_source_reload (ConfSource *source)
{
	int ret;

	nih_assert (source != NULL);

	/* Toggle the flag so we can detect deleted files and items. */
	source->flag = (! source->flag);

	/* Reload the source itself. */
	switch (source->type) {
	case CONF_FILE:
		ret = conf_source_reload_file (source);
		break;
	case CONF_DIR:
	case CONF_JOB_DIR:
		ret = conf_source_reload_dir (source);
		break;
	default:
		nih_assert_not_reached ();
	}

	/* Scan for files that have been deleted since the last time we
	 * reloaded; these are simple to detect, as they will have the wrong
	 * flag.
	 */
	NIH_HASH_FOREACH_SAFE (source->files, iter) {
		ConfFile *file = (ConfFile *)iter;

		if (file->flag != source->flag)
			conf_file_free (file);
	}

	return ret;
}

/**
 * conf_source_reload_file:
 * @source: configuration source to reload.
 *
 * Reloads the configuration file specified by @source.
 *
 * If not already established, an inotify watch is created on the parent
 * directory so that future changes to the file are automatically detected
 * and parsed.  It is the parent directory because we need to watch out for
 * editors that rename over the top, etc.
 *
 * We then parse the current state of the file, propogating the value of
 * the flag member to all items that we find so that deletions can be
 * detected by the calling function.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
conf_source_reload_file (ConfSource *source)
{
	NihError *err = NULL;

	nih_assert (source != NULL);
	nih_assert (source->type == CONF_FILE);

	if (! source->watch) {
		char *dpath, *dname;

		NIH_MUST (dpath = nih_strdup (NULL, source->path));
		dname = dirname (dpath);

		source->watch = nih_watch_new (source, dname, FALSE, FALSE,
					       (NihFileFilter)conf_file_filter,
					       (NihCreateHandler)conf_create_modify_handler,
					       (NihModifyHandler)conf_create_modify_handler,
					       (NihDeleteHandler)conf_delete_handler,
					       source);

		nih_free (dpath);

		/* If successful mark the file descriptor close-on-exec,
		 * otherwise stash the error for comparison with a later
		 * failure to parse the file.
		 */
		if (source->watch) {
			nih_io_set_cloexec (source->watch->fd);
		} else {
			err = nih_error_get ();
		}
	}

	/* Parse the file itself.  If this fails, then we can discard the
	 * inotify error, since this one will be better.
	 */
	if (conf_reload_path (source, source->path) < 0) {
		if (err)
			nih_free (err);

		return -1;
	}

	/* We were able to parse the file, but were not able to set up an
	 * inotify watch.  This isn't critical, so we just warn about it,
	 * unless this is simply that inotify isn't supported, in which case
	 * we do nothing.
	 */
	if (err) {
		if (err->number != ENOSYS)
			nih_warn ("%s: %s: %s", source->path,
				  _("Unable to watch configuration file"),
				  err->message);

		nih_free (err);
	}

	return 0;
}

/**
 * conf_source_reload_dir:
 * @source: configuration source to reload.
 *
 * Reloads the configuration directory specified by @source.
 *
 * If not already established, an inotify watch is created on the directory
 * so that future changes to the structure or files within it are
 * automatically parsed.  This has the side-effect of parsing the current
 * tree.
 *
 * Otherwise we walk the tree ourselves and parse all files that we find,
 * propogating the value of the flag member to all files and items so that
 * deletion can be detected by the calling function.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
conf_source_reload_dir (ConfSource *source)
{
	NihError *err = NULL;

	nih_assert (source != NULL);
	nih_assert (source->type != CONF_FILE);

	if (! source->watch) {
		source->watch = nih_watch_new (source, source->path,
					       TRUE, TRUE, nih_file_ignore,
					       (NihCreateHandler)conf_create_modify_handler,
					       (NihModifyHandler)conf_create_modify_handler,
					       (NihDeleteHandler)conf_delete_handler,
					       source);

		/* If successful, the directory tree will have been walked
		 * already; so just mark the file descriptor close-on-exec
		 * and return; otherwise we'll try and walk ourselves, so
		 * stash the error for comparison.
		 */
		if (source->watch) {
			nih_io_set_cloexec (source->watch->fd);
			return 0;
		} else {
			err = nih_error_get ();
		}
	}

	/* We're either performing a mandatory reload, or we failed to set
	 * up an inotify watch; walk the directory tree the old fashioned
	 * way.  If this fails too, then we can discard the inotify error
	 * since this one will be better.
	 */
	if (nih_dir_walk (source->path, nih_file_ignore,
			  (NihFileVisitor)conf_file_visitor, NULL,
			  source) < 0) {
		if (err)
			nih_free (err);

		return -1;
	}

	/* We were able to walk the directory, but were not able to set up
	 * an inotify watch.  This isn't critical, so we just warn about it,
	 * unless this is simply that inotify isn't supported, in which case
	 * we do nothing.
	 */
	if (err) {
		if (err->number != ENOSYS)
			nih_warn ("%s: %s: %s", source->path,
				  _("Unable to watch configuration directory"),
				  err->message);

		nih_free (err);
	}

	return 0;
}


/**
 * conf_file_filter:
 * @source: configuration source,
 * @path: path to check.
 *
 * When we watch the parent directory of a file for changes, we receive
 * notification about all changes to that directory.  We only care about
 * those that affect the path in @source, so we use this function to filter
 * out all others.
 *
 * Returns: FALSE if @path matches @source, TRUE otherwise.
 **/
static int
conf_file_filter (ConfSource *source,
		  const char *path)
{
	nih_assert (source != NULL);
	nih_assert (path != NULL);

	if (! strcmp (source->path, path))
		return FALSE;

	return TRUE;
}

/**
 * conf_create_modify_handler:
 * @source: configuration source,
 * @watch: NihWatch for source,
 * @path: full path to modified file,
 * @statbuf: stat of @path.
 *
 * This function will be called whenever a file is created in a directory
 * that we're watching, moved into the directory we're watching, or is
 * modified.  This works for both directory and file sources, since the
 * watch for the latter is on the parent and filtered to only return the
 * path that we're interested in.
 *
 * After checking that it was a regular file that was changed, we reload it;
 * we expect this to fail sometimes since the file may be only partially
 * written.
  **/
static void
conf_create_modify_handler (ConfSource  *source,
			    NihWatch    *watch,
			    const char  *path,
			    struct stat *statbuf)
{
	nih_assert (source != NULL);
	nih_assert (watch != NULL);
	nih_assert (path != NULL);
	nih_assert (statbuf != NULL);

	if (! S_ISREG (statbuf->st_mode))
		return;

	if (conf_reload_path (source, path) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_error ("%s: %s: %s", path,
			   _("Error while loading configuration file"),
			   err->message);
		nih_free (err);
	}
}

/**
 * conf_delete_handler:
 * @source: configuration source,
 * @watch: NihWatch for source,
 * @path: full path to deleted file.
 *
 * This function will be called whenever a file is removed or moved out
 * of a directory that we're watching.  This works for both directory and
 * file sources, since the watch for the latter is on the parent and
 * filtered to only return the path that we're interested in.
 *
 * We lookup the file in our hash table, and if we can find it, perform
 * the usual deletion on all of its items and the file itself.
  **/
static void
conf_delete_handler (ConfSource *source,
		     NihWatch   *watch,
		     const char *path)
{
	ConfFile *file;

	nih_assert (source != NULL);
	nih_assert (watch != NULL);
	nih_assert (path != NULL);

	/* Lookup the file in the source; if we haven't parsed it, there's
	 * no point worrying about it.
	 */
	file = (ConfFile *)nih_hash_lookup (source->files, path);
	if (! file)
		return;

	conf_file_free (file);
}

/**
 * conf_file_visitor:
 * @source: configuration source,
 * @dirname: top-level directory being walked,
 * @path: path found in directory,
 * @statbuf: stat of @path.
 *
 * This function is called when walking a directory tree for each file
 * found within it.
 *
 * After checking that it's a regular file, we reload it.
 *
 * Returns: always zero.
 **/
static int
conf_file_visitor (ConfSource  *source,
		   const char  *dirname,
		   const char  *path,
		   struct stat *statbuf)
{
	nih_assert (source != NULL);
	nih_assert (dirname != NULL);
	nih_assert (path != NULL);
	nih_assert (statbuf != NULL);

	if (! S_ISREG (statbuf->st_mode))
		return 0;

	if (conf_reload_path (source, path) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_error ("%s: %s: %s", path,
			   _("Error while loading configuration file"),
			   err->message);
		nih_free (err);
	}

	return 0;
}


/**
 * conf_reload_path:
 * @source: configuration source,
 * @path: path of file to be reloaded.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
conf_reload_path (ConfSource *source,
		  const char *path)
{
	ConfFile   *file;
	NihList     old_items;
	ConfItem   *item;
	const char *buf, *name;
	size_t      len, pos, lineno;
	NihError   *err = NULL;

	nih_assert (source != NULL);
	nih_assert (path != NULL);

	NIH_MUST (file = conf_file_get (source, path));

	/* Map the file into memory for parsing. */
	buf = nih_file_map (file->path, O_RDONLY | O_NOCTTY, &len);
	if (! buf)
		return -1;

	/* If we've parsed this file before, we'll have a list of old items
	 * that once existed and need to be cleaned up once we've parsed
	 * the new items.  The easiest way to identify them is to put
	 * them into a different list for safe-keeping.
	 */
	nih_list_init (&old_items);
	nih_list_add (&old_items, file->items.next);
	nih_list_remove (&file->items);

	/* Parse the file buffer, registering items found against the
	 * ConfFile; the existing items are removed later.
	 */
	pos = 0;
	lineno = 1;

	switch (source->type) {
	case CONF_FILE:
	case CONF_DIR:
		/* Parse the file, this deals with item creation itself
		 * since only it knows the item types and names.
		 */
#if 0
		if (parse_conf (buf, len, &pos, &lineno) < 0)
			err = nih_error_get ();
#endif

		break;
	case CONF_JOB_DIR:
		/* Construct the job name by taking the path and removing
		 * the directory name from the front.
		 */
		name = path;
		if (! strncmp (name, source->path, strlen (source->path)))
			name += strlen (source->path);

		while (*name == '/')
			name++;

		/* Create a new job item and parse the buffer to produce
		 * the job definition.  Discard the item if this fails.
		 */
		NIH_MUST (item = conf_item_new (source, file, CONF_JOB));
		item->job = parse_job (NULL, name, buf, len, &pos, &lineno);
		if (! item->job) {
			err = nih_error_get ();

			nih_list_free (&item->entry);
		}

		break;
	default:
		nih_assert_not_reached ();
	}

	/* Deal with any parsing errors that occurred; we don't consider
	 * these to be hard failures, which means we can warn about them
	 * here and give the path and line number along with the warning.
	 */
	if (err) {
		switch (err->number) {
		case NIH_CONFIG_EXPECTED_TOKEN:
		case NIH_CONFIG_UNEXPECTED_TOKEN:
		case NIH_CONFIG_TRAILING_SLASH:
		case NIH_CONFIG_UNTERMINATED_QUOTE:
		case NIH_CONFIG_UNTERMINATED_BLOCK:
		case NIH_CONFIG_UNKNOWN_STANZA:
		case PARSE_ILLEGAL_INTERVAL:
		case PARSE_ILLEGAL_EXIT:
		case PARSE_ILLEGAL_UMASK:
		case PARSE_ILLEGAL_NICE:
		case PARSE_ILLEGAL_LIMIT:
			nih_error ("%s:%zi: %s", path, lineno, err->message);
			nih_free (err);
			err = NULL;
			break;
		}
	}

	/* Delete the old items now we've parsed in the list of new ones.
	 */
	NIH_LIST_FOREACH_SAFE (&old_items, iter) {
		ConfItem *item = (ConfItem *)iter;

		conf_item_free (item);
	}

	/* Unmap the file again; in theory this shouldn't fail, but if
	 * it does, return an error condition even though we've actually
	 * loaded some of the new things.
	 */
	if (nih_file_unmap ((void *)buf, len) < 0) {
		if (err)
			nih_free (err);
		return -1;
	}

	if (err) {
		nih_error_raise_again (err);
		return -1;
	}

	return 0;
}


/**
 * conf_source_free:
 * @source: configuration source to be freed.
 *
 * Frees the watch held by @source, all files parsed by source and the items
 * helped by them, and then frees the source itself.
 *
 * Returns: return value from destructor, or 0.
 **/
int
conf_source_free (ConfSource *source)
{
	nih_assert (source != NULL);

	NIH_HASH_FOREACH_SAFE (source->files, iter) {
		ConfFile *file = (ConfFile *)iter;

		conf_file_free (file);
	}

	if (source->watch)
		nih_watch_free (source->watch);

	return nih_list_free (&source->entry);
}

/**
 * conf_file_free:
 * @file: configuration file to be deleted and freed.
 *
 * Frees all items held by @file and then removes @file from its containing
 * source and frees the memory allocated for it.
 *
 * Returns: return value from destructor, or 0.
 **/
int
conf_file_free (ConfFile *file)
{
	nih_assert (file != NULL);

	/* Delete all items parsed from here. */
	NIH_LIST_FOREACH_SAFE (&file->items, iter) {
		ConfItem *item = (ConfItem *)iter;

		conf_item_free (item);
	}

	return nih_list_free (&file->entry);
}

/**
 * conf_item_free:
 * @item: configuration item to be deleted and freed.
 *
 * Removes @item from its containing file and frees the memory allocated
 * for it.
 *
 * Returns: return value from destructor, or 0.
 **/
int
conf_item_free (ConfItem *item)
{
	nih_assert (item != NULL);

	switch (item->type) {
	case CONF_JOB:
		/* NB:
		 *
		 * If it doesn't have a replacement already, mark it for
		 * deletion.
		 *
		 * FIXME: what if we're the replacement for another job?
		 * (copy our replacement to be that one's replacement)
		 */
		if (! item->job->replacement)
			item->job->replacement = (void *)-1;

		/*
		if (item->job->replacement_for) {
			item->job->replacement_for->replacement = item->job->replacement;
		*/

		if (job_should_replace (item->job))
			job_change_state (item->job, job_next_state (item->job));

		break;
	}

	return nih_list_free (&item->entry);
}
