#!/usr/bin/python3
# -*- coding: utf-8 -*-
#---------------------------------------------------------------------
# Copyright  2013 Canonical Ltd.
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
# Description: Session-level Upstart tests for the pyupstart module.
#
# Notes: Should be run both as a non-privileged user and then again
#        as the root user; in both cases, an Upstart user session
#        will be created.
#---------------------------------------------------------------------

import os
import sys
import re
import json

base_dir = os.path.abspath(os.path.dirname(__file__))
module_dir = os.path.normpath(os.path.realpath(base_dir + os.sep + '..'))

# top-level unpacked source directory
top_srcdir = os.path.normpath(os.path.realpath(module_dir + os.sep + '..'))

bridge_session_conf_dir = top_srcdir + os.sep + 'extra/conf-session'

# Tell Python where the uninstalled module lives in the source tree
sys.path.append(module_dir)
from pyupstart import *

import unittest

class TestSessionUpstart(unittest.TestCase):

    FILE_BRIDGE_CONF = 'upstart-file-bridge.conf'
    REEXEC_CONF = 're-exec.conf'

    PSCMD_FMT = 'ps --no-headers -p %d -o pid,comm,args'

    def setUp(self):

        # If this test is run as root, chances are that user won't have
        # an XDG_RUNTIME_DIR, so create a temporary one and set the
        # variable since this is required by the Session Init.
        xdg_runtime_dir = os.environ.get('XDG_RUNTIME_DIR', None)
        if not xdg_runtime_dir or not os.path.exists(xdg_runtime_dir):
            tmp_xdg_runtime_dir = tempfile.mkdtemp(prefix='tmp-xdg-runtime-dir')
            os.environ['XDG_RUNTIME_DIR'] = tmp_xdg_runtime_dir
            print('INFO: User has no XDG_RUNTIME_DIR so created one: {}'.format(tmp_xdg_runtime_dir))


        self.file_bridge_conf = '{}{}{}'.format(bridge_session_conf_dir, os.sep, self.FILE_BRIDGE_CONF)
        self.reexec_conf = '{}{}{}'.format(bridge_session_conf_dir, os.sep, self.REEXEC_CONF)

        # Prefer to use the installed job files if available and the
        # appropriate environment variable is set since they are going
        # to be more current and appropriate for the environment under
        # test.
        if os.environ.get('UPSTART_TEST_USE_INSTALLED_CONF', None):
            tmp = '{}{}{}'.format(DEFAULT_SESSION_INSTALL_PATH, os.sep, self.FILE_BRIDGE_CONF)
            if os.path.exists(tmp):
                print('INFO: UPSTART_TEST_USE_INSTALLED_CONF set - using {} rather than {}'.format(tmp, self.file_bridge_conf))
                self.file_bridge_conf = tmp

            tmp = '{}{}{}'.format(DEFAULT_SESSION_INSTALL_PATH, os.sep, self.REEXEC_CONF)
            if os.path.exists(tmp):
                print('INFO: UPSTART_TEST_USE_INSTALLED_CONF set - using {} rather than {}'.format(tmp, self.reexec_conf))
                reexec_conf = tmp

        self.assertTrue(os.path.exists(self.file_bridge_conf))
        self.assertTrue(os.path.exists(self.reexec_conf))

        self.upstart = None

        self.logger = logging.getLogger(self.__class__.__name__)
        for cmd in get_init(), get_initctl():
            if not os.path.exists(cmd):
                raise UpstartException('Command %s not found' % cmd)

    def tearDown(self):
        # Ensure no state file exists
        state_file = '{}{}{}'.format(self.log_dir, os.sep, UPSTART_STATE_FILE)
        self.assertFalse(os.path.exists(state_file))

    def start_session_init(self):
        """
        Start a Session Init.
        """
        self.assertFalse(self.upstart)

        extra_args = ['--no-startup-event', '--debug']
        self.upstart = \
            SessionInit(extra=extra_args, capture=DEFAULT_LOGFILE)
        self.assertTrue(self.upstart)

        # save log location to allow it to be checked for state files at
        # the end of the tests (after the Upstart objects have been
        # destroyed).
        self.log_dir = self.upstart.log_dir

        # Check it's running
        os.kill(self.upstart.pid, 0)

        # Checks it responds
        self.assertTrue(self.upstart.version())

    def stop_session_init(self):
        """
        Stop a Session Init.
        """
        self.assertTrue(self.upstart)

        pid = self.upstart.pid

        # Check it's running
        os.kill(pid, 0)

        self.upstart.destroy()
        self.assertRaises(ProcessLookupError, os.kill, pid, 0)

        self.upstart = None

class TestFileBridge(TestSessionUpstart):

    def test_init_start_file_bridge(self):
        self.start_session_init()

        # Create upstart-file-bridge.conf
        #
        # Note that we do not use the bundled user job due to our
        # requirement for a different start condition and different
        # command options.
        cmd = '{} --daemon --user --debug'.format(get_file_bridge())
        lines = """
        start on startup
        stop on session-end

        emits file

        expect daemon
        respawn
        exec {}
        """.format(cmd)

        file_bridge = self.upstart.job_create('upstart-file-bridge', lines)
        self.assertTrue(file_bridge)
        file_bridge.start()

        pids = file_bridge.pids()

        self.assertEqual(len(pids.keys()), 1)

        for proc, pid in pids.items():
            self.assertEqual(proc, 'main')
            self.assertIsInstance(pid, int)
            os.kill(pid, 0)

        target_dir = tempfile.mkdtemp()
        file = target_dir + os.sep + 'foo'
        dir = target_dir + os.sep + 'bar'

        # Create a job that makes use of the file event to watch a
        # file in a newly-created directory.
        file_msg = 'got file %s' % file
        lines = []
        lines.append('start on file FILE=%s EVENT=create' % file)
        lines.append('exec echo %s' % file_msg)
        create_file_job = self.upstart.job_create('wait-for-file-creation', lines)
        self.assertTrue(create_file_job)

        # Create job that waits for a file modification
        lines = []
        lines.append('start on file FILE=%s EVENT=modify' % file)
        lines.append('exec echo %s' % file_msg)
        modify_file_job = self.upstart.job_create('wait-for-file-modify', lines)
        self.assertTrue(modify_file_job)

        # Create another job that triggers when the same file is deleted
        lines = []
        lines.append('start on file FILE=%s EVENT=delete' % file)
        lines.append('exec echo %s' % file_msg)
        delete_file_job = self.upstart.job_create('wait-for-file-deletion', lines)
        self.assertTrue(delete_file_job)

        # Create job that triggers on directory creation
        dir_msg = 'got directory %s' % dir
        lines = []
        # XXX: note the trailing slash to force a directory watch
        lines.append('start on file FILE=%s/ EVENT=create' % dir)
        lines.append('exec echo %s' % dir_msg)
        create_dir_job = self.upstart.job_create('wait-for-dir-creation', lines)
        self.assertTrue(create_dir_job)

        # Create job that triggers on directory modification
        lines = []
        # XXX: note the trailing slash to force a directory watch
        lines.append('start on file FILE=%s/ EVENT=modify' % dir)
        lines.append('exec echo %s' % dir_msg)
        modify_dir_job = self.upstart.job_create('wait-for-dir-modify', lines)
        self.assertTrue(modify_dir_job)

        # Create job that triggers on directory deletion
        lines = []
        # XXX: note the trailing slash to force a directory watch
        lines.append('start on file FILE=%s/ EVENT=delete' % dir)
        lines.append('exec echo %s' % dir_msg)
        delete_dir_job = self.upstart.job_create('wait-for-dir-delete', lines)
        self.assertTrue(delete_dir_job)

        # Create empty file
        open(file, 'w').close()

        # Create directory
        os.mkdir(dir)

        # No need to start the jobs of course as the file-bridge handles that!

        # Identify full path to job logfiles
        create_file_job_logfile = create_file_job.logfile_name(dbus_encode(''))
        self.assertTrue(create_file_job_logfile)

        modify_file_job_logfile = modify_file_job.logfile_name(dbus_encode(''))
        self.assertTrue(modify_file_job_logfile)

        delete_file_job_logfile = delete_file_job.logfile_name(dbus_encode(''))
        self.assertTrue(delete_file_job_logfile)

        create_dir_job_logfile = create_dir_job.logfile_name(dbus_encode(''))
        self.assertTrue(create_dir_job_logfile)

        modify_dir_job_logfile = modify_dir_job.logfile_name(dbus_encode(''))
        self.assertTrue(modify_dir_job_logfile)

        delete_dir_job_logfile = delete_dir_job.logfile_name(dbus_encode(''))
        self.assertTrue(delete_dir_job_logfile)

        #--------------------

        # Wait for the create file job to run and produce output
        self.assertTrue(wait_for_file(create_file_job_logfile))

        # Check the output
        with open(create_file_job_logfile, 'r', encoding='utf-8') as f:
            lines = f.readlines()
        self.assertTrue(len(lines) == 1)
        self.assertEqual(file_msg, lines[0].rstrip())

        #--------------------

        # Wait for the create directory job to run and produce output
        self.assertTrue(wait_for_file(create_dir_job_logfile))

        # Check the output
        with open(create_dir_job_logfile, 'r', encoding='utf-8') as f:
            lines = f.readlines()
        self.assertTrue(len(lines) == 1)
        self.assertEqual(dir_msg, lines[0].rstrip())

        #--------------------

        # Modify the file
        open(file, 'w').close()

        # Wait for the create file job to run and produce output
        self.assertTrue(wait_for_file(modify_file_job_logfile))

        # Check the output
        with open(modify_file_job_logfile, 'r', encoding='utf-8') as f:
            lines = f.readlines()
        self.assertTrue(len(lines) == 1)
        self.assertEqual(file_msg, lines[0].rstrip())

        #--------------------
        # Modify the directory by creating a new file in it.

        dir_file = dir + os.sep + 'baz'
        open(dir_file, 'w').close()

        # Wait for the modify directory job to run and produce output
        self.assertTrue(wait_for_file(modify_dir_job_logfile))

        # Check the output
        with open(modify_dir_job_logfile, 'r', encoding='utf-8') as f:
            lines = f.readlines()
        self.assertTrue(len(lines) == 1)
        self.assertEqual(dir_msg, lines[0].rstrip())

        #--------------------

        os.remove(dir_file)
        os.rmdir(dir)

        # Wait for the delete directory job to run and produce output
        self.assertTrue(wait_for_file(delete_dir_job_logfile))

        # Check the output
        with open(delete_dir_job_logfile, 'r', encoding='utf-8') as f:
            lines = f.readlines()
        self.assertTrue(len(lines) == 1)
        self.assertEqual(dir_msg, lines[0].rstrip())
        #--------------------

        shutil.rmtree(target_dir)

        # Wait for the delete job to run and produce output
        self.assertTrue(wait_for_file(delete_file_job_logfile))

        # Check the output
        with open(delete_file_job_logfile, 'r', encoding='utf-8') as f:
            lines = f.readlines()
        self.assertTrue(len(lines) == 1)
        self.assertEqual(file_msg, lines[0].rstrip())

        #--------------------

        os.remove(create_file_job_logfile)
        os.remove(modify_file_job_logfile)
        os.remove(delete_file_job_logfile)
        os.remove(create_dir_job_logfile)
        os.remove(modify_dir_job_logfile)
        os.remove(delete_dir_job_logfile)

        file_bridge.stop()
        self.stop_session_init()

class TestSessionInitReExec(TestSessionUpstart):

    def test_session_init_reexec(self):
        self.start_session_init()

        self.assertTrue(self.upstart.pid)

        cmd = self.PSCMD_FMT % self.upstart.pid

        output = subprocess.getoutput(cmd)

        # Ensure no stateful-reexec already performed.
        self.assertFalse(re.search('state-fd', output))

        version = self.upstart.version()
        self.assertTrue(version)

        # Create an invalid job to ensure this causes no problems for
        # the re-exec. Note that we cannot use job_create() since
        # that validates the syntax of the .conf file).
        #
        # We create this file before any other to allow time for Upstart
        # to _attempt to parse it_ by the time the re-exec is initiated.
        invalid_conf = "{}/invalid.conf".format(self.upstart.test_dir)
        with open(invalid_conf, 'w', encoding='utf-8') as fh:
            print("invalid", file=fh)

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

        # Trigger re-exec and catch the D-Bus exception resulting
        # from disconnection from Session Init when it severs client
        # connections.
        with self.assertRaises(dbus.exceptions.DBusException):
            self.upstart.reexec()

        os.kill(self.upstart.pid, 0)

        # SessionInit does not sanitise its command-line and will show
        # the fd used to read state from after a re-exec.
        output = subprocess.getoutput(cmd)
        result = re.search('--state-fd\s+(\d+)', output)
        self.assertTrue(result)

        # Upstart is now in the process of starting, but we need to
        # reconnect to it via D-Bus since it cannot yet retain
        # client connections. However, since the re-exec won't be
        # instantaneous, try a few times.
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

        os.remove(invalid_conf)

        self.stop_session_init()

    def test_session_init_reexec_when_pid1_does(self):

        timeout = 5

        self.start_session_init()

        self.assertTrue(self.upstart.pid)

        # Create the REEXEC_CONF job in the correct test location by copying
        # the system-provided session job.
        with open(self.reexec_conf, 'r', encoding='utf-8') as f:
            lines = f.readlines()
        reexec_job = self.upstart.job_create('re-exec', lines)
        self.assertTrue(reexec_job)

        cmd = self.PSCMD_FMT % self.upstart.pid

        output = subprocess.getoutput(cmd)

        # Ensure no stateful-reexec already performed.
        self.assertFalse(re.search('state-fd', output))

        # Simulate a PID 1 restart event.
        self.upstart.emit(':sys:restarted')

        # Wait for a reasonable period of time for the stateful re-exec
        # to occur.
        until = datetime.now() + timedelta(seconds=timeout)

        while datetime.now() < until:
            output = subprocess.getoutput(cmd)
            result = re.search('--state-fd\s+(\d+)', output)
            if result:
                break
            time.sleep(0.1)
        else:
            raise AssertionError('Failed to detect re-exec')

        # Upstart is now in the process of starting, but we need to
        # reconnect to it via D-Bus since it cannot yet retain
        # client connections. However, since the re-exec won't be
        # instantaneous, try a few times.
        self.upstart.polling_connect(force=True)

        # check that we can still operate on the re-exec'd Upstart
        self.assertTrue(self.upstart.version())

        self.stop_session_init()

class TestSessionInit(TestSessionUpstart):

    def test_session_init(self):
        self.start_session_init()
        job = self.upstart.job_create('zebra', 'exec sleep 999')
        self.assertTrue(job)

        inst = job.start()
        pids = job.pids()
        self.assertEqual(len(pids), 1)

        for key, value in pids.items():
            self.assertEqual(key, 'main')
            self.assertTrue(isinstance(value, int))

        # expected since there is only a single instance of the job
        self.assertEqual(inst.pids(), pids)

        inst.stop()
        self.stop_session_init()

class TestState(TestSessionUpstart):

    def test_state(self):
        """
        Create a job and perform some basics checks on Upstarts internal
        state for that job.
        """
        self.start_session_init()
        job = self.upstart.job_create('foo', 'exec sleep 666')
        self.assertTrue(job)

        state = self.upstart.get_state()

        conf_sources = state['conf_sources']
        self.assertTrue(conf_sources)
        self.assertTrue(len(conf_sources) == 1)

        conf_files = conf_sources[0]['conf_files']
        self.assertTrue(conf_files)
        self.assertTrue(len(conf_files) == 1)

        conf_file = conf_files[0]
        self.assertTrue(conf_file)
        path = conf_file['path']
        self.assertTrue(path)

        full_job_path = '{}{}{}.conf'.format(job.job_dir, os.sep, job.name)

        self.assertEqual(path, full_job_path)
        self.stop_session_init()

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
