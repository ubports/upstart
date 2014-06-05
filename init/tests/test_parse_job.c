/* upstart
 *
 * test_parse_job.c - test suite for init/parse_job.c
 *
 * Copyright © 2010 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
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

#include <nih/test.h>

#include <signal.h>
#include <string.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/error.h>
#include <nih/logging.h>
#include <nih/error.h>
#include <nih/errors.h>

#include "job_class.h"
#include "conf.h"
#include "parse_job.h"
#include "errors.h"
#include "apparmor.h"

#ifdef ENABLE_CGROUPS

#include "cgroup.h"

#endif /* ENABLE_CGROUPS */

#include "test_util_common.h"


void
test_parse_job (void)
{
	JobClass *job = NULL;
	Process  *process;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("parse_job");
	nih_error_init ();
	job_class_init ();
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));
		TEST_EQ_P (job->start_on, NULL);
		TEST_EQ_P (job->stop_on, NULL);

		process = job->process[PROCESS_MAIN];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
		TEST_EQ (process->script, FALSE);
		TEST_ALLOC_PARENT (process->command, process);
		TEST_EQ_STR (process->command, "/sbin/daemon -d");

		process = job->process[PROCESS_PRE_START];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));
		TEST_EQ_P (job->process[PROCESS_MAIN], NULL);

		nih_free (job);
	}

	TEST_FEATURE ("with non-NULL update parameter (override)");
	{
		JobClass *tmp = NULL;

		strcpy (buf, "start on starting\n");
		strcat (buf, "author \"me\"\n");

		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
				&pos, &lineno);

		TEST_NE_P (job, NULL);
		TEST_EQ_STR (job->author, "me");
		TEST_NE_P (job->start_on, NULL);

		strcat (buf, "author \"you\"\n");
		strcat (buf, "manual\n");
		strcat (buf, "description \"my description\"\n");

		pos = 0;
		lineno = 1;
  		tmp = parse_job (NULL, NULL, job, "test", buf, strlen (buf),
				&pos, &lineno);
		TEST_NE_P (tmp, NULL);

		/* if passed a job, the same object should be returned.
		 */
		TEST_EQ_P (tmp, job);

		TEST_EQ_STR (tmp->author, "you");
		TEST_EQ_P (tmp->start_on, NULL);
		TEST_NE_P (tmp->description, NULL);

		TEST_EQ_STR (tmp->description, "my description");

		nih_free (job);
	}
}

void
test_stanza_exec (void)
{
	JobClass *job;
	Process  *process;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_exec");

	/* Check that an exec stanza sets the process of the job as a single
	 * string.
	 */
	TEST_FEATURE ("with arguments");
	strcpy (buf, "exec /sbin/daemon -d \"foo\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_MAIN];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_MAIN];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_MAIN];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	JobClass *job;
	Process  *process;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_MAIN];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_MAIN];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_MAIN];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 7);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_apparmor (void)
{
	JobClass *job;
	Process  *process;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_apparmor");


	/* Check that an apparmor load stanza sets the process of the
	 * job as a single string.
	 */
	TEST_FEATURE ("with load and profile");
	strcpy (buf, "apparmor load /etc/apparmor.d/usr.sbin.cupsd\n");

	/* TODO: investigate why we can't use TEST_ALLOC_FAIL here.
	 * It fails when nih_sprintf() is used.
	 */

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
			 &pos, &lineno);

	TEST_EQ (pos, strlen (buf));
	TEST_EQ (lineno, 2);

	TEST_ALLOC_SIZE (job, sizeof (JobClass));

	process = job->process[PROCESS_SECURITY];
	TEST_ALLOC_PARENT (process, job->process);
	TEST_ALLOC_SIZE (process, sizeof (Process));
	TEST_EQ (process->script, FALSE);
	TEST_ALLOC_PARENT (process->command, process);
	strcpy (buf, APPARMOR_PARSER);
	strcat (buf, " ");
	strcat (buf, APPARMOR_PARSER_OPTS);
	strcat (buf, " /etc/apparmor.d/usr.sbin.cupsd");
	TEST_EQ_STR (process->command, buf);

	nih_free (job);


	/* Check that the last of multiple apparmor load stanzas is used. */
	TEST_FEATURE ("with multiple load");
	strcpy (buf, "apparmor load /etc/apparmor.d/usr.sbin.rsyslogd\n");
	strcat (buf, "apparmor load /etc/apparmor.d/usr.sbin.cupsd\n");

	/* TODO: investigate why we can't use TEST_ALLOC_FAIL here.
	 * It fails when nih_sprintf() is used.
	 */

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
			 &pos, &lineno);

	TEST_EQ (pos, strlen (buf));
	TEST_EQ (lineno, 3);

	TEST_ALLOC_SIZE (job, sizeof (JobClass));

	process = job->process[PROCESS_SECURITY];
	TEST_ALLOC_PARENT (process, job->process);
	TEST_ALLOC_SIZE (process, sizeof (Process));
	TEST_EQ (process->script, FALSE);
	TEST_ALLOC_PARENT (process->command, process);
	strcpy (buf, APPARMOR_PARSER);
	strcat (buf, " ");
	strcat (buf, APPARMOR_PARSER_OPTS);
	strcat (buf, " /etc/apparmor.d/usr.sbin.cupsd");
	TEST_EQ_STR (process->command, buf);

	nih_free (job);


	/* Check that an apparmor load stanza without any arguments results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with load but no profile");
	strcpy (buf, "apparmor load\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 13);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that an apparmor load stanza with an extra argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with extra argument to load");
	strcpy (buf, "apparmor load /etc/apparmor.d/usr.sbin.cupsd extra\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 45);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that an apparmor stanza with an unknown second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with unknown argument");
	strcpy (buf, "apparmor foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNKNOWN_STANZA);
	TEST_EQ (pos, 9);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that an apparmor stanza with no second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "apparmor\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 8);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that an apparmor switch stanza results in it
	 * being stored in the job.
	 */
	TEST_FEATURE ("with switch and profile");
	strcpy (buf, "apparmor switch /usr/sbin/cupsd\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_ALLOC_PARENT (job->apparmor_switch, job);
		TEST_EQ_STR (job->apparmor_switch, "/usr/sbin/cupsd");

		nih_free (job);
	}


	/* Check that the last of multiple apparmor switch stanzas is used. */
	TEST_FEATURE ("with multiple apparmor switch stanzas");
	strcpy (buf, "apparmor switch /usr/sbin/rsyslogd\n");
	strcat (buf, "apparmor switch /usr/sbin/cupsd\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_ALLOC_PARENT (job->apparmor_switch, job);
		TEST_EQ_STR (job->apparmor_switch, "/usr/sbin/cupsd");

		nih_free (job);
	}


	/* Check that an apparmor switch stanza without a profile results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with switch and no profile");
	strcpy (buf, "apparmor switch\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 15);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that an apparmor switch stanza with an extra second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with extra argument to switch");
	strcpy (buf, "apparmor switch /usr/sbin/cupsd extra\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 32);
	TEST_EQ (lineno, 1);
	nih_free (err);

}

void
test_stanza_pre_start (void)
{
	JobClass *job;
	Process  *process;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_pre_start");

	/* Check that a pre-start exec stanza sets the process of the
	 * job as a single string.
	 */
	TEST_FEATURE ("with exec and command");
	strcpy (buf, "pre-start exec /bin/tool -d \"foo\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_PRE_START];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_PRE_START];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_PRE_START];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_PRE_START];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_PRE_START];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_PRE_START];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	JobClass *job;
	Process  *process;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_post_start");

	/* Check that a post-start exec stanza sets the process of the
	 * job as a single string.
	 */
	TEST_FEATURE ("with exec and command");
	strcpy (buf, "post-start exec /bin/tool -d \"foo\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_POST_START];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_POST_START];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_POST_START];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_POST_START];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_POST_START];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_POST_START];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	JobClass *job;
	Process  *process;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_pre_stop");

	/* Check that a pre-stop exec stanza sets the process of the
	 * job as a single string.
	 */
	TEST_FEATURE ("with exec and command");
	strcpy (buf, "pre-stop exec /bin/tool -d \"foo\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_PRE_STOP];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_PRE_STOP];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_PRE_STOP];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_PRE_STOP];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_PRE_STOP];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_PRE_STOP];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	JobClass *job;
	Process  *process;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_post_stop");

	/* Check that a post-stop exec stanza sets the process of the
	 * job as a single string.
	 */
	TEST_FEATURE ("with exec and command");
	strcpy (buf, "post-stop exec /bin/tool -d \"foo\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_POST_STOP];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_POST_STOP];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_POST_STOP];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_POST_STOP];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_POST_STOP];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		process = job->process[PROCESS_POST_STOP];
		TEST_ALLOC_PARENT (process, job->process);
		TEST_ALLOC_SIZE (process, sizeof (Process));
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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	JobClass     *job;
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_ALLOC_SIZE (job->start_on, sizeof (EventOperator));
		TEST_ALLOC_PARENT (job->start_on, job);

		oper = job->start_on;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wibble");
		TEST_EQ_P (oper->env, NULL);

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_ALLOC_SIZE (job->start_on, sizeof (EventOperator));
		TEST_ALLOC_PARENT (job->start_on, job);

		oper = job->start_on;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wibble");

		TEST_ALLOC_SIZE (oper->env, sizeof (char *) * 4);
		TEST_EQ_STR (oper->env[0], "foo");
		TEST_EQ_STR (oper->env[1], "bar");
		TEST_EQ_STR (oper->env[2], "b?z*");
		TEST_EQ_P (oper->env[3], NULL);

		TEST_EQ_P (oper->node.parent, NULL);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		nih_free (job);
	}


	/* Check that a start on stanza may have an event name followed
	 * by arguments matched by position and then arguments matched by
	 * name.  Various rules of quoting should be allowed for both;
	 * this is all tested elsewhere, but I want to make sure I don't
	 * break something I'm going to document.
	 */
	TEST_FEATURE ("with event name and various arguments");
	strcpy (buf, "start on wibble foo bar KEY=b?z* \"FRODO=foo bar\" "
		"BILBO=\"foo bar\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_ALLOC_SIZE (job->start_on, sizeof (EventOperator));
		TEST_ALLOC_PARENT (job->start_on, job);

		oper = job->start_on;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wibble");

		TEST_ALLOC_SIZE (oper->env, sizeof (char *) * 6);
		TEST_EQ_STR (oper->env[0], "foo");
		TEST_EQ_STR (oper->env[1], "bar");
		TEST_EQ_STR (oper->env[2], "KEY=b?z*");
		TEST_EQ_STR (oper->env[3], "FRODO=foo bar");
		TEST_EQ_STR (oper->env[4], "BILBO=foo bar");
		TEST_EQ_P (oper->env[5], NULL);

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		TEST_EQ_P (oper->env, NULL);

		TEST_EQ_P (oper->node.parent, &job->start_on->node);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->start_on->node.right;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wobble");
		TEST_EQ_P (oper->env, NULL);

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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

		TEST_ALLOC_SIZE (oper->env, sizeof (char *) * 3);
		TEST_EQ_STR (oper->env[0], "foo");
		TEST_EQ_STR (oper->env[1], "bar");
		TEST_EQ_P (oper->env[2], NULL);

		TEST_EQ_P (oper->node.parent, &job->start_on->node);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->start_on->node.right;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wobble");

		TEST_ALLOC_SIZE (oper->env, sizeof (char *) * 3);
		TEST_EQ_STR (oper->env[0], "frodo");
		TEST_EQ_STR (oper->env[1], "bilbo");
		TEST_EQ_P (oper->env[2], NULL);

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		TEST_EQ_P (oper->env, NULL);

		TEST_EQ_P (oper->node.parent, job->start_on->node.left);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->start_on->node.left->right;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wobble");
		TEST_EQ_P (oper->env, NULL);

		TEST_EQ_P (oper->node.parent, job->start_on->node.left);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->start_on->node.right;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wiggle");
		TEST_EQ_P (oper->env, NULL);

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		TEST_EQ_P (oper->env, NULL);

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
		TEST_EQ_P (oper->env, NULL);

		TEST_EQ_P (oper->node.parent, job->start_on->node.right);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->start_on->node.right->right;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wiggle");
		TEST_EQ_P (oper->env, NULL);

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		TEST_EQ_P (oper->env, NULL);

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
		TEST_EQ_P (oper->env, NULL);

		TEST_EQ_P (oper->node.parent, job->start_on->node.right);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->start_on->node.right->right;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wiggle");
		TEST_EQ_P (oper->env, NULL);

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_ALLOC_SIZE (job->start_on, sizeof (EventOperator));
		TEST_ALLOC_PARENT (job->start_on, job);

		oper = job->start_on;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "waggle");
		TEST_EQ_P (oper->env, NULL);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_EXPECTED_OPERATOR);
	TEST_EQ (pos, 22);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that positional arguments to events may not follow
	 * named-based ones, resulting in a syntax error.
	 */
	TEST_FEATURE ("with positional arguments after name-based ones");
	strcpy (buf, "start on wibble foo KEY=bar baz\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_EXPECTED_VARIABLE);
	TEST_EQ (pos, 31);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_manual (void)
{
	char           buf[1024];
	size_t         pos, lineno;
	JobClass      *job;
	NihError      *err;
	EventOperator *oper;

	TEST_FUNCTION ("stanza_manual");

	/* manual only ignores *previously specified* start on
	 * events.
	 */
	TEST_FEATURE ("manual_stanza before start on");
	strcpy (buf, "manual\nstart on wibble\n");

	/* ensure we haven't broken a basic start on event by introducing the
	 * manual stanza into a config
	 */
	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
				 &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (lineno, 3);

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_ALLOC_SIZE (job->start_on, sizeof (EventOperator));
		TEST_ALLOC_PARENT (job->start_on, job);

		oper = job->start_on;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wibble");
		TEST_EQ_P (oper->env, NULL);

		TEST_EQ_P (oper->node.parent, NULL);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		nih_free (job);
	}

	TEST_FEATURE ("manual stanza after start on");
	strcpy (buf, "start on wibble\nmanual\n");

	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
			&pos, &lineno);

	TEST_NE_P (job, NULL);
	TEST_ALLOC_SIZE (job, sizeof (JobClass));
	TEST_EQ_P (job->start_on, NULL);

	nih_free (job);
}

void
test_stanza_stop (void)
{
	JobClass     *job;
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_ALLOC_SIZE (job->stop_on, sizeof (EventOperator));
		TEST_ALLOC_PARENT (job->stop_on, job);

		oper = job->stop_on;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wibble");
		TEST_EQ_P (oper->env, NULL);

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_ALLOC_SIZE (job->stop_on, sizeof (EventOperator));
		TEST_ALLOC_PARENT (job->stop_on, job);

		oper = job->stop_on;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wibble");

		TEST_ALLOC_SIZE (oper->env, sizeof (char *) * 4);
		TEST_EQ_STR (oper->env[0], "foo");
		TEST_EQ_STR (oper->env[1], "bar");
		TEST_EQ_STR (oper->env[2], "b?z*");
		TEST_EQ_P (oper->env[3], NULL);

		TEST_EQ_P (oper->node.parent, NULL);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		nih_free (job);
	}


	/* Check that a stop on stanza may have an event name followed
	 * by arguments matched by position and then arguments matched by
	 * name.  Various rules of quoting should be allowed for both;
	 * this is all tested elsewhere, but I want to make sure I don't
	 * break something I'm going to document.
	 */
	TEST_FEATURE ("with event name and various arguments");
	strcpy (buf, "stop on wibble foo bar KEY=b?z* \"FRODO=foo bar\" "
		"BILBO=\"foo bar\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_ALLOC_SIZE (job->stop_on, sizeof (EventOperator));
		TEST_ALLOC_PARENT (job->stop_on, job);

		oper = job->stop_on;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wibble");

		TEST_ALLOC_SIZE (oper->env, sizeof (char *) * 6);
		TEST_EQ_STR (oper->env[0], "foo");
		TEST_EQ_STR (oper->env[1], "bar");
		TEST_EQ_STR (oper->env[2], "KEY=b?z*");
		TEST_EQ_STR (oper->env[3], "FRODO=foo bar");
		TEST_EQ_STR (oper->env[4], "BILBO=foo bar");
		TEST_EQ_P (oper->env[5], NULL);

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		TEST_EQ_P (oper->env, NULL);

		TEST_EQ_P (oper->node.parent, &job->stop_on->node);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->stop_on->node.right;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wobble");
		TEST_EQ_P (oper->env, NULL);

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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

		TEST_ALLOC_SIZE (oper->env, sizeof (char *) * 3);
		TEST_EQ_STR (oper->env[0], "foo");
		TEST_EQ_STR (oper->env[1], "bar");
		TEST_EQ_P (oper->env[2], NULL);

		TEST_EQ_P (oper->node.parent, &job->stop_on->node);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->stop_on->node.right;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wobble");

		TEST_ALLOC_SIZE (oper->env, sizeof (char *) * 3);
		TEST_EQ_STR (oper->env[0], "frodo");
		TEST_EQ_STR (oper->env[1], "bilbo");
		TEST_EQ_P (oper->env[2], NULL);

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		TEST_EQ_P (oper->env, NULL);

		TEST_EQ_P (oper->node.parent, job->stop_on->node.left);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->stop_on->node.left->right;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wobble");
		TEST_EQ_P (oper->env, NULL);

		TEST_EQ_P (oper->node.parent, job->stop_on->node.left);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->stop_on->node.right;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wiggle");
		TEST_EQ_P (oper->env, NULL);

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		TEST_EQ_P (oper->env, NULL);

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
		TEST_EQ_P (oper->env, NULL);

		TEST_EQ_P (oper->node.parent, job->stop_on->node.right);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->stop_on->node.right->right;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wiggle");
		TEST_EQ_P (oper->env, NULL);

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		TEST_EQ_P (oper->env, NULL);

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
		TEST_EQ_P (oper->env, NULL);

		TEST_EQ_P (oper->node.parent, job->stop_on->node.right);
		TEST_EQ_P (oper->node.left, NULL);
		TEST_EQ_P (oper->node.right, NULL);

		oper = (EventOperator *)job->stop_on->node.right->right;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "wiggle");
		TEST_EQ_P (oper->env, NULL);

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_ALLOC_SIZE (job->stop_on, sizeof (EventOperator));
		TEST_ALLOC_PARENT (job->stop_on, job);

		oper = job->stop_on;
		TEST_EQ (oper->type, EVENT_MATCH);
		TEST_EQ_STR (oper->name, "waggle");
		TEST_EQ_P (oper->env, NULL);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_EXPECTED_OPERATOR);
	TEST_EQ (pos, 21);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that positional arguments to events may not follow
	 * named-based ones, resulting in a syntax error.
	 */
	TEST_FEATURE ("with positional arguments after name-based ones");
	strcpy (buf, "stop on wibble foo KEY=bar baz\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_EXPECTED_VARIABLE);
	TEST_EQ (pos, 30);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_description (void)
{
	JobClass*job;
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	JobClass*job;
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	JobClass*job;
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	JobClass *job;
	NihError  *err;
	size_t     pos, lineno;
	char       buf[1024];

	TEST_FUNCTION ("stanza_emits");

	/* Check that an emits stanza with a single argument results in
	 * the named event being added to the emits list.
	 */
	TEST_FEATURE ("with single argument");
	strcpy (buf, "emits wibble\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_ALLOC_PARENT (job->emits, job);
		TEST_ALLOC_SIZE (job->emits, sizeof (char *) * 2);
		TEST_ALLOC_PARENT (job->emits[0], job->emits);
		TEST_EQ_STR (job->emits[0], "wibble");
		TEST_EQ_P (job->emits[1], NULL);

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_ALLOC_PARENT (job->emits, job);
		TEST_ALLOC_SIZE (job->emits, sizeof (char *) * 4);
		TEST_ALLOC_PARENT (job->emits[0], job->emits);
		TEST_EQ_STR (job->emits[0], "wibble");
		TEST_ALLOC_PARENT (job->emits[1], job->emits);
		TEST_EQ_STR (job->emits[1], "wobble");
		TEST_ALLOC_PARENT (job->emits[2], job->emits);
		TEST_EQ_STR (job->emits[2], "waggle");
		TEST_EQ_P (job->emits[3], NULL);

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_ALLOC_PARENT (job->emits, job);
		TEST_ALLOC_SIZE (job->emits, sizeof (char *) * 5);
		TEST_ALLOC_PARENT (job->emits[0], job->emits);
		TEST_EQ_STR (job->emits[0], "wibble");
		TEST_ALLOC_PARENT (job->emits[1], job->emits);
		TEST_EQ_STR (job->emits[1], "wobble");
		TEST_ALLOC_PARENT (job->emits[2], job->emits);
		TEST_EQ_STR (job->emits[2], "waggle");
		TEST_ALLOC_PARENT (job->emits[3], job->emits);
		TEST_EQ_STR (job->emits[3], "wuggle");
		TEST_EQ_P (job->emits[4], NULL);

		nih_free (job);
	}


	/* Check that an emits stanza without an argument results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "emits\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 5);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_expect (void)
{
	JobClass *job;
	NihError  *err;
	size_t     pos, lineno;
	char       buf[1024];

	TEST_FUNCTION ("stanza_expect");

	/* Check that expect stop sets the job's expect member to
	 * EXPECT_STOP.
	 */
	TEST_FEATURE ("with stop argument");
	strcpy (buf, "expect stop\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_EQ (job->expect, EXPECT_STOP);

		nih_free (job);
	}


	/* Check that expect daemon sets the job's expect member to
	 * EXPECT_DAEMON.
	 */
	TEST_FEATURE ("with daemon argument");
	strcpy (buf, "expect daemon\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_EQ (job->expect, EXPECT_DAEMON);

		nih_free (job);
	}


	/* Check that expect fork sets the job's expect member to
	 * EXPECT_FORK.
	 */
	TEST_FEATURE ("with fork argument");
	strcpy (buf, "expect fork\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_EQ (job->expect, EXPECT_FORK);

		nih_free (job);
	}


	/* Check that expect none sets the job's expect member to
	 * EXPECT_NONE.
	 */
	TEST_FEATURE ("with none argument");
	strcpy (buf, "expect none\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_EQ (job->expect, EXPECT_NONE);

		nih_free (job);
	}


	/* Check that the last of multiple expect stanzas is used.
	 */
	TEST_FEATURE ("with multiple for stanzas");
	strcpy (buf, "expect stop\n");
	strcat (buf, "expect none\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_EQ (job->expect, EXPECT_NONE);

		nih_free (job);
	}


	/* Check that a expect stanza without an argument results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "expect\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 6);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a expect stanza with an unknown argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with unknown argument");
	strcpy (buf, "expect foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNKNOWN_STANZA);
	TEST_EQ (pos, 7);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a expect stanza with an extra argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	strcpy (buf, "expect daemon foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 14);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_respawn (void)
{
	JobClass*job;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_respawn");

	/* Check that a respawn stanza sets the job's respawn flag */
	TEST_FEATURE ("with no argument");
	strcpy (buf, "respawn\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_TRUE (job->respawn);

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_TRUE (job->respawn);

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_INTERVAL);
	TEST_EQ (pos, 17);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a respawn limit stanza with a too-large interval
	 * argument results in a syntax error.
	 */
	TEST_FEATURE ("with limit and too-large interval argument");
	strcpy (buf, "respawn limit 10 10000000000000000000\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_LIMIT);
	TEST_EQ (pos, 14);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a respawn limit stanza with a too-large limit
	 * argument results in a syntax error.
	 */
	TEST_FEATURE ("with limit and too-large limit argument");
	strcpy (buf, "respawn limit 10000000000000000000 120\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNKNOWN_STANZA);
	TEST_EQ (pos, 8);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_task (void)
{
	JobClass*job;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_task");

	/* Check that a task stanza without any arguments sets the job's
	 * task flag.
	 */
	TEST_FEATURE ("with no arguments");
	strcpy (buf, "task\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_TRUE (job->task);

		nih_free (job);
	}


	/* Check that multiple task stanzas are permitted. */
	TEST_FEATURE ("with multiple stanzas");
	strcpy (buf, "task\n");
	strcat (buf, "task\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_TRUE (job->task);

		nih_free (job);
	}


	/* Check that a task stanza with arguments results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with arguments");
	strcpy (buf, "task foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 5);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_instance (void)
{
	JobClass*job;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_instance");

	/* Check that an instance stanza with an argument sets both the
	 * job's instance name.
	 */
	TEST_FEATURE ("with argument");
	strcpy (buf, "instance $FOO\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_ALLOC_PARENT (job->instance, job);
		TEST_EQ_STR (job->instance, "$FOO");

		nih_free (job);
	}


	/* Check that the last of multiple instance stanzas is used.
	 */
	TEST_FEATURE ("with multiple stanzas");
	strcpy (buf, "instance $FOO\n");
	strcpy (buf, "instance $BAR\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_ALLOC_PARENT (job->instance, job);
		TEST_EQ_STR (job->instance, "$BAR");

		nih_free (job);
	}


	/* Check that extra arguments to the instance stanza results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	strcpy (buf, "instance $FOO foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 14);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that no argument to the instance stanza results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "instance\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 8);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_kill (void)
{
	JobClass*job;
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_EQ (job->kill_timeout, 10);

		nih_free (job);
	}


	/* Check that a kill stanza with the signal argument and signal,
	 * sets the right signal on the jobs class.
	 */
	TEST_FEATURE ("with signal and single argument");
	strcpy (buf, "kill signal INT\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_EQ (job->kill_signal, SIGINT);

		nih_free (job);
	}

	/* Check that a kill stanza with the signal argument and numeric signal,
	 * sets the right signal on the jobs class.
	 */
	TEST_FEATURE ("with signal and single numeric argument");
	strcpy (buf, "kill signal 30\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		/* Don't check symbolic here since different
		 * architectures have different mappings.
		 */
		TEST_EQ (job->kill_signal, 30);

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_EQ (job->kill_timeout, 10);

		nih_free (job);
	}


	TEST_FEATURE ("with multiple signal and single argument stanzas");
	strcpy (buf, "kill signal INT\n");
	strcat (buf, "kill signal TERM\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_EQ (job->kill_signal, SIGTERM);

		nih_free (job);
	}


	/* Check that a kill stanza without an argument results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "kill\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 12);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a kill stanza with the timeout argument but no timeout
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with signal and missing argument");
	strcpy (buf, "kill signal\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 11);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a kill timeout stanza with a non-integer argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with timeout and non-integer argument");
	strcpy (buf, "kill timeout foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_INTERVAL);
	TEST_EQ (pos, 13);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a kill timeout stanza with a too-large argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with timeout and too-large argument");
	strcpy (buf, "kill timeout 10000000000000000000\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_INTERVAL);
	TEST_EQ (pos, 13);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a kill signal stanza with an unknown signal argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with signal and unknown signal argument");
	strcpy (buf, "kill signal foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_SIGNAL);
	TEST_EQ (pos, 12);
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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 16);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a kill stanza with the signal argument and signal,
	 * but with an extra argument afterwards results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with signal and extra argument");
	strcpy (buf, "kill signal INT foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 16);
	TEST_EQ (lineno, 1);
	nih_free (err);
}


void
test_stanza_reload (void)
{
	JobClass*job;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_reload");


	/* Check that a reload stanza with the signal argument and signal,
	 * sets the right signal on the jobs class.
	 */
	TEST_FEATURE ("with signal and single argument");
	strcpy (buf, "reload signal USR2\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_EQ (job->reload_signal, SIGUSR2);

		nih_free (job);
	}

	/* Check that a reload stanza with the signal argument and numeric signal,
	 * sets the right signal on the jobs class.
	 */
	TEST_FEATURE ("with signal and single numeric argument");
	strcpy (buf, "reload signal 31\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		/* Don't check symbolic here since different
		 * architectures have different mappings.
		 */
		TEST_EQ (job->reload_signal, 31);

		nih_free (job);
	}

	/* Check that the last of multiple reload stanzas is used.
	 */
	TEST_FEATURE ("with multiple signal and single argument stanzas");
	strcpy (buf, "reload signal USR2\n");
	strcat (buf, "reload signal HUP\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_EQ (job->reload_signal, SIGHUP);

		nih_free (job);
	}


	/* Check that a reload stanza without an argument results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "reload\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 6);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a reload stanza with an invalid second-level stanza
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with unknown second argument");
	strcpy (buf, "reload foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNKNOWN_STANZA);
	TEST_EQ (pos, 7);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a reload stanza with the timeout argument but no timeout
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with signal and missing argument");
	strcpy (buf, "reload signal\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 13);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a reload signal stanza with an unknown signal argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with signal and unknown signal argument");
	strcpy (buf, "reload signal foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_SIGNAL);
	TEST_EQ (pos, 14);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a reload stanza with the signal argument and signal,
	 * but with an extra argument afterwards results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with signal and extra argument");
	strcpy (buf, "reload signal INT foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 18);
	TEST_EQ (lineno, 1);
	nih_free (err);
}


void
test_stanza_normal (void)
{
	JobClass*job;
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	JobClass*job;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_console");

	/* Check that console none sets the job's console to
	 * CONSOLE_NONE.
	 */
	TEST_FEATURE ("with none argument");
	strcpy (buf, "console none\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_EQ (job->console, CONSOLE_NONE);

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_EQ (job->console, CONSOLE_OWNER);

		nih_free (job);
	}


	/* Check that console log sets the job's console to
	 * CONSOLE_LOG.
	 */
	TEST_FEATURE ("with log argument");
	strcpy (buf, "console log\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_EQ (job->console, CONSOLE_LOG);

		nih_free (job);
	}

	/* Check that the last of multiple console stanzas is used.
	 */
	TEST_FEATURE ("with multiple stanzas");
	strcpy (buf, "console output\n");
	strcat (buf, "console owner\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_EQ (job->console, CONSOLE_OWNER);

		nih_free (job);
	}


	/* Check that an unknown argument raises a syntax error.
	 */
	TEST_FEATURE ("with unknown argument");
	strcpy (buf, "console wibble\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 14);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a missing argument raises a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "console\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 7);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_env (void)
{
	JobClass*job;
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 12);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_export (void)
{
	JobClass *job;
	NihError  *err;
	size_t     pos, lineno;
	char       buf[1024];

	TEST_FUNCTION ("stanza_export");

	/* Check that an export stanza with a single argument results in
	 * the argument being added to the export array.
	 */
	TEST_FEATURE ("with single argument");
	strcpy (buf, "export FOO\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_ALLOC_PARENT (job->export, job);
		TEST_ALLOC_SIZE (job->export, sizeof (char *) * 2);
		TEST_ALLOC_PARENT (job->export[0], job->export);
		TEST_EQ_STR (job->export[0], "FOO");
		TEST_EQ_P (job->export[1], NULL);

		nih_free (job);
	}


	/* Check that an export stanza with multiple arguments results in
	 * all of the arguments being added to the export array.
	 */
	TEST_FEATURE ("with multiple arguments");
	strcpy (buf, "export FOO BAR BAZ\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_ALLOC_PARENT (job->export, job);
		TEST_ALLOC_SIZE (job->export, sizeof (char *) * 4);
		TEST_ALLOC_PARENT (job->export[0], job->export);
		TEST_EQ_STR (job->export[0], "FOO");
		TEST_ALLOC_PARENT (job->export[1], job->export);
		TEST_EQ_STR (job->export[1], "BAR");
		TEST_ALLOC_PARENT (job->export[2], job->export);
		TEST_EQ_STR (job->export[2], "BAZ");
		TEST_EQ_P (job->export[3], NULL);

		nih_free (job);
	}


	/* Check that repeated export stanzas are permitted, each appending
	 * to the last.
	 */
	TEST_FEATURE ("with multiple stanzas");
	strcpy (buf, "export FOO\n");
	strcat (buf, "export BAR BAZ\n");
	strcat (buf, "export QUUX\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_ALLOC_PARENT (job->export, job);
		TEST_ALLOC_SIZE (job->export, sizeof (char *) * 5);
		TEST_ALLOC_PARENT (job->export[0], job->export);
		TEST_EQ_STR (job->export[0], "FOO");
		TEST_ALLOC_PARENT (job->export[1], job->export);
		TEST_EQ_STR (job->export[1], "BAR");
		TEST_ALLOC_PARENT (job->export[2], job->export);
		TEST_EQ_STR (job->export[2], "BAZ");
		TEST_ALLOC_PARENT (job->export[3], job->export);
		TEST_EQ_STR (job->export[3], "QUUX");
		TEST_EQ_P (job->export[4], NULL);

		nih_free (job);
	}


	/* Check that an export stanza without an argument results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "export\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 6);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_umask (void)
{
	JobClass*job;
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	JobClass*job;
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_EQ (job->nice, 10);

		nih_free (job);
	}


	/* Check that a nice stanza with a negative timeout results
	 * in it being stored in the job.
	 */
	TEST_FEATURE ("with negative argument");
	strcpy (buf, "nice -10\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 8);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

#define ADJ_TO_SCORE(x) ((x * 1000) / ((x < 0) ? 17 : 15))

void
test_stanza_oom (void)
{
	JobClass*job;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_oom");

	/* Check that an oom stanza with an positive timeout results
	 * in it being stored in the job.
	 */
	TEST_FEATURE ("with positive argument");
	strcpy (buf, "oom 10\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_EQ (job->oom_score_adj, ADJ_TO_SCORE(10));

		nih_free (job);
	}

	TEST_FEATURE ("with positive score argument");
	strcpy (buf, "oom score 100\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_EQ (job->oom_score_adj, 100);

		nih_free (job);
	}

	/* Check that an oom stanza with a negative timeout results
	 * in it being stored in the job.
	 */
	TEST_FEATURE ("with negative argument");
	strcpy (buf, "oom -10\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_EQ (job->oom_score_adj, ADJ_TO_SCORE(-10));

		nih_free (job);
	}

	TEST_FEATURE ("with negative score argument");
	strcpy (buf, "oom score -100\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_EQ (job->oom_score_adj, -100);

		nih_free (job);
	}


	/* Check that an oom stanza may have the special never argument
	 * which stores -17 in the job.
	 */
	TEST_FEATURE ("with never argument");
	strcpy (buf, "oom never\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_EQ (job->oom_score_adj, ADJ_TO_SCORE(-17));

		nih_free (job);
	}


	/* Check that an oom score stanza may have the special never
	 *  argument which stores -1000 in the job.
	 */
	TEST_FEATURE ("with never score argument");
	strcpy (buf, "oom score never\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_EQ (job->oom_score_adj, -1000);

		nih_free (job);
	}


	/* Check that the last of multiple oom stanzas is used.
	 */
	TEST_FEATURE ("with multiple stanzas");
	strcpy (buf, "oom -10\n");
	strcat (buf, "oom 10\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_EQ (job->oom_score_adj, ADJ_TO_SCORE(10));

		nih_free (job);
	}

	TEST_FEATURE ("with multiple score stanzas");
	strcpy (buf, "oom score -500\n");
	strcat (buf, "oom score 500\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_EQ (job->oom_score_adj, 500);

		nih_free (job);
	}

	/* Check that the last of multiple distinct oom stanzas is
	 * used.
	 */
	TEST_FEATURE ("with an oom overriding an oom score stanza");
	strcpy (buf, "oom score -10\n");
	strcat (buf, "oom 10\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_EQ (job->oom_score_adj, ADJ_TO_SCORE(10));

		nih_free (job);
	}

	TEST_FEATURE ("with an oom score overriding an oom stanza");
	strcpy (buf, "oom -10\n");
	strcat (buf, "oom score 10\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_EQ (job->oom_score_adj, 10);

		nih_free (job);
	}


	/* Check that an oom stanza without an argument results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "oom\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 3);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that an oom score stanza without an argument results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with missing score argument");
	strcpy (buf, "oom score\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 9);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that an oom stanza with an overly large argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with overly large argument");
	strcpy (buf, "oom 20\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_OOM);
	TEST_EQ (pos, 4);
	TEST_EQ (lineno, 1);
	nih_free (err);

	TEST_FEATURE ("with overly large score argument");
	strcpy (buf, "oom score 1200\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_OOM);
	TEST_EQ (pos, 10);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that an oom stanza with an overly small argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with overly small argument");
	strcpy (buf, "oom -21\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_OOM);
	TEST_EQ (pos, 4);
	TEST_EQ (lineno, 1);
	nih_free (err);

	TEST_FEATURE ("with overly small score argument");
	strcpy (buf, "oom score -1200\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_OOM);
	TEST_EQ (pos, 10);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that an oom stanza with a non-integer argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with non-integer argument");
	strcpy (buf, "oom foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_OOM);
	TEST_EQ (pos, 4);
	TEST_EQ (lineno, 1);
	nih_free (err);

	TEST_FEATURE ("with non-integer score argument");
	strcpy (buf, "oom score foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_OOM);
	TEST_EQ (pos, 10);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that an oom stanza with a partially numeric argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with alphanumeric argument");
	strcpy (buf, "oom 12foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_OOM);
	TEST_EQ (pos, 4);
	TEST_EQ (lineno, 1);
	nih_free (err);

	TEST_FEATURE ("with alphanumeric score argument");
	strcpy (buf, "oom score 12foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_OOM);
	TEST_EQ (pos, 10);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that an oom stanza with a priority but with an extra
	 * argument afterwards results in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	strcpy (buf, "oom 10 foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 7);
	TEST_EQ (lineno, 1);
	nih_free (err);

	TEST_FEATURE ("with extra score argument");
	strcpy (buf, "oom score 500 foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 14);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_limit (void)
{
	JobClass*job;
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_LIMIT);
	TEST_EQ (pos, 11);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a limit stanza with a too-large hard value
	 * argument results in a syntax error.
	 */
	TEST_FEATURE ("with too-large hard value argument");
	strcpy (buf, "limit core 10 20000000000000000000\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PARSE_ILLEGAL_LIMIT);
	TEST_EQ (pos, 14);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a limit stanza with a too-large soft value
	 * argument results in a syntax error.
	 */
	TEST_FEATURE ("with too-large soft value argument");
	strcpy (buf, "limit core 20000000000000000000 20\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	JobClass*job;
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	JobClass*job;
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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

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
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 22);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_setuid (void)
{
	JobClass*job;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_setuid");

	/* Check that a setuid stanza with an argument results in it
	 * being stored in the job.
	 */
	TEST_FEATURE ("with single argument");
	strcpy (buf, "setuid www-data\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_ALLOC_PARENT (job->setuid, job);
		TEST_EQ_STR (job->setuid, "www-data");

		nih_free (job);
	}


	/* Check that the last of multiple setuid stanzas is used.
	 */
	TEST_FEATURE ("with multiple stanzas");
	strcpy (buf, "setuid www-data\n");
	strcat (buf, "setuid pulse\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_ALLOC_PARENT (job->setuid, job);
		TEST_EQ_STR (job->setuid, "pulse");

		nih_free (job);
	}


	/* Check that a setuid stanza without an argument results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "setuid\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 6);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a setuid stanza with an extra second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	strcpy (buf, "setuid www-data foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 16);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_setgid (void)
{
	JobClass*job;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_setgid");

	/* Check that a setgid stanza with an argument results in it
	 * being stored in the job.
	 */
	TEST_FEATURE ("with single argument");
	strcpy (buf, "setgid kvm\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_ALLOC_PARENT (job->setgid, job);
		TEST_EQ_STR (job->setgid, "kvm");

		nih_free (job);
	}


	/* Check that the last of multiple setgid stanzas is used.
	 */
	TEST_FEATURE ("with multiple stanzas");
	strcpy (buf, "setgid kvm\n");
	strcat (buf, "setgid fuse\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_ALLOC_PARENT (job->setgid, job);
		TEST_EQ_STR (job->setgid, "fuse");

		nih_free (job);
	}


	/* Check that a setgid stanza without an argument results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "setgid\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 6);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a setgid stanza with an extra second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	strcpy (buf, "setgid kvm foo\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 11);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

void
test_stanza_usage (void)
{
	JobClass*job;
	NihError *err;
	size_t    pos, lineno;
	char      buf[1024];

	TEST_FUNCTION ("stanza_usage");

	/* Check that a usage stanza with an argument results in it
	 * being stored in the job.
	 */
	TEST_FEATURE ("with single argument");
	strcpy (buf, "usage \"stanza usage test message\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_ALLOC_PARENT (job->usage, job);
		TEST_EQ_STR (job->usage, "stanza usage test message");

		nih_free (job);
	}


	/* Check that the last of multiple usage stanzas is used.
	 */
	TEST_FEATURE ("with multiple stanzas");
	strcpy (buf, "usage \"stanza usage original\"\n");
	strcat (buf, "usage \"stanza usage test message\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf),
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

		TEST_ALLOC_SIZE (job, sizeof (JobClass));

		TEST_ALLOC_PARENT (job->usage, job);
		TEST_EQ_STR (job->usage, "stanza usage test message");

		nih_free (job);
	}


	/* Check that a usage stanza without an argument results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "usage\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	TEST_EQ (pos, 5);
	TEST_EQ (lineno, 1);
	nih_free (err);


	/* Check that a usage stanza with an extra second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	strcpy (buf, "usage stanza usage test message\n");

	pos = 0;
	lineno = 1;
	job = parse_job (NULL, NULL, NULL, "test", buf, strlen (buf), &pos, &lineno);

	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	TEST_EQ (pos, 13);
	TEST_EQ (lineno, 1);
	nih_free (err);
}

#ifdef ENABLE_CGROUPS

void
test_stanza_cgroup (void)
{
	JobClass       *job;
	NihError       *err;
	CGroup         *cgroup;
	CGroupName     *cgname;
	CGroupSetting  *setting;
	size_t          pos;
	size_t          lineno;
	char            buf[1024];
	int             len;
	size_t          count;

	TEST_FUNCTION ("stanza_cgroup");

	TEST_FEATURE ("no arguments rejected");

	pos = 0;
	lineno = 1;
	len = sprintf (buf, "cgroup\n");

	job = parse_job (NULL, NULL, NULL, "test", buf, len, &pos, &lineno);
	TEST_EQ_P (job, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);

	/* Don't count NL */
	TEST_EQ (pos, (size_t)len-1);

	TEST_EQ (lineno, 1);
	nih_free (err);

	TEST_FEATURE ("single argument (controller) accepted");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		len = sprintf (buf, "cgroup perf_event\n");
		job = parse_job (NULL, NULL, NULL, "test", buf, len, &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_NE_P (job, NULL);

		TEST_LIST_NOT_EMPTY (&job->cgroups);
		count = test_list_count (&job->cgroups);
		TEST_EQ (count, 1);

		cgroup = (CGroup *)test_list_get_index (&job->cgroups, 0);
		TEST_NE_P (cgroup, NULL);
		TEST_ALLOC_PARENT (cgroup, job);
		TEST_ALLOC_SIZE (cgroup, sizeof (CGroup));
		TEST_EQ_STR (cgroup->controller, "perf_event");
		TEST_ALLOC_PARENT (cgroup->controller, cgroup);
		TEST_ALLOC_SIZE (cgroup->controller, 1+strlen ("perf_event"));

		count = test_list_count (&cgroup->names);
		TEST_EQ (count, 1);

		cgname = (CGroupName *)test_list_get_index (&cgroup->names, 0);
		TEST_NE_P (cgname, NULL);

		TEST_ALLOC_PARENT (cgname, cgroup);
		TEST_ALLOC_SIZE (cgname, sizeof (CGroupName));
		TEST_EQ_STR (cgname->name, "$UPSTART_CGROUP");
		TEST_ALLOC_PARENT (cgname->name, cgname);
		TEST_ALLOC_SIZE (cgname->name, 1+strlen ("$UPSTART_CGROUP"));

		TEST_LIST_EMPTY (&cgname->settings);
		TEST_EQ (pos, (size_t)len);
		TEST_EQ (lineno, 2);

		nih_free (job);
	}

	TEST_FEATURE ("2 arguments (controller + unquoted name) accepted");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		len = sprintf (buf, "cgroup perf_event foo\n");

		job = parse_job (NULL, NULL, NULL, "test", buf, len, &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_NE_P (job, NULL);
		TEST_LIST_NOT_EMPTY (&job->cgroups);
		count = test_list_count (&job->cgroups);
		TEST_EQ (count, 1);

		cgroup = (CGroup *)test_list_get_index (&job->cgroups, 0);
		TEST_NE_P (cgroup, NULL);
		TEST_ALLOC_PARENT (cgroup, job);
		TEST_ALLOC_SIZE (cgroup, sizeof (CGroup));
		TEST_EQ_STR (cgroup->controller, "perf_event");
		TEST_ALLOC_PARENT (cgroup->controller, cgroup);
		TEST_ALLOC_SIZE (cgroup->controller, 1+strlen ("perf_event"));

		count = test_list_count (&cgroup->names);
		TEST_EQ (count, 1);

		cgname = (CGroupName *)test_list_get_index (&cgroup->names, 0);
		TEST_NE_P (cgname, NULL);

		TEST_ALLOC_PARENT (cgname, cgroup);
		TEST_ALLOC_SIZE (cgname, sizeof (CGroupName));
		TEST_EQ_STR (cgname->name, "foo");
		TEST_ALLOC_PARENT (cgname->name, cgname);
		TEST_ALLOC_SIZE (cgname->name, 1+strlen ("foo"));

		TEST_LIST_EMPTY (&cgname->settings);
		TEST_EQ (pos, (size_t)len);
		TEST_EQ (lineno, 2);

		nih_free (job);
	}

	TEST_FEATURE ("2 arguments (controller + quoted name) accepted");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		len = sprintf (buf, "cgroup perf_event \"foo bar\"\n");

		job = parse_job (NULL, NULL, NULL, "test", buf, len, &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_NE_P (job, NULL);
		TEST_LIST_NOT_EMPTY (&job->cgroups);
		count = test_list_count (&job->cgroups);
		TEST_EQ (count, 1);

		cgroup = (CGroup *)test_list_get_index (&job->cgroups, 0);
		TEST_NE_P (cgroup, NULL);
		TEST_ALLOC_PARENT (cgroup, job);
		TEST_ALLOC_SIZE (cgroup, sizeof (CGroup));
		TEST_EQ_STR (cgroup->controller, "perf_event");
		TEST_ALLOC_PARENT (cgroup->controller, cgroup);
		TEST_ALLOC_SIZE (cgroup->controller, 1+strlen ("perf_event"));

		count = test_list_count (&cgroup->names);
		TEST_EQ (count, 1);

		cgname = (CGroupName *)test_list_get_index (&cgroup->names, 0);
		TEST_NE_P (cgname, NULL);

		TEST_ALLOC_PARENT (cgname, cgroup);
		TEST_ALLOC_SIZE (cgname, sizeof (CGroupName));
		TEST_EQ_STR (cgname->name, "foo bar");
		TEST_ALLOC_PARENT (cgname->name, cgname);
		TEST_ALLOC_SIZE (cgname->name, 1+strlen ("foo bar"));

		TEST_LIST_EMPTY (&cgname->settings);
		TEST_EQ (pos, (size_t)len);
		TEST_EQ (lineno, 2);

		nih_free (job);
	}

	TEST_FEATURE ("3 arguments (controller, key and value) accepted");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		len = sprintf (buf, "cgroup perf_event key1 value1\n");

		job = parse_job (NULL, NULL, NULL, "test", buf, len, &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_NE_P (job, NULL);
		TEST_LIST_NOT_EMPTY (&job->cgroups);
		count = test_list_count (&job->cgroups);
		TEST_EQ (count, 1);

		cgroup = (CGroup *)test_list_get_index (&job->cgroups, 0);
		TEST_NE_P (cgroup, NULL);
		TEST_ALLOC_PARENT (cgroup, job);
		TEST_ALLOC_SIZE (cgroup, sizeof (CGroup));
		TEST_EQ_STR (cgroup->controller, "perf_event");
		TEST_ALLOC_PARENT (cgroup->controller, cgroup);
		TEST_ALLOC_SIZE (cgroup->controller, 1+strlen ("perf_event"));

		count = test_list_count (&cgroup->names);
		TEST_EQ (count, 1);

		cgname = (CGroupName *)test_list_get_index (&cgroup->names, 0);
		TEST_NE_P (cgname, NULL);

		TEST_ALLOC_PARENT (cgname, cgroup);
		TEST_ALLOC_SIZE (cgname, sizeof (CGroupName));
		TEST_EQ_STR (cgname->name, "$UPSTART_CGROUP");
		TEST_ALLOC_PARENT (cgname->name, cgname);
		TEST_ALLOC_SIZE (cgname->name, 1+strlen ("$UPSTART_CGROUP"));

		TEST_LIST_NOT_EMPTY (&cgname->settings);

		count = test_list_count (&cgname->settings);
		TEST_EQ (count, 1);

		setting = (CGroupSetting *)test_list_get_index (&cgname->settings, 0);
		TEST_NE_P (setting, NULL);

		TEST_ALLOC_PARENT (setting, cgname);
		TEST_ALLOC_SIZE (setting, sizeof (CGroupSetting));

		TEST_EQ_STR (setting->key, "key1");
		TEST_ALLOC_PARENT (setting->key, setting);
		TEST_ALLOC_SIZE (setting->key, 1+strlen ("key1"));

		TEST_EQ_STR (setting->value, "value1");
		TEST_ALLOC_PARENT (setting->value, setting);
		TEST_ALLOC_SIZE (setting->value, 1+strlen ("value1"));

		TEST_EQ (pos, (size_t)len);
		TEST_EQ (lineno, 2);

		nih_free (job);
	}

	TEST_FEATURE ("4 arguments (controller + unquoted name, key and value) accepted");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		len = sprintf (buf, "cgroup perf_event foo key1 value1\n");

		job = parse_job (NULL, NULL, NULL, "test", buf, len, &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_NE_P (job, NULL);
		TEST_LIST_NOT_EMPTY (&job->cgroups);
		count = test_list_count (&job->cgroups);
		TEST_EQ (count, 1);

		cgroup = (CGroup *)test_list_get_index (&job->cgroups, 0);
		TEST_NE_P (cgroup, NULL);
		TEST_ALLOC_PARENT (cgroup, job);
		TEST_ALLOC_SIZE (cgroup, sizeof (CGroup));
		TEST_EQ_STR (cgroup->controller, "perf_event");
		TEST_ALLOC_PARENT (cgroup->controller, cgroup);
		TEST_ALLOC_SIZE (cgroup->controller, 1+strlen ("perf_event"));

		count = test_list_count (&cgroup->names);
		TEST_EQ (count, 1);

		cgname = (CGroupName *)test_list_get_index (&cgroup->names, 0);
		TEST_NE_P (cgname, NULL);

		TEST_ALLOC_PARENT (cgname, cgroup);
		TEST_ALLOC_SIZE (cgname, sizeof (CGroupName));
		TEST_EQ_STR (cgname->name, "foo");
		TEST_ALLOC_PARENT (cgname->name, cgname);
		TEST_ALLOC_SIZE (cgname->name, 1+strlen ("foo"));

		TEST_LIST_NOT_EMPTY (&cgname->settings);

		count = test_list_count (&cgname->settings);
		TEST_EQ (count, 1);

		setting = (CGroupSetting *)test_list_get_index (&cgname->settings, 0);
		TEST_NE_P (setting, NULL);

		TEST_ALLOC_PARENT (setting, cgname);
		TEST_ALLOC_SIZE (setting, sizeof (CGroupSetting));

		TEST_EQ_STR (setting->key, "key1");
		TEST_ALLOC_PARENT (setting->key, setting);
		TEST_ALLOC_SIZE (setting->key, 1+strlen ("key1"));

		TEST_EQ_STR (setting->value, "value1");
		TEST_ALLOC_PARENT (setting->value, setting);
		TEST_ALLOC_SIZE (setting->value, 1+strlen ("value1"));

		TEST_EQ (pos, (size_t)len);
		TEST_EQ (lineno, 2);

		nih_free (job);
	}

	TEST_FEATURE ("4 arguments (controller + quoted name, unquoted key and quoted value) accepted");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		len = sprintf (buf, "cgroup perf_event \"a silly name\" key1 \"hello world\"\n");

		job = parse_job (NULL, NULL, NULL, "test", buf, len, &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_NE_P (job, NULL);
		TEST_LIST_NOT_EMPTY (&job->cgroups);
		count = test_list_count (&job->cgroups);
		TEST_EQ (count, 1);

		cgroup = (CGroup *)test_list_get_index (&job->cgroups, 0);
		TEST_NE_P (cgroup, NULL);
		TEST_ALLOC_PARENT (cgroup, job);
		TEST_ALLOC_SIZE (cgroup, sizeof (CGroup));
		TEST_EQ_STR (cgroup->controller, "perf_event");
		TEST_ALLOC_PARENT (cgroup->controller, cgroup);
		TEST_ALLOC_SIZE (cgroup->controller, 1+strlen ("perf_event"));

		count = test_list_count (&cgroup->names);
		TEST_EQ (count, 1);

		cgname = (CGroupName *)test_list_get_index (&cgroup->names, 0);
		TEST_NE_P (cgname, NULL);

		TEST_ALLOC_PARENT (cgname, cgroup);
		TEST_ALLOC_SIZE (cgname, sizeof (CGroupName));
		TEST_EQ_STR (cgname->name, "a silly name");
		TEST_ALLOC_PARENT (cgname->name, cgname);
		TEST_ALLOC_SIZE (cgname->name, 1+strlen ("foo"));

		TEST_LIST_NOT_EMPTY (&cgname->settings);

		count = test_list_count (&cgname->settings);
		TEST_EQ (count, 1);

		setting = (CGroupSetting *)test_list_get_index (&cgname->settings, 0);
		TEST_NE_P (setting, NULL);

		TEST_ALLOC_PARENT (setting, cgname);
		TEST_ALLOC_SIZE (setting, sizeof (CGroupSetting));

		TEST_EQ_STR (setting->key, "key1");
		TEST_ALLOC_PARENT (setting->key, setting);
		TEST_ALLOC_SIZE (setting->key, 1+strlen ("key1"));

		TEST_EQ_STR (setting->value, "hello world");
		TEST_ALLOC_PARENT (setting->value, setting);
		TEST_ALLOC_SIZE (setting->value, 1+strlen ("hello world"));

		TEST_EQ (pos, (size_t)len);
		TEST_EQ (lineno, 2);

		nih_free (job);
	}

	/* FIXME: variables are only expanded on job start so cannot
	 * validate.
	 */
	TEST_FEATURE ("name with embedded variable is accepted");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		len = sprintf (buf, "cgroup perf_event \"$VARIABLE\" key value\n");

		job = parse_job (NULL, NULL, NULL, "test", buf, len, &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_NE_P (job, NULL);
		TEST_LIST_NOT_EMPTY (&job->cgroups);
		count = test_list_count (&job->cgroups);
		TEST_EQ (count, 1);

		cgroup = (CGroup *)test_list_get_index (&job->cgroups, 0);
		TEST_NE_P (cgroup, NULL);
		TEST_ALLOC_PARENT (cgroup, job);
		TEST_ALLOC_SIZE (cgroup, sizeof (CGroup));
		TEST_EQ_STR (cgroup->controller, "perf_event");
		TEST_ALLOC_PARENT (cgroup->controller, cgroup);
		TEST_ALLOC_SIZE (cgroup->controller, 1+strlen ("perf_event"));

		count = test_list_count (&cgroup->names);
		TEST_EQ (count, 1);

		cgname = (CGroupName *)test_list_get_index (&cgroup->names, 0);
		TEST_NE_P (cgname, NULL);

		TEST_ALLOC_PARENT (cgname, cgroup);
		TEST_ALLOC_SIZE (cgname, sizeof (CGroupName));
		TEST_EQ_STR (cgname->name, "$VARIABLE");
		TEST_ALLOC_PARENT (cgname->name, cgname);
		TEST_ALLOC_SIZE (cgname->name, 1+strlen ("$VARIABLE"));

		TEST_LIST_NOT_EMPTY (&cgname->settings);

		count = test_list_count (&cgname->settings);
		TEST_EQ (count, 1);

		setting = (CGroupSetting *)test_list_get_index (&cgname->settings, 0);
		TEST_NE_P (setting, NULL);

		TEST_ALLOC_PARENT (setting, cgname);
		TEST_ALLOC_SIZE (setting, sizeof (CGroupSetting));

		TEST_EQ_STR (setting->key, "key");
		TEST_ALLOC_PARENT (setting->key, setting);
		TEST_ALLOC_SIZE (setting->key, 1+strlen ("key"));

		TEST_EQ_STR (setting->value, "value");
		TEST_ALLOC_PARENT (setting->value, setting);
		TEST_ALLOC_SIZE (setting->value, 1+strlen ("value"));

		TEST_EQ (pos, (size_t)len);
		TEST_EQ (lineno, 2);

		nih_free (job);
	}

	TEST_FEATURE ("duplicate stanza is ignored");

	len = sprintf (buf,
			"cgroup perf_event foo key1 value1\n"
			"cgroup perf_event foo key1 value1\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;

		job = parse_job (NULL, NULL, NULL, "test", buf, len, &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_NE_P (job, NULL);
		TEST_LIST_NOT_EMPTY (&job->cgroups);
		count = test_list_count (&job->cgroups);
		TEST_EQ (count, 1);

		cgroup = (CGroup *)test_list_get_index (&job->cgroups, 0);
		TEST_NE_P (cgroup, NULL);
		TEST_ALLOC_PARENT (cgroup, job);
		TEST_ALLOC_SIZE (cgroup, sizeof (CGroup));
		TEST_EQ_STR (cgroup->controller, "perf_event");
		TEST_ALLOC_PARENT (cgroup->controller, cgroup);
		TEST_ALLOC_SIZE (cgroup->controller, 1+strlen ("perf_event"));

		count = test_list_count (&cgroup->names);
		TEST_EQ (count, 1);

		cgname = (CGroupName *)test_list_get_index (&cgroup->names, 0);
		TEST_NE_P (cgname, NULL);

		TEST_ALLOC_PARENT (cgname, cgroup);
		TEST_ALLOC_SIZE (cgname, sizeof (CGroupName));
		TEST_EQ_STR (cgname->name, "foo");
		TEST_ALLOC_PARENT (cgname->name, cgname);
		TEST_ALLOC_SIZE (cgname->name, 1+strlen ("foo"));

		TEST_LIST_NOT_EMPTY (&cgname->settings);

		count = test_list_count (&cgname->settings);
		TEST_EQ (count, 1);

		setting = (CGroupSetting *)test_list_get_index (&cgname->settings, 0);
		TEST_NE_P (setting, NULL);

		TEST_ALLOC_PARENT (setting, cgname);
		TEST_ALLOC_SIZE (setting, sizeof (CGroupSetting));

		TEST_EQ_STR (setting->key, "key1");
		TEST_ALLOC_PARENT (setting->key, setting);
		TEST_ALLOC_SIZE (setting->key, 1+strlen ("key1"));

		TEST_EQ_STR (setting->value, "value1");
		TEST_ALLOC_PARENT (setting->value, setting);
		TEST_ALLOC_SIZE (setting->value, 1+strlen ("value1"));

		TEST_EQ (pos, (size_t)len);
		TEST_EQ (lineno, 3);

		nih_free (job);
	}

	TEST_FEATURE ("duplicate equivalent stanza is ignored");

	len = sprintf (buf,
			"cgroup perf_event foo \"key1\" value1\n"
			"cgroup perf_event foo key1 \"value1\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;

		job = parse_job (NULL, NULL, NULL, "test", buf, len, &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_NE_P (job, NULL);
		TEST_LIST_NOT_EMPTY (&job->cgroups);
		count = test_list_count (&job->cgroups);
		TEST_EQ (count, 1);

		cgroup = (CGroup *)test_list_get_index (&job->cgroups, 0);
		TEST_NE_P (cgroup, NULL);
		TEST_ALLOC_PARENT (cgroup, job);
		TEST_ALLOC_SIZE (cgroup, sizeof (CGroup));
		TEST_EQ_STR (cgroup->controller, "perf_event");
		TEST_ALLOC_PARENT (cgroup->controller, cgroup);
		TEST_ALLOC_SIZE (cgroup->controller, 1+strlen ("perf_event"));

		count = test_list_count (&cgroup->names);
		TEST_EQ (count, 1);

		cgname = (CGroupName *)test_list_get_index (&cgroup->names, 0);
		TEST_NE_P (cgname, NULL);

		TEST_ALLOC_PARENT (cgname, cgroup);
		TEST_ALLOC_SIZE (cgname, sizeof (CGroupName));
		TEST_EQ_STR (cgname->name, "foo");
		TEST_ALLOC_PARENT (cgname->name, cgname);
		TEST_ALLOC_SIZE (cgname->name, 1+strlen ("foo"));

		TEST_LIST_NOT_EMPTY (&cgname->settings);

		count = test_list_count (&cgname->settings);
		TEST_EQ (count, 1);

		setting = (CGroupSetting *)test_list_get_index (&cgname->settings, 0);
		TEST_NE_P (setting, NULL);

		TEST_ALLOC_PARENT (setting, cgname);
		TEST_ALLOC_SIZE (setting, sizeof (CGroupSetting));

		TEST_EQ_STR (setting->key, "key1");
		TEST_ALLOC_PARENT (setting->key, setting);
		TEST_ALLOC_SIZE (setting->key, 1+strlen ("key1"));

		TEST_EQ_STR (setting->value, "value1");
		TEST_ALLOC_PARENT (setting->value, setting);
		TEST_ALLOC_SIZE (setting->value, 1+strlen ("value1"));

		TEST_EQ (pos, (size_t)len);
		TEST_EQ (lineno, 3);

		nih_free (job);
	}

	TEST_FEATURE ("multiple names per controller are accepted");

	len = sprintf (buf,
			"cgroup perf_event foo key1 value1\n"
			"cgroup perf_event bar key2 value2\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;

		job = parse_job (NULL, NULL, NULL, "test", buf, len, &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_NE_P (job, NULL);
		TEST_LIST_NOT_EMPTY (&job->cgroups);
		count = test_list_count (&job->cgroups);
		TEST_EQ (count, 1);

		cgroup = (CGroup *)test_list_get_index (&job->cgroups, 0);
		TEST_NE_P (cgroup, NULL);
		TEST_ALLOC_PARENT (cgroup, job);
		TEST_ALLOC_SIZE (cgroup, sizeof (CGroup));
		TEST_EQ_STR (cgroup->controller, "perf_event");
		TEST_ALLOC_PARENT (cgroup->controller, cgroup);
		TEST_ALLOC_SIZE (cgroup->controller, 1+strlen ("perf_event"));

		count = test_list_count (&cgroup->names);
		TEST_EQ (count, 2);

		/* first */
		cgname = (CGroupName *)test_list_get_index (&cgroup->names, 0);
		TEST_NE_P (cgname, NULL);

		TEST_ALLOC_PARENT (cgname, cgroup);
		TEST_ALLOC_SIZE (cgname, sizeof (CGroupName));
		TEST_EQ_STR (cgname->name, "foo");
		TEST_ALLOC_PARENT (cgname->name, cgname);
		TEST_ALLOC_SIZE (cgname->name, 1+strlen ("foo"));

		TEST_LIST_NOT_EMPTY (&cgname->settings);

		count = test_list_count (&cgname->settings);
		TEST_EQ (count, 1);

		setting = (CGroupSetting *)test_list_get_index (&cgname->settings, 0);
		TEST_NE_P (setting, NULL);

		TEST_ALLOC_PARENT (setting, cgname);
		TEST_ALLOC_SIZE (setting, sizeof (CGroupSetting));

		TEST_EQ_STR (setting->key, "key1");
		TEST_ALLOC_PARENT (setting->key, setting);
		TEST_ALLOC_SIZE (setting->key, 1+strlen ("key1"));

		TEST_EQ_STR (setting->value, "value1");
		TEST_ALLOC_PARENT (setting->value, setting);
		TEST_ALLOC_SIZE (setting->value, 1+strlen ("value1"));

		/* second */
		cgname = (CGroupName *)test_list_get_index (&cgroup->names, 1);
		TEST_NE_P (cgname, NULL);

		TEST_ALLOC_PARENT (cgname, cgroup);
		TEST_ALLOC_SIZE (cgname, sizeof (CGroupName));
		TEST_EQ_STR (cgname->name, "bar");
		TEST_ALLOC_PARENT (cgname->name, cgname);
		TEST_ALLOC_SIZE (cgname->name, 1+strlen ("bar"));

		TEST_LIST_NOT_EMPTY (&cgname->settings);

		count = test_list_count (&cgname->settings);
		TEST_EQ (count, 1);

		setting = (CGroupSetting *)test_list_get_index (&cgname->settings, 0);
		TEST_NE_P (setting, NULL);

		TEST_ALLOC_PARENT (setting, cgname);
		TEST_ALLOC_SIZE (setting, sizeof (CGroupSetting));

		TEST_EQ_STR (setting->key, "key2");
		TEST_ALLOC_PARENT (setting->key, setting);
		TEST_ALLOC_SIZE (setting->key, 1+strlen ("key2"));

		TEST_EQ_STR (setting->value, "value2");
		TEST_ALLOC_PARENT (setting->value, setting);
		TEST_ALLOC_SIZE (setting->value, 1+strlen ("value2"));

		TEST_EQ (pos, (size_t)len);
		TEST_EQ (lineno, 3);

		nih_free (job);
	}

	TEST_FEATURE ("multiple keys per controller name are accepted");

	len = sprintf (buf,
			"cgroup perf_event foo key1 value1\n"
			"cgroup perf_event foo key2 \"value2\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;

		job = parse_job (NULL, NULL, NULL, "test", buf, len, &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_NE_P (job, NULL);
		TEST_LIST_NOT_EMPTY (&job->cgroups);
		count = test_list_count (&job->cgroups);
		TEST_EQ (count, 1);

		cgroup = (CGroup *)test_list_get_index (&job->cgroups, 0);
		TEST_NE_P (cgroup, NULL);
		TEST_ALLOC_PARENT (cgroup, job);
		TEST_ALLOC_SIZE (cgroup, sizeof (CGroup));
		TEST_EQ_STR (cgroup->controller, "perf_event");
		TEST_ALLOC_PARENT (cgroup->controller, cgroup);
		TEST_ALLOC_SIZE (cgroup->controller, 1+strlen ("perf_event"));

		count = test_list_count (&cgroup->names);
		TEST_EQ (count, 1);

		cgname = (CGroupName *)test_list_get_index (&cgroup->names, 0);
		TEST_NE_P (cgname, NULL);

		TEST_ALLOC_PARENT (cgname, cgroup);
		TEST_ALLOC_SIZE (cgname, sizeof (CGroupName));
		TEST_EQ_STR (cgname->name, "foo");
		TEST_ALLOC_PARENT (cgname->name, cgname);
		TEST_ALLOC_SIZE (cgname->name, 1+strlen ("foo"));

		TEST_LIST_NOT_EMPTY (&cgname->settings);

		count = test_list_count (&cgname->settings);
		TEST_EQ (count, 2);

		setting = (CGroupSetting *)test_list_get_index (&cgname->settings, 0);
		TEST_NE_P (setting, NULL);

		TEST_ALLOC_PARENT (setting, cgname);
		TEST_ALLOC_SIZE (setting, sizeof (CGroupSetting));

		TEST_EQ_STR (setting->key, "key1");
		TEST_ALLOC_PARENT (setting->key, setting);
		TEST_ALLOC_SIZE (setting->key, 1+strlen ("key1"));

		TEST_EQ_STR (setting->value, "value1");
		TEST_ALLOC_PARENT (setting->value, setting);
		TEST_ALLOC_SIZE (setting->value, 1+strlen ("value1"));

		setting = (CGroupSetting *)test_list_get_index (&cgname->settings, 1);
		TEST_NE_P (setting, NULL);

		TEST_ALLOC_PARENT (setting, cgname);
		TEST_ALLOC_SIZE (setting, sizeof (CGroupSetting));

		TEST_EQ_STR (setting->key, "key2");
		TEST_ALLOC_PARENT (setting->key, setting);
		TEST_ALLOC_SIZE (setting->key, 1+strlen ("key2"));

		TEST_EQ_STR (setting->value, "value2");
		TEST_ALLOC_PARENT (setting->value, setting);
		TEST_ALLOC_SIZE (setting->value, 1+strlen ("value2"));

		TEST_EQ (pos, (size_t)len);
		TEST_EQ (lineno, 3);

		nih_free (job);
	}

	TEST_FEATURE ("new stanza overrides old arguments");

	/* value saved for key1 should be "hello world", not "bar" */
	len = sprintf (buf,
			"cgroup perf_event foo key1 bar\n"
			"cgroup perf_event foo key1 \"hello world\"\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;

		job = parse_job (NULL, NULL, NULL, "test", buf, len, &pos, &lineno);

		if (test_alloc_failed) {
			TEST_EQ_P (job, NULL);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_NE_P (job, NULL);
		TEST_LIST_NOT_EMPTY (&job->cgroups);
		count = test_list_count (&job->cgroups);
		TEST_EQ (count, 1);

		cgroup = (CGroup *)test_list_get_index (&job->cgroups, 0);
		TEST_NE_P (cgroup, NULL);
		TEST_ALLOC_PARENT (cgroup, job);
		TEST_ALLOC_SIZE (cgroup, sizeof (CGroup));
		TEST_EQ_STR (cgroup->controller, "perf_event");
		TEST_ALLOC_PARENT (cgroup->controller, cgroup);
		TEST_ALLOC_SIZE (cgroup->controller, 1+strlen ("perf_event"));

		count = test_list_count (&cgroup->names);
		TEST_EQ (count, 1);

		cgname = (CGroupName *)test_list_get_index (&cgroup->names, 0);
		TEST_NE_P (cgname, NULL);

		TEST_ALLOC_PARENT (cgname, cgroup);
		TEST_ALLOC_SIZE (cgname, sizeof (CGroupName));
		TEST_EQ_STR (cgname->name, "foo");
		TEST_ALLOC_PARENT (cgname->name, cgname);
		TEST_ALLOC_SIZE (cgname->name, 1+strlen ("foo"));

		TEST_LIST_NOT_EMPTY (&cgname->settings);

		count = test_list_count (&cgname->settings);
		TEST_EQ (count, 1);

		setting = (CGroupSetting *)test_list_get_index (&cgname->settings, 0);
		TEST_NE_P (setting, NULL);

		TEST_ALLOC_PARENT (setting, cgname);
		TEST_ALLOC_SIZE (setting, sizeof (CGroupSetting));

		TEST_EQ_STR (setting->key, "key1");
		TEST_ALLOC_PARENT (setting->key, setting);
		TEST_ALLOC_SIZE (setting->key, 1+strlen ("key1"));

		TEST_EQ_STR (setting->value, "hello world");
		TEST_ALLOC_PARENT (setting->value, setting);
		TEST_ALLOC_SIZE (setting->value, 1+strlen ("hello world"));

		TEST_EQ (pos, (size_t)len);
		TEST_EQ (lineno, 3);

		nih_free (job);
	}
}

#endif /* ENABLE_CGROUPS */

int
main (int   argc,
      char *argv[])
{
	/* run tests in legacy (pre-session support) mode */
	setenv ("UPSTART_NO_SESSIONS", "1", 1);

	test_parse_job ();

	test_stanza_instance ();

	test_stanza_description ();
	test_stanza_version ();
	test_stanza_author ();

	test_stanza_env ();
	test_stanza_export ();

	test_stanza_start ();
	test_stanza_stop ();
	test_stanza_emits ();
	test_stanza_manual ();

	test_stanza_exec ();
	test_stanza_script ();
	test_stanza_apparmor ();
	test_stanza_pre_start ();
	test_stanza_post_start ();
	test_stanza_pre_stop ();
	test_stanza_post_stop ();

	test_stanza_expect ();
	test_stanza_task ();

	test_stanza_kill ();

	test_stanza_reload ();

	test_stanza_respawn ();
	test_stanza_normal ();

	test_stanza_console ();

	test_stanza_umask ();
	test_stanza_nice ();
	test_stanza_oom ();
	test_stanza_limit ();
	test_stanza_chroot ();
	test_stanza_chdir ();
	test_stanza_setuid ();
	test_stanza_setgid ();
	test_stanza_usage ();

#ifdef ENABLE_CGROUPS
	test_stanza_cgroup ();
#endif /* ENABLE_CGROUPS */

	return 0;
}
