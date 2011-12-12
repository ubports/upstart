#!/bin/sh
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
sys_job_dir="/etc/init"
user_job_dir="$HOME/.init"
user_log_dir="$HOME/.cache/upstart/log"
sys_log_dir="/var/log/upstart"
bug_url="https://bugs.launchpad.net/upstart/+filebug"
test_dir=
test_dir_suffix=
user_to_create=
uid=
gid=
opt=
OPTARG=
debug_enabled=0
feature=

# allow non-priv users to find 'initctl'
export PATH=$PATH:/sbin

# for assertions
die()
{
  msg="$*"
  echo "ERROR: $msg" >&2
  exit 1
}

debug()
{
  str="$1"
  [ "$debug_enabled" = 1 ] && echo "DEBUG: $str"
}

get_job_pid()
{
  job="$1"
  [ -z "$job" ] && die "need job"

  pid=$(initctl status "$job"|grep process|awk '{print $NF}')
  [ -z "$pid" ] && die "job $job has no pid"

  echo "$pid"
}

# take a string and convert it into a valid job name
make_job_name()
{
  str="$1"

  echo "$str" |\
    sed -e 's/>/ gt /g' -e 's/</ lt /g' -e 's/+/ and /g' |\
    sed -e 's/[[:punct:]]//g' -e 's/  */ /g' |\
    tr ' ' '-'
}

upstart_encode()
{
  str="$1"

  echo "$str" | sed 's!/!_!g'
}

# take a string and convert it into a valid job log file name
make_log_name()
{
  str="$1"
  upstart_encode "$str"
}

TEST_FAILED()
{
  args="$*"

  [ -z "$args" ] && die "need args"

  echo
  echo "ERROR: TEST FAILED ('$feature')"
  echo
  printf "BAD: ${args}\n"
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
  echo "ERROR: TEST FAILED ('$feature')"
  echo
  exit 1
}

TEST_GROUP()
{
  name="$1"

  [ -z "$name" ] && die "need name"

  printf "Testing %s\n" "$name"
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

checks()
{
  cmd=initctl
  [ -z "$(command -v $cmd)" ] && die "cannot find command $cmd"

  [ "$(id -u)" = 0 ] && die "ERROR: should not run this function as root"

  # This will fail for a non-root user unless D-Bus is correctly
  # configured
  $cmd emit foo || die \
    "You do not appear to have configured D-Bus for Upstart user sessions. See usage."
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

  checks

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
  eval "$cmd" >/dev/null 2>&1
  rc=$?
  TEST_EQ "$cmd" $rc 0
}

# Note that if the specified job is *not* as task, it is expected to run
# indefinately. This allows us to perform PID checks, etc.
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
  eval "$cmd" >/dev/null 2>&1
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
exec /bin/true > $OUTFILE"

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
  exec /bin/true > $OUTFILE
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

  /bin/true
  /bin/true;/bin/true
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

  /bin/true
  /bin/true
  /bin/true

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

test_user_job_setuid_setgid()
{
    group="user job with setuid and setgid me"
    job_name="setuid_setgid_me_test"
    script="\
setuid $(id -un)
setgid $(id -gn)
exec sleep 999"
    test_user_job "$group" "$job_name" "$script" no ""

    TEST_GROUP "user job with setuid and setgid root"
    script="\
setuid root
setgid root
exec sleep 999"

    job_name="setuid_setgid_root_test"
    job_file="${test_dir}/${job_name}.conf"
    job="${test_dir_suffix}/${job_name}"

    echo "$script" > $job_file

    ensure_job_known "$job" "$job_name"

    TEST_FEATURE "ensure job fails to start as root"
    cmd="start ${job}"
    output=$(eval "$cmd" 2>&1)
    rc=$?
    TEST_EQ "$cmd" $rc 1

    TEST_FEATURE "ensure 'start' indicates job failure"
    error=$(echo "$output"|grep failed)
    TEST_NE "error" "$error" ""

    TEST_FEATURE "ensure 'initctl' does not list job"
    initctl list|grep -q "^$job stop/waiting" || \
        TEST_FAILED "job $job_name not listed as stopped"

    delete_job "$job_name"
}

get_job_file()
{
  job_name="$1"

  [ -z "$job_name" ] && die "no job name"
  echo "${test_dir}/${job_name}.conf"
}

ensure_no_output()
{
  job_name="$1"
  script="$2"
  instance="$3"

  job="${test_dir_suffix}/${job_name}"

  create_job "$job_name" "$script"
  start_job "$job" "$job_name" "$instance"

  [ ! -z "$(ls $user_log_dir 2>/dev/null)" ] && \
    TEST_FAILED "job $job_name created logfile unexpectedly in '$user_log_dir'"

  # XXX: note that it mihgt appear that checking in $sys_log_dir
  # could result in false positives, but this isn't so since
  # (currently) it is not possible for a user job to have the
  # same name as a system job. start_job() will detect this.
  # scenario.
  for dir in "$user_log_dir" "$sys_log_dir"
  do
    log_file="${dir}/${job_name}.log"
    [ -f "$log_file" ] && \
      TEST_FAILED "job $job_name created logfile unexpectedly as '$log_file'"
  done

  job_file="$(get_job_file $job_name)"
  rm "$job_file" || \
    TEST_FAILED "unable to remove script '$job_file'"
}

create_job()
{
  job_name="$1"
  script="$2"

  # XXX: script could be empty
  [ -z "$job_name" ] && die "no job name"

  debug "create_job: job_name='$job_name'"
  debug "create_job: script='$script'"

  # Not currently possible to have a user job with the
  # same name as a system job.
  #
  # XXX: Note that this test assumes that user has *not* specified
  # XXX: an alternate configuration directory using the
  # XXX: '--confdir' option.
  [ -e "${sys_job_dir}/${job_name}.conf" ] && \
    die "job '$job_name' already exists as a system job"

  job_file="${test_dir}/${job_name}.conf"
  job="${test_dir_suffix}/${job_name}"

  echo "$script" > "$job_file"
  sync
}

delete_job()
{
  job_name="$1"

  [ -z "$job_name" ] && die "no job name"

  job_file="${test_dir}/${job_name}.conf"

  rm "$job_file" || TEST_FAILED "unable to remove job file '$job_file'"
}

start_job()
{
  job="$1"
  job_file="$2"
  instance="$3"

  # XXX: instance may be blank
  [ -z "$job" ] && die "no job"
  [ -z "$job_file" ] && die "no job file"

  debug "start_job: job='$job'"
  debug "start_job: job_file='$job_file'"
  debug "start_job: instance='$instance'"

  eval output=$(mktemp)

  # XXX: Don't quote instance as we don't want to pass a null instance to
  # start(8).
  cmd="start \"$job\" $instance >${output} 2>&1"
  debug "start_job: running '$cmd'"
  eval "$cmd" || TEST_FAILED "job $job_file not started: $(cat $output)"
  rm -f "$output"
}

get_job_logfile_name()
{
  job_name="$1"
  instance_value="$2"

  # XXX: instance may be null
  [ -z "$job_name" ] && die "no job name"

  encoded_test_dir_suffix=$(upstart_encode "${test_dir_suffix}/")
  file_name="${encoded_test_dir_suffix}$(make_log_name $job_name)"

  if [ ! -z "$instance_value" ]
  then
    log_file="${user_log_dir}/${file_name}-${instance_value}.log"
  else
    log_file="${user_log_dir}/${file_name}.log"
  fi

  echo "$log_file"
}

run_job()
{
  job="$1"
  job_name="$2"
  script="$3"
  instance="$4"

  # XXX: script, instance might be blank
  [ -z "$job" ] && die "no job"
  [ -z "$job_name" ] && die "no job name"

  debug "run_job: job='$job'"
  debug "run_job: job_name='$job_name'"
  debug "run_job: script='$script'"
  debug "run_job: instance='$instance'"

  create_job "$job_name" "$script"
  start_job "$job" "$job_name" "$instance"
}

ensure_file_meta()
{
  file="$1"
  expected_owner="$2"
  expected_group="$3"
  expected_perms="$4"

  [ -z "$file" ] && die "no file"
  [ -z "$expected_owner" ] && die "no expected owner"
  [ -z "$expected_group" ] && die "no expected group"
  [ -z "$expected_perms" ] && die "no expected perms"

  [ ! -f "$file" ] && die "file $file does not exist"

  expected_perms="640"
  umask_value=$(umask)
  umask_expected=0022

  if [ "$umask_value" != "$umask_expected" ]
  then
    msg="umask value is $umask_value -"
    msg="${msg} changing it to $umask_expected."
    echo "WARNING: $msg"
    umask "$umask_expected" || TEST_FAILED "unable to change umask"
  fi

  owner=$(ls -l "$file"|awk '{print $3}')
  group=$(ls -l "$file"|awk '{print $4}')
  perms=$(stat --printf "%a\n" "$file")

  [ "$owner" = "$expected_owner" ] || TEST_FAILED \
    "file $file has wrong owner (expected $expected_owner, got $owner)"

  [ "$group" = "$expected_group" ] || TEST_FAILED \
    "file $file has wrong group (expected $expected_group, got $group)"

  [ "$perms" = "$expected_perms" ] || TEST_FAILED \
    "file $file has wrong group (expected $expected_perms, got $perms)"
}


ensure_output()
{
  job_name="$1"
  script="$2"
  expected_output="$3"
  instance="$4"
  instance_value="$5"
  options="$6"

  # XXX: remaining args could be null
  [ -z "$job_name" ] && die "no job name"

  debug "ensure_output: job_name='$job_name'"
  debug "ensure_output: script='$script'"
  debug "ensure_output: expected_ouput='$expected_ouput'"
  debug "ensure_output: instance='$instance'"
  debug "ensure_output: instance_value='$instance_value'"
  debug "ensure_output: options='$options'"

  regex=n
  retain=n
  unique=""
  use_od=n

  for opt in $options
  do
    case "$opt" in
      regex)
        regex=y
        ;;
      retain)
        retain=y
        ;;
      unique)
        unique='|sort -u'
        ;;
      use_od)
        use_od=y
        ;;
    esac
  done

  debug "ensure_output: regex='$regex'"
  debug "ensure_output: retain='$retain'"
  debug "ensure_output: unique='$unique'"
  debug "ensure_output: use_od='$use_od'"

  expected_owner=$(id -un)
  expected_group=$(id -gn)
  expected_perms="640"

  job="${test_dir_suffix}/${job_name}"

  run_job "$job" "$job_name" "$script" "$instance"

  debug "ensure_output: user_log_dir='$user_log_dir'"
  debug "ensure_output: test_dir='$test_dir'"
  debug "ensure_output: test_dir_suffix='$test_dir_suffix'"

  log_file=$(get_job_logfile_name "$job_name" "$instance_value")

  debug "ensure_output: log_file='$log_file'"

  # Give Upstart a chance to parse the file
  count=1
  while ! status "$job" >/dev/null 2>&1
  do
    sleep 1
    count=$((count+1))
    [ "$count" -eq 5 ] && break
  done

  # give job a chance to start
  count=1
  while [ ! -f "$log_file" ]
  do
    sleep 1
    count=$((count+1))
    [ "$count" -eq 5 ] && break
  done

  [ ! -f "$log_file" ] && \
      TEST_FAILED "job '$job_name' failed to create logfile"

  ensure_file_meta \
    "$log_file" \
    "$expected_owner" \
    "$expected_group" \
    "$expected_perms"

  # XXX: note we have to remove carriage returns added by the line
  # discipline
  if [ "$regex" = y ]
  then
    log=$(eval "cat $log_file|tr -d '\r' $unique")
    msg="job '$job_name' failed to log correct data\n"
    msg="${msg}\texpected regex: '$expected_output'\n"
    msg="${msg}\tgot           : '$log'"
    cat "$log_file" | egrep "$expected_output" || TEST_FAILED "$msg"
  elif [ "$use_od" = y ]
  then
    log=$(eval "cat $log_file|tr -d '\r' $unique|od -x")
    msg="job '$job_name' failed to log correct data\n"
    msg="${msg}\texpected hex: '$expected_output'\n"
    msg="${msg}\tgot         : '$log'"
    [ "$expected_output" != "$log" ] && TEST_FAILED "$msg"
  else
    log=$(eval "cat $log_file|tr -d '\r' $unique")
    msg="job '$job_name' failed to log correct data\n"
    msg="${msg}\texpected text: '$expected_output'\n"
    msg="${msg}\tgot          : '$log'"
    [ "$expected_output" != "$log" ] && TEST_FAILED "$msg"
  fi

  if [ "$retain" = n ]
  then
    delete_job "$job_name"
    rm "$log_file" || TEST_FAILED "unable to remove log file '$log_file'"
  fi
}

test_ensure_no_unexpected_output()
{
  #---------------------------------------------------------------------
  feature="ensure command job does not create log file with no console"
  TEST_FEATURE "$feature"

  job_name=$(make_job_name "$feature")

  script="\
    console none
    exec echo hello world"

  ensure_no_output "$job_name" "$script" ""

  #---------------------------------------------------------------------
  feature="ensure 1-line script job does not create log file with no console"
  TEST_FEATURE "$feature"

  job_name=$(make_job_name "$feature")

  script="\
    console none
    script
      echo hello world
    end script
  "

  ensure_no_output "$job_name" "$script" ""

  #---------------------------------------------------------------------
  feature="ensure multi-line script job does not create log file with no console"
  TEST_FEATURE "$feature"

  job_name=$(make_job_name "$feature")

  script="\
    console none
    script
      /bin/true
      echo hello world
    end script
  "

  ensure_no_output "$job_name" "$script" ""

  #---------------------------------------------------------------------
  feature="ensure no output if log directory does not exist"
  TEST_FEATURE "$feature"

  rmdir "${user_log_dir}" || \
    TEST_FAILED "unable to delete log directory '$user_log_dir'"

  job_name=$(make_job_name "$feature")
  string="hello world"
  script="\
    console log
    script
      /bin/true
      /bin/echo hello world
    end script
  "

  ensure_no_output "$job_name" "$script" ""

  mkdir "${user_log_dir}" || \
    TEST_FAILED "unable to recreate log directory '$user_log_dir'"
}

test_output_logged()
{
  # XXX: upstart won't create this
  mkdir -p "$user_log_dir"

  test_ensure_no_unexpected_output
}

test_user_jobs()
{
  test_user_job_binary
  test_user_job_single_line_script
  test_user_job_multi_line_script

  test_user_job_binary_task
  test_user_job_single_line_script_task
  test_user_job_multi_line_script_task

  test_user_job_setuid_setgid

  test_user_emit_events

  test_output_logged
}

tests()
{
  echo
  echo -n "Running Upstart user session tests as user '`whoami`'"
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

DESCRIPTION:

Run simple set of Upstart user session tests.

PREREQUISITE:

For this test to run, non-root users must be allowed to invoke all D-Bus
methods on Upstart via configuration file:

  /etc/dbus-1/system.d/Upstart.conf 

See dbus-daemon(1) for further details.

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

while getopts "dhu:" opt
do
  case "$opt" in
    d)
      debug_enabled=1
    ;;

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
