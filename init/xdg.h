/* upstart
 *
 * Copyright © 2012 Canonical Ltd.
 * Author: Dmitrijs Ledkovs <dmitrijs.ledkovs@canonical.com>.
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

#ifndef INIT_XDG_H
#define INIT_XDG_H

#include "paths.h"
#include <nih/macros.h>

NIH_BEGIN_EXTERN

char *    get_home_subdir        (const char * suffix)
	__attribute__ ((malloc, warn_unused_result));

char *    xdg_get_config_home    (void)
	__attribute__ ((malloc, warn_unused_result));

char **   xdg_get_config_dirs    (void)
	__attribute__ ((malloc, warn_unused_result));

char **   get_user_upstart_dirs  (void)
	__attribute__ ((malloc, warn_unused_result));

NIH_END_EXTERN

#endif /* INIT_XDG_H */
