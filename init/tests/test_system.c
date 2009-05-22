/* upstart
 *
 * test_system.c - test suite for init/system.c
 *
 * Copyright © 2009 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
 *

#include <nih/test.h>

#include <sys/types.h>
#include <sys/wait.h>

#include <unistd.h>

#include "system.h"


void
test_kill (void)
{
	pid_t pid1, pid2, pid3;
	int   ret, status;

	TEST_FUNCTION ("system_kill");

	/* Check that when we normally kill the process, the TERM signal
	 * is sent to all processes in its process group.
	 */
	TEST_FEATURE ("with TERM signal");
	TEST_CHILD (pid1) {
		pause ();
	}
	TEST_CHILD (pid2) {
		pause ();
	}

	setpgid (pid1, pid1);
	setpgid (pid2, pid1);

	ret = system_kill (pid1, FALSE);
	waitpid (pid1, &status, 0);

	TEST_EQ (ret, 0);
	TEST_TRUE (WIFSIGNALED (status));
	TEST_EQ (WTERMSIG (status), SIGTERM);

	waitpid (pid2, &status, 0);

	TEST_EQ (ret, 0);
	TEST_TRUE (WIFSIGNALED (status));
	TEST_EQ (WTERMSIG (status), SIGTERM);


	/* Check that when we force the kill, the KILL signal is sent
	 * instead.
	 */
	TEST_FEATURE ("with KILL signal");
	TEST_CHILD (pid1) {
		pause ();
	}
	TEST_CHILD (pid2) {
		pause ();
	}

	setpgid (pid1, pid1);
	setpgid (pid2, pid1);

	ret = system_kill (pid1, TRUE);
	waitpid (pid1, &status, 0);

	TEST_EQ (ret, 0);
	TEST_TRUE (WIFSIGNALED (status));
	TEST_EQ (WTERMSIG (status), SIGKILL);

	waitpid (pid2, &status, 0);

	TEST_EQ (ret, 0);
	TEST_TRUE (WIFSIGNALED (status));
	TEST_EQ (WTERMSIG (status), SIGKILL);


	/* Check that we can still send the signal to the process group
	 * when the leader is no longer around.
	 */
	TEST_FEATURE ("with no group leader");
	TEST_CHILD (pid1) {
		pause ();
	}
	TEST_CHILD (pid2) {
		pause ();
	}
	TEST_CHILD (pid3) {
		pause ();
	}

	setpgid (pid1, pid1);
	setpgid (pid2, pid1);
	setpgid (pid3, pid1);

	kill (pid1, SIGTERM);
	waitpid (pid1, &status, 0);

	ret = system_kill (pid2, FALSE);
	waitpid (pid2, &status, 0);

	TEST_EQ (ret, 0);
	TEST_TRUE (WIFSIGNALED (status));
	TEST_EQ (WTERMSIG (status), SIGTERM);

	waitpid (pid3, &status, 0);

	TEST_EQ (ret, 0);
	TEST_TRUE (WIFSIGNALED (status));
	TEST_EQ (WTERMSIG (status), SIGTERM);
}


int
main (int   argc,
      char *argv[])
{
	test_kill ();

	return 0;
}
