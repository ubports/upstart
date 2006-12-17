/* upstart
 *
 * wire.c - control socket wire protocol
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */

#include <arpa/inet.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <stdarg.h>
#include <string.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/logging.h>

#include <upstart/wire.h>


/**
 * MAGIC:
 *
 * Magic string that is placed on the front of all messages.  In theory, we
 * don't need this; as we strongly guard against invalid messages anyway -
 * however it's a useful check that both sides are at least agreeing in
 * principal to communicate.
 **/
#define MAGIC "upstart\n"


/**
 * upstart_read_int:
 * @iovec: iovec to read from,
 * @pos: position within iovec,
 * @value: pointer to write to.
 *
 * Read an integer value from @pos bytes into the @iovec given and store
 * it in the pointer @value.  @pos is incremented by the number of bytes
 * the integer used.
 *
 * Returns: zero on success, negative value if insufficient space.
 **/
int
upstart_read_int (struct iovec *iovec,
		  size_t       *pos,
		  int          *value)
{
	size_t start;
	int    n_value;

	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);
	nih_assert (pos != NULL);
	nih_assert (value != NULL);

	start = *pos;
	*pos += sizeof (int);

	if (*pos > iovec->iov_len)
		return -1;

	memcpy (&n_value, iovec->iov_base + start, sizeof (int));
	*value = ntohl (n_value);

	return 0;
}

/**
 * upstart_write_int:
 * @iovec: iovec to write to,
 * @size: size of iovec buffer,
 * @value: value to write.
 *
 * Write an integer @value to the end of the @iovec given, which has a
 * buffer of @size bytes.  The length of the @iovec is incremented by
 * the number of bytes the integer used.
 *
 * Returns: zero on success, negative value if insufficient space.
 **/
int
upstart_write_int (struct iovec *iovec,
		   size_t        size,
		   int           value)
{
	size_t start;
	int    n_value;

	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);

	start = iovec->iov_len;
	iovec->iov_len += sizeof (int);

	if (iovec->iov_len > size)
		return -1;

	n_value = htonl (value);
	memcpy (iovec->iov_base + start, &n_value, sizeof (int));

	return 0;
}

/**
 * upstart_read_ints:
 * @iovec: iovec to read from,
 * @pos: position within iovec,
 * @number: number of integers to read.
 *
 * Read @number integer values from @pos bytes into the @iovec given and
 * store them in the pointers given as additional arguments to the function.
 * @pos is incremented by the number of bytes the integers used.
 *
 * Returns: zero on success, negative value if insufficient space.
 **/
int
upstart_read_ints (struct iovec *iovec,
		   size_t       *pos,
		   int           number,
		   ...)
{
	va_list args;

	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);
	nih_assert (pos != NULL);

	va_start (args, number);

	while (number--) {
		int *value;

		value = va_arg (args, int *);

		if (upstart_read_int (iovec, pos, value))
			return -1;
	}

	va_end (args);

	return 0;
}

/**
 * upstart_write_ints:
 * @iovec: iovec to write to,
 * @size: size of iovec buffer,
 * @number: number of integers to read.
 *
 * Write @number integer values to the end of the @iovec given, which has a
 * buffer of @size bytes.  The values are taken from additional arguments
 * to the function.  The length of the @iovec is incremented by the number
 * of bytes the integers used.
 *
 * Returns: zero on success, negative value if insufficient space.
 **/
int
upstart_write_ints (struct iovec *iovec,
		    size_t        size,
		    int           number,
		    ...)
{
	va_list args;

	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);

	va_start (args, number);

	while (number--) {
		int value;

		value = va_arg (args, int);

		if (upstart_write_int (iovec, size, value))
			return -1;
	}

	va_end (args);

	return 0;
}

/**
 * upstart_read_str:
 * @iovec: iovec to read from,
 * @pos: position within iovec.
 * @parent: parent of new string,
 * @value: pointer to store string.
 *
 * Read a string value from @pos bytes into the @iovec given,
 * allocates a new string to contain it and stores it in the pointer
 * @value.
 *
 * The string will be NULL terminated.  If a zero-length string is
 * read, @value will be set to NULL.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned string will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * Returns: zero on success, negative value if insufficient space
 * or memory.
 **/
int
upstart_read_str (struct iovec  *iovec,
		  size_t        *pos,
		  const void    *parent,
		  char         **value)
{
	size_t start, length;

	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);
	nih_assert (pos != NULL);
	nih_assert (value != NULL);

	if (upstart_read_int (iovec, pos, (int *)&length))
		return -1;

	if (! length) {
		*value = NULL;
		return 0;
	}


	start = *pos;
	*pos += length;

	if (*pos > iovec->iov_len)
		return -1;

	*value = nih_alloc (parent, length + 1);
	if (! *value)
		return -1;

	memcpy (*value, iovec->iov_base + start, length);
	(*value)[length] = '\0';

	return 0;
}

/**
 * upstart_write_str:
 * @iovec: iovec to write to,
 * @size: size of iovec buffer,
 * @value: value to write.
 *
 * Write a string @value to the end of the @iovec given, which has a
 * buffer of @size bytes.  The length of the @iovec is incremented by
 * the number of bytes the string used.
 *
 * If @value is NULL, a zero-length string is written.
 *
 * Returns: zero on success, negative value if insufficient space.
 **/
int
upstart_write_str (struct iovec *iovec,
		   size_t        size,
		   const char   *value)
{
	size_t start, length;

	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);

	length = value ? strlen (value) : 0;
	if (upstart_write_int (iovec, size, length))
		return -1;

	if (! length)
		return 0;


	start = iovec->iov_len;
	iovec->iov_len += length;

	if (iovec->iov_len > size)
		return -1;

	memcpy (iovec->iov_base + start, value, length);

	return 0;
}

/**
 * upstart_read_header:
 * @iovec: iovec to read from,
 * @pos: position within iovec,
 * @version: pointer to write version to,
 * @type: pointer to write message type to.
 *
 * Read a message header from @pos bytes into the @iovec given, storing
 * the message version number in @version and message type in @type.  @pos
 * is incremented by the number of bytes the header.
 *
 * Returns: zero on success, negative value if insufficient space or invalid
 * format.
 **/
int
upstart_read_header (struct iovec   *iovec,
		     size_t         *pos,
		     int            *version,
		     UpstartMsgType *type)
{
	size_t start;

	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);
	nih_assert (pos != NULL);
	nih_assert (version != NULL);
	nih_assert (type != NULL);

	start = *pos;
	*pos += sizeof (MAGIC) - 1;

	if (*pos > iovec->iov_len)
		return -1;

	if (memcmp (iovec->iov_base + start, MAGIC, sizeof (MAGIC) - 1))
		return -1;

	if (upstart_read_int (iovec, pos, version))
		return -1;

	if (upstart_read_int (iovec, pos, (int *)type))
		return -1;

	return 0;
}

/**
 * upstart_write_header:
 * @iovec: iovec to read from,
 * @size: size of iovec buffer,
 * @version: version to write,
 * @type: message type to write.
 *
 * Write a message header to the end of the @iovec given, which has a
 * buffer of @size bytes.  The header declares a message version number of
 * @version and a message type of @type.  The length of the @iovec is
 * incremented by the number of bytes the header used.
 *
 * Returns: zero on success, negative value if insufficient space or invalid
 * format.
 **/
int
upstart_write_header (struct iovec   *iovec,
		      size_t          size,
		      int             version,
		      UpstartMsgType  type)
{
	size_t start;

	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);

	start = iovec->iov_len;
	iovec->iov_len += sizeof (MAGIC) - 1;

	if (iovec->iov_len > size)
		return -1;

	memcpy (iovec->iov_base + start, MAGIC, sizeof (MAGIC) - 1);

	if (upstart_write_int (iovec, size, version))
		return -1;

	if (upstart_write_int (iovec, size, type))
		return -1;

	return 0;
}
