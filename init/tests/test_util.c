#include "test_util.h"
#include <nih/logging.h>
#include <nih/test.h>

/**
 * string_check:
 *
 * @a: first string,
 * @b: second string.
 *
 * Compare @a and @b either or both of which may be NULL.
 *
 * Returns 0 if strings are identical or both NULL, else 1.
 **/
int
string_check (const char *a, const char *b)
{
	if (!a && !b)
		return 0;

	if (!a || !b)
		return 1;

	if (strcmp (a, b))
		return 1;

	return 0;
}

/**
 * event_operator_diff:
 * @a: first Blocked,
 * @b: second Blocked,
 * @seen: object type that has already been seen.
 *
 * Compare two Blocked objects for equivalence.
 *
 * Returns: 0 if @a and @b are identical, else 1.
 **/
int
event_operator_diff (EventOperator *a, EventOperator *b)
{
	if (! a && ! b)
		return 0;

	if ((! a && b) || (a && ! b))
		return 1;

	TEST_TWO_TREES_FOREACH (&a->node, &b->node, iter_a, iter_b) {
		EventOperator   *opera = (EventOperator *)iter_a;
		EventOperator   *operb = (EventOperator *)iter_b;
		nih_local char  *env_a = NULL;
		nih_local char  *env_b = NULL;

		if ((! opera && operb) || (! operb && opera))
			return 1;

		TEST_NE_P (opera, NULL);
		TEST_NE_P (operb, NULL);

		if (opera->type != operb->type)
			return 1;

		if (opera->value != operb->value)
			return 1;

		if (opera->type == EVENT_MATCH && opera->env) {
			env_a = state_collapse_env ((const char **)opera->env);
			if (! env_a)
				return 1;
		}

		if (operb->type == EVENT_MATCH && operb->env) {
			env_b = state_collapse_env ((const char **)operb->env);
			if (! env_b)
				return 1;
		}

		if (string_check (env_a, env_b))
			return 1;

		if (opera->event != operb->event)
			return 1;
	}

	return 0;
}

/**
 * session_from_chroot:
 * @chroot: full path to chroot.
 *
 * Obtain the session relating to the specified chroot.
 *
 * Returns: Session, or NULL if no session found.
 **/
Session *
session_from_chroot (const char *chroot)
{
	nih_assert (chroot);

	session_init ();

	NIH_LIST_FOREACH (sessions, iter) {
		Session *session = (Session *)iter;

		if (! strcmp (session->chroot, chroot))
				return session;
	}

	return NULL;
}
