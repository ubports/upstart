/* upstart
 *
 * job.c - core state machine of tasks and services
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/logging.h>

#include <nih/dbus.h>

#include "job.h"
#include "event.h"
#include "blocked.h"


/**
 * blocked_new:
 * @parent: parent of blocked structure,
 * @type: type of object blocked,
 * @data: pointer to object.
 *
 * Allocates a Blocked structure for the object details given, which is
 * normally appended to the caller's own blocking list.  It is also up
 * to the called to ensure that the object is aware of the block, and handle
 * unblocking the object when done.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.
 *
 * Returns: new Blocked structure or NULL if insufficient memory.
 **/
Blocked *
blocked_new (const void  *parent,
	     BlockedType  type,
	     void        *data)
{
	Blocked *blocked;

	nih_assert (data != NULL);

	blocked = nih_new (parent, Blocked);
	if (! blocked)
		return NULL;

	nih_list_init (&blocked->entry);
	nih_alloc_set_destructor (blocked, (NihDestructor)nih_list_destroy);

	blocked->type = type;
	switch (blocked->type) {
	case BLOCKED_JOB:
		blocked->job = (Job *)data;
		break;
	case BLOCKED_EVENT:
		blocked->event = (Event *)data;
		break;
	case BLOCKED_EMIT_METHOD:
	case BLOCKED_INSTANCE_START_METHOD:
	case BLOCKED_INSTANCE_STOP_METHOD:
	case BLOCKED_INSTANCE_RESTART_METHOD:
		blocked->message = (NihDBusMessage *)data;
		break;
	default:
		nih_assert_not_reached ();
	}

	return blocked;
}
