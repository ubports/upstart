#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <string.h>

/* TEST_ENSURE_CLEAN_ENV:
 *
 * Ensure the environment is as pristine as possible (to avoid follow-on
 * errors caused by not freeing objects in a previous test, say)
 */
#define TEST_ENSURE_CLEAN_ENV()                                      \
{                                                                    \
	setvbuf(stdout, NULL, _IONBF, 0);                            \
                                                                     \
	if (job_classes) {                                           \
		TEST_HASH_EMPTY (job_classes);                       \
	}                                                            \
                                                                     \
	if (conf_sources) {                                          \
		TEST_LIST_EMPTY (conf_sources);                      \
	}                                                            \
                                                                     \
	if (nih_io_watches) {                                        \
		TEST_LIST_EMPTY (nih_io_watches);                    \
	}                                                            \
                                                                     \
	if (nih_timers) {                                            \
		TEST_LIST_EMPTY (nih_timers);                        \
	}                                                            \
                                                                     \
	if (events) {                                                \
		TEST_LIST_EMPTY (events);                            \
	}                                                            \
}

/**
 * _TEST_WATCH_UPDATE:
 * @force: if TRUE, force an update,
 * @timeout: struct timeval pointer, or NULL if no timeout required.
 *
 * Request NIH look for a file event relating to any NihIo objects,
 * with an optional timeout. Behaviour can be forced via @force.
 **/
#define _TEST_WATCH_UPDATE(force, timeout)                           \
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
	if (! force) {                                               \
	  ret = select (nfds, &readfds, &writefds,                   \
			&exceptfds, timeout);                        \
	}                                                            \
	if (force || ret > 0)                                        \
		nih_io_handle_fds (&readfds, &writefds, &exceptfds); \
}

/**
 * TEST_WATCH_UPDATE:
 *
 * Request NIH look for a file event relating to any NihIo objects,
 * */
#define TEST_WATCH_UPDATE()                                          \
	_TEST_WATCH_UPDATE (0, NULL)

/**
 * TEST_WATCH_UPDATE_TIMEOUT:
 * @timeout: struct timeval pointer.
 *
 * Request NIH look for a file event relating to any NihIo objects
 * within time period @timeout.
 **/
#define TEST_WATCH_UPDATE_TIMEOUT(timeout)                           \
	_TEST_WATCH_UPDATE (0, timeout)

/**
 * TEST_FORCE_WATCH_UPDATE:
 *
 * Force NIH to look for a file event relating to any NihIo objects.
 **/
#define TEST_FORCE_WATCH_UPDATE()                                    \
	_TEST_WATCH_UPDATE (1, NULL)

/**
 * TEST_FORCE_WATCH_UPDATE_TIMEOUT:
 * @timeout: struct timeval pointer.
 *
 * Force NIH to look for a file event relating to any NihIo objects
 * within time period @timeout.
 **/
#define TEST_FORCE_WATCH_UPDATE_TIMEOUT(timeout)                     \
	_TEST_WATCH_UPDATE (1, timeout)

/**
 * ENSURE_DIRECTORY_EMPTY:
 * @path: Full path to a directory.
 *
 * Ensure specified directory is empty.
 **/
#define ENSURE_DIRECTORY_EMPTY(path)                                 \
{                                                                    \
	DIR            *dp = NULL;                                   \
	struct dirent  *file = NULL;                                 \
	int             count = 0;                                   \
                                                                     \
	dp = opendir (path);                                         \
	TEST_NE_P (dp, NULL);                                        \
                                                                     \
	while((file = readdir (dp))) {                               \
		if (!strcmp (".", file->d_name) ||                   \
				!strcmp ("..", file->d_name))        \
			continue;                                    \
		count++;                                             \
	}                                                            \
                                                                     \
	closedir (dp);                                               \
                                                                     \
	TEST_EQ (count, 0);                                          \
}

/**
 * obj_string_check:
 *
 * @a: first object,
 * @b: second object,
 * @name: name of string element.
 *
 * Compare string element @name in objects @a and @b.
 *
 * Returns: 0 if strings are identical
 * (or both NULL), else 1.
 **/
#define obj_string_check(a, b, name) \
	string_check ((a)->name, (b)->name)

/**
 * obj_num_check:
 *
 * @a: first object,
 * @b: second object.
 * @name: name of numeric element.
 *
 * Compare numeric element @name in objects @a and @b.
 *
 * Returns: 0 if @a and @b are identical, else 1.
 **/
#define obj_num_check(a, b, name) \
	(a->name != b->name)

/* Prototypes */
int string_check (const char *a, const char *b);

#endif /* TEST_UTIL_H */
