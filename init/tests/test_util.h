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
 * TEST_WATCH_UPDATE_TIMEOUT_SECS:
 * @secs: seconds to wait before timeout.
 *
 * Request NIH look for a file event relating to any NihIo objects
 * within @secs timeout.
 **/
#define TEST_WATCH_UPDATE_TIMEOUT_SECS(secs)                         \
{                                                                    \
	struct timeval _t;                                           \
	_t.tv_sec  = secs;                                           \
	_t.tv_usec = 0;                                              \
	_TEST_WATCH_UPDATE (0, &_t);                                 \
}

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
 * TEST_FORCE_WATCH_UPDATE_TIMEOUT_SECS:
 * @timeout: struct timeval pointer.
 *
 * Force NIH to look for a file event relating to any NihIo objects
 * within time period @timeout.
 **/
#define TEST_FORCE_WATCH_UPDATE_TIMEOUT_SECS(secs)                   \
{                                                                    \
	struct timeval _t;                                           \
	_t.tv_sec  = secs;                                           \
	_t.tv_usec = 0;                                              \
	_TEST_WATCH_UPDATE (1, &_t);                                 \
}

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

/**
 * TEST_CMP_INT_ARRAYS:
 * @a: first array,
 * @b: second array,
 * @sizea: size of @a,
 * @sizeb: size of @b.
 *
 * Compare integer arrays @a and @b for equivalence.
 *
 * Returns: 0 if arrays are identical, else -1.
 **/
#define TEST_CMP_INT_ARRAYS(a, b, sizea, sizeb) \
({int ret = 0; \
 size_t __i; \
 if (sizea == sizeb) { \
	 for (__i = 0; \
		 __i < sizea; \
		 __i++) { \
	 	if ((a)[__i] != (b)[__i]) { \
 			ret = -1; \
 			break; \
 		} \
 	} \
 } else \
 	ret = -1; \
 ret;})

/**
 * TEST_CMP_STR_ARRAYS:
 * @a: first string array,
 * @b: second string array,
 * @sizea: length of @a, 
 * @sizeb: length of @b.
 *
 * Compare string arrays @a and @b for equivalence.
 *
 * Returns: 0 if arrays are identical, else -1.
 **/
#define TEST_CMP_STR_ARRAYS(a, b, sizea, sizeb) \
({ int ret = 0; \
 if (sizea == sizeb) { \
	 for (size_t __i = 0; \
		 __i < sizea; \
		 __i++) { \
	 	if (strcmp (a[__i], b[__i])) { \
 			ret = -1; \
 			break; \
 		} \
 	} \
 } else \
 	ret = -1; \
 ret;})

/**
 * TEST_TWO_LISTS_FOREACH:
 * @list1: entry in the first list to iterate,
 * @list2: entry in the second list to iterate,
 * @iter1: name of iterator variable for @list1,
 * @iter2: name of iterator variable for @list2.
 *
 * Dual version of NIH_LIST_FOREACH() which iterates 
 * two lists in tandem.
 **/
#define TEST_TWO_LISTS_FOREACH(list1, list2, iter1, iter2) \
	for (NihList *iter1 = (list1)->next, \
		     *iter2 = (list2)->next; \
		iter1 != (list1) && iter2 != (list2); \
		iter1 = iter1->next, \
		iter2 = iter2->next)

/**
 * TEST_TWO_HASHES_FOREACH:
 * @hash1: entry in the first hash to iterate,
 * @hash2: entry in the second hash to iterate,
 * @iter1: name of iterator variable for @hash1,
 * @iter2: name of iterator variable for @hash2.
 *
 * Dual version of NIH_HASH_FOREACH() which iterates
 * two hashes in tandem.
 **/
#define TEST_TWO_HASHES_FOREACH(hash1, hash2, iter1, iter2) \
	for (size_t _##iter##_i = 0; _##iter##_i < (hash1)->size; \
	     _##iter##_i++) \
		TEST_TWO_LISTS_FOREACH (&(hash1)->bins[_##iter##_i], \
					&(hash2)->bins[_##iter##_i], \
				iter1, iter2)

/**
 * TEST_TWO_TREES_FOREACH:
 * @tree1: root of the first tree to iterate,
 * @tree2: root of the second tree to iterate,
 * @iter1: name of iterator variable for @tree1,
 * @iter2: name of iterator variable for @tree2.
 *
 * Dual version of NIH_TREE_FOREACH() which walks
 * two trees in tandem.
 **/
#define TEST_TWO_TREES_FOREACH(tree1, tree2, iter1, iter2) \
	for (NihTree *iter1 = nih_tree_next (tree1, NULL), \
			*iter2 = nih_tree_next (tree2, NULL); \
			iter1 != NULL && iter2 != NULL; \
			iter1 = nih_tree_next (tree1, iter1), \
			iter2 = nih_tree_next (tree2, iter2))


/**
 * TEST_ARRAY_SIZE:
 * @array: array.
 * 
 * Determine size of specified array.
 *
 * Returns: array size.
 **/
#define TEST_ARRAY_SIZE(array) \
	(sizeof (array) / sizeof (array[0]))

/* Prototypes */
extern int string_check (const char *a, const char *b);

#endif /* TEST_UTIL_H */
