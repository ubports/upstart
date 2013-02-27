/* upstart
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

#ifndef INIT_LOG_H
#define INIT_LOG_H

#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/io.h>
#include <nih/file.h>
#include <nih/string.h>
#include <nih/logging.h>
#include <nih/error.h>

#include "state.h"

/** LOG_DEFAULT_UMASK:
 *
 * The default file creation mask for log files.
 **/
#define LOG_DEFAULT_UMASK        (S_IXUSR | S_IXGRP | S_IRWXO)

/** LOG_DEFAULT_MODE:
 *
 * File creation mode for log files.
 **/
#define LOG_DEFAULT_MODE         (S_IRWXU | S_IRGRP)

/** LOG_READ_SIZE:
 *
 * Minimum buffer size for reading log data.
 **/
#define LOG_READ_SIZE            1024

/**
 * Log:
 *
 * @fd: Write file descriptor associated with @path,
 * @path: Full path to log file,
 * @io: NihIo associated with jobs stdout and stderr,
 * @uid: User ID of caller,
 * @unflushed: Unflushed data,
 * @detached: TRUE if log is no longer associated with a parent (job),
 * @remote_closed: TRUE if remote end of pty has been closed,
 * @open_errno: value of errno immediately after last attempt to open @path.
 **/
typedef struct log {
	int          fd;
	char        *path;
	NihIo       *io;
	uid_t        uid;
	NihIoBuffer *unflushed;
	int          detached;
	int          remote_closed;
	int          open_errno;
} Log;

NIH_BEGIN_EXTERN

extern NihList *log_unflushed_files;

Log  *log_new                (const void *parent, const char *path,
			      int fd, uid_t uid)
	__attribute__ ((warn_unused_result));
void  log_io_reader          (Log *log, NihIo *io, const char *buf, size_t len);
void  log_io_error_handler   (Log *log, NihIo *io);
int   log_destroy            (Log *log)
	__attribute__ ((warn_unused_result));
int   log_handle_unflushed   (void *parent, Log *log)
	__attribute__ ((warn_unused_result));
int   log_clear_unflushed    (void)
	__attribute__ ((warn_unused_result));
void  log_unflushed_init     (void);
json_object * log_serialise (Log *log)
	__attribute__ ((warn_unused_result));
Log * log_deserialise (const void *parent, json_object *json)
	__attribute__ ((warn_unused_result));

NIH_END_EXTERN

#endif /* INIT_LOG_H */
