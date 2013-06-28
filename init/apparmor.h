/* upstart
 *
 * Copyright © 2013 Canonical Ltd.
 * Author: Marc Deslauriers <marc.deslauriers@canonical.com>.
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

#ifndef INIT_APPARMOR_H
#define INIT_APPARMOR_H

#include "job.h"

/**
 * APPARMOR_PARSER:
 *
 * Location of apparmor_parser binary
 *
 **/
#define APPARMOR_PARSER "/sbin/apparmor_parser"

/**
 * APPARMOR_PARSER_OPTS:
 *
 * apparmor_parser options
 *
 **/
#define APPARMOR_PARSER_OPTS "-r -W"


NIH_BEGIN_EXTERN

int    apparmor_switch (char *profile)
	__attribute__ ((warn_unused_result));

int    apparmor_available (void)
	__attribute__ ((warn_unused_result));

NIH_END_EXTERN

#endif /* INIT_APPARMOR_H */
