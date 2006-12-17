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

#ifndef UPSTART_WIRE_H
#define UPSTART_WIRE_H

#include <sys/types.h>
#include <sys/socket.h>

#include <nih/macros.h>

#include <upstart/control.h>


NIH_BEGIN_EXTERN

int upstart_write_int      (struct iovec *iovec, size_t size, int value);
int upstart_read_int       (struct iovec *iovec, size_t *pos, int *value);

int upstart_write_unsigned (struct iovec *iovec, size_t size,
			    unsigned int value);
int upstart_read_unsigned  (struct iovec *iovec, size_t *pos,
			    unsigned int *value);

int upstart_read_str       (struct iovec *iovec, size_t *pos,
			    const void *parent, char **value);
int upstart_write_str      (struct iovec *iovec, size_t size,
			    const char *value);

int upstart_read_header    (struct iovec *iovec, size_t *pos,
			    int *version, UpstartMsgType *type);
int upstart_write_header   (struct iovec *iovec, size_t size,
			    int version, UpstartMsgType type);

NIH_END_EXTERN

#endif /* UPSTART_WIRE_H */
