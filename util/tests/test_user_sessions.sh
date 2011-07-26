#!/bin/sh -u
#---------------------------------------------------------------------
# Script to run minimal Upstart user session tests.
#
# Note that this script _cannot_ be run as part of the "make check"
# tests since those tests stimulate functions and features of the
# as-yet-uninstalled version of Upstart. However, this script needs to
# run on a system where the version of Upstart under test has _already_
# been fully installed.
#---------------------------------------------------------------------
#
# Copyright (C) 2011 Canonical Ltd.
#
# Author: James Hunt <james.hunt@canonical.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 3 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
#---------------------------------------------------------------------

script_name=${0##*/}
user_job_dir="$HOME/.init"
bug_url="https://bugs.launchpad.net/upstart/+filebug"
test_dir=
test_dir_suffix=
user_to_create=
uid=
gid=
opt=
OPTARG=

# allow non-priv users to find 'initctl'
export PATH=$PATH:/sbin

# for assertions
die()
{
  msg="$*"
  echo "ERROR: $msg" >&2
  exit 1
}

TEST_FAILED()
{
  args="$*"

  [ -z "$args" ] && die "need args"

  printf "BAD: %s\n" "$args"
  printf "\nPlease report a bug at $bug_url including the following details:\n"
  printf "\nUpstart:\n"
  /sbin/init --version|head -n1
  /sbin/initctl --version|head -n1
  echo
  printf "cmdline:\n"
  cat /proc/cmdline
  echo
  printf "Upstart Env:\n"
  set|grep UPSTART_
  echo
  printf "lsb:\n"
  lsb_release -a
  printf "\nuname:\n"
  uname -a
  echo
  sync
  exit 1
}

TEST_GROUP()
{
  name="$1"

  [ -z "$name" ] && die "need name"

  printf "Testing %s\n", "$name"
}

TEST_FEATURE()
{
  feature="$1"

  [ -z "$feature" ] && die "need feature"

  printf "...%s\n" "$feature"
}

TEST_NE()
{
  cmd="$1"
  value="$2"
  expected="$3"

  # XXX: no checks on value or expected since they might be blank
  [ -z "$cmd" ] && die "need cmd"

  [ "$value" = "$expected" ] && TEST_FAILED \
  "wrong value for '$cmd', expected $expected got $value"
}

TEST_EQ()
{
  cmd="$1"
  value="$2"
  expected="$3"

  # XXX: no checks on value or expected since they might be blank
  [ -z "$cmd" ] && die "need cmd"

  [ "$value" != "$expected" ] && TEST_FAILED \
  "wrong value for '$cmd', expected '$expected' got '$value'"
}

setup()
{
  uid=$(id -u)
  gid=$(id -g)

  if [ "$uid" = 0 ]
  then
    [ -z "$user_to_create" ] && die "need '-u' option when running as root"

    getent passwd "$user_to_create" && \
      die "user '$user_to_create' already exists"

    echo "Creating user '$user_to_create'"
    cmd="useradd -mU -c 'Upstart Test User' $user_to_create"
    eval "$cmd"
    TEST_EQ "$cmd" $? 0

    echo "Locking account for user '$user_to_create'"
    cmd="usermod -L $user_to_create"
    eval "$cmd"
    TEST_EQ "$cmd" $? 0

    # Run ourselves again as the new user
    su -c "$0 -a" "$user_to_create"
    test_run_rc=$?

    if [ $test_run_rc -eq 0 ]
    then
      echo "Deleting user '$user_to_create'"
      cmd="userdel -r \"$user_to_create\""
      eval "$cmd"
      TEST_EQ "$cmd" $? 0
    fi

    exit $test_run_rc
  fi

  # setup
  if [ ! -d "$user_job_dir" ]
  then
    cmd="mkdir -p \"$user_job_dir\""
    eval $cmd
    TEST_EQ "$cmd" $? 0

    cmd="chmod 755 \"$user_job_dir\""
    eval "$cmd"
    TEST_EQ "$cmd" $? 0
  fi
  
  # create somewhere to store user jobs
  cmd="mktemp -d --tmpdir=\"$user_job_dir\""
  test_dir=$(eval "$cmd")
  TEST_EQ "$cmd" $? 0
  TEST_NE "$test_dir" "$test_dir" ""
  test_dir_suffix=${test_dir#${user_job_dir}/}

  # ensure files in this directory are accessible since
  # mktemp sets directory perms to 0700 regardless of umask.
  cmd="chmod 755 \"$test_dir\""
  eval "$cmd"
  TEST_EQ "$cmd" $? 0

  TEST_NE "HOME" "$HOME" ""
}

cleanup()
{
  if [ -d "$test_dir" ]
  then
    echo "Removing test directory '$test_dir'"
    cmd="rmdir \"$test_dir\""
    eval "$cmd"
    TEST_EQ "$cmd" $? 0
  fi
}

ensure_job_known()
{
  job="$1"
  job_name="$2"

  [ -z "$job" ] && die "no job"
  [ -z "$job_name" ] && die "no job name"

  TEST_FEATURE "ensure 'initctl' recognises job"
  initctl list|grep -q "^$job " || \
    TEST_FAILED "job $job_name not known to initctl"

  TEST_FEATURE "ensure 'status' recognises job"
  cmd="status ${job}"
  eval "$cmd"
  rc=$?
  TEST_EQ "$cmd" $rc 0
}

run_user_job_tests()
{
  job_name="$1"
  job_file="$2"
  task="$3"
  env="$4"

  # XXX: env can be empty
  [ -z "$job_name" ] && die "no job name"
  [ -z "$job_file" ] && die "no job file"
  [ -z "$task" ] && die "no task value"

  job="${test_dir_suffix}/${job_name}"

  [ -f "$job_file" ] || TEST_FAILED "job file '$job_file' does not exist"

  ensure_job_known "$job" "$job_name"

  TEST_FEATURE "ensure job can be started"
  cmd="start ${job} ${env}"
  output=$(eval "$cmd")
  rc=$?
  TEST_EQ "$cmd" $rc 0

  if [ "$task" = no ]
  then
    TEST_FEATURE "ensure 'start' shows job pid"
    pid=$(echo "$output"|awk '{print $4}')
    TEST_NE "pid" "$pid" ""
  
    TEST_FEATURE "ensure 'initctl' shows job is running with pid"
    initctl list|grep -q "^$job start/running, process $pid" || \
      TEST_FAILED "job $job_name did not start"
  
    TEST_FEATURE "ensure 'status' shows job is running with pid"
    cmd="status ${job}"
    output=$(eval "$cmd")
    echo "$output"|while read job_tmp state ignored status_pid
    do
      state=$(echo $state|tr -d ',')
      TEST_EQ "job name"  "$job_tmp" "$job"
      TEST_EQ "job state" "$state" "start/running"
      TEST_EQ "job pid"   "$status_pid" "$pid"
    done
  
    TEST_FEATURE "ensure job pid is running with correct uids"
    pid_uids=$(ps --no-headers -p $pid -o euid,ruid)
    for pid_uid in $pid_uids
    do
      TEST_EQ "pid uid" "$pid_uid" "$uid"
    done
  
    TEST_FEATURE "ensure job pid is running with correct gids"
    pid_gids=$(ps --no-headers -p $pid -o egid,rgid)
    for pid_gid in $pid_gids
    do
      TEST_EQ "pid gid" "$pid_gid" "$gid"
    done
  
    TEST_FEATURE "ensure process is running in correct directory"
    cwd=$(readlink /proc/$pid/cwd)
    TEST_EQ "cwd" "$cwd" "$HOME"
  
    TEST_FEATURE "ensure job can be stopped"
    cmd="stop ${job}"
    output=$(eval "$cmd")
    rc=$?
    TEST_EQ "$cmd" $rc 0
  
    TEST_FEATURE "ensure job pid no longer exists"
    pid_ids=$(ps --no-headers -p $pid -o euid,ruid,egid,rgid)
    TEST_EQ "pid uids+gids" "$pid_ids" ""
  fi

  remove_job_file "$job_file"
  ensure_job_gone "$job" "$job_name" "$env"
}

remove_job_file()
{
  job_file="$1"

  [ -z "$job_file" ] && die "no job file"
  [ ! -f "$job_file" ] && TEST_FAILED "job file '$job_file' does not exist"

  cmd="rm $job_file"
  eval "$cmd"
  TEST_EQ "$cmd" $? 0
}

ensure_job_gone()
{
  job="$1"
  job_name="$2"
  env="$3"

  # XXX: no check on env since it can be empty
  [ -z "$job" ] && die "no job"
  [ -z "$job_name" ] && die "no job name"

  TEST_FEATURE "ensure 'initctl' no longer recognises job"
  initctl list|grep -q "^$job " && \
    TEST_FAILED "deleted job $job_name still known to initctl"

  TEST_FEATURE "ensure 'status' no longer recognises job"
  cmd="status ${job}"
  eval "$cmd"
  rc=$?
  TEST_NE "$cmd" $rc 0
}

test_user_job()
{
  test_group="$1"
  job_name="$2"
  script="$3"
  task="$4"
  env="$5"

  # XXX: no test on script or env since they might be empty
  [ -z "$test_group" ] && die "no test group"
  [ -z "$job_name" ] && die "no job name"
  [ -z "$task" ] && die "no task"

  TEST_GROUP "$test_group"

  job_file="${test_dir}/${job_name}.conf"
  
  echo "$script" > $job_file

  run_user_job_tests "$job_name" "$job_file" "$task" "$env"
}

test_user_job_binary()
{
  group="user job running a binary"
  job_name="binary_test"
  script="exec sleep 999"
  test_user_job "$group" "$job_name" "$script" no ""
}

test_user_job_binary_task()
{
  group="user job running a binary task"
  job_name="binary_task_test"
  OUTFILE=$(mktemp)

  script="\
task
exec true > $OUTFILE"

  test_user_job "$group" "$job_name" "$script" yes "OUTFILE=$OUTFILE"
  rm -f $OUTFILE
}

test_user_job_single_line_script()
{
  group="user job running a single-line script"
  job_name="single_line_script_test"
  script="\
script
  sleep 999
end script"
  test_user_job "$group" "$job_name" "$script" no ""
}

test_user_job_single_line_script_task()
{
  group="user job running a single-line script task"
  job_name="single_line_script_task_test"
  OUTFILE=$(mktemp)

  script="\
task
script
  exec true > $OUTFILE
end script"
  test_user_job "$group" "$job_name" "$script" yes "OUTFILE=$OUTFILE"
  rm -f $OUTFILE
}

test_user_job_multi_line_script()
{
  group="user job running a multi-line script"
  job_name="multi_line_script_test"
  script="\
script

  true
  true;true
  sleep 999

end script"
  test_user_job "$group" "$job_name" "$script" no ""
}

test_user_job_multi_line_script_task()
{
  group="user job running a multi-line script task"
  job_name="multi_line_script_task_test"
  OUTFILE=$(mktemp)

  script="\
task
script

  true
  true
  true

end script"
  test_user_job "$group" "$job_name" "$script" yes "OUTFILE=$OUTFILE"
  rm -f $OUTFILE
}

test_user_emit_events()
{
  job_name="start_on_foo"

  TEST_GROUP "user emitting an event"
  initctl emit foo || TEST_FAILED "failed to emit event as user"

  TEST_GROUP "user emitting an event to start a job"
  script="\
    start on foo BAR=2
    stop on baz cow=moo or hello
    exec sleep 999"

  job_file="${test_dir}/${job_name}.conf"
  job="${test_dir_suffix}/${job_name}"

  echo "$script" > $job_file

  ensure_job_known "$job" "$job_name"

  initctl list|grep -q "^$job stop/waiting" || \
      TEST_FAILED "job $job_name not stopped"

  TEST_FEATURE "ensure job can be started with event"
  initctl emit foo BAR=2 || \
    TEST_FAILED "failed to emit event for user job"

  initctl status "$job"|grep -q "^$job start/running" || \
      TEST_FAILED "job $job_name failed to start"
  
  TEST_FEATURE "ensure job can be stopped with event"
  initctl emit baz cow=moo || \
    TEST_FAILED "failed to emit event for user job"

  initctl list|grep -q "^$job stop/waiting" || \
      TEST_FAILED "job $job_name not stopped"

  rm -f "$job_file"
}

test_user_jobs()
{
  test_user_job_binary
  test_user_job_single_line_script
  test_user_job_multi_line_script

  test_user_job_binary_task
  test_user_job_single_line_script_task
  test_user_job_multi_line_script_task

  test_user_emit_events
}

tests()
{
  echo
  echo -n "Running user session tests as user '`whoami`'"
  echo " (uid $uid, gid $gid) in directory '$test_dir'"
  echo

  test_user_jobs

  echo
  echo "All tests completed successfully"
  echo
}

usage()
{
cat <<EOT
USAGE: $script_name [options]

OPTIONS:

 -a        : Actually run this script.      
 -h        : Show this help.
 -u <user> : Specify name of test user to create.

DESCRIPTION: Run simple set of Upstart user session tests.

WARNING: Note that this script is unavoidably invasive, so read what
WARNING: follows before running!

If run as a non-root user, this script will create a uniquely-named
subdirectory below "\$HOME/.init/" to run its tests in. On successful
completion of these tests, the unique subdirectory and its contents will
be removed.

If however, this script is invoked as the root user, the script will
refuse to run until given the name of a test user to create via the "-u"
option. If the user specified to this option already exists, this script
will exit with an error. If the user does not already exist, it will be
created, the script then run *as that user* and assuming successful
completion of the tests, the test user and their home directory will
then be deleted.
 
EOT
}

#---------------------------------------------------------------------
# main
#---------------------------------------------------------------------

while getopts "hu:" opt
do
  case "$opt" in
    h)
      usage
      exit 0
    ;;

    u)
      user_to_create="$OPTARG"
    ;;
  esac
done

setup
tests
cleanup
exit 0
