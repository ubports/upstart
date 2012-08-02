/* upstart
 *
 * Copyright © 2010 Canonical Ltd.
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

#ifndef INIT_SYSTEM_H
#define INIT_SYSTEM_H

#include <sys/types.h>

#include <nih/macros.h>

#include "job_class.h"


NIH_BEGIN_EXTERN

int system_kill          (pid_t pid, int signal)
	__attribute__ ((warn_unused_result));

int system_setup_console (ConsoleType type, int reset)
	__attribute__ ((warn_unused_result));

int system_mount         (const char *type, const char *dir,
			  unsigned long flags)
	__attribute__ ((warn_unused_result));

NIH_END_EXTERN

#endif /* INIT_SYSTEM_H */
