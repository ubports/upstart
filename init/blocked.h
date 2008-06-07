/* upstart
 *
 * Copyright © 2008 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
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

#ifndef INIT_BLOCKED_H
#define INIT_BLOCKED_H

#include <nih/macros.h>
#include <nih/list.h>

#include <nih/dbus.h>

#include "job.h"
#include "event.h"


/**
 * BlockedType:
 *
 * This identifies what is blocked, giving the appropriate type of the
 * following union to use.
 **/
typedef enum blocked_type {
	BLOCKED_JOB,
	BLOCKED_EVENT,
	BLOCKED_EMIT_METHOD,
	BLOCKED_INSTANCE_START_METHOD,
	BLOCKED_INSTANCE_STOP_METHOD,
	BLOCKED_INSTANCE_RESTART_METHOD
} BlockedType;


/**
 * Blocked:
 * @entry: list header,
 * @type: type of object blocked,
 * @job: job pointer if @type is BLOCKED_JOB,
 * @event: event pointer if @type is BLOCKED_EVENT,
 * @message: D-Bus message pointer if @type is BLOCKED_*_METHOD,
 * @data: generic pointer to blocked object.
 *
 * This structure is used to reference an object that is blocked on
 * some other, such as an event completing or a job reaching a goal.
 * Handling of actually blocking the types here is up to the called.
 **/
typedef struct blocked {
	NihList     entry;
	BlockedType type;

	union {
		Job            *job;
		Event          *event;
		NihDBusMessage *message;
		void           *data;
	};
} Blocked;


NIH_BEGIN_EXTERN

Blocked *blocked_new (const void *parent, BlockedType type, void *data)
	__attribute__ ((warn_unused_result, malloc));

NIH_END_EXTERN

#endif /* INIT_BLOCKED_H */
