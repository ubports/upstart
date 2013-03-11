/* upstart
 *
 * Copyright © 2013 Canonical Ltd.
 * Author: James Hunt <james.hunt@canonical.com>.
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

/**
 * This is the Upstart File Bridge which allows jobs to react to files
 * being created, modified and deleted.
 *
 * = Design =
 *
 * This bridge creates inotify watches on the _first existing parent
 * directory_ for the file (normal or directory) being watched for. As
 * directories are created, the watch is moved to become more specific
 * (closer to the actually requested file path) and as directories are
 * deleted, the watch is correspondingly changed to a less specific, but
 * existing, directory.
 *
 * This is necessary since:
 *
 * - It conserves system resources.
 *
 *   There is little point creating 'n' watches on existing files when a
 *   single watch on the parent directory will suffice.
 *
 * - It is not possible to create an inotify watch for a non-existent
 *   entity (*).
 *
 * - In a sense, it simplifies the design.
 *
 *   Otherwise the bridge would have to put a watch on each existing
 *   file for modify and delete requests, but watch the parent for
 *   create requests. And for a combination of requests who share
 *   a parent directory, it's easier to just watch the parent alone.
 *
 * = Limitations =
 *
 * Since inotify is used, this bridge has a number of significant
 * limitations:
 *
 * 1) It cannot be anything but inherently racy.
 *
 * inotify(7) does not support recursive watches, so in some -- and not
 * necessarily pathological -- cases, events may be missed. This is
 * unfortunately exacerbated by the design of the bridge which creates
 * watches on the parent directory. This takes time, but in the window
 * when the watch is being created, files may have been modified
 * undetectably.
 *
 * For example, if the user requests a watch on '/var/log/app/foo.log',
 * the following might happen:
 *
 * (1) Watch is created for existing directory '/var/log/'.
 * (2) A process creates '/var/log/app/'.
 * (3) The bridge detects this and moves the watch from
 *     '/var/log/' to '/var/log/app/'.
 * (4) Whilst (3) is happening, some process removes '/var/log/app/'.
 * (5) The bridge now has an impotent watch on the now-deleted
 *     '/var/log/app/'.
 * (6) The app starts and (re-)creates '/var/log/app/'.
 * (7) The app now creates '/var/log/app/foo.log'.
 * (8) No event is emitted due to the impotent watch in (5).
 *
 * The situation is sadly actually worse than this: if a job watches for
 * a deep directory, if any one of the directory elements that is
 * created gets missed due to a race between the directory creation and
 * this bridge creating or moving a watch, the event will not be
 * emitted.
 *
 * = Advice =
 *
 * - Attempt to only watch for files to be created/modified/deleted
 *   in directories that are guaranteed to already exist at
 *   system startup. This avoids the racy behaviour between
 *   directory creation and inotify watch manipulation.
 *
 * - If the directory is not guaranteed to exist at system startup,
 *   create an Upstart job that creates the directory before the bridge
 *   starts ('start on starting upstart-file-bridge').
 *
 * = Alternative Approaches =
 *
 * fanotify is an alternative but again, it is limited:
 *
 * == Pros ==
 *
 * + Supports recursive watches.
 *
 * == Cons ==
 *
 * - Does not support a file delete event.
 *
 * - Potentially high system performance impact since _every_ file
 *   operation on the partition (except delete) is inspected.
 *
 *---------- 
 *
 * (*) - this is half true: inotify alarmingly does allow a watch to be
 * created on a non-existent entity, but is impotent - if that entity is
 * ever created, no event is received.
 **/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fnmatch.h>
#include <glob.h>
#include <pwd.h>

#include <nih/alloc.h>
#include <nih/command.h>
#include <nih/error.h>
#include <nih/hash.h>
#include <nih/io.h>
#include <nih/list.h>
#include <nih/logging.h>
#include <nih/macros.h>
#include <nih/main.h>
#include <nih/option.h>
#include <nih/string.h>
#include <nih/test.h>
#include <nih/timer.h>
#include <nih/watch.h>

#include <nih-dbus/dbus_connection.h>
#include <nih-dbus/dbus_proxy.h>

#include "dbus/upstart.h"
#include "com.ubuntu.Upstart.h"
#include "com.ubuntu.Upstart.Job.h"

/**
 * FILE_EVENT:
 *
 * Name of event this program handles
 **/
#define FILE_EVENT "file"

/**
 * ALL_FILE_EVENTS:
 *
 * All the inotify file events we care about.
 **/
#define ALL_FILE_EVENTS (IN_CREATE|IN_MODIFY|IN_CLOSE_WRITE|IN_DELETE)

/**
 * GLOB_CHARS:
 *
 * Wildcard characters recognised by glob(3) and fnmatch(3).
 **/
#define GLOB_CHARS	"*?[]"

/**
 * original_path:
 *
 * @file: WatchedFile:
 *
 * Obtain the appropriate WatchedFile path: either the original if the
 * path underwent expansion, else the initial unexpanded path.
 *
 * Required for emitting events since jobs need the unexpanded path to
 * allow their start/stop condition to match even if the path has
 * subsequently been expanded by this bridge.
 **/
#define original_path(file) \
	 (file->original ? file->original : file->path)

/**
 * Job:
 *
 * @entry: list header,
 * @path: D-Bus path of Upstart job,
 * @files: list of pointers to WatchedFile files Job will watch.
 *
 * Structure we use for tracking Upstart jobs.
 **/
typedef struct job {
	NihList   entry;
	char     *path;
	NihList   files;
} Job;

/** 
 * WatchedDir:
 *
 * @entry: list header,
 * @path: full path of directory being watched,
 * @files: hash of WatchedFile objects representing all files
 *         watched in directory @path and sub-directories,
 * @watch: watch object.
 *
 * Every watched file is handled by watching the first parent
 * directory that currently exists. This allows use to:
 *
 * - minimise watch descriptors
 * - easily handle the case where a job wants to watch for a file being
 *   created when that file doesn't yet exist (*).
 *
 * The drawback to this strategy is the complexity of handling watched
 * files and directories when files are created and deleted.
 *
 * Note that the WatchedFiles in @files are not necessarily _immediate_
 * children of @path, but they are children.
 *
 * (*) Irritatingly, inotify _does_ allow for a watch on a
 *     non-existing file to be created, but the watch is
 *     impotent in that when the file _is_ created, no inotify
 *     event results.
 *
 **/
typedef struct watched_dir {
	NihList    entry;
	char      *path;
	NihHash   *files;
	NihWatch  *watch;
} WatchedDir;

/**
 * WatchedFile:
 *
 * @entry: list header,
 * @path: full path to file being watched (or a glob),
 * @original: original (relative) path as specified by job
 *  (or NULL if path expansion was not necessary),
 * @glob: glob file pattern (or NULL if globbing disabled),
 * @dir: TRUE if @path is a directory,
 * @events: mask of inotify events file is interested in,
 * @parent: parent who is watching over us.
 *
 * Details of the file being watched.
 **/
typedef struct watched_file {
	NihList      entry;
	char        *path;
	char        *original;
	char        *glob;
	int          dir;
	uint32_t     events;
	WatchedDir  *parent;
} WatchedFile;

/**
 * FileEvent:
 *
 * @entry: list header,
 * @path: full path to file being watched,
 * @event: event to emit,
 * @match: optional file match if @path is a directory or glob.
 *
 * Details of the event to be emitted.
 **/
typedef struct file_event {
	NihList      entry;
	char        *path;
	uint32_t     event;
	char        *match;
} FileEvent;

/* Prototypes for static functions */
static WatchedDir *watched_dir_new (const char *path, const struct stat *statbuf)
	__attribute__ ((warn_unused_result));

static WatchedFile *watched_file_new (const char *path,
				      const char *original,
				      uint32_t events,
				      const char *glob)
	__attribute__ ((warn_unused_result));

static Job *job_new (const char *class_path)
	__attribute__ ((warn_unused_result));

static int  file_filter (WatchedDir *dir, const char *path, int is_dir);

static void create_handler (WatchedDir *dir, NihWatch *watch,
				  const char *path, struct stat *statbuf);

static void modify_handler (WatchedDir *dir, NihWatch *watch,
				  const char *path, struct stat *statbuf);

static void delete_handler (WatchedDir *dir, NihWatch *watch,
				  const char *path);

static void upstart_job_added (void *data, NihDBusMessage *message,
				  const char *job_path);

static void upstart_job_removed (void *data, NihDBusMessage *message,
				  const char *job_path);

static void job_add_file (Job *job, char **file_info);

static void emit_event_error (void *data, NihDBusMessage *message);
static int  emit_event (const char *path, uint32_t event_type,
				  const char  *match);

static FileEvent *file_event_new (void *parent, const char *path,
				  uint32_t event, const char *match);

static void upstart_disconnected (DBusConnection *connection);

static void handle_event (NihHash *handled, const char  *path,
			  uint32_t event, const char  *match);

static int job_destroy (Job *job);

static char * find_first_parent (const char *path)
	__attribute__ ((warn_unused_result));

void watched_dir_init (void);

static void ensure_watched (Job *job, WatchedFile *file);

static int string_match (const char *a, const char *b)
	__attribute__ ((warn_unused_result));

char * expand_path (const void *parent, const char *path)
	__attribute__ ((warn_unused_result));

static int path_valid (const char *path)
	__attribute__ ((warn_unused_result));

/**
 * daemonise:
 *
 * Set to TRUE if we should become a daemon, rather than just running
 * in the foreground.
 **/
static int daemonise = FALSE;

/**
 * jobs:
 *
 * Hash of Upstart jobs that we're monitoring.
 **/
static NihHash *jobs = NULL;

/**
 * watched_dirs:
 *
 * Hash of WatchedDir objects representing the minimum set of existing
 * parent directories that allow all WatchedFiles to be watched for.
 **/
static NihHash *watched_dirs = NULL;

/**
 * upstart:
 *
 * Proxy to Upstart daemon.
 **/
static NihDBusProxy *upstart = NULL;

/**
 * user:
 *
 * If TRUE, run in User Session mode connecting to the Session Init
 * rather than PID 1. In this mode, certain relative paths are also
 * expanded.
 **/
static int user = FALSE;

/**
 * home_dir:
 *
 * Full path to home directory.
 **/
char home_dir[PATH_MAX];

/**
 * options:
 *
 * Command-line options accepted by this program.
 **/
static NihOption options[] = {
	{ 0, "daemon", N_("Detach and run in the background"),
	  NULL, NULL, &daemonise, NULL },
	{ 0, "user", N_("Connect to user session"),
	  NULL, NULL, &user, NULL },

	NIH_OPTION_LAST
};


int
main (int   argc,
      char *argv[])
{
	char          **args;
	DBusConnection *connection;
	char          **job_class_paths;
	int             ret;
	char           *user_session_addr = NULL;

	nih_main_init (argv[0]);

	nih_option_set_synopsis (_("Bridge inotify events into upstart"));
	nih_option_set_help (
		_("By default, upstart-inotify-bridge does not detach from the "
		  "console and remains in the foreground.  Use the --daemon "
		  "option to have it detach."));

	args = nih_option_parser (NULL, argc, argv, options, FALSE);
	if (! args)
		exit (EXIT_FAILURE);

	if (user) {
		struct passwd *pw;
		user_session_addr = getenv ("UPSTART_SESSION");
		if (! user_session_addr) {
			nih_fatal (_("UPSTART_SESSION isn't set in environment"));
			exit (EXIT_FAILURE);
		}

		pw = getpwuid (getuid ());

		if (! pw) {
			nih_error ("Failed to get password entry");
			exit (EXIT_FAILURE);
		}

		nih_assert (pw->pw_dir);

		strcpy (home_dir, (pw->pw_dir));
	}

	/* Allocate jobs hash table */
	jobs = NIH_MUST (nih_hash_string_new (NULL, 0));

	/* Initialise the connection to Upstart */
	connection = NIH_SHOULD (nih_dbus_connect (user
				? user_session_addr
				: DBUS_ADDRESS_UPSTART,
				upstart_disconnected));
	if (! connection) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not connect to Upstart"),
			   err->message);
		nih_free (err);

		exit (EXIT_FAILURE);
	}

	upstart = NIH_SHOULD (nih_dbus_proxy_new (NULL, connection,
						  NULL, DBUS_PATH_UPSTART,
						  NULL, NULL));
	if (! upstart) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not create Upstart proxy"),
			   err->message);
		nih_free (err);

		exit (EXIT_FAILURE);
	}

	/* Connect signals to be notified when jobs come and go */
	if (! nih_dbus_proxy_connect (upstart, &upstart_com_ubuntu_Upstart0_6, "JobAdded",
				      (NihDBusSignalHandler)upstart_job_added, NULL)) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not create JobAdded signal connection"),
			   err->message);
		nih_free (err);

		exit (EXIT_FAILURE);
	}

	if (! nih_dbus_proxy_connect (upstart, &upstart_com_ubuntu_Upstart0_6, "JobRemoved",
				      (NihDBusSignalHandler)upstart_job_removed, NULL)) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not create JobRemoved signal connection"),
			   err->message);
		nih_free (err);

		exit (EXIT_FAILURE);
	}

	/* Request a list of all current jobs */
	if (upstart_get_all_jobs_sync (NULL, upstart, &job_class_paths) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Could not obtain job list"),
			   err->message);
		nih_free (err);

		exit (EXIT_FAILURE);
	}

	/* Look for jobs that specify the FILE_EVENT event and handle
	 * them.
	 */
	for (char **job_class_path = job_class_paths;
	     job_class_path && *job_class_path; job_class_path++) {
		upstart_job_added (NULL, NULL, *job_class_path);
	}

	nih_free (job_class_paths);

	/* Become daemon */
	if (daemonise) {
		if (nih_main_daemonise () < 0) {
			NihError *err;

			err = nih_error_get ();
			nih_fatal ("%s: %s", _("Unable to become daemon"),
				   err->message);
			nih_free (err);

			exit (EXIT_FAILURE);
		}

		/* Send all logging output to syslog */
		openlog (program_name, LOG_PID, LOG_DAEMON);
		nih_log_set_logger (nih_logger_syslog);
	}

	if (user) {
		/* Ensure we are sitting in $HOME so relative FPATH
		 * values work as expected.
		 */
		if (chdir (home_dir) < 0) {
			nih_error ("Failed to change working directory");
			exit (EXIT_FAILURE);
		}
	}

	/* Handle TERM and INT signals gracefully */
	nih_signal_set_handler (SIGTERM, nih_signal_handler);
	NIH_MUST (nih_signal_add_handler (NULL, SIGTERM, nih_main_term_signal, NULL));

	if (! daemonise) {
		nih_signal_set_handler (SIGINT, nih_signal_handler);
		NIH_MUST (nih_signal_add_handler (NULL, SIGINT, nih_main_term_signal, NULL));
	}

	ret = nih_main_loop ();

	return ret;
}

/**
 * upstart_job_added:
 *
 * @data: (unused),
 * @message: Nih D-Bus message (unused),
 * @job_path: Upstart job class (D-Bus) path associated with job.
 *
 * Called automatically when a new Upstart job appears on D-Bus ("JobAdded" signal).
 **/
static void
upstart_job_added (void            *data,
		   NihDBusMessage  *message,
		   const char      *job_path)
{
	Job                      *job;
	nih_local NihDBusProxy   *job_class = NULL;
	nih_local char         ***start_on = NULL;
	nih_local char         ***stop_on = NULL;

	nih_assert (job_path);

	/* Obtain a proxy to the job */
	job_class = nih_dbus_proxy_new (NULL, upstart->connection,
					upstart->name, job_path,
					NULL, NULL);
	if (! job_class) {
		NihError *err;

		err = nih_error_get ();
		nih_error ("Could not create proxy for job %s: %s",
			   job_path, err->message);
		nih_free (err);

		return;
	}

	job_class->auto_start = FALSE;

	/* Obtain the start_on and stop_on properties of the job */
	if (job_class_get_start_on_sync (NULL, job_class, &start_on) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_error ("Could not obtain job start condition %s: %s",
			   job_path, err->message);
		nih_free (err);

		return;
	}

	if (job_class_get_stop_on_sync (NULL, job_class, &stop_on) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_error ("Could not obtain job stop condition %s: %s",
			   job_path, err->message);
		nih_free (err);

		return;
	}

	/* Free any existing record for the job (should never happen,
	 * but worth being safe).
	 */
	job = (Job *)nih_hash_lookup (jobs, job_path);
	if (job)
		nih_free (job);

	/* Create new record for the job */
	job = job_new (job_path);
	if (! job) {
		nih_error ("%s %s",
			_("Failed to create job"), job_path);
		return;
	}

	/* Find out whether this job listens for any FILE_EVENT events */
	for (char ***event = start_on; event && *event && **event; event++) {
		if (! strcmp (**event, FILE_EVENT))
			job_add_file (job, *event);
	}

	for (char ***event = stop_on; event && *event && **event; event++)
		if (! strcmp (**event, FILE_EVENT))
			job_add_file (job, *event);

	/* If we didn't end up with any files, free the job and move on */
	if (NIH_LIST_EMPTY (&job->files)) {
		nih_free (job);
		return;
	}

	nih_message ("Job got added %s", job_path);

}

/**
 * upstart_job_removed:
 *
 * @data: (unused),
 * @message: Nih D-Bus message (unused),
 * @job_path: Upstart job class (D-Bus) path associated with job.
 *
 * Called automatically when an Upstart job disappears from D-Bus
 * ("JobRemoved" signal).
 *
 **/
static void
upstart_job_removed (void            *data,
		     NihDBusMessage  *message,
		     const char      *job_path)
{
	Job *job;

	nih_assert (job_path);

	job = (Job *)nih_hash_lookup (jobs, job_path);

	if (! job)
		return;

	nih_message ("Job went away %s", job_path);

	nih_free (job);
}


/**
 * job_add_file:
 *
 * @job: Job,
 * @file_info: environment variables Upstart job has specified
 * relating to FILE_EVENT.
 *
 * Create a WatchedFile object based on @file_info and ensure that
 * WatchedFile file (or glob) is watched.
 **/
static void
job_add_file (Job    *job,
	      char  **file_info)
{
	uint32_t         events;
	WatchedFile     *file = NULL;
	nih_local char  *error = NULL;
	nih_local char  *glob_expr = NULL;
	nih_local char  *expanded = NULL;
	char             path[PATH_MAX];

	nih_assert (job);
	nih_assert (job->path);
	nih_assert (file_info);
	nih_assert (! strcmp (file_info[0], FILE_EVENT));

	memset (path, '\0', sizeof (path));

	for (char **env = file_info + 1; env && *env; env++) {
		char   *val;
		size_t  name_len;

		val = strchr (*env, '=');
		if (! val) {
			nih_warn ("%s: Ignored %s event without variable name",
					job->path, FILE_EVENT);
			goto error;
		}

		name_len = val - *env;
		val++;

		if (! strncmp (*env, "FPATH", name_len)) {
			char     dirpart[PATH_MAX];
			char     basepart[PATH_MAX];
			char    *dir;
			char    *base;
			size_t   len2;

			strcpy (path, val);

			if (user && path[0] != '/') {
				expanded = expand_path (NULL, path);
				if (! expanded) {
					nih_error ("Failed to expand path");
					goto error;
				}
			}

			if (! path_valid (path))
				goto error;

			strcpy (dirpart, path);
			dir = dirname (dirpart);

			/* See dirname(3) */
			nih_assert (*dir != '.');

			len2 = strlen (dir);

			if (strcspn (dir, GLOB_CHARS) < len2) {
				nih_warn ("%s: %s", job->path, _("Directory globbing not supported"));
				goto error;
			}

			strcpy (basepart, path);
			base = basename (basepart);

			/* See dirname(3) */
			nih_assert (strcmp (base, basepart));

			len2 = strlen (base);

			if (strcspn (base, GLOB_CHARS) < len2) {
				strcpy (path, dir);
				glob_expr = NIH_MUST (nih_strdup (NULL, base));
			}
		} else if (! strncmp (*env, "FEVENT", name_len)) {
			if (! strcmp (val, "create")) {
				events = IN_CREATE;
			} else if (! strcmp (val, "modify")) {
				events = (IN_MODIFY|IN_CLOSE_WRITE);
			} else if (! strcmp (val, "delete")) {
				events |= IN_DELETE;
			}
		}
	}

	if (! *path)
		goto error;

	if (! events)
		events = ALL_FILE_EVENTS;

	file = watched_file_new (expanded ? expanded : path,
			expanded ? path : NULL,
			events, glob_expr);

	if (! file) {
		nih_warn ("%s: %s",
			_("Failed to add new file"), path);
		goto error;
	}

	/* If the job cares about the file or directory existing and it
	 * _already_ exists, emit the event.
	 *
	 * Although technically fraudulent (the file might not have _just
	 * been created_ - it may have existed forever), it is necessary
	 * since otherwise jobs will hang around wating for the file to
	 * be 'freshly-created'. However, although nih_watch_new() has
	 * been told to run the create handler for pre-existing files
	 * that doesn't help as we don't watch the files, we watch
	 * their first existing parent directory.
	 **/
	if ((file->events & IN_CREATE)) {
		struct stat statbuf;

		if (glob_expr) {
			glob_t   globbuf;
			char     pattern[PATH_MAX];

			sprintf (pattern, "%s/%s",
					expanded ? expanded : path, glob_expr);

			if (! glob (pattern, 0, NULL, &globbuf)) {
				size_t    i;
				char    **results;

				results = globbuf.gl_pathv;

				/* emit one event per matching file */
				for (i = 0; i < globbuf.gl_pathc; i++) {
					emit_event (pattern, IN_CREATE, results[i]);
				}
			}

			globfree (&globbuf);
		} else {
			if (! stat (file->path, &statbuf))
				emit_event (file->path, IN_CREATE, NULL);
		}
	}

	ensure_watched (job, file);

	return;

error:
	if (file)
		nih_free (file);
}

/**
 * file_filter:
 *
 * @dir: WatchedDir,
 * @path: full path to file to consider,
 * @is_dir: TRUE if @path is a directory, else FALSE.
 *
 * Watch handler function to sift the wheat from the chaff.
 *
 * Returns: TRUE if @path should be ignored, FALSE otherwise.
 **/
int
file_filter (WatchedDir  *dir,
	     const char  *path,
	     int          is_dir)
{
	nih_assert (dir);
	nih_assert (path);

	NIH_HASH_FOREACH_SAFE (dir->files, iter) {
		WatchedFile *file = (WatchedFile *)iter;

		if (strstr (file->path, path) == file->path) {
			/* Either an exact match or path is a child of the watched file.
			 * Paths in the latter category will be inspected more closely by
			 * the handlers.
			 */
			return FALSE;
		} else if ((file->dir || file->glob) && strstr (path, file->path) == path) {
			return FALSE;
		}
	}

	return TRUE;
}

/**
 * create_handler:
 *
 * @dir: WatchedDir,
 * @watch: NihWatch for directory tree,
 * @path: full path to file,
 * @statbuf: stat of @path.
 *
 * Watch handler function called when a WatchedFile is created in @dir.
 **/
void
create_handler (WatchedDir   *dir,
		NihWatch     *watch,
		const char   *path,
		struct stat  *statbuf)
{
	WatchedDir         *new_dir;
	char               *p;
	int                 add_dir = FALSE;
	int                 empty;

	/* Hash of events already emitted (required to avoid sending
	 * same event multiple times).
	 */
	nih_local NihHash  *handled = NULL;

	/* List of existing WatchedFiles that need to be added against
	 * @path (since @path either exactly matches their path, or
	 * @path is more specific ancestor of their path).
	 */
	NihList             entries;

	nih_assert (dir);
	nih_assert (watch);
	nih_assert (path);
	nih_assert (statbuf);

	/* path should be a file below the WatchedDir */
	nih_assert (strstr (path, dir->path) == path);

	nih_list_init (&entries);
	handled = NIH_MUST (nih_hash_string_new (NULL, 0));

	NIH_HASH_FOREACH_SAFE (dir->files, iter) {
		WatchedFile *file = (WatchedFile *)iter;

		if (file->dir) {
			if (! strcmp (file->path, dir->path)) {
				/* Watch is on the directory itself and a file within that
				 * watched directory was created, hence emit the _directory_
				 * was modified.
				 */
				if (file->events & IN_MODIFY)
					handle_event (handled, file->path, IN_MODIFY, path);
			} else if (! strcmp (file->path, path)) {
				/* Directory has been created */
				handle_event (handled, file->path, IN_CREATE, NULL);
				add_dir = TRUE;
				nih_list_add (&entries, &file->entry);
			}
		} else if (file->glob) {
			char full_path[PATH_MAX];

			/* reconstruct the full path */
			strcpy (full_path, file->path);
			strcat (full_path, "/");
			strcat (full_path, file->glob);

			if (! fnmatch (full_path, path, FNM_PATHNAME) && (file->events & IN_CREATE))
				handle_event (handled, full_path, IN_CREATE, path);
		} else {
			if (! strcmp (file->path, path) && (file->events & IN_CREATE)) {
				/* exact match, so emit event */
				handle_event (handled, file->path, IN_CREATE, NULL);

			} else if ((p=strstr (file->path, path)) && p == file->path
					&& S_ISDIR (statbuf->st_mode)) {
				/* The created file is actually a directory
				 * more specific that the current watch
				 * directory associated with @file.
				 *
				 * As such, we can make the watch on @file more
				 * specific by dropping the old watch, creating
				 * a new WatchedDir for @path and adding @file
				 * to the new WatchedDir's files hash.
				 *
				 * This has to be handled carefully due to NIH
				 * list/hash handling constraints. First, the
				 * new directory is marked as needing to be
				 * added to the directory hash and secondly we
				 * add the WatchedFile to a list representing
				 * all WatchedFiles that need to be added for
				 * the new path.
				 */
				add_dir = TRUE;
				nih_list_add (&entries, &file->entry);
			}
		}
	}

	if (! add_dir)
		return;

	/* we should have atleast 1 file to add to the new watch */
	nih_assert (! NIH_LIST_EMPTY (&entries));

	new_dir = watched_dir_new (path, statbuf);
	if (! new_dir) {
		nih_warn ("%s: %s",
				_("Failed to watch directory"), path);
		return;
	}

	/* Add all list entries to the newly-created WatchedDir */
	NIH_LIST_FOREACH_SAFE (&entries, iter) {
		WatchedFile *file = (WatchedFile *)iter;

		nih_hash_add (new_dir->files, &file->entry);
	}

	empty = TRUE;
	NIH_HASH_FOREACH (dir->files, iter) {
		empty = FALSE;
		break;
	}

	if (empty) {
		/* Remove the old directory watch */
		nih_free (dir);
	}
}

/**
 * modify_handler:
 *
 * @dir: WatchedDir,
 * @watch: NihWatch for directory tree,
 * @path: full path to file,
 * @statbuf: stat of @path.
 *
 * Watch handler function called when a WatchedFile is modified in @dir.
 **/
void
modify_handler (WatchedDir   *dir,
		NihWatch     *watch,
		const char   *path,
		struct stat  *statbuf)
{
	nih_local NihHash  *handled = NULL;

	nih_assert (dir);
	nih_assert (watch);
	nih_assert (path);
	nih_assert (statbuf);

	/* path should be a file below the WatchedDir */
	nih_assert (strstr (path, dir->path) == path);

	handled = NIH_MUST (nih_hash_string_new (NULL, 0));

	NIH_HASH_FOREACH_SAFE (dir->files, iter) {
		WatchedFile *file = (WatchedFile *)iter;

		if (! (file->events & IN_MODIFY))
			continue;

		if (file->dir) {
			if (! strcmp (file->path, dir->path)) {
				/* Watch is on the directory itself and a file within that
				 * watched directory was modified, hence emit the _directory_
				 * was modified.
				 */
				handle_event (handled, original_path (file), IN_MODIFY, path);
			}
		} else if (file->glob) {
			char full_path[PATH_MAX];

			/* reconstruct the full path */
			strcpy (full_path, file->path);
			strcat (full_path, "/");
			strcat (full_path, file->glob);
			if (! fnmatch (full_path, path, FNM_PATHNAME) && (file->events & IN_MODIFY))
				handle_event (handled, full_path, IN_MODIFY, path);
		} else {
			if (! strcmp (file->path, path)) {
				/* exact match, so emit event */
				handle_event (handled, original_path (file), IN_MODIFY, NULL);
			} else if (file->dir && strstr (path, file->path) == path) {
				/* file in watched directory modified, so emit event */
				handle_event (handled, path, IN_MODIFY, NULL);
			}
		}
	}
}

/**
 * delete_handler:
 *
 * @dir: WatchedDir,
 * @watch: NihWatch for directory tree,
 * @path: full path to file that was deleted.
 *
 * Watch handler function called when a WatchedFile is deleted in @dir.
 */
void
delete_handler (WatchedDir  *dir,
		NihWatch    *watch,
		const char  *path)
{
	WatchedDir         *new_dir;
	char               *parent;
	char               *p;
	struct stat         statbuf;
	int                 rm_dir = FALSE;
	nih_local NihHash  *handled = NULL;

	/* List of existing WatchedFiles that need to be added against
	 * @path (since @path either exactly matches their path, or
	 * @path is more specific ancestor of their path).
	 */
	NihList     entries;

	nih_assert (dir);
	nih_assert (watch);
	nih_assert (path);

	/* path should be a file below the WatchedDir */
	nih_assert (strstr (path, dir->path) == path);

	nih_list_init (&entries);
	handled = NIH_MUST (nih_hash_string_new (NULL, 0));

	NIH_HASH_FOREACH_SAFE (dir->files, iter) {
		WatchedFile *file = (WatchedFile *)iter;

		if (file->dir) {
			if (! strcmp (file->path, path)) {
				/* Directory itself was deleted */
				handle_event (handled, original_path (file), IN_DELETE, NULL);
			} else if (! strcmp (file->path, dir->path)) {
				/* Watch is on the directory itself and a file within that
				 * watched directory was deleted, hence emit the directory was
				 * modified.
				 */
				if (file->events & IN_MODIFY)
					handle_event (handled, original_path (file), IN_MODIFY, path);
			}
		} else if (file->glob) {
			char full_path[PATH_MAX];

			/* reconstruct the full path */
			strcpy (full_path, file->path);
			strcat (full_path, "/");
			strcat (full_path, file->glob);

			if (! fnmatch (full_path, path, FNM_PATHNAME) && (file->events & IN_DELETE))
				handle_event (handled, full_path, IN_DELETE, path);
		} else {
			if (! strcmp (file->path, path) && (file->events & IN_DELETE)) {
				handle_event (handled, original_path (file), IN_DELETE, NULL);
			} else if ((p=strstr (file->path, path)) && p == file->path) {
				/* Create a new directory watch for all
				 * WatchedFiles whose immediate parent directory
				 * matches @path (in other words,
				 * make the watch looking after a WatchedFile
				 * less specific). This has to be handled
				 * carefully due to NIH list/hash handling
				 * constraints. First, the new directory is
				 * marked as needing to be added to the
				 * directory hash and secondly we add the
				 * WatchedFile to a list representing all
				 * WatchedFiles that need to be added for the
				 * new path.
				 */
				rm_dir = TRUE;
				nih_list_add (&entries, &file->entry);
			} else if (file->dir && strstr (path, file->path) == path && (file->events & IN_DELETE)) {
				/* file in watched directory deleted, so emit event */
				handle_event (handled, path, IN_DELETE, NULL);
			}
		}
	}

	if (! rm_dir)
		return;

	/* Remove the old directory watch */
	nih_free (dir);

	nih_assert (! NIH_LIST_EMPTY (&entries));

	parent = find_first_parent (dir->path);
	if (! parent) {
		nih_warn ("%s: %s",
				_("Failed to find parent directory"), dir->path);
		return;
	}

	/* Check to see if there is already an existing watch for the
	 * parent.
	 */
	new_dir = (WatchedDir *)nih_hash_lookup (watched_dirs, parent);

	if (! new_dir) {
		if (stat (parent, &statbuf) < 0) {
			nih_warn ("%s: %s",
					_("Failed to stat directory"), parent);
			return;
		}

		new_dir = watched_dir_new (parent, &statbuf);
		if (! new_dir) {
			nih_warn ("%s: %s",
					_("Failed to watch directory"), parent);
			return;
		}
	}

	/* Add all list entries to the newly-created WatchedDir. */
	NIH_LIST_FOREACH_SAFE (&entries, iter) {
		WatchedFile *file = (WatchedFile *)iter;

		nih_hash_add (new_dir->files, &file->entry);
	}
}

/**
 * upstart_disconnected:
 *
 * @connection: connection to Upstart.
 *
 * Handler called when bridge disconnected from Upstart.
 **/
static void
upstart_disconnected (DBusConnection *connection)
{
	nih_fatal (_("Disconnected from Upstart"));
	nih_main_loop_exit (1);
}

/**
 * ensure_watched:
 *
 * @job: job,
 * @file: file we want to watch.
 *
 * Ensure that the WatchedFile file specified is watched.
 *
 * For regular files, this is achieved by adding a watch to
 * the first *existing* _parent_ directory encountered and adding
 * that WatchedDir to the watched_dirs hash.
 *
 * For directories, if they do not yet exist, the strategy is as for
 * regular files. If the directories do exist, the watch is placed on
 * the directory itself.
 **/
static void
ensure_watched (Job          *job,
		WatchedFile  *file)
{
	WatchedDir      *dir = NULL;
	nih_local char  *path = NULL;
	NihListEntry    *entry;
	struct stat      statbuf;

	nih_assert (job);
	nih_assert (file);

	watched_dir_init ();

	if (file->dir || file->glob) {
		if (! stat (file->path, &statbuf)) {
			/* Directory already exists, so we can watch it,
			 * not its parent as is done for file watches.
			 */
			path = file->path;
			goto lookup;
		}
	}

	path = find_first_parent (file->path);
	if (! path) {
		nih_warn ("%s: %s",
				_("Failed to find parent directory"), file->path);
		return;
	}

lookup:
	dir = (WatchedDir *)nih_hash_lookup (watched_dirs, path);
	if (! dir) {
		dir = watched_dir_new (path, &statbuf);
		if (! dir)
			return;
	}

	/* Associate the WatchedFile with the job such that when the job
	 * is freed, the corresponding files are removed from their
	 * containing WatchedDirs.
	 */
	nih_ref (file, job);

	file->parent = dir;
	nih_hash_add (dir->files, &file->entry);

	/* Create a link from the job to the WatchedFile.
	*/
	entry = NIH_MUST (nih_list_entry_new (job));
	entry->data = file;
	nih_list_add (&job->files, &entry->entry);
}

/**
 * dir_watched_init:
 *
 * Initialise the watched_dirs hash table.
 **/
void
watched_dir_init (void)
{
	if (! watched_dirs)
		watched_dirs = NIH_MUST (nih_hash_string_new (NULL, 0));
}

/**
 * emit_event:
 *
 * @path: original path as specified by a registered job,
 * @event_type: inotify event type that occured,
 * @match: file match that resulted from @path if it contains glob
 *  wildcards (or NULL).
 *
 * Emit an Upstart event.
 **/
static int
emit_event (const char   *path,
	    uint32_t      event_type,
	    const char   *match)
{
	DBusPendingCall    *pending_call;
	nih_local char    **env = NULL;
	nih_local char     *var = NULL;
	size_t              env_len = 0;

	nih_assert (path);
	nih_assert (event_type == IN_CREATE ||
			event_type == IN_MODIFY ||
			event_type == IN_DELETE);

	env = NIH_MUST (nih_str_array_new (NULL));

	var = NIH_MUST (nih_sprintf (NULL, "FPATH=%s", path));
	NIH_MUST (nih_str_array_addp (&env, NULL, &env_len, var));

	var = NIH_MUST (nih_sprintf (NULL, "FEVENT=%s",
				event_type == IN_CREATE ? "create" :
				event_type == IN_MODIFY ? "modify" :
				"delete"));
	NIH_MUST (nih_str_array_addp (&env, NULL, &env_len, var));

	if (match) {
		var = NIH_MUST (nih_sprintf (NULL, "FMATCH=%s", match));
		NIH_MUST (nih_str_array_addp (&env, NULL, &env_len, var));
	}

	pending_call = NIH_SHOULD (upstart_emit_event (upstart,
				FILE_EVENT, env, FALSE,
				NULL, emit_event_error, NULL,
				NIH_DBUS_TIMEOUT_NEVER));
	if (! pending_call) {
		NihError *err;

		err = nih_error_get ();
		nih_warn ("%s", err->message);
		nih_free (err);
		return FALSE;
	}

	return TRUE;
}

/**
 * emit_event_error:
 *
 * @data: (unused),
 * @message: Nih D-Bus message (unused),
 *
 * Handle failure to emit an event by consuming raised error and
 * displaying its details.
 **/
static void
emit_event_error (void            *data,
		  NihDBusMessage  *message)
{
	NihError *err;

	err = nih_error_get ();
	nih_warn ("%s", err->message);
	nih_free (err);
}

/**
 * watched_dir_new:
 *
 * @path: Absolute path to watch.
 * @statbuf: stat of @path.
 *
 * Create a new directory watch object for @path.
 *
 * Returns: WatchedDir or NULL on error.
 **/
static WatchedDir *
watched_dir_new (const char         *path,
		 const struct stat  *statbuf)
{
	char         watched_path[PATH_MAX];
	size_t       len;
	WatchedDir  *dir;

	nih_assert (path);
	nih_assert (statbuf);

	/* we shouldn't already be watching this directory */
	nih_assert (! nih_hash_lookup (watched_dirs, path));

	watched_dir_init ();

	strcpy (watched_path, path);
	len = strlen (watched_path);

	if (watched_path[len-1] == '/') {
		/* Better to remove a trailing slash before handing to
		 * inotify since although all works as expected, the
		 * path handed to inotify also gets given to the
		 * create/modify/delete handlers which can then lead to
		 * multiple contiguous slashes which could result in
		 * jobs failing to start as they would not expect FMATCH
		 * to contain such values.
		 */
		watched_path[len-1] = '\0';
	}

	dir = nih_new (watched_dirs, WatchedDir);
	if (! dir)
		return NULL;

	nih_list_init (&dir->entry);

	nih_alloc_set_destructor (dir, nih_list_destroy);

	dir->path = nih_strdup (dir, path);
	if (! dir->path)
		goto error;

	dir->files = nih_hash_string_new (dir, 0);
	if (! dir->files)
		goto error;

	nih_hash_add (watched_dirs, &dir->entry);

	/* Create a watch on the specified directory.
	 *
	 * Don't set a recursive watch as there is no need
	 * (individual jobs only care about a single directory,
	 * and anyway the parent directory may be arbitrarily
	 * deep so it could be prohibitively expensive).
	 */
	dir->watch = nih_watch_new (dir, watched_path,
			FALSE, TRUE,
			(NihFileFilter)file_filter,
			(NihCreateHandler)create_handler,
			(NihModifyHandler)modify_handler,
			(NihDeleteHandler)delete_handler,
			dir);
	if (! dir->watch) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s %s: %s", _("Could not create watch for path"),
				path,
				err->message);
		nih_free (err);

		goto error;
	}

	return dir;

error:
	nih_free (dir);
	return NULL;
}

/**
 * watched_file_new:
 *
 * @path: full path to file,
 * @original: original (relative) path specified by job,
 * @events: events job wishes to watch for @path,
 * @glob: glob file pattern, or NULL,
 *
 * Create a WatchedFile object representing @path.
 *
 * If path expansion was required, @original must specify the original
 * path as specified by the job else it may be NULL.
 *
 * If @glob is set, @path will be the directory portion of the original
 * path with @glob being the file (or basename) portion.
 *
 * Returns: WatchedFile object, or NULL on insufficient memory.
 **/
static WatchedFile *
watched_file_new (const char  *path,
		  const char  *original,
		  uint32_t     events,
		  const char  *glob)
{
	size_t       len;
	WatchedFile *file;

	nih_assert (path);
	nih_assert (events);

	file = nih_new (NULL, WatchedFile);
	if (! file)
		return NULL;

	nih_list_init (&file->entry);

	nih_alloc_set_destructor (file, nih_list_destroy);

	len = strlen (path);

	file->dir = (path[len-1] == '/');

	/* optionally one or the other, but not both */
	if (file->dir || file->glob)
		nih_assert (file->dir || file->glob);

	file->path = nih_strdup (file, path);
	if (! file->path)
		goto error;

	file->original = NULL;
	if (original) {
		file->original = nih_strdup (file, original);
		if (! file->original)
			goto error;
	}

	file->glob = NULL;
	if (glob) {
		file->glob = nih_strdup (file, glob);
		if (! file->glob)
			goto error;
	}

	file->events = events;

	return file;

error:
	nih_free (file);
	return NULL;
}

/**
 * job_new:
 *
 * @path: Upstart job class (D-Bus) path job is registered on.
 *
 * Create a new Job object representing an Upstart job.
 *
 * Returns: job, or NULL on insufficient memory.
 **/
static Job *
job_new (const char *path)
{
	Job *job;

	nih_assert (path);

	job = nih_new (NULL, Job);
	if (! job)
		return NULL;

	nih_list_init (&job->entry);
	nih_list_init (&job->files);

	nih_alloc_set_destructor (job, job_destroy);

	job->path = nih_strdup (job, path);
	if (! job->path)
		goto error;

	nih_hash_add (jobs, &job->entry);

	return job;

error:
	nih_free (job);
	return NULL;
}

/**
 * job_destroy:
 *
 * @job: job.
 *
 * Destructor that handles the replacement and deletion of a Job,
 * ensuring that it is removed from the containing linked list and that
 * the item attached to it is destroyed if not currently in use.
 *
 * Normally used or called from an nih_alloc() destructor so that the
 * list item is automatically removed from its containing list when
 * freed.
 *
 * Returns: zero.
 **/
static int
job_destroy (Job *job)
{
	nih_assert (job);

	nih_list_destroy (&job->entry);

	NIH_LIST_FOREACH_SAFE (&job->files, iter) {
		NihListEntry *entry = (NihListEntry *)iter;
		WatchedFile  *file;

		nih_assert (entry->data);

		file = (WatchedFile *)entry->data;

		/* Remove file from associated WatchedDir */
		nih_free (file);
	}

	return 0;
}

/**
 * find_first_parent:
 * @path: initial absolute path to start search from.
 *
 * Starting at @path, search for the first existing path by
 * progressively removing individual path elements until an existing
 * path is found.
 *
 * Returns: Newly-allocated string representing path closest to @path
 * that currently exists, or NULL on insufficient memory.
 **/
static char *
find_first_parent (const char *path)
{
	char           current[PATH_MAX];
	char           tmp[PATH_MAX];
	char          *parent;
	WatchedDir    *dir = NULL;
	struct stat    statbuf;

	nih_assert (path);

	/* Ensure path is absolute */
	nih_assert (path[0] == '/');

	strncpy (current, path, sizeof (current));
	/* ensure termination */
	current[PATH_MAX-1] = '\0';

	do {
		/* save parent for next time through the loop */
		strcpy (tmp, current);
		parent = dirname (tmp);

		/* Ensure dirname returned something sane */
		nih_assert (strcmp (parent, "."));

		dir = (WatchedDir *)nih_hash_lookup (watched_dirs, current);

		if (dir || ! stat (current, &statbuf)) {
			/* either path is already a watched directory
			 * (and hence must exist), or it actually does exist.
			 */
			return nih_strdup (NULL, current);
		}

		/* Failed to find path, so make parent the path to look
		 * for.
		 */
		memmove (current, parent, 1+strlen (parent));
	} while (TRUE);

	/* If your root directory doesn't exist, you have problems :) */
	nih_assert_not_reached ();
}

/**
 * file_event_new:
 *
 * @parent: parent,
 * @path: path that event should contain,
 * @event: inotify event,
 * @match: file match if @path contains glob wildcards.
 *
 * Returns: newly-allocated FileEvent or NULL on insufficient memory.
 **/
static FileEvent *
file_event_new (void *parent, const char *path, uint32_t event, const char *match)
{
	FileEvent *file_event;

	nih_assert (path);
	nih_assert (event);

	file_event = nih_new (parent, FileEvent);
	if (! file_event)
		return NULL;

	nih_list_init (&file_event->entry);

	nih_alloc_set_destructor (file_event, nih_list_destroy);

	file_event->path = NIH_MUST (nih_strdup (file_event, path));
	file_event->event = event;
	file_event->match = match
		? NIH_MUST (nih_strdup (file_event, match))
		: NULL;

	return file_event;
}

/**
 * handle_event:
 *
 * @handled: hash of FileEvents already handled,
 * @file_event: FileEvent to consider.
 *
 * Determine if @file_event has already been handled; if not emit the
 * event and record its details in @handled.
 **/
static void
handle_event (NihHash    *handled,
	     const char  *path,
	     uint32_t     event,
	     const char  *match)
{
	FileEvent  *file_event;

	nih_assert (handled);
	nih_assert (path);
	nih_assert (event);

	file_event = (FileEvent *)nih_hash_search (handled, path, NULL);

	while (file_event) {
		if ((file_event->event & event) && string_match (file_event->match, match)) {
			return;
		}

		file_event = (FileEvent *)nih_hash_search (handled, path,
				&file_event->entry);
	}

	nih_assert (! file_event);

	/* Event has not yet been handled, so emit it and record fact
	 * it's now been handled.
	 */
	file_event = NIH_MUST (file_event_new (handled, path, event, match));
	nih_hash_add (handled, &file_event->entry);

	emit_event (path, event, match);
}

/**
 * string_match:
 *
 * @a: first string,
 * @b: second string.
 *
 * Compare @a and @b either or both of which may be NULL.
 *
 * Returns TRUE if strings are identical or both NULL, else FALSE.
 **/
static int
string_match (const char *a, const char *b)
{
	if (!a && !b)
		return TRUE;

	if (!a || !b)
		return FALSE;

	if (strcmp (a, b))
		return FALSE;

	return TRUE;
}

/**
 * expand_path:
 *
 * @parent: parent,
 * @path: path.
 *
 * Expand @path by replacing a leading '~/', './' or no path prefix by
 * the users home directory.
 *
 * Limitations: Does not expand '~user'.
 *
 * Returns: Newly-allocated fully-expanded path, or NULL on error.
 **/
char *
expand_path (const void *parent, const char *path)
{
	char        *new;
	const char  *p;

	nih_assert (path);

	/* Only user instances support this limited form of relative
	 * path.
	 */
	nih_assert (user);

	/* Avoid looking up users password entry again */
	nih_assert (home_dir[0]);

	/* absolute path so nothing to do */
	nih_assert (path[0] != '/');

	p = path;

	if (strstr (path, "~/") == path || strstr (path, "./") == path)
		p += 2;

	new = nih_sprintf (parent, "%s/%s", home_dir, p);

	return new;
}

/**
 * path_valid:
 *
 * @path: path.
 *
 * Perform basic tests to determine if @path is valid for
 * the purposes of this bridge.
 *
 * Returns: TRUE if @path is acceptable, else FALSE.
 **/
static int
path_valid (const char *path)
{
	size_t len;

	nih_assert (path);

	len = strlen (path);

	if (len > PATH_MAX-1) {
		nih_debug ("%s: %.*s...",
				_("Path too long"), PATH_MAX-1, path);
		return FALSE;
	}

	if (user) {
		/* Support absolute or relative paths where the latter
		 * begins with a directory name implicitly below $HOME.
		 */
		if (*path == '.') {
			nih_warn ("%s: %s", _("Path must be absolute"), path);
			return FALSE;
		}
	} else {
		if (*path != '/') {
			nih_warn ("%s: %s", _("Path must be absolute"), path);
			return FALSE;
		}
	}

	if (strstr (path, "../")) {
		nih_warn ("%s: %s", _("Path must not contain parent reference"), path);
		return FALSE;
	}

	return TRUE;
}
