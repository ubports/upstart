/* libupstart
 *
 * test_wire.c - test suite for upstart/wire.c
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

#include <nih/test.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <nih/macros.h>

#include <upstart/wire.h>


void
test_write_int (void)
{
	struct iovec  iovec;
	unsigned char buf[14];
	int           ret;

	TEST_FUNCTION ("upstart_write_int");
	iovec.iov_base = buf;
	iovec.iov_len = 0;

	/* Check that we can write an integer into an empty iovec that has
	 * room; the integer should show up in network byte order at the
	 * start of the buffer, and the length of the buffer should be
	 * increased.
	 */
	TEST_FEATURE ("with space in empty buffer");
	ret = upstart_write_int (&iovec, sizeof (buf), 42);

	TEST_EQ (ret, 0);
	TEST_EQ (iovec.iov_len, 4);
	TEST_EQ_MEM (iovec.iov_base, "\0\0\0\x2a", 4);


	/* Check that we can write an integer into an iovec that already has
	 * some thing in it, it should be appended and the buffer increased
	 * in length to include both.
	 */
	TEST_FEATURE ("with space in used buffer");
	ret = upstart_write_int (&iovec, sizeof (buf), 1234567);

	TEST_EQ (ret, 0);
	TEST_EQ (iovec.iov_len, 8);
	TEST_EQ_MEM (iovec.iov_base, "\0\0\0\x2a\0\x12\xd6\x87", 8);


	/* Check that we can place a negative number into the iovec. */
	TEST_FEATURE ("with negative number");
	ret = upstart_write_int (&iovec, sizeof (buf), -42);

	TEST_EQ (ret, 0);
	TEST_EQ (iovec.iov_len, 12);
	TEST_EQ_MEM (iovec.iov_base + 8, "\xff\xff\xff\xd6", 4);


	/* Check that -1 is returned if there is not enough space in the
	 * buffer for the integer, and that the length is incremented past
	 * the size to indicate an invalid message.
	 */
	TEST_FEATURE ("with insufficient space in buffer");
	ret = upstart_write_int (&iovec, sizeof (buf), 100);

	TEST_LT (ret, 0);
	TEST_EQ (iovec.iov_len, 16);
}

void
test_read_int (void)
{
	struct iovec  iovec;
	unsigned char buf[14];
	size_t        pos;
	int           ret, value;

	TEST_FUNCTION ("upstart_read_int");
	iovec.iov_base = buf;
	iovec.iov_len = 14;
	memcpy (iovec.iov_base, "\0\0\0\x2a\0\x12\xd6\x87\xff\xff\xff\xd6\0\0",
		14);
	pos = 0;

	/* Check that we can read an integer from the start of an iovec;
	 * the integer should be returned in host byte order from the start
	 * of the buffer, and the pos variable should be incremented past it.
	 */
	TEST_FEATURE ("with integer at start of buffer");
	ret = upstart_read_int (&iovec, &pos, &value);

	TEST_EQ (ret, 0);
	TEST_EQ (pos, 4);
	TEST_EQ (value, 42);


	/* Check that we can read an integer from a position inside the
	 * iovec.  The pos variable should be incremented, not set.
	 */
	TEST_FEATURE ("with integer inside buffer");
	ret = upstart_read_int (&iovec, &pos, &value);

	TEST_EQ (ret, 0);
	TEST_EQ (pos, 8);
	TEST_EQ (value, 1234567);


	/* Check that we can read a negative number from an iovec. */
	TEST_FEATURE ("with negative number");
	ret = upstart_read_int (&iovec, &pos, &value);

	TEST_EQ (ret, 0);
	TEST_EQ (pos, 12);
	TEST_EQ (value, -42);


	/* Check that -1 is returned if there is not enough space in the
	 * buffer for an integer, and that the pos is incremented past
	 * the size to indicate an invalid message.  value should be
	 * unchanged.
	 */
	TEST_FEATURE ("with insufficient space in buffer");
	ret = upstart_read_int (&iovec, &pos, &value);

	TEST_LT (ret, 0);
	TEST_EQ (pos, 16);
	TEST_EQ (value, -42);
}



int
main (int   argc,
      char *argv[])
{
	test_write_int ();
	test_read_int ();

	return 0;
}
