#!/bin/bash
#---------------------------------------------------------------------
# Script to determine if specified config file is valid or not
# (whether upstart can parse it successfully).
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
confdir=$(mktemp -d /tmp/${script_name}.XXXXXXXXXX)
upstart_path=/sbin/init
initctl_path=/sbin/initctl
debug_enabled=n
file_valid=n

cleanup()
{
  kill -0 $upstart_pid >/dev/null 2>&1 && \
  kill -9 $upstart_pid >/dev/null 2>&1
  [ -d $confdir ] && rm -rf $confdir
  [ $file_valid = y ] && exit 0
  exit 1
}

usage()
{
cat <<EOT
Description: Determine if specified Upstart (init(8)) job configuration
             file is valid.

Usage: $script_name [options] -f <conf_file>
       $script_name [options]    <conf_file>

Options:

  -d        : Show some debug output.
  -f <file> : Job configuration file to check.
  -i <path> : Specify path to initctl binary
              (default=$initctl_path).
  -x <path> : Specify path to init daemon binary
              (default=$upstart_path).
  -h        : Show this help.

EOT
}

debug()
{
  msg="$*"
  [ $debug_enabled = y ] && echo "DEBUG: $msg"
}

error()
{
  msg="$*"
  echo -e "ERROR: $msg" >&2
}

die()
{
  error "$*"
  exit 1
}

trap cleanup EXIT SIGINT SIGTERM

while getopts "dhf:i:x:" opt
do
  case "$opt" in
    d)
      debug_enabled=y
    ;;

    f)
      file=$OPTARG
    ;;

    h)
      usage
      exit 0
    ;;

    i)
      initctl_path=$OPTARG
    ;;

    x)
      upstart_path=$OPTARG
    ;;
  esac
done

shift $[$OPTIND-1]

[ ! -z "$@" ] && file="$1"

# safety first
[ "$(id -u)" -eq 0 ] && die "cannot run as root"

[   -z "$file" ] && die "must specify configuration file"
[ ! -f "$file" ] && die "file $file does not exist"

debug "upstart_path=$upstart_path"
debug "initctl_path=$initctl_path"

for cmd in $upstart_path $initctl_path
do
  [ -f $cmd ] || die "Path $cmd does not exist"
  [ -x $cmd ] || die "File $cmd not executable"
  [ -z "$($cmd --help|grep -- --session 2>/dev/null)" ] && \
    die "version of $cmd too old"
done

# this is the only safe way to run another instance of Upstart
$upstart_path --help|grep -q -- --no-startup-event || die "$upstart_path too old"

debug "confdir=$confdir"
debug "file=$file"

filename=$(basename $file)

echo $filename | egrep -q '\.conf$' || die "file must end in .conf"

job=${filename%.conf}

cp $file $confdir
debug "job=$job"

upstart_out=$(mktemp /tmp/${script_name}-upstart-output.XXXXXXXXXX)
debug "upstart_out=$upstart_out"

upstart_cmd=$(printf \
   "%s --session --no-startup-event --verbose --confdir %s" \
  "$upstart_path" \
  "$confdir")
debug "upstart_cmd=$upstart_cmd"

nohup $upstart_cmd >$upstart_out 2>&1 &
upstart_pid=$!

# Stop the shell outputting a message when Upstart is killed.
# We handle this ourselves in cleanup().
disown 

# wait for upstart to initialize
for i in $(seq 1 5)
do
  dbus-send --session --print-reply \
    --dest='com.ubuntu.Upstart' /com/ubuntu/Upstart \
    org.freedesktop.DBus.Properties.GetAll \
    string:'com.ubuntu.Upstart0_6' >/dev/null 2>&1
  if [ $? -eq 0 ]
  then
    running=y
    break
  fi
  sleep 1
done

[ $running = n ] && die "failed to start upstart"

debug "upstart ($upstart_cmd) running with PID $upstart_pid"

if $initctl_path --session status $job >/dev/null 2>&1
then
  file_valid=y
  echo "File $file: syntax ok"
  exit 0
fi

errors=$(grep $job $upstart_out|sed "s,${confdir}/,,g")
error "File $file: syntax invalid:\n$errors"
