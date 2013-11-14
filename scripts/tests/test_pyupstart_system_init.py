#!/usr/bin/python3
# -*- coding: utf-8 -*-
#---------------------------------------------------------------------
# Copyright © 2013 Canonical Ltd.
#
# Author: James Hunt <james.hunt@canonical.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2, as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#---------------------------------------------------------------------

#---------------------------------------------------------------------
# Description: System-level Upstart tests for the pyupstart module.
#
# Notes: Can only be run as the root user.
#---------------------------------------------------------------------

import os
import sys

base_dir = os.path.abspath(os.path.dirname(__file__))
module_dir = os.path.normpath(os.path.realpath(base_dir + os.sep + '..'))

# top-level unpacked source directory
top_srcdir = os.path.normpath(os.path.realpath(module_dir + os.sep + '..'))

# Tell Python where the uninstalled module lives in the source tree
sys.path.append(module_dir)
from pyupstart import *

import unittest

class TestSystemUpstart(unittest.TestCase):
    def setUp(self):
        if os.geteuid():
            raise unittest.SkipTest('Need root for System-level Upstart tests')

        # Tests must not operate within a session
        self.assertEqual(None, os.environ.get('UPSTART_SESSION', None))

        self.upstart = SystemInit()

    def tearDown(self):
        # Ensure no state file exists
        state_file = '{}{}{}'.format(SYSTEM_LOG_DIR, os.sep, UPSTART_STATE_FILE)
        self.assertFalse(os.path.exists(state_file))

class TestSystemInitReExec(TestSystemUpstart):

    def test_pid1_reexec(self):
      version = self.upstart.version()
      self.assertTrue(version)

      # create a job and start it, marking it such that the .conf file
      # will be retained when object becomes unusable (after re-exec).
      job = self.upstart.job_create('sleeper', 'exec sleep 123', retain=True)
      self.assertTrue(job)

      # Used when recreating the job
      conf_path = job.conffile

      inst = job.start()
      self.assertTrue(inst)
      pids = job.pids()
      self.assertEqual(len(pids), 1)
      pid = pids['main']

      self.upstart.reexec()

      # PID 1 Upstart is now in the process of starting, but we need to
      # reconnect to it via D-Bus since it cannot yet retain client
      # connections. However, since the re-exec won't be instantaneous,
      # try a few times.
      self.upstart.polling_connect(force=True)

      # Since the parent job was created with 'retain', this is actually
      # a NOP but is added to denote that the old instance is dead.
      inst.destroy()

      # check that we can still operate on the re-exec'd Upstart
      version_postexec = self.upstart.version()
      self.assertTrue(version_postexec)
      self.assertEqual(version, version_postexec)

      # Ensure the job is still running with the same PID
      os.kill(pid, 0)

      # XXX: The re-exec will have severed the D-Bus connection to
      # Upstart. Hence, revivify the job with some magic.
      job = self.upstart.job_recreate('sleeper', conf_path)
      self.assertTrue(job)

      # Recreate the instance
      inst = job.get_instance()
      self.assertTrue(inst)

      self.assertTrue(job.running('_'))
      pids = job.pids()
      self.assertEqual(len(pids), 1)
      self.assertTrue(pids['main'])

      # The pid should not have changed after a restart
      self.assertEqual(pid, pids['main'])

      job.stop()

      # Ensure the pid has gone
      with self.assertRaises(ProcessLookupError):
          os.kill(pid, 0)

      # Clean up
      self.upstart.destroy()

class TestSystemInitChrootSession(TestSystemUpstart):
    CHROOT_ENVVAR = 'UPSTART_TEST_CHROOT_PATH'

    def test_chroot_session_reexec(self):
        chroot_path = os.environ.get(self.CHROOT_ENVVAR, None)

        if not chroot_path:
            raise unittest.SkipTest('{} variable not set'.format(self.CHROOT_ENVVAR))

        # Ensure the chroot exists
        self.assertTrue(os.path.exists(chroot_path))

        # Ensure Upstart is installed in the chroot
        chroot_initctl = '{}{}{}'.format(chroot_path, os.sep, get_initctl())
        self.assertTrue(os.path.exists(chroot_initctl))

        # No sessions should exist before the test starts
        self.assertFalse(self.upstart.sessions_exist())

        # Create an Upstart chroot session by talking from the chroot
        # back to PID 1.
        ret = subprocess.call(['chroot', chroot_path, get_initctl(), 'list'])
        self.assertEqual(0, ret)

        # Ensure a session now exists
        self.assertTrue(self.upstart.sessions_exist())

        # Restart
        self.upstart.reexec()

        # Ensure Upstart responds
        self.upstart.polling_connect(force=True)
        self.assertTrue(self.upstart.version())

def main():
    kwargs = {}
    format =             \
        '%(asctime)s:'   \
        '%(filename)s:'  \
        '%(name)s:'      \
        '%(funcName)s:'  \
        '%(levelname)s:' \
        '%(message)s'

    kwargs['format'] = format

    # We want to see what's happening
    kwargs['level'] = logging.DEBUG

    logging.basicConfig(**kwargs)

    unittest.main(
        testRunner=unittest.TextTestRunner(
            stream=sys.stdout,
            verbosity=2
        )
    )

    sys.exit(0)

if __name__ == '__main__':
    main()
