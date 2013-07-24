/* upstart
 *
 * test_util_common.c - common test utilities
 *
 * Copyright © 2012-2013 Canonical Ltd.
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */

#include <nih/test.h>
#include <nih/file.h>
#include <nih/string.h>
#include <nih/signal.h>
#include <nih/logging.h>
#include <nih-dbus/test_dbus.h>

#include <dbus/dbus.h>

#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <regex.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>

#include <nih-dbus/dbus_error.h>
#include <nih-dbus/dbus_connection.h>
#include <nih-dbus/dbus_proxy.h>
#include <nih-dbus/errors.h>  

#include "dbus/upstart.h"

#include "test_util_common.h"

#ifndef UPSTART_BINARY
#error unable to find init binary as UPSTART_BINARY not defined
#endif /* UPSTART_BINARY */

#ifndef INITCTL_BINARY
#error unable to find initctl binary as INITCTL_BINARY not defined
#endif /* INITCTL_BINARY */

static void selfpipe_write (int n);
static void selfpipe_setup (void);

/**
 * wait_for_upstart:
 *
 * @session_init_pid: pid of Session Init (which uses a private bus
 * rather than the session bus), else 0.
 *
 * Wait for Upstart to appear on D-Bus denoting its completion of
 * initialisation. Wait time is somewhat arbitrary (but more
 * than adequate!).
 **/
void
wait_for_upstart (int session_init_pid)
{
	nih_local NihDBusProxy *upstart = NULL;
	DBusConnection         *connection;
	char                   *address;
	NihError               *err;
	int                     running = FALSE;

	/* XXX: arbitrary value */
	int                     attempts = 10;

	if (session_init_pid) {
		TEST_TRUE (set_upstart_session (session_init_pid));
		address = getenv ("UPSTART_SESSION");
	} else {
		address = getenv ("DBUS_SESSION_BUS_ADDRESS");
	}

	TEST_TRUE (address);

	while (attempts) {
		attempts--;
		sleep (1);
		connection = nih_dbus_connect (address, NULL);

		if (! connection) {
			err = nih_error_get ();
			nih_free (err);
			continue;
		}

		upstart = nih_dbus_proxy_new (NULL, connection,
				      	      NULL,
					      DBUS_PATH_UPSTART,
				      	      NULL, NULL);

		if (! upstart) {
			err = nih_error_get ();
			nih_free (err);
			dbus_connection_unref (connection);
		} else {
			running = TRUE;
			break;
		}
	}
	TEST_EQ (running, TRUE);
}

/* TRUE to denote that Upstart is running in user session mode
 * (FALSE to denote it's using the users D-Bus session bus).
 */
int test_user_mode = FALSE;

/**
 * set_upstart_session:
 *
 * @session_init_pid: pid of Session Init.
 *
 * Attempt to "enter" an Upstart session by setting UPSTART_SESSION to
 * the value of the session running under pid @session_init_pid.
 *
 * Returns: TRUE if it was possible to enter the currently running
 * Upstart session, else FALSE.
 **/
int
set_upstart_session (pid_t session_init_pid)
{
	char                     *value;
	nih_local char           *cmd = NULL;
	nih_local char          **output = NULL;
	size_t                    lines = 0;
	int                       got = FALSE;
	int                       i;
	pid_t                     pid;

	/* XXX: arbitrary value */
	int                       loops = 5;

	nih_assert (session_init_pid);

	/* list-sessions relies on this */
	if (! getenv ("XDG_RUNTIME_DIR"))
		return FALSE;

	cmd = nih_sprintf (NULL, "%s list-sessions 2>&1", INITCTL_BINARY);
	TEST_NE_P (cmd, NULL);

	/* We expect the list-sessions command to return a valid session
	 * within a reasonable period of time.
	 */
	for (i = 0; i < loops; i++) {
        sleep (1);

		RUN_COMMAND (NULL, cmd, &output, &lines);

        if (lines < 1)
            continue;

        /* Look for the specific session */
        for (size_t line = 0; line < lines; lines++) {

            /* No pid in output */
            if (! isdigit(output[line][0]))
                continue;

            pid = (pid_t)atoi(output[line]);
            nih_assert (pid > 0);

            if (pid != session_init_pid)
                continue;

            /* look for separator between pid and value of
             * UPSTART_SESSION.
             */
            value = strstr (output[0], " ");
            if (! value)
                continue;

            /* jump over space */
            value  += 1;
            if (! value)
                continue;

            /* No socket address */
            if (strstr (value, "unix:abstract") == value) {
                got = TRUE;
                goto out;
            }
        }
	}

out:

	if (got != TRUE)
		return FALSE;

	assert0 (setenv ("UPSTART_SESSION", value, 1));

	return TRUE;
}

/**
 * selfpipe:
 *
 * Used to allow a timed process wait.
 **/
static int selfpipe[2] = { -1, -1 };

static void
selfpipe_write (int n)
{
    assert (selfpipe[1] != -1);

    TEST_EQ (write (selfpipe[1], "", 1), 1);
}

/**
 * selfpipe_setup:
 *
 * Arrange for SIGCHLD to write to selfpipe such that we can select(2)
 * on child process status changes.
 **/
static void
selfpipe_setup (void)
{
    static struct sigaction  act;
    int                      read_flags;
    int                      write_flags;

    assert (selfpipe[0] == -1);

    assert (! pipe (selfpipe));

    /* Set non-blocking */
    read_flags = fcntl (selfpipe[0], F_GETFL);
    write_flags = fcntl (selfpipe[1], F_GETFL);

    read_flags |= O_NONBLOCK;
    write_flags |= O_NONBLOCK;

    assert (fcntl (selfpipe[0], F_SETFL, read_flags) == 0);
    assert (fcntl (selfpipe[1], F_SETFL, write_flags) == 0);

    /* Don't leak */
    assert (fcntl (selfpipe[0], F_SETFD, FD_CLOEXEC) == 0);
    assert (fcntl (selfpipe[1], F_SETFD, FD_CLOEXEC) == 0);

    memset (&act, 0, sizeof (act));

    /* register SIGCHLD handler which will cause pipe write when child
     * changes state.
     */
    act.sa_handler = selfpipe_write;

    sigaction (SIGCHLD, &act, NULL);
}

/**
 * timed_waitpid:
 *
 * @pid: pid to wait for,
 * @timeout: seconds to wait for @pid to change state.
 *
 * Simplified waitpid(2) with timeout using a pipe to allow select(2)
 * with timeout to be used to wait for process state change.
 **/
pid_t
timed_waitpid (pid_t pid, time_t timeout)
{
    static char     buffer[1];
    fd_set          read_fds;
    struct timeval  tv;
    int             status;
    int             nfds;
    int             ret;
    pid_t           ret2;

    assert (pid);
    assert (timeout);

    if (selfpipe[0] == -1)
	    selfpipe_setup ();

    FD_ZERO (&read_fds);
    FD_SET (selfpipe[0], &read_fds);

    nfds = 1 + selfpipe[0];

    tv.tv_sec   = timeout;
    tv.tv_usec  = 0;

    /* wait for some activity */
    ret = select (nfds, &read_fds, NULL, NULL, &tv);

    if (! ret)
	    /* timed out */
	    return 0;

    /* discard any data written to pipe */
    while (read (selfpipe[0], buffer, sizeof (buffer)) > 0)
	    ;

    while (TRUE) {
	    /* wait for status change or error */
	    ret2 = waitpid (pid, &status, WNOHANG);

	    if (ret2 < 0)
		    return -1;

	    if (! ret2)
		    /* give child a chance to change state */
		    sleep (1);

	    if (ret2) {
		    if (WIFEXITED (status))
			    return ret2;

		    /* unexpected status change */
		    return -1;
	    }
    }
}


/**
 * get_initctl():
 *
 * Determine a suitable initctl command-line for testing purposes.
 *
 * Returns: Static string representing full path to initctl binary with
 * default option to allow communication with an Upstart started using
 * START_UPSTART().
 **/
char *
get_initctl (void)
{
	static char path[PATH_MAX + 1024] = { 0 };
	int         ret;

	ret = sprintf (path, "%s %s",
			INITCTL_BINARY,
			test_user_mode
			? "--user"
			: "--session");

	assert (ret > 0);

	return path;
}

/*
 * _start_upstart:
 *
 * @pid: PID of running instance,
 * @user: TRUE if upstart will run in User Session mode (FALSE to
 *  use the users D-Bus session bus),
 * @args: optional list of arguments to specify.
 *
 * Start an instance of Upstart.
 *
 * If the instance fails to start, abort(3) is called.
 **/
void
_start_upstart (pid_t *pid, int user, char * const *args)
{
	nih_local char  **argv = NULL;
	sigset_t          child_set, orig_set;

	assert (pid);

	argv = NIH_MUST (nih_str_array_new (NULL));

	NIH_MUST (nih_str_array_add (&argv, NULL, NULL,
				UPSTART_BINARY));

	if (args)
		NIH_MUST (nih_str_array_append (&argv, NULL, NULL, args));

	sigfillset (&child_set);
	sigprocmask (SIG_BLOCK, &child_set, &orig_set);

	TEST_NE (*pid = fork (), -1);

	if (! *pid) {
		int fd;
		nih_signal_reset ();
		sigprocmask (SIG_SETMASK, &orig_set, NULL);

		if (! getenv ("UPSTART_TEST_VERBOSE")) {
			fd = open ("/dev/null", O_RDWR);
			assert (fd >= 0);
			assert (dup2 (fd, STDIN_FILENO) != -1);
			assert (dup2 (fd, STDOUT_FILENO) != -1);
			assert (dup2 (fd, STDERR_FILENO) != -1);
		}

		assert (execv (argv[0], argv) != -1);
	}

	sigprocmask (SIG_SETMASK, &orig_set, NULL);
	wait_for_upstart (user ? *pid : 0);
}

/**
 * start_upstart_common:
 *
 * @pid: PID of running instance,
 * @user: TRUE if upstart should run in User Session mode (FALSE to
 * use the users D-Bus session bus),
 * @confdir: full path to configuration directory,
 * @logdir: full path to log directory,
 * @extra: optional extra arguments.
 *
 * Wrapper round _start_upstart() which specifies common options.
 **/
void
start_upstart_common (pid_t *pid, int user, const char *confdir,
		      const char *logdir, char * const *extra)
{
	nih_local char  **args = NULL;

	assert (pid);

	args = NIH_MUST (nih_str_array_new (NULL));

	if (user) {
		NIH_MUST (nih_str_array_add (&args, NULL, NULL,
					"--user"));
		test_user_mode = TRUE;
	} else {
		TEST_TRUE (getenv ("DBUS_SESSION_BUS_ADDRESS"));
		NIH_MUST (nih_str_array_add (&args, NULL, NULL,
					"--session"));
	}

	NIH_MUST (nih_str_array_add (&args, NULL, NULL,
				"--no-startup-event"));

	NIH_MUST (nih_str_array_add (&args, NULL, NULL,
				"--no-sessions"));

	NIH_MUST (nih_str_array_add (&args, NULL, NULL,
				"--no-inherit-env"));

	if (confdir) {
		NIH_MUST (nih_str_array_add (&args, NULL, NULL,
					"--confdir"));
		NIH_MUST (nih_str_array_add (&args, NULL, NULL,
					confdir));
	}

	if (logdir) {
		NIH_MUST (nih_str_array_add (&args, NULL, NULL,
					"--logdir"));
		NIH_MUST (nih_str_array_add (&args, NULL, NULL,
					logdir));
	}

	if (extra)
		NIH_MUST (nih_str_array_append (&args, NULL, NULL, extra));

	_start_upstart (pid, user, args);
}

/**
 * start_upstart:
 *
 * @pid: PID of running instance.
 *
 * Wrapper round _start_upstart() which just runs an instance with no
 * options.
 **/
void
start_upstart (pid_t *pid)
{
	start_upstart_common (pid, FALSE, NULL, NULL, NULL);
}

/**
 * job_to_pid:
 *
 * @job: job name.
 *
 * Determine pid of running job.
 *
 * WARNING: it is the callers responsibility to ensure that
 * @job is still running when this function is called!!
 *
 * Returns: pid of job, or -1 if not found.
 **/
pid_t
job_to_pid (const char *job)
{
	pid_t            pid;
	regex_t          regex;
	regmatch_t       regmatch[2];
	int              ret;
	nih_local char  *cmd = NULL;
	nih_local char  *pattern = NULL;
	size_t           lines;
	char           **status;
	nih_local char  *str_pid = NULL;

	assert (job);

	pattern = NIH_MUST (nih_sprintf
			(NULL, "^\\b%s\\b .*, process ([0-9]+)", job));

	cmd = NIH_MUST (nih_sprintf (NULL, "%s status %s 2>&1",
			get_initctl (), job));
	RUN_COMMAND (NULL, cmd, &status, &lines);
	TEST_EQ (lines, 1);

	ret = regcomp (&regex, pattern, REG_EXTENDED);
	assert0 (ret);

	ret = regexec (&regex, status[0], 2, regmatch, 0);
	if (ret == REG_NOMATCH) {
		ret = -1;
		goto out;
	}
	assert0 (ret);

	if (regmatch[1].rm_so == -1 || regmatch[1].rm_eo == -1) {
		ret = -1;
		goto out;
	}

	/* extract the pid */
	NIH_MUST (nih_strncat (&str_pid, NULL,
			&status[0][regmatch[1].rm_so],
			regmatch[1].rm_eo - regmatch[1].rm_so));

	nih_free (status);

	pid = (pid_t)atol (str_pid);

	/* check it's running */
	ret = kill (pid, 0);
	if (! ret)
		ret = pid;

out:
	regfree (&regex);
	return ret;
}

const char *
get_upstart_binary (void)
{
	return UPSTART_BINARY;
}

const char *
get_initctl_binary (void)
{
	return INITCTL_BINARY;
}

/**
 * string_check:
 *
 * @a: first string,
 * @b: second string.
 *
 * Compare @a and @b either or both of which may be NULL.
 *
 * Returns 0 if strings are identical or both NULL, else 1.
 **/
int
string_check (const char *a, const char *b)
{
	if (!a && !b)
		return 0;

	if (!a || !b)
		return 1;

	if (strcmp (a, b))
		return 1;

	return 0;
}

/**
 * strcmp_compar:
 *
 * @a: first string,
 * @b: second string.
 *
 * String comparison function suitable for passing to qsort(3).
 * See the qsort(3) man page for further details.
 **/
int
strcmp_compar (const void *a, const void *b)
{
	return strcmp(*(char * const *)a, *(char * const *)b);
}

/**
 * get_session_file:
 *
 * @xdg_runtime_dir: Directory to treat as XDG_RUNTIME_DIR,
 * @pid: pid of running Session Init instance.
 *
 * Determine full path to a Session Inits session file.
 *
 * Note: No check on the existence of the session file is performed.
 *
 * Returns: Newly-allocated string representing full path to Session
 *          Inits session file.
 **/
char *
get_session_file (const char *xdg_runtime_dir, pid_t pid)
{
	char *session_file;
	
	nih_assert (xdg_runtime_dir);
	nih_assert (pid);

	session_file = nih_sprintf (NULL, "%s/upstart/sessions/%d.session",
			xdg_runtime_dir, (int)pid);

	nih_assert (session_file);

	return session_file;
}

/**
 * in_chroot:
 *
 * Determine if running inside a chroot environment.
 *
 * Failures are fatal.
 *
 * Returns TRUE if within a chroot, else FALSE.
 **/
int
in_chroot (void)
{
	struct stat st;
	int i;
	char dir[] = "/";

	i = stat(dir, &st);
	    
	if ( i != 0 ) { 
		fprintf (stderr, "ERROR: cannot stat '%s'\n", dir);
		exit (EXIT_FAILURE);
	}

	if ( st.st_ino == 2 )
		return FALSE;

	return TRUE;
}

/**
 * dbus_configured
 *
 * Determine if D-Bus has been configured (with dbus-uuidgen).
 *
 * Returns TRUE if D-Bus appears to have been configured,
 * else FALSE.
 **/
int
dbus_configured (void)
{
	struct stat st;
	char path[] = "/var/lib/dbus/machine-id";

	return !stat (path, &st);
}

/**
 * search_and_replace:
 *
 * @parent: parent for returned string,
 * @str: string to operate on,
 * @from: string to look for,
 * @to: string to replace @from with.
 *
 * Replace all occurences of @from in @str with @to.
 *
 * Returns: Newly-allocated string, or NULL on error or
 * if @str does not contain any occurences of @from.
 **/
char *
search_and_replace (void        *parent,
		    const char  *str,
		    const char  *from,
		    const char  *to)
{
	const char *start;
	const char *match;
	char       *new = NULL;
	size_t      len;

	nih_assert (str);
	nih_assert (from);
	nih_assert (to);

	start = str;
	len = strlen (from);

	while (start && *start) {
		match = strstr (start, from);

		if (! match) {
			/* No more matches, so copy the remainder of the original string */
			if (! nih_strcat (&new, parent, start))
				return NULL;
			break;
		}

		/* Copy data from start of segment to the match */
		if (! nih_strncat (&new, parent , start, match - start))
			return NULL;

		/* Replace the string */
		if (! nih_strcat (&new, parent, to))
			return NULL;

		/* Make start move to 1 byte beyond the end of the match */
		start = match + len;
	}

	return new;
}
