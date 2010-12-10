/* upstart
 *
 * session.c - session segregation
 *
 * Copyright © 2010 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <dbus/dbus.h>

#include <sys/types.h>

#include <pwd.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/string.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <nih-dbus/dbus_message.h>

#include "session.h"
#include "conf.h"
#include "paths.h"


/**
 * sessions:
 *
 * This list holds the list of known sessions; each item is a Session
 * structure.
 **/
NihList *sessions = NULL;


/* Prototypes for static functions */
static void session_create_conf_source (Session *sesson);


/**
 * session_init:
 *
 * Initialise the sessions list.
 **/
void
session_init (void)
{
	if (! sessions)
		sessions = NIH_MUST (nih_list_new (NULL));
}


Session *
session_new (const void *parent,
	     const char *chroot,
	     uid_t       user)
{
	Session *session;

	nih_assert ((chroot != NULL) || (user != 0));

	session_init ();

	session = nih_new (parent, Session);
	if (! session)
		return NULL;

	nih_list_init (&session->entry);

	if (chroot) {
		session->chroot = nih_strdup (session, chroot);
		if (! session->chroot) {
			nih_free (session);
			return NULL;
		}
	} else {
		session->chroot = NULL;
	}

	session->user = user;

	session->conf_path = NULL;

	nih_alloc_set_destructor (session, nih_list_destroy);

	nih_list_add (sessions, &session->entry);

	return session;
}

Session *
session_from_dbus (const void *    parent,
		   NihDBusMessage *message)
{
	const char *  sender;
	DBusError     dbus_error;
	unsigned long unix_user;
	unsigned long unix_process_id;
	char          root[PATH_MAX];
	Session *     session;

	nih_assert (message != NULL);

	session_init ();

	/* Ask D-Bus nicely for the origin uid and/or pid of the caller;
	 * sadly we can't ask the bus daemon for the origin pid, so that
	 * one will just have to stay user-session only.
	 */
	dbus_error_init (&dbus_error);

	sender = dbus_message_get_sender (message->message);
	if (sender) {
		unix_user = dbus_bus_get_unix_user (message->connection, sender,
						    &dbus_error);
		if (unix_user == (unsigned long)-1) {
			dbus_error_free (&dbus_error);
			unix_user = 0;
		}

		unix_process_id = 0;

	} else {
		if (! dbus_connection_get_unix_user (message->connection,
						     &unix_user))
			unix_process_id = 0;

		if (! dbus_connection_get_unix_process_id (message->connection,
							   &unix_process_id))
			unix_process_id = 0;
	}

	/* If we retrieved a process id, look up the root path for it. */
	if (unix_process_id) {
		nih_local char *symlink = NULL;
		ssize_t len;

		symlink = NIH_MUST (nih_sprintf (NULL, "/proc/%lu/root",
						 unix_process_id));
		len = readlink (symlink, root, sizeof root);
		if (len < 0)
			return NULL;

		root[len] = '\0';

	} else if (! unix_user) {
		/* No process id or user id found, return the NULL session */
		return NULL;

	}

	/* Now find in the existing Sessions list */
	NIH_LIST_FOREACH (sessions, iter) {
		Session *session = (Session *)iter;

		if (unix_process_id) {
			if (! session->chroot)
				continue;
			if (strcmp (session->chroot, root))
				continue;
		}

		if (unix_user != session->user)
			continue;

		if (! session->conf_path)
			session_create_conf_source (session);
		return session;
	}

	/* Didn't find one, make a new one */
	session = NIH_MUST (session_new (parent, unix_process_id ? root : NULL,
					 unix_user));
	session_create_conf_source (session);

	return session;
}

static void
session_create_conf_source (Session *session)
{
	ConfSource *source;

	nih_assert (session != NULL);
	nih_assert (session->conf_path == NULL);

	if (session->chroot)
		session->conf_path = NIH_MUST (nih_strdup (NULL, session->chroot));
	if (session->user) {
		struct passwd *pwd;

		pwd = getpwuid (session->user);
		if (! pwd) {
			nih_error ("%d: %s: %s", session->user,
				   _("Unable to lookup home directory"),
				   strerror (errno));

			nih_free (session->conf_path);
			session->conf_path = NULL;
			return;
		}

		NIH_MUST (nih_strcat_sprintf (&session->conf_path, NULL, "%s/%s",
					      pwd->pw_dir, USERCONFDIR));
	} else {
		NIH_MUST (nih_strcat (&session->conf_path, NULL, CONFDIR));
	}

	source = NIH_MUST (conf_source_new (session, session->conf_path,
					    CONF_JOB_DIR));
	source->session = session;

	if (conf_source_reload (source) < 0) {
		NihError *err;

		err = nih_error_get ();
		if (err->number != ENOENT)
			nih_error ("%s: %s: %s", source->path,
				   _("Unable to load configuration"),
				   err->message);
		nih_free (err);

		nih_free (source);
		nih_free (session->conf_path);
		session->conf_path = NULL;
		return;
	}
}
