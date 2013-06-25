#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include "event_operator.h"

/* Prototypes */
int event_operator_diff (EventOperator *a, EventOperator *b)
	__attribute__ ((warn_unused_result));

Session *session_from_chroot (const char *chroot)
	__attribute__ ((warn_unused_result));

#endif /* TEST_UTIL_H */
