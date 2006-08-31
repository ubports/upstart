/* upstart
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/socket.h>

#include <linux/if.h>
#include <linux/hdreg.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/main.h>
#include <nih/option.h>
#include <nih/logging.h>
#include <nih/error.h>


/**
 * PROC_IDE:
 *
 * Directory to iterate to find IDE disks.
 **/
#define PROC_IDE "/proc/ide"

/**
 * DEV:
 *
 * Directory containing device nodes.
 **/
#define DEV "/dev"


/* Operation modes */
enum {
	REBOOT,
	HALT,
	POWEROFF
};


/* Prototypes for static functions */
static void down_drives     (void);
static void down_interfaces (void);


/**
 * no_sync:
 *
 * TRUE if we do not wish to call sync() before reboot().
 **/
static int no_sync = FALSE;

/**
 * force:
 *
 * TRUE to behave as if we're called by shutdown.
 **/
static int force = FALSE;

/**
 * poweroff:
 *
 * TRUE if the power should be switched off.
 **/
static int poweroff = FALSE;

/**
 * disk_standby:
 *
 * TRUE if hard disks should be placed into standby.
 **/
static int disk_standby = FALSE;

/**
 * interface_down:
 *
 * TRUE if network interfaces should be brought down.
 **/
static int interface_down = FALSE;


/**
 * options:
 *
 * Command-line options accepted.
 **/
static NihOption options[] = {
	{ 'n', "no-sync", N_("don't sync before reboot or halt"),
	  NULL, NULL, &no_sync, NULL },
	{ 'f', "force", N_("force reboot or halt, don't call shutdown(8)"),
	  NULL, NULL, &force, NULL },
	{ 'p', "poweroff", N_("switch off the power when called as halt"),
	  NULL, NULL, &poweroff, NULL },
	{ 'h', "hard-disk-standby", N_("put hard disks into standby"),
	  NULL, NULL, &disk_standby, NULL },
	{ 'i', "interface-down", N_("bring down network interfaces"),
	  NULL, NULL, &interface_down, NULL },

	/* Compatibility options, all ignored */
	{ 'w', NULL, NULL, NULL, NULL, NULL, NULL },
	{ 'd', NULL, NULL, NULL, NULL, NULL, NULL },
	{ 't', NULL, NULL, NULL, "SECS", NULL, NULL },

	NIH_OPTION_LAST
};


int
main (int   argc,
      char *argv[])
{
	char **args;
	int    mode;

	nih_main_init (argv[0]);

	mode = REBOOT;
	if (! strcmp (program_name, "halt")) {
		mode = HALT;
	} else if (! strcmp (program_name, "poweroff")) {
		mode = POWEROFF;
	}

	args = nih_option_parser (NULL, argc, argv, options, FALSE);
	if (! args)
		exit (1);

	/* Check we're root */
	setuid (geteuid ());
	if (getuid ()) {
		nih_error (_("Need to be root"));
		exit (1);
	}


	/* Check for -p if halt */
	if ((mode == HALT) && poweroff)
		mode = POWEROFF;

	/* Normally we just exec shutdown, which notifies everyone and
	 * signals init.
	 */
	if (! force) {
		char *args[5];
		int   i = 0;

		args[i++] = "/sbin/shutdown";

		switch (mode) {
		case REBOOT:
			args[i++] = "-r";
			break;
		case HALT:
			args[i++] = "-h";
			args[i++] = "-H";
			break;
		case POWEROFF:
			args[i++] = "-h";
			args[i++] = "-P";
			break;
		}

		args[i++] = "now";
		args[i] = NULL;

		nih_info (_("Calling shutdown"));
		execv (args[0], args);

		nih_error (_("Unable to execute shutdown: %s"),
			   strerror (errno));
		exit (1);
	}


	/* Re-enable Control-Alt-Delete in case it breaks */
	reboot (RB_ENABLE_CAD);

	/* Sync the disks */
	chdir ("/");
	if (! no_sync) {
		nih_info (_("Syncing disks"));
		sync ();
		sleep (2);
	}

	/* Deconfigured network interfaces and power down drives */
	if (interface_down)
		down_interfaces ();
	if (disk_standby)
		down_drives ();

	/* Do the syscall */
	switch (mode) {
	case REBOOT:
		nih_info (_("Rebooting"));
		reboot (RB_AUTOBOOT);
		break;
	case HALT:
		nih_info (_("Halting"));
		reboot (RB_HALT_SYSTEM);
		break;
	case POWEROFF:
		nih_info (_("Powering off"));
		reboot (RB_POWER_OFF);
		break;
	}

	/* Shouldn't get here, but if we do, carry on */
	reboot (RB_DISABLE_CAD);

	return 0;
}


/**
 * down_drives:
 *
 * Iterates the IDE disks on the system and power them down, which will
 * cause the writecache to be flushed.
 **/
static void
down_drives (void)
{
	DIR           *proc;
	struct dirent *ent;

	nih_info (_("Putting disks into standby"));

	/* Open /proc/ide */
	proc = opendir (PROC_IDE);
	if (! proc) {
		nih_warn (_("Unable to iterate IDE devices: %s"),
			  strerror (errno));
		return;
	}

	/* Look for any node beginning hd and read its media attribute to
	 * determine whether or not it is a disk
	 */
	while ((ent = readdir (proc)) != NULL) {
		unsigned char cmd1[4] = { WIN_STANDBYNOW1, 0, 0, 0 };
		unsigned char cmd2[4] = { WIN_STANDBYNOW2, 0, 0, 0 };
		char          filename[PATH_MAX + 1], buf[80];
		FILE         *media;
		int           dev;

		if (strncmp (ent->d_name, "hd", 2))
			continue;

		/* Look in /proc/ide/%s/media */
		snprintf (filename, sizeof (filename), "%s/%s/media",
			  PROC_IDE, ent->d_name);
		media = fopen (filename, "r");
		if (! media)
			continue;

		/* Read a single line from the file */
		if (fgets (buf, sizeof (buf), media) == NULL) {
			fclose (media);
			continue;
		}
		fclose (media);

		/* Should be disk */
		if (strcmp (buf, "disk\n"))
			continue;


		/* Ok, we're happy that this is a disk, now do some scary
		 * ioctl shit on it.
		 */
		snprintf (filename, sizeof (filename), "%s/%s",
			  DEV, ent->d_name);
		dev = open (filename, O_RDWR | O_NOCTTY);
		if (dev < 0)
			continue;

		nih_debug ("%s", filename);
		if (ioctl (dev, HDIO_DRIVE_CMD, &cmd1) < 0) {
			nih_warn (_("Failed to put %s into standby: %s"),
				  ent->d_name, strerror (errno));
			close (dev);
			continue;
		}

		if (ioctl (dev, HDIO_DRIVE_CMD, &cmd2) < 0) {
			nih_warn (_("Failed to put %s into standby: %s"),
				  ent->d_name, strerror (errno));
			close (dev);
			continue;
		}

		close (dev);
	}

	closedir (proc);
}

/**
 * down_interfaces:
 *
 * Iterate the up network interfaces and bring them down, dealing with
 * shapers first.
 **/
static void
down_interfaces (void)
{
	struct ifreq  req[64];
	struct ifconf conf;
	int           sock, nreqs, i;

	nih_info (_("Bringing interfaces down"));

	sock = socket (AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		nih_warn (_("Unable to open socket to iterate interfaces: %s"),
			  strerror (errno));
		return;
	}

	/* Request the interface list */
	conf.ifc_req = req;
	conf.ifc_len = sizeof (req);
	if (ioctl (sock, SIOCGIFCONF, &conf) < 0) {
		nih_warn (_("Unable to get list of interfaces: %s"),
			  strerror (errno));
		close (sock);
		return;
	}
	nreqs = conf.ifc_len / sizeof (struct ifreq);

	/* First find the shapers, we need to bring these down first so the
	 * machine doesn't crash
	 */
	for (i = 0; i < nreqs; i++) {
		/* Only bring down non-virtual shaper devices */
		if (strncmp (req[i].ifr_name, "shaper", 6))
			continue;
		if (strchr (req[i].ifr_name, ':'))
			continue;

		nih_debug ("%s", req[i].ifr_name);
		req[i].ifr_flags &= ~IFF_UP;
		if (ioctl (sock, SIOCSIFFLAGS, &req[i]) < 0)
			nih_warn (_("Failed to down interface %s: %s"),
				  req[i].ifr_name, strerror (errno));
	}

	/* Now bring down everything else */
	for (i = 0; i < nreqs; i++) {
		/* Don't bring down loopback or virtual devices */
		if (! strncmp (req[i].ifr_name, "shaper", 6))
			continue;
		if (! strncmp (req[i].ifr_name, "lo", 2))
			continue;
		if (strchr (req[i].ifr_name, ':'))
			continue;

		nih_debug ("%s", req[i].ifr_name);
		req[i].ifr_flags &= ~IFF_UP;
		if (ioctl (sock, SIOCSIFFLAGS, &req[i]) < 0)
			nih_warn (_("Failed to down interface %s: %s"),
				  req[i].ifr_name, strerror (errno));
	}

	close (sock);
}
