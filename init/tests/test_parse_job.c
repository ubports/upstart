/* upstart
 *
 * test_parse_job.c - test suite for init/parse_job.c
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

#include <string.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/error.h>
#include <nih/logging.h>
#include <nih/errors.h>

#include "job.h"
#include "conf.h"
#include "parse_job.h"
#include "errors.h"


void
test_parse_job (void)
{
	JobConfig  *job = NULL;
	JobProcess *process;
	NihError   *err;
	size_t      pos, lineno;
	char        buf[1024];

	TEST_FUNCTION ("parse_job");
	job_init ();
	conf_init ();

	/* Check that a simple job file can be parsed, with all of the
	 * information given filled into the job structure.
	 */
	TEST_FEATURE ("with simple job file");
	strcpy (buf, "exec /sbin/daemon -d\n");
	strcat (buf, "pre-start script\n");
	strcat (buf, "    rm /var/lock/daemon\n");
	strcat (buf, "end script\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 5);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));
		TEST_EQ_P (job->start_on, NULL);
		TEST_EQ_P (job->stop_on, NULL);

		process = job->process[PROCESS_MAIN];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, FALSE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "/sbin/daemon -d");

		process = job->process[PROCESS_PRE_START];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, TRUE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "rm /var/lock/daemon\n");

		nih_free (job);
	}


	/* Check that a job may have both exec and script missing.
	 */
	TEST_FEATURE ("with missing exec and script");
	strcpy (buf, "description state\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));
		TEST_EQ_P (job->process[PROCESS_MAIN], NULL);

		nih_free (job);
	}
}

void
test_stanza_exec (void)
{
	JobConfig  *job;
	JobProcess *process;
	NihError   *err;
	size_t      pos, lineno;
	char        buf[1024];

	TEST_FUNCTION ("stanza_exec");

	/* Check that an exec stanza sets the process of the job as a single
	 * string.
	 */
	TEST_FEATURE ("with arguments");
	strcpy (buf, "exec /sbin/daemon -d \"foo\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_MAIN];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, FALSE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "/sbin/daemon -d \"foo\"");

		nih_free (job);
	}


	/* Check that the last of duplicate exec stanzas is used. */
	TEST_FEATURE ("with duplicates");
	strcpy (buf, "exec /sbin/daemon -d\n");
	strcpy (buf, "exec /sbin/daemon -d \"foo\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_MAIN];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, FALSE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "/sbin/daemon -d \"foo\"");

		nih_free (job);
	}


	/* Check that an exec stanza overrides a previous script stanza. */
	TEST_FEATURE ("with exec following script");
	strcpy (buf, "script\n");
	strcat (buf, "    echo\n");
	strcat (buf, "end script\n");
	strcpy (buf, "exec /sbin/daemon -d \"foo\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_MAIN];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, FALSE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "/sbin/daemon -d \"foo\"");

		nih_free (job);
	}


	/* Check that an exec stanza without any arguments results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with no arguments");
	strcpy (buf, "exec\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 4);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_script (void)
{
	JobConfig  *job;
	JobProcess *process;
	NihError   *err;
	size_t      pos, lineno;
	char        buf[1024];

	TEST_FUNCTION ("stanza_script");

	/* Check that a script stanza begins a block which is stored in
	 * the script member of the job.
	 */
	TEST_FEATURE ("with block");
	strcpy (buf, "script\n");
	strcat (buf, "    echo\n");
	strcat (buf, "end script\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 4);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_MAIN];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, TRUE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "echo\n");

		nih_free (job);
	}


	/* Check that the last of multiple script stanzas is used. */
	TEST_FEATURE ("with multiple blocks");
	strcpy (buf, "script\n");
	strcat (buf, "    ls\n");
	strcat (buf, "end script\n");
	strcat (buf, "script\n");
	strcat (buf, "    echo\n");
	strcat (buf, "end script\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 7);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_MAIN];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, TRUE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "echo\n");

		nih_free (job);
	}


	/* Check that a script stanza overrides a previous exec stanza. */
	TEST_FEATURE ("with script following exec");
	strcpy (buf, "exec /sbin/daemon -d \"foo\"\n");
	strcat (buf, "script\n");
	strcat (buf, "    echo\n");
	strcat (buf, "end script\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 5);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_MAIN];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, TRUE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "echo\n");

		nih_free (job);
	}


	/* Check that a script stanza with an extra argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with argument");
	strcpy (buf, "script foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 7);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_pre_start (void)
{
	JobConfig  *job;
	JobProcess *process;
	NihError   *err;
	size_t      pos, lineno;
	char        buf[1024];

	TEST_FUNCTION ("stanza_pre_start");

	/* Check that a pre-start exec stanza sets the process of the
	 * job as a single string.
	 */
	TEST_FEATURE ("with exec and command");
	strcpy (buf, "pre-start exec /bin/tool -d \"foo\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_PRE_START];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, FALSE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "/bin/tool -d \"foo\"");

		nih_free (job);
	}


	/* Check that the last of multiple pre-start exec stanzas is used. */
	TEST_FEATURE ("with multiple exec");
	strcpy (buf, "pre-start exec /bin/tool -d\n");
	strcat (buf, "pre-start exec /bin/tool -d \"foo\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 3);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_PRE_START];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, FALSE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "/bin/tool -d \"foo\"");

		nih_free (job);
	}


	/* Check that a pre-start script stanza begins a block which
	 * is stored in the process.
	 */
	TEST_FEATURE ("with script and block");
	strcpy (buf, "pre-start script\n");
	strcat (buf, "    echo\n");
	strcat (buf, "end script\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 4);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_PRE_START];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, TRUE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "echo\n");

		nih_free (job);
	}


	/* Check that the last of multiple pre-start script stanzas is used. */
	TEST_FEATURE ("with multiple script");
	strcpy (buf, "pre-start script\n");
	strcat (buf, "    ls\n");
	strcat (buf, "end script\n");
	strcat (buf, "pre-start script\n");
	strcat (buf, "    echo\n");
	strcat (buf, "end script\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 7);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_PRE_START];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, TRUE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "echo\n");

		nih_free (job);
	}


	/* Check that a script stanza overrides any previous exec stanza. */
	TEST_FEATURE ("with script following exec");
	strcpy (buf, "pre-start exec /bin/tool -d \"foo\"\n");
	strcat (buf, "pre-start script\n");
	strcat (buf, "    echo\n");
	strcat (buf, "end script\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 5);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_PRE_START];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, TRUE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "echo\n");

		nih_free (job);
	}


	/* Check that an exec stanza overrides any previous script stanza. */
	TEST_FEATURE ("with exec following script");
	strcpy (buf, "pre-start script\n");
	strcat (buf, "    echo\n");
	strcat (buf, "end script\n");
	strcat (buf, "pre-start exec /bin/tool -d \"foo\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 5);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_PRE_START];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, FALSE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "/bin/tool -d \"foo\"");

		nih_free (job);
	}


	/* Check that a pre-start exec stanza without any arguments results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with exec but no command");
	strcpy (buf, "pre-start exec\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 14);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a pre-start script stanza with an extra argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with argument to script");
	strcpy (buf, "pre-start script foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 17);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a pre-start stanza with an unknown second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with unknown argument");
	strcpy (buf, "pre-start foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNKNOWN_STANZA);
	TEST_EQ (pos, 10);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a pre-start stanza with no second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "pre-start\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 9);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_post_start (void)
{
	JobConfig  *job;
	JobProcess *process;
	NihError   *err;
	size_t      pos, lineno;
	char        buf[1024];

	TEST_FUNCTION ("stanza_post_start");

	/* Check that a post-start exec stanza sets the process of the
	 * job as a single string.
	 */
	TEST_FEATURE ("with exec and command");
	strcpy (buf, "post-start exec /bin/tool -d \"foo\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_POST_START];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, FALSE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "/bin/tool -d \"foo\"");

		nih_free (job);
	}


	/* Check that the last of multiple post-start exec stanzas is used. */
	TEST_FEATURE ("with multiple exec");
	strcpy (buf, "post-start exec /bin/tool -d\n");
	strcat (buf, "post-start exec /bin/tool -d \"foo\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 3);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_POST_START];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, FALSE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "/bin/tool -d \"foo\"");

		nih_free (job);
	}


	/* Check that a post-start script stanza begins a block which
	 * is stored in the process.
	 */
	TEST_FEATURE ("with script and block");
	strcpy (buf, "post-start script\n");
	strcat (buf, "    echo\n");
	strcat (buf, "end script\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 4);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_POST_START];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, TRUE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "echo\n");

		nih_free (job);
	}


	/* Check that the last of multiple post-start script stanzas is used. */
	TEST_FEATURE ("with multiple script");
	strcpy (buf, "post-start script\n");
	strcat (buf, "    ls\n");
	strcat (buf, "end script\n");
	strcat (buf, "post-start script\n");
	strcat (buf, "    echo\n");
	strcat (buf, "end script\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 7);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_POST_START];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, TRUE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "echo\n");

		nih_free (job);
	}


	/* Check that a script stanza overrides any previous exec stanza. */
	TEST_FEATURE ("with script following exec");
	strcpy (buf, "post-start exec /bin/tool -d \"foo\"\n");
	strcat (buf, "post-start script\n");
	strcat (buf, "    echo\n");
	strcat (buf, "end script\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 5);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_POST_START];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, TRUE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "echo\n");

		nih_free (job);
	}


	/* Check that an exec stanza overrides any previous script stanza. */
	TEST_FEATURE ("with exec following script");
	strcpy (buf, "post-start script\n");
	strcat (buf, "    echo\n");
	strcat (buf, "end script\n");
	strcat (buf, "post-start exec /bin/tool -d \"foo\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 5);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_POST_START];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, FALSE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "/bin/tool -d \"foo\"");

		nih_free (job);
	}


	/* Check that a post-start exec stanza without any arguments results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with exec but no command");
	strcpy (buf, "post-start exec\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 15);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a post-start script stanza with an extra argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with argument to script");
	strcpy (buf, "post-start script foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 18);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a post-start stanza with an unknown second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with unknown argument");
	strcpy (buf, "post-start foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNKNOWN_STANZA);
	TEST_EQ (pos, 11);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a post-start stanza with no second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "post-start\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 10);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_pre_stop (void)
{
	JobConfig  *job;
	JobProcess *process;
	NihError   *err;
	size_t      pos, lineno;
	char        buf[1024];

	TEST_FUNCTION ("stanza_pre_stop");

	/* Check that a pre-stop exec stanza sets the process of the
	 * job as a single string.
	 */
	TEST_FEATURE ("with exec and command");
	strcpy (buf, "pre-stop exec /bin/tool -d \"foo\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_PRE_STOP];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, FALSE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "/bin/tool -d \"foo\"");

		nih_free (job);
	}


	/* Check that the last of multiple pre-stop exec stanzas is used. */
	TEST_FEATURE ("with multiple exec");
	strcpy (buf, "pre-stop exec /bin/tool -d\n");
	strcat (buf, "pre-stop exec /bin/tool -d \"foo\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 3);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_PRE_STOP];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, FALSE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "/bin/tool -d \"foo\"");

		nih_free (job);
	}


	/* Check that a pre-stop script stanza begins a block which
	 * is stored in the process.
	 */
	TEST_FEATURE ("with script and block");
	strcpy (buf, "pre-stop script\n");
	strcat (buf, "    echo\n");
	strcat (buf, "end script\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 4);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_PRE_STOP];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, TRUE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "echo\n");

		nih_free (job);
	}


	/* Check that the last of multiple pre-stop script stanzas is used. */
	TEST_FEATURE ("with multiple script");
	strcpy (buf, "pre-stop script\n");
	strcat (buf, "    ls\n");
	strcat (buf, "end script\n");
	strcat (buf, "pre-stop script\n");
	strcat (buf, "    echo\n");
	strcat (buf, "end script\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 7);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_PRE_STOP];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, TRUE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "echo\n");

		nih_free (job);
	}


	/* Check that a script stanza overrides any previous exec stanza. */
	TEST_FEATURE ("with script following exec");
	strcpy (buf, "pre-stop exec /bin/tool -d \"foo\"\n");
	strcat (buf, "pre-stop script\n");
	strcat (buf, "    echo\n");
	strcat (buf, "end script\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 5);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_PRE_STOP];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, TRUE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "echo\n");

		nih_free (job);
	}


	/* Check that an exec stanza overrides any previous script stanza. */
	TEST_FEATURE ("with exec following script");
	strcpy (buf, "pre-stop script\n");
	strcat (buf, "    echo\n");
	strcat (buf, "end script\n");
	strcat (buf, "pre-stop exec /bin/tool -d \"foo\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 5);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_PRE_STOP];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, FALSE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "/bin/tool -d \"foo\"");

		nih_free (job);
	}


	/* Check that a pre-stop exec stanza without any arguments results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with exec but no command");
	strcpy (buf, "pre-stop exec\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 13);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a pre-stop script stanza with an extra argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with argument to script");
	strcpy (buf, "pre-stop script foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 16);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a pre-stop stanza with an unknown second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with unknown argument");
	strcpy (buf, "pre-stop foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNKNOWN_STANZA);
	TEST_EQ (pos, 9);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a pre-stop stanza with no second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "pre-stop\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 8);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_post_stop (void)
{
	JobConfig  *job;
	JobProcess *process;
	NihError   *err;
	size_t      pos, lineno;
	char        buf[1024];

	TEST_FUNCTION ("stanza_post_stop");

	/* Check that a post-stop exec stanza sets the process of the
	 * job as a single string.
	 */
	TEST_FEATURE ("with exec and command");
	strcpy (buf, "post-stop exec /bin/tool -d \"foo\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_POST_STOP];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, FALSE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "/bin/tool -d \"foo\"");

		nih_free (job);
	}


	/* Check that the last of multiple post-stop exec stanzas is used. */
	TEST_FEATURE ("with multiple exec");
	strcpy (buf, "post-stop exec /bin/tool -d\n");
	strcat (buf, "post-stop exec /bin/tool -d \"foo\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 3);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_POST_STOP];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, FALSE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "/bin/tool -d \"foo\"");

		nih_free (job);
	}


	/* Check that a post-stop script stanza begins a block which
	 * is stored in the process.
	 */
	TEST_FEATURE ("with script and block");
	strcpy (buf, "post-stop script\n");
	strcat (buf, "    echo\n");
	strcat (buf, "end script\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 4);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_POST_STOP];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, TRUE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "echo\n");

		nih_free (job);
	}


	/* Check that the last of multiple post-stop script stanzas is used. */
	TEST_FEATURE ("with multiple script");
	strcpy (buf, "post-stop script\n");
	strcat (buf, "    ls\n");
	strcat (buf, "end script\n");
	strcat (buf, "post-stop script\n");
	strcat (buf, "    echo\n");
	strcat (buf, "end script\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 7);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_POST_STOP];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, TRUE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "echo\n");

		nih_free (job);
	}


	/* Check that a script stanza overrides any previous exec stanza. */
	TEST_FEATURE ("with script following exec");
	strcpy (buf, "post-stop exec /bin/tool -d \"foo\"\n");
	strcat (buf, "post-stop script\n");
	strcat (buf, "    echo\n");
	strcat (buf, "end script\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 5);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_POST_STOP];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, TRUE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "echo\n");

		nih_free (job);
	}


	/* Check that an exec stanza overrides any previous script stanza. */
	TEST_FEATURE ("with exec following script");
	strcpy (buf, "post-stop script\n");
	strcat (buf, "    echo\n");
	strcat (buf, "end script\n");
	strcat (buf, "post-stop exec /bin/tool -d \"foo\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 5);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		process = job->process[PROCESS_POST_STOP];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (JobProcess));
		TEST_EQ (process->script, FALSE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "/bin/tool -d \"foo\"");

		nih_free (job);
	}


	/* Check that a post-stop exec stanza without any arguments results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with exec but no command");
	strcpy (buf, "post-stop exec\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 14);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a post-stop script stanza with an extra argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with argument to script");
	strcpy (buf, "post-stop script foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 17);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a post-stop stanza with an unknown second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with unknown argument");
	strcpy (buf, "post-stop foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNKNOWN_STANZA);
	TEST_EQ (pos, 10);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a post-stop stanza with no second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "post-stop\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 9);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_start (void)
{
	JobConfig     *job;
	EventOperator *oper;
	NihError      *err;
	size_t         pos, lineno;
	char           buf[1024];

	TEST_FUNCTION ("stanza_start");

	/* Check that a start on stanza may have a single event name,
	 * which will be the sole operator in the expression.
	 */
	TEST_FEATURE ("with event name");
	strcpy (buf, "start on wibble\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_SIZE (job->start_on, sizeof (EventOperator));
		TEST_ALLOC_PARENT (job->start_on, job);

		oper = job->start_on;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wibble");
		TEST_EQ_P (oper->args, NULL);

		TEST_EQ_P (oper->node.parent, NULL);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		nih_free (job);
	}


	/* Check that a start on stanza may have an event name followed
	 * by multiple arguments,the event will be the sole operator in
	 * the expression, and have the additional arguments as arguments
	 * to the event.
	 */
	TEST_FEATURE ("with event name and arguments");
	strcpy (buf, "start on wibble foo bar b?z*\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_SIZE (job->start_on, sizeof (EventOperator));
		TEST_ALLOC_PARENT (job->start_on, job);

		oper = job->start_on;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wibble");

		TEST_ALLOC_SIZE (oper->args, sizeof (char *) * 4);
		TEST_EQ_STR (oper->args[0], "foo");
		TEST_EQ_STR (oper->args[1], "bar");
		TEST_EQ_STR (oper->args[2], "b?z*");
		TEST_EQ_P (oper->args[3], NULL);

		TEST_EQ_P (oper->node.parent, NULL);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		nih_free (job);
	}


	/* Check that a start on stanza may have a multiple events seperated
	 * by an operator; the operator will be the root of the expression,
	 * with the two events as its children.
	 */
	TEST_FEATURE ("with operator and two events");
	strcpy (buf, "start on wibble or wobble\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_SIZE (job->start_on, sizeof (EventOperator));
		TEST_ALLOC_PARENT (job->start_on, job);

		oper = job->start_on;
		TEST_EQ (oper->type, EVENT_OR);

		TEST_EQ_P (oper->node.parent, NULL);
		TEST_ALLOC_SIZE (oper->node.left, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.left, oper);
		TEST_ALLOC_SIZE (oper->node.right, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.right, oper);

		oper = (EventOperator *)job->start_on->node.left;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wibble");
		TEST_EQ_P (oper->args, NULL);

		TEST_EQ_P (oper->node.parent, &job->start_on->node);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->start_on->node.right;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wobble");
		TEST_EQ_P (oper->args, NULL);

		TEST_EQ_P (oper->node.parent, &job->start_on->node);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		nih_free (job);
	}


	/* Check that a start on stanza may have a multiple events seperated
	 * by an operator, and that those events may have arguments; the
	 * operator will be the root of the expression, with the two events
	 * as its children.
	 */
	TEST_FEATURE ("with operator and two events with arguments");
	strcpy (buf, "start on wibble foo bar and wobble frodo bilbo\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_SIZE (job->start_on, sizeof (EventOperator));
		TEST_ALLOC_PARENT (job->start_on, job);

		oper = job->start_on;
		TEST_EQ (oper->type, EVENT_AND);

		TEST_EQ_P (oper->node.parent, NULL);
		TEST_ALLOC_SIZE (oper->node.left, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.left, oper);
		TEST_ALLOC_SIZE (oper->node.right, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.right, oper);

		oper = (EventOperator *)job->start_on->node.left;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wibble");

		TEST_ALLOC_SIZE (oper->args, sizeof (char *) * 3);
		TEST_EQ_STR (oper->args[0], "foo");
		TEST_EQ_STR (oper->args[1], "bar");
		TEST_EQ_P (oper->args[2], NULL);

		TEST_EQ_P (oper->node.parent, &job->start_on->node);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->start_on->node.right;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wobble");

		TEST_ALLOC_SIZE (oper->args, sizeof (char *) * 3);
		TEST_EQ_STR (oper->args[0], "frodo");
		TEST_EQ_STR (oper->args[1], "bilbo");
		TEST_EQ_P (oper->args[2], NULL);

		TEST_EQ_P (oper->node.parent, &job->start_on->node);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		nih_free (job);
	}


	/* Check that a start on stanza may have a multiple events seperated
	 * by multiple operators; the operators should be left-associative,
	 * and stack up.
	 */
	TEST_FEATURE ("with multiple operators");
	strcpy (buf, "start on wibble or wobble or wiggle\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_SIZE (job->start_on, sizeof (EventOperator));
		TEST_ALLOC_PARENT (job->start_on, job);

		oper = job->start_on;
		TEST_EQ (oper->type, EVENT_OR);

		TEST_EQ_P (oper->node.parent, NULL);
		TEST_ALLOC_SIZE (oper->node.left, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.left, oper);
		TEST_ALLOC_SIZE (oper->node.right, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.right, oper);

		oper = (EventOperator *)job->start_on->node.left;
		TEST_EQ (oper->type, EVENT_OR);
		TEST_EQ_P (oper->node.parent, &job->start_on->node);
		TEST_ALLOC_SIZE (oper->node.left, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.left, oper);
		TEST_ALLOC_SIZE (oper->node.right, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.right, oper);

		oper = (EventOperator *)job->start_on->node.left->left;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wibble");
		TEST_EQ_P (oper->args, NULL);

		TEST_EQ_P (oper->node.parent, job->start_on->node.left);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->start_on->node.left->right;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wobble");
		TEST_EQ_P (oper->args, NULL);

		TEST_EQ_P (oper->node.parent, job->start_on->node.left);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->start_on->node.right;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wiggle");
		TEST_EQ_P (oper->args, NULL);

		TEST_EQ_P (oper->node.parent, &job->start_on->node);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		nih_free (job);
	}


	/* Check that a start on stanza may have groups of operators
	 * placed with parentheses, altering the expression structure.
	 */
	TEST_FEATURE ("with parentheses");
	strcpy (buf, "start on wibble or (wobble or wiggle)\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_SIZE (job->start_on, sizeof (EventOperator));
		TEST_ALLOC_PARENT (job->start_on, job);

		oper = job->start_on;
		TEST_EQ (oper->type, EVENT_OR);

		TEST_EQ_P (oper->node.parent, NULL);
		TEST_ALLOC_SIZE (oper->node.left, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.left, oper);
		TEST_ALLOC_SIZE (oper->node.right, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.right, oper);

		oper = (EventOperator *)job->start_on->node.left;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wibble");
		TEST_EQ_P (oper->args, NULL);

		TEST_EQ_P (oper->node.parent, &job->start_on->node);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->start_on->node.right;
		TEST_EQ (oper->type, EVENT_OR);

		TEST_EQ_P (oper->node.parent, &job->start_on->node);
		TEST_ALLOC_SIZE (oper->node.left, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.left, oper);
		TEST_ALLOC_SIZE (oper->node.right, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.right, oper);

		oper = (EventOperator *)job->start_on->node.right->left;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wobble");
		TEST_EQ_P (oper->args, NULL);

		TEST_EQ_P (oper->node.parent, job->start_on->node.right);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->start_on->node.right->right;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wiggle");
		TEST_EQ_P (oper->args, NULL);

		TEST_EQ_P (oper->node.parent, job->start_on->node.right);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		nih_free (job);
	}


	/* Check that a start on stanza may have nested groups of parentheses,
	 * and that newlines are treated as whitespace within them.
	 */
	TEST_FEATURE ("with nested parentheses");
	strcpy (buf, "start on (wibble\n");
	strcat (buf, "          or (wobble or wiggle))\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 3);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_SIZE (job->start_on, sizeof (EventOperator));
		TEST_ALLOC_PARENT (job->start_on, job);

		oper = job->start_on;
		TEST_EQ (oper->type, EVENT_OR);

		TEST_EQ_P (oper->node.parent, NULL);
		TEST_ALLOC_SIZE (oper->node.left, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.left, oper);
		TEST_ALLOC_SIZE (oper->node.right, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.right, oper);

		oper = (EventOperator *)job->start_on->node.left;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wibble");
		TEST_EQ_P (oper->args, NULL);

		TEST_EQ_P (oper->node.parent, &job->start_on->node);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->start_on->node.right;
		TEST_EQ (oper->type, EVENT_OR);

		TEST_EQ_P (oper->node.parent, &job->start_on->node);
		TEST_ALLOC_SIZE (oper->node.left, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.left, oper);
		TEST_ALLOC_SIZE (oper->node.right, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.right, oper);

		oper = (EventOperator *)job->start_on->node.right->left;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wobble");
		TEST_EQ_P (oper->args, NULL);

		TEST_EQ_P (oper->node.parent, job->start_on->node.right);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->start_on->node.right->right;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wiggle");
		TEST_EQ_P (oper->args, NULL);

		TEST_EQ_P (oper->node.parent, job->start_on->node.right);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		nih_free (job);
	}


	/* Check that the last of repeated start on stanzas is used. */
	TEST_FEATURE ("with multiple on stanzas");
	strcpy (buf, "start on wibble or wiggle\n");
	strcat (buf, "start on wobble and wave\n");
	strcat (buf, "start on waggle\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 4);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_SIZE (job->start_on, sizeof (EventOperator));
		TEST_ALLOC_PARENT (job->start_on, job);

		oper = job->start_on;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "waggle");
		TEST_EQ_P (oper->args, NULL);

		TEST_EQ_P (oper->node.parent, NULL);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		nih_free (job);
	}


	/* Check that a start stanza without a second-level argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "start\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 5);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a start stanza with an unknown second-level argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with unknown argument");
	strcpy (buf, "start foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNKNOWN_STANZA);
	TEST_EQ (pos, 6);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a start on stanza without an argument results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with on and missing argument");
	strcpy (buf, "start on\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 8);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that starting the expression with an operator results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with operator at start of expression");
	strcpy (buf, "start on or foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_EXPECTED_EVENT);
	TEST_EQ (pos, 9);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that ending the expression with an operator results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with operator at end of expression");
	strcpy (buf, "start on foo or\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_EXPECTED_EVENT);
	TEST_EQ (pos, 13);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that two operators in a row result in a syntax error. */
	TEST_FEATURE ("with consecutive operators");
	strcpy (buf, "start on foo or and bar\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_EXPECTED_EVENT);
	TEST_EQ (pos, 16);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that starting a group expression with an operator results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with operator at start of group");
	strcpy (buf, "start on foo or (or foo)\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_EXPECTED_EVENT);
	TEST_EQ (pos, 17);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that ending a group expression with an operator results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with operator at end of group");
	strcpy (buf, "start on foo or (bar or)\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_EXPECTED_EVENT);
	TEST_EQ (pos, 23);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that failing to start a group expression results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with missing open paren");
	strcpy (buf, "start on foo or bar or foo)\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_MISMATCHED_PARENS);
	TEST_EQ (pos, 26);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that failing to end a group expression results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with missing close paren");
	strcpy (buf, "start on foo or (bar or foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_MISMATCHED_PARENS);
	TEST_EQ (pos, 28);
	TEST_EQ (lineno, 2);
	nih_free (err);


	/* Check that a group expression following an event name results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with group immediately after event");
	strcpy (buf, "start on frodo (foo or bar)\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_EXPECTED_OPERATOR);
	TEST_EQ (pos, 15);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that an event name following a group expression results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with event immediately after group");
	strcpy (buf, "start on (foo or bar) frodo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_EXPECTED_OPERATOR);
	TEST_EQ (pos, 22);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_stop (void)
{
	JobConfig     *job;
	EventOperator *oper;
	NihError      *err;
	size_t         pos, lineno;
	char           buf[1024];

	TEST_FUNCTION ("stanza_stop");

	/* Check that a stop on stanza may have a single event name,
	 * which will be the sole operator in the expression.
	 */
	TEST_FEATURE ("with event name");
	strcpy (buf, "stop on wibble\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_SIZE (job->stop_on, sizeof (EventOperator));
		TEST_ALLOC_PARENT (job->stop_on, job);

		oper = job->stop_on;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wibble");
		TEST_EQ_P (oper->args, NULL);

		TEST_EQ_P (oper->node.parent, NULL);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		nih_free (job);
	}


	/* Check that a stop on stanza may have an event name followed
	 * by multiple arguments,the event will be the sole operator in
	 * the expression, and have the additional arguments as arguments
	 * to the event.
	 */
	TEST_FEATURE ("with event name and arguments");
	strcpy (buf, "stop on wibble foo bar b?z*\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_SIZE (job->stop_on, sizeof (EventOperator));
		TEST_ALLOC_PARENT (job->stop_on, job);

		oper = job->stop_on;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wibble");

		TEST_ALLOC_SIZE (oper->args, sizeof (char *) * 4);
		TEST_EQ_STR (oper->args[0], "foo");
		TEST_EQ_STR (oper->args[1], "bar");
		TEST_EQ_STR (oper->args[2], "b?z*");
		TEST_EQ_P (oper->args[3], NULL);

		TEST_EQ_P (oper->node.parent, NULL);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		nih_free (job);
	}


	/* Check that a stop on stanza may have a multiple events seperated
	 * by an operator; the operator will be the root of the expression,
	 * with the two events as its children.
	 */
	TEST_FEATURE ("with operator and two events");
	strcpy (buf, "stop on wibble or wobble\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_SIZE (job->stop_on, sizeof (EventOperator));
		TEST_ALLOC_PARENT (job->stop_on, job);

		oper = job->stop_on;
		TEST_EQ (oper->type, EVENT_OR);

		TEST_EQ_P (oper->node.parent, NULL);
		TEST_ALLOC_SIZE (oper->node.left, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.left, oper);
		TEST_ALLOC_SIZE (oper->node.right, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.right, oper);

		oper = (EventOperator *)job->stop_on->node.left;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wibble");
		TEST_EQ_P (oper->args, NULL);

		TEST_EQ_P (oper->node.parent, &job->stop_on->node);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->stop_on->node.right;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wobble");
		TEST_EQ_P (oper->args, NULL);

		TEST_EQ_P (oper->node.parent, &job->stop_on->node);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		nih_free (job);
	}


	/* Check that a stop on stanza may have a multiple events seperated
	 * by an operator, and that those events may have arguments; the
	 * operator will be the root of the expression, with the two events
	 * as its children.
	 */
	TEST_FEATURE ("with operator and two events with arguments");
	strcpy (buf, "stop on wibble foo bar and wobble frodo bilbo\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_SIZE (job->stop_on, sizeof (EventOperator));
		TEST_ALLOC_PARENT (job->stop_on, job);

		oper = job->stop_on;
		TEST_EQ (oper->type, EVENT_AND);

		TEST_EQ_P (oper->node.parent, NULL);
		TEST_ALLOC_SIZE (oper->node.left, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.left, oper);
		TEST_ALLOC_SIZE (oper->node.right, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.right, oper);

		oper = (EventOperator *)job->stop_on->node.left;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wibble");

		TEST_ALLOC_SIZE (oper->args, sizeof (char *) * 3);
		TEST_EQ_STR (oper->args[0], "foo");
		TEST_EQ_STR (oper->args[1], "bar");
		TEST_EQ_P (oper->args[2], NULL);

		TEST_EQ_P (oper->node.parent, &job->stop_on->node);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->stop_on->node.right;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wobble");

		TEST_ALLOC_SIZE (oper->args, sizeof (char *) * 3);
		TEST_EQ_STR (oper->args[0], "frodo");
		TEST_EQ_STR (oper->args[1], "bilbo");
		TEST_EQ_P (oper->args[2], NULL);

		TEST_EQ_P (oper->node.parent, &job->stop_on->node);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		nih_free (job);
	}


	/* Check that a stop on stanza may have a multiple events seperated
	 * by multiple operators; the operators should be left-associative,
	 * and stack up.
	 */
	TEST_FEATURE ("with multiple operators");
	strcpy (buf, "stop on wibble or wobble or wiggle\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_SIZE (job->stop_on, sizeof (EventOperator));
		TEST_ALLOC_PARENT (job->stop_on, job);

		oper = job->stop_on;
		TEST_EQ (oper->type, EVENT_OR);

		TEST_EQ_P (oper->node.parent, NULL);
		TEST_ALLOC_SIZE (oper->node.left, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.left, oper);
		TEST_ALLOC_SIZE (oper->node.right, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.right, oper);

		oper = (EventOperator *)job->stop_on->node.left;
		TEST_EQ (oper->type, EVENT_OR);
		TEST_EQ_P (oper->node.parent, &job->stop_on->node);
		TEST_ALLOC_SIZE (oper->node.left, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.left, oper);
		TEST_ALLOC_SIZE (oper->node.right, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.right, oper);

		oper = (EventOperator *)job->stop_on->node.left->left;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wibble");
		TEST_EQ_P (oper->args, NULL);

		TEST_EQ_P (oper->node.parent, job->stop_on->node.left);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->stop_on->node.left->right;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wobble");
		TEST_EQ_P (oper->args, NULL);

		TEST_EQ_P (oper->node.parent, job->stop_on->node.left);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->stop_on->node.right;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wiggle");
		TEST_EQ_P (oper->args, NULL);

		TEST_EQ_P (oper->node.parent, &job->stop_on->node);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		nih_free (job);
	}


	/* Check that a stop on stanza may have groups of operators
	 * placed with parentheses, altering the expression structure.
	 */
	TEST_FEATURE ("with parentheses");
	strcpy (buf, "stop on wibble or (wobble or wiggle)\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_SIZE (job->stop_on, sizeof (EventOperator));
		TEST_ALLOC_PARENT (job->stop_on, job);

		oper = job->stop_on;
		TEST_EQ (oper->type, EVENT_OR);

		TEST_EQ_P (oper->node.parent, NULL);
		TEST_ALLOC_SIZE (oper->node.left, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.left, oper);
		TEST_ALLOC_SIZE (oper->node.right, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.right, oper);

		oper = (EventOperator *)job->stop_on->node.left;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wibble");
		TEST_EQ_P (oper->args, NULL);

		TEST_EQ_P (oper->node.parent, &job->stop_on->node);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->stop_on->node.right;
		TEST_EQ (oper->type, EVENT_OR);

		TEST_EQ_P (oper->node.parent, &job->stop_on->node);
		TEST_ALLOC_SIZE (oper->node.left, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.left, oper);
		TEST_ALLOC_SIZE (oper->node.right, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.right, oper);

		oper = (EventOperator *)job->stop_on->node.right->left;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wobble");
		TEST_EQ_P (oper->args, NULL);

		TEST_EQ_P (oper->node.parent, job->stop_on->node.right);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->stop_on->node.right->right;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wiggle");
		TEST_EQ_P (oper->args, NULL);

		TEST_EQ_P (oper->node.parent, job->stop_on->node.right);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		nih_free (job);
	}


	/* Check that a stop on stanza may have nested groups of parentheses,
	 * and that newlines are treated as whitespace within them.
	 */
	TEST_FEATURE ("with nested parentheses");
	strcpy (buf, "stop on (wibble\n");
	strcat (buf, "          or (wobble or wiggle))\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 3);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_SIZE (job->stop_on, sizeof (EventOperator));
		TEST_ALLOC_PARENT (job->stop_on, job);

		oper = job->stop_on;
		TEST_EQ (oper->type, EVENT_OR);

		TEST_EQ_P (oper->node.parent, NULL);
		TEST_ALLOC_SIZE (oper->node.left, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.left, oper);
		TEST_ALLOC_SIZE (oper->node.right, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.right, oper);

		oper = (EventOperator *)job->stop_on->node.left;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wibble");
		TEST_EQ_P (oper->args, NULL);

		TEST_EQ_P (oper->node.parent, &job->stop_on->node);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->stop_on->node.right;
		TEST_EQ (oper->type, EVENT_OR);

		TEST_EQ_P (oper->node.parent, &job->stop_on->node);
		TEST_ALLOC_SIZE (oper->node.left, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.left, oper);
		TEST_ALLOC_SIZE (oper->node.right, sizeof (EventOperator));
		TEST_ALLOC_PARENT (oper->node.right, oper);

		oper = (EventOperator *)job->stop_on->node.right->left;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wobble");
		TEST_EQ_P (oper->args, NULL);

		TEST_EQ_P (oper->node.parent, job->stop_on->node.right);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->stop_on->node.right->right;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wiggle");
		TEST_EQ_P (oper->args, NULL);

		TEST_EQ_P (oper->node.parent, job->stop_on->node.right);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		nih_free (job);
	}


	/* Check that the last of repeated stop on stanzas is used. */
	TEST_FEATURE ("with multiple on stanzas");
	strcpy (buf, "stop on wibble or wiggle\n");
	strcat (buf, "stop on wobble and wave\n");
	strcat (buf, "stop on waggle\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 4);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_SIZE (job->stop_on, sizeof (EventOperator));
		TEST_ALLOC_PARENT (job->stop_on, job);

		oper = job->stop_on;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "waggle");
		TEST_EQ_P (oper->args, NULL);

		TEST_EQ_P (oper->node.parent, NULL);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		nih_free (job);
	}


	/* Check that a stop stanza without a second-level argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "stop\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 4);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a stop stanza with an unknown second-level argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with unknown argument");
	strcpy (buf, "stop foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNKNOWN_STANZA);
	TEST_EQ (pos, 5);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a stop on stanza without an argument results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with on and missing argument");
	strcpy (buf, "stop on\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 7);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that starting the expression with an operator results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with operator at start of expression");
	strcpy (buf, "stop on or foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_EXPECTED_EVENT);
	TEST_EQ (pos, 8);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that ending the expression with an operator results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with operator at end of expression");
	strcpy (buf, "stop on foo or\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_EXPECTED_EVENT);
	TEST_EQ (pos, 12);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that two operators in a row result in a syntax error. */
	TEST_FEATURE ("with consecutive operators");
	strcpy (buf, "stop on foo or and bar\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_EXPECTED_EVENT);
	TEST_EQ (pos, 15);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that starting a group expression with an operator results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with operator at start of group");
	strcpy (buf, "stop on foo or (or foo)\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_EXPECTED_EVENT);
	TEST_EQ (pos, 16);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that ending a group expression with an operator results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with operator at end of group");
	strcpy (buf, "stop on foo or (bar or)\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_EXPECTED_EVENT);
	TEST_EQ (pos, 22);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that failing to start a group expression results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with missing open paren");
	strcpy (buf, "stop on foo or bar or foo)\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_MISMATCHED_PARENS);
	TEST_EQ (pos, 25);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that failing to end a group expression results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with missing close paren");
	strcpy (buf, "stop on foo or (bar or foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_MISMATCHED_PARENS);
	TEST_EQ (pos, 27);
	TEST_EQ (lineno, 2);
	nih_free (err);


	/* Check that a group expression following an event name results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with group immediately after event");
	strcpy (buf, "stop on frodo (foo or bar)\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_EXPECTED_OPERATOR);
	TEST_EQ (pos, 14);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that an event name following a group expression results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with event immediately after group");
	strcpy (buf, "stop on (foo or bar) frodo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_EXPECTED_OPERATOR);
	TEST_EQ (pos, 21);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_description (void)
{
	JobConfig*job;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_description");

	/* Check that a description stanza with an argument results in it
	 * being stored in the job.
	 */
	TEST_FEATURE ("with single argument");
	strcpy (buf, "description \"a test job\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->description, job);
		TEST_EQ_STR (job->description, "a test job");

		nih_free (job);
	}


	/* Check that the last of duplicate description stanzas is used. */
	TEST_FEATURE ("with multiple stanzas");
	strcpy (buf, "description \"an example job\"\n");
	strcat (buf, "description \"a test job\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 3);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->description, job);
		TEST_EQ_STR (job->description, "a test job");

		nih_free (job);
	}


	/* Check that a description stanza without an argument results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "description\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 11);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a description stanza with an extra second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	strcpy (buf, "description \"a test job\" \"ya ya\"\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 25);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_author (void)
{
	JobConfig*job;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_author");

	/* Check that a author stanza with an argument results in it
	 * being stored in the job.
	 */
	TEST_FEATURE ("with single argument");
	strcpy (buf, "author \"joe bloggs\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->author, job);
		TEST_EQ_STR (job->author, "joe bloggs");

		nih_free (job);
	}


	/* Check that the last of multiple author stanzas is used. */
	TEST_FEATURE ("with multiple stanzas");
	strcpy (buf, "author \"john doe\"\n");
	strcat (buf, "author \"joe bloggs\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 3);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->author, job);
		TEST_EQ_STR (job->author, "joe bloggs");

		nih_free (job);
	}


	/* Check that a author stanza without an argument results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "author\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 6);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a author stanza with an extra second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	strcpy (buf, "author \"joe bloggs\" \"john doe\"\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 20);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_version (void)
{
	JobConfig*job;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_version");

	/* Check that a version stanza with an argument results in it
	 * being stored in the job.
	 */
	TEST_FEATURE ("with single argument");
	strcpy (buf, "version \"1.0\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->version, job);
		TEST_EQ_STR (job->version, "1.0");

		nih_free (job);
	}


	/* Check that the last of multiple version stanzas is used. */
	TEST_FEATURE ("with multiple stanzas");
	strcpy (buf, "version \"0.8\"\n");
	strcat (buf, "version \"1.0\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 3);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->version, job);
		TEST_EQ_STR (job->version, "1.0");

		nih_free (job);
	}


	/* Check that a version stanza without an argument results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "version\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 7);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a version stanza with an extra second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	strcpy (buf, "version \"1.0\" \"0.8\"\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 14);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_emits (void)
{
	JobConfig    *job;
	NihListEntry *emits;
	NihError     *err;
	size_t        pos, lineno;
	char          buf[1024];

	TEST_FUNCTION ("stanza_emits");

	/* Check that an emits stanza with a single argument results in
	 * the named event being added to the emits list.
	 */
	TEST_FEATURE ("with single argument");
	strcpy (buf, "emits wibble\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));
		TEST_LIST_NOT_EMPTY (&job->emits);

		emits = (NihListEntry *)job->emits.next;
		TEST_ALLOC_SIZE (emits, sizeof (NihListEntry));
		TEST_EQ_STR (emits->str, "wibble");
		TEST_ALLOC_PARENT (emits->str, emits);

		nih_free (job);
	}


	/* Check that an emits stanza with multiple arguments results in
	 * all of the named events being added to the emits list.
	 */
	TEST_FEATURE ("with multiple arguments");
	strcpy (buf, "emits wibble wobble waggle\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));
		TEST_LIST_NOT_EMPTY (&job->emits);

		emits = (NihListEntry *)job->emits.next;
		TEST_ALLOC_SIZE (emits, sizeof (NihListEntry));
		TEST_EQ_STR (emits->str, "wibble");
		TEST_ALLOC_PARENT (emits->str, emits);

		emits = (NihListEntry *)emits->entry.next;
		TEST_ALLOC_SIZE (emits, sizeof (NihListEntry));
		TEST_EQ_STR (emits->str, "wobble");
		TEST_ALLOC_PARENT (emits->str, emits);

		emits = (NihListEntry *)emits->entry.next;
		TEST_ALLOC_SIZE (emits, sizeof (NihListEntry));
		TEST_EQ_STR (emits->str, "waggle");
		TEST_ALLOC_PARENT (emits->str, emits);

		nih_free (job);
	}


	/* Check that repeated emits stanzas are permitted, each appending
	 * to the last.
	 */
	TEST_FEATURE ("with multiple stanzas");
	strcpy (buf, "emits wibble\n");
	strcat (buf, "emits wobble waggle\n");
	strcat (buf, "emits wuggle\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 4);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));
		TEST_LIST_NOT_EMPTY (&job->emits);

		emits = (NihListEntry *)job->emits.next;
		TEST_ALLOC_SIZE (emits, sizeof (NihListEntry));
		TEST_EQ_STR (emits->str, "wibble");
		TEST_ALLOC_PARENT (emits->str, emits);

		emits = (NihListEntry *)emits->entry.next;
		TEST_ALLOC_SIZE (emits, sizeof (NihListEntry));
		TEST_EQ_STR (emits->str, "wobble");
		TEST_ALLOC_PARENT (emits->str, emits);

		emits = (NihListEntry *)emits->entry.next;
		TEST_ALLOC_SIZE (emits, sizeof (NihListEntry));
		TEST_EQ_STR (emits->str, "waggle");
		TEST_ALLOC_PARENT (emits->str, emits);

		emits = (NihListEntry *)emits->entry.next;
		TEST_ALLOC_SIZE (emits, sizeof (NihListEntry));
		TEST_EQ_STR (emits->str, "wuggle");
		TEST_ALLOC_PARENT (emits->str, emits);

		nih_free (job);
	}


	/* Check that an emits stanza without an argument results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "emits\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 5);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_wait (void)
{
	JobConfig*job;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_wait");

	/* Check that wait for stop sets the job's wait for member to
	 * JOB_WAIT_STOP.
	 */
	TEST_FEATURE ("with stop argument");
	strcpy (buf, "wait for stop\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_EQ (job->wait_for, JOB_WAIT_STOP);

		nih_free (job);
	}


	/* Check that wait for daemon sets the job's wait for member to
	 * JOB_WAIT_DAEMON.
	 */
	TEST_FEATURE ("with daemon argument");
	strcpy (buf, "wait for daemon\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_EQ (job->wait_for, JOB_WAIT_DAEMON);

		nih_free (job);
	}


	/* Check that wait for fork sets the job's wait for member to
	 * JOB_WAIT_FORK.
	 */
	TEST_FEATURE ("with fork argument");
	strcpy (buf, "wait for fork\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_EQ (job->wait_for, JOB_WAIT_FORK);

		nih_free (job);
	}


	/* Check that wait for none sets the job's wait for member to
	 * JOB_WAIT_NONE.
	 */
	TEST_FEATURE ("with none argument");
	strcpy (buf, "wait for none\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_EQ (job->wait_for, JOB_WAIT_NONE);

		nih_free (job);
	}


	/* Check that the last of multiple wait for stanzas is used.
	 */
	TEST_FEATURE ("with multiple for stanzas");
	strcpy (buf, "wait for stop\n");
	strcat (buf, "wait for none\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 3);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_EQ (job->wait_for, JOB_WAIT_NONE);

		nih_free (job);
	}


	/* Check that a wait for stanza without an argument results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "wait for\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 8);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a wait for stanza with an unknown third argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with unknown third argument");
	strcpy (buf, "wait for foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNKNOWN_STANZA);
	TEST_EQ (pos, 9);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a wait for stanza with an extra fourth argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	strcpy (buf, "wait for daemon foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 16);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a wait stanza with something other than "for"
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with unknown argument");
	strcpy (buf, "wait wibble\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNKNOWN_STANZA);
	TEST_EQ (pos, 5);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a wait stanza without an argument results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with missing for");
	strcpy (buf, "wait\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 4);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_respawn (void)
{
	JobConfig*job;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_respawn");

	/* Check that a respawn stanza sets the job's respawn and service */
	TEST_FEATURE ("with no argument");
	strcpy (buf, "respawn\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_TRUE (job->respawn);
		TEST_TRUE (job->service);

		nih_free (job);
	}


	/* Check that a respawn stanza with no arguments can be used multiple
	 * times.
	 */
	TEST_FEATURE ("with multiple no argument stanzas");
	strcpy (buf, "respawn\n");
	strcat (buf, "respawn\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 3);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_TRUE (job->respawn);
		TEST_TRUE (job->service);

		nih_free (job);
	}


	/* Check that a respawn stanza with the limit argument and numeric
	 * rate and timeout results in it being stored in the job.
	 */
	TEST_FEATURE ("with limit and two arguments");
	strcpy (buf, "respawn limit 10 120\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_EQ (job->respawn_limit, 10);
		TEST_EQ (job->respawn_interval, 120);

		nih_free (job);
	}


	/* Check that a respawn stanza with the limit argument can have
	 * the single word unlimited after it.
	 */
	TEST_FEATURE ("with limit and unlimited");
	strcpy (buf, "respawn limit unlimited\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_EQ (job->respawn_limit, 0);
		TEST_EQ (job->respawn_interval, 0);

		nih_free (job);
	}


	/* Check that the most recent of multiple respawn stanzas is used. */
	TEST_FEATURE ("with multiple limit and two argument stanzas");
	strcpy (buf, "respawn limit 5 60\n");
	strcat (buf, "respawn limit 10 120\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 3);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_EQ (job->respawn_limit, 10);
		TEST_EQ (job->respawn_interval, 120);

		nih_free (job);
	}


	/* Check that a respawn stanza with the limit argument but no
	 * interval results in a syntax error.
	 */
	TEST_FEATURE ("with limit and missing second argument");
	strcpy (buf, "respawn limit 10\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 16);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a respawn stanza with the limit argument but no
	 * arguments results in a syntax error.
	 */
	TEST_FEATURE ("with limit and missing arguments");
	strcpy (buf, "respawn limit\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 13);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a respawn limit stanza with a non-integer interval
	 * argument results in a syntax error.
	 */
	TEST_FEATURE ("with limit and non-integer interval argument");
	strcpy (buf, "respawn limit 10 foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_INTERVAL);
	TEST_EQ (pos, 17);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a respawn limit stanza with a non-integer limit
	 * argument results in a syntax error.
	 */
	TEST_FEATURE ("with limit and non-integer limit argument");
	strcpy (buf, "respawn limit foo 120\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_LIMIT);
	TEST_EQ (pos, 14);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a respawn limit stanza with a partially numeric
	 * interval argument results in a syntax error.
	 */
	TEST_FEATURE ("with limit and alphanumeric interval argument");
	strcpy (buf, "respawn limit 10 99foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_INTERVAL);
	TEST_EQ (pos, 17);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a respawn limit stanza with a partially numeric
	 * limit argument results in a syntax error.
	 */
	TEST_FEATURE ("with limit and alphanumeric limit argument");
	strcpy (buf, "respawn limit 99foo 120\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_LIMIT);
	TEST_EQ (pos, 14);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a respawn limit stanza with a negative interval
	 * value results in a syntax error.
	 */
	TEST_FEATURE ("with limit and negative interval argument");
	strcpy (buf, "respawn limit 10 -1\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_INTERVAL);
	TEST_EQ (pos, 17);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a respawn limit stanza with a negative limit
	 * value results in a syntax error.
	 */
	TEST_FEATURE ("with limit and negative interval argument");
	strcpy (buf, "respawn limit -1 120\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_LIMIT);
	TEST_EQ (pos, 14);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a respawn limit stanza with an extra argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with extra argument to limit");
	strcpy (buf, "respawn limit 0 1 foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 18);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a respawn stanza with an unknown second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with unknown argument to limit");
	strcpy (buf, "respawn foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNKNOWN_STANZA);
	TEST_EQ (pos, 8);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_service (void)
{
	JobConfig*job;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_service");

	/* Check that a service stanza without any arguments sets the job's
	 * service flag.
	 */
	TEST_FEATURE ("with no arguments");
	strcpy (buf, "service\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_TRUE (job->service);

		nih_free (job);
	}


	/* Check that multiple service stanzas are permitted. */
	TEST_FEATURE ("with multiple stanzas");
	strcpy (buf, "service\n");
	strcat (buf, "service\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 3);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_TRUE (job->service);

		nih_free (job);
	}


	/* Check that we can specify both of the respawn and service stanzas.
	 */
	TEST_FEATURE ("with respawn followed by service");
	strcpy (buf, "respawn\n");
	strcat (buf, "service\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 3);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_TRUE (job->respawn);
		TEST_TRUE (job->service);

		nih_free (job);
	}


	/* Check that we can specify both of the service and respawn stanzas.
	 */
	TEST_FEATURE ("with service followed by respawn");
	strcpy (buf, "service\n");
	strcat (buf, "respawn\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 3);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_TRUE (job->respawn);
		TEST_TRUE (job->service);

		nih_free (job);
	}


	/* Check that a service stanza with arguments results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with arguments");
	strcpy (buf, "service foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 8);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_instance (void)
{
	JobConfig*job;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_instance");

	/* Check that an instance stanza sets the job's instance flag.
	 */
	TEST_FEATURE ("with no argument");
	strcpy (buf, "instance\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_TRUE (job->instance);

		nih_free (job);
	}


	/* Check that multiple instance stanzas are permitted.
	 */
	TEST_FEATURE ("with multiple stanzas");
	strcpy (buf, "instance\n");
	strcat (buf, "instance\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 3);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_TRUE (job->instance);

		nih_free (job);
	}


	/* Check that any arguments to the instance stanza results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with argument");
	strcpy (buf, "instance foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 9);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_kill (void)
{
	JobConfig*job;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_kill");

	/* Check that a kill stanza with the timeout argument and a numeric
	 * timeout results in it being stored in the job.
	 */
	TEST_FEATURE ("with timeout and single argument");
	strcpy (buf, "kill timeout 10\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_EQ (job->kill_timeout, 10);

		nih_free (job);
	}


	/* Check that the last of multiple kill stanzas is used.
	 */
	TEST_FEATURE ("with multiple timeout and single argument stanzas");
	strcpy (buf, "kill timeout 5\n");
	strcat (buf, "kill timeout 10\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 3);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_EQ (job->kill_timeout, 10);

		nih_free (job);
	}


	/* Check that a kill stanza without an argument results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "kill\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 4);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a kill stanza with an invalid second-level stanza
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with unknown second argument");
	strcpy (buf, "kill foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNKNOWN_STANZA);
	TEST_EQ (pos, 5);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a kill stanza with the timeout argument but no timeout
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with timeout and missing argument");
	strcpy (buf, "kill timeout\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 12);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a kill timeout stanza with a non-integer argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with timeout and non-integer argument");
	strcpy (buf, "kill timeout foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_INTERVAL);
	TEST_EQ (pos, 13);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a kill timeout stanza with a partially numeric argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with timeout and alphanumeric argument");
	strcpy (buf, "kill timeout 99foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_INTERVAL);
	TEST_EQ (pos, 13);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a kill timeout stanza with a negative value results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with timeout and negative argument");
	strcpy (buf, "kill timeout -1\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_INTERVAL);
	TEST_EQ (pos, 13);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a kill stanza with the timeout argument and timeout,
	 * but with an extra argument afterwards results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with timeout and extra argument");
	strcpy (buf, "kill timeout 99 foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 16);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_normal (void)
{
	JobConfig*job;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_normal");

	/* Check that a normal exit stanza with a single argument results in
	 * the exit code given being added to the normalexit array, which
	 * should be allocated.
	 */
	TEST_FEATURE ("with single argument");
	strcpy (buf, "normal exit 99\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_EQ (job->normalexit_len, 1);
		TEST_ALLOC_SIZE (job->normalexit,
				 sizeof (int) * job->normalexit_len);
		TEST_ALLOC_PARENT (job->normalexit, job);

		TEST_EQ (job->normalexit[0], 99);

		nih_free (job);
	}


	/* Check that an argument in a normal exit stanza may be a signal name,
	 * in which case the signal number is shifted left and then added
	 * to the normalexit array.
	 */
	TEST_FEATURE ("with single argument containing signal name");
	strcpy (buf, "normal exit INT\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_EQ (job->normalexit_len, 1);
		TEST_ALLOC_SIZE (job->normalexit,
				 sizeof (int) * job->normalexit_len);
		TEST_ALLOC_PARENT (job->normalexit, job);

		TEST_EQ (job->normalexit[0], SIGINT << 8);

		nih_free (job);
	}


	/* Check that a normal exit stanza with multiple arguments results in
	 * all of the given exit codes being added to the array, which should
	 * have been increased in size.
	 */
	TEST_FEATURE ("with multiple arguments");
	strcpy (buf, "normal exit 99 100 101 SIGTERM\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_EQ (job->normalexit_len, 4);
		TEST_ALLOC_SIZE (job->normalexit,
				 sizeof (int) * job->normalexit_len);
		TEST_ALLOC_PARENT (job->normalexit, job);

		TEST_EQ (job->normalexit[0], 99);
		TEST_EQ (job->normalexit[1], 100);
		TEST_EQ (job->normalexit[2], 101);
		TEST_EQ (job->normalexit[3], SIGTERM << 8);

		nih_free (job);
	}


	/* Check that repeated normal exit stanzas are permitted, each
	 * appending to the array.
	 */
	TEST_FEATURE ("with multiple stanzas");
	strcpy (buf, "normal exit 99\n");
	strcat (buf, "normal exit 100 101\n");
	strcat (buf, "normal exit QUIT\n");
	strcat (buf, "normal exit 900\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 5);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_EQ (job->normalexit_len, 5);
		TEST_ALLOC_SIZE (job->normalexit,
				 sizeof (int) * job->normalexit_len);
		TEST_ALLOC_PARENT (job->normalexit, job);

		TEST_EQ (job->normalexit[0], 99);
		TEST_EQ (job->normalexit[1], 100);
		TEST_EQ (job->normalexit[2], 101);
		TEST_EQ (job->normalexit[3], SIGQUIT << 8);
		TEST_EQ (job->normalexit[4], 900);

		nih_free (job);
	}


	/* Check that a normal exit stanza without an argument results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "normal exit\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 11);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a normal exit stanza with a non-integer argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with non-integer argument");
	strcpy (buf, "normal exit foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_EXIT);
	TEST_EQ (pos, 12);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a normal exit stanza with a partially numeric argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with alphanumeric argument");
	strcpy (buf, "normal exit 99foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_EXIT);
	TEST_EQ (pos, 12);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a normal exit stanza with a negative value results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with negative argument");
	strcpy (buf, "normal exit -1\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_EXIT);
	TEST_EQ (pos, 12);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a normal stanza with something other than "exit"
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with unknown argument");
	strcpy (buf, "normal wibble\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNKNOWN_STANZA);
	TEST_EQ (pos, 7);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a normal stanza without an argument results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with missing exit");
	strcpy (buf, "normal\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 6);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_console (void)
{
	JobConfig*job;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_console");

	/* Check that console logged sets the job's console to
	 * CONSOLE_LOGGED.
	 */
	TEST_FEATURE ("with logged argument");
	strcpy (buf, "console logged\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_EQ (job->console, CONSOLE_LOGGED);

		nih_free (job);
	}


	/* Check that console output sets the job's console to
	 * CONSOLE_OUTPUT.
	 */
	TEST_FEATURE ("with output argument");
	strcpy (buf, "console output\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_EQ (job->console, CONSOLE_OUTPUT);

		nih_free (job);
	}


	/* Check that console owner sets the job's console to
	 * CONSOLE_OWNER.
	 */
	TEST_FEATURE ("with owner argument");
	strcpy (buf, "console owner\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_EQ (job->console, CONSOLE_OWNER);

		nih_free (job);
	}


	/* Check that console none sets the job's console to
	 * CONSOLE_NONE.
	 */
	TEST_FEATURE ("with none argument");
	strcpy (buf, "console none\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_EQ (job->console, CONSOLE_NONE);

		nih_free (job);
	}


	/* Check that the last of multiple console stanzas is used.
	 */
	TEST_FEATURE ("with multiple stanzas");
	strcpy (buf, "console output\n");
	strcat (buf, "console logged\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 3);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_EQ (job->console, CONSOLE_LOGGED);

		nih_free (job);
	}


	/* Check that an unknown argument raises a syntax error.
	 */
	TEST_FEATURE ("with unknown argument");
	strcpy (buf, "console wibble\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNKNOWN_STANZA);
	TEST_EQ (pos, 8);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that additional arguments to the stanza results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with argument");
	strcpy (buf, "console owner foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 14);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_env (void)
{
	JobConfig*job;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_env");

	/* Check that a env stanza with an argument results in it
	 * being stored in the job.
	 */
	TEST_FEATURE ("with single argument");
	strcpy (buf, "env FOO=BAR\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->env, job);
		TEST_ALLOC_SIZE (job->env, sizeof (char *) * 2);
		TEST_EQ_STR (job->env[0], "FOO=BAR");
		TEST_EQ_P (job->env[1], NULL);

		nih_free (job);
	}


	/* Check that repeated env stanzas are appended to those stored in
	 * the job.
	 */
	TEST_FEATURE ("with repeated stanzas");
	strcpy (buf, "env FOO=BAR\n");
	strcat (buf, "env BAZ=QUUX\n");
	strcat (buf, "env FRODO=BILBO\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 4);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->env, job);
		TEST_ALLOC_SIZE (job->env, sizeof (char *) * 4);
		TEST_EQ_STR (job->env[0], "FOO=BAR");
		TEST_EQ_STR (job->env[1], "BAZ=QUUX");
		TEST_EQ_STR (job->env[2], "FRODO=BILBO");
		TEST_EQ_P (job->env[3], NULL);

		nih_free (job);
	}


	/* Check that a env stanza without an argument results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "env\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 3);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a env stanza with an extra second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	strcpy (buf, "env FOO=BAR oops\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 12);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_umask (void)
{
	JobConfig*job;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_umask");

	/* Check that a umask stanza with an octal timeout results
	 * in it being stored in the job.
	 */
	TEST_FEATURE ("with single argument");
	strcpy (buf, "umask 0755\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_EQ (job->umask, 0755);

		nih_free (job);
	}


	/* Check that the last of multiple umask stanzas is used.
	 */
	TEST_FEATURE ("with multiple stanzas");
	strcpy (buf, "umask 0644\n");
	strcat (buf, "umask 0755\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 3);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_EQ (job->umask, 0755);

		nih_free (job);
	}


	/* Check that a umask stanza without an argument results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "umask\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 5);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a umask stanza with a non-octal argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with non-octal argument");
	strcpy (buf, "umask 999\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_UMASK);
	TEST_EQ (pos, 6);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a umask stanza with a non-integer argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with non-integer argument");
	strcpy (buf, "umask foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_UMASK);
	TEST_EQ (pos, 6);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a umask stanza with a partially numeric argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with alphanumeric argument");
	strcpy (buf, "umask 99foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_UMASK);
	TEST_EQ (pos, 6);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a umask stanza with a negative value results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with negative argument");
	strcpy (buf, "umask -1\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_UMASK);
	TEST_EQ (pos, 6);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a umask stanza with a creation mask
	 * but with an extra argument afterwards results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with extra argument");
	strcpy (buf, "umask 0755 foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 11);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_nice (void)
{
	JobConfig*job;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_nice");

	/* Check that a nice stanza with an positive timeout results
	 * in it being stored in the job.
	 */
	TEST_FEATURE ("with positive argument");
	strcpy (buf, "nice 10\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_EQ (job->nice, 10);

		nih_free (job);
	}


	/* Check that a nice stanza with a negative timeout results
	 * in it being stored in the job.
	 */
	TEST_FEATURE ("with positive argument");
	strcpy (buf, "nice -10\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_EQ (job->nice, -10);

		nih_free (job);
	}


	/* Check that the last of multiple nice stanzas is used.
	 */
	TEST_FEATURE ("with multiple stanzas");
	strcpy (buf, "nice -10\n");
	strcat (buf, "nice 10\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 3);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_EQ (job->nice, 10);

		nih_free (job);
	}


	/* Check that a nice stanza without an argument results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "nice\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 4);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a nice stanza with an overly large argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with overly large argument");
	strcpy (buf, "nice 20\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_NICE);
	TEST_EQ (pos, 5);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a nice stanza with an overly small argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with overly small argument");
	strcpy (buf, "nice -21\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_NICE);
	TEST_EQ (pos, 5);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a nice stanza with a non-integer argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with non-integer argument");
	strcpy (buf, "nice foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_NICE);
	TEST_EQ (pos, 5);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a nice stanza with a partially numeric argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with alphanumeric argument");
	strcpy (buf, "nice 12foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_NICE);
	TEST_EQ (pos, 5);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a nice stanza with a priority but with an extra
	 * argument afterwards results in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	strcpy (buf, "nice 10 foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 8);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_limit (void)
{
	JobConfig*job;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_limit");

	/* Check that the limit as stanza sets the RLIMIT_AS resource.
	 */
	TEST_FEATURE ("with as limit");
	strcpy (buf, "limit as 10 20\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->limits[RLIMIT_AS], job);
		TEST_EQ (job->limits[RLIMIT_AS]->rlim_cur, 10);
		TEST_EQ (job->limits[RLIMIT_AS]->rlim_max, 20);

		nih_free (job);
	}


	/* Check that the limit core stanza sets the RLIMIT_CORE resource.
	 */
	TEST_FEATURE ("with core limit");
	strcpy (buf, "limit core 10 20\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->limits[RLIMIT_CORE], job);
		TEST_EQ (job->limits[RLIMIT_CORE]->rlim_cur, 10);
		TEST_EQ (job->limits[RLIMIT_CORE]->rlim_max, 20);

		nih_free (job);
	}


	/* Check that the limit as stanza sets the RLIMIT_CPU resource.
	 */
	TEST_FEATURE ("with cpu limit");
	strcpy (buf, "limit cpu 10 20\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->limits[RLIMIT_CPU], job);
		TEST_EQ (job->limits[RLIMIT_CPU]->rlim_cur, 10);
		TEST_EQ (job->limits[RLIMIT_CPU]->rlim_max, 20);

		nih_free (job);
	}


	/* Check that the limit data stanza sets the RLIMIT_DATA resource.
	 */
	TEST_FEATURE ("with data limit");
	strcpy (buf, "limit data 10 20\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->limits[RLIMIT_DATA], job);
		TEST_EQ (job->limits[RLIMIT_DATA]->rlim_cur, 10);
		TEST_EQ (job->limits[RLIMIT_DATA]->rlim_max, 20);

		nih_free (job);
	}


	/* Check that the limit fsize stanza sets the RLIMIT_FSIZE resource.
	 */
	TEST_FEATURE ("with fsize limit");
	strcpy (buf, "limit fsize 10 20\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->limits[RLIMIT_FSIZE], job);
		TEST_EQ (job->limits[RLIMIT_FSIZE]->rlim_cur, 10);
		TEST_EQ (job->limits[RLIMIT_FSIZE]->rlim_max, 20);

		nih_free (job);
	}


	/* Check that the limit memlock stanza sets the RLIMIT_MEMLOCK
	 * resource.
	 */
	TEST_FEATURE ("with memlock limit");
	strcpy (buf, "limit memlock 10 20\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->limits[RLIMIT_MEMLOCK], job);
		TEST_EQ (job->limits[RLIMIT_MEMLOCK]->rlim_cur, 10);
		TEST_EQ (job->limits[RLIMIT_MEMLOCK]->rlim_max, 20);

		nih_free (job);
	}


	/* Check that the limit msgqueue stanza sets the RLIMIT_MSGQUEUE
	 * resource.
	 */
	TEST_FEATURE ("with msgqueue limit");
	strcpy (buf, "limit msgqueue 10 20\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->limits[RLIMIT_MSGQUEUE], job);
		TEST_EQ (job->limits[RLIMIT_MSGQUEUE]->rlim_cur, 10);
		TEST_EQ (job->limits[RLIMIT_MSGQUEUE]->rlim_max, 20);

		nih_free (job);
	}


	/* Check that the limit nice stanza sets the RLIMIT_NICE resource.
	 */
	TEST_FEATURE ("with nice limit");
	strcpy (buf, "limit nice 10 20\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->limits[RLIMIT_NICE], job);
		TEST_EQ (job->limits[RLIMIT_NICE]->rlim_cur, 10);
		TEST_EQ (job->limits[RLIMIT_NICE]->rlim_max, 20);

		nih_free (job);
	}


	/* Check that the limit nofile stanza sets the RLIMIT_NOFILE
	 * resource.
	 */
	TEST_FEATURE ("with nofile limit");
	strcpy (buf, "limit nofile 10 20\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->limits[RLIMIT_NOFILE], job);
		TEST_EQ (job->limits[RLIMIT_NOFILE]->rlim_cur, 10);
		TEST_EQ (job->limits[RLIMIT_NOFILE]->rlim_max, 20);

		nih_free (job);
	}


	/* Check that the limit nproc stanza sets the RLIMIT_NPROC resource.
	 */
	TEST_FEATURE ("with nproc limit");
	strcpy (buf, "limit nproc 10 20\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->limits[RLIMIT_NPROC], job);
		TEST_EQ (job->limits[RLIMIT_NPROC]->rlim_cur, 10);
		TEST_EQ (job->limits[RLIMIT_NPROC]->rlim_max, 20);

		nih_free (job);
	}


	/* Check that the limit rss stanza sets the RLIMIT_RSS resource.
	 */
	TEST_FEATURE ("with rss limit");
	strcpy (buf, "limit rss 10 20\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->limits[RLIMIT_RSS], job);
		TEST_EQ (job->limits[RLIMIT_RSS]->rlim_cur, 10);
		TEST_EQ (job->limits[RLIMIT_RSS]->rlim_max, 20);

		nih_free (job);
	}


	/* Check that the limit rtprio stanza sets the RLIMIT_RTPRIO resource.
	 */
	TEST_FEATURE ("with rtprio limit");
	strcpy (buf, "limit rtprio 10 20\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->limits[RLIMIT_RTPRIO], job);
		TEST_EQ (job->limits[RLIMIT_RTPRIO]->rlim_cur, 10);
		TEST_EQ (job->limits[RLIMIT_RTPRIO]->rlim_max, 20);

		nih_free (job);
	}


	/* Check that the limit sigpending stanza sets the RLIMIT_SIGPENDING
	 * resource.
	 */
	TEST_FEATURE ("with sigpending limit");
	strcpy (buf, "limit sigpending 10 20\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->limits[RLIMIT_SIGPENDING], job);
		TEST_EQ (job->limits[RLIMIT_SIGPENDING]->rlim_cur, 10);
		TEST_EQ (job->limits[RLIMIT_SIGPENDING]->rlim_max, 20);

		nih_free (job);
	}


	/* Check that the limit stack stanza sets the RLIMIT_STACK resource.
	 */
	TEST_FEATURE ("with stack limit");
	strcpy (buf, "limit stack 10 20\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->limits[RLIMIT_STACK], job);
		TEST_EQ (job->limits[RLIMIT_STACK]->rlim_cur, 10);
		TEST_EQ (job->limits[RLIMIT_STACK]->rlim_max, 20);

		nih_free (job);
	}


	/* Check that multiple limit stanzas are permitted provided they
	 * refer to different resources, all are set.
	 */
	TEST_FEATURE ("with multiple limits");
	strcpy (buf, "limit core 10 20\n");
	strcat (buf, "limit cpu 15 30\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 3);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->limits[RLIMIT_CORE], job);
		TEST_EQ (job->limits[RLIMIT_CORE]->rlim_cur, 10);
		TEST_EQ (job->limits[RLIMIT_CORE]->rlim_max, 20);

		TEST_ALLOC_PARENT (job->limits[RLIMIT_CPU], job);
		TEST_EQ (job->limits[RLIMIT_CPU]->rlim_cur, 15);
		TEST_EQ (job->limits[RLIMIT_CPU]->rlim_max, 30);

		nih_free (job);
	}


	/* Check that the last of multiple stanzas for the same limit is used.
	 */
	TEST_FEATURE ("with multiple of a single limit");
	strcpy (buf, "limit core 5 10\n");
	strcat (buf, "limit core 10 20\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 3);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->limits[RLIMIT_CORE], job);
		TEST_EQ (job->limits[RLIMIT_CORE]->rlim_cur, 10);
		TEST_EQ (job->limits[RLIMIT_CORE]->rlim_max, 20);

		nih_free (job);
	}


	/* Check that the hard resource limit can be set to unlimited with
	 * a special argument of that name
	 */
	TEST_FEATURE ("with unlimited hard limit");
	strcpy (buf, "limit core 10 unlimited\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->limits[RLIMIT_CORE], job);
		TEST_EQ (job->limits[RLIMIT_CORE]->rlim_cur, 10);
		TEST_EQ (job->limits[RLIMIT_CORE]->rlim_max, RLIM_INFINITY);

		nih_free (job);
	}


	/* Check that the soft resource limit can be set to unlimited with
	 * a special argument of that name
	 */
	TEST_FEATURE ("with unlimited soft limit");
	strcpy (buf, "limit core unlimited 20\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->limits[RLIMIT_CORE], job);
		TEST_EQ (job->limits[RLIMIT_CORE]->rlim_cur, RLIM_INFINITY);
		TEST_EQ (job->limits[RLIMIT_CORE]->rlim_max, 20);

		nih_free (job);
	}


	/* Check that a limit stanza with the soft argument but no hard value
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with missing hard limit");
	strcpy (buf, "limit core 10\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 13);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a limit stanza with no soft value results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with missing soft limit");
	strcpy (buf, "limit core\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 10);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a limit stanza with an unknown resource name results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with unknown resource type");
	strcpy (buf, "limit foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNKNOWN_STANZA);
	TEST_EQ (pos, 6);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a limit stanza with no resource name results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with missing resource type");
	strcpy (buf, "limit\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 5);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a limit stanza with a non-integer hard value
	 * argument results in a syntax error.
	 */
	TEST_FEATURE ("with non-integer hard value argument");
	strcpy (buf, "limit core 10 foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_LIMIT);
	TEST_EQ (pos, 14);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a limit stanza with a non-integer soft value
	 * argument results in a syntax error.
	 */
	TEST_FEATURE ("with non-integer soft value argument");
	strcpy (buf, "limit core foo 20\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_LIMIT);
	TEST_EQ (pos, 11);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a limit stanza with a partially numeric hard value
	 * argument results in a syntax error.
	 */
	TEST_FEATURE ("with alphanumeric hard value argument");
	strcpy (buf, "limit core 10 99foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_LIMIT);
	TEST_EQ (pos, 14);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a limit stanza with a partially numeric soft value
	 * argument results in a syntax error.
	 */
	TEST_FEATURE ("with alphanumeric soft value argument");
	strcpy (buf, "limit core 99foo 20\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_LIMIT);
	TEST_EQ (pos, 11);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a limit stanza with an extra argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	strcpy (buf, "limit core 10 20 foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 17);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_chroot (void)
{
	JobConfig*job;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_chroot");

	/* Check that a chroot stanza with an argument results in it
	 * being stored in the job.
	 */
	TEST_FEATURE ("with single argument");
	strcpy (buf, "chroot /chroot/daemon\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->chroot, job);
		TEST_EQ_STR (job->chroot, "/chroot/daemon");

		nih_free (job);
	}


	/* Check that the last of multiple chroot stanzas is used.
	 */
	TEST_FEATURE ("with multiple stanzas");
	strcpy (buf, "chroot /var/daemon\n");
	strcat (buf, "chroot /chroot/daemon\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 3);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->chroot, job);
		TEST_EQ_STR (job->chroot, "/chroot/daemon");

		nih_free (job);
	}


	/* Check that a chroot stanza without an argument results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "chroot\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 6);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a chroot stanza with an extra second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	strcpy (buf, "chroot /chroot/daemon foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 22);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_chdir (void)
{
	JobConfig*job;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_chdir");

	/* Check that a chdir stanza with an argument results in it
	 * being stored in the job.
	 */
	TEST_FEATURE ("with single argument");
	strcpy (buf, "chdir /var/lib/daemon\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 2);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->chdir, job);
		TEST_EQ_STR (job->chdir, "/var/lib/daemon");

		nih_free (job);
	}


	/* Check that the last of multiple chdir stanzas is used.
	 */
	TEST_FEATURE ("with multiple stanzas");
	strcpy (buf, "chdir /var/daemon\n");
	strcat (buf, "chdir /var/lib/daemon\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (pos, strlen (buf));
		TEST_EQ (lineno, 3);

		TEST_ALLOC_SIZE (job, sizeof (JobConfig));

		TEST_ALLOC_PARENT (job->chdir, job);
		TEST_EQ_STR (job->chdir, "/var/lib/daemon");

		nih_free (job);
	}


	/* Check that a chdir stanza without an argument results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "chdir\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 5);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a chdir stanza with an extra second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	strcpy (buf, "chdir /var/lib/daemon foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 22);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

int
main (int   argc,
      char *argv[])
{
	test_parse_job ();

	test_stanza_exec ();
	test_stanza_script ();
	test_stanza_pre_start ();
	test_stanza_post_start ();
	test_stanza_pre_stop ();
	test_stanza_post_stop ();
	test_stanza_start ();
	test_stanza_stop ();
	test_stanza_description ();
	test_stanza_version ();
	test_stanza_author ();
	test_stanza_emits ();
	test_stanza_wait ();
	test_stanza_respawn ();
	test_stanza_service ();
	test_stanza_instance ();
	test_stanza_kill ();
	test_stanza_normal ();
	test_stanza_console ();
	test_stanza_env ();
	test_stanza_umask ();
	test_stanza_nice ();
	test_stanza_limit ();
	test_stanza_chroot ();
	test_stanza_chdir ();

	return 0;
}
