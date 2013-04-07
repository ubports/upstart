#!/usr/bin/python3
#---------------------------------------------------------------------
# = Limitations =
#
# - Override files are not currently supported.
#
#   Note that you can make use of Upstart.get_test_dir() to determine
#   where to create the override but be aware that after creating a
#   '.override' file, you must wait until Upstart has re-parsed the job.
#
#---------------------------------------------------------------------

"""
Upstart test module.
"""

import os
import sys
import string
import logging
import pwd
import tempfile
import pyinotify
import subprocess
import shutil
import dbus
import dbus.service
import dbus.mainloop.glib
import time
import unittest
from gi.repository import GLib


VERSION  = '0.1'
NAME     = 'TestUpstart'

UPSTART     = '/sbin/init'
INITCTL     = '/sbin/initctl'

UPSTART_SESSION_ENV = 'UPSTART_SESSION'

INIT_SOCKET = 'unix:abstract=/com/ubuntu/upstart'

SYSTEM_JOB_DIR = '/etc/init'
SYSTEM_LOG_DIR = '/var/log/upstart'

SESSION_DIR_FMT = '/run/user/%s/upstart/sessions'

BUS_NAME                 = 'com.ubuntu.Upstart'
INTERFACE_NAME           = 'com.ubuntu.Upstart0_6'
JOB_INTERFACE_NAME       = 'com.ubuntu.Upstart0_6.Job'
INSTANCE_INTERFACE_NAME  = 'com.ubuntu.Upstart0_6.Instance'
OBJECT_PATH              = '/com/ubuntu/Upstart'
FREEDESKTOP_PROPERTIES   = 'org.freedesktop.DBus.Properties'

# Maximum time to wait for Upstart to detect a new job has been created
JOB_WAIT_SECS = 5

# Number of seconds to wait for session file to appear after startup of
# Session Init.
SESSION_FILE_WAIT_SECS = 5

FILE_WAIT_SECS = 5

# Default time to wait for a logfile to be created.
LOGFILE_WAIT_SECS = 5

#---------------------------------------------------------------------

def dbus_encode(str):
    """
    Simulate nih_dbus_path() which Upstart uses to convert
    a job path into one suitable for use as a D-Bus object path.

    This entails converting all non-alpha-numeric bytes in the
    string into a 3 byte string comprising an underscore ('_'),
    followed by the 2 byte lower-case hex representation of the byte
    in question. Alpha-numeric bytes are left unmolested.

    Note that in the special case of the specified string being None
    or the nul string, it is encoded as '_'.

    Example: 'hello-world' would be encoded as 'hello_2dworld' since
    '-' is 2d in hex.

    """
    if not str:
        return '_'

    hex = []
    for ch in str:
        if ch.isalpha() or ch.isdigit():
            hex.append(ch)
        else:
            hex.append("_%02x" % ord(ch))

    # convert back into a string
    return ''.join(hex)

def secs_to_milli(secs):
    """
    Convert @secs seconds to milli-seconds.
    """
    return secs * 1000

def wait_for_file(path, timeout=FILE_WAIT_SECS):
    """
    Wait for a specified file to exist.

    @path: Full path to file to wait for.

    Returns: True if file was created within @timeout seconds, else
     False.
    """
    for i in range(timeout):
        if os.path.exists(path):
            return True
        time.sleep(1)
    return False

class UpstartException(Exception):
    """
    An Upstart Exception.
    """
    pass


class Upstart():
    """
    Upstart Class.

    conf_dir: Full path to job configuration file directory.
    test_dir: Full path to directory below @conf_dir used to store
     test jobs.
    test_dir_name: Relative directory of @test_dir below @conf_dir
        (effectively '@conf_dir - @test_dir').
    log_dir: Full path to job log files directory.
    """

    def __init__(self):
        self.logger = logging.getLogger(self.__class__.__name__)
        self.jobs = []

        self.conf_dir = None
        self.test_dir = None
        self.test_dir_name = None
        self.log_dir = None

        self.socket = None
        self.connection = None

        # Set to True when a new job is created
        self.job_seen = False

        self.new_job = None
        self.mainloop = None
        self.timeout_source = None

        dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)

        self.create_dirs()

    def connect(self):
        """
        Connect to Upstart.
        """
        # Create appropriate D-Bus connection
        self.connection = dbus.connection.Connection(self.socket)
        self.remote_object = self.connection.get_object(object_path=OBJECT_PATH)
        self.proxy = dbus.Interface(self.remote_object, INTERFACE_NAME)

    def __timeout_cb(self):
        """
        Handle timeout if job not seen in a reasonable amount of time.
        """
        self.mainloop.quit()

    def __idle_create_job_cb(self, *args):
        """
        Handler to create a Job Configuration File as soon as
        the main loop starts.

        """
        name = args[0]
        body = args[1]
        self.new_job = Job(self, self.test_dir, self.test_dir_name, name, body)

        # deregister
        return False

    def __job_added_cb(self, path):
        """
        Handle the 'JobAdded(Object path)' signal.
        """

        # ignore signals that don't match the job we care about.
        if path != self.job_object_path:
            return True

        self.job_seen = True

        # remove timeout handler
        assert(self.timeout_source)
        GLib.source_remove(self.timeout_source)

        self.mainloop.quit()

        # deregister
        return False

    def set_test_dir(self):
        """
        Create a directory to hold the test jobs beneath the job
        configuration directory.
        """
        self.test_dir = tempfile.mkdtemp(prefix=NAME + '-', dir=self.conf_dir)
        self.test_dir_name = self.test_dir.replace("%s/" % self.conf_dir, '')

    def get_test_dir(self):
        return self.test_dir

    def create_dirs(self):
        """
        Create the directories required to store job configuration files
        and log job output.
        """
        for dir in (self.conf_dir, self.test_dir, self.log_dir):
            if dir and not os.path.exists(dir):
                os.makedirs(dir)

    def destroy(self):
        """
        Remove all jobs.
        """
        for job in self.jobs:
            job.destroy()

        if self.test_dir is not None:
            os.rmdir(self.test_dir)

    def emit(self, event, env=[], wait=True):
        """
        @event: Name of event to emit.
        @env: optional environment for event.
        @wait: if True, wait for event to be fully emitted
         (synchronous), else async.

        Emit event @event with optional environment @env.
        """
        self.proxy.EmitEvent(event, dbus.Array(env, 's'), wait)

    def version(self, raw=False):
        """
        Determine version of running instance of Upstart.

        @raw: if True, return full version string, else just the
        version in the form 'x.y'.

        Returns: Version as a string.

        """
        properties = dbus.Interface(self.remote_object, FREEDESKTOP_PROPERTIES)
        version_string = properties.Get(INTERFACE_NAME, 'version')
        if raw == True:
            return version_string

        return (version_string.split()[2]).strip(')')

    def reexec(self):
        """
        Request Upstart re-exec itself.
        """
        raise NotImplementedError('method must be implemented by subclass')

    def job_create(self, name, body):
        """
        Create a Job Configuration File.

        @name: Name to give the job.
        @body: String representation of configuration file, or list of
         strings.

        Strategy:

        Arranging for this method to detect that Upstart has registered
        this job in an efficient manner is tricky. The approach adopted
        is to:

        - Create a glib main loop.
        - Register a D-Bus signal handler which looks for the 'JobAdded'
          signal emitted by Upstart when a new job is available (in
          other words when Upstart has parsed its job configuration file
          successfully).
        - Create a glib main loop idle handler that will be called as
          soon as the loop starts and will actually create the job.
        - Add a glib main loop timeout handler which will detect if the
          job failed to be registered.
        - Run the main loop, which performs the following steps:
          - Calls the idle handler immediately. This creates the
            job configuration file, then deregisters itself so it is
            never called again.
            - If Upstart fails to parse the job, the timeout handler gets
              called which causes the main loop to exit. job_seen will not
              be set.
            - If Upstart does parse the file, the __job_added_cb()
              callback gets called as a result of the 'JobAdded' D-Bus
              signal being emitted. This will set job_seen and request
              the main loop exits.
        - Check the job_seen variable and react accordingly.
        """

        # Create a new mainloop
        self.mainloop = GLib.MainLoop()

        # reset
        self.job_seen = False
        self.new_job = None

        # construct the D-Bus path for the new job
        job_path = "%s/%s" % (self.test_dir_name, name)
        self.job_object_path = "%s/%s/%s" % \
            (OBJECT_PATH, 'jobs', dbus_encode(job_path))

        self.connection.add_signal_receiver(self.__job_added_cb,
        dbus_interface=INTERFACE_NAME,
        path=OBJECT_PATH,
        signal_name='JobAdded')

        GLib.idle_add(self.__idle_create_job_cb, name, body)
        self.timeout_source = GLib.timeout_add(secs_to_milli(JOB_WAIT_SECS),
            self.__timeout_cb)
        self.mainloop.run()

        if self.job_seen == False:
            return None

        # reset
        self.job_seen = False
        self.mainloop = None

        self.jobs.append(self.new_job)

        return self.new_job


class Job():
    """
    Representation of an Upstart Job.

    This equates to the Job Configuration file and details of the
    running instances.

    For single-instance jobs (those that do not specify the 'instance'
    stanza), this object is sufficient to control the job.

    For multi-instance jobs, manipulating this object will operate on
    *all* the instances where it makes sense to do so. If this is not
    desired behaviour, capture the JobInstance() returned from
    start() and operate on that instead.

    """

    def __init__(self, upstart, dir_name, subdir_name, job_name, body):
        """
        @upstart: Upstart() parent object.
        @dir_name: Full path to job configuration files directory.
        @subdir_name: Relative directory of test_dir below job
         configuration file directory.
        @job_name: Name of job.
        @body: Contents of job configuration file (either a string, or a
         list of strings).
        """

        self.logger = logging.getLogger(self.__class__.__name__)

        self.instances = []
        self.instance_names = []

        self.upstart = upstart
        self.subdir_name = subdir_name
        self.name = job_name
        self.job_dir = dir_name
        self.body = body

        self.instance_name = None

        # proxy to job instance
        self.instance = None

        self.properties = None

        self.conffile = self.job_dir + os.sep + self.name + '.conf'

        fh = open(self.conffile, 'w')

        if isinstance(body, list):
            for line in body:
                fh.write("%s\n" % line.rstrip())
        else:
            fh.write(body)

        fh.write("\n")
        fh.close()
        self.valid = True

        subdir_object_path = dbus_encode("%s/%s" % (self.subdir_name, self.name))
        self.object_path = "%s/%s/%s" % (OBJECT_PATH, 'jobs', subdir_object_path)

        self.remote_object = \
            self.upstart.connection.get_object(BUS_NAME, self.object_path)
        self.interface = dbus.Interface(self.remote_object, JOB_INTERFACE_NAME)

    def destroy(self):
        """
        Stop all instances and cleanup.
        """
        try:
            for instance in self.instances:
                instance.destroy()

            os.remove(self.conffile)
        except:
            pass
        finally:
            self.valid = False

    def start(self, env=[], wait=True):
        """
        Start the job. For multi-instance jobs (those that specify the
        'instance' stanza), you will need to use the returned
        JobInstance object to manipulate the individual instance.

        Returns: JobInstance.
        """

        instance_path = self.interface.Start(dbus.Array(env, 's'), wait)
        instance_name = instance_path.replace("%s/" % self.object_path, '')

        # store the D-Bus encoded instance name ('_' for single-instance jobs)
        self.instance_names.append(instance_name)

        instance = JobInstance(self, instance_name, instance_path)
        self.instances.append(instance)
        return instance

    def _get_instance(self, name=None):
        """
        Retrieve job instance and its properties.

        @name: D-Bus encoded instance name.

        """

        object_path = '%s/%s' % (self.object_path, name)

        remote_object = self.upstart.connection.get_object(BUS_NAME, object_path)

        return dbus.Interface(remote_object, INSTANCE_INTERFACE_NAME)

    def stop(self, wait=True):
        """
        Stop all running instance of the job.

        @wait: if False, stop job instances asynchronously.
        """

        for name in self.instance_names:
            instance = self._get_instance(name)
            try:
                instance.Stop(wait)
            except dbus.exceptions.DBusException:
                # job has already stopped
                pass

    def restart(self, wait=True):
        """
        Restart all running instance of the job.

        @wait: if False, stop job instances asynchronously.
        """
        for name in self.instance_names:
            instance = self._get_instance(name)
            instance.Restart(wait)

    def instance_object_paths(self):
        """
        Returns a list of instance object paths.
        """
        return ["%s/%s" % (self.object_path, instance) \
            for instance in self.instance_names]

    def instances(self, name):
        """
        Return list of instances.
        """
        instance_list = []

        for instance in self.instance_names:
            remote_object = \
            self.job.upstart.connection.get_object(BUS_NAME, self.object_path)
            instance = dbus.Interface(self.remote_object, INSTANCE_INTERFACE_NAME)
            instance_list.append(instance)

        return instance_list

    def pids(self, name=None):
        """
        @name: D-Bus encoded instance name.

        Returns: Map of job processes:
            name=job process name
            value=pid

        Notes: If your job has multiple instances, call the method of
        the same name on the individual instance objects.
        """

        name = name or '_'

        if len(self.instance_names) > 1:
            raise UpstartException('Cannot handle multiple instances')

        assert(name in self.instance_names)

        instance = self._get_instance(name)

        properties = dbus.Interface(instance, FREEDESKTOP_PROPERTIES)
        procs = properties.Get(INSTANCE_INTERFACE_NAME, 'processes')

        pid_map = {}

        for proc in procs:
            # convert back to natural types
            job_proc = str(proc[0])
            job_pid = int(proc[1])

            pid_map[job_proc] = job_pid

        return pid_map

    def running(self, name):
        """
        @name: D-Bus encoded name of job instance.

        Determine if an instance is currently running.

        Returns: True if @name is running, else false.
        """
        if len(self.instance_names) > 1:
            raise UpstartException('Cannot handle multiple instances')

        if name not in self.instance_names:
            return False

        return True if len(self.pids(name)) > 0 else False

    def logfile_name(self, instance_name):
        """
        Determine full path to logfile for job instance.

        @instance_name: D-Bus encoded job instance name.

        Note: it is up to the caller to ensure the logfile exists.

        Returns: full path to logfile.
        """

        if instance_name != '_':
            filename = "%s_%s-%s.log" % \
                (self.subdir_name, self.name, instance_name)
        else:
            # Note the underscore that Upstart auto-maps from the subdirectory
            # slash (see init(5)).
            filename = "%s_%s.log" % (self.subdir_name, self.name)

        logfile = "%s/%s" % (self.upstart.log_dir, filename)

        return logfile


class LogFile():
    """
    Representation of an Upstart job logfile.
    """

    def __init__(self, path):
        """
        @path: full path to logfile.
        """
        self.logger = logging.getLogger(self.__class__.__name__)
        self.path = path
        self.valid = True

    def destroy(self):
        """
        Clean up: *MUST* be called by caller!
        """
        try:
            os.remove(self.path)
        except FileNotFoundError:
            pass
        finally:
            self.valid = False

    def exists(self):
        """
        Determine if logfile exists.

        Returns: True or False.
        """
        return os.path.exists(self.path)

    def _get_lines(self):
        """
        Get contents of log.

        Notes: '\r' characters added by pty() are removed by
        readlines().

        Returns: List of lines in logfile.

        """

        assert(self.path)

        lines = []

        with open(self.path) as fh:
            lines = fh.readlines()
        return lines

    def readlines(self, timeout=LOGFILE_WAIT_SECS):
        """
        Read logfile. A timeout has to be used to avoid "hanging" since the job
        associated with this logfile:

        - may not create any output.
        - may produce output only after a long period of time.

        @timeout: seconds to wait file to be created.

        Notes:
          - Raises an UpstartException() on timeout.
          - '\r' characters added by pty() will be removed.

        Returns: Array of lines in logfile or None if logfile was not
        created in @timeout seconds.

        """

        self.timeout = timeout

        # Polling - ugh. However, arranging for an inotify watch
        # at this level is tricky since the watch cannot be created here
        # as by the time the watch is in place, the logfile may already
        # have been created, which not only nullifies the reason for
        # adding the watch in the first place, but also results in an
        # impotent watch (unless a timeout on the watch is specified).
        # The correct place to create the watch is in one of the Upstart
        # classes. However, even then, timing issues result wrt to
        # calling the appropriate inotify APIs.
        #
        # Hence, altough polling is gross it has the advantage of
        # simplicity and reliability in this instance.
        for i in range(self.timeout):
            if self.exists():
                return self._get_lines()
            time.sleep(1)

        return None


class JobInstance():
    """
    Representation of a running Upstart Job Instance.
    """

    def __init__(self, job, instance_name, object_path):
        """
        @instance_name: D-Bus encoded instance name.
        @object_path: D-Bus object path.
        """
        self.logger = logging.getLogger(self.__class__.__name__)
        self.job = job
        self.instance_name = instance_name
        self.object_path = object_path
        self.remote_object = \
            self.job.upstart.connection.get_object(BUS_NAME, self.object_path)
        self.instance = dbus.Interface(self.remote_object, INSTANCE_INTERFACE_NAME)
        self.properties = dbus.Interface(self.instance, FREEDESKTOP_PROPERTIES)

        # all jobs are expected to be created in a subdirectory.
        assert(self.job.subdir_name)

        logfile = job.logfile_name(instance_name)
        self.logfile = LogFile(logfile)

    def stop(self, wait=True):
        """
        Stop instance.

        @wait: if True, wait for job, else perform operation
         asynchronously.
        """
        try:
            self.instance.Stop(wait)
        except dbus.exceptions.DBusException:
            # job has already stopped
            pass

    def restart(self, wait=True):
        """
        Restart instance.

        @wait: if True, wait for job, else perform operation
         asynchronously.
        """
        self.instance.Restart(wait)

    def pids(self):
        procs = self.properties.Get(INSTANCE_INTERFACE_NAME, 'processes')

        pid_map = {}

        for proc in procs:
            # convert back to natural types
            job_proc = str(proc[0])
            job_pid = int(proc[1])

            pid_map[job_proc] = job_pid

        return pid_map

    def destroy(self):
        """
        Stop the instance and cleanup.
        """
        self.stop()
        self.logfile.destroy()


class SystemInit(Upstart):

    def __init__(self):
        super().__init__()
        self.logger = logging.getLogger(self.__class__.__name__)
        self.socket = INIT_SOCKET
        self.conf_dir = SYSTEM_JOB_DIR
        self.log_dir = SYSTEM_LOG_DIR
        self.set_test_dir()
        self.connect()

    def destroy(self):
        super().destroy()

    def reexec(self):
        os.system('telinit u')


class SessionInit(Upstart):
    """
    Create a new Upstart Session or join an existing one.
    """

    timeout = SESSION_FILE_WAIT_SECS

    class InotifyHandler(pyinotify.ProcessEvent):

        # We don't actually do anything here since all we care
        # about is whether we timed-out.
        def process_IN_CREATE(self, event):
            pass

    def _get_sessions(self):
        """
        Obtain a list of running sessions.

        Returns: Map of sessions:
            name=pid
            value=socket address
        """

        sessions = {}

        args = [INITCTL, 'list-sessions']

        proc = subprocess.Popen(args, stdout=subprocess.PIPE)

        for byte_line in proc.stdout:
            line = byte_line.decode('utf-8').splitlines()[0]
            pid, socket = line.split()

            sessions[pid] = socket

        proc.wait()
        return sessions

    def __init__(self, join=False, capture=None, extra=None):
        """
        @join: If False, start a new session. If TRUE join the existing
         main (non-test) session.
        @capture: Set to the name of a file to capture all stdout and
         stderr output.
        @extra: Array of extra arguments (only used when @join is False).

        Notes:
        - If @join is True, an UpstartException is raised if either
        no existing session is found, or multiple existing sessions are
        found.
        - Joining implies joining the main Upstart session, not a
        test session.

        """
        super().__init__()

        self.logger = logging.getLogger(self.__class__.__name__)

        self.join = join
        self.capture = capture

        self.socket = os.environ.get(UPSTART_SESSION_ENV)

        sessions = self._get_sessions()
        if self.join and len(sessions) > 1:
            raise UpstartException('Multiple existing sessions')

        if self.join and not self.socket:
            raise UpstartException('No existing session')

        if self.join:
            self.session = self.socket

            # We are joining the main desktop session.
            #
            # Multiple conf file directories are supported, but we'll
            # stick with the default.
            config_home = os.environ.get('XDG_CONFIG_HOME', "%s/%s" \
                % (os.environ.get('HOME'), '.config'))
            cache_home = os.environ.get('XDG_CACHE_HOME', "%s/%s" \
                % (os.environ.get('HOME'), '.cache'))

            self.conf_dir = "%s/%s" % (config_home, 'upstart')
            self.log_dir = "%s/%s" % (cache_home, 'upstart')
        else:
            args = []

            pid = os.getpid()

            self.conf_dir = tempfile.mkdtemp(prefix="%s-confdir-%d-" % (NAME, pid))
            self.log_dir = tempfile.mkdtemp(prefix="%s-logdir-%d-" % (NAME, pid))

            args.append(UPSTART)
            args.append('--user')
            args.append('--confdir')
            args.append(self.conf_dir)
            args.append('--logdir')
            args.append(self.log_dir)

            if extra:
                args.extend(extra)

            self.logger.debug('Starting Session Init with arguments: %s' % " ".join(args))

            watch_manager = pyinotify.WatchManager()
            mask = pyinotify.IN_CREATE
            notifier = pyinotify.Notifier(watch_manager, SessionInit.InotifyHandler())
            user = pwd.getpwuid(os.geteuid())[0]
            watch = watch_manager.add_watch(SESSION_DIR_FMT % user, mask)

            notifier.process_events()

            if self.capture:
                self.out = open(self.capture, 'w')
            else:
                self.out = subprocess.DEVNULL

            self.proc = subprocess.Popen(args=args, stdout=self.out, stderr=self.out)

            self.pid = self.proc.pid

            self.logger.debug('Session Init running with pid %d' % self.pid)

            if not notifier.check_events(timeout=(secs_to_milli(self.timeout))):
                msg = \
                    "Timed-out waiting for session file after %d seconds" \
                    % self.timeout
                raise UpstartException(msg)

            # consume
            notifier.read_events()
            notifier.stop()

            if self.capture:
                self.out.flush()

            # Activity has been seen in the session file directory, so
            # our Session Init should now have written the session file
            # (although this assumes no other Session Inits are being
            # started).
            sessions = self._get_sessions()

            if not str(self.pid) in sessions.keys():
                msg = "Session with pid %d not found" % self.pid
                raise UpstartException(msg)

            self.socket = sessions[str(self.pid)]
            self.logger.debug("Created Upstart Session '%s'" % self.socket)
            os.putenv(UPSTART_SESSION_ENV, self.socket)

        self.set_test_dir()
        self.connect()

    def destroy(self):
        """
        Stop the SessionInit (if session was not joined) and cleanup.
        """
        super().destroy()
        if self.capture:
            self.out.close()
        if not self.join:
            self.logger.debug('Stopping Session Init running with pid %d' % self.pid)
            self.proc.terminate()
            self.proc.wait()
            os.rmdir(self.log_dir)
            shutil.rmtree(self.conf_dir)
            os.unsetenv(UPSTART_SESSION_ENV)

    def reexec(self):
        self.proxy.Restart()


class TestUpstart(unittest.TestCase):

    FILE_BRIDGE_CONF = \
        '/usr/share/upstart/sessions/upstart-file-bridge.conf'

    REEXEC_CONF = \
        '/usr/share/upstart/sessions/re-exec.conf'

    PSCMD_FMT = 'ps --no-headers -p %d -o comm,args'

    def setUp(self):
        self.upstart = None

        self.logger = logging.getLogger(self.__class__.__name__)
        for cmd in UPSTART, INITCTL:
            if not os.path.exists(cmd):
                raise UpstartException('Command %s not found' % cmd)

    def start_session_init(self):
        """
        Start a Session Init.
        """
        self.assertFalse(self.upstart)
        self.upstart = SessionInit(extra=['--no-startup-event', '--debug'], capture='/tmp/upstart.log')
        self.assertTrue(self.upstart)

        # check it's running
        os.kill(self.upstart.pid, 0)

        # checks it responds
        self.assertTrue(self.upstart.version())

    def stop_session_init(self):
        """
        Stop a Session Init.
        """
        self.assertTrue(self.upstart)

        pid = self.upstart.pid

        # check it's running
        os.kill(pid, 0)

        self.upstart.destroy()
        with self.assertRaises(ProcessLookupError):
            os.kill(pid, 0)

        self.upstart = None

    def test_init_start_file_bridge(self):
        self.start_session_init()

        # create the file-bridge job in the correct test location by copying
        # the system-provided session job.
        lines = []
        with open(self.FILE_BRIDGE_CONF, 'r') as f:
            lines.extend(f.readlines())
        file_bridge = self.upstart.job_create('upstart-file-bridge', lines)
        self.assertTrue(file_bridge)
        file_bridge.start()

        pids = file_bridge.pids()
        self.assertTrue(len(pids.keys()) == 1)
        for proc, pid in pids.items():
            self.assertEqual(proc, 'main')
            self.assertTrue(isinstance(pid, int))
            os.kill(pid, 0)

        # create a job that makes use ofthe file event to watch to a
        # file in a newly-created directory.
        dir = tempfile.mkdtemp()
        file = dir + os.sep + 'foo'

        msg = 'got file %s' % file
        lines = []
        lines.append('start on file FILE=%s EVENT=create' % file)
        lines.append('exec echo %s' % msg)
        create_job = self.upstart.job_create('wait-for-file-creation', lines)
        self.assertTrue(create_job)

        # create empty file
        open(file, 'w').close()

        # create another job that triggers when the same file is deleted
        lines = []
        lines.append('start on file FILE=%s EVENT=delete' % file)
        lines.append('exec echo %s' % msg)
        delete_job = self.upstart.job_create('wait-for-file-deletion', lines)
        self.assertTrue(delete_job)

        # No need to start the jobs of course as the file-bridge handles that!

        # Identify full path to job logfiles
        create_job_logfile = create_job.logfile_name(dbus_encode(''))
        assert(create_job_logfile)

        delete_job_logfile = delete_job.logfile_name(dbus_encode(''))
        assert(delete_job_logfile)

        # wait for the create job to run and produce output
        self.assertTrue(wait_for_file(create_job_logfile))

        # check the output
        lines = []
        with open(create_job_logfile, 'r') as f:
            lines.extend(f.readlines())
        self.assertTrue(len(lines) == 1)
        self.assertEqual(msg, lines[0].rstrip())

        os.remove(file)

        # wait for the delete job to run and produce output
        self.assertTrue(wait_for_file(delete_job_logfile))

        # check the output
        lines = []
        with open(delete_job_logfile, 'r') as f:
            lines.extend(f.readlines())
        self.assertTrue(len(lines) == 1)
        self.assertEqual(msg, lines[0].rstrip())

        os.remove(create_job_logfile)
        os.remove(delete_job_logfile)

        file_bridge.stop()
        self.stop_session_init()

    def test_session_init_reexec(self):
        self.start_session_init()

        self.assertTrue(self.upstart.pid)

        cmd = self.PSCMD_FMT % self.upstart.pid

        output = subprocess.getoutput(cmd)

        # ensure no stateful-reexec already performed.
        self.assertFalse(re.search('state-fd', output))

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
        fd = result.group(1)

        self.stop_session_init()

    def test_session_init_reexec_when_pid1_does(self):

        timeout = 5

        self.start_session_init()

        self.assertTrue(self.upstart.pid)

        # create the REEXEC_CONF job in the correct test location by copying
        # the system-provided session job.
        lines = []
        with open(self.REEXEC_CONF, 'r') as f:
            lines.extend(f.readlines())
        reexec_job = self.upstart.job_create('re-exec', lines)
        self.assertTrue(reexec_job)

        cmd = self.PSCMD_FMT % self.upstart.pid

        output = subprocess.getoutput(cmd)

        # ensure no stateful-reexec already performed.
        self.assertFalse(re.search('state-fd', output))

        # Simulate a PID 1 restart event.
        self.upstart.emit(':sys:restarted')

        # wait for a reasonable period of time for the stateful re-exec
        # to occur.
        got = False
        for i in range(timeout):
            output = subprocess.getoutput(cmd)
            result = re.search('--state-fd\s+(\d+)', output)
            if result:
                got = True
                break
            time.sleep(1)

        self.assertTrue(got)

        self.stop_session_init()

    def test_session_init(self):
        self.start_session_init()
        job = self.upstart.job_create('zebra', 'exec sleep 999')
        self.assertTrue(job)

        inst = job.start()
        pids = job.pids()
        self.assertTrue(len(pids.keys()) == 1)
        for key, value in pids.items():
            self.assertEqual(key, 'main')
            self.assertTrue(isinstance(value, int))

        # expected since there is only a single instance of the job
        self.assertDictEqual(inst.pids(), pids)

        inst.stop()
        self.stop_session_init()

    def tearDown(self):
        pass


if __name__ == '__main__':
    import re

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
    unittest.main(verbosity=2)
    sys.exit(0)
