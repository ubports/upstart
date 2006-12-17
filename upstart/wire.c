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
 * upstart_write_int:
 * @iovec: iovec to write to,
 * @size: size of iovec buffer,
 * @value: value to write.
 *
 * Write an integer @value to the end of the @iovec given, which has
 * @size bytes available in its buffer.
 *
 * Integers are transmitted across the wire as signed 32-bit values,
 * in network byte order.
 *
 * On return from this function, the @iovec length will have been
 * incremented by the number of bytes used by this integer in the stream;
 * if there is insufficient space in the stream for this integer, the
 * length will be greater than @size.
 *
 * Returns: zero on success, negative value on error.
 **/
int
upstart_write_int (struct iovec *iovec,
		   size_t        size,
		   int           value)
{
	size_t  start;
	int32_t wire_value;

	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);

	if ((value < INT32_MIN) || (value > INT32_MAX))
		return -1;

	start = iovec->iov_len;
	iovec->iov_len += sizeof (wire_value);

	if (iovec->iov_len > size)
		return -1;

	wire_value = ntohl (value);
	memcpy (iovec->iov_base + start, &wire_value, sizeof (wire_value));

	return 0;
}

/**
 * upstart_read_int:
 * @iovec: iovec to read from,
 * @pos: position within iovec,
 * @value: pointer to write to.
 *
 * Read an integer value from @pos bytes into the @iovec given, storing
 * the value found in the integer pointed to by @value.
 *
 * Integers are transmitted across the wire as signed 32-bit values,
 * in network byte order.
 *
 * On return from this function, @pos will have been incremented by the
 * number of bytes used by this integer in the stream; if there is
 * insufficient space in the stream for this integer, @pos will be
 * greater than the length of the stream.
 *
 * Returns: zero on success, negative value on error.
 **/
int
upstart_read_int (struct iovec *iovec,
		  size_t       *pos,
		  int          *value)
{
	size_t  start;
	int32_t wire_value;

	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);
	nih_assert (pos != NULL);
	nih_assert (value != NULL);

	start = *pos;
	*pos += sizeof (wire_value);

	if (*pos > iovec->iov_len)
		return -1;

	memcpy (&wire_value, iovec->iov_base + start, sizeof (wire_value));
	wire_value = ntohl (wire_value);

	if ((wire_value < INT_MIN) || (wire_value > INT_MAX))
		return -1;

	*value = (int)wire_value;

	return 0;
}


/**
 * upstart_write_unsigned:
 * @iovec: iovec to write to,
 * @size: size of iovec buffer,
 * @value: value to write.
 *
 * Write an unsigned @value to the end of the @iovec given, which has
 * @size bytes available in its buffer.
 *
 * Unsigneds are transmitted across the wire as 32-bit values,
 * in network byte order.
 *
 * On return from this function, the @iovec length will have been
 * incremented by the number of bytes used by this unsigned in the stream;
 * if there is insufficient space in the stream for this integer, the
 * length will be greater than @size.
 *
 * Returns: zero on success, negative value on error.
 **/
int
upstart_write_unsigned (struct iovec *iovec,
			size_t        size,
			unsigned int  value)
{
	size_t   start;
	uint32_t wire_value;

	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);

	if (value > UINT32_MAX)
		return -1;

	start = iovec->iov_len;
	iovec->iov_len += sizeof (wire_value);

	if (iovec->iov_len > size)
		return -1;

	wire_value = ntohl (value);
	memcpy (iovec->iov_base + start, &wire_value, sizeof (wire_value));

	return 0;
}

/**
 * upstart_read_unsigned:
 * @iovec: iovec to read from,
 * @pos: position within iovec,
 * @value: pointer to write to.
 *
 * Read an unsigned value from @pos bytes into the @iovec given, storing
 * the value found in the variable pointed to by @value.
 *
 * Unsigneds are transmitted across the wire as 32-bit values,
 * in network byte order.
 *
 * On return from this function, @pos will have been incremented by the
 * number of bytes used by this unsigned in the stream; if there is
 * insufficient space in the stream for this unsigned, @pos will be
 * greater than the length of the stream.
 *
 * Returns: zero on success, negative value on error.
 **/
int
upstart_read_unsigned (struct iovec *iovec,
		       size_t       *pos,
		       unsigned int *value)
{
	size_t   start;
	uint32_t wire_value;

	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);
	nih_assert (pos != NULL);
	nih_assert (value != NULL);

	start = *pos;
	*pos += sizeof (wire_value);

	if (*pos > iovec->iov_len)
		return -1;

	memcpy (&wire_value, iovec->iov_base + start, sizeof (wire_value));
	wire_value = ntohl (wire_value);

	if (wire_value > UINT_MAX)
		return -1;

	*value = (unsigned int)wire_value;

	return 0;
}


/**
 * upstart_write_string:
 * @iovec: iovec to write to,
 * @size: size of iovec buffer,
 * @value: value to write.
 *
 * Write a string @value to the end of the @iovec given, which has
 * @size bytes available in its buffer.
 *
 * Strings are transmitted across the wire as an unsigned 32-bit value
 * containing the length, followed by that number of bytes containing the
 * string itself, without any NULL terminator.
 *
 * @value may be an empty string, in which case a zero length is sent
 * with no following bytes; it may also be NULL in which case the special
 * length 0xffffffff is sent followed by no bytes.
 *
 * On return from this function, the @iovec length will have been
 * incremented by the number of bytes used by the string in the stream
 * (including the space to store the length); if there is insufficient
 * space in the stream for this integer, the length will be greater
 * than @size.
 *
 * Returns: zero on success, negative value on error.
 **/
int
upstart_write_string (struct iovec *iovec,
		      size_t        size,
		      const char   *value)
{
	size_t start, length;

	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);

	if (value) {
		length = strlen (value);
		if (length > 0xfffffffe)
			return -1;
	} else {
		length = 0xffffffff;
	}

	if (upstart_write_unsigned (iovec, size, length))
		return -1;

	if (! value)
		return 0;


	start = iovec->iov_len;
	iovec->iov_len += length;

	if (iovec->iov_len > size)
		return -1;

	memcpy (iovec->iov_base + start, value, length);

	return 0;
}

/**
 * upstart_read_string:
 * @iovec: iovec to read from,
 * @pos: position within iovec.
 * @parent: parent of new string,
 * @value: pointer to store string.
 *
 * Read a string value from @pos bytes into the @iovec given, allocate
 * the new string with nih_alloc and store it in the variable pointed to
 * by @value.
 *
 * Strings are transmitted across the wire as an unsigned 32-bit value
 * containing the length, followed by that number of bytes containing the
 * string itself, without any NULL terminator.
 *
 * If the length of the string on the wire is zero, @value will be set
 * to an allocated zero-length string; if the special length 0xffffffff
 * is read, @value will be set to NULL.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned string will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * On return from this function, @pos will have been incremented by the
 * number of bytes used by the string in the stream (including the space
 * to store the length); if there is insufficient space in the stream for
 * the string, @pos will be greater than the length of the stream.
 *
 * Returns: zero on success, negative value on error.
 **/
int
upstart_read_string (struct iovec  *iovec,
		     size_t        *pos,
		     const void    *parent,
		     char         **value)
{
	size_t start, length;

	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);
	nih_assert (pos != NULL);
	nih_assert (value != NULL);

	if (upstart_read_unsigned (iovec, pos, (unsigned int *)&length))
		return -1;

	if (length == 0xffffffff) {
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
 * upstart_write_header:
 * @iovec: iovec to read from,
 * @size: size of iovec buffer,
 * @type: message type to write.
 *
 * Write a header for a @type message to the end of the @iovec given,
 * which has @size bytes available in its buffer.
 *
 * The message header consists of a "magic" string ("upstart\n") followed
 * by the message type transmitted as a signed 32-bit value in network
 * byte order.
 *
 * On return from this function, the @iovec length will have been
 * incremented by the number of bytes used by the header in the stream;
 * if there is insufficient space in the stream for the header, the
 * length will be greater than @size.
 *
 * Returns: zero on success, negative value on error.
 **/
int
upstart_write_header (struct iovec   *iovec,
		      size_t          size,
		      UpstartMsgType  type)
{
	size_t start;

	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);

	start = iovec->iov_len;
	iovec->iov_len += strlen (MAGIC);

	if (iovec->iov_len > size)
		return -1;

	memcpy (iovec->iov_base + start, MAGIC, strlen (MAGIC));

	if (upstart_write_int (iovec, size, type))
		return -1;

	return 0;
}

/**
 * upstart_read_header:
 * @iovec: iovec to read from,
 * @pos: position within iovec,
 * @type: pointer to write message type to.
 *
 * Read a message header from @pos bytes into the @iovec given, storing
 * the type of message found in the variable pointed to by @value.
 *
 * The message header consists of a "magic" string ("upstart\n") followed
 * by the message type transmitted as a signed 32-bit value in network
 * byte order.
 *
 * On return from this function, @pos will have been incremented by the
 * number of bytes used by the header in the stream; if there is
 * insufficient space in the stream for the header, @pos will be
 * greater than the length of the stream.
 *
 * Returns: zero on success, negative value on error.
 **/
int
upstart_read_header (struct iovec   *iovec,
		     size_t         *pos,
		     UpstartMsgType *type)
{
	size_t start;

	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);
	nih_assert (pos != NULL);
	nih_assert (type != NULL);

	start = *pos;
	*pos += strlen (MAGIC);

	if (*pos > iovec->iov_len)
		return -1;

	if (memcmp (iovec->iov_base + start, MAGIC, strlen (MAGIC)))
		return -1;

	if (upstart_read_int (iovec, pos, (int *)type))
		return -1;

	return 0;
}


/**
 * upstart_write_packv:
 * @iovec: iovec to write to,
 * @size: size of iovec buffer,
 * @pack: pack of values,
 * @args: arguments.
 *
 * Write a set of values, as determined by @pack, to the end of the @iovec
 * given, which has @size bytes available in its buffer.
 *
 * @pack is a string that indicates the types of @args.
 *  i - int          (written with upstart_write_int)
 *  u - unsigned int (written with upstart_write_unsigned)
 *  s - const char * (written with upstart_write_string)
 *
 * On return from this function, the @iovec length will have been
 * incremented by the number of bytes used by in the stream; if there is
 * insufficient space in the stream, the length will be greater than @size.
 *
 * Returns: zero on success, negative value on error.
 **/
int
upstart_write_packv (struct iovec *iovec,
		     size_t        size,
		     const char   *pack,
		     va_list       args)
{
	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);
	nih_assert (pack != NULL);

	for (; *pack; pack++) {
		int ret;

		switch (*pack) {
		case 'i':
			ret = upstart_write_int (
				iovec, size, va_arg (args, int));
			break;
		case 'u':
			ret = upstart_write_unsigned (
				iovec, size, va_arg (args, unsigned int));
			break;
		case 's':
			ret = upstart_write_string (
				iovec, size, va_arg (args, const char *));
			break;
		default:
			nih_assert_notreached ();
		}

		if (ret)
			return ret;
	}

	return 0;
}

/**
 * upstart_write_pack:
 * @iovec: iovec to write to,
 * @size: size of iovec buffer,
 * @pack: pack of values.
 *
 * Write a set of values, as determined by @pack, to the end of the @iovec
 * given, which has @size bytes available in its buffer.
 *
 * @pack is a string that indicates the types of the following arguments:
 *  i - int          - written with upstart_write_int()
 *  u - unsigned int - written with upstart_write_unsigned()
 *  s - const char * - written with upstart_write_string()
 *
 * On return from this function, the @iovec length will have been
 * incremented by the number of bytes used by in the stream; if there is
 * insufficient space in the stream, the length will be greater than @size.
 *
 * Returns: zero on success, negative value on error.
 **/
int
upstart_write_pack (struct iovec *iovec,
		    size_t        size,
		    const char   *pack,
		    ...)
{
	va_list args;
	int     ret;

	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);
	nih_assert (pack != NULL);

	va_start (args, pack);
	ret = upstart_write_packv (iovec, size, pack, args);
	va_end (args);

	return ret;
}

/**
 * upstart_read_pack:
 * @iovec: iovec to write to,
 * @pos: position within iovec,
 * @parent: parent of new strings,
 * @pack: pack of values,
 * @args: arguments.
 *
 * Read a set of values, as determined by @pack, from @pos bytes into the
 * @iovec given.
 *
 * @pack is a string that indicates the types of @args:
 *  i - int *          - read with upstart_read_int()
 *  u - unsigned int * - read with upstart_read_unsigned()
 *  s - char **        - read with upstart_read_string()
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned string will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * On return from this function, @pos will have been incremented by the
 * number of bytes used in the stream; if there is insufficient space in
 * the stream, @pos will be greater than the length of the stream.
 *
 * Note that errors may be detected after strings have already been
 * allocated for previous members in the pack, those will remain allocated
 * if this returns with an error, so care should be taken to free those
 * if necessary.
 *
 * Returns: zero on success, negative value on error.
 **/
int
upstart_read_packv (struct iovec *iovec,
		    size_t       *pos,
		    const void   *parent,
		    const char   *pack,
		    va_list       args)
{
	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);
	nih_assert (pos != NULL);
	nih_assert (pack != NULL);

	for (; *pack; pack++) {
		int ret;

		switch (*pack) {
		case 'i':
			ret = upstart_read_int (
				iovec, pos, va_arg (args, int *));
			break;
		case 'u':
			ret = upstart_read_unsigned (
				iovec, pos, va_arg (args, unsigned int *));
			break;
		case 's':
			ret = upstart_read_string (
				iovec, pos, parent, va_arg (args, char **));
			break;
		default:
			nih_assert_notreached ();
		}

		if (ret)
			return ret;
	}

	return 0;
}

/**
 * upstart_read_pack:
 * @iovec: iovec to write to,
 * @pos: position within iovec,
 * @parent: parent of new strings,
 * @pack: pack of values.
 *
 * Read a set of values, as determined by @pack, from @pos bytes into the
 * @iovec given.
 *
 * @pack is a string that indicates the types of the following arguments:
 *  i - int *          - read with upstart_read_int()
 *  u - unsigned int * - read with upstart_read_unsigned()
 *  s - char **        - read with upstart_read_string()
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned string will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * On return from this function, @pos will have been incremented by the
 * number of bytes used in the stream; if there is insufficient space in
 * the stream, @pos will be greater than the length of the stream.
 *
 * Returns: zero on success, negative value on error.
 **/
int
upstart_read_pack (struct iovec *iovec,
		   size_t       *pos,
		   const void   *parent,
		   const char   *pack,
		   ...)
{
	va_list args;
	int     ret;

	nih_assert (iovec != NULL);
	nih_assert (iovec->iov_base != NULL);
	nih_assert (pos != NULL);
	nih_assert (pack != NULL);

	va_start (args, pack);
	ret = upstart_read_packv (iovec, pos, parent, pack, args);
	va_end (args);

	return ret;
}
