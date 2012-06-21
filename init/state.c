/* upstart
 *
 * state.c - serialisation and deserialisation support.
 *
 * Copyright © 2012 Canonical Ltd.
 * Author: James Hunt <james.hunt@canonical.com>.
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

#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>

#include <nih/macros.h>
#include <nih/logging.h>
#include <nih/string.h>
#include <nih/io.h>

#include "paths.h"
#include "state.h"
#include "session.h"
#include "event.h"
#include "job_class.h"
#include "environ.h"

json_object  *json_sessions = NULL;
json_object  *json_events = NULL;
json_object  *json_job_classes = NULL;

/**
 * state_show_version:
 *
 * Display latest serialistion data format to stdout.
 * NIH routines are not used to guarantee output is not redirected.
 **/
void
state_show_version (void)
{
	printf ("%d\n", STATE_VERSION);
	fflush (stdout);
}

/**
 * FIXME:
 *
 * @version: .
 **/
void
state_init (int version)
{
	const char *which;

	nih_assert (version > 0);

	if (version == STATE_VERSION)
		which = "same";
	else if (version > STATE_VERSION)
		which = "newer";
	else 
		which = "older";

	nih_info ("%s %s %s %s",
		UPSTART,
		_("supports"),
		which,
		_("serialisation format version as running instance"));

}

/**
 * state_get_version:
 *
 * Determine the highest serialisation data format version that the
 * latest UPSTART supports.
 *
 * UPSTART must be known to support serialisation before calling this
 * function.
 *
 * Returns: serialisation version, or -1 on error.
 **/
int
state_get_version (void)
{
	int                     version = -1;
	FILE                   *file;
	nih_local NihIoBuffer  *buffer = NULL;
	nih_local char         *cmdline = NULL;
	char                   *cmd;
       

	/* Useful for testing */
	cmd = getenv ("UPSTART_CMD");

	cmdline = NIH_MUST (nih_sprintf (NULL, "%s %s",
				cmd ? cmd : UPSTART,
				"--state-version"));

	buffer = nih_io_buffer_new (NULL);
	if (! buffer)
		return -1;

	if (nih_io_buffer_resize (buffer, 1024) < 0)
		return -1;

	file = popen (cmdline, "r");
	if (! file)
		return -1;

	if (! fgets (buffer->buf, buffer->size, file))
		goto out;

	version = atoi (buffer->buf + buffer->len);

	if (version <= 0)
		version = -1;

out:
	pclose (file);

	return version;
}
/**
 * state_serialiseable:
 *
 * Determine if UPSTART supports serialisation. This is generally
 * expected to be true as all future versions of Upstart will support
 * it, but we must check to catch the scenario where the user is 
 * downgrading to a version that does not support serialisation.
 *
 * The technique used is somewhat inelegant (although reliable).
 *
 * We really have no other option since NIH ignores all invalid options,
 * which precludes running UPSTART with an expected flag to check
 * if serialisation is supported - we'd just end up running another
 * instance of upstart as root.
 *
 * Returns: TRUE if serialisation is supported, or FALSE on error or
 * serialisation is not supported.
 **/
int
state_serialiseable (void)
{
	char                   *cmd = UPSTART " " "--help";
	nih_local NihIoBuffer  *buffer = NULL;
	FILE                   *file;
	int                     ret = FALSE;

	buffer = nih_io_buffer_new (NULL);
	if (! buffer)
		return ret;

	if (nih_io_buffer_resize (buffer, 1024) < 0)
		return ret;

	file = popen (cmd, "r");
	if (! file)
		return ret;

	while (fgets (buffer->buf, buffer->size, file)) {

		/* FIXME: don't hard-code! */
		if (strstr (buffer->buf, "--state-version")) {
			ret = TRUE;
			break;
		}
	}

	pclose (file);

	return ret;
}

/**
 * state_read:
 *
 * Read JSON-encoded state from specified file descriptor.
 *
 * Returns: 0 on success, or -1 on error.
 **/
int
state_read (int fd)
{
	int             nfds;
	int             ret;
	fd_set          readfds, exceptfds;
	struct timeval  timeout;

	nih_assert (fd != -1);
	nih_assert (getpid () == (pid_t)1);

	timeout.tv_sec  = STATE_WAIT_SECS;
	timeout.tv_usec = 0;

	FD_ZERO (&readfds);
	FD_ZERO (&exceptfds);

	FD_SET (fd, &readfds);
	FD_SET (fd, &exceptfds);

	nfds = 1 + fd;

	while (TRUE) {
		ret = select (nfds, &readfds, NULL, &exceptfds, &timeout);

		if (ret < 0 && errno != EINTR)
			return -1;
	}

	nih_assert (! ret);

	/* Now, write the data */
	if (state_read_objects (fd) < 0)
		return -1;

	return 0;
}

/**
 * state_write:
 *
 * Write internal state to specified file descriptor in JSON format.
 *
 * Note the timeout - it is possible that the new PID 1 instance may be
 * unable to read from its end of the file descriptor, either due to
 * some error scenario or more likely due to it not supporting stateful
 * re-exec. Hence, we must have a way to detect this and abort the
 * child.
 *
 * Returns: 0 on success, or -1 on error.
 **/
int
state_write (int fd)
{
	int             nfds;
	int             ret;
	fd_set          writefds, exceptfds;
	struct timeval  timeout;

	nih_assert (fd != -1);
	nih_assert (getpid () != (pid_t)1);

	timeout.tv_sec  = STATE_WAIT_SECS;
	timeout.tv_usec = 0;

	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	FD_SET (fd, &writefds);
	FD_SET (fd, &exceptfds);

	nfds = 1 + fd;

	while (TRUE) {
		ret = select (nfds, NULL, &writefds, &exceptfds, &timeout);

		if (ret < 0 && errno != EINTR)
			return -1;
	}

	nih_assert (! ret);

	if (state_write_objects (fd) < 0)
		return -1;

	return 0;
}


/**
 * state_read_objects:
 *
 * @fd: file descriptor to read serialisation data on.
 *
 * Read serialisation data from specified file descriptor.
 * @fd is assumed to be open and valid to write to.
 *
 * Returns: 0 on success, -1 on error.
 **/
int
state_read_objects (int fd)
{
	nih_local NihIoBuffer   *buffer = NULL;
	nih_local char          *buf;
	ssize_t                  ret;
	int                      initial_size = 1024;
	json_object             *json;
	enum json_tokener_error  error;

	nih_assert (fd > 0);

	buffer = nih_io_buffer_new (NULL);

	buf = nih_alloc (NULL, initial_size);
	if (! buf)
		return -1;

	if (nih_io_buffer_resize (buffer, sizeof(buf)) < 0)
		return -1;

	while (TRUE) {

		ret = read (fd, buf, sizeof(buf));
		if (ret < 0 && (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK))
			return -1;

		if (nih_io_buffer_push (buffer, buf, ret) < 0)
			return -1;
	}

	/* FIXME: */
	nih_message ("XXX: read json '%s'", buffer->buf);

	json = json_tokener_parse_verbose (buffer->buf, &error);

	if (! json)
		return -1;

	if (! state_check_type (json, object)) {
		json_object_put (json);
		return -1;
	}

	/* FIXME:
	 *
	 * XXX: Now, deserialise all objects!!
	 *
	 * XXX: See state_from_string ().
	 *
	 */
	return 0;
}

/**
 * state_write_objects:
 *
 * @fd: file descriptor to write serialisation data on.
 *
 * Write serialisation data to specified file descriptor.
 * @fd is assumed to be open and valid to write to.
 *
 * Note that the ordering of internal objects is important:
 *
 * 1) Session objects
 * 2) Event objects
 * 3) Log objects
 * 4) JobClass objects
 * 5) Job objects
 *
 * Sessions are handled first since they do not reference any other
 * objects so are discrete.
 *
 * Log objects are stated next to ensure they are known before the
 * Jobs are stated.
 *
 * Event, JobClass and Job objects are more difficult since:
 *   a) Events can reference Jobs via event->blocked
 *      (list of Blocked objects).
 *   b) Jobs can reference Events via the job->blocker Event and the
 *   c) JobClasses reference Jobs via class->instances hash of Job
 *      instances.
 *   d) Jobs reference JobClasses via job->class.
 *
 * Circular dependency (a)+(b) is broken by: FIXME.
 * Circular dependency (c)+(d) is broken by: FIXME.
 *
 * Returns: 0 on success, -1 on error.
 **/
int
state_write_objects (int fd)
{
	/* FIXME
	 *
	 * - write:
	 *   - sessions
	 *   - events
	 *   - ...
	 */
	return 0;
}

/* FIXME: TEST/DEBUG only */
#if 1

/**
 * state_to_string:
 *
 * Convert internal data structures to a JSON string.
 *
 * Returns: string on success, NULL on error.
 **/
char *
state_to_string (void)
{
	json_object  *json;
	char         *value;

	json = json_object_new_object ();

	if (! json)
		return NULL;

	json_sessions = session_serialise_all ();
	if (! json_sessions)
		return NULL;

	json_object_object_add (json, "sessions", json_sessions);

	json_events = event_serialise_all ();
	if (! json_events)
		return NULL;

	json_object_object_add (json, "events", json_events);

	json_job_classes = job_class_serialise_all ();

	if (! json_job_classes)
		return NULL;
	json_object_object_add (json, "job_classes", json_job_classes);


	/* FIXME */
	fprintf(stderr, "sessions='%s'\n", json_object_to_json_string (json_sessions));
	fprintf(stderr, "events='%s'\n", json_object_to_json_string (json_events));
	fprintf(stderr, "job_classes='%s'\n", json_object_to_json_string (json_job_classes));
	fprintf(stderr, "json='%s'\n", json_object_to_json_string (json));

	value = NIH_MUST (nih_strdup (NULL,
			json_object_to_json_string (json)));

	json_object_put (json);

	return value;
}

/**
 * state_from_string:
 *
 * @state: JSON-encoded state.
 *
 * Convert JSON string to an pseudo-internal representation for testing.
 *
 * FIXME:XXX: for TESTING ONLY.
 *
 * Returns: 0 on success, -1 on error.
 **/
int
state_from_string (const char *state)
{
	int                       ret = -1;
	json_object              *json;
	enum json_tokener_error   error;

	nih_assert (state);

	json = json_tokener_parse_verbose (state, &error);

	if (! json) {
		nih_error ("XXX:ERROR: json error='%s'",
				json_tokener_error_desc (error));
		return ret;
	}

	if (! state_check_type (json, object))
		goto out;

	if (session_deserialise_all (json) < 0)
		goto out;

	if (event_deserialise_all (json) < 0)
		goto out;

	if (job_class_deserialise_all (json) < 0)
		goto out;

	ret = 0;

out:
	/* Only need to free the root JSON node */
	json_object_put (json);

	return ret;
}

#endif

#if 0
/**
 * state_get_session_idx:
 *
 * @session: session.
 *
 * Determine JSON-serialised array index for specified @session.
 *
 * Returns: zero-based array index for @session, or -1 on error.
 **/
int
state_get_session_idx (const Session *session)
{
	int i;
#if 0
	int len;

	nih_assert (session_name);
	nih_assert (json_sessions);



	len = json_object_array_length (json_sessions);

	for (int i = 0; i < len; ++i) {
		json_object  *jsession;
		json_object  *jname;
		const char   *name;
	       
		jsession = json_object_array_get_idx (json_sessions, i);

		if ( !jsession)
			goto error;

		jname = json_object_object_get (jsession, "name");
		if (! jname)
			goto error;

		//name = json_object_get_string (jname);
		if (! name)
			goto error;

		if (! strcmp (session_name, name))
			return i;
	}

error:
	return -1;
#endif

	/* Handle NULL session */
	if (! session)
		return 0;

	i = 1;
	NIH_LIST_FOREACH (sessions, iter) {
		Session *s = (Session *)iter;

		if (s == session)
			return i;

		++i;
	}

	return -1;
}
#endif

/**
 * state_toggle_cloexec:
 *
 * @fd: file descriptor,
 * @set: set close-on-exec flag if TRUE, clear if FALSE.
 *
 * Set or clear the close-on-exec file descriptor flag.
 *
 * Returns: 0 on success, -1 on error.
 **/
int
state_toggle_cloexec (int fd, int set)
{
	long   flags;
	int    ret;

	flags = fcntl (fd, F_GETFD);

	if (flags < 0)
		return -1;

	if (set)
		flags |= FD_CLOEXEC;
	else
		flags &= ~FD_CLOEXEC;

	ret = fcntl (fd, F_SETFD, flags);
	if (ret < 0)
		return -1;

	return 0;
}

/**
 * state_serialize_str_array:
 *
 * @array: string array to serialise.
 *
 * Convert string array @array into a JSON array object.
 *
 * Returns JSON-serialised @array, or NULL on error.
 **/
json_object *
state_serialize_str_array (char ** const array)
{
	char * const       *elem;
	json_object        *json;
	json_object        *jelem;
	int                 i;

	nih_assert (array);

	json = json_object_new_array ();
	if (! json)
		return NULL;

	for (elem = array, i = 0; elem && *elem; ++elem, ++i) {

		/* We should never see a blank value, but paranoia is
		 * good.
		 */
		jelem = json_object_new_string (*elem ? *elem : "");

		if (! jelem)
			goto error;

		if (json_object_array_put_idx (json, i, jelem) < 0)
			goto error;
	}

	return json;

error:
	json_object_put (json);
	return NULL;
}

/**
 * state_deserialize_str_array:
 *
 * @parent: parent object for new array,
 * @json: JSON array object representing a string array.
 *
 * Convert JSON array object @json into a string array.
 *
 * Returns string array, or NULL on error.
 **/
char **
state_deserialize_str_array (void *parent, json_object *json)
{
	size_t    env_len = 0;
	char    **array = NULL;

	nih_assert (parent);
	nih_assert (json);

	if (! state_check_type (json, array))
		goto error;

	array = NIH_MUST (nih_str_array_new (parent));

	for (int i = 0; i < json_object_array_length (json); i++) {
		json_object  *jenv_var;
		const char   *env_var;

		jenv_var = json_object_array_get_idx (json, i);
		if (! state_check_type (jenv_var, string))
			goto error;

		env_var = json_object_get_string (jenv_var);
		if (! env_var)
			goto error;

		NIH_MUST (environ_add (&array, parent, &env_len, TRUE, env_var));

#if 1
		/* FIXME */
		nih_message ("%s:%d: found env var '%s'", __func__, __LINE__, env_var);
#endif
	}

	return array;

error:
	nih_free (array);
	return NULL;
}

/**
 * state_serialize_int_array:
 *
 * @array: array of integers,
 * @count: number of values in @array.
 *
 * Convert integer array @array into a JSON array object.
 *
 * Returns JSON-serialised @array, or NULL on error.
 **/
json_object *
state_serialize_int_array (int *array, int count)
{
	json_object        *json;
	json_object        *jelem;
	int                 i;

	nih_assert (array);
	nih_assert (count >= 0);

	json = json_object_new_array ();
	if (! json)
		return NULL;

	if (! count)
		return json;

	for (i = 0; i < count; ++i) {

		jelem = json_object_new_int (array[i]);

		if (! jelem)
			goto error;

		if (json_object_array_put_idx (json, i, jelem) < 0)
			goto error;
	}

	return json;

error:
	json_object_put (json);
	return NULL;
}
