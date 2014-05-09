/* upstart
 *
 * session.c - session segregation
 *
 * Copyright © 2010,2011 Canonical Ltd.
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

extern json_object *json_sessions;

/* Prototypes for static functions */
static json_object  *session_serialise   (const Session *session)
	__attribute__ ((warn_unused_result));

static Session *session_deserialise (json_object *json)
	__attribute__ ((warn_unused_result));


/**
 * sessions:
 *
 * This list holds the list of known sessions; each item is a Session
 * structure.
 **/
NihList *sessions = NULL;

/**
 * chroot_sessions:
 *
 * If TRUE, enable chroot sessions, we default to a
 * "traditional" (pre-session support) system.
 **/
int chroot_sessions = FALSE;


/* Prototypes for static functions */
static void session_create_conf_source (Session *sesson, int deserialised);

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

/**
 * session_destroy:
 *
 * Clean up the sessions list.
 **/
void
session_destroy (void)
{
	if (sessions)
		nih_free (sessions);
}


/**
 * session_new:
 * @parent: parent,
 * @chroot: full chroot path.
 *
 * Create a new chroot session.
 *
 * Returns: new Session, or NULL on error.
 **/
Session *
session_new (const void *parent,
	     const char *chroot)
{
	Session *session;

	nih_assert (chroot);

	session_init ();

	session = nih_new (parent, Session);
	if (! session)
		return NULL;

	nih_list_init (&session->entry);

	session->chroot = nih_strdup (session, chroot);
	if (! session->chroot) {
		nih_free (session);
		return NULL;
	}

	session->conf_path = NULL;

	nih_alloc_set_destructor (session, nih_list_destroy);

	nih_list_add (sessions, &session->entry);

	return session;
}

/**
 * session_from_dbus:
 * @parent: parent,
 * @message: D-Bus message.
 *
 * Create a new session, based on the specified D-Bus message.
 *
 * Returns: new Session, or NULL on error.
 **/
Session *
session_from_dbus (const void     *parent,
		   NihDBusMessage *message)
{
	DBusError        dbus_error;
	unsigned long    unix_process_id;
	char             root[PATH_MAX];
	Session         *session;
	nih_local char  *conf_path = NULL;
	nih_local char  *symlink = NULL;
	ssize_t          len;

	nih_assert (message != NULL);

	/* Handle explicit command-line request and alternative request
	 * method (primarily for test framework) to disable session support.
	 */
	if (! chroot_sessions || getenv ("UPSTART_NO_SESSIONS"))
		return NULL;

	session_init ();

	dbus_error_init (&dbus_error);

	/* Query origin pid of the caller */
	if (! dbus_connection_get_unix_process_id (message->connection,
				&unix_process_id)) {
		return NULL;
	}

	/* Look up the root path for retrieved pid */
	symlink = NIH_MUST (nih_sprintf (NULL, "/proc/%lu/root",
				unix_process_id));
	len = readlink (symlink, root, sizeof (root)-1);
	if (len < 0)
		return NULL;

	root[len] = '\0';

	/* Path is not inside a chroot */
	if (! strcmp (root, "/"))
		return NULL;

	/* Now find in the existing Sessions list */
	NIH_LIST_FOREACH_SAFE (sessions, iter) {
		Session *session = (Session *)iter;

		if (strcmp (session->chroot, root))
			/* ignore sessions relating to other chroots */
			continue;

		if (! session->conf_path)
			session_create_conf_source (session, FALSE);

		return session;
	}

	/* Didn't find one, make a new one */
	session = NIH_MUST (session_new (parent, root));
	session_create_conf_source (session, FALSE);

	return session;
}

/**
 * session_create_conf_source:
 * @session: Session,
 * @deserialised: TRUE if ConfSource is to be created from a deserialised
 * session object.
 *
 * Create a new ConfSource object and associate the specified Session
 * with it.
 **/
static void
session_create_conf_source (Session *session, int deserialised)
{
	ConfSource *source;

	nih_assert (session != NULL);
	nih_assert (deserialised
			? session->conf_path != NULL
			: session->conf_path == NULL);

	session_init ();

	if (! deserialised) {
		session->conf_path = NIH_MUST (nih_strdup (NULL, session->chroot));
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
		return;
	}
}

/**
 * session_serialise:
 * @session: session to serialise.
 *
 * Convert @session into a JSON representation for serialisation.
 *
 * Caller must free returned value using json_object_put().
 *
 * Returns: JSON-serialised Session object, or NULL on error.
 **/
static json_object *
session_serialise (const Session *session)
{
	json_object  *json;
	json_object  *conf_path = NULL;
	json_object  *chroot = NULL;

	nih_assert (session);
	nih_assert (session->chroot);
	nih_assert (session->conf_path);

	session_init ();

	json = json_object_new_object ();
	if (! json)
		return NULL;

	chroot = json_object_new_string (session->chroot);
	if (! chroot)
		goto error;

	json_object_object_add (json, "chroot", chroot);

	conf_path = json_object_new_string (session->conf_path);
	if (! conf_path)
		goto error;

	json_object_object_add (json, "conf_path", conf_path);

	return json;

error:
	json_object_put (json);
	return NULL;

}

/**
 * session_serialise_all:
 *
 * Convert existing Session objects to JSON representation.
 *
 * Returns: JSON object containing array of Sessions, or NULL on error.
 **/
json_object *
session_serialise_all (void)
{
	json_object  *json;
	json_object  *json_session;

	session_init ();

	json = json_object_new_array ();
	if (! json)
		return NULL;

	NIH_LIST_FOREACH (sessions, iter) {
		Session      *session = (Session *)iter;

		json_session = session_serialise (session);

		if (! json_session)
			goto error;

		if (json_object_array_add (json, json_session) < 0)
			goto error;
	}

	return json;

error:
	json_object_put (json);
	return NULL;
}

/**
 * session_deserialise:
 * @json: JSON-serialised Session object to deserialise.
 *
 * Convert @json into a Session object.
 *
 * NOTE: Any user sessions seen when re-execing from an older version
 * of Upstart are implicitly ignored.
 *
 * Returns: Session object, or NULL on error.
 **/
static Session *
session_deserialise (json_object *json)
{
	Session              *session = NULL;
	nih_local const char *chroot = NULL;

	nih_assert (json);

	if (! state_check_json_type (json, object))
		return NULL;

	/* Note no check on value returned since chroot may be NULL */
	if (! state_get_json_string_var (json, "chroot", NULL, chroot))
		return NULL;

	if (! chroot)
		return NULL;

	/* Create a new session */
	session = NIH_MUST (session_new (NULL, chroot));

	if (! state_get_json_string_var_to_obj (json, session, conf_path))
		goto error;

	return session;

error:
	nih_free (session);
	return NULL;
}

/**
 * session_deserialise_all:
 *
 * @json: root of JSON-serialised state.
 *
 * Convert JSON representation of sessions back into Session objects.
 *
 * Returns: 0 on success, -1 on error.
 **/
int
session_deserialise_all (json_object *json)
{
	Session  *session;

	nih_assert (json);

	session_init ();

	nih_assert (NIH_LIST_EMPTY (sessions));

	if (! json_object_object_get_ex (json, "sessions", &json_sessions))
		goto error;

	if (! state_check_json_type (json_sessions, array))
		goto error;

	for (int i = 0; i < json_object_array_length (json_sessions); i++) {
		json_object   *json_session;

		json_session = json_object_array_get_idx (json_sessions, i);
		if (! json_session)
			goto error;

		if (! state_check_json_type (json_session, object))
			goto error;

		session = session_deserialise (json_session);
		if (! session)
			goto error;

		/* Note that we do not call session_create_conf_source()
		 * to create a conf source for each session.
		 *
		 * Once we support the deserialisation of chroots, this
		 * will need to happen. However, currently making this
		 * call will result in the creation of JobClasses for
		 * each ConfFile associated with a Session. This is
		 * pointless since those session-specific JobClasses
		 * are never used.
		 */
	}

	return 0;

error:
	return -1;
}

/**
 * session_get_index:
 *
 * @session: session.
 *
 * Determine JSON-serialised array index for specified @session.
 *
 * Returns: zero-based array index for @session, or -1 on error.
 **/
int
session_get_index (const Session *session)
{
	int i;

	/* Handle NULL session (which is not encoded) */
	if (! session)
		return 0;

	/* Sessions are serialised in order, so just return the list
	 * index.
	 */
	i = 1;
	NIH_LIST_FOREACH (sessions, iter) {
		Session *s = (Session *)iter;

		if (s == session)
			return i;

		++i;
	}

	return -1;
}

/**
 * session_from_index:
 *
 * @idx: zero-based index.
 *
 * Lookup session by index number.
 *
 * Returns: Session (which may be the NULL session).
 **/
Session *
session_from_index (int idx)
{
	int       i;
	Session  *session;

	nih_assert (idx >= 0);

	/* NULL session */
	if (! idx)
		return NULL;

	i = 1;
	NIH_LIST_FOREACH (sessions, iter) {
		session = (Session *)iter;

		if (i == idx)
			return session;
		++i;
	}

	/* TODO xnox 20130719 shouldn't we return -1 on error, just
	 * like session_get_index() above? */
	nih_assert_not_reached ();
}
