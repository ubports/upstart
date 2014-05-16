NihList *pid_list;
pid_t main_pid;
int main_pid_test = FALSE;
int exit_main_loop_counter = 0;
int main_pid_exited = FALSE;

typedef struct test_list_entry {
	NihList         entry;
	pid_t           pid;
	NihChildEvents  event;
	int             status;
} TestListEntry;

/**
 * test_job_process_handler:
 *
 * @data: existing NihList that this function will add entries to,
 * @pid: process that changed,
 * @event: event that occurred on the child,
 * @status: exit status, signal raised or ptrace event.
 *
 * Handler that just sets some globals and requests the main loop to
 * exit to allow the test that installs it to check the values passed to
 * this function as appropriate.
 **/
void
test_job_process_handler (void           *data,
			  pid_t           pid,
			  NihChildEvents  event,
			  int             status)
{
	int            max;
	TestListEntry  *entry;
	int             count = 0;
	Job            *job;
	ProcessType     process;


	nih_assert (pid_list);

        if (data)
            max = *(int *)data;
        else
            max = 1;

	nih_assert (max > 0);

	if (main_pid_test) {

		job = job_process_find (pid, &process);
		TEST_NE_P (job, NULL);

		if (process == PROCESS_MAIN && event == NIH_CHILD_STOPPED && ! main_pid) {
			/* Main process has been stopped, so record its pid for
			 * later.
			 */
			main_pid = pid;
			return;
		}

		if (process == PROCESS_MAIN && event == NIH_CHILD_EXITED && main_pid_test)
			main_pid_exited = TRUE;

		if (process == PROCESS_POST_START && event == NIH_CHILD_EXITED) {
			if (main_pid) {
				fflush (NULL);
				assert0 (kill (main_pid, SIGCONT));
			}
		}
	}

	entry = nih_new (pid_list, TestListEntry);
	TEST_NE_P (entry, NULL);

	nih_list_init (&entry->entry);
	nih_alloc_set_destructor (entry, nih_list_destroy);

	entry->pid = pid;
	entry->event = event;
	entry->status = status;

	nih_list_add (pid_list, &entry->entry);

	fflush (NULL);

	NIH_LIST_FOREACH (pid_list, iter) {
		count++;
	}

	if (count == max) {
		fflush (NULL);
		if (! main_pid_test) {
			nih_main_loop_exit (0);
		}
	}
}
