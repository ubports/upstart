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
#include <signal.h>
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
#include "job.h"
#include "environ.h"
#include "blocked.h"
#include "conf.h"
#include "control.h"

json_object *json_sessions = NULL;
json_object *json_events = NULL;
json_object *json_classes = NULL;

extern int use_session_bus;

/**
 * args_copy:
 *
 * Copy of original argv used when re-executing to ensure same
 * command-line is used. Required since we clear the actual args for
 * ps(1) et al.
 */
char **args_copy = NULL;

/**
 * restart:
 *
 * This is set to TRUE if we're being re-exec'd by an existing init
 * process.
 **/
int restart = FALSE;

/* Prototypes for static functions */
static json_object *state_rlimit_serialise (const struct rlimit *rlimit)
	__attribute__ ((malloc, warn_unused_result));

static struct rlimit *state_rlimit_deserialise (json_object *json)
	__attribute__ ((malloc, warn_unused_result));

static json_object *
state_serialise_blocked (const Blocked *blocked)
	__attribute__ ((malloc, warn_unused_result));

static Blocked *
state_deserialise_blocked (void *parent, json_object *json, NihList *list)
	__attribute__ ((malloc, warn_unused_result));

static JobClass *
state_index_to_job_class (int job_class_index)
	__attribute__ ((warn_unused_result));

static Job *
state_get_job (const char *job_class, const char *job_name)
	__attribute__ ((warn_unused_result));

/**
 * state_read:
 *
 * @fd: Open file descriptor to read JSON from.
 *
 * Read JSON-encoded state from specified file descriptor and recreate
 * all internal objects based on JSON representation. The read will
 * timeout, resulting in a failure after STATE_WAIT_SECS seconds
 * indicating a problem with the child.
 *
 * Returns: 0 on success, or -1 on error.
 **/
int
state_read (int fd)
{
	int             nfds;
	int             ret;
	fd_set          readfds;
	struct timeval  timeout;

	nih_assert (fd != -1);

	state_get_timeout (timeout.tv_sec);
	timeout.tv_usec = 0;

	nfds = 1 + fd;

	while (TRUE) {
		FD_ZERO (&readfds);
		FD_SET (fd, &readfds);

		ret = select (nfds, &readfds, NULL, NULL,
				timeout.tv_sec < 0 ? NULL : &timeout);

		if (ret < 0 && (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK))
			return -1;

		if (FD_ISSET (fd, &readfds))
			break;
	}

	nih_assert (ret == 1);

	/* Now, read the data */
	if (state_read_objects (fd) < 0)
		return -1;

	return 0;
}

/**
 * state_write:
 *
 * @fd: Open file descriptor to write JSON to,
 * @state_data: JSON string representing internal object state,
 * @len: length of @state_data.
 *
 * Write internal state to specified file descriptor in JSON format.
 *
 * Signals are assumed to be blocked when this call is made.
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
state_write (int fd, const char *state_data, size_t len)
{
	int             nfds;
	int             ret;
	fd_set          writefds;
	struct timeval  timeout;

	nih_assert (fd != -1);
	nih_assert (state_data);
	nih_assert (len);

	/* must be called from child process */
	nih_assert (getpid () != (pid_t)1);

	state_get_timeout (timeout.tv_sec);
	timeout.tv_usec = 0;

	nfds = 1 + fd;

	while (TRUE) {
		FD_ZERO (&writefds);
		FD_SET (fd, &writefds);

		ret = select (nfds, NULL, &writefds, NULL,
				timeout.tv_sec < 0 ? NULL : &timeout);

		if (ret < 0 && (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK))
			return -1;

		if (FD_ISSET (fd, &writefds))
			break;
	}

	nih_assert (ret == 1);

	if (state_write_objects (fd, state_data, len) < 0)
		return -1;

	return 0;
}


/**
 * state_read_objects:
 *
 * @fd: file descriptor to read serialisation data from.
 *
 * Read serialisation data from specified file descriptor.
 * @fd is assumed to be open and readable.
 *
 * Returns: 0 on success, -1 on error.
 **/
int
state_read_objects (int fd)
{
	ssize_t                  ret;
	int                      initial_size = 4096;
	nih_local NihIoBuffer   *buffer = NULL;
	nih_local char          *buf = NULL;

	nih_assert (fd != -1);

	buffer = nih_io_buffer_new (NULL);

	buf = nih_alloc (NULL, initial_size);
	if (! buf)
		goto error;

	/* Read the JSON data into the buffer */
	do {
		if (nih_io_buffer_resize (buffer, initial_size) < 0)
			goto error;

		ret = read (fd, buf, sizeof (buf));
		if (ret < 0) {
			if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
				goto error;
			continue;
		} else if (! ret)
			break;

		if (nih_io_buffer_push (buffer, buf, ret) < 0)
			goto error;
	} while (TRUE);

	/* Recreate internal state from JSON */
	if (state_from_string (buffer->buf) < 0)
		goto error;

	return 0;

error:
	return -1;
}

/**
 * state_write_objects:
 *
 * @fd: file descriptor to write serialisation data on,
 * @state_data: JSON string representing internal object state,
 * @len: length of @state_data.
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
state_write_objects (int fd, const char *state_data, size_t len)
{
	ssize_t   ret;

	nih_assert (fd != -1);
	nih_assert (state_data);
	nih_assert (len);

#if 1
	/* FIXME: useful debug aid! */
	{
		FILE *fo = fopen("/tmp/state.json", "w");
		if (fo) {
			(void)fwrite (state_data, len, 1, fo);
			fclose (fo);
		}
	}
#endif

	ret = write (fd, state_data, len);

	return (ret < 0 ? -1 : 0);
}

/**
 * state_to_string:
 *
 * @json_string; newly-allocated string,
 * @len: length of @json_string.
 *
 * Serialise internal data structures to a JSON string.
 *
 * Returns: 0 on success, -1 on error.
 **/
int
state_to_string (char **json_string, size_t *len)
{
	json_object        *json;
	const char         *value;

	nih_assert (json_string);
	nih_assert (len);

	json = json_object_new_object ();

	if (! json)
		return -1;

	json_sessions = session_serialise_all ();
	if (! json_sessions)
		goto error;

	json_object_object_add (json, "sessions", json_sessions);

	json_events = event_serialise_all ();
	if (! json_events)
		goto error;

	json_object_object_add (json, "events", json_events);

	json_classes = job_class_serialise_all ();

	if (! json_classes)
		goto error;

	json_object_object_add (json, "job_classes", json_classes);

	/* Note that the returned value is managed by json-c! */
	value = json_object_to_json_string (json);
	if (! value)
		goto error;

	*len = strlen (value);

	*json_string = NIH_MUST (nih_strndup (NULL, value, *len));

	json_object_put (json);

	return 0;

error:
	json_object_put (json);
	return -1;
}

/**
 * state_from_string:
 *
 * @state: JSON-encoded state.
 *
 * Convert JSON string back to an internal representation.
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

	/* This function is called before conf_source_new (), so setup
	 * the environment.
	 */
	conf_init ();

	json = json_tokener_parse_verbose (state, &error);

	if (! json) {
		nih_error ("%s: %s",
				_("Detected invalid serialisation data"),
				json_tokener_error_desc (error));
		return ret;
	}

	if (! state_check_json_type (json, object))
		goto out;

	if (session_deserialise_all (json) < 0)
		goto out;

	if (event_deserialise_all (json) < 0)
		goto out;

	if (job_class_deserialise_all (json) < 0)
		goto out;

	if (state_deserialise_resolve_deps (json) < 0)
		goto out;

	ret = 0;

out:
	/* Only need to free the root JSON node */
	json_object_put (json);

	return ret;
}


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

	nih_assert (fd >= 0);

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
 * state_fd_valid:
 * @fd: file descriptor.
 *
 * Return TRUE if @fd is valid, else FALSE.
 **/
int
state_fd_valid (int fd)
{
	int flags = 0;

	if (fd < 0)
		return FALSE;

	errno = 0;
	flags = fcntl (fd, F_GETFL);

	if (flags < 0)
		return FALSE;

	/* redundant really */
	if (errno == EBADF)
		return FALSE;

	return TRUE;
}

/**
 * state_serialise_str_array:
 *
 * @array: string array to serialise.
 *
 * Convert string array @array into a JSON array object.
 *
 * Returns: JSON-serialised @array, or NULL on error.
 **/
json_object *
state_serialise_str_array (char ** const array)
{
	char * const       *elem;
	json_object        *json;
	json_object        *json_element;
	int                 i;

	nih_assert (array);

	json = json_object_new_array ();
	if (! json)
		return NULL;

	for (elem = array, i = 0; elem && *elem; ++elem, ++i) {

		/* We should never see a blank value, but paranoia is
		 * good.
		 */
		json_element = json_object_new_string (*elem ? *elem : "");

		if (! json_element)
			goto error;

		if (json_object_array_put_idx (json, i, json_element) < 0)
			goto error;
	}

	return json;

error:
	json_object_put (json);
	return NULL;
}

/**
 * state_deserialise_str_array:
 *
 * @parent: parent object for new array (may be NULL),
 * @json: JSON array object representing a string array,
 * @env: TRUE if @json represents an array of environment
 * variables, FALSE for simple strings.
 *
 * Convert JSON array object @json into a string array.
 *
 * Returns: string array, or NULL on error.
 **/
char **
state_deserialise_str_array (void *parent, json_object *json, int env)
{
	size_t    len = 0;
	char    **array = NULL;

	nih_assert (json);

	if (! state_check_json_type (json, array))
		goto error;

	array = nih_str_array_new (parent);
	if (! array)
		return NULL;

	for (int i = 0; i < json_object_array_length (json); i++) {
		json_object  *json_element;
		const char   *element;

		json_element = json_object_array_get_idx (json, i);
		if (! json_element)
			goto error;

		if (! state_check_json_type (json_element, string))
			goto error;

		element = json_object_get_string (json_element);
		if (! element)
			goto error;

		if (env) {
			if (! environ_add (&array, parent, &len, TRUE, element))
				goto error;
		} else {
			if (! nih_str_array_add (&array, parent, &len, element))
				goto error;
		}
	}

	return array;

error:
	nih_free (array);
	return NULL;
}

/**
 * state_serialise_int32_array:
 *
 * @array: array of 32-bit integers,
 * @count: number of values in @array,
 *
 * Convert integer array @array into a JSON array object.
 *
 * Returns: JSON-serialised @array, or NULL on error.
 **/
json_object *
state_serialise_int32_array (int32_t *array, int count)
{
	json_object   *json;
	json_object   *json_element;
	int            i;

	nih_assert (count >= 0);

	json = json_object_new_array ();
	if (! json)
		return NULL;

	if (! count || ! array)
		return json;

	for (i = 0; i < count; ++i) {

		json_element = json_object_new_int (array[i]);

		if (! json_element)
			goto error;

		if (json_object_array_put_idx (json, i, json_element) < 0)
			goto error;
	}

	return json;

error:
	json_object_put (json);
	return NULL;
}

/**
 * state_serialise_int64_array:
 *
 * @array: array of 64-bit integers,
 * @count: number of values in @array,
 *
 * Convert integer array @array into a JSON array object.
 *
 * Returns: JSON-serialised @array, or NULL on error.
 **/
json_object *
state_serialise_int64_array (int64_t *array, int count)
{
	json_object   *json;
	json_object   *json_element;
	int            i;

	nih_assert (count >= 0);

	json = json_object_new_array ();
	if (! json)
		return NULL;

	if (! count || ! array)
		return json;

	for (i = 0; i < count; ++i) {

		json_element = json_object_new_int64 (array[i]);

		if (! json_element)
			goto error;

		if (json_object_array_put_idx (json, i, json_element) < 0)
			goto error;
	}

	return json;

error:
	json_object_put (json);
	return NULL;
}

/**
 * state_deserialise_int32_array:
 *
 * @parent: parent object for new array,
 * @json: JSON array object representing an integer array,
 * @array: array of 32-bit integers,
 * @len: length of @array.
 *
 * Convert JSON array object @json into an array of 32-bit integers.
 *
 * Returns: 0 on success, -1 on ERROR.
 **/
int
state_deserialise_int32_array (void *parent, json_object *json, int32_t **array, size_t *len)
{
	nih_assert (parent);
	nih_assert (json);
	nih_assert (array);
	nih_assert (len);

	if (! state_check_json_type (json, array))
		return -1;

	*len = json_object_array_length (json);

	*array = nih_alloc (parent, (*len) * sizeof (int));
	if (! *array)
		return -1;

	for (int i = 0; i < json_object_array_length (json); i++) {
		int32_t      *element;
		json_object  *json_element;

		element = (*array)+i;

		json_element = json_object_array_get_idx (json, i);
		if (! json_element)
			goto error;

		if (! state_check_json_type (json_element, int))
			goto error;

		errno = 0;
		*element = json_object_get_int (json_element);
		if (! *element && errno == EINVAL)
			goto error;
	}

	return 0;

error:
	nih_free (*array);
	return -1;
}

/**
 * state_deserialise_int64_array:
 *
 * @parent: parent object for new array,
 * @json: JSON array object representing an integer array,
 * @array: array of 64-bit integers,
 * @len: length of @array.
 *
 * Convert JSON array object @json into an array of 64-bit integers.
 *
 * Returns: 0 on success, -1 on ERROR.
 **/
int
state_deserialise_int64_array (void *parent, json_object *json, int64_t **array, size_t *len)
{
	nih_assert (parent);
	nih_assert (json);
	nih_assert (array);
	nih_assert (len);

	if (! state_check_json_type (json, array))
		return -1;

	*len = json_object_array_length (json);

	*array = nih_alloc (parent, (*len) * sizeof (int));
	if (! *array)
		return -1;

	for (int i = 0; i < json_object_array_length (json); i++) {
		int64_t      *element;
		json_object  *json_element;

		element = (*array)+i;

		json_element = json_object_array_get_idx (json, i);
		if (! json_element)
			goto error;

		if (! state_check_json_type (json_element, int))
			goto error;

		errno = 0;
		*element = json_object_get_int64 (json_element);
		if (! *element && errno == EINVAL)
			goto error;
	}

	return 0;

error:
	nih_free (*array);
	return -1;
}

/**
 * state_rlimit_serialise:
 * @limit: rlimit to serialise.
 *
 * Convert @limit into a JSON representation for serialisation.
 * Caller must free returned value using json_object_put().
 *
 * Returns: JSON-serialised rlimit structure, or NULL on error.
 **/
static json_object *
state_rlimit_serialise (const struct rlimit *rlimit)
{
	json_object  *json;

	nih_assert (rlimit);

	json = json_object_new_object ();
	if (! json)
		return NULL;

	if (! state_set_json_int_var_from_obj (json, rlimit, rlim_cur))
		goto error;

	if (! state_set_json_int_var_from_obj (json, rlimit, rlim_max))
		goto error;

	return json;

error:
	json_object_put (json);
	return NULL;
}

/**
 * state_rlimit_serialise_all:
 *
 * @rlimits: array of rlimit structures.
 *
 * Convert array of rlimit structures to JSON representation.
 *
 * Returns: JSON object containing array of rlimits, or NULL on error.
 */
json_object *
state_rlimit_serialise_all (struct rlimit * const *rlimits)
{
	json_object    *json;
	json_object    *json_rlimit;
	struct rlimit   dummy = { 0x0, 0x0 };

	nih_assert (rlimits);

	json = json_object_new_array ();
	if (! json)
		return NULL;

	for (int i = 0; i < RLIMIT_NLIMITS; i++) {
		/* We must encode a blank entry for missing array elements
		 * to ensure correct ordering of limits on deserialisation.
		 */
		json_rlimit = state_rlimit_serialise (rlimits[i]
				? rlimits[i] : &dummy);

		if (! json_rlimit)
			goto error;

		if (json_object_array_add (json, json_rlimit) < 0)
			goto error;
	}

	return json;

error:

	json_object_put (json);
	return NULL;
}

/**
 * state_rlimit_deserialise:
 *
 * @json: JSON-serialised rlimit structure to deserialise.
 *
 * Convert @json into an rlimit structure.
 *
 * Caller must manually nih_ref() returned object to a parent object.
 *
 * Returns: struct rlimit, or NULL on error.
 **/
static struct rlimit *
state_rlimit_deserialise (json_object *json)
{
	struct rlimit   *rlimit;

	nih_assert (json);

	if (! state_check_json_type (json, object))
		goto error;

	rlimit = nih_new (NULL, struct rlimit);
	if (! rlimit)
		return NULL;

	memset (rlimit, '\0', sizeof (struct rlimit));

	if (! state_get_json_int_var_to_obj (json, rlimit, rlim_cur))
		goto error;

	if (! state_get_json_int_var_to_obj (json, rlimit, rlim_cur))
		goto error;

	return rlimit;

error:
	nih_free (rlimit);
	return NULL;
}

/**
 * state_rlimit_deserialise_all:
 *
 * @json: root of JSON-serialised state,
 * @parent: parent of @,
 * @limits: pre-allocated pointer array to hold rlimits array.
 *
 * Convert JSON representation of rlimits back into
 * an array of rlimit structures.
 *
 * Returns: 0 on success, -1 on error.
 **/
int
state_rlimit_deserialise_all (json_object *json, const void *parent,
		struct rlimit *(*rlimits)[])
{
	json_object        *json_limits;
	struct rlimit      *rlimit;
	int                 i;

	nih_assert (json);
	nih_assert (parent);
	nih_assert (rlimits);

	json_limits = json_object_object_get (json, "limits");

	if (! json_limits)
		goto error;

	if (! state_check_json_type (json_limits, array))
		goto error;

	for (i = 0; i < json_object_array_length (json_limits); i++) {
		json_object *json_rlimit;

		nih_assert (i <= RLIMIT_NLIMITS);

		json_rlimit = json_object_array_get_idx (json_limits, i);
		if (! json_rlimit)
			goto error;

		if (! state_check_json_type (json_rlimit, object))
			goto error;

		rlimit = state_rlimit_deserialise (json_rlimit);
		if (! rlimit)
			goto error;

		if (! rlimit->rlim_cur && ! rlimit->rlim_max) {
			/* This limit was simply a placeholder so
			 * don't set it. Arguably, it is possible to set
			 * a limit of zero, but that is non-sensical with
			 * the exception of the nice and rtprio limits,
			 * which conveniently the kernel defaults to zero
			 * anyway ;-)
			 */
			nih_free (rlimit);
			(*rlimits)[i] = NULL;
			continue;
		}

		(*rlimits)[i] = rlimit;
		nih_ref ((*rlimits)[i], parent);
	}

	return 0;

error:
	/* Clean up what we can */
	for (; i >= 0; i--)
		if ((*rlimits)[i])
			nih_free ((*rlimits)[i]);

	return -1;
}

/**
 * state_collapse_env:
 *
 * @env: string array.
 *
 * Convert @env into a flattened string, quoting values as required.
 *
 * Returns: newly-allocated flattened string representing @env on
 * success, or NULL on error.
 **/
char *
state_collapse_env (const char **env)
{
	char          *p;
	char          *flattened = NULL;
	const char  **elem;

	if (! env)
		return NULL;

	for (elem = env; elem && *elem; ++elem) {
		p = strchr (*elem, '=');

		/* If an environment variable contains an equals and whitespace
		 * in the value part, quote the value.
		 */
		if (p && strpbrk (p, " \t")) {
			/* append name and equals */
			NIH_MUST (nih_strcat_sprintf (&flattened, NULL,
						"%s%.*s",
						elem == env ? "" : " ",
						(int)((p - *elem) + 1), *elem));
			p++;

			/* add quoted value */
			if (p) {
				NIH_MUST (nih_strcat_sprintf
						(&flattened, NULL, "\"%s\"", p));
			}
		} else {
			/* either a simple 'name' environment variable,
			 * or a name/value pair without space in the
			 * value part.
			 */
			NIH_MUST (nih_strcat_sprintf
					(&flattened, NULL,
					 "%s%s",
					 elem == env ? "" : " ",
					 *elem));
		}
	}

	return flattened;
}

/**
 * state_get_json_type:
 *
 * @short_type: name of JSON type without the prefix.
 *
 * Convert JSON short-type name to a full JSON type.
 *
 * This function is only required due to JSON-C's integer handling:
 * RFC 4627, the JSON "memo" (not a spec!) alludes to JSON being a
 * subset of ECMA-262, and yet no mention is made of the maximum integer
 * size. ECMA-262 defines a 'Number' to be a 64-bit entity, but older
 * versions of JSON-C defined a number to be the same size as a native
 * integer (which might be 32-bit or 64-bit). Version 0.10 rectified
 * this by storing all integer types as 64-bit internally but it does
 * not define 
 *
 * Returns: JSON type.
 **/
inline enum json_type
state_get_json_type (const char *short_type)
{
	nih_assert (short_type);

#define state_make_type_check(var, short_type) \
	else if (! strcmp (var, #short_type)) \
	return (json_type_ ## short_type)

	if (! strcmp (short_type, "int64"))
		return json_type_int;

	state_make_type_check (short_type, null);
	state_make_type_check (short_type, boolean);
	state_make_type_check (short_type, double);
	state_make_type_check (short_type, int);
	state_make_type_check (short_type, object);
	state_make_type_check (short_type, array);
	state_make_type_check (short_type, string);

#undef state_make_type_check

	nih_assert_not_reached ();

	/* keep compiler happy */
	return json_type_null;
}


/**
 * state_deserialise_resolve_deps:
 *
 * @json: root of JSON-serialised state.
 *
 * Resolve circular dependencies between Events and Jobs (via their
 * parent JobClass) and update JSON accordingly to allow deserialisation
 * of such dependencies.
 *
 * Returns: 0 on success, -1 on error.
 **/
int
state_deserialise_resolve_deps (json_object *json)
{
	nih_assert (json);

	/* XXX: Events, JobClasses, Jobs and DBusConnections must have
	 * previously been deserialised before invoking this function.
	 */
	nih_assert (json_sessions);
	nih_assert (json_events);
	nih_assert (json_classes);

	for (int i = 0; i < json_object_array_length (json_events); i++) {
		json_object  *json_event;
		Event        *event = NULL;

		json_event = json_object_array_get_idx (json_events, i);
		if (! json_event)
			goto error;

		if (! state_check_json_type (json_event, object))
			goto error;

		event = event_from_index (i);
		if (! event)
			goto error;

		if (state_deserialise_blocking (event, &event->blocking, json_event) < 0)
			goto error;
	}

	for (int i = 0; i < json_object_array_length (json_classes); i++) {
		json_object  *json_class;
		json_object  *json_jobs;
		JobClass     *class = NULL;

		json_class = json_object_array_get_idx (json_classes, i);
		if (! json_class)
			goto error;

		if (! state_check_json_type (json_class, object))
			goto error;

		/* lookup class associated with JSON class index */
		class = state_index_to_job_class (i);
		if (! class)
			goto error;

		if (! state_get_json_var_full (json_class, "jobs", array, json_jobs))
			goto error;

		/* look for jobs in JSON with associated blocked entries */
		for (int j = 0; j < json_object_array_length (json_jobs); j++) {
			json_object     *json_blocking;
			json_object     *json_job;
			Job             *job = NULL;
			nih_local char  *job_name = NULL;

			json_job = json_object_array_get_idx (json_jobs, j);
			if (! json_job)
				goto error;

			if (! state_check_json_type (json_job, object))
				goto error;

			json_blocking = json_object_object_get (json_job, "blocking");
			if (! json_blocking)
				continue;

			if (! state_get_json_string_var_strict (json_job, "name", NULL, job_name))
				goto error;

			/* lookup job */
			job = state_get_job (class->name, job_name);
			if (! job)
				goto error;

			/* recreate blocked entries */
			if (state_deserialise_blocking (job, &job->blocking, json_job) < 0)
				goto error;
		}
	}

	return 0;

error:
	return -1;
}

/**
 * state_serialise_blocked:
 *
 * @blocked: Blocked object.
 *
 * Convert a Blocked object into JSON comprising a 'type'
 * field and a 'data' field. The 'data' field is type-specific:
 *
 * - blocked jobs encode the Job instance ('name')
 *   and JobClass name ('class').
 *
 * - blocked events encode the event index number ('index').
 *
 * - all D-Bus blocked types encode the marshalled D-Bus
 *   message ('msg-data'), the D-Bus message serial
 *   number ('msg-id') and the D-Bus connection associated with this
 *   D-Bus message ('msg-connection').
 *
 * Returns: JSON-serialised Blocked object, or NULL on error.
 **/
static json_object *
state_serialise_blocked (const Blocked *blocked)
{
	json_object  *json;
	json_object  *json_blocked_data;

	nih_assert (blocked);

	json = json_object_new_object ();

	if (! json)
		return NULL;

	json_blocked_data = json_object_new_object ();
	if (! json_blocked_data)
		goto error;

	switch (blocked->type) {
	case BLOCKED_JOB:
		{
			/* Need to encode JobClass name and Job name to make
			 * it unique.
			 */
			if (! state_set_json_string_var (json_blocked_data,
						"class",
						blocked->job->class->name))
				goto error;

			if (! state_set_json_string_var (json_blocked_data,
						"name",
						blocked->job->name))
				goto error;

			json_object_object_add (json, "data", json_blocked_data);

		}
		break;

	case BLOCKED_EVENT:
		{
			int event_index = 0;

			event_index = event_to_index (blocked->event);
			if (event_index < 0)
				goto error;

			if (! state_set_json_int_var (json_blocked_data,
						"index", event_index))
				goto error;

			json_object_object_add (json, "data", json_blocked_data);
		}
		break;

	default:
		/* Handle the D-Bus types by encoding the D-Bus message
		 * serial number and marshalled message data.
		 *
		 * This scenario occurs when "initctl emit foo" blocks -
		 * the D-Bus message is "in-flight" but blocked on some
		 * event. Therefore, we must serialise the entire D-Bus
		 * message and reconstruct it on deserialisation.
		 */
		{
			DBusMessage     *message;
			DBusConnection  *connection;
			char            *dbus_message_data_raw = NULL;
			char            *dbus_message_data_str = NULL;
			int              len = 0;
			int              conn_index;
			dbus_uint32_t    serial;

			message    = blocked->message->message;
			connection = blocked->message->connection;

			serial = dbus_message_get_serial (message);

			if (! state_set_json_int_var (json_blocked_data,
						"msg-id",
						serial))
				goto error;

			if (! dbus_message_marshal (message, &dbus_message_data_raw, &len))
				goto error;

			dbus_message_data_str = state_data_to_hex (NULL,
					dbus_message_data_raw,
					len);

			if (! dbus_message_data_str)
				goto error;

			/* returned memory is managed by D-Bus, not NIH */
			dbus_free (dbus_message_data_raw);

			if (! state_set_json_string_var (json_blocked_data,
						"msg-data",
						dbus_message_data_str))
				goto error;

			conn_index = control_conn_to_index (connection);
			if (conn_index < 0)
				goto error;

			if (! state_set_json_int_var (json_blocked_data,
						"msg-connection",
						conn_index))
				goto error;

			json_object_object_add (json, "data", json_blocked_data);
		}
		break;
	}

	if (! state_set_json_enum_var (json,
				blocked_type_enum_to_str,
				"type", blocked->type))
		goto error;

	return json;

error:
	json_object_put (json);
	return NULL;
}

/**
 * state_serialise_blocking:
 *
 * @blocking: list of Blocked objects.
 *
 * Convert a list of Blocked objects into JSON.
 *
 * Returns: JSON-serialised Blocked list, or NULL on error.
 **/
json_object *
state_serialise_blocking (const NihList *blocking)
{
	json_object *json;

	json = json_object_new_array ();

	if (! json)
		return NULL;

	if (! blocking)
		return json;

	NIH_LIST_FOREACH (blocking, iter) {
		Blocked *blocked = (Blocked *)iter;

		json_object *json_blocked;

		/* FIXME: D-Bus blocked objects not serialisable until D-Bus provides
		 * dbus_connection_open_from_fd() to allow
		 * deserialisation.
		 */
#if 1
		if (blocked->type != BLOCKED_EVENT && blocked->type != BLOCKED_JOB) {
			nih_warn ("XXX: WARNING (%s:%d): D-Bus blocked objects NOT being serialised yet",
					__func__, __LINE__);
			continue;
		}
#else
		nih_warn ("XXX: WARNING (%s:%d): D-Bus blocked objects being serialised using experimental D-Bus API",
				__func__, __LINE__);
#endif

		json_blocked = state_serialise_blocked (blocked);
		if (! json_blocked)
			goto error;

		if (json_object_array_add (json, json_blocked) < 0)
			goto error;

	}

	return json;

error:
	json_object_put (json);
	return NULL;
}

/**
 * state_deserialise_blocked:
 *
 * @parent: parent,
 * @json: JSON data representing a blocking array entry,
 * @list: list to add Blocked entry to.
 *
 * Create a single Blocked entry based on data found in JSON and add to
 * @list.
 *
 * Returns: new Blocked object, or NULL on error.
 **/
static Blocked *
state_deserialise_blocked (void *parent, json_object *json,
		NihList *list)
{
	json_object     *json_blocked_data;
	Blocked         *blocked = NULL;
	nih_local char  *blocked_type_str = NULL;
	BlockedType      blocked_type;
	int             ret;

	nih_assert (parent);
	nih_assert (json);
	nih_assert (list);
	nih_assert (control_conns);

	if (! state_get_json_string_var_strict (json, "type", NULL, blocked_type_str))
		goto error;

	blocked_type = blocked_type_str_to_enum (blocked_type_str);
	if (blocked_type == (BlockedType)-1)
		goto error;

	json_blocked_data = json_object_object_get (json, "data");
	if (! json_blocked_data)
		goto error;

	switch (blocked_type) {
	case BLOCKED_JOB:
		{
			nih_local char  *job_name = NULL;
			nih_local char  *job_class_name = NULL;
			Job             *job;

			if (! state_get_json_string_var_strict (json_blocked_data,
						"name", NULL, job_name))
				goto error;

			if (! state_get_json_string_var_strict (json_blocked_data,
						"class", NULL, job_class_name))
				goto error;

			job = state_get_job (job_class_name, job_name);
			if (! job)
				goto error;

			blocked = NIH_MUST (blocked_new (parent, BLOCKED_JOB, job));
			nih_list_add (list, &blocked->entry);
		}
		break;

	case BLOCKED_EVENT:
		{
			Event  *event = NULL;
			int     event_index = -1;

			if (! state_get_json_int_var (json_blocked_data,
						"index", event_index))
				goto error;

			event = event_from_index (event_index);
			if (! event)
				goto error;

			blocked = NIH_MUST (blocked_new (parent, BLOCKED_EVENT, event));
			nih_list_add (list, &blocked->entry);
			event_block (blocked->event);
		}
		break;

	default:

		/* Handle D-Bus types by demarshalling deserialised D-Bus
		 * message and then setting the D-Bus serial number.
		 */
		{
			DBusMessage     *dbus_msg = NULL;
			DBusConnection  *dbus_conn = NULL;
			NihDBusMessage  *nih_dbus_msg = NULL;
			DBusError        error;
			dbus_uint32_t    serial;
			size_t           raw_len;
			nih_local char  *dbus_message_data_str = NULL;
			nih_local char  *dbus_message_data_raw = NULL;
			int              conn_index;

			if (! state_get_json_string_var_strict (json_blocked_data,
						"msg-data",
						NULL,
						dbus_message_data_str))
				goto error;

			if (! state_get_json_int_var (json_blocked_data,
						"msg-id", serial))
				goto error;

			ret = state_hex_to_data (NULL,
					dbus_message_data_str,
					strlen (dbus_message_data_str),
					&dbus_message_data_raw,
					&raw_len);

			if (ret < 0)
				goto error;

			if (! state_get_json_int_var (json_blocked_data, "msg-connection", conn_index))
				goto error;

			dbus_conn = control_conn_from_index (conn_index);
			if (! dbus_conn)
				goto error;

			dbus_error_init (&error);
			dbus_msg = dbus_message_demarshal (dbus_message_data_raw,
					(int)raw_len,
					&error);
			if (! dbus_msg || dbus_error_is_set (&error)) {
				nih_error ("%s: %s",
						_("failed to demarshal D-Bus message"),
						error.message);
				dbus_error_free (&error);
				goto error;
			}

			dbus_message_set_serial (dbus_msg, serial);

			/* FIXME:
			 *
			 * *EITHER*:
			 *
			 * a) call nih_dbus_message_new() then *deref*
			 * both the DBusMessage and the DBusConnection
			 * (since they've *ALREADY* been refed by the
			 * pre-re-exec call to nih_dbus_message_new(),
			 *
			 * b) Create nih_dbus_message_renew (const void
			 * *parent, DBusConnection *connection,
			 * DBusMessage *   message) that creates a
			 * NihDBusMessage, but does *NOT* ref() the
			 * msg+connection (again).
			 */
			/* FIXME: parent is incorrect?!? */
			nih_dbus_msg = nih_dbus_message_new (NULL, dbus_conn, dbus_msg);
			if (! nih_dbus_msg)
				goto error;

			blocked = NIH_MUST (blocked_new (parent, blocked_type, nih_dbus_msg));
			nih_list_add (list, &blocked->entry);
		}
		break;
	}

	return blocked;

error:
	if (blocked)
		nih_free (blocked);

	return NULL;
}

/**
 * state_deserialise_blocking:
 *
 * @parent: parent,
 * @list: list to add new blocked entries to,
 * @json: JSON representing object containing a blocking entry.
 *
 * Recreate Blocked objects from JSON encoded blocking array and add to
 * specified list.
 *
 * Returns: 0 on success, or -1 on error.
 **/
int
state_deserialise_blocking (void *parent, NihList *list, json_object *json)
{
	json_object *json_blocking;

	nih_assert (parent);
	nih_assert (list);
	nih_assert (json);

	json_blocking = json_object_object_get (json, "blocking");

	/* parent is not blocking anything */
	if (! json_blocking)
		return 0;

	for (int i = 0; i < json_object_array_length (json_blocking); i++) {
		json_object * json_blocked;

		json_blocked = json_object_array_get_idx (json_blocking, i);
		if (! json_blocked)
			goto error;

		if (! state_deserialise_blocked (parent, json_blocked, list))
			goto error;
	}

	return 0;

error:
	return -1;
}

/**
 * state_index_to_job_class:
 *
 * @job_class_index: job class index number.
 *
 * Lookup JobClass based on JSON array index number.
 *
 * Returns: existing JobClass on success, or NULL if JobClass not found.
 **/
JobClass *
	state_index_to_job_class (int job_class_index)
{
	int     i = 0;

	nih_assert (job_class_index >= 0);
	nih_assert (job_classes);

	NIH_HASH_FOREACH (job_classes, iter) {
		JobClass *class = (JobClass *)iter;

		if (i == job_class_index)
			return class;

		i++;
	}

	return NULL;
}

/**
 * state_get_job:
 *
 * @job_class: name of job class,
 * @job_name: name of job instance.
 *
 * Lookup job based on parent class name and
 * job instance name.
 *
 * Returns: existing Job on success, or NULL if job class or
 * job not found.
 **/
Job *
state_get_job (const char *job_class, const char *job_name)
{
	JobClass  *class;
	Job       *job;

	nih_assert (job_class);
	nih_assert (job_classes);

	class = (JobClass *)nih_hash_lookup (job_classes, job_class);
	if (! class)
		goto error;

	job = (Job *)nih_hash_lookup (class->instances, job_name);
	if (! job)
		goto error;

	return job;

error:
	return NULL;
}

/**
 * state_data_to_hex:
 *
 * @parent: parent,
 * @data: data to convert,
 * @len: length of @data.
 *
 * Convert @data to a hex-encoded string.
 *
 * Returns: newly-allocated hex-encoded string,
 * or NULL on error.
 **/
char *
state_data_to_hex (void *parent, const void *data, size_t len)
{
	unsigned char  *p;
	char           *encoded = NULL;
	size_t          i;

	nih_assert (data);
	nih_assert (len);

	for (i = 0, p = (unsigned char *)data;
			i < len;
			i++, p++) {
		if (! nih_strcat_sprintf (&encoded, parent, "%02x", *p))
			goto error;
	}

	return encoded;

error:
	if (encoded)
		nih_free (encoded);

	return NULL;
}

/**
 * state_hex_to_data:
 *
 * @parent: parent,
 * @hex_data: hex data to convert,
 * @hex_len: length of @hex,
 * @data: newly-allocated data,
 * @data_len: length of @data.
 *
 * Convert hex-encoded data @hex back into its
 * natural representation.
 *
 * Returns: 0 on success, -1 on error.
 **/
int
state_hex_to_data (void      *parent,
		const void   *hex_data,
		size_t        hex_len,
		char        **data,
		size_t       *data_len)
{
	char    *p;
	char    *d;
	char    *decoded;
	size_t   new_len;

	nih_assert (hex_data);
	nih_assert (hex_len);
	nih_assert (data);
	nih_assert (data_len);
	nih_assert (! (hex_len % 2));

	new_len = hex_len / 2;

	*data = decoded = nih_alloc (parent, new_len);
	if (! decoded)
		return 0;

	memset (decoded, '\0', new_len);

	d = (char *)decoded;

	for (size_t i = 0; i < hex_len; i += 2, d++) {
		char   byte;
		char   str[3] = { '\0' };
		char  *endptr;

		p = ((char *)hex_data)+i;

		str[0] = *p;
		str[1] = *(p+1);

		errno = 0;
		byte = strtol (str, &endptr, 16);

		if (errno || *endptr)
			goto error;

		*d = byte;
	}

	*data_len = (size_t)(d - decoded);

	nih_assert (*data_len == new_len);

	return 0;

error:
	nih_free (*data);
	return -1;
}

/**
 * perform_reexec:
 *
 * Perform a bare re-exec.
 *
 * Note that unless the appropriate command-line option has
 * _already_ been specified in @args_copy, all internal state will be lost.
 **/
void
perform_reexec (void)
{
	NihError    *err;
	const char  *loglevel = NULL;

	/* Although we have a copy of the original arguments (which may
	 * have included an option to modify the log level), we need to
	 * handle the case where the log priority has been changed at
	 * runtime which potentially invalidates the original command-line
	 * option value.
	 *
	 * Fortuitously, this can be handled easily: NIH option parsing
	 * semantics allow any option to be specified multiple times -
	 * the last value seen is used. Therefore, we just append the
	 * current log-level option and ignore any existing (earlier)
	 * log level options.
	 *
	 * Note that should Upstart be re-exec'ed too many times,
	 * eventually an unexpected log level may result if the
	 * command-line becomes too large (and thus truncates).
	 *
	 * The correct way to handle this would be to prune now invalid
	 * options from the command-line to ensure it does not continue
	 * to increase. That said, if we hit the limit, worse things
	 * are probably going on so for now we'll settle for the
	 * simplistic approach.
	 */
	if (nih_log_priority <= NIH_LOG_DEBUG) {
		loglevel = "--debug";
	} else if (nih_log_priority <= NIH_LOG_INFO) {
		loglevel = "--verbose";
	} else if (nih_log_priority >= NIH_LOG_ERROR) {
		loglevel = "--error";
	} else {
		/* User has not modified default log level of
		 * NIH_LOG_MESSAGE.
		 */
		loglevel = NULL;
	}

	if (loglevel)
		NIH_MUST (nih_str_array_add (&args_copy, NULL, NULL, loglevel));

	/* if the currently running instance wasn't invoked as
	 * part of a re-exec, ensure that the next instance is (since
	 * otherwise, why would this function be being called!? :)
	 */
	if (! restart)
		NIH_MUST (nih_str_array_add (&args_copy, NULL, NULL, "--restart"));

	execv (args_copy[0], args_copy);
	nih_error_raise_system ();

	err = nih_error_get ();
	nih_error (_("Failed to re-execute %s: %s"), args_copy[0], err->message);
	nih_free (err);
}

/**
 * stateful_reexec:
 *
 * Perform re-exec with state-passing. UPSTART must be capable of
 * stateful re-exec for this routine to be called. Any failures
 * result in a basic re-exec being performed where all state
 * will be lost.
 *
 * The process involves the initial Upstart instance (PID 1) creating a
 * pipe and then forking. The child then writes its serialised state
 * over the pipe back to PID 1 which has now re-exec'd itself.
 *
 * Once the state has been passed, the child can exit.
 **/
void
stateful_reexec (void)
{
	int             fds[2] = { -1 };
	pid_t           pid;
	sigset_t        mask, oldmask;
	nih_local char *state_data = NULL;
	size_t          len;


	/* Block signals while we work.  We're the last signal handler
	 * installed so this should mean that they're all handled now.
	 *
	 * The child must make sure that it unblocks these again when
	 * it's ready.
	 */
	sigfillset (&mask);
	sigprocmask (SIG_BLOCK, &mask, &oldmask);

	if (state_to_string (&state_data, &len) < 0) {
		nih_error ("%s - %s",
				_("Failed to generate serialisation data"),
				_("reverting to stateless re-exec"));
		goto reexec;
	}

	if (pipe (fds) < 0)
		goto reexec;

	nih_info (_("Performing stateful re-exec"));

	/* retain the D-Bus connection across the re-exec */
	control_prepare_reexec ();

	/* Clear CLOEXEC flag for any job log objects prior to re-exec */
	job_class_prepare_reexec ();

	pid = fork ();

	if (pid < 0)
		goto reexec;
	else if (pid > 0) {
		nih_local char *arg = NULL;

		/* Parent */
		close (fds[1]);

		/* Tidy up from any previous re-exec */
		clean_args ();

		/* Tell the new instance where to read the
		 * serialisation data from.
		 *
		 * Note that if the "new" instance is actually an older
		 * version of Upstart (that does not understand stateful
		 * re-exec), due to the way NIH handles command-line
		 * paring, this option will be ignored and the new instance
		 * will therefore not be able to read the state and overall
		 * a stateless re-exec will therefore be performed.
		 */
		arg = NIH_MUST (nih_strdup (NULL, "--state-fd"));
		NIH_MUST (nih_str_array_add (&args_copy, NULL, NULL, arg));

		arg = NIH_MUST (nih_sprintf (NULL, "%d", fds[0]));
		NIH_MUST (nih_str_array_add (&args_copy, NULL, NULL, arg));
	} else {
		/* Child */
		close (fds[0]);

		nih_info (_("Passing state from PID %d to parent"), (int)getpid ());

		/* D-Bus name and the private control server connection must be
		 * relinquished now to allow parent to acquire them.
		 */
		if (control_bus_release_name () < 0) {
			NihError *err;

			err = nih_error_get ();
			nih_error (_("Failed to release D-Bus name: %s"),
					err->message);
			nih_free (err);
		}

		control_server_close ();

		if (state_write (fds[1], state_data, len) < 0) {
			nih_error ("%s",
				_("Failed to write serialisation data"));
			exit (1);
		}

		/* The baton has now been passed */
		exit (0);
	}

reexec:
	/* Attempt stateful re-exec */
	perform_reexec ();

	/* We should never end up here since it likely indicates the
	 * new init binary is damaged.
	 *
	 * All we can do is restore the signal handler and drop back into
	 * the main loop.
	 */

	/* Restore */
	sigprocmask (SIG_SETMASK, &oldmask, NULL);
}

/**
 * clean_args:
 *
 * Remove any existing state fd and log-level-altering arguments.
 *
 * This stops command-line exhaustion if stateful re-exec is
 * performed many times.
 **/
void
clean_args (void)
{
	int i;

	nih_assert (args_copy);

	for (i = 1; args_copy[i]; i++) {
		int tmp = i;

		if (! strcmp (args_copy[i], "--state-fd")) {
			/* Remove existing option and associated fd
			 * paramter.
			 */
			nih_free (args_copy[tmp]);
			nih_free (args_copy[tmp+1]);

			/* shuffle up the remaining args */
			for (int j = tmp+2; args_copy[j]; tmp++, j++)
				args_copy[tmp] = args_copy[j];

			/* terminate */
			args_copy[tmp] = NULL;

			/* reconsider the newly-shuffled index entry */
			i--;
		} else if ((! strcmp (args_copy[i], "--debug")) ||
			   (! strcmp (args_copy[i], "--verbose")) ||
			   (! strcmp (args_copy[i], "--error"))) {
			/* Remove existing option */
			nih_free (args_copy[i]);

			/* shuffle up the remaining args */
			for (int j = tmp+1; args_copy[j]; tmp++, j++)
				args_copy[tmp] = args_copy[j];

			/* terminate */
			args_copy[tmp] = NULL;

			/* reconsider the newly-shuffled index entry */
			i--;
		}
	}
}
