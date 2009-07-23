/* upstart
 *
 * Copyright © 2009 Canonical Ltd.
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

#ifndef INIT_PARSE_CONF_H
#define INIT_PARSE_CONF_H

#include <nih/macros.h>

#include "conf.h"


NIH_BEGIN_EXTERN

int parse_conf (ConfFile *conffile, const char *file, size_t len,
		size_t *pos, size_t *lineno)
	__attribute__ ((warn_unused_result));

NIH_END_EXTERN

#endif /* INIT_PARSE_CONF_H */
