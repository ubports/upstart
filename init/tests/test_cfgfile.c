/* upstart
 *
 * test_cfgfile.c - test suite for init/cfgfile.c
 *
 * Copyright © 2007 Canonical Ltd.
 * Author: Scott James Remnant <scott@ubuntu.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <nih/test.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <time.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/list.h>
#include <nih/timer.h>
#include <nih/main.h>

#include "cfgfile.h"


/* Macro to aid us in testing the output which includes the program name
 * and filename.
 */
#define TEST_ERROR_EQ(_file, _text) \
	do { \
		char text[512]; \
		sprintf (text, "%s:%s:%s", program_name, filename, (_text)); \
		TEST_FILE_EQ (_file, text); \
	} while (0);


static int was_called = 0;

static int
destructor_called (void *ptr)
{
	was_called++;

	return 0;
}

static void
my_timer (void *data, NihTimer *timer)
{
	return;
}

void
test_read_job (void)
{
	Job  *job;
	FILE *jf, *output;
	char  dirname[PATH_MAX], filename[PATH_MAX];
	int   i;

	TEST_FUNCTION ("cfg_read_job");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (dirname);
	sprintf (filename, "%s/foo", dirname);
	mkdir (dirname, 0700);


	/* Check that a simple job file can be parsed, with all of the
	 * information given filled into the job structure.
	 */
	TEST_FEATURE ("with simple job file");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon -d\n");
	fprintf (jf, "start script\n");
	fprintf (jf, "    rm /var/lock/daemon\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_LIST_EMPTY (&job->start_events);
	TEST_LIST_EMPTY (&job->stop_events);

	TEST_EQ_STR (job->command, "/sbin/daemon -d");
	TEST_ALLOC_PARENT (job->command, job);
	TEST_EQ_STR (job->start_script, "rm /var/lock/daemon\n");
	TEST_ALLOC_PARENT (job->start_script, job);


	/* Check that we can give a new file for an existing job; this
	 * frees the existing structure, while copying over critical
	 * information from it to a new structure.
	 */
	TEST_FEATURE ("with re-reading existing job file");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon --daemon\n");
	fclose (jf);

	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;
	job->pid = 1000;

	job->kill_timer = nih_timer_add_timeout (job, 1000, my_timer, job);
	job->pid_timer = nih_timer_add_timeout (job, 500, my_timer, job);

	was_called = 0;
	nih_alloc_set_destructor (job, destructor_called);

	job = cfg_read_job (NULL, filename, "test");

	TEST_TRUE (was_called);

	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_LIST_EMPTY (&job->start_events);
	TEST_LIST_EMPTY (&job->stop_events);

	TEST_EQ_STR (job->command, "/sbin/daemon --daemon");
	TEST_ALLOC_PARENT (job->command, job);

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_EQ (job->process_state, PROCESS_ACTIVE);
	TEST_EQ (job->pid, 1000);

	TEST_ALLOC_PARENT (job->kill_timer, job);
	TEST_LE (job->kill_timer->due, time (NULL) + 1000);
	TEST_EQ_P (job->kill_timer->callback, my_timer);
	TEST_EQ_P (job->kill_timer->data, job);

	TEST_ALLOC_PARENT (job->pid_timer, job);
	TEST_LE (job->pid_timer->due, time (NULL) + 500);
	TEST_EQ_P (job->pid_timer->callback, my_timer);
	TEST_EQ_P (job->pid_timer->data, job);

	nih_list_free (&job->entry);


	/* Check a pretty complete job file, with all the major toggles.
	 * Make sure the job structure is filled in properly.
	 */
	TEST_FEATURE ("with complete job file");
	jf = fopen (filename, "w");
	fprintf (jf, "# this is a comment\n");
	fprintf (jf, "\n");
	fprintf (jf, "description \"an example daemon\"\n");
	fprintf (jf, "author \"joe bloggs\"\n");
	fprintf (jf, "version \"1.0\"\n");
	fprintf (jf, "\n");
	fprintf (jf, "exec /sbin/daemon -d \"arg here\"\n");
	fprintf (jf, "respawn  # restart the job when it fails\n");
	fprintf (jf, "console owner\n");
	fprintf (jf, "\n");
	fprintf (jf, "start on startup\n");
	fprintf (jf, "stop on shutdown\n");
	fprintf (jf, "\n");
	fprintf (jf, "on explosion\n");
	fprintf (jf, "\n");
	fprintf (jf, "env PATH=\"/usr/games:/usr/bin\"\n");
	fprintf (jf, "env LANG=C\n");
	fprintf (jf, "\n");
	fprintf (jf, "umask 0155\n");
	fprintf (jf, "nice -20\n");
	fprintf (jf, "limit core 0 0\n");
	fprintf (jf, "limit cpu 50 100\n");
	fprintf (jf, "respawn limit 5 120\n");
	fprintf (jf, "\n");
	fprintf (jf, "chroot /jail/daemon\n");
	fprintf (jf, "chdir /var/lib\n");
	fprintf (jf, "\n");
	fprintf (jf, "start script\n");
	fprintf (jf, "    [ -d /var/run/daemon ] || mkdir /var/run/daemon\n");
	fprintf (jf, "  [ -d /var/lock/daemon ] || mkdir /var/lock/daemon\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "\n");
	fprintf (jf, "stop script\n");
	fprintf (jf, "    rm -rf /var/run/daemon /var/lock/daemon\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "\n");
	fprintf (jf, "kill timeout 30\n");
	fprintf (jf, "normalexit 0\n");
	fprintf (jf, "normalexit 99 100\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_EQ_STR (job->description, "an example daemon");
	TEST_ALLOC_PARENT (job->description, job);
	TEST_EQ_STR (job->author, "joe bloggs");
	TEST_ALLOC_PARENT (job->author, job);
	TEST_EQ_STR (job->version, "1.0");
	TEST_ALLOC_PARENT (job->version, job);

	TEST_EQ_STR (job->command, "/sbin/daemon -d \"arg here\"");
	TEST_ALLOC_PARENT (job->command, job);

	TEST_EQ_STR (job->start_script,
		     ("  [ -d /var/run/daemon ] || mkdir /var/run/daemon\n"
		      "[ -d /var/lock/daemon ] || mkdir /var/lock/daemon\n"));
	TEST_ALLOC_PARENT (job->start_script, job);
	TEST_EQ_STR (job->stop_script,
		     ("rm -rf /var/run/daemon /var/lock/daemon\n"));
	TEST_ALLOC_PARENT (job->stop_script, job);

	TEST_EQ_STR (job->chroot, "/jail/daemon");
	TEST_ALLOC_PARENT (job->chroot, job);
	TEST_EQ_STR (job->chdir, "/var/lib");
	TEST_ALLOC_PARENT (job->chdir, job);

	TEST_TRUE (job->respawn);
	TEST_EQ (job->console, CONSOLE_OWNER);
	TEST_EQ (job->umask, 0155);
	TEST_EQ (job->nice, -20);
	TEST_EQ (job->kill_timeout, 30);

	/* Check we got all of the start events we expected */
	i = 0;
	TEST_LIST_NOT_EMPTY (&job->start_events);
	NIH_LIST_FOREACH (&job->start_events, iter) {
		Event *event = (Event *)iter;

		TEST_ALLOC_PARENT (event, job);

		if (! strcmp (event->name, "startup")) {
			i |= 1;
		} else if (! strcmp (event->name, "explosion")) {
			i |= 2;
		} else {
			TEST_FAILED ("wrong start event, got unexpected '%s'",
				     event->name);
		}
	}
	if (i != 3)
		TEST_FAILED ("missing at least one start event");

	/* Check we got all of the start events we expected */
	i = 0;
	TEST_LIST_NOT_EMPTY (&job->stop_events);
	NIH_LIST_FOREACH (&job->stop_events, iter) {
		Event *event = (Event *)iter;

		TEST_ALLOC_PARENT (event, job);

		if (! strcmp (event->name, "shutdown")) {
			i |= 1;
		} else {
			TEST_FAILED ("wrong stop event, got unexpected '%s'",
				     event->name);
		}
	}
	if (i != 1)
		TEST_FAILED ("missing at least one stop event");

	TEST_NE_P (job->env, NULL);
	TEST_ALLOC_PARENT (job->env, job);
	TEST_EQ_STR (job->env[0], "PATH=/usr/games:/usr/bin");
	TEST_ALLOC_PARENT (job->env[0], job->env);
	TEST_EQ_STR (job->env[1], "LANG=C");
	TEST_ALLOC_PARENT (job->env[1], job->env);
	TEST_EQ_P (job->env[2], NULL);

	TEST_EQ (job->normalexit_len, 3);
	TEST_NE_P (job->normalexit, NULL);
	TEST_ALLOC_SIZE (job->normalexit, sizeof (int) * 3);
	TEST_ALLOC_PARENT (job->normalexit, job);
	TEST_EQ (job->normalexit[0], 0);
	TEST_EQ (job->normalexit[1], 99);
	TEST_EQ (job->normalexit[2], 100);

	TEST_NE_P (job->limits[RLIMIT_CORE], NULL);
	TEST_ALLOC_SIZE (job->limits[RLIMIT_CORE], sizeof (struct rlimit));
	TEST_ALLOC_PARENT (job->limits[RLIMIT_CORE], job);
	TEST_EQ (job->limits[RLIMIT_CORE]->rlim_cur, 0);
	TEST_EQ (job->limits[RLIMIT_CORE]->rlim_max, 0);

	TEST_NE_P (job->limits[RLIMIT_CPU], NULL);
	TEST_ALLOC_SIZE (job->limits[RLIMIT_CPU], sizeof (struct rlimit));
	TEST_ALLOC_PARENT (job->limits[RLIMIT_CPU], job);
	TEST_EQ (job->limits[RLIMIT_CPU]->rlim_cur, 50);
	TEST_EQ (job->limits[RLIMIT_CPU]->rlim_max, 100);

	TEST_EQ (job->respawn_limit, 5);
	TEST_EQ (job->respawn_interval, 120);

	nih_list_free (&job->entry);


	/* Check that both exec and respawn can be given together,
	 * and that respawn doesn't clear that.
	 */
	TEST_FEATURE ("with exec and respawn");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /usr/bin/foo arg\n");
	fprintf (jf, "respawn\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_TRUE (job->respawn);
	TEST_EQ_STR (job->command, "/usr/bin/foo arg");

	nih_list_free (&job->entry);


	/* Check that respawn can be given arguments, which acts like
	 * passing that and exec with those arguments.
	 */
	TEST_FEATURE ("with arguments to respawn");
	jf = fopen (filename, "w");
	fprintf (jf, "respawn /usr/bin/foo arg\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_TRUE (job->respawn);
	TEST_EQ_STR (job->command, "/usr/bin/foo arg");

	nih_list_free (&job->entry);


	/* Check that both exec and daemon can be given together,
	 * and that daemon doesn't clear that.
	 */
	TEST_FEATURE ("with exec and daemon");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /usr/bin/foo arg\n");
	fprintf (jf, "daemon\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_TRUE (job->daemon);
	TEST_EQ_STR (job->command, "/usr/bin/foo arg");

	nih_list_free (&job->entry);


	/* Check that daemon can be given arguments, which acts like
	 * passing that and exec with those arguments.
	 */
	TEST_FEATURE ("with arguments to daemon");
	jf = fopen (filename, "w");
	fprintf (jf, "daemon /usr/bin/foo arg\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_TRUE (job->daemon);
	TEST_EQ_STR (job->command, "/usr/bin/foo arg");

	nih_list_free (&job->entry);


	/* Check that the instance stanza marks the job as such. */
	TEST_FEATURE ("with instance job");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /usr/bin/foo\n");
	fprintf (jf, "instance\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_TRUE (job->spawns_instance);

	nih_list_free (&job->entry);


	/* Check an extreme case of bad formatting to make sure the config
	 * file parser does the right thing and makes it sane.
	 */
	TEST_FEATURE ("with interesting formatting");
	jf = fopen (filename, "w");
	fprintf (jf, "    description   \"foo\n");
	fprintf (jf, "   bar\"\n");
	fprintf (jf, "\n");
	fprintf (jf, "author \"  something  with  spaces  \"\n");
	fprintf (jf, "\n");
	fprintf (jf, "version 'foo\\'bar'\n");
	fprintf (jf, "\n");
	fprintf (jf, "exec /usr/bin/foo \\\n");
	fprintf (jf, "  first second \"third \n");
	fprintf (jf, "  argument\"\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_EQ_STR (job->description, "foo bar");
	TEST_EQ_STR (job->author, "  something  with  spaces  ");
	TEST_EQ_STR (job->version, "foo'bar");
	TEST_EQ_STR (job->command,
		     "/usr/bin/foo first second \"third argument\"");

	nih_list_free (&job->entry);


	/* Check that the parsing of 'end script' is strict enough to allow
	 * all sorts of other things in between.
	 */
	TEST_FEATURE ("with things that aren't script ends");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/foo\n");
	fprintf (jf, "start script\n");
	fprintf (jf, "endscript\n");
	fprintf (jf, "end foo\n");
	fprintf (jf, "end scripting\n");
	fprintf (jf, "end script # wibble\n");
	fprintf (jf, "stop script\n");
	fprintf (jf, "# ok\n");
	fprintf (jf, "  end script");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_EQ_STR (job->start_script,
		     "endscript\nend foo\nend scripting\n");
	TEST_EQ_STR (job->stop_script, "# ok\n");

	nih_list_free (&job->entry);


	/* Check that an exec line with an unterminated quote is caught.
	 */
	TEST_FEATURE ("with unterminated quote");
	jf = fopen (filename, "w");
	fprintf (jf, "exec \"/sbin/foo bar");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "1: Unterminated quoted string\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a line with a trailing slash but no following line
	 * is caught.
	 */
	TEST_FEATURE ("with trailing slash");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/foo bar \\");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "1: Trailing slash in file\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that an unfinished script stanza is caught.
	 */
	TEST_FEATURE ("with incomplete script");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/foo\n");
	fprintf (jf, "start script\n");
	fprintf (jf, "    rm /var/lock/daemon\n");
	fprintf (jf, "    rm /var/run/daemon\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "5: Unterminated block\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a job may not be missing both exec and script.
	 * Doing this causes no job to be returned.
	 */
	TEST_FEATURE ("with missing exec and script");
	jf = fopen (filename, "w");
	fprintf (jf, "description buggy");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, " 'exec' or 'script' must be specified\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a job may not supply both exec and script.
	 * Doing this causes no job to be returned.
	 */
	TEST_FEATURE ("with both exec and script");
	jf = fopen (filename, "w");
	fprintf (jf, "description buggy\n");
	fprintf (jf, "exec /sbin/foo\n");
	fprintf (jf, "script\n");
	fprintf (jf, "   /sbin/foo\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output,
		       " only one of 'exec' and 'script' may be specified\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a job may not use options that normally affect respawn
	 * if it doesn't use respawn itself.  It gets warnings.
	 */
	TEST_FEATURE ("with respawn options and not respawn");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/foo\n");
	fprintf (jf, "respawn script\n");
	fprintf (jf, "do something\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "pid file /var/run/foo.pid\n");
	fprintf (jf, "pid binary /lib/foo/foo.bin\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_ERROR_EQ (output,
		       " 'respawn script' ignored unless 'respawn' specified\n");
	TEST_ERROR_EQ (output,
		       " 'pid file' ignored unless 'respawn' specified\n");
	TEST_ERROR_EQ (output,
		       " 'pid binary' ignored unless 'respawn' specified\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);

	nih_list_free (&job->entry);


	/* Check that a non-existant file is caught properly. */
	TEST_FEATURE ("with non-existant file");
	unlink (filename);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, " unable to read: No such file or directory\n");
	TEST_FILE_END (output);


	fclose (output);

	unlink (filename);
	rmdir (dirname);
}


void
test_stanza_description (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_description");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a version stanza with an argument results in it
	 * being stored in the job.
	 */
	TEST_FEATURE ("with single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "description \"a test job\"\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->description, job);
	TEST_EQ_STR (job->description, "a test job");

	nih_list_free (&job->entry);


	/* Check that a version stanza without an argument results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "description\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a version stanza with an extra second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "description \"a test job\" \"ya ya\"\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a repeated description stanza results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with duplicate stanza");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "description \"a test job\"\n");
	fprintf (jf, "description \"ya ya\"\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
}

void
test_stanza_author (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_author");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a version stanza with an argument results in it
	 * being stored in the job.
	 */
	TEST_FEATURE ("with single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "author \"a test job\"\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->author, job);
	TEST_EQ_STR (job->author, "a test job");

	nih_list_free (&job->entry);


	/* Check that a version stanza without an argument results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "author\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a version stanza with an extra second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "author \"a test job\" \"ya ya\"\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a repeated author stanza results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with duplicate stanza");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "author \"a test job\"\n");
	fprintf (jf, "author \"ya ya\"\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
}

void
test_stanza_version (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_version");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a version stanza with an argument results in it
	 * being stored in the job.
	 */
	TEST_FEATURE ("with single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "version \"a test job\"\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->version, job);
	TEST_EQ_STR (job->version, "a test job");

	nih_list_free (&job->entry);


	/* Check that a version stanza without an argument results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "version\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a version stanza with an extra second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "version \"a test job\" \"ya ya\"\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a repeated version stanza results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with duplicate stanza");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "version \"a test job\"\n");
	fprintf (jf, "version \"ya ya\"\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
}

void
test_stanza_emits (void)
{
	Job   *job;
	Event *event;
	FILE  *jf, *output;
	char   filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_emits");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that an emits stanza with a single argument results in
	 * the named event being added to the emits list.
	 */
	TEST_FEATURE ("with single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "emits wibble\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_LIST_NOT_EMPTY (&job->emits);

	event = (Event *)job->emits.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wibble");

	nih_list_free (&job->entry);


	/* Check that an emits stanza with multiple arguments results in
	 * all of the named events being added to the emits list.
	 */
	TEST_FEATURE ("with multiple arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "emits wibble wobble waggle\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_LIST_NOT_EMPTY (&job->emits);

	event = (Event *)job->emits.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wibble");

	event = (Event *)event->entry.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wobble");

	event = (Event *)event->entry.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "waggle");

	nih_list_free (&job->entry);


	/* Check that repeated emits stanzas are permitted, each appending
	 * to the last.
	 */
	TEST_FEATURE ("with multiple stanzas");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "emits wibble\n");
	fprintf (jf, "emits wobble waggle\n");
	fprintf (jf, "emits wuggle\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_LIST_NOT_EMPTY (&job->emits);

	event = (Event *)job->emits.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wibble");

	event = (Event *)event->entry.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wobble");

	event = (Event *)event->entry.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "waggle");

	event = (Event *)event->entry.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wuggle");

	nih_list_free (&job->entry);


	/* Check that an emits stanza without an argument results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "emits\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	fclose (output);
}

void
test_stanza_on (void)
{
	Job   *job;
	Event *event;
	FILE  *jf, *output;
	char   filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_on");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that an on stanza with a single argument results in
	 * the named event being added to the start events list.
	 */
	TEST_FEATURE ("with single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "on wibble\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_LIST_NOT_EMPTY (&job->start_events);

	event = (Event *)job->start_events.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wibble");

	nih_list_free (&job->entry);


	/* Check that repeated on stanzas are permitted, each appending
	 * to the last.
	 */
	TEST_FEATURE ("with multiple stanzas");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "on wibble\n");
	fprintf (jf, "on wobble\n");
	fprintf (jf, "on waggle\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_LIST_NOT_EMPTY (&job->start_events);

	event = (Event *)job->start_events.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wibble");

	event = (Event *)event->entry.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wobble");

	event = (Event *)event->entry.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "waggle");

	nih_list_free (&job->entry);


	/* Check that on and start on stanzas can be freely intermixed.
	 */
	TEST_FEATURE ("with multiple arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "on wibble\n");
	fprintf (jf, "start on wobble\n");
	fprintf (jf, "on waggle\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_LIST_NOT_EMPTY (&job->start_events);

	event = (Event *)job->start_events.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wibble");

	event = (Event *)event->entry.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wobble");

	event = (Event *)event->entry.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "waggle");

	nih_list_free (&job->entry);


	/* Check that an on stanza without an argument results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "on\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that an on stanza with an extra argument results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "on foo bar\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
}

void
test_stanza_start (void)
{
	Job   *job;
	Event *event;
	FILE  *jf, *output;
	char   filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_start");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a start stanza with an on argument followed by an
	 * event name results in the named event being added to the
	 * start events list.
	 */
	TEST_FEATURE ("with on and single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "start on wibble\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_LIST_NOT_EMPTY (&job->start_events);

	event = (Event *)job->start_events.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wibble");

	nih_list_free (&job->entry);


	/* Check that a start stanza with a script argument begins a
	 * block which is stored in the start_script member of the job.
	 */
	TEST_FEATURE ("with script and block");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "start script\n");
	fprintf (jf, "    echo\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->start_script, job);
	TEST_EQ_STR (job->start_script, "echo\n");

	nih_list_free (&job->entry);


	/* Check that repeated start on stanzas are permitted, each appending
	 * to the last.
	 */
	TEST_FEATURE ("with multiple on stanzas");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "start on wibble\n");
	fprintf (jf, "start on wobble\n");
	fprintf (jf, "start on waggle\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_LIST_NOT_EMPTY (&job->start_events);

	event = (Event *)job->start_events.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wibble");

	event = (Event *)event->entry.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wobble");

	event = (Event *)event->entry.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "waggle");

	nih_list_free (&job->entry);


	/* Check that multiple start script stanzas results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with script and multiple blocks");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "start script\n");
	fprintf (jf, "    echo\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "start script\n");
	fprintf (jf, "    ls\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "6: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a start stanza without a second-level argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "start\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a start stanza with an unknown second-level argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with unknown argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "start foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unknown stanza\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a start on stanza without an argument results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with on and missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "start on\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a start on stanza with an extra argument results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "start on foo bar\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a start script stanza with an extra argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "start script foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
}

void
test_stanza_stop (void)
{
	Job   *job;
	Event *event;
	FILE  *jf, *output;
	char   filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_stop");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a stop stanza with an on argument followed by an
	 * event name results in the named event being added to the
	 * stop events list.
	 */
	TEST_FEATURE ("with on and single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "stop on wibble\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_LIST_NOT_EMPTY (&job->stop_events);

	event = (Event *)job->stop_events.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wibble");

	nih_list_free (&job->entry);


	/* Check that a stop stanza with a script argument begins a
	 * block which is stored in the stop_script member of the job.
	 */
	TEST_FEATURE ("with script and block");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "stop script\n");
	fprintf (jf, "    echo\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->stop_script, job);
	TEST_EQ_STR (job->stop_script, "echo\n");

	nih_list_free (&job->entry);


	/* Check that repeated stop on stanzas are permitted, each appending
	 * to the last.
	 */
	TEST_FEATURE ("with multiple on stanzas");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "stop on wibble\n");
	fprintf (jf, "stop on wobble\n");
	fprintf (jf, "stop on waggle\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_LIST_NOT_EMPTY (&job->stop_events);

	event = (Event *)job->stop_events.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wibble");

	event = (Event *)event->entry.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wobble");

	event = (Event *)event->entry.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "waggle");

	nih_list_free (&job->entry);


	/* Check that multiple stop script stanzas results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with script and multiple blocks");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "stop script\n");
	fprintf (jf, "    echo\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "stop script\n");
	fprintf (jf, "    ls\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "6: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a stop stanza without a second-level argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "stop\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a stop stanza with an unknown second-level argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with unknown argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "stop foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unknown stanza\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a stop on stanza without an argument results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with on and missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "stop on\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a stop on stanza with an extra argument results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "stop on foo bar\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a stop script stanza with an extra argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "stop script foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
}

void
test_stanza_exec (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_exec");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that an exec stanza sets the command of the job as a single
	 * string.
	 */
	TEST_FEATURE ("with arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon -d \"foo\"\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->command, job);
	TEST_EQ_STR (job->command, "/sbin/daemon -d \"foo\"");

	nih_list_free (&job->entry);


	/* Check that an exec stanza without any arguments results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with no arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "exec\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "1: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that duplicate occurances of the exec stanza
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with duplicates");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon -d\n");
	fprintf (jf, "exec /sbin/daemon\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);

	fclose (output);
}

void
test_stanza_daemon (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_daemon");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a daemon stanza without any arguments sets the job's
	 * daemon flag.
	 */
	TEST_FEATURE ("with no arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "daemon\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_TRUE (job->daemon);

	nih_list_free (&job->entry);


	/* Check that a daemon stanza with arguments sets the job's
	 * command and the daemon flag.
	 */
	TEST_FEATURE ("with arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "daemon /sbin/daemon -d \"foo\"\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_TRUE (job->daemon);
	TEST_ALLOC_PARENT (job->command, job);
	TEST_EQ_STR (job->command, "/sbin/daemon -d \"foo\"");

	nih_list_free (&job->entry);


	/* Check that duplicate occurances of the daemon stanza without
	 * arguments results in a syntax error.
	 */
	TEST_FEATURE ("with duplicate without arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "daemon\n");
	fprintf (jf, "daemon\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that duplicate occurances of the daemon or exec stanza with
	 * arguments results in a syntax error.
	 */
	TEST_FEATURE ("with duplicate with arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon -d\n");
	fprintf (jf, "daemon /sbin/daemon\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);

	fclose (output);
}

void
test_stanza_respawn (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_respawn");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a respawn stanza sets the job's respawn flag.
	 */
	TEST_FEATURE ("with no argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/respawn\n");
	fprintf (jf, "respawn\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_TRUE (job->respawn);

	nih_list_free (&job->entry);


	/* Check that a respawn stanza with arguments sets the job's
	 * command and the respawn flag.
	 */
	TEST_FEATURE ("with arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "respawn /sbin/daemon -d \"foo\"\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_TRUE (job->respawn);
	TEST_ALLOC_PARENT (job->command, job);
	TEST_EQ_STR (job->command, "/sbin/daemon -d \"foo\"");

	nih_list_free (&job->entry);


	/* Check that a respawn stanza with a script argument begins a
	 * block which is stored in the respawn_script member of the job.
	 */
	TEST_FEATURE ("with script and block");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "respawn script\n");
	fprintf (jf, "    echo\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->respawn_script, job);
	TEST_EQ_STR (job->respawn_script, "echo\n");

	nih_list_free (&job->entry);


	/* Check that a respawn stanza with the limit argument and numeric
	 * rate and timeout results in it being stored in the job.
	 */
	TEST_FEATURE ("with limit and two arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "respawn limit 10 120\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_EQ (job->respawn_limit, 10);
	TEST_EQ (job->respawn_interval, 120);

	nih_list_free (&job->entry);


	/* Check that a respawn stanza with the limit argument but no
	 * interval results in a syntax error.
	 */
	TEST_FEATURE ("with limit and missing second argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "respawn limit 10\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a respawn stanza with the limit argument but no
	 * arguments results in a syntax error.
	 */
	TEST_FEATURE ("with limit and missing arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "respawn limit\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a respawn limit stanza with a non-integer interval
	 * argument results in a syntax error.
	 */
	TEST_FEATURE ("with limit and non-integer interval argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "respawn limit 10 foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a respawn limit stanza with a non-integer limit
	 * argument results in a syntax error.
	 */
	TEST_FEATURE ("with limit and non-integer limit argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "respawn limit foo 120\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a respawn limit stanza with a partially numeric
	 * interval argument results in a syntax error.
	 */
	TEST_FEATURE ("with limit and alphanumeric interval argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "respawn limit 10 99foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a respawn limit stanza with a partially numeric
	 * limit argument results in a syntax error.
	 */
	TEST_FEATURE ("with limit and alphanumeric limit argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "respawn limit 99foo 120\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a respawn limit stanza with a negative interval
	 * value results in a syntax error.
	 */
	TEST_FEATURE ("with limit and negative interval argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "respawn limit 10 -1\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a respawn limit stanza with a negative limit
	 * value results in a syntax error.
	 */
	TEST_FEATURE ("with limit and negative interval argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "respawn limit -1 120\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that duplicate occurances of the respawn stanza without
	 * arguments results in a syntax error.
	 */
	TEST_FEATURE ("with duplicate without arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/respawn\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "respawn\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that duplicate occurances of the respawn or exec stanza with
	 * arguments results in a syntax error.
	 */
	TEST_FEATURE ("with duplicate with arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/respawn -d\n");
	fprintf (jf, "respawn /sbin/respawn\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that multiple respawn script stanzas results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with script and multiple blocks");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "respawn script\n");
	fprintf (jf, "    echo\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "respawn script\n");
	fprintf (jf, "    ls\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "7: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that duplicate respawn limit stanzas results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with duplicate limit stanzas");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "respawn limit 10 120\n");
	fprintf (jf, "respawn limit 20 90\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "4: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a respawn script stanza with an extra argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn script foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a respawn limit stanza with an extra argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn limit 0 1 foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
}

void
test_stanza_instance (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_instance");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that an instance stanza sets the job's spawn instance
	 * flag.
	 */
	TEST_FEATURE ("with no argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "instance\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_TRUE (job->spawns_instance);

	nih_list_free (&job->entry);


	/* Check that any arguments to the instance stanza results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "instance foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that duplicate occurances of the instance stanza result
	 * in a syntax error.
	 */
	TEST_FEATURE ("with duplicate");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "instance\n");
	fprintf (jf, "instance\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);

	fclose (output);
}

void
test_stanza_pid (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_pid");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a pid stanza with the file argument and a filename
	 * results in the filename being stored in the job.
	 */
	TEST_FEATURE ("with file and single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "pid file /var/run/daemon.pid\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->pid_file, job);
	TEST_EQ_STR (job->pid_file, "/var/run/daemon.pid");

	nih_list_free (&job->entry);


	/* Check that a pid stanza with the binary argument and a filename
	 * results in the filename being stored in the job.
	 */
	TEST_FEATURE ("with binary and single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "pid binary /usr/lib/daemon\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->pid_binary, job);
	TEST_EQ_STR (job->pid_binary, "/usr/lib/daemon");

	nih_list_free (&job->entry);


	/* Check that a pid stanza with the timeout argument and a numeric
	 * timeout results in it being stored in the job.
	 */
	TEST_FEATURE ("with timeout and single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "pid timeout 10\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_EQ (job->pid_timeout, 10);

	nih_list_free (&job->entry);


	/* Check that a pid stanza without an argument results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "pid\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pid stanza with an invalid second-level stanza
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with unknown second argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "pid foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Unknown stanza\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pid stanza with the file argument but no filename
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with file and missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "pid file\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pid stanza with the binary argument but no filename
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with binary and missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "pid binary\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pid stanza with the timeout argument but no timeout
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with timeout and missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "pid timeout\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pid timeout stanza with a non-integer argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with timeout and non-integer argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "pid timeout foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pid timeout stanza with a partially numeric argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with timeout and alphanumeric argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "pid timeout 99foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pid timeout stanza with a negative value results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with timeout and negative argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "pid timeout -1\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pid stanza with the file argument and filename,
	 * but with an extra argument afterwards results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with file and extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "pid file /var/run/daemon.pid foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pid stanza with the binary argument and filename,
	 * but with an extra argument afterwards results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with binary and extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "pid binary /usr/lib/daemon foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pid stanza with the timeout argument and timeout,
	 * but with an extra argument afterwards results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with timeout and extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "pid timeout 99 foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a repeated pid file stanza results in a syntax error.
	 */
	TEST_FEATURE ("with duplicate file stanza");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "pid file /var/run/daemon.pid\n");
	fprintf (jf, "pid file /var/run/daemon/daemon.pid\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a repeated pid binary stanza results in a syntax error.
	 */
	TEST_FEATURE ("with duplicate binary stanza");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "pid binary /usr/lib/daemon\n");
	fprintf (jf, "pid binary /usr/lib/daemon/daemon\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a repeated pid timeout stanza results in a syntax error.
	 */
	TEST_FEATURE ("with duplicate timeout stanza");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "pid timeout 99\n");
	fprintf (jf, "pid timeout 100\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
}

void
test_stanza_kill (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_kill");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a kill stanza with the timeout argument and a numeric
	 * timeout results in it being stored in the job.
	 */
	TEST_FEATURE ("with timeout and single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "kill timeout 10\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_EQ (job->kill_timeout, 10);

	nih_list_free (&job->entry);


	/* Check that a kill stanza without an argument results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "kill\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a kill stanza with an invalid second-level stanza
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with unknown second argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "kill foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unknown stanza\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a kill stanza with the timeout argument but no timeout
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with timeout and missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "kill timeout\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a kill timeout stanza with a non-integer argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with timeout and non-integer argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "kill timeout foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a kill timeout stanza with a partially numeric argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with timeout and alphanumeric argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "kill timeout 99foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a kill timeout stanza with a negative value results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with timeout and negative argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "kill timeout -1\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a kill stanza with the timeout argument and timeout,
	 * but with an extra argument afterwards results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with timeout and extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "kill timeout 99 foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a repeated kill timeout stanza results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with duplicate timeout stanza");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "kill timeout 99\n");
	fprintf (jf, "kill timeout 100\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
}

void
test_stanza_normalexit (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_normalexit");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a normalexit stanza with a single argument results in
	 * the exit code given being added to the normalexit array, which
	 * should be allocated.
	 */
	TEST_FEATURE ("with single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "normalexit 99\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_EQ (job->normalexit_len, 1);
	TEST_ALLOC_SIZE (job->normalexit, sizeof (int) * job->normalexit_len);
	TEST_ALLOC_PARENT (job->normalexit, job);

	TEST_EQ (job->normalexit[0], 99);

	nih_list_free (&job->entry);


	/* Check that a normalexit stanza with multiple arguments results in
	 * all of the given exit codes being added to the array, which should
	 * have been increased in size.
	 */
	TEST_FEATURE ("with multiple arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "normalexit 99 100 101\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_EQ (job->normalexit_len, 3);
	TEST_ALLOC_SIZE (job->normalexit, sizeof (int) * job->normalexit_len);
	TEST_ALLOC_PARENT (job->normalexit, job);

	TEST_EQ (job->normalexit[0], 99);
	TEST_EQ (job->normalexit[1], 100);
	TEST_EQ (job->normalexit[2], 101);

	nih_list_free (&job->entry);


	/* Check that repeated normalexit stanzas are permitted, each
	 * appending to the array.
	 */
	TEST_FEATURE ("with multiple stanzas");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "normalexit 99\n");
	fprintf (jf, "normalexit 100 101\n");
	fprintf (jf, "normalexit 900\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_EQ (job->normalexit_len, 4);
	TEST_ALLOC_SIZE (job->normalexit, sizeof (int) * job->normalexit_len);
	TEST_ALLOC_PARENT (job->normalexit, job);

	TEST_EQ (job->normalexit[0], 99);
	TEST_EQ (job->normalexit[1], 100);
	TEST_EQ (job->normalexit[2], 101);
	TEST_EQ (job->normalexit[3], 900);

	nih_list_free (&job->entry);


	/* Check that a normalexit stanza without an argument results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "normalexit\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a normalexit stanza with a non-integer argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with non-integer argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "normalexit foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a normalexit stanza with a partially numeric argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with alphanumeric argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "normalexit 99foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a normalexit stanza with a negative value results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with negative argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "normalexit -1\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
}


int
main (int   argc,
      char *argv[])
{
	test_read_job ();
	test_stanza_description ();
	test_stanza_version ();
	test_stanza_author ();
	test_stanza_emits ();
	test_stanza_on ();
	test_stanza_start ();
	test_stanza_stop ();
	test_stanza_exec ();
	test_stanza_daemon ();
	test_stanza_respawn ();
	test_stanza_instance ();
	test_stanza_pid ();
	test_stanza_kill ();
	test_stanza_normalexit ();

	return 0;
}
