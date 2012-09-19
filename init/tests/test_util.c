#include "test_util.h"

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
	if ((a == b) && !a)
		return 0;

	if (!a || !b)
		return 1;

	if (strcmp (a, b))
		return 1;

	return 0;
}

