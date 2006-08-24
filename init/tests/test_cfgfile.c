/* upstart
 *
 * test_cfgfile.c - test suite for init/cfgfile.c
 *
 * Copyright © 2006 Canonical Ltd.
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

#include <config.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/timer.h>

#include "cfgfile.h"


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

int
test_read_job (void)
{
	Job  *job;
	FILE *jf, *output;
	char  dirname[25], filename[35], text[161];
	int   ret = 0, i, oldstderr;

	printf ("Testing cfg_read_job()\n");
	sprintf (dirname, "/tmp/test_cfgfile.%d", getpid ());
	sprintf (filename, "%s/foo", dirname);
	mkdir (dirname, 0700);

	output = tmpfile ();
	oldstderr = dup (STDERR_FILENO);


	printf ("...with simple job file\n");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon -d\n");
	fprintf (jf, "start script\n");
	fprintf (jf, "    rm /var/lock/daemon\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	/* Command should be that passed to exec */
	if (strcmp (job->command, "/sbin/daemon -d")) {
		printf ("BAD: command wasn't what we expected.\n");
		ret = 1;
	}

	/* Start script should be that passed to exec */
	if (strcmp (job->start_script, "rm /var/lock/daemon\n")) {
		printf ("BAD: start script wasn't what we expected.\n");
		ret = 1;
	}

	/* Should be allocated with nih_alloc */
	if (nih_alloc_size (job) != sizeof (Job)) {
		printf ("BAD: nih_alloc was not used.\n");
		ret = 1;
	}

	/* Command should be nih_alloc child of job */
	if (nih_alloc_parent (job->command) != job) {
		printf ("BAD: command was not nih_alloc child of job.\n");
		ret = 1;
	}

	/* Start script should be nih_alloc child of job */
	if (nih_alloc_parent (job->start_script) != job) {
		printf ("BAD: start script was not nih_alloc child of job.\n");
		ret = 1;
	}


	printf ("...with re-reading existing job file\n");
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

	/* Old job should have been freed */
	if (! was_called) {
		printf ("BAD: original job was not freed.\n");
		ret = 1;
	}

	/* Goal should have been copied */
	if (job->goal != JOB_START) {
		printf ("BAD: job goal was not copied.\n");
		ret = 1;
	}

	/* State should have been copied */
	if (job->state != JOB_RUNNING) {
		printf ("BAD: job state was not copied.\n");
		ret = 1;
	}

	/* Process state should have been copied */
	if (job->process_state != PROCESS_ACTIVE) {
		printf ("BAD: process state was not copied.\n");
		ret = 1;
	}

	/* pid should have been copied */
	if (job->pid != 1000) {
		printf ("BAD: pid was not copied.\n");
		ret = 1;
	}

	/* New kill timer should have been created */
	if (nih_alloc_parent (job->kill_timer) != job) {
		printf ("BAD: newly parented timer wasn't created.\n");
		ret = 1;
	}

	/* Due time should be copied */
	if (job->kill_timer->due > time(NULL) + 1000) {
		printf ("BAD: timer due time wasn't copied.\n");
		ret =1;
	}

	/* Timer callback should be copied */
	if (job->kill_timer->callback != my_timer) {
		printf ("BAD: timer calback wasn't copied.\n");
		ret = 1;
	}

	/* Timer data should be set to new job */
	if (job->kill_timer->data != job) {
		printf ("BAD: timer data wasn't updated.\n");
		ret = 1;
	}

	/* New pid timer should have been created */
	if (nih_alloc_parent (job->pid_timer) != job) {
		printf ("BAD: newly parented timer wasn't created.\n");
		ret = 1;
	}

	/* Due time should be copied */
	if (job->pid_timer->due > time(NULL) + 500) {
		printf ("BAD: timer due time wasn't copied.\n");
		ret =1;
	}

	/* Timer callback should be copied */
	if (job->pid_timer->callback != my_timer) {
		printf ("BAD: timer calback wasn't copied.\n");
		ret = 1;
	}

	/* Timer data should be set to new job */
	if (job->pid_timer->data != job) {
		printf ("BAD: timer data wasn't updated.\n");
		ret = 1;
	}

	nih_list_free (&job->entry);


	printf ("...with complete job file\n");
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
	fprintf (jf, "start when default-route is up\n");
	fprintf (jf, "stop when default-route is down\n");
	fprintf (jf, "\n");
	fprintf (jf, "on explosion\n");
	fprintf (jf, "when life is over\n");
	fprintf (jf, "while sanity is gone\n");
	fprintf (jf, "\n");
	fprintf (jf, "depends frodo bilbo\n");
	fprintf (jf, "depends galadriel\n");
	fprintf (jf, "\n");
	fprintf (jf, "env PATH=\"/usr/games:/usr/bin\"\n");
	fprintf (jf, "env LANG=C\n");
	fprintf (jf, "\n");
	fprintf (jf, "umask 0155\n");
	fprintf (jf, "nice -20\n");
	fprintf (jf, "limit core 0 0\n");
	fprintf (jf, "limit cpu 50 100\n");
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

	/* Description should be the unquoted string */
	if (strcmp (job->description, "an example daemon")) {
		printf ("BAD: description wasn't what we expected.\n");
		ret = 1;
	}

	/* Author should be the unquoted string */
	if (strcmp (job->author, "joe bloggs")) {
		printf ("BAD: author wasn't what we expected.\n");
		ret = 1;
	}

	/* Version should be the unquoted string */
	if (strcmp (job->version, "1.0")) {
		printf ("BAD: version wasn't what we expected.\n");
		ret = 1;
	}

	/* Command should be the exact string */
	if (strcmp (job->command, "/sbin/daemon -d \"arg here\"")) {
		printf ("BAD: command wasn't what we expected.\n");
		ret = 1;
	}

	/* Start script should be the fragment with initial ws removed */
	if (strcmp (job->start_script,
		    ("  [ -d /var/run/daemon ] || mkdir /var/run/daemon\n"
		     "[ -d /var/lock/daemon ] || mkdir /var/lock/daemon\n"))) {
		printf ("BAD: start script wasn't what we expected.\n");
		ret = 1;
	}

	/* Stop script should be the fragment with initial ws removed */
	if (strcmp (job->stop_script,
		    "rm -rf /var/run/daemon /var/lock/daemon\n")) {
		printf ("BAD: stop script wasn't what we expected.\n");
		ret = 1;
	}

	/* chroot should be the filename given */
	if (strcmp (job->chroot, "/jail/daemon")) {
		printf ("BAD: chroot wasn't what we expected.\n");
		ret = 1;
	}

	/* chdir should be the filename given */
	if (strcmp (job->chdir, "/var/lib")) {
		printf ("BAD: chdir wasn't what we expected.\n");
		ret = 1;
	}

	/* Should be marked as requiring respawn */
	if (! job->respawn) {
		printf ("BAD: respawn wasn't what we expected.\n");
		ret = 1;
	}

	/* Console type should be CONSOLE_OWNER */
	if (job->console != CONSOLE_OWNER) {
		printf ("BAD: console type wasn't what we expected.\n");
		ret = 1;
	}

	/* Umask should be the value given */
	if (job->umask != 0155) {
		printf ("BAD: file creation mask wasn't what we expected.\n");
		ret = 1;
	}

	/* Nice level should be the value given */
	if (job->nice != -20) {
		printf ("BAD: nice level wasn't what we expected.\n");
		ret = 1;
	}

	/* Kill timeout should be the value given */
	if (job->kill_timeout != 30) {
		printf ("BAD: kill timeout wasn't what we expected.\n");
		ret = 1;
	}

	/* Start event list should not be empty */
	if (NIH_LIST_EMPTY (&job->start_events)) {
		printf ("BAD: start events list unexpectedly empty.\n");
		ret = 1;
	}

 	/* Check the start events list */
	i = 0;
	NIH_LIST_FOREACH (&job->start_events, iter) {
		Event *event = (Event *)iter;

		/* Name should be one we expect */
		if (! strcmp (event->name, "startup")) {
			i |= 1;

			/* Event should have no value */
			if (event->value != NULL) {
				printf ("BAD: event value wasn't what we "
					"expected.\n");
				ret = 1;
			}
		} else if (! strcmp (event->name, "default-route")) {
			i |= 2;

			/* Event should have a value */
			if (strcmp (event->value, "up")) {
				printf ("BAD: event value wasn't what we "
					"expected.\n");
				ret = 1;
			}
		} else if (! strcmp (event->name, "explosion")) {
			i |= 4;

			/* Event should have no value */
			if (event->value != NULL) {
				printf ("BAD: event value wasn't what we "
					"expected.\n");
				ret = 1;
			}
		} else if (! strcmp (event->name, "life")) {
			i |= 8;

			/* Event should have a value */
			if (strcmp (event->value, "over")) {
				printf ("BAD: event value wasn't what we "
					"expected.\n");
				ret = 1;
			}
		} else if (! strcmp (event->name, "sanity")) {
			i |= 16;

			/* Event should have a value */
			if (strcmp (event->value, "gone")) {
				printf ("BAD: event value wasn't what we "
					"expected.\n");
				ret = 1;
			}
		} else {
			printf ("BAD: event name wasn't what we expected.\n");
			ret = 1;
		}

		/* Should be child of job */
		if (nih_alloc_parent (event) != job) {
			printf ("BAD: event wasn't nih_alloc child.\n");
			ret = 1;
		}
	}

	/* Should have had both */
	if (i != 31) {
		printf ("BAD: start events list wasn't what we expected.\n");
		ret = 1;
	}

	/* Stop event list should not be empty */
	if (NIH_LIST_EMPTY (&job->stop_events)) {
		printf ("BAD: stop events list unexpectedly empty.\n");
		ret = 1;
	}

 	/* Check the stop events list */
	i = 0;
	NIH_LIST_FOREACH (&job->stop_events, iter) {
		Event *event = (Event *)iter;

		/* Name should be one we expect */
		if (! strcmp (event->name, "shutdown")) {
			i |= 1;

			/* Event should have no value */
			if (event->value != NULL) {
				printf ("BAD: event value wasn't what we "
					"expected.\n");
				ret = 1;
			}
		} else if (! strcmp (event->name, "default-route")) {
			i |= 2;

			/* Event should have a value */
			if (strcmp (event->value, "down")) {
				printf ("BAD: event value wasn't what we "
					"expected.\n");
				ret = 1;
			}
		} else if (! strcmp (event->name, "sanity")) {
			i |= 4;

			/* Event should have no value */
			if (event->value != NULL) {
				printf ("BAD: event value wasn't what we "
					"expected.\n");
				ret = 1;
			}
		} else {
			printf ("BAD: event name wasn't what we expected.\n");
			ret = 1;
		}

		/* Should be child of job */
		if (nih_alloc_parent (event) != job) {
			printf ("BAD: event wasn't nih_alloc child.\n");
			ret = 1;
		}
	}

	/* Should have had both */
	if (i != 7) {
		printf ("BAD: stop events list wasn't what we expected.\n");
		ret = 1;
	}

	/* Dependency list should not be empty */
	if (NIH_LIST_EMPTY (&job->depends)) {
		printf ("BAD: dependency list unexpectedly empty.\n");
		ret = 1;
	}

 	/* Check the dependency list */
	i = 0;
	NIH_LIST_FOREACH (&job->depends, iter) {
		JobName *dep = (JobName *)iter;

		/* Name should be one we expect */
		if (! strcmp (dep->name, "frodo")) {
			i |= 1;
		} else if (! strcmp (dep->name, "bilbo")) {
			i |= 2;
		} else if (! strcmp (dep->name, "galadriel")) {
			i |= 4;
		} else {
			printf ("BAD: dep name wasn't what we expected.\n");
			ret = 1;
		}

		/* Should be child of job */
		if (nih_alloc_parent (dep) != job) {
			printf ("BAD: dependency wasn't nih_alloc child.\n");
			ret = 1;
		}
	}

	/* Should have had both */
	if (i != 7) {
		printf ("BAD: dependency list wasn't what we expected.\n");
		ret = 1;
	}

	/* Environment should be provided */
	if (job->env == NULL) {
		printf ("BAD: environment list unexpectedly empty.\n");
		ret = 1;
	}

	/* Environment array should be child of job */
	if (nih_alloc_parent (job->env) != job) {
		printf ("BAD: environment array wasn't nih_alloc child.\n");
		ret = 1;
	}

	/* First environment variable should be unquoted */
	if (strcmp (job->env[0], "PATH=/usr/games:/usr/bin")) {
		printf ("BAD: PATH variable wasn't what we expected.\n");
		ret = 1;
	}

	/* Environment variable should be child of array */
	if (nih_alloc_parent (job->env[0]) != job->env) {
		printf ("BAD: environment var wasn't nih_alloc child.\n");
		ret = 1;
	}

	/* Second environment variable should be unquoted */
	if (strcmp (job->env[1], "LANG=C")) {
		printf ("BAD: LANG variable wasn't what we expected.\n");
		ret = 1;
	}

	/* Environment variable should be child of array */
	if (nih_alloc_parent (job->env[1]) != job->env) {
		printf ("BAD: environment var wasn't nih_alloc child.\n");
		ret = 1;
	}

	/* Last environment variable should be NULL */
	if (job->env[2] != NULL) {
		printf ("BAD: last environment wasn't what we expected.\n");
		ret = 1;
	}

	/* Normal exit array should not be NULL */
	if (job->normalexit == NULL) {
		printf ("BAD: exit array unexpectedly empty.\n");
		ret = 1;
	}

	/* Length of normal exit array should be three */
	if (job->normalexit_len != 3) {
		printf ("BAD: exit array length wasn't what we expected.\n");
		ret = 1;
	}

	/* First exit status should be what we expected */
	if (job->normalexit[0] != 0) {
		printf ("BAD: exit status wasn't what we expected.\n");
		ret = 1;
	}

	/* Second exit status should be what we expected */
	if (job->normalexit[1] != 99) {
		printf ("BAD: exit status wasn't what we expected.\n");
		ret = 1;
	}

	/* Third exit status should be what we expected */
	if (job->normalexit[2] != 100) {
		printf ("BAD: exit status wasn't what we expected.\n");
		ret = 1;
	}

	/* Normal exit array should be nih_alloc child of job */
	if (nih_alloc_parent (job->normalexit) != job) {
		printf ("BAD: exit array wasn't nih_alloc child of job.\n");
		ret = 1;
	}

	/* RLIMIT_CORE limit should be set */
	if (job->limits[RLIMIT_CORE] == NULL) {
		printf ("BAD: core limit wasn't set.\n");
		ret = 1;
	}

	/* RLIMIT_CORE soft limit should be that given */
	if (job->limits[RLIMIT_CORE]->rlim_cur != 0) {
		printf ("BAD: core soft limit wasn't what we expected.\n");
		ret = 1;
	}

	/* RLIMIT_CORE hard limit should be that given */
	if (job->limits[RLIMIT_CORE]->rlim_max != 0) {
		printf ("BAD: core hard limit wasn't what we expected.\n");
		ret = 1;
	}

	/* RLIMIT_CORE limit should be allocated with nih_alloc */
	if (nih_alloc_size (job->limits[RLIMIT_CORE])
	    != sizeof (struct rlimit)) {
		printf ("BAD: core limit wasn't allocated with nih_alloc.\n");
		ret = 1;
	}

	/* RLIMIT_CORE limit should be nih_alloc child of job */
	if (nih_alloc_parent (job->limits[RLIMIT_CORE]) != job) {
		printf ("BAD: core limit wasn't nih_alloc child of job.\n");
		ret = 1;
	}

	/* RLIMIT_CPU limit should be set */
	if (job->limits[RLIMIT_CPU] == NULL) {
		printf ("BAD: cpu limit wasn't set.\n");
		ret = 1;
	}

	/* RLIMIT_CPU soft limit should be that given */
	if (job->limits[RLIMIT_CPU]->rlim_cur != 50) {
		printf ("BAD: cpu soft limit wasn't what we expected.\n");
		ret = 1;
	}

	/* RLIMIT_CPU hard limit should be that given */
	if (job->limits[RLIMIT_CPU]->rlim_max != 100) {
		printf ("BAD: cpu hard limit wasn't what we expected.\n");
		ret = 1;
	}

	/* RLIMIT_CPU limit should be allocated with nih_alloc */
	if (nih_alloc_size (job->limits[RLIMIT_CPU])
	    != sizeof (struct rlimit)) {
		printf ("BAD: cpu limit wasn't allocated with nih_alloc.\n");
		ret = 1;
	}

	/* RLIMIT_CPU limit should be nih_alloc child of job */
	if (nih_alloc_parent (job->limits[RLIMIT_CPU]) != job) {
		printf ("BAD: cpu limit wasn't nih_alloc child of job.\n");
		ret = 1;
	}

	/* Description should be nih_alloc child of job */
	if (nih_alloc_parent (job->description) != job) {
		printf ("BAD: description was not nih_alloc child of job.\n");
		ret = 1;
	}

	/* Author should be nih_alloc child of job */
	if (nih_alloc_parent (job->author) != job) {
		printf ("BAD: author was not nih_alloc child of job.\n");
		ret = 1;
	}

	/* Version should be nih_alloc child of job */
	if (nih_alloc_parent (job->version) != job) {
		printf ("BAD: version was not nih_alloc child of job.\n");
		ret = 1;
	}

	/* Command should be nih_alloc child of job */
	if (nih_alloc_parent (job->command) != job) {
		printf ("BAD: command was not nih_alloc child of job.\n");
		ret = 1;
	}

	/* Start script should be nih_alloc child of job */
	if (nih_alloc_parent (job->start_script) != job) {
		printf ("BAD: start script was not nih_alloc child of job.\n");
		ret = 1;
	}

	/* Stop script should be nih_alloc child of job */
	if (nih_alloc_parent (job->stop_script) != job) {
		printf ("BAD: stop script was not nih_alloc child of job.\n");
		ret = 1;
	}

	/* chroot should be nih_alloc child of job */
	if (nih_alloc_parent (job->chroot) != job) {
		printf ("BAD: chroot was not nih_alloc child of job.\n");
		ret = 1;
	}

	/* chdir should be nih_alloc child of job */
	if (nih_alloc_parent (job->chdir) != job) {
		printf ("BAD: chdir was not nih_alloc child of job.\n");
		ret = 1;
	}

	nih_list_free (&job->entry);


	printf ("...with exec and respawn\n");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /usr/bin/foo arg\n");
	fprintf (jf, "respawn\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	/* Job should be respawned */
	if (! job->respawn) {
		printf ("BAD: respawn wasn't what we expected.\n");
		ret = 1;
	}

	/* Command should be what we put */
	if (strcmp (job->command, "/usr/bin/foo arg")) {
		printf ("BAD: command wasn't what we expected.\n");
		ret = 1;
	}

	nih_list_free (&job->entry);


	printf ("...with arguments to respawn\n");
	jf = fopen (filename, "w");
	fprintf (jf, "respawn /usr/bin/foo arg\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	/* Job should be respawned */
	if (! job->respawn) {
		printf ("BAD: respawn wasn't what we expected.\n");
		ret = 1;
	}

	/* Command should be what we put */
	if (strcmp (job->command, "/usr/bin/foo arg")) {
		printf ("BAD: command wasn't what we expected.\n");
		ret = 1;
	}

	nih_list_free (&job->entry);


	printf ("...with exec and daemon\n");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /usr/bin/foo arg\n");
	fprintf (jf, "daemon\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	/* Job should be hunted for */
	if (! job->daemon) {
		printf ("BAD: daemon wasn't what we expected.\n");
		ret = 1;
	}

	/* Command should be what we put */
	if (strcmp (job->command, "/usr/bin/foo arg")) {
		printf ("BAD: command wasn't what we expected.\n");
		ret = 1;
	}

	nih_list_free (&job->entry);


	printf ("...with arguments to daemon\n");
	jf = fopen (filename, "w");
	fprintf (jf, "daemon /usr/bin/foo arg\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	/* Job should be hunted for */
	if (! job->daemon) {
		printf ("BAD: daemon wasn't what we expected.\n");
		ret = 1;
	}

	/* Command should be what we put */
	if (strcmp (job->command, "/usr/bin/foo arg")) {
		printf ("BAD: command wasn't what we expected.\n");
		ret = 1;
	}

	nih_list_free (&job->entry);


	printf ("...with instance job\n");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /usr/bin/foo\n");
	fprintf (jf, "instance\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	/* Job should be an instance */
	if (! job->spawns_instance) {
		printf ("BAD: spawns_instance wasn't what we expected.\n");
		ret = 1;
	}

	nih_list_free (&job->entry);


	printf ("...with interesting formatting\n");
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

	/* Description should be stripped of newline */
	if (strcmp (job->description, "foo bar")) {
		printf ("BAD: description wasn't what we expected.\n");
		ret = 1;
	}

	/* Author should be unchanged */
	if (strcmp (job->author, "  something  with  spaces  ")) {
		printf ("BAD: author wasn't what we expected.\n");
		ret = 1;
	}

	/* Version should have slash stripped */
	if (strcmp (job->version, "foo'bar")) {
		printf ("BAD: version wasn't what we expected.\n");
		ret = 1;
	}

	/* Command should be laid out sensibly */
	if (strcmp (job->command, "/usr/bin/foo first "
		    "second \"third argument\"")) {
		printf ("BAD: command wasn't what we expected.\n");
		ret = 1;
	}

	nih_list_free (&job->entry);


	printf ("...with odd constructions\n");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/foo\n");
	fprintf (jf, "start when foo is\n");
	fprintf (jf, "start when foo is is\n");
	fprintf (jf, "stop when foo is\n");
	fprintf (jf, "stop when foo is is\n");
	fprintf (jf, "when foo is\n");
	fprintf (jf, "when foo is is\n");
	fprintf (jf, "while foo is\n");
	fprintf (jf, "while foo is is\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	/* Start events should not be empty */
	if (NIH_LIST_EMPTY (&job->start_events)) {
		printf ("BAD: job list unexpectedly empty.\n");
		ret = 1;
	}

	/* Check start events */
	NIH_LIST_FOREACH (&job->start_events, iter) {
		Event *event = (Event *)iter;

		if (strcmp (event->name, "foo")) {
			printf ("BAD: event name wasn't what we expected.\n");
			ret = 1;
		}

		if (strcmp (event->value, "is")) {
			printf ("BAD: event value wasn't what we expected.\n");
			ret = 1;
		}
	}

	/* Stop events should not be empty */
	if (NIH_LIST_EMPTY (&job->stop_events)) {
		printf ("BAD: job list unexpectedly empty.\n");
		ret = 1;
	}

	/* Check stop events */
	NIH_LIST_FOREACH (&job->stop_events, iter) {
		Event *event = (Event *)iter;

		if (strcmp (event->name, "foo")) {
			printf ("BAD: event name wasn't what we expected.\n");
			ret = 1;
		}

		if ((event->value != NULL) && strcmp (event->value, "is")) {
			printf ("BAD: event value wasn't what we expected.\n");
			ret = 1;
		}
	}

	nih_list_free (&job->entry);


	printf ("...with things that aren't script ends\n");
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

	/* Start script should hold the first set */
	if (strcmp (job->start_script,
		    "endscript\nend foo\nend scripting\n")) {
		printf ("BAD: start script wasn't what we expected.\n");
		ret = 1;
	}

	/* Stop script should be set */
	if (strcmp (job->stop_script, "# ok\n")) {
		printf ("BAD: stop script wasn't what we expected.\n");
		ret = 1;
	}

	nih_list_free (&job->entry);


	printf ("...with multiple stanzas\n");
	jf = fopen (filename, "w");
	fprintf (jf, "respawn\n");
	fprintf (jf, "\n");
	fprintf (jf, "description oops\n");
	fprintf (jf, "description yay\n");
	fprintf (jf, "author oops\n");
	fprintf (jf, "author yay\n");
	fprintf (jf, "version oops\n");
	fprintf (jf, "version yay\n");
	fprintf (jf, "\n");
	fprintf (jf, "start script\n");
	fprintf (jf, "oops\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "\n");
	fprintf (jf, "start script\n");
	fprintf (jf, "yay\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "\n");
	fprintf (jf, "stop script\n");
	fprintf (jf, "oops\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "\n");
	fprintf (jf, "stop script\n");
	fprintf (jf, "yay\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "\n");
	fprintf (jf, "respawn script\n");
	fprintf (jf, "oops\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "\n");
	fprintf (jf, "respawn script\n");
	fprintf (jf, "yay\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "\n");
	fprintf (jf, "exec oops\n");
	fprintf (jf, "exec yay\n");
	fprintf (jf, "\n");
	fprintf (jf, "chroot oops\n");
	fprintf (jf, "chroot yay\n");
	fprintf (jf, "chdir oops\n");
	fprintf (jf, "chdir yay\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	/* Description should be second one given */
	if (strcmp (job->description, "yay")) {
		printf ("BAD: description wasn't what we expected.\n");
		ret = 1;
	}

	/* Author should be second one given */
	if (strcmp (job->author, "yay")) {
		printf ("BAD: author wasn't what we expected.\n");
		ret = 1;
	}

	/* Version should be second one given */
	if (strcmp (job->version, "yay")) {
		printf ("BAD: version wasn't what we expected.\n");
		ret = 1;
	}

	/* Start script should be second one given */
	if (strcmp (job->start_script, "yay\n")) {
		printf ("BAD: start_script wasn't what we expected.\n");
		ret = 1;
	}

	/* Stop script should be second one given */
	if (strcmp (job->stop_script, "yay\n")) {
		printf ("BAD: stop_script wasn't what we expected.\n");
		ret = 1;
	}

	/* Respawn script should be second one given */
	if (strcmp (job->respawn_script, "yay\n")) {
		printf ("BAD: respawn_script wasn't what we expected.\n");
		ret = 1;
	}

	/* Command should be second one given */
	if (strcmp (job->command, "yay")) {
		printf ("BAD: command wasn't what we expected.\n");
		ret = 1;
	}

	/* chroot should be second one given */
	if (strcmp (job->chroot, "yay")) {
		printf ("BAD: chroot wasn't what we expected.\n");
		ret = 1;
	}

	/* chdir should be second one given */
	if (strcmp (job->chdir, "yay")) {
		printf ("BAD: chdir wasn't what we expected.\n");
		ret = 1;
	}

	nih_list_free (&job->entry);


	printf ("...with multiple script stanzas\n");
	jf = fopen (filename, "w");
	fprintf (jf, "script\n");
	fprintf (jf, "oops\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "\n");
	fprintf (jf, "script\n");
	fprintf (jf, "yay\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	/* Script should be second one given */
	if (strcmp (job->script, "yay\n")) {
		printf ("BAD: script wasn't what we expected.\n");
		ret = 1;
	}

	nih_list_free (&job->entry);


	printf ("...with exec and respawn\n");
	jf = fopen (filename, "w");
	fprintf (jf, "exec oops\n");
	fprintf (jf, "respawn yay\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	/* Command should be second one given */
	if (strcmp (job->command, "yay")) {
		printf ("BAD: command wasn't what we expected.\n");
		ret = 1;
	}

	nih_list_free (&job->entry);


	printf ("...with exec and daemon\n");
	jf = fopen (filename, "w");
	fprintf (jf, "exec oops\n");
	fprintf (jf, "daemon yay\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	/* Command should be second one given */
	if (strcmp (job->command, "yay")) {
		printf ("BAD: command wasn't what we expected.\n");
		ret = 1;
	}

	nih_list_free (&job->entry);


	printf ("...with various errors\n");
	jf = fopen (filename, "w");
	fprintf (jf, "description\n");
	fprintf (jf, "description foo bar\n");
	fprintf (jf, "author\n");
	fprintf (jf, "author foo bar\n");
	fprintf (jf, "version\n");
	fprintf (jf, "version foo bar\n");
	fprintf (jf, "depends\n");

	fprintf (jf, "on\n");
	fprintf (jf, "on foo bar\n");
	fprintf (jf, "when\n");
	fprintf (jf, "when foo\n");
	fprintf (jf, "when foo is bar baz\n");
	fprintf (jf, "while\n");
	fprintf (jf, "while foo\n");
	fprintf (jf, "while foo is bar baz\n");

	fprintf (jf, "start\n");
	fprintf (jf, "start on\n");
	fprintf (jf, "start on foo bar\n");
	fprintf (jf, "start when\n");
	fprintf (jf, "start when foo\n");
	fprintf (jf, "start when foo is bar baz\n");
	fprintf (jf, "start wibble\n");
	fprintf (jf, "stop\n");
	fprintf (jf, "stop on\n");
	fprintf (jf, "stop on foo bar\n");
	fprintf (jf, "stop when\n");
	fprintf (jf, "stop when foo\n");
	fprintf (jf, "stop when foo is bar baz\n");
	fprintf (jf, "stop wibble\n");
	fprintf (jf, "exec\n");
	fprintf (jf, "instance foo\n");
	fprintf (jf, "pid\n");
	fprintf (jf, "pid file\n");
	fprintf (jf, "pid file foo baz\n");
	fprintf (jf, "pid binary\n");
	fprintf (jf, "pid binary foo baz\n");
	fprintf (jf, "pid timeout\n");
	fprintf (jf, "pid timeout abc\n");
	fprintf (jf, "pid timeout -40\n");
	fprintf (jf, "pid timeout 10 20\n");
	fprintf (jf, "pid wibble\n");
	fprintf (jf, "kill\n");
	fprintf (jf, "kill timeout\n");
	fprintf (jf, "kill timeout abc\n");
	fprintf (jf, "kill timeout -40\n");
	fprintf (jf, "kill timeout 10 20\n");
	fprintf (jf, "kill wibble\n");
	fprintf (jf, "normalexit\n");
	fprintf (jf, "normalexit abc\n");
	fprintf (jf, "console\n");
	fprintf (jf, "console wibble\n");
	fprintf (jf, "console output foo\n");
	fprintf (jf, "env\n");
	fprintf (jf, "env foo=bar baz\n");
	fprintf (jf, "umask\n");
	fprintf (jf, "umask abc\n");
	fprintf (jf, "umask 12345\n");
	fprintf (jf, "umask 099\n");
	fprintf (jf, "umask 0122 foo\n");
	fprintf (jf, "nice\n");
	fprintf (jf, "nice abc\n");
	fprintf (jf, "nice -30\n");
	fprintf (jf, "nice 25\n");
	fprintf (jf, "nice 0 foo\n");
	fprintf (jf, "limit\n");
	fprintf (jf, "limit wibble\n");
	fprintf (jf, "limit core\n");
	fprintf (jf, "limit core 0\n");
	fprintf (jf, "limit core abc 0\n");
	fprintf (jf, "limit core 0 abc\n");
	fprintf (jf, "limit core 0 0 0\n");
	fprintf (jf, "chroot\n");
	fprintf (jf, "chroot / foo\n");
	fprintf (jf, "chdir\n");
	fprintf (jf, "chdir / foo\n");
	fprintf (jf, "wibble\n");
	fprintf (jf, "script foo\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "start script foo\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "stop script foo\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "respawn script foo\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "respawn\n");
	fclose (jf);

	dup2 (fileno (output), STDERR_FILENO);
	job = cfg_read_job (NULL, filename, "test");
	dup2 (oldstderr, STDERR_FILENO);

	rewind (output);

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:1: expected job description\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:2: ignored additional arguments\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:3: expected author name\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:4: ignored additional arguments\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:5: expected version string\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:6: ignored additional arguments\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:7: expected job name\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:8: expected event name\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:9: ignored additional arguments\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:10: expected event name\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:11: expected 'is' or event value\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:12: ignored additional arguments\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:13: expected event name\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:14: expected 'is' or event value\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:15: ignored additional arguments\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:16: expected 'on', 'when' or 'script'\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:17: expected event name\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:18: ignored additional arguments\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:19: expected event name\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:20: expected 'is' or event value\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:21: ignored additional arguments\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:22: expected 'on', 'when' or 'script'\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:23: expected 'on', 'when' or 'script'\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:24: expected event name\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:25: ignored additional arguments\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:26: expected event name\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:27: expected 'is' or event value\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:28: ignored additional arguments\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:29: expected 'on', 'when' or 'script'\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:30: expected command\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:31: ignored additional arguments\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:32: expected 'file', 'binary' or 'timeout'\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:33: expected pid filename\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:34: ignored additional arguments\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:35: expected binary filename\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:36: ignored additional arguments\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:37: expected timeout\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:38: illegal value\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:39: illegal value\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:40: ignored additional arguments\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:41: expected 'file', 'binary' or 'timeout'\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:42: expected 'timeout'\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:43: expected timeout\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:44: illegal value\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:45: illegal value\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:46: ignored additional arguments\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:47: expected 'timeout'\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:48: expected exit status\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:49: illegal value\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:50: expected 'logged', 'output', 'owner' or 'none'\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:51: expected 'logged', 'output', 'owner' or 'none'\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:52: ignored additional arguments\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:53: expected variable setting\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:54: ignored additional arguments\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:55: expected file creation mask\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:56: illegal value\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:57: illegal value\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:58: illegal value\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:59: ignored additional arguments\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:60: expected nice level\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:61: illegal value\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:62: illegal value\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:63: illegal value\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:64: ignored additional arguments\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:65: expected limit name\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:66: unknown limit type\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:67: expected soft limit\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:68: expected hard limit\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:69: illegal value\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:70: illegal value\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:71: ignored additional arguments\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:72: expected directory name\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:73: ignored additional arguments\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:74: expected directory name\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:75: ignored additional arguments\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:76: ignored unknown stanza\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:77: ignored additional arguments\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:79: ignored additional arguments\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:81: ignored additional arguments\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:83: ignored additional arguments\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	/* Should be no more output */
	if (fgets (text, sizeof (text), output)) {
		printf ("BAD: more output than we expected.\n");
		ret = 1;
	}

	nih_list_free (&job->entry);

	rewind (output);
	ftruncate (fileno (output), 0);


	printf ("...with unterminated quote\n");
	jf = fopen (filename, "w");
	fprintf (jf, "exec \"/sbin/foo bar");
	fclose (jf);

	dup2 (fileno (output), STDERR_FILENO);
	job = cfg_read_job (NULL, filename, "test");
	dup2 (oldstderr, STDERR_FILENO);

	rewind (output);

	/* Should have error output */
	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:1: unterminated quoted string\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	/* Should be no more output */
	if (fgets (text, sizeof (text), output)) {
		printf ("BAD: more output than we expected.\n");
		ret = 1;
	}

	/* Command should still be set */
	if (strcmp (job->command, "\"/sbin/foo bar")) {
		printf ("BAD: command wasn't what we expected.\n");
		ret = 1;
	}

	nih_list_free (&job->entry);

	rewind (output);
	ftruncate (fileno (output), 0);


	printf ("...with trailing slash\n");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/foo bar \\");
	fclose (jf);

	dup2 (fileno (output), STDERR_FILENO);
	job = cfg_read_job (NULL, filename, "test");
	dup2 (oldstderr, STDERR_FILENO);

	rewind (output);

	/* Should have error output */
	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:1: ignored trailing slash\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	/* Should be no more output */
	if (fgets (text, sizeof (text), output)) {
		printf ("BAD: more output than we expected.\n");
		ret = 1;
	}

	/* Command should still be set */
	if (strcmp (job->command, "/sbin/foo bar")) {
		printf ("BAD: command wasn't what we expected.\n");
		ret = 1;
	}

	nih_list_free (&job->entry);

	rewind (output);
	ftruncate (fileno (output), 0);


	printf ("...with incomplete script\n");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/foo\n");
	fprintf (jf, "start script\n");
	fprintf (jf, "    rm /var/lock/daemon\n");
	fprintf (jf, "    rm /var/run/daemon\n");
	fclose (jf);

	dup2 (fileno (output), STDERR_FILENO);
	job = cfg_read_job (NULL, filename, "test");
	dup2 (oldstderr, STDERR_FILENO);

	rewind (output);

	/* Should have error output */
	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo:4: 'end script' expected\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	/* Should be no more output */
	if (fgets (text, sizeof (text), output)) {
		printf ("BAD: more output than we expected.\n");
		ret = 1;
	}

	/* Script should still be set */
	if (strcmp (job->start_script,
		    "    rm /var/lock/daemon\n    rm /var/run/daemon\n")) {
		printf ("BAD: script wasn't what we expected.\n");
		ret = 1;
	}

	nih_list_free (&job->entry);

	rewind (output);
	ftruncate (fileno (output), 0);


	printf ("...with missing exec and script\n");
	jf = fopen (filename, "w");
	fprintf (jf, "description buggy");
	fclose (jf);

	dup2 (fileno (output), STDERR_FILENO);
	job = cfg_read_job (NULL, filename, "test");
	dup2 (oldstderr, STDERR_FILENO);

	rewind (output);

	/* Should have error output */
	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"),
		    "foo: 'exec' or 'script' must be specified\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	/* Should be no more output */
	if (fgets (text, sizeof (text), output)) {
		printf ("BAD: more output than we expected.\n");
		ret = 1;
	}

	/* No job should be returned */
	if (job != NULL) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}

	rewind (output);
	ftruncate (fileno (output), 0);


	printf ("...with both exec and script\n");
	jf = fopen (filename, "w");
	fprintf (jf, "description buggy\n");
	fprintf (jf, "exec /sbin/foo\n");
	fprintf (jf, "script\n");
	fprintf (jf, "   /sbin/foo\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	dup2 (fileno (output), STDERR_FILENO);
	job = cfg_read_job (NULL, filename, "test");
	dup2 (oldstderr, STDERR_FILENO);

	rewind (output);

	/* Should have error output */
	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"), "foo: only one of 'exec' and "
		    "'script' may be specified\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	/* Should be no more output */
	if (fgets (text, sizeof (text), output)) {
		printf ("BAD: more output than we expected.\n");
		ret = 1;
	}

	/* No job should be returned */
	if (job != NULL) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}

	rewind (output);
	ftruncate (fileno (output), 0);


	printf ("...with respawn options and not respawn\n");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/foo\n");
	fprintf (jf, "respawn script\n");
	fprintf (jf, "do something\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "pid file /var/run/foo.pid\n");
	fprintf (jf, "pid binary /lib/foo/foo.bin\n");
	fclose (jf);

	dup2 (fileno (output), STDERR_FILENO);
	job = cfg_read_job (NULL, filename, "test");
	dup2 (oldstderr, STDERR_FILENO);

	rewind (output);

	/* Should have error output */
	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"), "foo: 'respawn script' ignored "
		    "unless 'respawn' specified\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"), "foo: 'pid file' ignored "
		    "unless 'respawn' specified\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"), "foo: 'pid binary' ignored "
		    "unless 'respawn' specified\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	/* Should be no more output */
	if (fgets (text, sizeof (text), output)) {
		printf ("BAD: more output than we expected.\n");
		ret = 1;
	}

	nih_list_free (&job->entry);

	rewind (output);
	ftruncate (fileno (output), 0);


	printf ("...with non-existant file\n");
	unlink (filename);

	dup2 (fileno (output), STDERR_FILENO);
	job = cfg_read_job (NULL, filename, "test");
	dup2 (oldstderr, STDERR_FILENO);

	rewind (output);

	/* Should have error output */
	fgets (text, sizeof (text), output);
	if (strcmp (strstr (text, "foo:"), "foo: unable to read: "
		    "No such file or directory\n")) {
		printf ("BAD: output wasn't what we expected.\n");
		ret = 1;
	}

	/* Should be no more output */
	if (fgets (text, sizeof (text), output)) {
		printf ("BAD: more output than we expected.\n");
		ret = 1;
	}

	/* No job should be returned */
	if (job != NULL) {
		printf ("BAD: return value wasn't what we expected.\n");
		ret = 1;
	}

	rewind (output);
	ftruncate (fileno (output), 0);


	fclose (output);

	unlink (filename);
	rmdir (dirname);

	return ret;
}


int
main (int   argc,
      char *argv[])
{
	int ret = 0;

	ret |= test_read_job ();

	return ret;
}
