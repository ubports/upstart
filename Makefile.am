## Process this file with automake to produce Makefile.in

SUBDIRS = test dbus init util extra lib conf doc contrib po scripts

EXTRA_DIST = HACKING README.tests

ACLOCAL_AMFLAGS = --install -I m4

# Broken with gcc-4.8 on ubuntu saucy at the moment
AM_DISTCHECK_CONFIGURE_FLAGS = --disable-abi-check
