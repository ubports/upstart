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

#ifndef INIT_SESSION_H
#define INIT_SESSION_H

#include <sys/types.h>

#include <nih/macros.h>
#include <nih/list.h>

#include <nih-dbus/dbus_message.h>


/**
 * Session:
 * @entry: list header,
 * @chroot: path all jobs are chrooted to,
 * @user: uid all jobs are switched to.
 *
 * This structure is used to identify collections of jobs that share either
 * a common @chroot and/or common @user.
 **/
typedef struct session {
	NihList entry;
	char *  chroot;
	uid_t   user;
} Session;


NIH_BEGIN_EXTERN

extern NihList *sessions;

void     session_init      (void);

Session *session_new       (const void *parent, const char *chroot, uid_t user)
	__attribute__ ((malloc, warn_unused_result));

Session *session_from_dbus (const void *parent, NihDBusMessage *message);

void     session_create_conf_source (Session *session);

NIH_END_EXTERN

#endif /* INIT_SESSION_H */
