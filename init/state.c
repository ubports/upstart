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
#include "job.h"
#include "environ.h"
#include "blocked.h"

json_object *json_sessions = NULL;
json_object *json_events = NULL;
json_object *json_classes = NULL;

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

static int
state_event_to_index (const Event *event)
	__attribute__ ((warn_unused_result));

static Event *
state_index_to_event (int event_index)
	__attribute__ ((warn_unused_result));

static JobClass *
state_index_to_job_class (int job_class_index)
	__attribute__ ((warn_unused_result));

static Job *
state_names_to_job (const char *job_class, const char *job_name)
	__attribute__ ((warn_unused_result));

static char *
state_data_to_hex (void *parent, const void *data, size_t len)
	__attribute__ ((warn_unused_result));

static int
state_hex_to_data (void *parent, const void *hex_data,
		   size_t hex_len, char **data,
		   size_t *data_len)
	__attribute__ ((warn_unused_result));

/* FIXME */
#if 1
#include "nih_iterators.h"
#endif

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

	if (nih_io_buffer_resize (buffer, sizeof (buf)) < 0)
		return -1;

	while (TRUE) {

		ret = read (fd, buf, sizeof (buf));
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

	if (! state_check_json_type (json, object)) {
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
 * Serialise internal data structures to a JSON string.
 *
 * Returns: string on success, NULL on error.
 **/
char *
state_to_string (void)
{
	json_object        *json;
	char               *value;
	//nih_local NihList  *blocked;

/* FIXME */
#if 1
	extern NihList *conf_sources;
	extern NihList *control_conns;

	session_init ();
	event_init ();

	nih_message ("#-----------------------------------------");
	nih_message ("# Serialisation summary");
	nih_message (" ");
	nih_message ("DEBUG:hash: job_classes=%d", nih_hash_count (job_classes));
	{
		NIH_HASH_FOREACH (job_classes, iter) {
			JobClass *class = (JobClass *)iter;
			int count = nih_hash_count (class->instances);

			nih_message ("DEBUG:hash: job_class '%s' has %d job%s",
					class->name,
					count,
					count == 1 ? "" : "s");
					
		}
	}
	nih_message ("DEBUG:list: sessions=%d", nih_list_count (sessions));
	nih_message ("DEBUG:list: events=%d", nih_list_count (events));
	nih_message ("DEBUG:list: conf_sources=%d", nih_list_count (conf_sources));
	nih_message ("DEBUG:list: control_conns=%d", nih_list_count (control_conns));
	nih_message (" ");
	nih_message ("#-----------------------------------------");
#endif

	json = json_object_new_object ();

	if (! json)
		return NULL;

	json_sessions = session_serialise_all ();
	if (! json_sessions)
		goto error;

	json_object_object_add (json, "sessions", json_sessions);

#if 0
	blocked = nih_list_new (NULL);
	if (! blocked)
		goto error;
#endif

	json_events = event_serialise_all ();
	if (! json_events)
		goto error;

	json_object_object_add (json, "events", json_events);

	json_classes = job_class_serialise_all ();

	if (! json_classes)
		goto error;

	/* FIXME:
	 *
	 * To serialise jobs:
	 *
	 * - call 'json_jobs = job_class_job_serialise_all()' that
	 *   loops through "job_classes->instances".
	 * - iterate through json_classes, setting numeric index
	 *   numbers to the appropriate job index number in json_jobs.
	 * - iterate through json_jobs, setting numeric index
	 *   numbers to the appropriate job_class index in
	 *   json_classes.
	 * - iterate through json_events setting refs to jobs.
	 */

	json_object_object_add (json, "job_classes", json_classes);

#if 0
	if (state_serialise_resolve_deps (json_events, json_classes) < 0)
		goto error;
#endif

#if 0
	json_jobs = job_serialise_all ();
	if (! json_jobs)
		goto error;
	json_object_object_add (json, "jobs", json_jobs);
#endif


	/* FIXME */
	fprintf(stderr, "sessions='%s'\n", json_object_to_json_string (json_sessions));
	fprintf(stderr, "events='%s'\n", json_object_to_json_string (json_events));
	fprintf(stderr, "job_classes='%s'\n", json_object_to_json_string (json_classes));
	fprintf(stderr, "json='%s'\n", json_object_to_json_string (json));

	value = NIH_MUST (nih_strdup (NULL,
			json_object_to_json_string (json)));

	json_object_put (json);

	return value;

error:
	json_object_put (json);
	return NULL;
}

/**
 * state_from_string:
 *
 * @state: JSON-encoded state.
 *
 * Convert JSON string to a pseudo-internal representation for testing.
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
		/* FIXME */
		nih_error ("XXX:ERROR: json error='%s'",
				json_tokener_error_desc (error));
		return ret;
	}

	/* FIXME */
#if 1
	extern NihList *conf_sources;
	extern NihList *control_conns;

	nih_message ("#-----------------------------------------");
	nih_message ("DEBUG:PRE:hash: job_classes=%d",
			job_classes ? nih_hash_count (job_classes) : 0);

	nih_message ("DEBUG:PRE:list: sessions=%d",
			sessions ? nih_list_count (sessions) : 0);

	nih_message ("DEBUG:PRE:list: events=%d",
			events ? nih_list_count (events) : 0);

	nih_message ("DEBUG:PRE:list: conf_sources=%d",
			conf_sources ? nih_list_count (conf_sources) : 0);

	nih_message ("DEBUG:PRE:list: control_conns=%d",
			control_conns ? nih_list_count (control_conns) : 0);
	nih_message ("#-----------------------------------------");
#endif

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

/* FIXME */
#if 1
	nih_message ("#-----------------------------------------");
	nih_message ("# Deserialisation summary");
	nih_message (" ");
	nih_message ("DEBUG:POST:hash: job_classes=%d", nih_hash_count (job_classes));
	{
		NIH_HASH_FOREACH (job_classes, iter) {
			JobClass *class = (JobClass *)iter;
			int count = nih_hash_count (class->instances);

			nih_message ("DEBUG:POST:hash: job_class '%s' has %d job%s",
					class->name,
					count,
					count == 1 ? "" : "s");
					
		}
	}
	nih_message ("DEBUG:POST:list: sessions=%d", nih_list_count (sessions));
	nih_message ("DEBUG:POST:list: events=%d", nih_list_count (events));
	nih_message ("DEBUG:POST:list: conf_sources=%d", nih_list_count (conf_sources));
	nih_message ("DEBUG:POST:list: control_conns=%d", nih_list_count (control_conns));
	nih_message (" ");
	nih_message ("#-----------------------------------------");
#endif

	ret = 0;

out:
	/* Only need to free the root JSON node */
	json_object_put (json);

	return ret;
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
 * @parent: parent object for new array,
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

	nih_assert (parent);
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

/* FIXME: encode as int/int64 rather than string! */

static json_object *
state_rlimit_serialise (const struct rlimit *rlimit)
{
	json_object    *json;
	nih_local char *buffer = NULL;

	nih_assert (rlimit);

	json = json_object_new_object ();
	if (! json)
		return NULL;

	buffer = nih_sprintf (buffer, "0x%lx", rlimit->rlim_cur);
	if (! buffer)
		goto error;

	if (! state_set_json_var_full (json, "rlim_cur", buffer, string))
		goto error;

	buffer = nih_sprintf (buffer, "0x%lx", rlimit->rlim_max);
	if (! buffer)
		goto error;

	if (! state_set_json_var_full (json, "rlim_max", buffer, string))
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
		 * to ensure correct deserialisation.
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
	struct rlimit  *rlimit;
	const char     *rlim_cur;
	const char     *rlim_max;
	char           *endptr;

	nih_assert (json);

	if (! state_check_json_type (json, object))
		goto error;

	rlimit = nih_new (NULL, struct rlimit);
	if (! rlimit)
		return NULL;

	memset (rlimit, '\0', sizeof (struct rlimit));

	if (! state_get_json_string_var (json, "rlim_cur", rlim_cur))
		goto error;

	errno = 0;
	rlimit->rlim_cur = strtoul (rlim_cur, &endptr, 16);
	if (errno || *endptr)
		goto error;

	if (! state_get_json_string_var (json, "rlim_max", rlim_max))
		goto error;

	errno = 0;
	rlimit->rlim_max = strtoul (rlim_max, &endptr, 16);
	if (errno || *endptr)
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

		(*rlimits)[i] = state_rlimit_deserialise (json_rlimit);
		if (! (*rlimits)[i])
			goto error;
	}

	return 0;

error:
	/* FIXME: trace alloc path to see if this is necessary, and if
	 * so add it for all equivalent functions!
	 */
#if 0
	while (i--)
		nih_free (rlimits[i]);
#endif

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
state_collapse_env (char **env)
{
	char   *p;
	char   *flattened;
	char  **elem;

	nih_assert (env);

	if (! env)
		return NULL;

	/* Start with a string we can append to */
	flattened = NIH_MUST (nih_strdup (NULL, ""));

	for (elem = env; elem && *elem; ++elem) {
		p = strchr (*elem, '=');

		/* If an environment variable contains an equals and whitespace
		 * in the value part, quote the value.
		 */
		if (p && strpbrk (p, " \t")) {
			/* append name and equals */
			NIH_MUST (nih_strcat_sprintf (&flattened, NULL, " %.*s",
						(int)((p - *elem) + 1), *elem));
			p++;

			/* add quoted value */
			if (p) {
				NIH_MUST (nih_strcat_sprintf (&flattened, NULL, "\"%s\"",
							p));
			}
		} else {
			/* either a simple 'name' environment variable,
			 * or a name/value pair without space in the
			 * value part.
			 */
			NIH_MUST (nih_strcat_sprintf (&flattened, NULL, " %s", *elem));
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
/* FIXME */
    //return json_type_null;
}


/* FIXME: remove - not needed? */
#if 0
/**
 * state_serialise_resolve_deps:
 *
 * @json_event: JSON array of events,
 * @json_classes: JSON array of job_classes.
 *
 * Resolve circular dependencies between Events and Jobs (via their
 * parent JobClass) and update JSON accordingly to allow deserialisation
 * of such dependencies.
 *
 * Returns: 0 on success, -1 on error.
 **/
int
state_serialise_resolve_deps (json_object *json_events,
		              json_object *json_classes)
{
	nih_assert (json_events);
	nih_assert (json_classes);

	NIH_HASH_FOREACH (job_classes, iter) {
		JobClass *class = (JobClass *)iter;

		NIH_HASH_FOREACH (class->instances, iter) {
			Job          *job = (Job *)iter;
			json_object  *blocking = NULL;
			json_object  *blocker;

			//blocker = json_object_object_get (json, "blocker");

			if (blocker) {
#if 0
				int event_index;

				errno = 0;
				event_index = json_object_get_int (blocker);
				if (! event_index && errno == EINVAL)
					goto error;
				NIH_
#endif
					//state_handle_job_blocker ();
			}
			NIH_LIST_FOREACH (&job->blocking, iter) {
				Blocked *blocked = (Blocked *)iter;

				//json_object *json_job = json_object_object_get ();

				nih_debug ("%s:%d: job '%s:%s' blocking type %d",
						__func__, __LINE__,
						class->name,
						job->name,
						blocked->type);

				switch (blocked->type) {
				case BLOCKED_JOB:
					{
						nih_debug ("XXX: job is blocking another job (of class '%s')", blocked->job->class->name);

						//json_job = state_get_json_job (json_classes, job);

						/* TODO:
						 *
						 * - find *this* job in
						 *   json_classes.
						 * - add
						 *
						 */
						//if (state_serialise_blocking (json_job, blocked) < 0)
						//	goto error;

						if (! blocking)
							blocking = json_object_new_array ();
						if (! blocking)
							goto error;

					}

					break;

				case BLOCKED_EVENT:
					nih_debug ("XXX: job is blocking event '%s'",
							blocked->event->name);
					break;

				default:
					/* D-Bus */
					nih_debug ("XXX: job is blocking a D-BUS thang");
					break;
				}
			}
		}
	}

	return 0;

error:
	return -1;
}
#endif

/**
 * state_deserialise_resolve_deps:
 *
 * @json: root of JSON-serialised state.
 *
 * Resolve circular dependencies between Events and Jobs (via their
 * parent JobClass) and update JSON accordingly to allow deserialisation
 * of such dependencies.
 *
 * XXX: FIXME: More details required!
 *
 * Returns: 0 on success, -1 on error.
 **/
int
state_deserialise_resolve_deps (json_object *json)
{
	nih_assert (json);

	/* XXX: Events, JobClasses and Jobs must have previously
	 * been deserialised before invoking this function.
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

		event = state_index_to_event (i);
		if (! event)
			goto error;

		if (state_deserialise_blocking (event, &event->blocking, json_event) < 0)
			goto error;
	}

	for (int i = 0; i < json_object_array_length (json_classes); i++) {
		json_object  *json_class;
		json_object  *json_jobs;
		JobClass     *class = NULL;

		/* FIXME: TODO:
		 *
		 * - iterate through JobClasses and find (JobClass == i).
		 * - iterate through JSON class->"jobs"
		 * - if any job has a blocking entry:
		 *   - find job in class->instances.
		 *   - recreate blocked objects.
		 */

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
			json_object  *json_blocking;
			json_object  *json_job;
			Job          *job = NULL;
			const char   *job_name;

			json_job = json_object_array_get_idx (json_jobs, j);
			if (! json_job)
				goto error;

			if (! state_check_json_type (json_job, object))
				goto error;

			json_blocking = json_object_object_get (json_job, "blocking");
			if (! json_blocking)
				continue;

			if (! state_get_json_string_var (json_job, "name", job_name))
				goto error;

			/* lookup job */
			job = state_names_to_job (class->name, job_name);
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
 * Convert a Blocked object into JSON comprising a "type" string
 * and a "data" string representing the type-specific data.
 *
 * Returns: JSON-serialised Blocked object, or NULL on error.
 **/
json_object *
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

#if 1
/* FIXME */
	nih_debug ("%s:%d: blocking type: %d",
			__func__, __LINE__,
			blocked->type);
#endif

	switch (blocked->type) {
	case BLOCKED_JOB:
		{
			/* Need to encode JobClass name and Job name to make
			 * it unique.
			 */
			if (! state_set_json_var_full (json_blocked_data,
						"class",
						blocked->job->class->name,
						string))
				goto error;

			if (! state_set_json_var_full (json_blocked_data,
						"name",
						blocked->job->name
						? blocked->job->name
						: "",
						string))
				goto error;

			json_object_object_add (json, "data", json_blocked_data);

			if (! state_set_json_var_full (json, "type", "job", string))
				goto error;
		}
		break;

	case BLOCKED_EVENT:
		{
			int event_index = 0;

			event_index = state_event_to_index (blocked->event);
			if (event_index < 0)
				goto error;

			if (! state_set_json_var_full (json_blocked_data,
						"index",
						event_index, int))
				goto error;

			json_object_object_add (json, "data", json_blocked_data);

			if (! state_set_json_var_full (json, "type", "event", string))
				goto error;
		}
		break;

	default:
		/* Handle the D-Bus types by encoding the D-Bus message
		 * serial number and message data.
		 *
		 * This scenario occurs when "initctl emit foo" blocks -
		 * the D-Bus message is "in-flight" but blocked on some
		 * event. Therefore, we must serialise the entire D-Bus
		 * message and reconstruct it on deserialisation.
		 */
		{
			DBusMessage  *message = blocked->message->message;
			char         *dbus_message_data_raw = NULL;
			char         *dbus_message_data_str = NULL;
			int           len = 0;

			if (! state_set_json_var_full (json_blocked_data,
						"msg-id",
						dbus_message_get_serial (message),
						int))
				goto error;

			if (! dbus_message_marshal (message, &dbus_message_data_raw, &len))
				goto error;

			dbus_message_data_str = state_data_to_hex (NULL,
					dbus_message_data_raw,
					len);

			if (! dbus_message_data_str)
					goto error;

			/* returned memory is not managed by NIH, hence use
			 * libc facilities.
			 */
			free (dbus_message_data_raw);

			if (! state_set_json_var_full (json_blocked_data,
						"msg-data",
						dbus_message_data_str,
						string))
				goto error;

			json_object_object_add (json, "data", json_blocked_data);

			if (! state_set_json_var_full (json, "type", "dbus", string))
				goto error;
		}
		break;
	}

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
Blocked *
state_deserialise_blocked (void *parent, json_object *json,
			   NihList *list)
{
	json_object  *json_blocked_data;
	Blocked      *blocked = NULL;
	const char   *blocked_type;
	int           ret;

	nih_assert (parent);
	nih_assert (json);
	nih_assert (list);

	if (! state_get_json_string_var (json, "type", blocked_type))
		goto error;

	json_blocked_data = json_object_object_get (json, "data");
	if (! json_blocked_data)
		goto error;

	if (! strcmp (blocked_type, "job")) {
		const char  *job_name;
		const char  *job_class_name;
		Job         *job;

		if (! state_get_json_string_var (json_blocked_data,
					"name", job_name))
			goto error;
		if (! state_get_json_string_var (json_blocked_data,
					"class", job_class_name))
			goto error;

		job = state_names_to_job (job_class_name, job_name);
		if (! job)
			goto error;

		blocked = NIH_MUST (blocked_new (parent, BLOCKED_JOB, job));
		nih_list_add (list, &blocked->entry);

	} else if (! strcmp (blocked_type, "event")) {
		Event  *event = NULL;
		int     event_index;

		if (! state_get_json_int_var (json_blocked_data,
					"index", event_index))
			goto error;

		event = state_index_to_event (event_index);
		if (! event)
			goto error;

		blocked = NIH_MUST (blocked_new (parent, BLOCKED_EVENT, event));
		nih_list_add (list, &blocked->entry);
		event_block (blocked->event);

	} else if (! strcmp (blocked_type, "dbus")) {
		DBusMessage     *message = NULL;
		DBusError        error;
		dbus_uint32_t    serial;
		size_t           raw_len;
		const char      *dbus_message_data_str = NULL;
		nih_local char  *dbus_message_data_raw = NULL;

		if (! state_get_json_string_var (json_blocked_data,
					"msg-data",
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

		dbus_error_init (&error);
		message = dbus_message_demarshal (dbus_message_data_raw,
				(int)raw_len,
				&error);
		if (! message || dbus_error_is_set (&error)) {
			nih_error ("%s: %s",
					_("failed to demarshal D-Bus message"),
					error.message);
			dbus_error_free (&error);
			goto error;
		}

		dbus_message_set_serial (message, serial);

		
#if 1
		/* FIXME: BUG: BUG: BUG!!!! */
		/* FIXME: BUG: BUG: BUG!!!! */
		/* FIXME: BUG: BUG: BUG!!!! */
		/* FIXME: BUG: BUG: BUG!!!! */
		/* FIXME: BUG: BUG: BUG!!!! */
		/* FIXME: BUG: BUG: BUG!!!! */
		/* FIXME: BUG: BUG: BUG!!!! */
#endif
		/* FIXME: type is incorrect!!! */
		blocked = NIH_MUST (blocked_new (parent, BLOCKED_EMIT_METHOD, message));
		nih_list_add (list, &blocked->entry);
	} else {
		nih_assert_not_reached ();
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
 * state_event_to_index:
 *
 * @event: event.
 *
 * Convert an event to an index number within
 * the list of events.
 *
 * Returns: event index, or -1 on error.
 **/
static int
state_event_to_index (const Event *event)
{
	int event_index = 0;
	int found = 0;

	nih_assert (event);
	nih_assert (events);

	NIH_LIST_FOREACH (events, iter) {
		Event *tmp = (Event *)iter;

		if (tmp == event) {
			found = 1;
			break;
		}

		event_index++;
	}

	if (! found)
		return -1;

	return event_index;
}


/**
 * state_index_to_event:
 *
 * @event_index: event index number.
 *
 * Lookup event based on JSON array index number.
 *
 * Returns: existing Job on success, or NULL if Job not found.
 **/
Event *
state_index_to_event (int event_index)
{
	int     i = 0;

	nih_assert (event_index >= 0);
	nih_assert (events);

	NIH_LIST_FOREACH (events, iter) {
		Event *event = (Event *)iter;

		if (i == event_index)
			return event;
		i++;
	}

	return NULL;
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
 * state_names_to_job:
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

/* FIXME: silly function name */
/* FIXME: silly function name */
/* FIXME: silly function name */

Job *
state_names_to_job (const char *job_class, const char *job_name)
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
static char *
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
static int
state_hex_to_data (void         *parent,
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
