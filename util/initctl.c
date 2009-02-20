/* upstart
 *
 * Copyright © 2009 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
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

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/signal.h>
#include <nih/io.h>
#include <nih/main.h>
#include <nih/option.h>
#include <nih/command.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <upstart/enum.h>
#include <upstart/message.h>
#include <upstart/errors.h>


/**
 * ProcInfo:
 * @process: process type,
 * @pid: pid of process.
 *
 * This structure is used to collate information about processes for an
 * individual job information structure.
 **/
typedef struct proc_info {
	ProcessType process;
	pid_t       pid;
} ProcInfo;

/**
 * JobInfo:
 * @id: unique id,
 * @name: name of job,
 * @instance: TRUE if the job is an instance,
 * @goal: current goal,
 * @state: current state,
 * @procs: list of processes,
 * @procs_sz: length of @procs,
 * @jobs: list of instances,
 * @jobs_sz: length of @instances.
 *
 * This structure is used to collate information about instances of jobs
 * and jobs within a larger list.
 **/
typedef struct job_info JobInfo;
struct job_info {
	unsigned int  id;
	char         *name;
	int           instance;

	JobGoal       goal;
	JobState      state;

	ProcInfo     *procs;
	size_t        procs_sz;

	JobInfo     **jobs;
	size_t        jobs_sz;
};


/* Prototypes for static functions */
static int   initctl_send    (UpstartMessageType type, ...);
static int   initctl_recv    (int responses);
static int   job_info_cmp    (const JobInfo **info1, const JobInfo **info2);
static void  job_info_output (const JobInfo *info, int with_name);
static char *output_name     (unsigned int id, const char *name);

/* Prototypes of response handler functions */
static int handle_version          (void *data, pid_t pid,
				    UpstartMessageType type,
				    const char *version);
static int handle_job              (void *data, pid_t pid,
				    UpstartMessageType type,
				    unsigned int id, const char *name);
static int handle_job_finished     (void *data, pid_t pid,
				    UpstartMessageType type,
				    unsigned int id, const char *name,
				    int failed, ProcessType failed_process,
				    int exit_status);
static int handle_job_list         (void *data, pid_t pid,
				    UpstartMessageType type,
				    const char *pattern);
static int handle_job_list_end     (void *data, pid_t pid,
				    UpstartMessageType type,
				    const char *pattern);
static int handle_job_instance     (void *data, pid_t pid,
				    UpstartMessageType type,
				    unsigned int id, char *name);
static int handle_job_instance_end (void *data, pid_t pid,
				    UpstartMessageType type,
				    unsigned int id, const char *name);
static int handle_job_status       (void *data, pid_t pid,
				    UpstartMessageType type,
				    unsigned int id, char *name,
				    JobGoal goal, JobState state);
static int handle_job_process      (void *data, pid_t pid,
				    UpstartMessageType type,
				    ProcessType process, pid_t process_pid);
static int handle_job_status_end   (void *data, pid_t pid,
				    UpstartMessageType type,
				    unsigned int id, const char *name,
				    JobGoal goal, JobState state);
static int handle_job_unknown      (void *data, pid_t pid,
				    UpstartMessageType type,
				    const char *name, unsigned int id);
static int handle_job_invalid      (void *data, pid_t pid,
				    UpstartMessageType type,
				    unsigned int id, const char *name);
static int handle_job_unchanged    (void *data, pid_t pid,
				    UpstartMessageType type,
				    unsigned int id, const char *name);
static int  handle_event           (void *data, pid_t pid,
				    UpstartMessageType type,
				    unsigned int id, const char *name,
				    char * const *args, char * const *env);
static int  handle_event_caused    (void *data, pid_t pid,
				    UpstartMessageType type,
				    unsigned int id);
static int  handle_event_finished  (void *data, pid_t pid,
				    UpstartMessageType type,
				    unsigned int id, int failed,
				    const char *name,
				    char * const *args, char * const *env);
static int  handle_unknown_message (void *data, pid_t pid,
				    UpstartMessageType type, ...);

/* Prototypes for option and command functions */
int env_option          (NihOption *option, const char *arg);
int start_action        (NihCommand *command, char * const *args);
int stop_action         (NihCommand *command, char * const *args);
int status_action       (NihCommand *command, char * const *args);
int list_action         (NihCommand *command, char * const *args);
int emit_action         (NihCommand *command, char * const *args);
int jobs_action         (NihCommand *command, char * const *args);
int events_action       (NihCommand *command, char * const *args);
int version_action      (NihCommand *command, char * const *args);
int log_priority_action (NihCommand *command, char * const *args);


/**
 * control_sock:
 *
 * Control socket opened by the main function for communication with the
 * init daemon.
 **/
int control_sock = -1;

/**
 * destination_pid:
 *
 * Process id to send the message to; nearly always the default of 1.
 **/
int destination_pid = 1;

/**
 * show_ids:
 *
 * Whether to show job and event ids as well as their name.
 **/
int show_ids = FALSE;

/**
 * by_id:
 *
 * Whether to treat job arguments as ids instead of names.
 **/
int by_id = FALSE;

/**
 * no_wait:
 *
 * Whether to wait for a job or event to be finished before exiting or not.
 **/
int no_wait = FALSE;

/**
 * emit_env:
 *
 * Environment variables to emit along with the event.
 **/
char **emit_env = NULL;


/**
 * num_responses:
 *
 * Number of messages received that could be considered responses to requests
 * we've made; this excludes any messages within blocks, etc.
 **/
static size_t num_responses = 0;

/**
 * num_jobs:
 *
 * Number of UPSTART_JOB replies we've received so far, without a terminating
 * UPSTART_JOB_FINISHED reply.  These messages group the non-atomic responses
 * to a request to change the goal of a job, so don't need special handling
 * other than acknowledging them.
 **/
static size_t num_jobs = 0;

/**
 * num_events:
 *
 * Number of UPSTART_EVENT replies we've received so far, without a terminating
 * UPSTART_EVENT_FINISHED reply.  These messages group the non-atomic responses
 * to a request to emit an event, so don't need special handling other than
 * acknowledging them.
 **/
static size_t num_events = 0;

/**
 * event_caused:
 *
 * TRUE if the next message was caused by an event.
 **/
static int event_caused = FALSE;

/**
 * current_list:
 *
 * Array of UPSTART_JOB_STATUS and UPSTART_JOB_INSTANCE replies we've received
 * so far since the last UPSTART_JOB_LIST reply without a terminating
 * UPSTART_JOB_LIST_END reply.
 **/
static JobInfo **current_list = NULL;

/**
 * current_list_sz:
 *
 * Number of entries in current_list.
 **/
static size_t current_list_sz = 0;

/**
 * current_instance:
 *
 * This is set to point to a JobInfo structure if we've received an
 * UPSTART_JOB_INSTANCE reply without a terminating UPSTART_JOB_INSTANCE_END
 * reply.  We collate information about the individual instances with it.
 **/
static JobInfo *current_instance = NULL;

/**
 * current_job:
 *
 * This is set to point to a JobInfo structure if we've received an
 * UPSTART_JOB_STATUS reply without a terminating UPSTART_JOB_STATUS_END reply.
 * We collate information about the job's processes with it.
 **/
static JobInfo *current_job = NULL;


/**
 * handlers:
 *
 * Functions to be called when we receive replies from the server, they
 * either update the global variables with the information received so far
 * or output it with nih_message and clear the variables for the next
 * message.
 **/
static UpstartMessage handlers[] = {
	{ -1, UPSTART_VERSION,
	  (UpstartMessageHandler)handle_version },

	{ -1, UPSTART_JOB,
	  (UpstartMessageHandler)handle_job },
	{ -1, UPSTART_JOB_FINISHED,
	  (UpstartMessageHandler)handle_job_finished },
	{ -1, UPSTART_JOB_LIST,
	  (UpstartMessageHandler)handle_job_list },
	{ -1, UPSTART_JOB_LIST_END,
	  (UpstartMessageHandler)handle_job_list_end },
	{ -1, UPSTART_JOB_INSTANCE,
	  (UpstartMessageHandler)handle_job_instance },
	{ -1, UPSTART_JOB_INSTANCE_END,
	  (UpstartMessageHandler)handle_job_instance_end },
	{ -1, UPSTART_JOB_STATUS,
	  (UpstartMessageHandler)handle_job_status },
	{ -1, UPSTART_JOB_PROCESS,
	  (UpstartMessageHandler)handle_job_process },
	{ -1, UPSTART_JOB_STATUS_END,
	  (UpstartMessageHandler)handle_job_status_end },
	{ -1, UPSTART_JOB_UNKNOWN,
	  (UpstartMessageHandler)handle_job_unknown },
	{ -1, UPSTART_JOB_INVALID,
	  (UpstartMessageHandler)handle_job_invalid },
	{ -1, UPSTART_JOB_UNCHANGED,
	  (UpstartMessageHandler)handle_job_unchanged },

	{ -1, UPSTART_EVENT,
	  (UpstartMessageHandler)handle_event },
	{ -1, UPSTART_EVENT_CAUSED,
	  (UpstartMessageHandler)handle_event_caused },
	{ -1, UPSTART_EVENT_FINISHED,
	  (UpstartMessageHandler)handle_event_finished },

	{ -1, -1,
	  (UpstartMessageHandler)handle_unknown_message },

	UPSTART_MESSAGE_LAST
};


/**
 * initctl_send:
 * @type: command type to send.
 *
 * This function wraps the upstart_message_new() function, ensuring that the
 * control socket is open (and we're root, if we need to) and sends the
 * message to the destination pid on the control socket.
 *
 * Errors are handled by outputting a message with nih_error().
 *
 * Returns: zero on success, negative value on error.
 **/
static int
initctl_send (UpstartMessageType type,
	      ...)
{
	va_list       args;
	NihIoMessage *message;
	NihError     *err;

	/* If this is the first command run, open a socket to the init
	 * daemon so we can communicate with it.  Leave it open after since
	 * we're short-lived anyway.
	 */
	if (control_sock < 0) {
		/* Check we're root */
		setuid (geteuid ());
		if (getuid ()) {
			nih_fatal (_("Need to be root"));
			return -1;
		}

		/* Connect to the daemon */
		control_sock = upstart_open ();
		if (control_sock < 0)
			goto error;
	}


	/* Construct the initial message and send it to the init daemon. */
	va_start (args, type);
	message = upstart_message_newv (NULL, destination_pid, type, args);
	va_end (args);

	if (! message) {
		nih_error_raise_system ();
		goto error;
	}

	if (nih_io_message_send (message, control_sock) < 0)
		goto error;

	nih_free (message);

	return 0;

error:
	err = nih_error_get ();
	nih_fatal (_("Unable to send message: %s"), err->message);
	nih_free (err);

	return -1;
}

/**
 * initctl_recv:
 * @responses: number of responses to expect.
 *
 * This function handles all replies from the init daemon for the commands
 * sent with initctl_send().  Replies are handled with the appropriate
 * function in the handlers table.
 *
 * Responses that consist of multiple replies are handled properly, since
 * the first reply identifies the set and the last one indicates the end
 * of the set.
 *
 * Waiting for all messages in the UPSTART_JOB and UPSTART_EVENT responses
 * can be disabled by setting the no_wait global variable to TRUE.  This
 * does not affect the UPSTART_JOB_LIST, UPSTART_JOB_INSTANCE or
 * UPSTART_JOB_STATUS messages since they are considered to be a single
 * message.
 *
 * Errors are handled by outputting a message with nih_error().  Some
 * responses may suggest a particular exit status for the command, e.g.
 * because a job doesn't exist.  These are returned once all replies are
 * handled.
 *
 * Returns: zero on success, negative value on error or positive value if the
 * command should exit with that exit status.
 **/
static int
initctl_recv (int responses)
{
	NihError *err;
	int       result = 0;

	num_responses = 0;

	while ((responses < 0) || (num_responses < responses)
	       || current_list || current_instance || current_job
	       || ((! no_wait) && (num_jobs || num_events)))
	{
		NihIoMessage *reply;
		size_t        len;
		int           ret;

		reply = nih_io_message_recv (NULL, control_sock, &len);
		if (! reply) {
			err = nih_error_get ();
			nih_fatal (_("Unable to receive message: %s"),
				   err->message);
			nih_free (err);

			return 1;
		}

		ret = upstart_message_handle (reply, reply, handlers, NULL);
		nih_free (reply);

		if (ret < 0) {
			err = nih_error_get ();
			if (err->number == UPSTART_MESSAGE_UNKNOWN) {
				nih_free (err);
				continue;
			}

			nih_fatal (_("Unable to handle message: %s"),
				   err->message);

			nih_free (err);
			return 1;

		} else if (ret > 0)
			result = ret;
	}

	/* Clear the counters that aren't guaranteed to be reset to zero */
	num_jobs = num_events = 0;

	return result;
}


/**
 * job_info_cmp:
 * @info1: first job info,
 * @info2: second job info.
 *
 * Compare two job information structures for alphabetical sorting by name.
 *
 * Returns: an integer less than, equal to, or greater than zero depending
 * whether the name of @info1 is less than, equal to or greater than that
 * of @info2.
 **/
static int
job_info_cmp (const JobInfo **info1,
	      const JobInfo **info2)
{
	nih_assert (info1 != NULL);
	nih_assert (*info1 != NULL);
	nih_assert (info2 != NULL);
	nih_assert (*info2 != NULL);

	return strcmp ((*info1)->name, (*info2)->name);
}

/**
 * job_info_output:
 * @info: job to output,
 * @with_name: include name (and id) in the output.
 *
 * Output information about the given job, which may be either an instance
 * job or a non-instance or instance of a job.
 *
 * The output format is intended to try and present information about a job
 * on a single line, only using additional lines for instances of that job
 * or additional processes.
 **/
static void
job_info_output (const JobInfo *info,
		 int            with_name)
{
	ProcInfo *proc, *proc_main;
	char     *name = NULL, *state;
	size_t    i;

	nih_assert (info != NULL);

	if (with_name)
		name = output_name (info->id, info->name);

	/* That's all that's useful for an instance job, so output it and
	 * then present the instances on lines by themselves.
	 */
	if (info->instance) {
		if (name) {
			nih_message (_("%s (instance)"), name);
			nih_free (name);
		}

		for (i = 0; i < info->jobs_sz; i++)
			job_info_output (info->jobs[i], FALSE);

		return;
	}

	/* All non-instance jobs have a goal and state, so we always include
	 * that in the line; appending it to the name, or a whitespace prefix
	 */
	if (name) {
		state = NIH_MUST (nih_sprintf (NULL, "%s (%s) %s", name,
					       job_goal_name (info->goal),
					       job_state_name (info->state)));
		nih_free (name);
	} else if (show_ids) {
		state = NIH_MUST (nih_sprintf (NULL, "    [#%u] (%s) %s",
					       info->id,
					       job_goal_name (info->goal),
					       job_state_name (info->state)));
	} else {
		state = NIH_MUST (nih_sprintf (NULL, "    (%s) %s",
					       job_goal_name (info->goal),
					       job_state_name (info->state)));
	}

	/* Find an appropriate process id to append to the information; we
	 * optimally put whichever is usually associated with the state,
	 * otherwise we fall back on the main process if there is one.
	 */
	proc = NULL;
	proc_main = NULL;

	for (i = 0; i < info->procs_sz; i++) {
		switch (info->state) {
		case JOB_PRE_START:
			if (info->procs[i].process == PROCESS_PRE_START)
				proc = &(info->procs[i]);
			break;
		case JOB_POST_START:
			if (info->procs[i].process == PROCESS_POST_START)
				proc = &(info->procs[i]);
			break;
		case JOB_PRE_STOP:
			if (info->procs[i].process == PROCESS_PRE_STOP)
				proc = &(info->procs[i]);
			break;
		case JOB_POST_STOP:
			if (info->procs[i].process == PROCESS_POST_STOP)
				proc = &(info->procs[i]);
			break;
		case JOB_SPAWNED:
		case JOB_RUNNING:
		case JOB_STOPPING:
		case JOB_KILLED:
			if (info->procs[i].process == PROCESS_MAIN)
				proc = &(info->procs[i]);
			break;
		default:
			break;
		}

		if (info->procs[i].process == PROCESS_MAIN)
			proc_main = &(info->procs[i]);
	}

	if (proc) {
		proc_main = NULL;
		nih_message (_("%s, process %d"), state, proc->pid);
	} else if (proc_main) {
		nih_message (_("%s, (main) process %d"),
			     state, proc_main->pid);
	} else {
		nih_message ("%s", state);
	}
	nih_free (state);

	/* Finally we output a line for each additional process */
	for (i = 0; i < info->procs_sz; i++) {
		if ((&(info->procs[i]) == proc)
		    || (&(info->procs[i])) == proc_main)
			continue;

		nih_message (_("\t%s process %d"),
			     process_name (info->procs[i].process),
			     info->procs[i].pid);
	}
}

/**
 * output_name:
 * @id: unique id of job or event,
 * @name: name of job or event.
 *
 * This function is used to append the unique id to the job or event name
 * if the --show-ids option is given.
 *
 * Returns: newly allocated string.
 **/
static char *
output_name (unsigned int  id,
	     const char   *name)
{
	char *str;

	nih_assert (name != NULL);

	if (show_ids) {
		str = NIH_MUST (nih_sprintf (NULL, "%s [#%u]", name, id));
	} else {
		str = NIH_MUST (nih_strdup (NULL, name));
	}

	return str;
}


/**
 * handle_version:
 * @data: data passed to handler,
 * @pid: origin of message,
 * @type: message type,
 * @version: package version string.
 *
 * Handles receipt of the UPSTART_VERSION reply from the server, which is
 * sent in response to an UPSTART_VERSION_QUERY message.
 *
 * Output the version string received and mark it as a response.
 *
 * Returns: zero on success, negative value on error or positive value if the
 * command should exit with that exit status.
 **/
static int
handle_version (void               *data,
		pid_t               pid,
		UpstartMessageType  type,
		const char         *version)
{
	nih_assert (pid > 0);
	nih_assert (type == UPSTART_VERSION);
	nih_assert (version != NULL);

	if (! event_caused)
		num_responses++;

	event_caused = FALSE;

	nih_message ("%s", version);

	return 0;
}

/**
 * handle_job:
 * @data: data passed to handler,
 * @pid: origin of message,
 * @type: message type,
 * @id: unique id of job,
 * @name: name of job.
 *
 * Handles receipt of the UPSTART_JOB reply from the server, which begins
 * a response to a change in a job's state ended by an UPSTART_JOB_FINISHED
 * reply.
 *
 * Normally this doesn't require any special handling other than incrementing
 * the counter of job messages we've received, since we don't wait before
 * outputing the status messages within it.  However if the no_wait flag is
 * set, we won't display the status messages so instead output a message
 * indicating that the job was changed.
 *
 * Returns: zero on success, negative value on error or positive value if the
 * command should exit with that exit status.
 **/
static int
handle_job (void               *data,
	    pid_t               pid,
	    UpstartMessageType  type,
	    unsigned int        id,
	    const char         *name)
{
	char *str;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB);
	nih_assert (name != NULL);

	if (! event_caused)
		num_responses++;

	event_caused = FALSE;
	num_jobs++;

	if (no_wait) {
		str = output_name (id, name);
		nih_message (_("%s: goal changed"), str);
		nih_free (str);
	}

	return 0;
}

/**
 * handle_job_finished:
 * @data: data passed to handler,
 * @pid: origin of message,
 * @type: message type,
 * @id: unique id of job,
 * @name: name of job,
 * @failed: whether the job failed,
 * @failed_process: type of process that failed,
 * @exit_status: exit status or signal of failed process.
 *
 * Handles receipt of the UPSTART_JOB_FINISHED reply from the server, which
 * ends a response to a change in a job's state begun by an UPSTART_JOB
 * reply.
 *
 * This decrements the counter of job messages we've received, and if
 * the no_wait flag is set returns normally since we were told not to wait
 * for this kind of message, and just received it by accident.
 *
 * Otherwise we check whether the job exited normally, and if not output
 * a warning and set the exit status to indicate some kind of failure.
 *
 * Returns: zero on success, negative value on error or positive value if the
 * command should exit with that exit status.
 **/
static int
handle_job_finished (void               *data,
		     pid_t               pid,
		     UpstartMessageType  type,
		     unsigned int        id,
		     const char         *name,
		     int                 failed,
		     ProcessType         failed_process,
		     int                 exit_status)
{
	char *str;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB_FINISHED);
	nih_assert (name != NULL);

	num_jobs--;

	if (no_wait || (! failed))
		return 0;

	str = output_name (id, name);

	if (failed_process == -1) {
		nih_warn (_("%s respawning too fast, stopped"), str);
	} else if (exit_status & ~0xff) {
		const char *sig;

		sig = nih_signal_to_name (exit_status >> 8);
		if (sig) {
			nih_warn (_("%s %s process killed by %s signal"),
				  str, process_name (failed_process), sig);
		} else {
			nih_warn (_("%s %s process killed by signal %d"),
				  str, process_name (failed_process),
				  exit_status >> 8);
		}
	} else {
		nih_warn (_("%s %s process terminated with status %d"),
			  str, process_name (failed_process), exit_status);
	}

	nih_free (str);

	return 1;
}


/**
 * handle_job_list:
 * @data: data passed to handler,
 * @pid: origin of message,
 * @type: message type,
 * @pattern: pattern sought or NULL.
 *
 * Handles receipt of the UPSTART_JOB_LIST reply from the server, which begins
 * a response to a request to find jobs matching a certain pattern ended
 * by an UPSTART_JOB_LIST_END reply.
 *
 * The job information immediately follows this message without any
 * possible intermixed messages, so we can treat a list as a single response.
 * Since this is true, we collate the status and instance information so we
 * can sort it.
 *
 * Returns: zero on success, negative value on error or positive value if the
 * command should exit with that exit status.
 **/
static int
handle_job_list (void               *data,
		 pid_t               pid,
		 UpstartMessageType  type,
		 const char         *pattern)
{
	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB_LIST);
	nih_assert (current_list == NULL);

	if (! event_caused)
		num_responses++;

	event_caused = FALSE;

	current_list = NIH_MUST (nih_alloc (NULL, 0));
	current_list_sz = 0;

	return 0;
}

/**
 * handle_job_list_end:
 * @data: data passed to handler,
 * @pid: origin of message,
 * @type: message type,
 * @pattern: pattern sought or NULL.
 *
 * Handles receipt of the UPSTART_JOB_LIST_END reply from the server, which
 * ends a response to a request to find jobs matching a certain pattern begun
 * by an UPSTART_JOB_LIST reply.
 *
 * The list of job status and instances received will have been collated in
 * the current_list variable; we sort this list and return it.
 *
 * Returns: zero on success, negative value on error or positive value if the
 * command should exit with that exit status.
 **/
static int
handle_job_list_end (void               *data,
		     pid_t               pid,
		     UpstartMessageType  type,
		     const char         *pattern)
{
	int ret = 0;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB_LIST_END);
	nih_assert (current_list != NULL);

	if (current_list_sz) {
		size_t i;

		qsort (current_list, current_list_sz, sizeof (JobInfo *),
		       (int (*)(const void *, const void *))job_info_cmp);

		for (i = 0; i < current_list_sz; i++)
			job_info_output (current_list[i], TRUE);

	} else if (pattern) {
		nih_warn (_("No jobs matching `%s'"), pattern);
		ret = 1;

	} else {
		nih_warn (_("No jobs registered"));
		ret = 1;

	}

	/* Reset the list for next time */
	nih_free (current_list);
	current_list = NULL;
	current_list_sz = 0;

	return ret;
}


/**
 * handle_job_instance:
 * @data: data passed to handler,
 * @pid: origin of message,
 * @type: message type,
 * @id: unique id of job,
 * @name: name of job.
 *
 * Handles receipt of the UPSTART_JOB_INSTANCE reply from the server, which
 * begins information about an instance job ended by an
 * UPSTART_JOB_INSTANCE_END reply.
 *
 * The job information immediately follows this message without any
 * possible intermixed messages, so we can treat an instance as a single
 * response.  Since this is true, we collate the information into a single
 * structure before outputting it.
 *
 * Returns: zero on success, negative value on error or positive value if the
 * command should exit with that exit status.
 **/
static int
handle_job_instance (void               *data,
		     pid_t               pid,
		     UpstartMessageType  type,
		     unsigned int        id,
		     char               *name)
{
	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB_INSTANCE);
	nih_assert (name != NULL);
	nih_assert (current_instance == NULL);

	if (current_list == NULL)
		if (! event_caused)
			num_responses++;

	current_instance = NIH_MUST (nih_new (current_list, JobInfo));

	nih_alloc_reparent (name, current_instance);

	current_instance->id = id;
	current_instance->name = name;
	current_instance->instance = TRUE;

	current_instance->goal = JOB_STOP;
	current_instance->state = JOB_WAITING;

	current_instance->procs = NULL;
	current_instance->procs_sz = 0;

	current_instance->jobs = NULL;
	current_instance->jobs_sz = 0;

	return 0;
}

/**
 * handle_job_instance_end:
 * @data: data passed to handler,
 * @pid: origin of message,
 * @type: message type,
 * @id: unique id of job,
 * @name: name of job.
 *
 * Handles receipt of the UPSTART_JOB_INSTANCE_END reply from the server, which
 * ends information about an instance job begun by an UPSTART_JOB_INSTANCE
 * reply.
 *
 * The list of instances of this job will have been collated into the
 * current_instance variable, we output this information in one go unless
 * there's a current_list, in which case we append it to the list.
 *
 * Returns: zero on success, negative value on error or positive value if the
 * command should exit with that exit status.
 **/
static int
handle_job_instance_end (void               *data,
			 pid_t               pid,
			 UpstartMessageType  type,
			 unsigned int        id,
			 const char         *name)
{
	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB_INSTANCE_END);
	nih_assert (name != NULL);
	nih_assert (current_instance != NULL);

	if (current_list) {
		current_list = NIH_MUST (nih_realloc (current_list, NULL,
						      ((current_list_sz + 1)
						       * sizeof (JobInfo *))));
		current_list = new_list;
		current_list[current_list_sz++] = current_instance;
	} else {
		if (! (event_caused && no_wait))
			job_info_output (current_instance, TRUE);

		event_caused = FALSE;

		nih_free (current_instance);
	}

	current_instance = NULL;

	return 0;
}


/**
 * handle_job_status:
 * @data: data passed to handler,
 * @pid: origin of message,
 * @type: message type.
 * @id: unique id of job,
 * @name: name of job,
 * @goal: goal of job,
 * @state: state of job.
 *
 * Handles receipt of the UPSTART_JOB_STATUS reply from the server, which
 * begins information about a non-instance or instance of a job ended by an
 * UPSTART_JOB_STATUS_END reply.
 *
 * Since jobs may have multiple processes, information about them immediately
 * follows this message without any possible intermixed messages, so we can
 * treat job status as a single response.  Since this is true, we collate the
 * information into a single structure before outputting it.
 *
 * Returns: zero on success, negative value on error or positive value if the
 * command should exit with that exit status.
 **/
static int
handle_job_status (void               *data,
		   pid_t               pid,
		   UpstartMessageType  type,
		   unsigned int        id,
		   char               *name,
		   JobGoal             goal,
		   JobState            state)
{
	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB_STATUS);
	nih_assert (name != NULL);
	nih_assert (current_job == NULL);

	if ((current_instance == NULL) && (current_list == NULL))
		if (! (event_caused || num_jobs))
			num_responses++;

	current_job = NIH_MUST (nih_new ((current_instance
					  ? (void *)current_instance
					  : (current_list
					     ? (void *)current_list
					     : NULL)),
					 JobInfo));

	nih_alloc_reparent (name, current_job);

	current_job->id = id;
	current_job->name = name;
	current_job->instance = FALSE;

	current_job->goal = goal;
	current_job->state = state;

	current_job->procs = NULL;
	current_job->procs_sz = 0;

	current_job->jobs = NULL;
	current_job->jobs_sz = 0;

	return 0;
}

/**
 * handle_job_process:
 * @data: data passed to handler,
 * @pid: origin of message,
 * @type: message type,
 * @process: type of process,
 * @process_pid: process id.
 *
 * Handles receipt of the UPSTART_JOB_PROCESS reply from the server, which
 * provides information about a process running for the current job.
 *
 * We append this information to that of the current job.
 *
 * Returns: zero on success, negative value on error or positive value if the
 * command should exit with that exit status.
 **/
static int
handle_job_process (void               *data,
		    pid_t               pid,
		    UpstartMessageType  type,
		    ProcessType         process,
		    pid_t               process_pid)
{
	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB_PROCESS);
	nih_assert (process_pid > 0);
	nih_assert (current_job != NULL);

	current_job->procs = NIH_MUST (nih_realloc (current_job->procs, current_job,
						    ((current_job->procs_sz + 1)
						     * sizeof (ProcInfo))));
	current_job->procs[current_job->procs_sz].process = process;
	current_job->procs[current_job->procs_sz].pid = process_pid;

	current_job->procs_sz++;

	return 0;
}

/**
 * handle_job_status_end:
 * @data: data passed to handler,
 * @pid: origin of message,
 * @type: message type,
 * @id: unique id of job,
 * @name: name of job.
 *
 * Handles receipt of the UPSTART_JOB_STATUS_END reply from the server, which
 * ends information about a non-instance or instance of a job begun by an
 * UPSTART_JOB_STATUS reply.
 *
 * The list of processes of this job will have been collated into the
 * current_job variable, we output this information in one go unless
 * there's a current_instance or current_list, in which case we append it
 * to the instance or list.
 *
 * Returns: zero on success, negative value on error or positive value if the
 * command should exit with that exit status.
 **/
static int
handle_job_status_end (void               *data,
		       pid_t               pid,
		       UpstartMessageType  type,
		       unsigned int        id,
		       const char         *name,
		       JobGoal             goal,
		       JobState            state)
{
	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB_STATUS_END);
	nih_assert (name != NULL);
	nih_assert (current_job != NULL);

	if (current_instance) {
		current_instance->jobs = NIH_MUST (nih_realloc (current_instance->jobs,
								current_instance,
								((current_instance->jobs_sz + 1)
								 * sizeof (JobInfo *))));
		current_instance->jobs[current_instance->jobs_sz++]
			= current_job;
	} else if (current_list) {
		current_list = NIH_MUST (nih_realloc (current_list, NULL,
						      ((current_list_sz + 1)
						       * sizeof (JobInfo *))));
		current_list[current_list_sz++] = current_job;
	} else {
		if (! ((event_caused || num_jobs) && no_wait))
			job_info_output (current_job, TRUE);

		event_caused = FALSE;

		nih_free (current_job);
	}

	current_job = NULL;

	return 0;
}


/**
 * handle_job_unknown:
 * @data: data passed to handler,
 * @pid: origin of message,
 * @type: message type,
 * @name: name of job,
 * @id: unique id of job, if @name is NULL.
 *
 * Handles receipt of the UPSTART_JOB_UNKNOWN reply from the server, which
 * indicates that a job named in a goal change or query message was not
 * known.
 *
 * We handle this by outputting an error with nih_warn() and returning to
 * indicate that we should fail with a non-zero exit status.
 *
 * Returns: zero on success, negative value on error or positive value if the
 * command should exit with that exit status.
 **/
static int
handle_job_unknown (void               *data,
		    pid_t               pid,
		    UpstartMessageType  type,
		    const char         *name,
		    unsigned int        id)
{
	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB_UNKNOWN);

	if (! event_caused)
		num_responses++;

	event_caused = FALSE;

	if (name) {
		nih_warn (_("Unknown job: %s"), name);
	} else {
		nih_warn (_("Unknown job: #%d"), id);
	}

	return 1;
}

/**
 * handle_job_invalid:
 * @data: data passed to handler,
 * @pid: origin of message,
 * @type: message type,
 * @name: name of job,
 * @id: unique id of job, if @name is NULL.
 *
 * Handles receipt of the UPSTART_JOB_INVALID reply from the server, which
 * indicates that a job named in a goal change or query message was not
 * valid in that context.
 *
 * We handle this by outputting an error with nih_warn() and returning to
 * indicate that we should fail with a non-zero exit status.
 *
 * Returns: zero on success, negative value on error or positive value if the
 * command should exit with that exit status.
 **/
static int
handle_job_invalid (void               *data,
		    pid_t               pid,
		    UpstartMessageType  type,
		    unsigned int        id,
		    const char         *name)
{
	char *str;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB_INVALID);
	nih_assert (name != NULL);

	if (! event_caused)
		num_responses++;

	event_caused = FALSE;

	str = output_name (id, name);
	nih_warn (_("Invalid job: %s"), str);
	nih_free (str);

	return 1;
}

/**
 * handle_job_unknown:
 * @data: data passed to handler,
 * @pid: origin of message,
 * @type: message type,
 * @name: name of job,
 * @id: unique id of job, if @name is NULL.
 *
 * Handles receipt of the UPSTART_JOB_UNCHANGED reply from the server, which
 * indicates that a job named in a goal change request was already started
 * or stopped.
 *
 * We handle this by outputting an error with nih_warn(), but we don't
 * return an exit status since the job is doing what they wanted.
 *
 * Returns: zero on success, negative value on error or positive value if the
 * command should exit with that exit status.
 **/
static int
handle_job_unchanged (void               *data,
		      pid_t               pid,
		      UpstartMessageType  type,
		      unsigned int        id,
		      const char         *name)
{
	char *str;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB_UNCHANGED);
	nih_assert (name != NULL);

	if (! event_caused)
		num_responses++;

	event_caused = FALSE;

	str = output_name (id, name);
	nih_warn (_("Job not changed: %s"), str);
	nih_free (str);

	return 0;
}


/**
 * handle_event:
 * @data: data passed to handler,
 * @pid: origin of message,
 * @type: message type,
 * @id: unique id of event
 * @name: name of event,
 * @args: arguments to event,
 * @env: environment for event.
 *
 * Handles receipt of the UPSTART_EVENT reply from the server, which begins
 * a response to an event emission ended by an UPSTART_EVENT_FINISHED reply.
 *
 * We output the event information and increment the counter of event messages
 * we're received.
 *
 * Returns: zero on success, negative value on error or positive value if the
 * command should exit with that exit status.
 **/
static int
handle_event (void               *data,
	      pid_t               pid,
	      UpstartMessageType  type,
	      unsigned int        id,
	      const char         *name,
	      char * const *      args,
	      char * const *      env)
{
	char         *msg;
	char * const *ptr;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_EVENT);
	nih_assert (name != NULL);

	if (! event_caused)
		num_responses++;

	event_caused = FALSE;
	num_events++;

	msg = output_name (id, name);

	for (ptr = args; ptr && *ptr; ptr++) {
		msg = NIH_MUST (nih_realloc (msg, NULL, (strlen (msg)
							 + strlen (*ptr)
							 + 2)));
		strcat (msg, " ");
		strcat (msg, *ptr);
	}

	nih_message ("%s", msg);
	nih_free (msg);

	for (ptr = env; ptr && *ptr; ptr++)
		nih_message ("    %s", *ptr);

	return 0;
}

/**
 * handle_event_caused:
 * @data: data passed to handler,
 * @pid: origin of message,
 * @type: message type,
 * @id: unique id of event.
 *
 * Handles receipt of the UPSTART_EVENT_CAUSED reply from the server, which
 * indicates that the next reply was caused by the emission of the event id
 * given.
 *
 * We set the event_caused flag, which ensures that the next message doesn't
 * consider itself a response to a command and doesn't output anything.
 *
 * Returns: zero on success, negative value on error or positive value if the
 * command should exit with that exit status.
 **/
static int
handle_event_caused (void               *data,
		     pid_t               pid,
		     UpstartMessageType  type,
		     unsigned int        id)
{
	nih_assert (pid > 0);
	nih_assert (type == UPSTART_EVENT_CAUSED);

	event_caused = TRUE;

	return 0;
}

/**
 * handle_event_finished:
 * @data: data passed to handler,
 * @pid: origin of message,
 * @type: message type,
 * @id: unique id of event,
 * @failed: whether the event failed,
 * @name: name of event,
 * @args: arguments to event,
 * @env: environment for event.
 *
 * Handles receipt of the UPSTART_EVENT_FINISHED reply from the server, which
 * ends a response to an event emission state begun by an UPSTART_EVENT
 * reply.
 *
 * This decrements the counter of event messages we've received, and if
 * the no_wait flag is set returns normally since we were told not to wait
 * for this kind of message, and just received it by accident.
 *
 * Otherwise we check whether the event failed, and if it did output
 * a warning and set the exit status to indicate some kind of failure.
 *
 * Returns: zero on success, negative value on error or positive value if the
 * command should exit with that exit status.
 **/
static int
handle_event_finished (void               *data,
		       pid_t               pid,
		       UpstartMessageType  type,
		       unsigned int        id,
		       int                 failed,
		       const char         *name,
		       char * const *      args,
		       char * const *      env)
{
	char *str;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_EVENT_FINISHED);
	nih_assert (name != NULL);

	num_events--;

	if (no_wait || (! failed))
		return 0;

	str = output_name (id, name);
	nih_warn (_("%s event failed"), str);
	nih_free (str);

	return 1;
}

/**
 * handle_unknown_message:
 * @data: data passed to handler,
 * @pid: origin of message,
 * @type: message type.
 *
 * Handles receipt of an unknown reply from the server.  This is likely
 * because the init daemon is of a later version than the initctl tool.
 *
 * We largely ignore unknown messages, except we take care to reset the
 * event_caused flag, and increase the number of responses, since something
 * clearly happened and we just don't know what.
 *
 * Returns: zero on success, negative value on error or positive value if the
 * command should exit with that exit status.
 **/
static int
handle_unknown_message (void               *data,
			pid_t               pid,
			UpstartMessageType  type,
			...)
{
	nih_assert (pid > 0);

	if (! event_caused)
		num_responses++;

	event_caused = FALSE;

	return 0;
}


/**
 * env_option:
 * @option: NihOption invoked,
 * @arg: argument to parse.
 *
 * This option setter is used to append @arg to the list of environment
 * variables pointed to by the value member of option, which must be a
 * pointer to a char **.
 *
 * If @arg does not contain an '=', the current value from the environment
 * is taken instead.
 *
 * The arg_name member of @option must not be NULL.
 *
 * Returns: zero on success, non-zero on error.
 **/
int
env_option (NihOption  *option,
	    const char *arg)
{
	char ***value;

	nih_assert (option != NULL);
	nih_assert (option->value != NULL);
	nih_assert (arg != NULL);

	value = (char ***)option->value;

	if (strchr (arg, '=')) {
		NIH_MUST (nih_str_array_add (value, NULL, NULL, arg));
	} else {
		char *env, *new_arg;

		env = getenv (arg);
		if (env) {
			new_arg = NIH_MUST (nih_sprintf (NULL, "%s=%s",
							 arg, env));
			NIH_MUST (nih_str_array_addp (value, NULL, NULL,
						      new_arg));
		}
	}

	return 0;
}


/**
 * start_action:
 * @command: NihCommand invoked,
 * @args: command-line arguments.
 *
 * This function is called for the "start" command; it takes one or more
 * arguments that give the names, or ids, of jobs that should be started.
 * If no arguments are given, the UPSTART_JOB_ID or UPSTART_JOB environment
 * variables are used in preferences.
 *
 * Returns: command exit status.
 **/
int
start_action (NihCommand   *command,
	      char * const *args)
{
	char * const *arg;
	size_t        responses = 0;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	/* If no arguments are given, check the UPSTART_JOB_ID and UPSTART_JOB
	 * environment variables instead.
	 */
	if (! args[0]) {
		char *env;

		if ((env = getenv ("UPSTART_JOB_ID")) != NULL) {
			char   *endptr;
			size_t  job_id;

			job_id = strtoul (env, &endptr, 10);
			if (*endptr) {
				fprintf (stderr, _("%s: invalid job id: %s\n"),
					 program_name, env);
				nih_main_suggest_help ();
				return 1;
			}

			no_wait = TRUE;

			if (initctl_send (UPSTART_JOB_START, NULL, job_id) < 0)
				return 1;

			responses++;

		} else if ((env = getenv ("UPSTART_JOB")) != NULL) {
			no_wait = TRUE;

			if (initctl_send (UPSTART_JOB_START, env, 0) < 0)
				return 1;

			responses++;

		} else {
			fprintf (stderr, _("%s: missing job name\n"),
				 program_name);
			nih_main_suggest_help ();
			return 1;
		}
	}

	/* Send a START message for all arguments */
	for (arg = args; *arg; arg++) {
		if (by_id) {
			char   *endptr;
			size_t  job_id;

			job_id = strtoul (*arg, &endptr, 10);
			if (*endptr) {
				fprintf (stderr, _("%s: invalid job id: %s\n"),
					 program_name, *arg);
				nih_main_suggest_help ();
				return 1;
			}

			if (initctl_send (UPSTART_JOB_START, NULL, job_id) < 0)
				return 1;
		} else {
			if (initctl_send (UPSTART_JOB_START, *arg, 0) < 0)
				return 1;
		}

		responses++;
	}

	return initctl_recv (responses);
}

/**
 * stop_action:
 * @command: NihCommand invoked,
 * @args: command-line arguments.
 *
 * This function is called for the "stop" command; it takes one or more
 * arguments that give the names, or ids, of jobs that should be stopped.
 * If no arguments are given, the UPSTART_JOB_ID or UPSTART_JOB environment
 * variables are used in preferences.
 *
 * Returns: command exit status.
 **/
int
stop_action (NihCommand   *command,
	     char * const *args)
{
	char * const *arg;
	size_t        responses = 0;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	/* If no arguments are given, check the UPSTART_JOB_ID and UPSTART_JOB
	 * environment variables instead.
	 */
	if (! args[0]) {
		char *env;

		if ((env = getenv ("UPSTART_JOB_ID")) != NULL) {
			char   *endptr;
			size_t  job_id;

			job_id = strtoul (env, &endptr, 10);
			if (*endptr) {
				fprintf (stderr, _("%s: invalid job id: %s\n"),
					 program_name, env);
				nih_main_suggest_help ();
				return 1;
			}

			no_wait = TRUE;

			if (initctl_send (UPSTART_JOB_STOP, NULL, job_id) < 0)
				return 1;

			responses++;

		} else if ((env = getenv ("UPSTART_JOB")) != NULL) {
			no_wait = TRUE;

			if (initctl_send (UPSTART_JOB_STOP, env, 0) < 0)
				return 1;

			responses++;

		} else {
			fprintf (stderr, _("%s: missing job name\n"),
				 program_name);
			nih_main_suggest_help ();
			return 1;
		}
	}

	/* Send a STOP message for all arguments */
	for (arg = args; *arg; arg++) {
		if (by_id) {
			char   *endptr;
			size_t  job_id;

			job_id = strtoul (*arg, &endptr, 10);
			if (*endptr) {
				fprintf (stderr, _("%s: invalid job id: %s\n"),
					 program_name, *arg);
				nih_main_suggest_help ();
				return 1;
			}

			if (initctl_send (UPSTART_JOB_STOP, NULL, job_id) < 0)
				return 1;
		} else {
			if (initctl_send (UPSTART_JOB_STOP, *arg, 0) < 0)
				return 1;
		}

		responses++;
	}

	return initctl_recv (responses);
}

/**
 * status_action:
 * @command: NihCommand invoked,
 * @args: command-line arguments.
 *
 * This function is called for the "status" command; it takes one or more
 * arguments that give the names, or ids, of jobs that should be queried
 * for their current status.
 *
 * Returns: command exit status.
 **/
int
status_action (NihCommand   *command,
	       char * const *args)
{
	char * const *arg;
	size_t        responses = 0;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	if (! args[0]) {
		fprintf (stderr, _("%s: missing job name\n"), program_name);
		nih_main_suggest_help ();
		return 1;
	}

	for (arg = args; *arg; arg++) {
		if (by_id) {
			char   *endptr;
			size_t  job_id;

			job_id = strtoul (*arg, &endptr, 10);
			if (*endptr) {
				fprintf (stderr, _("%s: invalid job id: %s\n"),
					 program_name, *arg);
				nih_main_suggest_help ();
				return 1;
			}

			if (initctl_send (UPSTART_JOB_QUERY, NULL, job_id) < 0)
				return 1;
		} else {
			if (initctl_send (UPSTART_JOB_QUERY, *arg, 0) < 0)
				return 1;
		}

		responses++;
	}

	return initctl_recv (responses);
}

/**
 * list_action:
 * @command: NihCommand invoked,
 * @args: command-line arguments.
 *
 * This function is called for the "list" command; it takes an optional
 * pattern argument and returns a list of jobs matching that argument, or
 * all jobs if the argument is not given.
 *
 * Returns: command exit status.
 **/
int
list_action (NihCommand   *command,
	     char * const *args)
{
	nih_assert (command != NULL);
	nih_assert (args != NULL);

	if (initctl_send (UPSTART_JOB_FIND, args[0]) < 0)
		return 1;

	return initctl_recv (1);
}

/**
 * emit_action:
 * @command: NihCommand invoked,
 * @args: command-line arguments.
 *
 * This function is called for the "emit" command; it takes an event name
 * followed by optional arguments, and also uses the emit_env option (-e)
 * to supply environment variables for the event.
 *
 * It causes that event to be emitted, and outputs job status changes caused
 * by the event until the event has been handled, unless no_wait is set.
 *
 * Returns: command exit status.
 **/
int
emit_action (NihCommand   *command,
	     char * const *args)
{
	nih_assert (command != NULL);
	nih_assert (args != NULL);

	if (! args[0]) {
		fprintf (stderr, _("%s: missing event name\n"), program_name);
		nih_main_suggest_help ();
		return 1;
	}

	if (initctl_send (UPSTART_EVENT_EMIT, args[0],
			  args[1] ? &(args[1]) : NULL, emit_env) < 0)
		return 1;

	return initctl_recv (1);
}

/**
 * jobs_action:
 * @command: NihCommand invoked,
 * @args: command-line arguments.
 *
 * This function is called for the "jobs" command.  It takes no arguments,
 * and subscribes itself to notification of job status changes; outputting
 * each one until the command is terminated.
 *
 * Returns: command exit status.
 **/
int
jobs_action (NihCommand   *command,
	     char * const *args)
{
	nih_assert (command != NULL);
	nih_assert (args != NULL);

	if (initctl_send (UPSTART_SUBSCRIBE_JOBS) < 0)
		return 1;

	return initctl_recv (-1);
}

/**
 * events_action:
 * @command: NihCommand invoked,
 * @args: command-line arguments.
 *
 * This function is called for the "events" command.  It takes no arguments,
 * and subscribes itself to notification of event emissions; outputting
 * each one until the command is terminated.
 *
 * Returns: command exit status.
 **/
int
events_action (NihCommand   *command,
	       char * const *args)
{
	nih_assert (command != NULL);
	nih_assert (args != NULL);

	if (initctl_send (UPSTART_SUBSCRIBE_EVENTS) < 0)
		return 1;

	return initctl_recv (-1);
}

/**
 * version_action:
 * @command: NihCommand invoked,
 * @args: command-line arguments.
 *
 * This function is called for the "version" command; it requests the
 * package version string from the init daemon and outputs the response.
 *
 * Returns: command exit status.
 **/
int
version_action (NihCommand   *command,
		char * const *args)
{
	nih_assert (command != NULL);
	nih_assert (args != NULL);

	if (initctl_send (UPSTART_VERSION_QUERY) < 0)
		return 1;

	return initctl_recv (1);
}

/**
 * log_priority_action:
 * @command: NihCommand invoked,
 * @args: command-line arguments.
 *
 * This function is called for the "log-priority" command; it takes a string
 * log priority which it converts into a NihLogLevel to be sent to the server.
 * No response is expected.
 *
 * Returns: command exit status.
 **/
int
log_priority_action (NihCommand   *command,
		     char * const *args)
{
	NihLogLevel priority;

	nih_assert (command != NULL);
	nih_assert (args != NULL);

	if (! args[0]) {
		fprintf (stderr, _("%s: missing priority\n"), program_name);
		nih_main_suggest_help ();
		return 1;
	}

	if (! strcmp (args[0], "debug")) {
		priority = NIH_LOG_DEBUG;
	} else if (! strcmp (args[0], "info")) {
		priority = NIH_LOG_INFO;
	} else if (! strcmp (args[0], "message")) {
		priority = NIH_LOG_MESSAGE;
	} else if (! strcmp (args[0], "warn")) {
		priority = NIH_LOG_WARN;
	} else if (! strcmp (args[0], "error")) {
		priority = NIH_LOG_ERROR;
	} else if (! strcmp (args[0], "fatal")) {
		priority = NIH_LOG_FATAL;
	} else {
		fprintf (stderr, _("%s: invalid priority\n"), program_name);
		nih_main_suggest_help ();
		return 1;
	}

	if (initctl_send (UPSTART_LOG_PRIORITY, priority) < 0)
		return 1;

	return 0;
}


#ifndef TEST
/**
 * options:
 *
 * Command-line options accepted for all arguments.
 **/
static NihOption options[] = {
	{ 'p', "pid", N_("destination process"),
	  NULL, "PID", &destination_pid, nih_option_int },

	NIH_OPTION_LAST
};


/**
 * start_options:
 *
 * Command-line options accepted for the start command.
 **/
NihOption start_options[] = {
	{ 'i', "id", N_("arguments are job ids, instead of names"),
	  NULL, NULL, &by_id, NULL },
	{ 0, "show-ids", N_("show job ids, as well as names"),
	  NULL, NULL, &show_ids, NULL },
	{ 'n', "no-wait", N_("do not wait for job to start before exiting"),
	  NULL, NULL, &no_wait, NULL },

	NIH_OPTION_LAST
};

/**
 * stop_options:
 *
 * Command-line options accepted for the stop command.
 **/
NihOption stop_options[] = {
	{ 'i', "id", N_("arguments are job ids, instead of names"),
	  NULL, NULL, &by_id, NULL },
	{ 0, "show-ids", N_("show job ids, as well as names"),
	  NULL, NULL, &show_ids, NULL },
	{ 'n', "no-wait", N_("do not wait for job to stop before exiting"),
	  NULL, NULL, &no_wait, NULL },

	NIH_OPTION_LAST
};

/**
 * status_options:
 *
 * Command-line options accepted for the status command.
 **/
NihOption status_options[] = {
	{ 'i', "id", N_("arguments are job ids, instead of names"),
	  NULL, NULL, &by_id, NULL },
	{ 0, "show-ids", N_("show job ids, as well as names"),
	  NULL, NULL, &show_ids, NULL },

	NIH_OPTION_LAST
};

/**
 * list_options:
 *
 * Command-line options accepted for the list command.
 **/
NihOption list_options[] = {
	{ 0, "show-ids", N_("show job ids, as well as names"),
	  NULL, NULL, &show_ids, NULL },

	NIH_OPTION_LAST
};

/**
 * emit_options:
 *
 * Command-line options accepted for the emit command.
 **/
NihOption emit_options[] = {
	{ 0, "show-ids", N_("show job and event ids, as well as names"),
	  NULL, NULL, &show_ids, NULL },
	{ 'n', "no-wait", N_("do not wait for event to finish before exiting"),
	  NULL, NULL, &no_wait, NULL },
	{ 'e', NULL, N_("set environment variable in jobs changed by this event"),
	  NULL, "NAME[=VALUE]", &emit_env, env_option },

	NIH_OPTION_LAST
};

/**
 * jobs_options:
 *
 * Command-line options accepted for the jobs command.
 **/
NihOption jobs_options[] = {
	{ 0, "show-ids", N_("show job ids, as well as names"),
	  NULL, NULL, &show_ids, NULL },

	NIH_OPTION_LAST
};

/**
 * events_options:
 *
 * Command-line options accepted for the events command.
 **/
NihOption events_options[] = {
	{ 0, "show-ids", N_("show event ids, as well as names"),
	  NULL, NULL, &show_ids, NULL },

	NIH_OPTION_LAST
};

/**
 * version_options:
 *
 * Command-line options accepted for the version command.
 **/
NihOption version_options[] = {
	NIH_OPTION_LAST
};

/**
 * log_priority_options:
 *
 * Command-line options accepted for the log-priority command.
 **/
NihOption log_priority_options[] = {
	NIH_OPTION_LAST
};


/**
 * job_group:
 *
 * Group of commands related to jobs.
 **/
static NihCommandGroup job_commands = { N_("Job") };

/**
 * event_group:
 *
 * Group of commands related to events.
 **/
static NihCommandGroup event_commands = { N_("Event") };

/**
 * commands:
 *
 * Commands accepts as the first non-option argument, or program name.
 **/
static NihCommand commands[] = {
	{ "start", N_("JOB..."),
	  N_("Start jobs."),
	  N_("JOB is one or more job names that are to be started.\n\n"
	     "Alternatively if --by-id is given, JOB is one or more numeric "
	     "job ids uniquely identifying a particular instance of a job."),
	  &job_commands, start_options, start_action },

	{ "stop", N_("JOB..."),
	  N_("Stop jobs."),
	  N_("JOB is one or more job names that are to be stopped.\n\n"
	     "Alternatively if --by-id is given, JOB is one or more numeric "
	     "job ids uniquely identifying a particular instance of a job."),
	  &job_commands, stop_options, stop_action },

	{ "status", N_("JOB..."),
	  N_("Query status of jobs."),
	  N_("JOB is one or more job names that are to be queried.\n\n"
	     "Alternatively if --by-id is given, JOB is one or more numeric "
	     "job ids uniquely identifying a particular instance of a job."),
	  &job_commands, status_options, status_action },

	{ "list", NULL,
	  N_("List known jobs."),
	  NULL,
	  &job_commands, list_options, list_action },

	{ "emit", N_("EVENT [ARG]..."),
	  N_("Emit an event."),
	  N_("EVENT is the name of an event the init daemon should emit, "
	     "which may have zero or more arguments specified by ARG.  These "
	     "may be matched in the job definition, and are passed to any "
	     "scripts run by the job.\n\n"
	     "Events may also pass environment variables to the job scripts, "
	     "defined using -e.  A value may be specified in the option, or "
	     "if omitted, the value is taken from the environment or ignored "
	     "if not present there."),
	  &event_commands, emit_options, emit_action },

	{ "jobs", NULL,
	  N_("Receive notification of job state changes."),
	  NULL,
	  &job_commands, jobs_options, jobs_action },

	{ "events", NULL,
	  N_("Receive notification of emitted events."),
	  NULL,
	  &event_commands, events_options, events_action },

	{ "version", NULL,
	  N_("Request the version of the init daemon."),
	  NULL,
	  NULL, version_options, version_action },
	{ "log-priority", N_("PRIORITY"),
	  N_("Change the minimum priority of log messages from the init "
	     "daemon"),
	  N_("PRIORITY may be one of "
	     "`debug' (messages useful for debugging upstart are logged, "
	     "equivalent to --debug on kernel command-line); "
	     "`info' (messages about job goal and state changes, as well "
	     "as event emissions are logged, equivalent to --verbose on the "
	     "kernel command-line); "
	     "`message' (informational and debugging messages are suppressed, "
	     "the default); "
	     "`warn' (ordinary messages are suppressed whilst still "
	     "logging warnings and errors); "
	     "`error' (only errors are logged, equivalent to --quiet on "
	     "the kernel command-line) or "
	     "`fatal' (only fatal errors are logged)."),
	  NULL, log_priority_options, log_priority_action },

	NIH_COMMAND_LAST
};


int
main (int   argc,
      char *argv[])
{
	int ret;

	nih_main_init (argv[0]);

	ret = nih_command_parser (NULL, argc, argv, options, commands);
	if (ret < 0)
		exit (1);

	return ret;
}
#endif
