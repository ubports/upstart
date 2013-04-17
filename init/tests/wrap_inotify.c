/* upstart
 *
 * wrap_inotify.c - library to subvert inotify calls.
 *
 * Copyright © Â 2013 Canonical Ltd.
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

/**
 * = Description =
 *
 * The test_conf upstart test requires certain test scenarios to run in
 * an environment where inotify(7) is not available/functional to force the
 * underlying NIH library to perform a manual filesystem tree traversal.
 *
 * Since inotify limits are _per user_ and not _per process_, it is not
 * possible to disable inotify on a system reliabliy for the duration
 * of a test run since the test is at the mercy of other processes that
 * are making inotify calls too.
 * 
 * The only reliable method therefore is to "fake" the inotify calls
 * using this library.
 *
 * To use this library:
 *
 * 1) Have the test code set the environment variable "INOTIFY_DISABLE" to
 *    any value to disable inotify, and unset the variable to leave it
 *    enabled.
 *
 * 2) Run the test code using LD_PRELOAD to force the dynamic
 *    link-loader to use these inotify definitions rather than those
 *    provided by libc:
 *
 *        (LD_PRELOAD=/path/to/this/libary.so test_code)
 *
 * To convince yourself this library is being used, set "INOTIFY_DEBUG"
 * to any value for some stdout debug messages.
 **/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <sys/inotify.h>
#include <dlfcn.h>

/**
 * Determine if inotify should be disabled.
 **/
#define disable_inotify() \
    (getenv ("INOTIFY_DISABLE"))

/**
 * Determine if inotify debug should be displayed to stdout.
 **/
#define debug_inotify() \
    (getenv ("INOTIFY_DEBUG"))

/**
 * debug_msg:
 *
 * If debug is enabled, display a message to stdout stating if inotify
 * is enabled along with details of the called function.
 **/
#define debug_msg() \
	do { \
		if (debug_inotify ()) { \
			printf ("DEBUG:%s:%d: inotify %s\n", \
					__func__, __LINE__, \
					disable_inotify () \
					? "disabled" : "enabled"); \
			fflush (NULL); \
		} \
	} while (0)

int __wrap_inotify_init (void)
	__attribute ((warn_unused_result, no_instrument_function));

int __wrap_inotify_add_watch (int fd, const char *pathname, uint32_t mask)
	__attribute ((warn_unused_result, no_instrument_function));

int __wrap_inotify_rm_watch (int fd, int wd)
	__attribute ((warn_unused_result, no_instrument_function));

static void *get_dlopen_handle (void)
	__attribute ((warn_unused_result, no_instrument_function));

int (*real_inotify_init_addr) (void);
int (*real_inotify_add_watch_addr) (int fd, const char *pathname, uint32_t mask);
int (*real_inotify_rm_watch_addr) (int fd, int wd);

/**
 * get_dlopen_handle:
 *
 * Returns: dlopen handle that can be used to resolve the real inotify
 * calls.
 **/
static void *
get_dlopen_handle (void)
{
	static void *handle = NULL;

	if (handle)
		goto out;

	/* This requires some explanation...
	 *
	 * Ideally, we'd open libc.so, but that won't work since
	 * dlopen(3) resolves that to the ld linker script of the same name.
	 *
	 * Specifying NULL as the filename makes the test go recursive.
	 *
	 * So, we cheat and specify a library we know is required (by
	 * _this_ library) and which is linked to libc _itself_.
	 **/
	handle = dlopen ("libdl.so", RTLD_NOW|RTLD_GLOBAL);
	assert (handle);

out:
	/* reset */
	(void)dlerror ();

	return handle;
}


int
__wrap_inotify_init (void)
{
	void *handle;

	if (disable_inotify ()) {
		/* simulate reaching inotify instances user limit */
		errno = EMFILE;
		return -1;
	}

	handle = get_dlopen_handle ();

	*(void **)(&real_inotify_init_addr) = dlsym (handle, "inotify_init");

	assert (! dlerror ()); 
	assert (real_inotify_init_addr);

	return real_inotify_init_addr ();
}

int
__wrap_inotify_add_watch (int fd, const char *pathname, uint32_t mask)
{
	void *handle;

	if (disable_inotify ()) {
		/* simulate reaching inotify watches user limit */
		errno = ENOSPC;
		return -1;
	}

	handle = get_dlopen_handle ();

	*(void **)(&real_inotify_add_watch_addr) = dlsym (handle, "inotify_add_watch");

	assert (! dlerror ()); 
	assert (real_inotify_add_watch_addr);

	return real_inotify_add_watch_addr (fd, pathname, mask);
}

int
__wrap_inotify_rm_watch (int fd, int wd)
{
	void *handle;

	if (disable_inotify ()) {
		; /* not meaningful, so just pass through */
	}

	handle = get_dlopen_handle ();

	*(void **)(&real_inotify_rm_watch_addr) = dlsym (handle, "inotify_rm_watch");

	assert (! dlerror ()); 
	assert (real_inotify_rm_watch_addr);

	return real_inotify_rm_watch_addr (fd, wd);
}

int
inotify_init (void)
{
	debug_msg ();

	return __wrap_inotify_init ();
}

int
inotify_add_watch (int fd, const char *pathname, uint32_t mask)
{
	debug_msg ();

	return __wrap_inotify_add_watch (fd, pathname, mask);

}

int
inotify_rm_watch (int fd, int wd)
{
	debug_msg ();

	return __wrap_inotify_rm_watch (fd, wd);
}
