#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include "event_operator.h"
#include "conf.h"
#include "job_class.h"
#include "session.h"

/* Prototypes */
int event_operator_diff (EventOperator *a, EventOperator *b)
	__attribute__ ((warn_unused_result));

Session *session_from_chroot (const char *chroot)
	__attribute__ ((warn_unused_result));

void ensure_env_clean (void);

#endif /* TEST_UTIL_H */
