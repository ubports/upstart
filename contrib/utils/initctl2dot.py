#!/usr/bin/python
# -*- coding: utf-8 -*-
#---------------------------------------------------------------------
#
# Copyright © 2011 Canonical Ltd.
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
# Script to take output of "initctl list -de" and convert it into
# a Graphviz DOT language (".dot") file for procesing with "dot", etc.
#
# Notes:
#
# - Slightly laborious logic used to satisfy graphviz requirement that
#   all nodes be defined before being referenced.
#
# Usage:
#
#   initctl list -de > initctl.out
#   initctl2dot.py -f initctl.out > upstart.dot
#   dot -Tpng -o upstart.png upstart.dot
#
# Or more simply:
#
#  initctl2dot.py -o - | dot -Tpng -o upstart.png
#
# See also:
#
# - dot(1).
# - initctl(8).
# - http://www.graphviz.org.
#---------------------------------------------------------------------

import sys
import re
import os
import datetime
from os import popen
from optparse import OptionParser

jobs   = {}
events = {}
cmd = "initctl --system list -de"
script_name =  os.path.basename(sys.argv[0])

default_color_emits    = 'green'
default_color_start_on = 'blue'
default_color_stop_on  = 'red'
default_color_event    = 'thistle'
default_color_job      = '#DCDCDC' # "Gainsboro"

default_outfile = 'upstart.dot'

def header(ofh):
  ofh.write("""
    digraph upstart {

       node [shape=record];
       rankdir=LR;
       overlap=false;

  """)


def footer(ofh):
  global options

  epilog = "overlap=false;\n"
  epilog += "label=\"Generated on %s by %s\\n" % \
    (str(datetime.datetime.now()), script_name)

  if options.infile:
    epilog += "(from file data).\\n"
  else:
    epilog += "(from '%s' on host %s).\\n" % \
      (cmd, os.uname()[1])

  epilog += "Boxes of color %s denote jobs.\\n" % options.color_job
  epilog += "Diamonds of color %s denote events.\\n" % options.color_event
  epilog += "Emits denoted by %s lines.\\n" % options.color_emits
  epilog += "Start on denoted by %s lines.\\n" % options.color_start_on
  epilog += "Stop on denoted by %s lines.\\n" % options.color_stop_on
  epilog += "\";"
  epilog += "}\n"
  ofh.write(epilog)


# Map dash to underscore since graphviz node names cannot
# contain dashes. Also remove dollars and colons
def sanitise(s):
  return s.replace('-', '_').replace('$', 'dollar_').replace('[', \
  'lbracket').replace(']', 'rbracket').replace('!', 'bang').replace(':', '_')


# Convert a dollar in @name to a unique-ish new name, based on @job and
# return it.
def encode_dollar(job, name):
  if name[0] == '$':
    name = job + ':' + name
  return name


def show_events(ofh):
  global events
  global options

  for e in events:
    sane_event = sanitise(e)
    ofh.write("""
    %s [label=\"%s\", shape=diamond, fillcolor=\"%s\", style=\"filled\"];
    """ % (sane_event, e, options.color_event))


def show_jobs(ofh):
  global jobs
  global options

  for j in jobs:
    sane_job = sanitise(j)
    ofh.write("""
    %s [label=\"<job> %s | { <start> start on | <stop> stop on }\", style=\"filled\", fillcolor=\"%s\"];
    """ % (sane_job, j, options.color_job))


def show_edges(ofh):
  global events
  global jobs
  global options

  for job in jobs:
    sane_job = sanitise(job)
    for s in jobs[job]['start on'] :
      sane_s = sanitise(s)
      if s in jobs:
        _str = "%s:job" % sane_s
      else:
        _str = sane_s
      ofh.write("%s:start -> %s [color=\"%s\"];\n" %
        (sane_job, _str, options.color_start_on))

    for s in jobs[job]['stop on']:
      sane_s = sanitise(s)
      if s in jobs:
        _str = "%s:job" % sane_s
      else:
        _str = sane_s
      ofh.write("%s:stop -> %s [color=\"%s\"];\n" %
        (sane_job, _str, options.color_stop_on))

    for e in jobs[job]['emits']:
      sane_e = sanitise(e)
      sane_job = sanitise(job)
      ofh.write("%s:job -> %s [color=\"%s\"];\n" %
        (sane_job, sane_e, options.color_emits))


def read_data():
  global jobs
  global events
  global options
  global cmd

  if options.infile:
    try:
      ifh = open(options.infile, 'r')
    except:
      sys.exit("ERROR: cannot read file '%s'" % options.infile)
  else:
    try:
      ifh = popen(cmd, 'r')
    except:
      sys.exit("ERROR: cannot run '%s'" % cmd)

  for line in ifh.readlines():
      record = {}
      line = line.rstrip()

      if re.match('^\s+start on', line):
        name = (line.lstrip().split())[2]
        name = encode_dollar(job, name)
        jobs[job]['start on'][name] = 1
      elif re.match('^\s+stop on', line):
        name = (line.lstrip().split())[2]
        name = encode_dollar(job, name)
        jobs[job]['stop on'][name] = 1
      elif re.match('^\s+emits', line):
        event = (line.lstrip().split())[1]
        event = encode_dollar(job, event)
        events[event] = 1
        jobs[job]['emits'][event] = 1
      else:
        job_record = {}
        start_on   = {}
        stop_on    = {}
        emits      = {}
        job_record['start on'] = start_on
        job_record['stop on']  = stop_on
        job_record['emits']    = emits

        job = (line.lstrip().split())[0]
        jobs[job] = job_record

  # Having loaded all the data, we now categorize "start on" and
  # "stop on" into events and jobs.

  total_start_on = {}
  total_stop_on  = {}

  for job in jobs:
    for name in jobs[job]['start on']:
      total_start_on[name] = 1
    for name in jobs[job]['stop on']:
      total_stop_on[name] = 1

  if options.check_mode == 1:
    missing_events = []
    all_names = dict(total_start_on)
    all_names.update(total_stop_on)

    for name in all_names:
      if not name in jobs.keys() and not name in events.keys():
        print "WARNING: job or event '%s' not emitted by any job" % name
    sys.exit(0)

  # Iterate through all "start on" and "stop on" conditions. If they are
  # jobs, we'll already have recorded them. If they are events, add them
  # if we haven't already done so.
  for name in total_start_on:
    if not name in jobs.keys() and not name in events.keys():
      # must have an event
      events[name] = 1

  for name in total_stop_on:
    if not name in jobs.keys() and not name in events.keys():
      # must have an event
      events[name] = 1


def main():
  global options
  global cmd
  global default_color_emits
  global default_color_start_on
  global default_color_stop_on
  global default_color_event
  global default_color_job

  description = "Convert initctl(8) output to a GraphViz dot diagram."
  epilog = \
    "See http://www.graphviz.org/doc/info/colors.html for available colours."

  parser = OptionParser(description=description, epilog=epilog)

  parser.add_option("-c", "--check",
    dest="check_mode",
    action="store_true",
    help="Look for missing jobs/events then exit (this may not be reliable).")

  parser.add_option("-f", "--infile",
      dest="infile",
      help="File to read '%s' output from. If not specified, " \
      "initctl will be run automatically." % cmd)

  parser.add_option("-o", "--outfile",
      dest="outfile",
      help="File to write output to (default=%s)" % default_outfile)

  parser.add_option("--color-emits",
      dest="color_emits",
      help="Specify color for 'emits' lines (default=%s)." %
      default_color_emits)

  parser.add_option("--color-start-on",
      dest="color_start_on",
      help="Specify color for 'start on' lines (default=%s)." %
      default_color_start_on)

  parser.add_option("--color-stop-on",
      dest="color_stop_on",
      help="Specify color for 'stop on' lines (default=%s)." %
      default_color_stop_on)

  parser.add_option("--color-event",
      dest="color_event",
      help="Specify color for event boxes (default=%s)." %
      default_color_event)

  parser.add_option("--color-job",
      dest="color_job",
      help="Specify color for job boxes (default=%s)." %
      default_color_job)

  parser.set_defaults(color_emits=default_color_emits,
  color_start_on=default_color_start_on,
  color_stop_on=default_color_stop_on,
  color_event=default_color_event,
  color_job=default_color_job,
  outfile=default_outfile)

  (options, args) = parser.parse_args()

  if options.outfile == '-':
    ofh = sys.stdout
  else:
    try:
      ofh = open(options.outfile, "w")
    except:
      sys.exit("ERROR: cannot open file %s for writing" % options.outfile)

  read_data()
  header(ofh)
  show_events(ofh)
  show_jobs(ofh)
  show_edges(ofh)
  footer(ofh)


if __name__ == "__main__":
  main()
