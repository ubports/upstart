/* upstart
 *
 * test_parse_conf.c - test suite for init/parse_conf.c
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

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/error.h>
#include <nih/errors.h>

#include "job.h"
#include "parse_conf.h"
#include "conf.h"
#include "errors.h"


void
test_parse_conf (void)
{
	ConfSource *source;
	ConfFile   *file;
	ConfItem   *item;
	NihError   *err;
	size_t      pos, lineno;
	char        buf[1024];
	int         ret;

	TEST_FUNCTION ("parse_conf");
	job_init ();

	source = conf_source_new (NULL, "/path", CONF_DIR);
	file = conf_file_get (source, "/path/file");

	/* Check that a simple configuration of two jobs can be parsed,
	 * with all of the information given filled into the structures.
	 */
	TEST_FEATURE ("with simple file");
	strcpy (buf, "job foo\n");
	strcat (buf, "  respawn\n");
	strcat (buf, "  exec /sbin/daemon -d\n");
	strcat (buf, "end job\n");
	strcat (buf, "job bar\n");
	strcat (buf, "  script\n");
	strcat (buf, "    echo\n");
	strcat (buf, "  end script\n");
	strcat (buf, "end job\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		ret = parse_conf (file, buf, strlen (buf), &pos, &lineno);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (ret, 0);
		TEST_LIST_NOT_EMPTY (&file->items);

		item = (ConfItem *)file->items.next;

		TEST_ALLOC_SIZE (item, sizeof (ConfItem));
		TEST_ALLOC_PARENT (item, file);
		TEST_EQ (item->type, CONF_JOB);

		TEST_ALLOC_SIZE (item->job, sizeof (Job));
		TEST_ALLOC_PARENT (item->job, NULL);
		TEST_EQ_STR (item->job->name, "foo");
		TEST_TRUE (item->job->respawn);
		TEST_NE_P (item->job->process[PROCESS_MAIN], NULL);
		TEST_EQ (item->job->process[PROCESS_MAIN]->script, FALSE);
		TEST_EQ_STR (item->job->process[PROCESS_MAIN]->command,
			     "/sbin/daemon -d");

		nih_free (item);

		item = (ConfItem *)file->items.next;

		TEST_ALLOC_SIZE (item, sizeof (ConfItem));
		TEST_ALLOC_PARENT (item, file);
		TEST_EQ (item->type, CONF_JOB);

		TEST_ALLOC_SIZE (item->job, sizeof (Job));
		TEST_ALLOC_PARENT (item->job, NULL);
		TEST_EQ_STR (item->job->name, "bar");
		TEST_FALSE (item->job->respawn);
		TEST_NE_P (item->job->process[PROCESS_MAIN], NULL);
		TEST_EQ (item->job->process[PROCESS_MAIN]->script, TRUE);
		TEST_EQ_STR (item->job->process[PROCESS_MAIN]->command,
			     "echo\n");

		nih_free (item);

		TEST_LIST_EMPTY (&file->items);
	}

	nih_free (source);
}


void
test_stanza_job (void)
{
	ConfSource *source;
	ConfFile   *file;
	ConfItem   *item;
	NihError   *err;
	size_t      pos, lineno;
	char        buf[1024];
	int         ret;

	TEST_FUNCTION ("parse_job");
	job_init ();

	source = conf_source_new (NULL, "/path", CONF_DIR);
	file = conf_file_get (source, "/path/file");

	/* Check that a job stanza begins a block which is parsed as a
	 * Job, with the name given as an argument afterwards.
	 */
	TEST_FEATURE ("with name and block");
	strcpy (buf, "job foo\n");
	strcat (buf, "  script\n");
	strcat (buf, "    echo\n");
	strcat (buf, "  end script\n");
	strcat (buf, "end job\n");

	TEST_ALLOC_FAIL {
		pos = 0;
		lineno = 1;
		ret = parse_conf (file, buf, strlen (buf), &pos, &lineno);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			continue;
		}

		TEST_EQ (ret, 0);

		TEST_LIST_NOT_EMPTY (&file->items);

		item = (ConfItem *)file->items.next;

		TEST_ALLOC_SIZE (item, sizeof (ConfItem));
		TEST_ALLOC_PARENT (item, file);
		TEST_EQ (item->type, CONF_JOB);

		TEST_ALLOC_SIZE (item->job, sizeof (Job));
		TEST_ALLOC_PARENT (item->job, NULL);
		TEST_EQ_STR (item->job->name, "foo");
		TEST_NE_P (item->job->process[PROCESS_MAIN], NULL);
		TEST_EQ (item->job->process[PROCESS_MAIN]->script, TRUE);
		TEST_EQ_STR (item->job->process[PROCESS_MAIN]->command,
			     "echo\n");

		nih_free (item);

		TEST_LIST_EMPTY (&file->items);
	}


	/* Check that an extra argument to the job stanza results in a syntax
	 * error at the point of the argument.
	 */
	TEST_FEATURE ("with extra argument");
	strcpy (buf, "job foo bar\n");
	strcat (buf, "  script\n");
	strcat (buf, "    echo\n");
	strcat (buf, "  end script\n");
	strcat (buf, "end job\n");

	pos = 0;
	lineno = 1;
	ret = parse_conf (file, buf, strlen (buf), &pos, &lineno);

	TEST_LT (ret, 0);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNEXPECTED_TOKEN);
	nih_free (err);

	TEST_EQ (lineno, 1);


	/* Check that a missing argument to the job stanza results in a syntax
	 * error at the point of the opening.
	 */
	TEST_FEATURE ("with missing argument");
	strcpy (buf, "job\n");
	strcat (buf, "  script\n");
	strcat (buf, "    echo\n");
	strcat (buf, "  end script\n");
	strcat (buf, "end job\n");

	pos = 0;
	lineno = 1;
	ret = parse_conf (file, buf, strlen (buf), &pos, &lineno);

	TEST_LT (ret, 0);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_EXPECTED_TOKEN);
	nih_free (err);

	TEST_EQ (lineno, 1);


	/* Check that a parse error within the job itself is still caught,
	 * and that the lineno variable points at the error, not past the
	 * block.
	 */
	TEST_FEATURE ("with error in job");
	strcpy (buf, "job foo\n");
	strcat (buf, "  respin\n");
	strcat (buf, "  script\n");
	strcat (buf, "    echo\n");
	strcat (buf, "  end script\n");
	strcat (buf, "end job\n");

	pos = 0;
	lineno = 1;
	ret = parse_conf (file, buf, strlen (buf), &pos, &lineno);

	TEST_LT (ret, 0);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNKNOWN_STANZA);
	nih_free (err);

	TEST_EQ (lineno, 2);


	/* Check that the block itself is the limit for the job, and that
	 * invalid nesting of blocks is not permitted.
	 */
	TEST_FEATURE ("with invalid nesting");
	strcpy (buf, "job foo\n");
	strcat (buf, "  script\n");
	strcat (buf, "    echo\n");
	strcat (buf, "end job\n");
	strcat (buf, "  end script\n");

	pos = 0;
	lineno = 1;
	ret = parse_conf (file, buf, strlen (buf), &pos, &lineno);

	TEST_LT (ret, 0);

	err = nih_error_get ();
	TEST_EQ (err->number, NIH_CONFIG_UNTERMINATED_BLOCK);
	nih_free (err);

	TEST_EQ (lineno, 4);


	nih_free (source);
}


int
main (int   argc,
      char *argv[])
{
	test_parse_conf ();

	test_stanza_job ();

	return 0;
}
