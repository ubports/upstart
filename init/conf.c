/* upstart
 *
 * conf.c - configuration management
 *
 * Copyright © 2009 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
 *

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
#include "parse_conf.h"
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
 * This list holds the list of known sources of configuration; each item
 * is a ConfSource structure.  The order of this list dictates the priority
 * of the sources, with the first one having the highest priority.
 **/
NihList *conf_sources = NULL;


/**
 * conf_init:
 *
 * Initialise the conf_sources list.
 **/
void
conf_init (void)
{
	if (! conf_sources)
		conf_sources = NIH_MUST (nih_list_new (NULL));
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
 * The returned structure is automatically added to the conf_sources list.
 *
 * Configuration is not parsed immediately, instead you must call
 * conf_source_reload() on this source to set up any watches and load the
 * current configuration.  Normally you would set up all of the sources and
 * then call conf_reload() which will load them all.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.
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

	conf_init ();

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
	source->files = nih_hash_string_new (source, 0);
	if (! source->files) {
		nih_free (source);
		return NULL;
	}

	nih_alloc_set_destructor (source, nih_list_destroy);

	nih_list_add (conf_sources, &source->entry);

	return source;
}

/**
 * conf_file_new:
 * @source: configuration source,
 * @path: path to file.
 *
 * Allocates and returns a new ConfFile structure for the given @source,
 * with @path indicating which file it is.
 *
 * The returned structure is automatically placed in the @source's files hash
 * and the flag of the returned ConfFile will be set to that of the @source.
 *
 * Returns: newly allocated ConfFile structure or NULL if insufficient memory.
 **/
ConfFile *
conf_file_new (ConfSource *source,
	       const char *path)
{
	ConfFile *file;

	nih_assert (source != NULL);
	nih_assert (path != NULL);

	file = nih_new (source, ConfFile);
	if (! file)
		return NULL;

	nih_list_init (&file->entry);

	file->path = nih_strdup (file, path);
	if (! file->path) {
		nih_free (file);
		return NULL;
	}

	file->source = source;
	file->flag = source->flag;
	file->data = NULL;

	nih_alloc_set_destructor (file, conf_file_destroy);

	nih_hash_add (source->files, &file->entry);

	return file;
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

	NIH_LIST_FOREACH (conf_sources, iter) {
		ConfSource *source = (ConfSource *)iter;

		if (conf_source_reload (source) < 0) {
			NihError *err;

			err = nih_error_get ();
			if (err->number != ENOENT)
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
	NihList deleted;
	int     ret;

	nih_assert (source != NULL);

	nih_info (_("Loading configuration from %s"), source->path);

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
	 *
	 * We take them out of the files list and then we can delete the
	 * attached jobs and free the file.  We can't just do this from
	 * the one loop because to delete the jobs, we need to be able
	 * to iterate the sources and files.
	 */
	nih_list_init (&deleted);
	NIH_HASH_FOREACH_SAFE (source->files, iter) {
		ConfFile *file = (ConfFile *)iter;

		if (file->flag != source->flag)
			nih_list_add (&deleted, &file->entry);
	}
	NIH_LIST_FOREACH_SAFE (&deleted, iter) {
		ConfFile *file = (ConfFile *)iter;

		nih_info (_("Handling deletion of %s"), file->path);
		nih_unref (file, source);
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
 * Returns: zero on success, negative value on raised error.
 **/
static int
conf_source_reload_file (ConfSource *source)
{
	NihError *err = NULL;

	nih_assert (source != NULL);
	nih_assert (source->type == CONF_FILE);

	if (! source->watch) {
		nih_local char *dpath = NULL;
		char           *dname;

		dpath = NIH_MUST (nih_strdup (NULL, source->path));
		dname = dirname (dpath);

		source->watch = nih_watch_new (source, dname, FALSE, FALSE,
					       (NihFileFilter)conf_file_filter,
					       (NihCreateHandler)conf_create_modify_handler,
					       (NihModifyHandler)conf_create_modify_handler,
					       (NihDeleteHandler)conf_delete_handler,
					       source);

		/* If successful mark the file descriptor close-on-exec,
		 * otherwise stash the error for comparison with a later
		 * failure to parse the file.
		 */
		if (source->watch) {
			nih_io_set_cloexec (source->watch->fd);
		} else {
			err = nih_error_steal ();
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
 * propogating the value of the flag member to all files so that deletion
 * can be detected by the calling function.
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
			err = nih_error_steal ();
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
 * those that affect the path in @source, and the path that we're watching,
 * so we use this function to filter out all others.
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

	if (! strcmp (source->watch->path, path))
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
 * the usual deletion of it.
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

	/* Lookup the file in the source.  If we haven't parsed it, this
	 * could actually mean that it was the top-level directory itself
	 * that was deleted, in which case we free the watch, otherwise
	 * it's probably a directory or something, so just ignore it.
	 */
	file = (ConfFile *)nih_hash_lookup (source->files, path);
	if (! file) {
		if (! strcmp (watch->path, path)) {
			nih_warn ("%s: %s", source->path,
				  _("Configuration directory deleted"));
			nih_unref (source->watch, source);
			source->watch = NULL;
		}

		return;
	}

	nih_unref (file, source);
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
 * This function is used to parse the file at @path in the context of the
 * given configuration @source.  Necessary ConfFile structures are allocated
 * and attached to @source as appropriate.  CONF_FILE sources always have
 * a single ConfFile when the file exists.
 *
 * If the file has been parsed before, then the existing item is deleted and
 * freed if the file fails to load, or after the new item has been parsed.
 * Items are not reused between reloads.
 *
 * Physical errors are returned, parse errors are not.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
conf_reload_path (ConfSource *source,
		  const char *path)
{
	ConfFile       *file;
	nih_local char *buf = NULL;
	const char     *name;
	size_t          len, pos, lineno;
	NihError       *err = NULL;

	nih_assert (source != NULL);
	nih_assert (path != NULL);

	/* Look up the old file in memory, and then free it.  In cases
	 * of failure, we discard it anyway, so there's no particular reason
	 * to keep it around anymore.
	 */
	file = (ConfFile *)nih_hash_lookup (source->files, path);
	if (file)
		nih_unref (file, source);

	/* Read the file into memory for parsing, if this fails we don't
	 * bother creating a new ConfFile structure for it and bail out
	 * now.
	 */
	buf = nih_file_read (NULL, path, &len);
	if (! buf)
		return -1;

	/* Parse the file, storing the item in a new ConfFile structure. */
	file = NIH_MUST (conf_file_new (source, path));

	pos = 0;
	lineno = 1;

	switch (source->type) {
	case CONF_FILE:
	case CONF_DIR:
		/* Simple file of options; usually no item attached to it. */
		nih_debug ("Loading configuration from %s", path);
		if (parse_conf (file, buf, len, &pos, &lineno) < 0)
			err = nih_error_get ();

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
		 * the job definition.
		 */
		nih_debug ("Loading %s from %s", name, path);
		file->job = parse_job (NULL, name, buf, len, &pos, &lineno);
		if (file->job) {
			job_class_consider (file->job);
		} else {
			err = nih_error_get ();
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
		case PARSE_ILLEGAL_OOM:
		case PARSE_ILLEGAL_LIMIT:
		case PARSE_EXPECTED_EVENT:
		case PARSE_EXPECTED_OPERATOR:
		case PARSE_EXPECTED_VARIABLE:
		case PARSE_MISMATCHED_PARENS:
			nih_error ("%s:%zi: %s", path, lineno, err->message);
			nih_free (err);
			err = NULL;
			break;
		}
	}

	/* If we had any unknown error from parsing the file, raise it again
	 * and return an error condition.
	 */
	if (err)
		return -1;

	return 0;
}


/**
 * conf_file_destroy:
 * @file: configuration file to be destroyed.
 *
 * Handles the replacement and deletion of a configuration file, ensuring
 * that @file is removed from the containing linked list and that the item
 * attached to it is destroyed if not currently in use.
 *
 * Normally used or called from an nih_alloc() destructor so that the list
 * item is automatically removed from its containing list when freed.
 *
 * Returns: zero.
 **/
int
conf_file_destroy (ConfFile *file)
{
	nih_assert (file != NULL);

	nih_list_destroy (&file->entry);

	switch (file->source->type) {
	case CONF_FILE:
	case CONF_DIR:
		break;
	case CONF_JOB_DIR:
		if (! file->job)
			break;

		/* Mark the job to be deleted when it stops, in case
		 * it cannot be deleted here.
		 */
		file->job->deleted = TRUE;

		/* Check whether the job is the current one with that name;
		 * if it is, try and replace it.  If it wasn't the current
		 * job, or isn't after replacement, we can free it now.
		 */
		if (job_class_reconsider (file->job)) {
			nih_debug ("Destroyed unused job %s", file->job->name);
			nih_free (file->job);
		}

		break;
	default:
		nih_assert_not_reached ();
	}

	return 0;
}


/**
 * conf_select_job:
 * @name: name of job class to locate.
 *
 * Select the best available class of a job named @name from the registered
 * configuration sources.
 *
 * Returns: Best available job class or NULL if none available.
 **/
JobClass *
conf_select_job (const char *name)
{
	nih_assert (name != NULL);

	conf_init ();

	NIH_LIST_FOREACH (conf_sources, iter) {
		ConfSource *source = (ConfSource *)iter;

		if (source->type != CONF_JOB_DIR)
			continue;

		NIH_HASH_FOREACH (source->files, file_iter) {
			ConfFile *file = (ConfFile *)file_iter;

			if (! file->job)
				continue;

			if (! strcmp (file->job->name, name))
				return file->job;
		}
	}

	return NULL;
}
