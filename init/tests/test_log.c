/* upstart
 *
 * test_log.c - test suite for init/log.c
 *
 * Copyright © 2011 Canonical Ltd.
 * Author: James Hunt <james.hunt@canonical.com>
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
#include <errno.h>
#include <pty.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <nih/test.h>
#include <nih/timer.h>
#include <nih/child.h>
#include <nih/signal.h>
#include <nih/main.h>
#include "job.h"

/* Force an inotify watch update */
#define TEST_FORCE_WATCH_UPDATE()                                    \
{                                                                    \
	int         nfds = 0;                                        \
	int         ret = 0;                                         \
	fd_set      readfds, writefds, exceptfds;                    \
	                                                             \
	FD_ZERO (&readfds);                                          \
	FD_ZERO (&writefds);                                         \
	FD_ZERO (&exceptfds);                                        \
	                                                             \
	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);  \
	ret = select (nfds, &readfds, &writefds, &exceptfds, NULL);  \
	if (ret > 0)                                                 \
		nih_io_handle_fds (&readfds, &writefds, &exceptfds); \
}

/*
 * To help with understanding the TEST_ALLOC_FAIL peculiarities
 * below...
 *
 * log_new() calls __nih_*alloc() *seven* times:
 *
 * log_new
 *   log = nih_new
 *     nih_alloc
 *       __nih_alloc # XXX: call 1
 *   log->path = nih_strndup
 *     nih_alloc
 *       __nih_alloc # XXX: call 2
 *   log->unflushed = nih_io_buffer_new
 *     nih_new
 *       nih_alloc
 *         __nih_alloc # XXX: call 3
 *   log->io = nih_io_reopen
 *     io = nih_new
 *       __nih_alloc # XXX: call 4
 *     io->send_buf = nih_io_buffer_new
 *       nih_new
 *         __nih_alloc # XXX: call 5
 *     io->recv_buf = nih_io_buffer_new
 *       nih_new
 *         __nih_alloc # XXX: call 6
 *     io->watch = nih_io_add_watch
 *       nih_new
 *         __nih_alloc # XXX: call 7
 *
 * XXX: Unfortunately, having created a log, we cannot intelligently test the
 * memory failure handling of the asynchronously called log_io_reader() due to the
 * underlying complexities of the way NIH re-allocs memory at particular
 * points.
 */
#define LOG_NEW_ALLOC_CALLS    7
void
test_log_new (void)
{
	Log	        *log;
	char	         path[] = "/foo";
	char             str[] = "hello, world!";
	char             str2[] = "The end?";
	char	         filename[1024];
	char	         dirname[1024];
	char             buffer[1024];
	ssize_t          ret;
	ssize_t          bytes;
	struct stat      statbuf;
	FILE            *output;
	mode_t           old_perms;
	off_t            old_size;
	int              pty_master;
	int              pty_slave;

	TEST_FUNCTION ("log_new");

	TEST_FILENAME (filename);
	TEST_FILENAME (dirname);       
	TEST_EQ (mkdir (dirname, 0755), 0);
	TEST_EQ (setenv ("UPSTART_LOGDIR", dirname, 1), 0);


	/* XXX:
	 *
	 * It is *essential* we call this prior to any TEST_ALLOC_FAIL
	 * blocks since TEST_ALLOC_FAIL tracks calls to memory
	 * allocation routines and expects the function under test to
	 * call said routines *the same number of times* on each loop.
	 * NIH will attempt to initialise internal data structures
	 * lazily so force it to not be lazy to avoid surprises wrt
	 * number of malloc calls.
	 */
	nih_io_init ();
	nih_error_init ();

	/************************************************************/
	TEST_FEATURE ("object checks with uid 0");

	TEST_ALLOC_FAIL {
		TEST_EQ (openpty (&pty_master, &pty_slave, NULL, NULL, NULL), 0);
		log = log_new (NULL, path, pty_master, 0);

		/* Handle all alloc failures where the alloc calls were
                 * initiated by log_new().
                 */
		if (test_alloc_failed &&
				(test_alloc_failed <= LOG_NEW_ALLOC_CALLS)) {
 
			TEST_EQ_P (log, NULL);
			close (pty_master);
			close (pty_slave);
			continue;
		}

		TEST_ALLOC_SIZE (log, sizeof(Log));

		TEST_ALLOC_PARENT (log->io, log);
		TEST_ALLOC_SIZE (log->io, sizeof(NihIo));

		TEST_ALLOC_PARENT (log->path, log);

		TEST_EQ_STR (log->path, path);
		TEST_EQ (log->io->watch->fd, pty_master);
		TEST_EQ (log->uid, 0);
		TEST_LT (log->fd, 0);

		close (pty_slave);

		/* frees fds[0] */
		nih_free (log);
		log = NULL;
	}

	/************************************************************/
	/* XXX: No support for logging of user job output currently */
	TEST_FEATURE ("ensure logging disallowed for uid >0");

	TEST_EQ (openpty (&pty_master, &pty_slave, NULL, NULL, NULL), 0);

	log = log_new (NULL, path, pty_master, 1);
	TEST_EQ (log, NULL);

	close (pty_master);
	close (pty_slave);

	/************************************************************/
	TEST_FEATURE ("parent check");

	TEST_ALLOC_FAIL {
		char *string = NULL;
		TEST_ALLOC_SAFE {
			string = NIH_MUST (nih_strdup (NULL, str));
		}

		TEST_EQ (openpty (&pty_master, &pty_slave, NULL, NULL, NULL), 0);

		log = log_new (string, path, pty_master, 0);

		if (test_alloc_failed &&
				(test_alloc_failed <= LOG_NEW_ALLOC_CALLS)) {
			TEST_EQ_P (log, NULL);         
			close (pty_master);
			close (pty_slave);
			nih_free (string);
			continue;
		}

		TEST_NE_P (log, NULL);
		TEST_ALLOC_PARENT (log, string);
		TEST_FREE_TAG (log);

		close (pty_slave);

		/* Freeing the parent should free the child */
		nih_free (string);
		TEST_FREE (log);
	}

	/************************************************************/
	TEST_FEATURE ("writing a new log file with uid 0");

	TEST_ALLOC_FAIL {

		TEST_GT (sprintf (filename, "%s/test.log", dirname), 0);
		TEST_LT (stat (filename, &statbuf), 0);
		TEST_EQ (openpty (&pty_master, &pty_slave, NULL, NULL, NULL), 0);

		log = log_new (NULL, filename, pty_master, 0);

		/* First time through at this point only log_new() has been called.
                 * But by the end of the first loop, log_io_reader() will have
                 * been called twice.
                 */
		if (_test_alloc_call == 0)
			TEST_EQ (_test_alloc_count, LOG_NEW_ALLOC_CALLS);

		/* Handle all alloc failures where the alloc calls were
                 * initiated by log_new().
                 */
		if (test_alloc_failed &&
				(test_alloc_failed <= LOG_NEW_ALLOC_CALLS)) {
			TEST_EQ_P (log, NULL);         
			close (pty_master);
			close (pty_slave);
			continue;
		}

		TEST_NE_P (log, NULL);

		ret = write (pty_slave, str, strlen (str));
		TEST_GT (ret, 0);
		ret = write (pty_slave, "\n", 1);
		TEST_EQ (ret, 1);

		TEST_FORCE_WATCH_UPDATE ();

		/* Now handle all alloc failures where the alloc calls were
                 * initiated asynchronously by log_io_reader().
                 */
		if (test_alloc_failed == 1+LOG_NEW_ALLOC_CALLS) {
			TEST_NE_P (log, NULL);         
			close (pty_slave);
			nih_free (log);
			TEST_EQ (unlink (filename), 0);
			continue;
		}

		close (pty_slave);
		nih_free (log);

		TEST_EQ (stat (filename, &statbuf), 0);

		TEST_TRUE  (S_ISREG (statbuf.st_mode));
		TEST_TRUE  (statbuf.st_mode & S_IRUSR);
		TEST_TRUE  (statbuf.st_mode & S_IWUSR);
		TEST_FALSE (statbuf.st_mode & S_IXUSR);

		TEST_TRUE  (statbuf.st_mode & S_IRGRP);
		TEST_FALSE (statbuf.st_mode & S_IWGRP);
		TEST_FALSE (statbuf.st_mode & S_IXGRP);

		TEST_FALSE (statbuf.st_mode & S_IROTH);
		TEST_FALSE (statbuf.st_mode & S_IWOTH);
		TEST_FALSE (statbuf.st_mode & S_IXOTH);

		output = fopen (filename, "r");
		TEST_NE_P (output, NULL);

		TEST_FILE_EQ (output, "hello, world!\r\n");
		TEST_FILE_END (output);
		fclose (output);

		TEST_EQ (unlink (filename), 0);
	}

	/************************************************************/
	TEST_FEATURE ("same logger appending to file with uid 0");

	TEST_EQ (openpty (&pty_master, &pty_slave, NULL, NULL, NULL), 0);

	TEST_GT (sprintf (filename, "%s/test.log", dirname), 0);
	TEST_LT (stat (filename, &statbuf), 0);

	log = log_new (NULL, filename, pty_master, 0);
	TEST_NE_P (log, NULL);

	ret = write (pty_slave, str, strlen (str));
	TEST_GT (ret, 0);
	ret = write (pty_slave, "\n", 1);
	TEST_EQ (ret, 1);

	TEST_FORCE_WATCH_UPDATE ();

	TEST_EQ (stat (filename, &statbuf), 0);

	TEST_TRUE  (S_ISREG (statbuf.st_mode));
	TEST_TRUE  (statbuf.st_mode & S_IRUSR);
	TEST_TRUE  (statbuf.st_mode & S_IWUSR);
	TEST_FALSE (statbuf.st_mode & S_IXUSR);

	TEST_TRUE  (statbuf.st_mode & S_IRGRP);
	TEST_FALSE (statbuf.st_mode & S_IWGRP);
	TEST_FALSE (statbuf.st_mode & S_IXGRP);

	TEST_FALSE (statbuf.st_mode & S_IROTH);
	TEST_FALSE (statbuf.st_mode & S_IWOTH);
	TEST_FALSE (statbuf.st_mode & S_IXOTH);
	old_size  = statbuf.st_size;

	output = fopen (filename, "r");
	TEST_NE_P (output, NULL);

	TEST_FILE_EQ (output, "hello, world!\r\n");
	TEST_FILE_END (output);
	fclose (output);

	ret = write (pty_slave, str2, strlen (str2));
	TEST_GT (ret, 0);

	TEST_FORCE_WATCH_UPDATE ();

	TEST_EQ (stat (filename, &statbuf), 0);

	TEST_TRUE  (S_ISREG (statbuf.st_mode));
	TEST_TRUE  (statbuf.st_mode & S_IRUSR);
	TEST_TRUE  (statbuf.st_mode & S_IWUSR);
	TEST_FALSE (statbuf.st_mode & S_IXUSR);

	TEST_TRUE  (statbuf.st_mode & S_IRGRP);
	TEST_FALSE (statbuf.st_mode & S_IWGRP);
	TEST_FALSE (statbuf.st_mode & S_IXGRP);

	TEST_FALSE (statbuf.st_mode & S_IROTH);
	TEST_FALSE (statbuf.st_mode & S_IWOTH);
	TEST_FALSE (statbuf.st_mode & S_IXOTH);

	TEST_NE (statbuf.st_size, 0);
	TEST_EQ (statbuf.st_size, old_size + ret);

	output = fopen (filename, "r");
	TEST_NE_P (output, NULL);

	TEST_FILE_EQ (output, "hello, world!\r\n");
	TEST_FILE_EQ (output, str2);
	TEST_FILE_END (output);
	fclose (output);

	TEST_EQ (unlink (filename), 0);
	close (pty_slave);
	nih_free (log);

	/************************************************************/
	TEST_FEATURE ("different logger appending to file with uid 0");

	TEST_EQ (openpty (&pty_master, &pty_slave, NULL, NULL, NULL), 0);

	TEST_GT (sprintf (filename, "%s/test.log", dirname), 0);
	log = log_new (NULL, filename, pty_master, 0);
	TEST_NE_P (log, NULL);

	bytes = 0;
	ret = write (pty_slave, str, strlen (str));
	TEST_GT (ret, 0);
	bytes += ret;
	ret = write (pty_slave, "\n", 1);
	TEST_EQ (ret, 1);
	/* XXX: '+1' for '\r' */
	bytes += (ret+1);

	TEST_FORCE_WATCH_UPDATE ();

	TEST_EQ (stat (filename, &statbuf), 0);

	TEST_TRUE  (S_ISREG (statbuf.st_mode));
	TEST_TRUE  (statbuf.st_mode & S_IRUSR);
	TEST_TRUE  (statbuf.st_mode & S_IWUSR);
	TEST_FALSE (statbuf.st_mode & S_IXUSR);

	TEST_TRUE  (statbuf.st_mode & S_IRGRP);
	TEST_FALSE (statbuf.st_mode & S_IWGRP);
	TEST_FALSE (statbuf.st_mode & S_IXGRP);

	TEST_FALSE (statbuf.st_mode & S_IROTH);
	TEST_FALSE (statbuf.st_mode & S_IWOTH);
	TEST_FALSE (statbuf.st_mode & S_IXOTH);

	old_size  = statbuf.st_size;
	TEST_EQ (old_size, bytes);

	output = fopen (filename, "r");
	TEST_NE_P (output, NULL);

	TEST_FILE_EQ (output, "hello, world!\r\n");
	TEST_FILE_END (output);
	fclose (output);

	close (pty_slave);
	nih_free (log);

	TEST_EQ (openpty (&pty_master, &pty_slave, NULL, NULL, NULL), 0);

	log = log_new (NULL, filename, pty_master, 0);
	TEST_NE_P (log, NULL);

	TEST_EQ (stat (filename, &statbuf), 0);

	TEST_TRUE  (S_ISREG (statbuf.st_mode));
	TEST_TRUE  (statbuf.st_mode & S_IRUSR);
	TEST_TRUE  (statbuf.st_mode & S_IWUSR);
	TEST_FALSE (statbuf.st_mode & S_IXUSR);

	TEST_TRUE  (statbuf.st_mode & S_IRGRP);
	TEST_FALSE (statbuf.st_mode & S_IWGRP);
	TEST_FALSE (statbuf.st_mode & S_IXGRP);

	TEST_FALSE (statbuf.st_mode & S_IROTH);
	TEST_FALSE (statbuf.st_mode & S_IWOTH);
	TEST_FALSE (statbuf.st_mode & S_IXOTH);

	TEST_EQ (statbuf.st_size, old_size);

	bytes = 0;
	ret = write (pty_slave, str2, strlen (str2));
	TEST_GT (ret, 0);
	bytes += ret;
	ret = write (pty_slave, "\n", 1);
	TEST_EQ (ret, 1);
	/* '+1' for '\r' */
	bytes += (1+ret);

	TEST_FORCE_WATCH_UPDATE ();

	close (pty_slave);
	nih_free (log);

	TEST_EQ (stat (filename, &statbuf), 0);

	TEST_TRUE  (S_ISREG (statbuf.st_mode));
	TEST_TRUE  (statbuf.st_mode & S_IRUSR);
	TEST_TRUE  (statbuf.st_mode & S_IWUSR);
	TEST_FALSE (statbuf.st_mode & S_IXUSR);

	TEST_TRUE  (statbuf.st_mode & S_IRGRP);
	TEST_FALSE (statbuf.st_mode & S_IWGRP);
	TEST_FALSE (statbuf.st_mode & S_IXGRP);

	TEST_FALSE (statbuf.st_mode & S_IROTH);
	TEST_FALSE (statbuf.st_mode & S_IWOTH);
	TEST_FALSE (statbuf.st_mode & S_IXOTH);

	TEST_EQ (statbuf.st_size, old_size + bytes);

	output = fopen (filename, "r");
	TEST_NE_P (output, NULL);

	TEST_FILE_EQ (output, "hello, world!\r\n");
	TEST_FILE_EQ (output, "The end?\r\n");
	TEST_FILE_END (output);
	fclose (output);

	TEST_EQ (unlink (filename), 0);

	/************************************************************/
	TEST_FEATURE ("ensure logging resumes when file made accessible with uid 0");

	TEST_EQ (openpty (&pty_master, &pty_slave, NULL, NULL, NULL), 0);

	TEST_GT (sprintf (filename, "%s/test.log", dirname), 0);
	log = log_new (NULL, filename, pty_master, 0);
	TEST_NE_P (log, NULL);

	ret = write (pty_slave, str, strlen (str));
	TEST_GT (ret, 0);
	ret = write (pty_slave, "\n", 1);
	TEST_EQ (ret, 1);

	TEST_FORCE_WATCH_UPDATE ();

	TEST_EQ (stat (filename, &statbuf), 0);

	TEST_TRUE  (S_ISREG (statbuf.st_mode));
	TEST_TRUE  (statbuf.st_mode & S_IRUSR);
	TEST_TRUE  (statbuf.st_mode & S_IWUSR);
	TEST_FALSE (statbuf.st_mode & S_IXUSR);

	TEST_TRUE  (statbuf.st_mode & S_IRGRP);
	TEST_FALSE (statbuf.st_mode & S_IWGRP);
	TEST_FALSE (statbuf.st_mode & S_IXGRP);

	TEST_FALSE (statbuf.st_mode & S_IROTH);
	TEST_FALSE (statbuf.st_mode & S_IWOTH);
	TEST_FALSE (statbuf.st_mode & S_IXOTH);

	/* Save */
	old_perms = statbuf.st_mode;

	old_size  = statbuf.st_size;

	output = fopen (filename, "r");
	TEST_NE_P (output, NULL);

	TEST_FILE_EQ (output, "hello, world!\r\n");
	TEST_FILE_END (output);
	fclose (output);

	/* Make file inaccessible */
	TEST_EQ (chmod (filename, 0x0), 0);

	/* Send more data to logger */
	ret = write (pty_slave, str2, strlen (str2));
	TEST_GT (ret, 0);
	ret = write (pty_slave, "\n", 1);
	TEST_EQ (ret, 1);

	/* File shouldn't have changed */
	TEST_EQ (stat (filename, &statbuf), 0);
	TEST_EQ (statbuf.st_size, old_size);

	/* Restore access */
	TEST_EQ (chmod (filename, old_perms), 0);

	/* Further data should cause previous data that could not be
	 * written to be flushed to the file.
	 */
	ret = write (pty_slave, "foo\n", 4);
	TEST_EQ (ret, 4);

	TEST_FORCE_WATCH_UPDATE ();

	close (pty_slave);
	nih_free (log);

	TEST_EQ (stat (filename, &statbuf), 0);
	TEST_GT (statbuf.st_size, old_size);

	TEST_TRUE  (S_ISREG (statbuf.st_mode));
	TEST_TRUE  (statbuf.st_mode & S_IRUSR);
	TEST_TRUE  (statbuf.st_mode & S_IWUSR);
	TEST_FALSE (statbuf.st_mode & S_IXUSR);

	TEST_TRUE  (statbuf.st_mode & S_IRGRP);
	TEST_FALSE (statbuf.st_mode & S_IWGRP);
	TEST_FALSE (statbuf.st_mode & S_IXGRP);

	TEST_FALSE (statbuf.st_mode & S_IROTH);
	TEST_FALSE (statbuf.st_mode & S_IWOTH);
	TEST_FALSE (statbuf.st_mode & S_IXOTH);

	output = fopen (filename, "r");
	TEST_NE_P (output, NULL);

	/* Re-check entire file contents */
	TEST_FILE_EQ (output, "hello, world!\r\n");
	TEST_FILE_EQ (output, "The end?\r\n");
	TEST_FILE_EQ (output, "foo\r\n");
	TEST_FILE_END (output);
	fclose (output);

	TEST_EQ (unlink (filename), 0);

	/************************************************************/
	TEST_FEATURE ("ensure logger flushes when destroyed with uid 0");

	TEST_EQ (openpty (&pty_master, &pty_slave, NULL, NULL, NULL), 0);

	TEST_GT (sprintf (filename, "%s/test.log", dirname), 0);

	TEST_EQ (rmdir (dirname), 0);

	log = log_new (NULL, filename, pty_master, 0);
	TEST_NE_P (log, NULL);

	ret = write (pty_slave, str, strlen (str));
	TEST_GT (ret, 0);
	ret = write (pty_slave, "\n", 1);
	TEST_EQ (ret, 1);

	TEST_FORCE_WATCH_UPDATE ();

	old_perms = umask (0);
	TEST_EQ (mkdir (dirname, 0755), 0);
	umask (old_perms);

	/* No more data sent to ensure logger writes it on log destroy */
	close (pty_slave);
	nih_free (log);

	output = fopen (filename, "r");
	TEST_NE_P (output, NULL);

	TEST_EQ (stat (filename, &statbuf), 0);

	TEST_TRUE  (S_ISREG (statbuf.st_mode));
	TEST_TRUE  (statbuf.st_mode & S_IRUSR);
	TEST_TRUE  (statbuf.st_mode & S_IWUSR);
	TEST_FALSE (statbuf.st_mode & S_IXUSR);

	TEST_TRUE  (statbuf.st_mode & S_IRGRP);
	TEST_FALSE (statbuf.st_mode & S_IWGRP);
	TEST_FALSE (statbuf.st_mode & S_IXGRP);

	TEST_FALSE (statbuf.st_mode & S_IROTH);
	TEST_FALSE (statbuf.st_mode & S_IWOTH);
	TEST_FALSE (statbuf.st_mode & S_IXOTH);
	TEST_FILE_EQ (output, "hello, world!\r\n");
	TEST_FILE_END (output);
	fclose (output);

	TEST_EQ (unlink (filename), 0);

	/************************************************************/
	TEST_FEATURE ("ensure log written when directory created accessible with uid 0");

	TEST_EQ (openpty (&pty_master, &pty_slave, NULL, NULL, NULL), 0);

	TEST_GT (sprintf (filename, "%s/test.log", dirname), 0);

	TEST_EQ (rmdir (dirname), 0);

	log = log_new (NULL, filename, pty_master, 0);
	TEST_NE_P (log, NULL);

	ret = write (pty_slave, str, strlen (str));
	TEST_GT (ret, 0);
	ret = write (pty_slave, "\n", 1);
	TEST_EQ (ret, 1);

	old_perms = umask (0);
	TEST_EQ (mkdir (dirname, 0755), 0);
	umask (old_perms);

	/* Send more data */
	ret = write (pty_slave, str2, strlen (str2));
	TEST_GT (ret, 0);
	ret = write (pty_slave, "\n", 1);
	TEST_EQ (ret, 1);

	close (pty_slave);
	nih_free (log);

	output = fopen (filename, "r");
	TEST_NE_P (output, NULL);

	TEST_EQ (stat (filename, &statbuf), 0);

	TEST_TRUE  (S_ISREG (statbuf.st_mode));
	TEST_TRUE  (statbuf.st_mode & S_IRUSR);
	TEST_TRUE  (statbuf.st_mode & S_IWUSR);
	TEST_FALSE (statbuf.st_mode & S_IXUSR);

	TEST_TRUE  (statbuf.st_mode & S_IRGRP);
	TEST_FALSE (statbuf.st_mode & S_IWGRP);
	TEST_FALSE (statbuf.st_mode & S_IXGRP);

	TEST_FALSE (statbuf.st_mode & S_IROTH);
	TEST_FALSE (statbuf.st_mode & S_IWOTH);
	TEST_FALSE (statbuf.st_mode & S_IXOTH);
	TEST_FILE_EQ (output, "hello, world!\r\n");
	TEST_FILE_EQ (output, "The end?\r\n");
	TEST_FILE_END (output);
	fclose (output);

	TEST_EQ (unlink (filename), 0);

	/************************************************************/
	TEST_FEATURE ("ensure remainder of log written when file deleted with uid 0");

	TEST_EQ (openpty (&pty_master, &pty_slave, NULL, NULL, NULL), 0);

	TEST_GT (sprintf (filename, "%s/test.log", dirname), 0);

	log = log_new (NULL, filename, pty_master, 0);
	TEST_NE_P (log, NULL);

	ret = write (pty_slave, str, strlen (str));
	TEST_GT (ret, 0);
	ret = write (pty_slave, "\n", 1);
	TEST_EQ (ret, 1);

	TEST_FORCE_WATCH_UPDATE ();

	output = fopen (filename, "r");
	TEST_NE_P (output, NULL);

	TEST_EQ (stat (filename, &statbuf), 0);

	TEST_TRUE  (S_ISREG (statbuf.st_mode));
	TEST_TRUE  (statbuf.st_mode & S_IRUSR);
	TEST_TRUE  (statbuf.st_mode & S_IWUSR);
	TEST_FALSE (statbuf.st_mode & S_IXUSR);

	TEST_TRUE  (statbuf.st_mode & S_IRGRP);
	TEST_FALSE (statbuf.st_mode & S_IWGRP);
	TEST_FALSE (statbuf.st_mode & S_IXGRP);

	TEST_FALSE (statbuf.st_mode & S_IROTH);
	TEST_FALSE (statbuf.st_mode & S_IWOTH);
	TEST_FALSE (statbuf.st_mode & S_IXOTH);
	TEST_EQ (fstat (log->fd, &statbuf), 0);

	TEST_FILE_EQ (output, "hello, world!\r\n");
	TEST_FILE_END (output);
	fclose (output);

	TEST_EQ (unlink (filename), 0);

	TEST_EQ (fstat (log->fd, &statbuf), 0);

	/* Send more data */
	ret = write (pty_slave, str2, strlen (str2));
	TEST_GT (ret, 0);
	ret = write (pty_slave, "\n", 1);
	TEST_EQ (ret, 1);

	TEST_FORCE_WATCH_UPDATE ();

	output = fopen (filename, "r");
	TEST_NE_P (output, NULL);

	TEST_EQ (stat (filename, &statbuf), 0);

	TEST_TRUE  (S_ISREG (statbuf.st_mode));
	TEST_TRUE  (statbuf.st_mode & S_IRUSR);
	TEST_TRUE  (statbuf.st_mode & S_IWUSR);
	TEST_FALSE (statbuf.st_mode & S_IXUSR);

	TEST_TRUE  (statbuf.st_mode & S_IRGRP);
	TEST_FALSE (statbuf.st_mode & S_IWGRP);
	TEST_FALSE (statbuf.st_mode & S_IXGRP);

	TEST_FALSE (statbuf.st_mode & S_IROTH);
	TEST_FALSE (statbuf.st_mode & S_IWOTH);
	TEST_FALSE (statbuf.st_mode & S_IXOTH);
	TEST_FILE_EQ (output, "The end?\r\n");
	TEST_FILE_END (output);
	fclose (output);

	close (pty_slave);
	nih_free (log);
	TEST_EQ (unlink (filename), 0);

	/************************************************************/
	TEST_FEATURE ("writing 1 null with uid 0");

	TEST_EQ (openpty (&pty_master, &pty_slave, NULL, NULL, NULL), 0);

	TEST_GT (sprintf (filename, "%s/test.log", dirname), 0);
	TEST_LT (stat (filename, &statbuf), 0);

	log = log_new (NULL, filename, pty_master, 0);
	TEST_NE_P (log, NULL);

	ret = write (pty_slave, "\000", 1);
	TEST_EQ (ret, 1);

	TEST_FORCE_WATCH_UPDATE ();

	close (pty_slave);
	nih_free (log);

	TEST_EQ (stat (filename, &statbuf), 0);

	TEST_TRUE  (S_ISREG (statbuf.st_mode));
	TEST_TRUE  (statbuf.st_mode & S_IRUSR);
	TEST_TRUE  (statbuf.st_mode & S_IWUSR);
	TEST_FALSE (statbuf.st_mode & S_IXUSR);

	TEST_TRUE  (statbuf.st_mode & S_IRGRP);
	TEST_FALSE (statbuf.st_mode & S_IWGRP);
	TEST_FALSE (statbuf.st_mode & S_IXGRP);

	TEST_FALSE (statbuf.st_mode & S_IROTH);
	TEST_FALSE (statbuf.st_mode & S_IWOTH);
	TEST_FALSE (statbuf.st_mode & S_IXOTH);
	TEST_EQ (statbuf.st_size, 1);

	output = fopen (filename, "r");
	TEST_NE_P (output, NULL);

	TEST_FILE_EQ (output, "");
	TEST_FILE_END (output);
	fclose (output);

	TEST_EQ (unlink (filename), 0);

	/************************************************************/
	TEST_FEATURE ("writing >1 null with uid 0");

	TEST_EQ (openpty (&pty_master, &pty_slave, NULL, NULL, NULL), 0);

	TEST_GT (sprintf (filename, "%s/test.log", dirname), 0);
	TEST_LT (stat (filename, &statbuf), 0);

	log = log_new (NULL, filename, pty_master, 0);
	TEST_NE_P (log, NULL);

	ret = write (pty_slave, "\000\000\000", 3);
	TEST_EQ (ret, 3);

	TEST_FORCE_WATCH_UPDATE ();

	close (pty_slave);
	nih_free (log);

	TEST_EQ (stat (filename, &statbuf), 0);

	TEST_TRUE  (S_ISREG (statbuf.st_mode));
	TEST_TRUE  (statbuf.st_mode & S_IRUSR);
	TEST_TRUE  (statbuf.st_mode & S_IWUSR);
	TEST_FALSE (statbuf.st_mode & S_IXUSR);

	TEST_TRUE  (statbuf.st_mode & S_IRGRP);
	TEST_FALSE (statbuf.st_mode & S_IWGRP);
	TEST_FALSE (statbuf.st_mode & S_IXGRP);

	TEST_FALSE (statbuf.st_mode & S_IROTH);
	TEST_FALSE (statbuf.st_mode & S_IWOTH);
	TEST_FALSE (statbuf.st_mode & S_IXOTH);
	TEST_EQ (statbuf.st_size, 3);

	output = fopen (filename, "r");
	TEST_NE_P (output, NULL);

	TEST_FILE_EQ (output, "\000\000\000");
	TEST_FILE_END (output);
	fclose (output);

	TEST_EQ (unlink (filename), 0);

	/************************************************************/
	TEST_FEATURE ("writing 1 non-printable only with uid 0");

	TEST_EQ (openpty (&pty_master, &pty_slave, NULL, NULL, NULL), 0);

	TEST_GT (sprintf (filename, "%s/test.log", dirname), 0);
	TEST_LT (stat (filename, &statbuf), 0);

	log = log_new (NULL, filename, pty_master, 0);
	TEST_NE_P (log, NULL);

	ret = write (pty_slave, " ", 1);
	TEST_EQ (ret, 1);

	TEST_FORCE_WATCH_UPDATE ();

	close (pty_slave);
	nih_free (log);

	TEST_EQ (stat (filename, &statbuf), 0);

	TEST_TRUE  (S_ISREG (statbuf.st_mode));
	TEST_TRUE  (statbuf.st_mode & S_IRUSR);
	TEST_TRUE  (statbuf.st_mode & S_IWUSR);
	TEST_FALSE (statbuf.st_mode & S_IXUSR);

	TEST_TRUE  (statbuf.st_mode & S_IRGRP);
	TEST_FALSE (statbuf.st_mode & S_IWGRP);
	TEST_FALSE (statbuf.st_mode & S_IXGRP);

	TEST_FALSE (statbuf.st_mode & S_IROTH);
	TEST_FALSE (statbuf.st_mode & S_IWOTH);
	TEST_FALSE (statbuf.st_mode & S_IXOTH);
	TEST_EQ (statbuf.st_size, 1);

	output = fopen (filename, "r");
	TEST_NE_P (output, NULL);

	TEST_FILE_EQ (output, " ");
	TEST_FILE_END (output);
	fclose (output);

	TEST_EQ (unlink (filename), 0);

	/************************************************************/
	TEST_FEATURE ("writing >1 non-printable only with uid 0");

	TEST_EQ (openpty (&pty_master, &pty_slave, NULL, NULL, NULL), 0);

	TEST_GT (sprintf (filename, "%s/test.log", dirname), 0);
	TEST_LT (stat (filename, &statbuf), 0);

	log = log_new (NULL, filename, pty_master, 0);
	TEST_NE_P (log, NULL);

	ret = write (pty_slave, "\n \t", 3);
	TEST_EQ (ret, 3);

	TEST_FORCE_WATCH_UPDATE ();

	close (pty_slave);
	nih_free (log);

	TEST_EQ (stat (filename, &statbuf), 0);

	TEST_TRUE  (S_ISREG (statbuf.st_mode));
	TEST_TRUE  (statbuf.st_mode & S_IRUSR);
	TEST_TRUE  (statbuf.st_mode & S_IWUSR);
	TEST_FALSE (statbuf.st_mode & S_IXUSR);

	TEST_TRUE  (statbuf.st_mode & S_IRGRP);
	TEST_FALSE (statbuf.st_mode & S_IWGRP);
	TEST_FALSE (statbuf.st_mode & S_IXGRP);

	TEST_FALSE (statbuf.st_mode & S_IROTH);
	TEST_FALSE (statbuf.st_mode & S_IWOTH);
	TEST_FALSE (statbuf.st_mode & S_IXOTH);

	/* '\r', '\n', ' ', '\t' */
	TEST_EQ (statbuf.st_size, 4);

	output = fopen (filename, "r");
	TEST_NE_P (output, NULL);

	TEST_EQ (fread (buffer, 1, 4,  output), 4);
	TEST_EQ (buffer[0], '\r');
	TEST_EQ (buffer[1], '\n');
	TEST_EQ (buffer[2], ' ');
	TEST_EQ (buffer[3], '\t');

	TEST_FILE_END (output);
	fclose (output);

	TEST_EQ (unlink (filename), 0);

	/************************************************************/
	TEST_FEATURE ("with very long relative path and uid 0");
	{
		int len = 0;

		/* Recall that PATH_MAX includes the terminating null
		 * and refers to a _relative path_.
		 */
		char long_path[PATH_MAX];

		len = sprintf (long_path, "%s", "../tmp/");
		TEST_GT (len, 0);

		long_path[sizeof(long_path)-1] = '\0';

		memset (long_path+len, 'J', sizeof(long_path)-len-1);

		nih_debug("long_path='%s'", long_path);

		pty_master = -1; pty_slave = -1;
		TEST_EQ (openpty (&pty_master, &pty_slave, NULL, NULL, NULL), 0);

		log = log_new (NULL, long_path, pty_master, 0);
		TEST_NE_P (log, NULL);

		close (pty_slave);
		pty_slave = -1;
		nih_free (log);
	}

	/************************************************************/
	TEST_FEATURE ("with overly long relative path and uid 0");
	{
		int len = 0;

		/* Recall that PATH_MAX includes the terminating null
		 * and refers to a _relative path_.
		 */
		char illegal_path[PATH_MAX+1];

		len = sprintf (illegal_path, "%s", "../tmp/");
		TEST_GT (len, 0);

		illegal_path[sizeof(illegal_path)-1] = '\0';

		memset (illegal_path+len, 'z', sizeof(illegal_path)-len-1);

		TEST_EQ (openpty (&pty_master, &pty_slave, NULL, NULL, NULL), 0);

		log = log_new (NULL, illegal_path, pty_master, 0);
		TEST_EQ_P (log, NULL);

		close (pty_master);
		close (pty_slave);
	}

	/************************************************************/
	TEST_FEATURE ("with very long absolute path and uid 0");
	{
		int len = 0;

		char long_path[_POSIX_PATH_MAX];

		len = sprintf (long_path, "%s", "/tmp/");
		TEST_GT (len, 0);

		long_path[sizeof(long_path)-1] = '\0';

		memset (long_path+len, 'J', sizeof(long_path)-len-1);

		TEST_EQ (openpty (&pty_master, &pty_slave, NULL, NULL, NULL), 0);

		log = log_new (NULL, long_path, pty_master, 0);
		TEST_NE_P (log, NULL);

		close (pty_slave);
		nih_free (log);
	}

	/************************************************************/
	TEST_FEATURE ("with overly long absolute path and uid 0");
	{
		int len = 0;

		char illegal_path[_POSIX_PATH_MAX+1];

		len = sprintf (illegal_path, "%s", "/tmp/");
		TEST_GT (len, 0);

		illegal_path[sizeof(illegal_path)-1] = '\0';

		memset (illegal_path+len, 'z', sizeof(illegal_path)-len-1);

		TEST_EQ (openpty (&pty_master, &pty_slave, NULL, NULL, NULL), 0);

		log = log_new (NULL, illegal_path, pty_master, 0);
		TEST_EQ_P (log, NULL);

		close (pty_master);
		close (pty_slave);
	}

	/************************************************************/
	/* Tidy up */

	TEST_EQ (rmdir (dirname), 0);
	TEST_EQ (unsetenv ("UPSTART_LOGDIR"), 0);
}

void
test_log_destroy (void)
{
	Log  *log;
	int   ret;
	int   flags;
	char  str[] = "hello, world!";
	int   pty_master;
	int   pty_slave;

	TEST_FUNCTION ("log_destroy");

	/************************************************************/
	TEST_FEATURE ("ensure log fd closed with uid 0");

	TEST_EQ (openpty (&pty_master, &pty_slave, NULL, NULL, NULL), 0);

	flags = fcntl (pty_master, F_GETFL);
	TEST_NE (flags, -1);

	log = log_new (NULL, "/foo", pty_master, 0);
	TEST_NE_P (log, NULL);

	close (pty_slave);
	nih_free (log);

	flags = fcntl (pty_master, F_GETFL);
	TEST_EQ (flags, -1);
	TEST_EQ (errno, EBADF);

	/************************************************************/
	TEST_FEATURE ("ensure path and io elements freed with uid 0");

	TEST_EQ (openpty (&pty_master, &pty_slave, NULL, NULL, NULL), 0);

	log = log_new (NULL, "/bar", pty_master, 0);
	TEST_NE_P (log, NULL);

	TEST_FREE_TAG (log->path);
	TEST_FREE_TAG (log->io);

	close (pty_slave);
	nih_free (log);

	TEST_FREE (log->path);
	TEST_FREE (log->io);

	/************************************************************/
	TEST_FEATURE ("ensure unflushed data freed with uid 0");

	TEST_EQ (openpty (&pty_master, &pty_slave, NULL, NULL, NULL), 0);

	log = log_new (NULL, "/bar", pty_master, 0);
	TEST_NE_P (log, NULL);

	ret = write (pty_slave, str, strlen (str));
	TEST_GT (ret, 0);

	TEST_FORCE_WATCH_UPDATE ();
	TEST_NE_P (log->unflushed, NULL);
	TEST_EQ (log->unflushed->len, strlen(str));
	TEST_EQ_STR (log->unflushed->buf, str);
	TEST_FREE_TAG (log->unflushed);

	close (pty_slave);
	nih_free (log);
	TEST_FREE (log->unflushed);
}


	int
main (int   argc,
		char *argv[])
{
	/* run tests in legacy (pre-session support) mode */
	setenv ("UPSTART_NO_SESSIONS", "1", 1);

	test_log_new ();
	test_log_destroy ();

	return 0;
}
