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

#ifndef INIT_PARSE_JOB_H
#define INIT_PARSE_JOB_H

#include <nih/macros.h>

#include "session.h"
#include "job_class.h"


NIH_BEGIN_EXTERN

JobClass *parse_job (const void *parent, Session *session,
		     JobClass *update, const char *name,
		     const char *file, size_t len,
		     size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result, malloc));

NIH_END_EXTERN

#endif /* INIT_PARSE_JOB_H */
