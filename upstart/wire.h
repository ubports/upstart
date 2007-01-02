/* upstart
 *
 * Copyright © 2007 Canonical Ltd.
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

#include <stdarg.h>

#include <nih/macros.h>
#include <nih/io.h>

#include <upstart/control.h>


NIH_BEGIN_EXTERN

int upstart_push_int      (NihIoMessage *message, int value);
int upstart_pop_int       (NihIoMessage *message, int *value);

int upstart_push_unsigned (NihIoMessage *message, unsigned int value);
int upstart_pop_unsigned  (NihIoMessage *message, unsigned int *value);

int upstart_push_string   (NihIoMessage *message, const char *value);
int upstart_pop_string    (NihIoMessage *message, const void *parent,
			   char **value);

int upstart_push_header   (NihIoMessage *message, UpstartMessageType type);
int upstart_pop_header    (NihIoMessage *message, UpstartMessageType *type);

int upstart_push_packv    (NihIoMessage *message, const char *pack,
			   va_list args);
int upstart_push_pack     (NihIoMessage *message, const char *pack, ...);
int upstart_pop_packv     (NihIoMessage *message, const void *parent,
			   const char *pack, va_list args);
int upstart_pop_pack      (NihIoMessage *message, const void *parent,
			   const char *pack, ...);

NIH_END_EXTERN

#endif /* UPSTART_WIRE_H */
