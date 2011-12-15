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
#include <sys/prctl.h>

static int  log_file_open   (Log *log);
static int  log_file_write  (Log *log, const char *buf, size_t len);

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

	log->fd          = -1;
	log->uid         = uid;
	log->unflushed   = NULL;
	log->io          = NULL;

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
 **/
int
log_destroy (Log *log)
{
	int ret;

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
		if (ret < 0)
			goto out;
	}

	/* Force file to flush
	 *
	 * Only attempt this for jobs which the current process is
	 * in control of.
	 */
	if (log->fd > 0)
		close (log->fd);

out:
	log->fd = -1;
	return 0;
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

	/* User job logging not currently available */
	nih_assert (log->uid == 0);

	/* Note we don't assert @len in case we are being called after
	 * an error is detected (where there is no new data, but may be
	 * unflushed data).
	 */

	/* Just in case we try to write more than read can inform us
	 * about (this should really be a build-time assertion).
	 */
	nih_assert (sizeof (size_t) == sizeof (ssize_t));

	if (log_file_open (log) < 0) {
		if (errno == ENOSPC) {
			/* Always discard when out of space */
			nih_io_buffer_shrink (io->recv_buf, len);
			return;
		} else {
			/* Add new data to unflushed buffer */
			if (nih_io_buffer_push (log->unflushed, buf, len) < 0)
				return;

			nih_io_buffer_shrink (io->recv_buf, len);
			/* No point attempting to write if we cannot
			 * open the file.
			 */
			return;
		}
	}

	ret = log_file_write (log, buf, len);
	if (ret < 0) {
		nih_warn ("%s %s", _("Failed to write to log file"), log->path);
	}
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

	if (log->io) {
		/* Close the connection */
		nih_free (log->io);
	}
}

/**
 * log_file_open:
 * @log: Log.
 *
 * Opens log file associated with @log if not already open.
 *
 * Returns 0 on success, -1 on failure.
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
	if (log->fd > -1 && (!ret && statbuf.st_nlink))
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
 * Returns 0 on success, -1 on failure.
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
			if (saved != ENOSPC && nih_io_buffer_push (log->unflushed, buf, len) < 0)
				goto error;

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

