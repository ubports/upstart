/* upstart
 *
 * Copyright © 2010,2011 Canonical Ltd.
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

#include <json.h>

/**
 * Session:
 * @entry: list header,
 * @chroot: path all jobs are chrooted to,
 * @conf_path: configuration path (full path to chroot root).
 *
 * This structure is used to identify collections of jobs
 * that share a common @chroot (*). Note that @conf_path is
 * unique across all sessions.
 *
 * Summary of Session values for different environments:
 *
 * +-------------+--------------------------------------------------+
 * |    D-Bus    |             Session                              |
 * +------+------+--------+-----+-----------------------------------+
 * | user | PID  | chroot | uid | Object contents                   |
 * +------+------+--------+-----+-----------------------------------+
 * | 0    | >0   | no     | 0   | NULL (*1)                         |
 * | 0    | >0   | yes    | 0   | chroot + conf_path set            |
 * | >0   | ??   | yes    | >0  | Not permitted (*2)                |
 * +------+------+--------+-----+-----------------------------------+
 *
 * Notes:
 *
 * (*) - this structure used to also store user session details (hence
 * the name), but the functionality was removed with the advent of
 * a true user mode.
 *
 * (*1) - The "NULL session" represents the "traditional" environment
 * before sessions were introduced (namely a non-chroot environment
 * where all job and event operations were handled by uid 0 (root)).
 *
 * (*2) - User lookup is not reliable since the user to query exists
 * within the chroot, but the only possible lookup is outside the
 * chroot.
 *
 **/
typedef struct session {
	NihList entry;
	char *  chroot;
	char *  conf_path;
} Session;


NIH_BEGIN_EXTERN

extern NihList *sessions;

void           session_init        (void);

Session      * session_new         (const void *parent, const char *chroot)
	__attribute__ ((malloc, warn_unused_result));

Session      * session_from_dbus   (const void *parent, NihDBusMessage *message);

json_object  * session_serialise_all   (void)
	__attribute__ ((malloc, warn_unused_result));

int            session_deserialise_all (json_object *json)
	__attribute__ ((warn_unused_result));

int            session_get_index (const Session *session)
	__attribute__ ((warn_unused_result));

Session *      session_from_index (int idx)
	__attribute__ ((warn_unused_result));

NIH_END_EXTERN

#endif /* INIT_SESSION_H */
