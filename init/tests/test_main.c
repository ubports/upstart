/* upstart
 *
 * test_main.c - test suite for init/main.c
 *
 * Copyright © 2013 Canonical Ltd.
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

#include <nih/string.h>
#include <nih/main.h>
#include <nih/test.h>

#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "conf.h"
#include "job_class.h"
#include "job.h"
#include "xdg.h"

#include "test_util_common.h"


void
test_confdir (void)
{
	char             confdir_a[PATH_MAX];
	char             confdir_b[PATH_MAX];
	char             xdg_config_home[PATH_MAX];
	char             xdg_runtime_dir[PATH_MAX];
	char             logdir[PATH_MAX];
	pid_t            upstart_pid = 0;
	pid_t            dbus_pid = 0;
	char           **output;
	size_t           lines;
	nih_local char  *cmd = NULL;
	nih_local char  *orig_xdg_config_home = NULL;
	nih_local char  *orig_xdg_runtime_dir = NULL;
	nih_local char  *xdg_conf_dir = NULL;
	nih_local char  *session_file = NULL;
	nih_local char  *path = NULL;

	/* space for 2 sets of confdir options and a terminator */
	char            *extra[5];

	TEST_GROUP ("--confdir command-line option handling");

	TEST_FILENAME (confdir_a);
	assert0 (mkdir (confdir_a, 0755));

	TEST_FILENAME (confdir_b);
	assert0 (mkdir (confdir_b, 0755));

	TEST_FILENAME (xdg_config_home);
	assert0 (mkdir (xdg_config_home, 0755));

	TEST_FILENAME (xdg_runtime_dir);
	assert0 (mkdir (xdg_runtime_dir, 0755));

	xdg_conf_dir = nih_sprintf (NULL, "%s/%s", xdg_config_home, "upstart");
	TEST_NE_P (xdg_conf_dir, NULL);
	assert0 (mkdir (xdg_conf_dir, 0755));

	TEST_FILENAME (logdir);
	assert0 (mkdir (logdir, 0755));

	/* Take care to avoid disrupting users environment by saving and
	 * restoring these variable (assuming the tests all pass...).
	 */
	orig_xdg_config_home = getenv ("XDG_CONFIG_HOME");
	if (orig_xdg_config_home)
		orig_xdg_config_home = NIH_MUST (nih_strdup (NULL, orig_xdg_config_home));

	assert0 (setenv ("XDG_CONFIG_HOME", xdg_config_home, 1));

	orig_xdg_runtime_dir = getenv ("XDG_RUNTIME_DIR");
	if (orig_xdg_runtime_dir)
		orig_xdg_runtime_dir = NIH_MUST (nih_strdup (NULL, orig_xdg_runtime_dir));

	assert0 (setenv ("XDG_RUNTIME_DIR", xdg_runtime_dir, 1));

	/* disable system default job dir */
	assert0 (setenv ("UPSTART_NO_SYSTEM_USERCONFDIR", "1", 1));

	TEST_DBUS (dbus_pid);

	/************************************************************/
	TEST_FEATURE ("Session Init without --confdir");

	CREATE_FILE (xdg_conf_dir, "foo.conf", "exec true");
	CREATE_FILE (xdg_conf_dir, "bar.conf", "exec true");
	CREATE_FILE (xdg_conf_dir, "baz.conf", "exec true");

	start_upstart_common (&upstart_pid, TRUE, NULL, logdir, NULL);

	/* Should be running */
	assert0 (kill (upstart_pid, 0));

	session_file = get_session_file (xdg_runtime_dir, upstart_pid);

	cmd = nih_sprintf (NULL, "%s list 2>&1", get_initctl ());
	TEST_NE_P (cmd, NULL);
	RUN_COMMAND (NULL, cmd, &output, &lines);

	qsort (output, lines, sizeof (output[0]), strcmp_compar);

	TEST_EQ (lines, 3);
	TEST_STR_MATCH (output[0], "bar stop/waiting");
	TEST_STR_MATCH (output[1], "baz stop/waiting");
	TEST_STR_MATCH (output[2], "foo stop/waiting");
	nih_free (output);

	DELETE_FILE (xdg_conf_dir, "foo.conf");
	DELETE_FILE (xdg_conf_dir, "bar.conf");
	DELETE_FILE (xdg_conf_dir, "baz.conf");

	STOP_UPSTART (upstart_pid);
	assert0 (unlink (session_file));

	/************************************************************/
	TEST_FEATURE ("Session Init with --confdir");

	CREATE_FILE (xdg_conf_dir, "xdg_dir_job.conf", "exec true");
	CREATE_FILE (confdir_a, "conf_dir_job.conf", "exec true");

	start_upstart_common (&upstart_pid, TRUE, confdir_a, logdir, NULL);

	/* Should be running */
	assert0 (kill (upstart_pid, 0));

	session_file = get_session_file (xdg_runtime_dir, upstart_pid);

	cmd = nih_sprintf (NULL, "%s list 2>&1", get_initctl ());
	TEST_NE_P (cmd, NULL);
	RUN_COMMAND (NULL, cmd, &output, &lines);

	qsort (output, lines, sizeof (output[0]), strcmp_compar);

	/* We expect jobs in xdg_conf_dir to be ignored */
	TEST_EQ (lines, 1);
	TEST_STR_MATCH (output[0], "conf_dir_job stop/waiting");
	nih_free (output);

	DELETE_FILE (xdg_conf_dir, "xdg_dir_job.conf");
	DELETE_FILE (confdir_a, "conf_dir_job.conf");

	STOP_UPSTART (upstart_pid);
	assert0 (unlink (session_file));

	/************************************************************/
	TEST_FEATURE ("Session Init with multiple --confdir");

	CREATE_FILE (xdg_conf_dir, "xdg_dir_job.conf", "exec true");
	CREATE_FILE (confdir_a, "conf_dir_a_job.conf", "exec true");
	CREATE_FILE (confdir_b, "conf_dir_b_job.conf", "exec true");

	extra[0] = "--confdir";
	extra[1] = confdir_a;
	extra[2] = "--confdir";
	extra[3] = confdir_b;
	extra[4] = NULL;

	/* pass 2 confdir directories */
	start_upstart_common (&upstart_pid, TRUE, NULL, logdir, extra);

	/* Should be running */
	assert0 (kill (upstart_pid, 0));

	session_file = get_session_file (xdg_runtime_dir, upstart_pid);

	cmd = nih_sprintf (NULL, "%s list 2>&1", get_initctl ());
	TEST_NE_P (cmd, NULL);
	RUN_COMMAND (NULL, cmd, &output, &lines);

	qsort (output, lines, sizeof (output[0]), strcmp_compar);

	/* We expect jobs in xdg_conf_dir to be ignored */
	TEST_EQ (lines, 2);
	TEST_STR_MATCH (output[0], "conf_dir_a_job stop/waiting");
	TEST_STR_MATCH (output[1], "conf_dir_b_job stop/waiting");
	nih_free (output);

	DELETE_FILE (xdg_conf_dir, "xdg_dir_job.conf");
	DELETE_FILE (confdir_a, "conf_dir_a_job.conf");
	DELETE_FILE (confdir_b, "conf_dir_b_job.conf");

	STOP_UPSTART (upstart_pid);
	assert0 (unlink (session_file));

	/************************************************************/
	TEST_FEATURE ("Session Init with multiple --confdir and conflicting names");

	CREATE_FILE (xdg_conf_dir, "conflict.conf", "emits xdg_conf_dir");
	CREATE_FILE (confdir_a, "conflict.conf", "emits confdir_a");
	CREATE_FILE (confdir_b, "foo.conf", "exec true");

	extra[0] = "--confdir";
	extra[1] = confdir_a;
	extra[2] = "--confdir";
	extra[3] = confdir_b;
	extra[4] = NULL;

	/* pass 2 confdir directories */
	start_upstart_common (&upstart_pid, TRUE, NULL, logdir, extra);

	/* Should be running */
	assert0 (kill (upstart_pid, 0));

	session_file = get_session_file (xdg_runtime_dir, upstart_pid);

	cmd = nih_sprintf (NULL, "%s list 2>&1", get_initctl ());
	TEST_NE_P (cmd, NULL);
	RUN_COMMAND (NULL, cmd, &output, &lines);

	qsort (output, lines, sizeof (output[0]), strcmp_compar);

	/* We expect jobs in xdg_conf_dir to be ignored */
	TEST_EQ (lines, 2);
	TEST_STR_MATCH (output[0], "conflict stop/waiting");
	TEST_STR_MATCH (output[1], "foo stop/waiting");
	nih_free (output);

	cmd = nih_sprintf (NULL, "%s show-config %s 2>&1", get_initctl (), "conflict");
	TEST_NE_P (cmd, NULL);
	RUN_COMMAND (NULL, cmd, &output, &lines);

	/* Ensure the correct version of the conflict job is found */
	TEST_EQ (lines, 2);
	TEST_STR_MATCH (output[0], "conflict");
	TEST_STR_MATCH (output[1], "  emits confdir_a");
	nih_free (output);

	DELETE_FILE (xdg_conf_dir, "conflict.conf");
	DELETE_FILE (confdir_a, "conflict.conf");
	DELETE_FILE (confdir_b, "foo.conf");

	STOP_UPSTART (upstart_pid);
	assert0 (unlink (session_file));

	/************************************************************/
	TEST_FEATURE ("System Init without --confdir");

	/* Use the "secret" interface */
	assert0 (setenv ("UPSTART_CONFDIR", confdir_a, 1));

	CREATE_FILE (confdir_a, "foo.conf", "exec true");
	CREATE_FILE (confdir_a, "bar.conf", "exec true");
	CREATE_FILE (confdir_a, "baz.conf", "exec true");

	/* Disable user mode */
	test_user_mode = FALSE;

	start_upstart_common (&upstart_pid, FALSE, NULL, logdir, NULL);

	/* Should be running */
	assert0 (kill (upstart_pid, 0));

	cmd = nih_sprintf (NULL, "%s list 2>&1", get_initctl ());
	TEST_NE_P (cmd, NULL);
	RUN_COMMAND (NULL, cmd, &output, &lines);

	qsort (output, lines, sizeof (output[0]), strcmp_compar);

	TEST_EQ (lines, 3);
	TEST_STR_MATCH (output[0], "bar stop/waiting");
	TEST_STR_MATCH (output[1], "baz stop/waiting");
	TEST_STR_MATCH (output[2], "foo stop/waiting");
	nih_free (output);

	DELETE_FILE (confdir_a, "foo.conf");
	DELETE_FILE (confdir_a, "bar.conf");
	DELETE_FILE (confdir_a, "baz.conf");

	STOP_UPSTART (upstart_pid);

	/************************************************************/
	TEST_FEATURE ("System Init with --confdir");

	CREATE_FILE (confdir_a, "foo.conf", "exec true");
	CREATE_FILE (confdir_a, "bar.conf", "exec true");
	CREATE_FILE (confdir_b, "baz.conf", "exec true");

	start_upstart_common (&upstart_pid, FALSE, confdir_b, logdir, NULL);

	/* Should be running */
	assert0 (kill (upstart_pid, 0));

	cmd = nih_sprintf (NULL, "%s list 2>&1", get_initctl ());
	TEST_NE_P (cmd, NULL);
	RUN_COMMAND (NULL, cmd, &output, &lines);

	qsort (output, lines, sizeof (output[0]), strcmp_compar);

	TEST_EQ (lines, 1);
	TEST_STR_MATCH (output[0], "baz stop/waiting");
	nih_free (output);

	DELETE_FILE (confdir_a, "foo.conf");
	DELETE_FILE (confdir_a, "bar.conf");
	DELETE_FILE (confdir_b, "baz.conf");

	STOP_UPSTART (upstart_pid);

	/************************************************************/
	TEST_FEATURE ("System Init with multiple --confdir");

	assert0 (setenv ("UPSTART_CONFDIR", xdg_conf_dir, 1));

	CREATE_FILE (xdg_conf_dir, "foo.conf", "exec true");
	CREATE_FILE (confdir_a, "bar.conf", "exec true");
	CREATE_FILE (confdir_b, "baz.conf", "exec true");
	CREATE_FILE (confdir_b, "qux.conf", "exec true");

	extra[0] = "--confdir";
	extra[1] = confdir_a;
	extra[2] = "--confdir";
	extra[3] = confdir_b;
	extra[4] = NULL;

	start_upstart_common (&upstart_pid, FALSE, NULL, logdir, extra);

	/* Should be running */
	assert0 (kill (upstart_pid, 0));

	cmd = nih_sprintf (NULL, "%s list 2>&1", get_initctl ());
	TEST_NE_P (cmd, NULL);
	RUN_COMMAND (NULL, cmd, &output, &lines);

	qsort (output, lines, sizeof (output[0]), strcmp_compar);

	TEST_EQ (lines, 2);
	/* XXX: Only the last instance of --confdir should be honoured.
	 *
	 * This behaviour deviates from running as a Session Init where *all*
	 * --confdir's specified are used.
	 */
	TEST_STR_MATCH (output[0], "baz stop/waiting");
	TEST_STR_MATCH (output[1], "qux stop/waiting");
	nih_free (output);

	DELETE_FILE (xdg_conf_dir, "foo.conf");
	DELETE_FILE (confdir_a, "bar.conf");
	DELETE_FILE (confdir_b, "baz.conf");
	DELETE_FILE (confdir_b, "qux.conf");

	STOP_UPSTART (upstart_pid);

	/************************************************************/
	TEST_FEATURE ("System Init with multiple --confdir and conflicting names");

	assert0 (setenv ("UPSTART_CONFDIR", xdg_conf_dir, 1));

	CREATE_FILE (xdg_conf_dir, "conflict.conf", "emits xdg_conf_dir");
	CREATE_FILE (confdir_a, "conflict.conf", "emits confdir_a");
	CREATE_FILE (confdir_b, "conflict.conf", "emits confdir_b");

	extra[0] = "--confdir";
	extra[1] = confdir_a;
	extra[2] = "--confdir";
	extra[3] = confdir_b;
	extra[4] = NULL;

	start_upstart_common (&upstart_pid, FALSE, NULL, logdir, extra);

	/* Should be running */
	assert0 (kill (upstart_pid, 0));

	cmd = nih_sprintf (NULL, "%s list 2>&1", get_initctl ());
	TEST_NE_P (cmd, NULL);
	RUN_COMMAND (NULL, cmd, &output, &lines);

	qsort (output, lines, sizeof (output[0]), strcmp_compar);

	TEST_EQ (lines, 1);
	/* only the last instance of --confdir should be honoured */
	TEST_STR_MATCH (output[0], "conflict stop/waiting");
	nih_free (output);

	cmd = nih_sprintf (NULL, "%s show-config %s 2>&1", get_initctl (), "conflict");
	TEST_NE_P (cmd, NULL);
	RUN_COMMAND (NULL, cmd, &output, &lines);

	/* Ensure the correct version of the conflict job is found */
	TEST_EQ (lines, 2);
	TEST_STR_MATCH (output[0], "conflict");
	TEST_STR_MATCH (output[1], "  emits confdir_b");
	nih_free (output);

	DELETE_FILE (xdg_conf_dir, "conflict.conf");
	DELETE_FILE (confdir_a, "conflict.conf");
	DELETE_FILE (confdir_b, "conflict.conf");

	STOP_UPSTART (upstart_pid);

	/************************************************************/

	TEST_DBUS_END (dbus_pid);

	if (orig_xdg_config_home) {
		/* restore */
		setenv ("XDG_CONFIG_HOME", orig_xdg_config_home, 1);
	} else {
		assert0 (unsetenv ("XDG_CONFIG_HOME"));
	}

	if (orig_xdg_runtime_dir) {
		/* restore */
		setenv ("XDG_RUNTIME_DIR", orig_xdg_runtime_dir, 1);
	} else {
		assert0 (unsetenv ("XDG_RUNTIME_DIR"));
	}

	assert0 (rmdir (confdir_a));
	assert0 (rmdir (confdir_b));
	assert0 (rmdir (xdg_conf_dir));
	assert0 (rmdir (xdg_config_home));

	/* Remove the directory tree the first Session Init created */
	path = NIH_MUST (nih_sprintf (NULL, "%s/upstart/sessions", xdg_runtime_dir));
	TEST_EQ (rmdir (path), 0);
	path = NIH_MUST (nih_sprintf (NULL, "%s/upstart", xdg_runtime_dir));
	TEST_EQ (rmdir (path), 0);
	assert0 (rmdir (xdg_runtime_dir));

	assert0 (rmdir (logdir));
	assert0 (unsetenv ("UPSTART_CONFDIR"));
}

int
main (int   argc,
      char *argv[])
{
	test_confdir ();

	return 0;
}
