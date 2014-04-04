#ifndef TEST_UTIL_COMMON_H
#define TEST_UTIL_COMMON_H

#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <nih-dbus/test_dbus.h>
#include <nih/list.h>
#include <nih/hash.h>
#include <nih/tree.h>

/**
 * TEST_DIR_MODE:
 *
 * Mode to use when creating test directories.
 **/
#define TEST_DIR_MODE 0750

#define BUFFER_SIZE 1024

/**
 * TEST_EXIT_TIME:
 *
 * Maximum time we expect upstart to wait in the QUIESCE_PHASE_WAIT
 * phase.
 **/
#define TEST_EXIT_TIME 5

/**
 * TEST_QUIESCE_KILL_PHASE:
 *
 * Maximum time we expect upstart to wait in the QUIESCE_PHASE_KILL
 * phase.
 **/
#define TEST_QUIESCE_KILL_PHASE 5

#define TEST_QUIESCE_TOTAL_WAIT_TIME (TEST_EXIT_TIME + TEST_QUIESCE_KILL_PHASE)

/* A 'reasonable' path, but which also contains a marker at the end so
 * we know when we're looking at a PATH these tests have set.
 */
#define TEST_INITCTL_DEFAULT_PATH "/usr/local/bin:/usr/local/sbin:/usr/bin:/usr/sbin:/bin:/sbin:/wibble"

/* Default value for TERM if not already set */
#define TEST_INITCTL_DEFAULT_TERM "linux"


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

/**
 * START_UPSTART:
 *
 * @pid: pid_t that will contain pid of running instance on success,
 * @user_mode: TRUE for Session Init (or FALSE to use D-Bus
 * session bus).
 *
 * Start an instance of Upstart and return PID in @pid.
 **/
#define START_UPSTART(pid, user_mode)                                \
	start_upstart_common (&(pid), user_mode, FALSE, NULL, NULL, NULL)

/**
 * KILL_UPSTART:
 *
 * @pid: pid of upstart to kill,
 * @signo: signal number to send to @pid,
 * @wait: TRUE to wait for @pid to die.
 *
 * Send specified signal to upstart process @pid.
 **/
#define KILL_UPSTART(pid, signo, wait)                               \
{                                                                    \
	int status;                                                  \
	assert (pid);                                                \
	assert (signo);                                              \
	                                                             \
	assert0 (kill (pid, signo));                                 \
	if (wait) {                                                  \
		TEST_EQ (waitpid (pid, &status, 0), pid);            \
		TEST_TRUE (WIFSIGNALED (status));                    \
		TEST_TRUE (WTERMSIG (status) == signo);              \
	}                                                            \
	/* reset since a subsequent start could specify a different  \
	 * user_mode value.                                          \
	 */                                                          \
	test_user_mode = FALSE;                                      \
}

/**
 * STOP_UPSTART:
 *
 * @pid: pid of upstart to kill.
 *
 * Stop upstart process @pid.
 **/
#define STOP_UPSTART(pid)                                            \
	KILL_UPSTART (pid, SIGKILL, TRUE)

/**
 * REEXEC_UPSTART:
 *
 * @pid: pid of upstart,
 * @user: TRUE if @pid refers to a Session Init, else FALSE.
 *
 * Force upstart to perform a re-exec.
 **/
#define REEXEC_UPSTART(pid, user)                                    \
	if (user) {                                                  \
		session_init_reexec (pid);                           \
	} else {                                                     \
	    KILL_UPSTART (pid, SIGTERM, FALSE);                      \
	}                                                            \
	wait_for_upstart (user ? pid : FALSE)

/**
 * RUN_COMMAND:
 *
 * @parent: pointer to parent object,
 * @cmd: string representing command to run,
 * @result: "char ***" pointer which will contain an array of string
 * values corresponding to lines of standard output generated by @cmd,
 * @len: size_t pointer which will be set to length of @result.
 *
 * Run a command and return its standard output. It is the callers
 * responsibility to free @result. Errors from running @cmd are fatal.
 *
 * Note: trailing '\n' characters are removed in returned command
 * output.
 **/
#define RUN_COMMAND(parent, cmd, result, len)                        \
{                                                                    \
	FILE    *f;                                                  \
	char     buffer[BUFFER_SIZE];                                \
	char   **ret;                                                \
	                                                             \
	assert (cmd[0]);                                             \
	                                                             \
	*(result) = nih_str_array_new (parent);                      \
	TEST_NE_P (*result, NULL);                                   \
	*(len) = 0;                                                  \
	                                                             \
	f = popen (cmd, "r");                                        \
	TEST_NE_P (f, NULL);                                         \
	                                                             \
	while (fgets (buffer, BUFFER_SIZE, f)) {                     \
		size_t l = strlen (buffer)-1;                        \
	                                                             \
		if ( buffer[l] == '\n')                              \
			buffer[l] = '\0';                            \
		ret = nih_str_array_add (result, parent, len,        \
			buffer);                                     \
		TEST_NE_P (ret, NULL);                               \
	}                                                            \
	                                                             \
	TEST_NE (pclose (f), -1);                                    \
}

/**
 * CREATE_FILE:
 *
 * @dirname: directory name (assumed to already exist),
 * @name: name of file to create (no leading slash),
 * @contents: string contents of @name.
 *
 * Create a file in the specified directory with the specified
 * contents.
 *
 * Notes: A newline character is added in the case where @contents does
 * not end with one.
 **/
#define CREATE_FILE(dirname, name, contents)                         \
{                                                                    \
	FILE    *f;                                                  \
	char     filename[PATH_MAX];                                 \
                                                                     \
	assert (dirname[0]);                                         \
	assert (name[0]);                                            \
                                                                     \
        strcpy (filename, dirname);                                  \
	if ( name[0] != '/' )                                        \
	  strcat (filename, "/");                                    \
        strcat (filename, name);                                     \
        f = fopen (filename, "w");                                   \
        TEST_NE_P (f, NULL);                                         \
        fprintf (f, "%s", contents);                                 \
	if ( contents[strlen(contents)-1] != '\n')                   \
          fprintf (f, "\n");                                         \
        fclose (f);                                                  \
}

/**
 * DELETE_FILE:
 *
 * @dirname: directory in which file to delete exists,
 * @name: name of file in @dirname to delete.
 *
 * Delete specified file.
 *
 **/
#define DELETE_FILE(dirname, name)                                   \
{                                                                    \
	char     filename[PATH_MAX];                                 \
                                                                     \
	assert (dirname[0]);                                         \
	assert (name[0]);                                            \
                                                                     \
        strcpy (filename, dirname);                                  \
	if ( name[0] != '/' )                                        \
	  strcat (filename, "/");                                    \
        strcat (filename, name);                                     \
                                                                     \
	TEST_EQ (unlink (filename), 0);                              \
}

/**
 * _WAIT_FOR_FILE():
 *
 * @path: full path to file to look for,
 * @sleep_secs: number of seconds to sleep per loop,
 * @loops: number of times to check for file.
 *
 * Wait for a reasonable period of time for @path to be created.
 *
 * Abort if file does not appear within (sleep_secs * loops) seconds.
 *
 * XXX:WARNING: this is intrinsically racy since although the file has
 * been _created_, it has not necessarily been fully written at the
 * point this macro signifies success. For that we need inotify or
 * similar.
 **/
#define _WAIT_FOR_FILE(path, sleep_secs, loops)                      \
{                                                                    \
	int              ok;                                         \
	struct stat      statbuf;                                    \
                                                                     \
	assert (path[0]);                                            \
                                                                     \
	/* Wait for log to be created */                             \
	ok = FALSE;                                                  \
	for (int i = 0; i < loops; i++) {                            \
		sleep (sleep_secs);                                  \
		if (! stat (path, &statbuf)) {                       \
			ok = TRUE;                                   \
			break;                                       \
		}                                                    \
	}                                                            \
	TEST_EQ (ok, TRUE);                                          \
}

/**
 * WAIT_FOR_FILE():
 *
 * @path: full path to file to look for.
 *
 * Wait for a "reasonable period of time" for @path to be created.
 *
 * Abort if file does not appear within.
 **/
#define WAIT_FOR_FILE(path)                                         \
        _WAIT_FOR_FILE (path, 1, 5)

/**
 * TEST_STR_MATCH:
 * @_string: string to check,
 * @_pattern: pattern to expect.
 *
 * Check that @_string matches the glob pattern @_pattern, which
 * should include the terminating newline if one is expected.
 *
 * Notes: Analagous to TEST_FILE_MATCH().
 **/
#define TEST_STR_MATCH(_string, _pattern)                            \
	do {                                                         \
		if (fnmatch ((_pattern), _string, 0))                \
			TEST_FAILED ("wrong string value, "          \
					"expected '%s' got '%s'",    \
			     (_pattern), _string);                   \
	} while (0)

/**
 * _TEST_STR_ARRAY_CONTAINS:
 *
 * @_array: string array,
 * @_pattern: pattern to expect,
 * @_invert: invert meaning.
 *
 * Check that atleast 1 element in @_array matches @_pattern.
 *
 * If @_invert is TRUE, ensure @_pattern is _NOT_ found in @_array.
 **/
#define _TEST_STR_ARRAY_CONTAINS(_array, _pattern, _invert)          \
	do {                                                         \
		char  **p;                                           \
		int     got = FALSE;                                 \
                                                                     \
		for (p = _array; p && *p; p++) {                     \
                                                                     \
			if (! fnmatch ((_pattern), *p, 0)) {         \
				got = TRUE;                          \
				break;                               \
			}                                            \
		}                                                    \
                                                                     \
		if (_invert) {                                       \
		  if (got) {                                         \
			TEST_FAILED ("wrong content in array "       \
				"%p (%s), '%s' found unexpectedly",  \
			     (_array), #_array, (_pattern));         \
		  }                                                  \
		} else {                                             \
		  if (! got) {                                       \
			TEST_FAILED ("wrong content in array "       \
				"%p (%s), '%s' not found",           \
			     (_array), #_array, (_pattern));         \
		  }                                                  \
		}                                                    \
	} while (0)

/**
 * _TEST_FILE_CONTAINS:
 * @_file: FILE to read from,
 * @_pattern: pattern to expect,
 * @_invert: invert meaning.
 *
 * Check that any subsequent line in file @_file matches the glob pattern
 * @_pattern, which should include the terminating newline if one is expected.
 *
 * If @_invert is TRUE, ensure @_pattern is _NOT_ found in @_file.
 **/
#define _TEST_FILE_CONTAINS(_file, _pattern, _invert)                \
	do {                                                         \
		char   buffer[1024];                                 \
		int    got = FALSE;                                  \
		int    ret;                                          \
		while (fgets (buffer, sizeof (buffer), _file)) {     \
                                                                     \
			ret = fnmatch ((_pattern), buffer, 0);       \
                                                                     \
			if (! ret) {                                 \
				got = TRUE;                          \
				break;                               \
			}                                            \
		}                                                    \
                                                                     \
		if (_invert) {                                       \
		    if (got) {                                       \
			TEST_FAILED ("wrong content in file "        \
				"%p (%s), '%s' found unexpectedly",  \
			     (_file), #_file, (_pattern));           \
		    }                                                \
		} else {                                             \
		    if (! got) {                                     \
			TEST_FAILED ("wrong content in file "        \
				"%p (%s), '%s' not found",           \
			     (_file), #_file, (_pattern));           \
		    }                                                \
		}                                                    \
	} while (0)


/**
 * TEST_FILE_CONTAINS:
 * @_file: FILE to read from,
 * @_pattern: pattern to expect.
 *
 * Check that any subsequent line in file @_file matches the glob pattern
 * @_pattern, which should include the terminating newline if one is expected.
 *
 **/
#define TEST_FILE_CONTAINS(_file, _pattern)                          \
	_TEST_FILE_CONTAINS(_file, _pattern, FALSE)

/**
 * TEST_FILE_NOT_CONTAINS:
 * @_file: FILE to read from,
 * @_pattern: pattern NOT to expect.
 *
 * Check that no subsequent line in file @_file does NOT match the glob pattern
 * @_pattern, which should include the terminating newline if one is expected.
 *
 **/
#define TEST_FILE_NOT_CONTAINS(_file, _pattern)                      \
	_TEST_FILE_CONTAINS(_file, _pattern, TRUE)

/**
 * TEST_STR_ARRAY_CONTAINS:
 *
 * @_array: string array,
 * @_pattern: pattern to expect.
 *
 * Check that atleast 1 element in @_array matches @_pattern.
 **/
#define TEST_STR_ARRAY_CONTAINS(_array, _pattern)                    \
        _TEST_STR_ARRAY_CONTAINS (_array, _pattern, FALSE)

/**
 * TEST_STR_ARRAY_NOT_CONTAINS:
 *
 * @_array: string array,
 * @_pattern: pattern to expect.
 *
 * Check that no element in @_array matches @_pattern.
 **/
#define TEST_STR_ARRAY_NOT_CONTAINS(_array, _pattern)                \
        _TEST_STR_ARRAY_CONTAINS (_array, _pattern, TRUE)

extern int test_user_mode;

/**
 * NihTreeHandler:
 * @node: tree entry being visited,
 * @data: data pointer.
 *
 * A tree handler is a function called for each tree node
 * when iterating over a tree.
 *
 * Returns: TRUE if tree entry process correctly, else FALSE.
 **/
typedef int (*NihTreeHandler) (NihTree *node, void *data);

/* Prototypes */
int set_upstart_session (pid_t session_init_pid)
	__attribute__ ((warn_unused_result));

void wait_for_upstart (int session_init_pid);
void session_init_reexec (int session_init_pid);

int have_timed_waitpid (void)
	__attribute__ ((warn_unused_result));

pid_t timed_waitpid (pid_t pid, time_t timeout)
	__attribute__ ((warn_unused_result));

char * get_initctl (void)
	__attribute__ ((warn_unused_result));

void _start_upstart (pid_t *pid, int user, char * const *args);

void start_upstart_common (pid_t *pid, int user, int inherit_env,
		      const char *confdir, const char *logdir,
		      char * const *extra);

void start_upstart (pid_t *pid);

pid_t job_to_pid (const char *job)
	__attribute__ ((warn_unused_result));

int string_check (const char *a, const char *b)
	__attribute__ ((warn_unused_result));

const char * get_upstart_binary (void)
	__attribute__ ((warn_unused_result));

const char * get_initctl_binary (void)
	__attribute__ ((warn_unused_result));

int strcmp_compar (const void *a, const void *b)
	__attribute__ ((warn_unused_result));

char *get_session_file (const char *xdg_runtime_dir, pid_t pid)
	__attribute__ ((warn_unused_result));

int in_chroot (void)
	__attribute__ ((warn_unused_result));

int dbus_configured (void)
	__attribute__ ((warn_unused_result));

char *search_and_replace (void *parent, const char *str,
			  const char *from, const char *to)
	__attribute__ ((warn_unused_result));

int file_exists (const char *path)
	__attribute__ ((warn_unused_result));

void test_common_setup (void);

void test_common_cleanup (void);

typedef int (*NihListHandler) (NihList *entry, void *data);

int test_list_handler_generic (NihList *entry, void *data)
    __attribute__ ((unused, noinline));

int test_list_foreach (const NihList *list, size_t *len,
		NihListHandler handler, void *data)
	__attribute__((unused));

size_t test_list_count (const NihList *list)
	__attribute__((warn_unused_result, unused));

NihList *test_list_get_index (NihList *list, size_t count)
	__attribute__((warn_unused_result, unused));

int test_hash_foreach (const NihHash *hash, size_t *len,
		NihListHandler handler, void *data)
	__attribute__((unused));

size_t test_hash_count (const NihHash *hash)
	__attribute__((warn_unused_result, unused));

int test_tree_foreach (NihTree *tree, size_t *len,
		NihTreeHandler handler, void *data)
	__attribute__((unused));

size_t test_tree_count (NihTree *tree)
	__attribute__((warn_unused_result, unused));

int connect_to_cgmanager(void);
void disconnect_cgmanager(void);
char *get_pid_cgroup(const char *controller, pid_t pid);
int setup_cgroup_sandbox(void);
#endif /* TEST_UTIL_COMMON_H */
