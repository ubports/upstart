/* upstart
 *
 * log.c - persist job output to a log file.
 *
 * Copyright © 2011 Canonical Ltd.
 * Authors: Scott James Remnant <keybuk@google.com>,
 *          James Hunt <james.hunt@canonical.com>.
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

#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <poll.h>
#include <nih/signal.h>
#include <nih/main.h>
#include "log.h"
#include "job_process.h"
#include "session.h"
#include "conf.h"
#include "paths.h"

static int  log_file_open   (Log *log);
static int  log_file_write  (Log *log, const char *buf, size_t len);
static void log_read_watch  (Log *log);
static void log_flush       (Log *log);

/**
 * log_flushed:
 *
 * TRUE if log_clear_unflushed() has been called successfully.
 **/
static int log_flushed = 0;

/**
 * log_unflushed_files:
 *
 * List of NihListEntry objects containing Log objects which are no
 * longer associated with Job processes.
 *
 * All the entries in the list contain unflushed Log data.
 *
 * Used to capture job process output early in the boot process for
 * jobs that end before the log partition is mounted and writeable.
 **/
NihList *log_unflushed_files = NULL;

/**
 * log_new:
 *
 * @parent: parent for new job class,
 * @path: full path to on-disk log file,
 * @fd: file descriptor associated with jobs stdout and stderr which
 *      will be read from,
 * @uid: user id associated with this logger.
 *
 * Allocates and returns a new Log structure with the given @path
 * and @session.
 *
 * If @parent is not NULL, it should be a pointer to another object which
 * will be used as a parent for the returned job class.  When all parents
 * of the returned job class are freed, the returned log will also be
 * freed.
 *
 * Note that @fd must refer to a valid and open pty(7) file
 * descriptor.
 *
 * Returns: newly allocated Log structure or NULL on error.
 **/
Log *
log_new (const void *parent,
	 const char *path,
	 int fd,
	 uid_t uid)
{
	Log    *log;
	size_t  len;

	nih_assert (path);
	nih_assert (fd > 0);

	/* User job logging not currently available */
	if (uid)
		return NULL;

	len = strlen (path);
	if (! len)
		return NULL;

	/* Ensure path is within bounds.
	 *
	 * PATH_MAX refers to a _relative path_. We should never
	 * need to worry about that (large) value since we expect the
	 * log directory to be an absolute path, but it pays to be
	 * careful.
	 *
	 * Absolute paths have a different limit.
	 */
	if (path[0] == '/') {
		if (len+1 > _POSIX_PATH_MAX)
			return NULL;
	} else {
		if (len+1 > PATH_MAX)
			return NULL;
	}

	log = nih_new (parent, Log);
	if (! log)
		return NULL;

	log_unflushed_init ();

	log->fd            = -1;
	log->uid           = uid;
	log->unflushed     = NULL;
	log->io            = NULL;
	log->detached      = 0;
	log->remote_closed = 0;
	log->open_errno    = 0;

	log->path = nih_strndup (log, path, len);
	if (! log->path)
		goto error;

	log->unflushed = nih_io_buffer_new (log);
	if (! log->unflushed)
		goto error;

	log->io = nih_io_reopen (log, fd, NIH_IO_STREAM,
			(NihIoReader)log_io_reader,
			NULL,
			(NihIoErrorHandler)log_io_error_handler,
			log);

	if (! log->io) {
		/* Consume */
		NihError *err = nih_error_get ();
		nih_free (err);
		goto error;
	}

	nih_alloc_set_destructor (log, log_destroy);

	return log;

error:
	nih_free (log);
	return NULL;
}

/**
 * log_destroy:
 *
 * @log: Log.
 *
 * Called automatically when Log is being destroyed.
 *
 * XXX: Note that the fd associated with the jobs stdout and stderr (as
 * passed to log_new()) *MUST* be closed by the time this function is
 * called since it will continue to read from the fd until an error is
 * detected. This behaviour is required to ensure all job output is
 * read.
 *
 * Returns: 0 always.
 **/
int
log_destroy (Log *log)
{
	nih_assert (log);

	/* User job logging not currently available */
	nih_assert (log->uid == 0);

	log_flush (log);

	/* Force file to flush */
	if (log->fd != -1)
		close (log->fd);

	log->fd = -1;

	return 0;
}

/**
 * log_flush:
 *
 * @log: Log.
 *
 * Ensure that no job output data is buffered and attempt to flush all
 * unflushed data to disk.
 *
 * It is safe to call this function multiple times and may in fact be
 * necessary if the log file cannot be written for any reason.
 *
 * Note no return value since there isn't much that can be done at
 * the point this function is called should the flushing operations
 * fail.
 **/
static void
log_flush (Log *log)
{
	int ret;
	int flags;

	nih_assert (log);

	/* User job logging not currently available */
	nih_assert (log->uid == 0);

	/* Job probably attempted to write data _only_ before the logger
	 * could access the disk. Last ditch attempt to persist the
	 * data.
	 *
	 * If any failures occur at this stage, we are powerless.
	 */
	if (log->unflushed->len) {
		if (log_file_open (log) < 0)
			goto out;

		ret = log_file_write (log, NULL, 0);
		if (ret < 0) {
			close (log->fd);
			log->fd = -1;
			goto out;
		}
	}

	if (log->io) {
		nih_assert (log->io->watch);

		/* If the job associated with this log produces output _after_
		 * nih_io_handle_fds() has been called in any loop of the main
		 * loop and just before the job is destroyed, we will miss it.
		 * 
		 * Therefore, attempt to read from the watch fd until we get an error.
		 */
		if (! log->remote_closed)
			log_read_watch (log);

		flags = fcntl (log->io->watch->fd, F_GETFL);

		if (flags < 0 && errno == EBADF) {
			/* The watch fd is now known to be invalid, so disable
			 * the error handler to avoid an infinite loop where the
			 * error handler attempts to free the NihIo, which would
			 * error, causing the error handler to be called
			 * _ad infinitum_.
			 *
			 * Note that the NihIo is freed via
			 * nih_io_destroy().
			 */
			log->io->error_handler = NULL;

			nih_free (log->io);
			log->io = NULL;
		}
	}

	/* Force file to flush */
	if (log->fd != -1)
		close (log->fd);

out:
	log->fd = -1;
}

/**
 * log_io_reader:
 *
 * @log: Log associated with this @io,
 * @io: NihIo with data to be read,
 * @buf: buffer data is available in,
 * @len: bytes in @buf available for reading.
 *
 * Called automatically when data is available to read on the fd
 * encapsulated in @io.
 *
 * Notes for user jobs:
 *
 * User jobs by necessity are handled differently to system jobs. Since
 * a user job must log their data to files owned by a non-root user, the
 * safest technique is for a process running as that user to create the
 * log file. If we simply redirected the jobs standard streams, this
 * would be simple: the job process itself could write the files.
 * However, since we want to give the impression the job is connected to
 * a real terminal by using a pseudo-tty, we necessarily need "some
 * other" process to handle the jobs logging as the user in question.
 *
 * Since most jobs do not produce any output it would be highly
 * inefficient to spawn such a logger process as soon as every user job
 * starts. Therefore the approach taken is the lazy one: create a user
 * logger process _when the job first produces output_. To avoid
 * terrible performance this process will then hang around until the job
 * has finished.
 *
 * Note that only the initial amount of data read from a user job is
 * necessarily buffered within init itself. This initial amount is very
 * small due to the default applied by nih_io_watcher_read().
 * All subsequent job output is buffered within USER_LOGGER.
 **/
void
log_io_reader (Log *log, NihIo *io, const char *buf, size_t len)
{
	int          ret;

	nih_assert (log);
	nih_assert (log->path);
	nih_assert (io);
	nih_assert (log->io == io);
	nih_assert (buf);
	nih_assert (len);

	/* User job logging not currently available */
	nih_assert (log->uid == 0);

	/* Just in case we try to write more than read can inform us
	 * about (this should really be a build-time assertion).
	 */
	nih_assert (sizeof (size_t) == sizeof (ssize_t));

	ret = log_file_open (log);

	if (ret < 0) {
		if (log->open_errno != ENOSPC) {
			/* Add new data to unflushed buffer */
			if (nih_io_buffer_push (log->unflushed, buf, len) < 0)
				return;
		}

		/* Note that we always discard when out of space */
		nih_io_buffer_shrink (io->recv_buf, len);

		/* No point attempting to write if we cannot
		 * open the file.
		 */
		return;
	}

	ret = log_file_write (log, buf, len);
	if (ret < 0)
		nih_warn ("%s %s", _("Failed to write to log file"), log->path);
}

/**
 * log_io_error_handler:
 *
 * @log: Log associated with this @io,
 * @io: NihIo.
 *
 * Called automatically when reading the jobs stdout/stderr causes
 * an error. This will occur when the parent attempts a read after
 * the child has exited abnormally. Note that this error is expected,
 * but we must provide this handler to nih_io_reopen() since we need
 * to consume the error to ensure NIH ignores it.
 */
void
log_io_error_handler (Log *log, NihIo *io)
{
	NihError *err;

	nih_assert (log);
	nih_assert (io);

	/* User job logging not currently available */
	nih_assert (log->uid == 0);

	/* Consume */
	err = nih_error_get ();

	nih_assert (err->number == EIO);

	nih_free (err);

	/* Ensure the NihIo is closed */
	nih_free (log->io);
	log->io = NULL;

	log->remote_closed = 1;
}

/**
 * log_file_open:
 * @log: Log.
 *
 * Opens log file associated with @log if not already open.
 *
 * Returns: 0 on success, -1 on failure.
 **/
static int
log_file_open (Log *log)
{
	struct stat  statbuf;
	int          ret = -1;
	int          mode = LOG_DEFAULT_MODE;
	int          flags = (O_CREAT | O_APPEND | O_WRONLY |
			      O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);

	nih_assert (log);
	nih_assert (log->path);

	/* User job logging not currently available */
	nih_assert (log->uid == 0);

	memset (&statbuf, '\0', sizeof (struct stat));

	ret = fstat (log->fd, &statbuf);

	/* Already open */
	if (log->fd > -1 && (! ret && statbuf.st_nlink))
		return 0;

	/* File was deleted. This isn't a problem for
	 * the logger as it is happy to keep writing the
	 * unlinked file, but it *is* a problem for
	 * users who expect to see some data. Therefore,
	 * close the file and attempt to rewrite it.
	 *
	 * This behaviour also allows tools such as logrotate(8)
	 * to operate without disrupting the logger.
	 */
	if (log->fd > -1 && ! statbuf.st_nlink) {
		close (log->fd);
		log->fd = -1;
	}

	nih_assert (log->fd == -1);

	/* Impose some sane defaults. */
	umask (LOG_DEFAULT_UMASK);

	/* Non-blocking to avoid holding up the main loop. Without
	 * this, we'd probably need to spawn a thread to handle
	 * job logging.
	 */
	log->fd = open (log->path, flags, mode);

	log->open_errno = errno;

	/* Open may have failed due to path being unaccessible
	 * (disk might not be mounted yet).
	 */
	if (log->fd < 0)
		return -1;

	return 0;
}


/**
 * log_file_write:
 *
 * @log: Log,
 * @buf: buffer data is available in,
 * @len: bytes in @buf available for reading.
 *
 * Performs actual write to log file associated with @log. Note that
 * @buf can be NULL. If so, only unbuffered data will be written.
 * If @buf is NULL, @len is ignored.
 *
 * Special case: the filesystem is full. We have a few options,
 * none of them ideal. Part of the problem is that we cannot know
 * whether the problem *will be* transitory or not.
 *
 * Options:
 *
 * (a) Ignore the problem by simply continuing
 *     to buffer unflushed data (unsafe:
 *     eventually we'll run out of core and crash).
 * (b) Retain existing unflushed buffer, but
 *     disable further appends (not reasonable
 *     since if space does become available, the
 *     most recent data will have been
 *     discarded).
 * (c) Discard all unflushed data and store *all*
 *     new data as unflushed (potentially unsafe
 *     - in low disk space situations, programs may
 *     produce *more* log messages to
 *     stdout/stderr).
 * (d) Retain the last 'n' bytes of
 *     data (unflushed + new), where 'n' is some
 *     fixed value (ring buffer) What heuristic can
 *     we use to decide upon 'n'? Since this is
 *     output, 1024 bytes at 80 bytes per line
 *     gives a reasonable amount of context. However,
 *     if space later becomes available, there will
 *     be mysterious gaps in the log files.
 * (e) Discard all new data (not ideal, but safe)
 *
 * In the interests of self-preservation, we have to assume the
 * problem is *not* transitory and as such we opt for the safest
 * option, namely option 'e' since:
 *
 * - We cannot cache all data (option 'a') as it could
 *   result in a crash.
 * - Any other option which caches data will result in
 *   a corrupted log file should space later become
 *   available.
 *
 * Returns: 0 on success, -1 on failure.
 **/
static int
log_file_write (Log *log, const char *buf, size_t len)
{
	ssize_t      wlen = 0;
	NihIo       *io;
	int          saved;

	nih_assert (log);
	nih_assert (log->path);
	nih_assert (log->unflushed);
	nih_assert (log->fd != -1);

	/* User job logging not currently available */
	nih_assert (log->uid == 0);

	io = log->io;

	/* Flush any data we previously failed to write */
	if (log->unflushed->len) {
		wlen = write (log->fd, log->unflushed->buf, log->unflushed->len);
		saved = errno;

		if (wlen < 0) {
			/* Failed to flush the unflushed data, so unlikely to be
			 * able to flush the new data. Hence, add the new data
			 * to the unflushed buffer.
                         *
			 * If this fails, we still want to indicate an error
			 * condition, so no explicit return check.
			 *
			 * Note that data is always discarded when out of
			 * space.
			 */
			if (saved != ENOSPC && len
					&& nih_io_buffer_push (log->unflushed, buf, len) < 0)
				goto error;

			if (len)
				nih_io_buffer_shrink (io->recv_buf, len);

			/* Still need to indicate that the write failed */
			goto error;
		}

		nih_io_buffer_shrink (log->unflushed, (size_t)wlen);
	}

	/* Only managed a partial write for the unflushed data,
	 * so don't attempt to write the new data as that would
	 * leave a gap in the log. Just store the new data for
	 * next time.
	 */
	if (log->unflushed->len) {
		if (! len)
			goto error;

		/* Save new data */
		if (nih_io_buffer_push (log->unflushed, buf, len) < 0)
			goto error;

		nih_io_buffer_shrink (io->recv_buf, len);

		goto error;
	}

	if (! buf || ! len)
		return 0;

	/* Write the new data */
	wlen = write (log->fd, buf, len);
	saved = errno;

	if (wlen < 0) {
		if (saved != ENOSPC && nih_io_buffer_push (log->unflushed, buf, len) < 0)
			goto error;

		nih_io_buffer_shrink (io->recv_buf, len);

		goto error;
	}

	/* Shrink buffer by amount of data written (which handles
	 * partial writes)
	 */
	nih_io_buffer_shrink (io->recv_buf, (size_t)wlen);

	return 0;

error:
	close (log->fd);
	log->fd = -1;
	return -1;
}

/**
 * log_read_watch:
 *
 * @log: Log.
 *
 * Attempt a final read from the watch descriptor to ensure we've
 * drained all the data from the job.
 *
 * This can only legitimately be called after the associated primary job
 * process has finished.
 **/
void
log_read_watch (Log *log)
{
	NihIo   *io;
	ssize_t  len;
	int      saved;

	nih_assert (log);

	/* Must not be called if there is unflushed data as the log
	 * would then not be written in order.
	 */
	io = log->io;

	if (! io)
		return;

	/* Slurp up any remaining data from the job that is cached in
	 * the kernel. Keep reading until we get EOF or an error
	 * condition.
	 */
	while (1) {
		/* Ensure we have some space to read data from the job */
		if (nih_io_buffer_resize (io->recv_buf, LOG_READ_SIZE) < 0)
			break;

		errno = 0;

		/* Append to buffer */
		len = read (io->watch->fd,
				io->recv_buf->buf + io->recv_buf->len,
				io->recv_buf->size - io->recv_buf->len);
		saved = errno;

		if (len > 0)
			io->recv_buf->len += len;

		if (io->recv_buf->len)
			log_io_reader (log, io, io->recv_buf->buf, io->recv_buf->len);

		/* This scenario indicates the process that has now
		 * ended has leaked one or more file descriptors to a
		 * child process, and that child process is still
		 * running. We know this since EAGAIN/EWOULDBLOCK
		 * indicate there _may_ be further data to read in the
		 * future, but that isn't possible as the process we care
		 * about has now ended. Thus a leakage must have
		 * occured such that data may be available in the future
		 * _from some other process_ that is holding the fd(s) open.
		 *
		 * For daemons, this is generally a bug (see for example bug 926468).
		 *
		 * Only display the message in debug mode though since
		 * it is not unusual for script sections to leak fds.
		 */
		if (len < 0 && (saved == EAGAIN || saved == EWOULDBLOCK)) {
			nih_debug ("%s %s",
					"Process associated with log leaked a file descriptor",
					log->path);
		}

		/* Either the job process (remote) end of the pty has
		 * been closed, or there really is no (more) data to be read.
		 *
		 * If an error occurs, it is likely to be EIO (remote
		 * end closed) or EBADF (fd invalid if exec(3) failed).
		 * But erring on the side of caution, any unusual error
		 * causes the loop to be exited.
		 */
		if (len <= 0) {
			/* Job process has ended and we've drained all the data the job
			 * produced, so remote end must have closed.
			 *
			 * This cannot be handled entirely by log_io_error_handler()
			 * since the job may produce some output prior to disks being
			 * writeable, then end without producing further output.
			 * In this scenario the error handler is never called.
			 *
			 */
			if (saved && saved != EAGAIN && saved != EWOULDBLOCK)
				log->remote_closed = 1;

			close (log->fd);
			log->fd = -1;
			break;
		}
	}
}

/**
 * log_unflushed_init:
 *
 * Initialise the log_unflushed_files list.
 **/
void
log_unflushed_init (void)
{
	if (! log_unflushed_files)
		log_unflushed_files = NIH_MUST (nih_list_new (NULL));
}

/**
 * log_handle_unflushed:
 * @parent: parent of log,
 * @log: log.
 *
 * Potentially add specified log to list of unflushed log files
 * (for processing when a disk becomes writeable).
 *
 * This function should be called for each log object at the time the
 * associated process exits to ensure that all data from that process is
 * captured to the log.
 *
 * Returns: 0 on success (log added to list), 1 if log does not need to
 * be added to the list, or -1 on error.
 **/
int
log_handle_unflushed (void *parent, Log *log)
{
	NihListEntry  *elem;

	nih_assert (log);
	nih_assert (log->detached == 0);

	log_read_watch (log);

	if (! log->unflushed->len)
		return 1;

	if ((log->open_errno != EROFS && log->open_errno != EPERM
			&& log->open_errno != EACCES) || log_flushed)
		return 1;

	log_unflushed_init ();

	/* re-parent */
	nih_ref (log, log_unflushed_files);
	nih_unref (log, parent);

	elem = nih_list_entry_new (log);
	if (! elem) {
		/* If memory is low, we discard the unflushed
		 * data buffer too.
		 */
		nih_unref (log, log_unflushed_files);
		return -1;
	}

	/* Indicate separation from parent */
	log->detached = 1;

	elem->data = log;    
	nih_list_add_after (log_unflushed_files, &elem->entry);

	return 0;
}

/* log_clear_unflushed:
 *
 * Attempt to flush all unflushed log buffers to persistent storage.
 *
 * Call once the log disk partition is mounted as read-write.
 *
 * Returns: 0 on success, -1 on error.
 */
int
log_clear_unflushed (void)
{
	log_unflushed_init ();

	NIH_LIST_FOREACH_SAFE (log_unflushed_files, iter) {
		NihListEntry  *elem;
		Log           *log;

		elem = (NihListEntry *)iter;
		log = elem->data;

		/* To be added to this list, log should have been
		 * detached from its parent job.
		 */
		nih_assert (log->detached);

		/* We expect 'an' error (as otherwise why would the log be
		 * in this list?), but don't assert EROFS specifically
		 * as a precaution (since an attempt to flush the log at
		 * another time may result in some other errno value).
		 */
		nih_assert (log->open_errno);

		if (log->remote_closed) {
			/* Parent job has ended and unflushed data
			 * exists.
			 */
			nih_assert (log->unflushed->len);
			nih_assert (! log->io);
		} else {
			/* Parent job itself has ended, but job spawned one or
			 * more processes that are still running and
			 * which might still produce output (the error
			 * handler has therefore not been called).
			 */
			nih_assert (log->io);
		}

		if (log_file_open (log) != 0)
			return -1;

		if (log_file_write (log, NULL, 0) < 0)
			return -1;

		/* This will handle any remaining unflushed log data */
		nih_free (log);
	}

	log_flushed = 1;

	return 0;
}

/**
 * log_serialise:
 * @log: log to serialise.
 *
 * Convert @log into a JSON representation for serialisation.
 * Caller must free returned value using json_object_put().
 *
 * Returns: JSON-serialised Log object, or NULL on error.
 **/
json_object *
log_serialise (Log *log)
{
	json_object     *json;
	nih_local char  *unflushed_hex = NULL;

	json = json_object_new_object ();
	if (! json)
		return NULL;

	if (! log || (! log->io && log->unflushed && ! log->unflushed->len))
		goto placeholder;

	/* Attempt to flush any cached data */
	if (log->unflushed && log->unflushed->len) {
		/* Don't check return values since if this fails and
		 * unflushed data remains, we encode it below.
		 */
		if (log->fd < 0)
			(void)log_file_open (log);
		if (log->fd != -1)
			(void)log_file_write (log, NULL, 0);
	}

	/* Job associated with log has ended. If we failed to write
	 * unflushed data above, it will now be lost as we cannot
	 * create a valid serialisation without an associated NihIo.
	 */
	if (! log->io)
		goto placeholder;

	if (! state_set_json_int_var_from_obj (json, log, fd))
		goto error;

	nih_assert (log->io->watch);

	if (! state_set_json_int_var (json, "io_watch_fd", log->io->watch->fd))
		goto error;

	if (! state_set_json_string_var_from_obj (json, log, path))
		goto error;

	/* log->io itself is not encoded */

	if (! state_set_json_int_var_from_obj (json, log, uid))
		goto error;

	/* Encode unflushed data as hex to ensure any embedded
	 * nulls are handled.
	 */
	if (log->unflushed && log->unflushed->len) {
		unflushed_hex = state_data_to_hex (NULL,
				log->unflushed->buf,
				log->unflushed->len);

		if (! unflushed_hex)
			goto error;

		if (! state_set_json_string_var (json, "unflushed", unflushed_hex))
			goto error;
	}

	if (! state_set_json_int_var_from_obj (json, log, detached))
		goto error;

	if (! state_set_json_int_var_from_obj (json, log, remote_closed))
		goto error;

	if (! state_set_json_int_var_from_obj (json, log, open_errno))
		goto error;

	return json;

placeholder:
	/* Create a "placeholder" log object for non-existent
	 * log objects and for those that are no longer usable.
	 */
	if (! state_set_json_string_var (json, "path", NULL))
		goto error;
	return json;

error:
	json_object_put (json);
	return NULL;
}

/**
 * log_deserialise:
 * @json: JSON-serialised Log object to deserialise.
 *
 * Convert @json into a Log object.
 *
 * Returns: Log object, or NULL on error.
 **/
Log *
log_deserialise (const void *parent,
		 json_object *json)
{
	Log             *log;
	nih_local char  *unflushed_hex = NULL;
	nih_local char  *unflushed = NULL;
	int              ret;
	size_t           len;
	json_object     *json_unflushed;
	nih_local char  *path = NULL;
	int              io_watch_fd = -1;
	uid_t            uid = (uid_t)-1;

	nih_assert (json);

	log_unflushed_init ();

	if (! state_check_json_type (json, object))
		return NULL;

	if (! state_get_json_string_var (json, "path", NULL, path))
		return NULL;

	if (! path) {
		/* placeholder log object */
		return NULL;
	}

	if (! state_get_json_int_var (json, "io_watch_fd", io_watch_fd))
		return NULL;

	nih_assert (io_watch_fd != -1);

	/* re-apply CLOEXEC flag to stop job fd being leaked to children */
	if (state_toggle_cloexec (io_watch_fd, TRUE) < 0)
		return NULL;

	if (! state_get_json_int_var (json, "uid", uid))
		return NULL;

	log = log_new (parent, path, io_watch_fd, uid);
	if (! log)
		return NULL;

	if (! state_get_json_int_var_to_obj (json, log, fd))
		goto error;

	/* Re-apply CLOEXEC flag to stop log file fd being leaked to children.
	 *
	 * Note we discard return value since if this fails,
	 * we would never close the fd.
	 */
	if (log->fd != -1)
		(void)state_toggle_cloexec (log->fd, TRUE);

	log->unflushed = nih_io_buffer_new (log);
	if (! log->unflushed)
		goto error;

	json_unflushed = json_object_object_get (json, "unflushed");
	if (json_unflushed) {
		if (! state_get_json_string_var_strict (json, "unflushed", NULL, unflushed_hex))
			goto error;

		ret = state_hex_to_data (NULL,
				unflushed_hex,
				strlen (unflushed_hex),
				&unflushed,
				&len);

		if (ret < 0)
			goto error;

		if (nih_io_buffer_push (log->unflushed, unflushed, len) < 0)
			goto error;
	}

	if (! state_get_json_int_var_to_obj (json, log, detached))
		goto error;

	if (! state_get_json_int_var_to_obj (json, log, remote_closed))
		goto error;

	if (! state_get_json_int_var_to_obj (json, log, open_errno))
		goto error;

	return log;

error:
	nih_free (log);
	return NULL;
}
