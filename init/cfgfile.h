/* upstart
 *
 * Copyright © 2006 Canonical Ltd.
 * Author: Scott James Remnant <scott@ubuntu.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef INIT_CFGFILE_H
#define INIT_CFGFILE_H

#include <nih/macros.h>

#include "job.h"


/**
 * CFG_DIR:
 *
 * Directory to find configuration files in.
 **/
#define CFG_DIR "/etc/rc.d"


NIH_BEGIN_EXTERN

Job *cfg_read_job  (void *parent, const char *filename, const char *jobname);

int  cfg_watch_dir (void *parent, const char *dirname, const char *prefix);

NIH_END_EXTERN

#endif /* INIT_CFGFILE_H */
