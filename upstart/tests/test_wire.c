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

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/io.h>

#include <upstart/control.h>
#include <upstart/wire.h>


void
test_push_int (void)
{
	NihIoMessage *msg;
	int           ret;

	TEST_FUNCTION ("upstart_push_int");
	msg = nih_io_message_new (NULL);

	/* Check that we can write an integer into an empty message that has
	 * room; the integer should show up in network byte order at the
	 * start of the buffer, and the length of the buffer should be
	 * increased.
	 */
	TEST_FEATURE ("with space in empty buffer");
	ret = upstart_push_int (msg, 42);

	TEST_EQ (ret, 0);
	TEST_EQ (msg->data->len, 4);
	TEST_EQ_MEM (msg->data->buf, "\0\0\0\x2a", 4);


	/* Check that we can write an integer into a message that already has
	 * some thing in it, it should be appended and the buffer increased
	 * in length to include both.
	 */
	TEST_FEATURE ("with space in used buffer");
	ret = upstart_push_int (msg, 1234567);

	TEST_EQ (ret, 0);
	TEST_EQ (msg->data->len, 8);
	TEST_EQ_MEM (msg->data->buf, "\0\0\0\x2a\0\x12\xd6\x87", 8);


	/* Check that we can place a negative number into the iovec. */
	TEST_FEATURE ("with negative number");
	ret = upstart_push_int (msg, -42);

	TEST_EQ (ret, 0);
	TEST_EQ (msg->data->len, 12);
	TEST_EQ_MEM (msg->data->buf + 8, "\xff\xff\xff\xd6", 4);


	nih_free (msg);
}

void
test_pop_int (void)
{
	NihIoMessage *msg;
	int           ret, value;

	TEST_FUNCTION ("upstart_pop_int");
	msg = nih_io_message_new (NULL);
	nih_io_buffer_push (msg->data,
			    "\0\0\0\x2a\0\x12\xd6\x87\xff\xff\xff\xd6\0\0",
			    14);


	/* Check that we can read an integer from the start of a message;
	 * the integer should be returned in host byte order from the start
	 * of the buffer, and then should be removed from it.
	 */
	TEST_FEATURE ("with integer at start of buffer");
	ret = upstart_pop_int (msg, &value);

	TEST_EQ (ret, 0);
	TEST_EQ (value, 42);

	TEST_EQ (msg->data->len, 10);
	TEST_EQ_MEM (msg->data->buf, "\0\x12\xd6\x87\xff\xff\xff\xd6\0\0", 10);


	/* Check that we can read an integer from a position inside the
	 * message, shrinking the buffer further.
	 */
	TEST_FEATURE ("with integer inside buffer");
	ret = upstart_pop_int (msg, &value);

	TEST_EQ (ret, 0);
	TEST_EQ (value, 1234567);

	TEST_EQ (msg->data->len, 6);
	TEST_EQ_MEM (msg->data->buf, "\xff\xff\xff\xd6\0\0", 6);


	/* Check that we can read a negative number from a message. */
	TEST_FEATURE ("with negative number");
	ret = upstart_pop_int (msg, &value);

	TEST_EQ (ret, 0);
	TEST_EQ (value, -42);

	TEST_EQ (msg->data->len, 2);
	TEST_EQ_MEM (msg->data->buf, "\0\0", 2);


	/* Check that -1 is returned if there is not enough space in the
	 * buffer for an integer.
	 */
	TEST_FEATURE ("with insufficient space in buffer");
	ret = upstart_pop_int (msg, &value);

	TEST_LT (ret, 0);
	TEST_EQ (value, -42);


	nih_free (msg);
}


void
test_push_unsigned (void)
{
	NihIoMessage *msg;
	int           ret;

	TEST_FUNCTION ("upstart_push_unsigned");
	msg = nih_io_message_new (NULL);


	/* Check that we can write an integer into an empty message that has
	 * room; the integer should show up in network byte order at the
	 * start of the buffer, and the length of the buffer should be
	 * increased.
	 */
	TEST_FEATURE ("with space in empty buffer");
	ret = upstart_push_unsigned (msg, 42);

	TEST_EQ (ret, 0);
	TEST_EQ (msg->data->len, 4);
	TEST_EQ_MEM (msg->data->buf, "\0\0\0\x2a", 4);


	/* Check that we can write an integer into a message that already has
	 * some thing in it, it should be appended and the buffer increased
	 * in length to include both.
	 */
	TEST_FEATURE ("with space in used buffer");
	ret = upstart_push_unsigned (msg, 1234567);

	TEST_EQ (ret, 0);
	TEST_EQ (msg->data->len, 8);
	TEST_EQ_MEM (msg->data->buf, "\0\0\0\x2a\0\x12\xd6\x87", 8);


	/* Check that we can write a very large number into the message. */
	TEST_FEATURE ("with very large number");
	ret = upstart_push_unsigned (msg, 0xfedcba98);

	TEST_EQ (ret, 0);
	TEST_EQ (msg->data->len, 12);
	TEST_EQ_MEM (msg->data->buf + 8, "\xfe\xdc\xba\x98", 4);


	nih_free (msg);
}

void
test_pop_unsigned (void)
{
	NihIoMessage *msg;
	unsigned int  value;
	int           ret;

	TEST_FUNCTION ("upstart_pop_unsigned");
	msg = nih_io_message_new (NULL);
	nih_io_buffer_push (msg->data,
			    "\0\0\0\x2a\0\x12\xd6\x87\xfe\xdc\xba\x98\0\0",
			    14);


	/* Check that we can read an integer from the start of a message;
	 * the integer should be returned in host byte order from the start
	 * of the buffer, and removed from it.
	 */
	TEST_FEATURE ("with integer at start of buffer");
	ret = upstart_pop_unsigned (msg, &value);

	TEST_EQ (ret, 0);
	TEST_EQ_U (value, 42);

	TEST_EQ (msg->data->len, 10);
	TEST_EQ_MEM (msg->data->buf, "\0\x12\xd6\x87\xfe\xdc\xba\x98\0\0", 10);


	/* Check that we can read an integer from a position inside the
	 * message.  Again it should be removed from it.
	 */
	TEST_FEATURE ("with integer inside buffer");
	ret = upstart_pop_unsigned (msg, &value);

	TEST_EQ (ret, 0);
	TEST_EQ_U (value, 1234567);

	TEST_EQ (msg->data->len, 6);
	TEST_EQ_MEM (msg->data->buf, "\xfe\xdc\xba\x98\0\0", 6);


	/* Check that we can read a very large number from a message. */
	TEST_FEATURE ("with very large number");
	ret = upstart_pop_unsigned (msg, &value);

	TEST_EQ (ret, 0);
	TEST_EQ_U (value, 0xfedcba98);

	TEST_EQ (msg->data->len, 2);
	TEST_EQ_MEM (msg->data->buf, "\0\0", 2);


	/* Check that -1 is returned if there is not enough space in the
	 * buffer for an integer.
	 */
	TEST_FEATURE ("with insufficient space in buffer");
	ret = upstart_pop_unsigned (msg, &value);

	TEST_LT (ret, 0);
	TEST_EQ_U (value, 0xfedcba98);


	nih_free (msg);
}


void
test_push_string (void)
{
	NihIoMessage *msg;
	int           ret;

	TEST_FUNCTION ("upstart_push_string");
	msg = nih_io_message_new (NULL);


	/* Check that we can write a string into an empty message that has
	 * room; the string should show up with the length in network byte
	 * order at the start of the buffer, followed by the string bytes.
	 * The length of the buffer should be increased.
	 */
	TEST_FEATURE ("with space in empty buffer");
	ret = upstart_push_string (msg, "hello");

	TEST_EQ (ret, 0);
	TEST_EQ (msg->data->len, 9);
	TEST_EQ_MEM (msg->data->buf, "\0\0\0\x05hello", 9);


	/* Check that we can write a string into a message that already has
	 * some thing in it, it should be appended and the buffer increased
	 * in length to include both.
	 */
	TEST_FEATURE ("with space in used buffer");
	ret = upstart_push_string (msg, "goodbye");

	TEST_EQ (ret, 0);
	TEST_EQ (msg->data->len, 20);
	TEST_EQ_MEM (msg->data->buf, "\0\0\0\x05hello\0\0\0\x07goodbye", 20);


	/* Check that we can write the empty string into the message. */
	TEST_FEATURE ("with empty string");
	ret = upstart_push_string (msg, "");

	TEST_EQ (ret, 0);
	TEST_EQ (msg->data->len, 24);
	TEST_EQ_MEM (msg->data->buf + 20, "\0\0\0\0", 4);


	/* Check that we can write NULL into the message. */
	TEST_FEATURE ("with NULL string");
	ret = upstart_push_string (msg, NULL);

	TEST_EQ (ret, 0);
	TEST_EQ (msg->data->len, 28);
	TEST_EQ_MEM (msg->data->buf + 24, "\xff\xff\xff\xff", 4);


	nih_free (msg);
}

void
test_pop_string (void)
{
	NihIoMessage *msg;
	char         *value;
	int           ret;

	TEST_FUNCTION ("upstart_pop_string");
	msg = nih_io_message_new (NULL);
	nih_io_buffer_push (msg->data, ("\0\0\0\x05hello\0\0\0\x07goodbye"
					"\0\0\0\0\xff\xff\xff\xff"
					"\0\0\0\x04te"), 34);


	/* Check that we can read a string from the start of a message;
	 * the string should be allocated with nih_alloc, copied from the
	 * start of the buffer and include a NULL terminator.  It should
	 * then be removed from the message.
	 */
	TEST_FEATURE ("with string at start of buffer");
	ret = upstart_pop_string (msg, NULL, &value);

	TEST_EQ (ret, 0);
	TEST_ALLOC_SIZE (value, 6);
	TEST_EQ (value[5], '\0');
	TEST_EQ_STR (value, "hello");

	TEST_EQ (msg->data->len, 25);
	TEST_EQ_MEM (msg->data->buf, ("\0\0\0\x07goodbye"
				      "\0\0\0\0\xff\xff\xff\xff"
				      "\0\0\0\x04te"), 25);

	nih_free (value);


	/* Check that we can read a string from a position inside the
	 * message, and then removed.
	 */
	TEST_FEATURE ("with string inside buffer");
	ret = upstart_pop_string (msg, NULL, &value);

	TEST_EQ (ret, 0);
	TEST_ALLOC_SIZE (value, 8);
	TEST_EQ (value[7], '\0');
	TEST_EQ_STR (value, "goodbye");

	TEST_EQ (msg->data->len, 14);
	TEST_EQ_MEM (msg->data->buf,
		     "\0\0\0\0\xff\xff\xff\xff\0\0\0\x04te", 14);

	nih_free (value);


	/* Check that we can read the empty string from the message. */
	TEST_FEATURE ("with empty string in buffer");
	ret = upstart_pop_string (msg, NULL, &value);

	TEST_EQ (ret, 0);
	TEST_ALLOC_SIZE (value, 1);
	TEST_EQ (value[0], '\0');

	TEST_EQ (msg->data->len, 10);
	TEST_EQ_MEM (msg->data->buf, "\xff\xff\xff\xff\0\0\0\x04te", 10);

	nih_free (value);


	/* Check that we can read NULL from the message. */
	TEST_FEATURE ("with NULL string in buffer");
	ret = upstart_pop_string (msg, NULL, &value);

	TEST_EQ (ret, 0);
	TEST_EQ_P (value, NULL);

	TEST_EQ (msg->data->len, 6);
	TEST_EQ_MEM (msg->data->buf, "\0\0\0\x04te", 6);


	/* Check that -1 is returned if there is enough space in the buffer
	 * for the length of the string, but not the string.
	 */
	TEST_FEATURE ("with insufficient space for string");
	ret = upstart_pop_string (msg, NULL, &value);

	TEST_LT (ret, 0);
	TEST_EQ_P (value, NULL);

	TEST_EQ (msg->data->len, 2);
	TEST_EQ_MEM (msg->data->buf, "te", 2);


	/* Check that -1 is returned if there is not enough space in the
	 * buffer for the length of the string.
	 */
	TEST_FEATURE ("with insufficient space in buffer for length");
	ret = upstart_pop_string (msg, NULL, &value);

	TEST_LT (ret, 0);
	TEST_EQ_P (value, NULL);


	nih_free (msg);
}


void
test_push_header (void)
{
	NihIoMessage *msg;
	int           ret;

	TEST_FUNCTION ("upstart_push_header");
	msg = nih_io_message_new (NULL);


	/* Check that we can write a header into an empty message that has
	 * room; the magic string should be written at the start of the
	 * buffer, followed by the message type in network byte order.
	 * The length of the buffer should be increased.
	 */
	TEST_FEATURE ("with space in empty buffer");
	ret = upstart_push_header (msg, UPSTART_NO_OP);

	TEST_EQ (ret, 0);
	TEST_EQ (msg->data->len, 12);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\0", 12);


	/* Check that we can write a header into a message that already has
	 * something in it, it should be appended and the buffer increased
	 * in length to include both.
	 */
	TEST_FEATURE ("with space in used buffer");
	ret = upstart_push_header (msg, UPSTART_NO_OP);

	TEST_EQ (ret, 0);
	TEST_EQ (msg->data->len, 24);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\0upstart\n\0\0\0\0", 24);


	nih_free (msg);
}

void
test_pop_header (void)
{
	NihIoMessage   *msg;
	UpstartMsgType  value;
	int             ret;

	TEST_FUNCTION ("upstart_pop_header");
	msg = nih_io_message_new (NULL);
	nih_io_buffer_push (msg->data,
			    "upstart\n\0\0\0\0upstart\n\0\0\0\0upstart\n\0\0",
			    34);


	/* Check that we can read a header from the start of a message,
	 * and have the message type stored in value, and then removed from
	 * the buffer.
	 */
	TEST_FEATURE ("with header at start of buffer");
	ret = upstart_pop_header (msg, &value);

	TEST_EQ (ret, 0);
	TEST_EQ (value, UPSTART_NO_OP);

	TEST_EQ (msg->data->len, 22);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\0upstart\n\0\0", 22);


	/* Check that we can read a header from a position inside the
	 * message and have it removed.
	 */
	TEST_FEATURE ("with string inside buffer");
	ret = upstart_pop_header (msg, &value);

	TEST_EQ (ret, 0);
	TEST_EQ (value, UPSTART_NO_OP);

	TEST_EQ (msg->data->len, 10);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0", 10);


	/* Check that -1 is returned if there is enough space in the buffer
	 * for the magic string, but not the message type.
	 */
	TEST_FEATURE ("with insufficient space for message type");
	value = -1;
	ret = upstart_pop_header (msg, &value);

	TEST_LT (ret, 0);
	TEST_EQ (value, -1);

	TEST_EQ (msg->data->len, 2);
	TEST_EQ_MEM (msg->data->buf, "\0\0", 2);


	/* Check that -1 is returned if there is not enough space in the
	 * buffer for the magic string.
	 */
	TEST_FEATURE ("with insufficient space in buffer for magic");
	ret = upstart_pop_header (msg, &value);

	TEST_LT (ret, 0);
	TEST_EQ (value, -1);


	nih_free (msg);
}


void
test_push_pack (void)
{
	NihIoMessage *msg;
	int           ret;

	TEST_FUNCTION ("upstart_push_pack");
	msg = nih_io_message_new (NULL);


	/* Check that we can write a series of different values in a single
	 * function call, resulting in them being placed at the start of the
	 * message in order.
	 */
	TEST_FEATURE ("with empty buffer");
	ret = upstart_push_pack (msg, "iusi", 100, 0x98765432,
				 "string value", -42);

	TEST_EQ (ret, 0);
	TEST_EQ (msg->data->len, 28);
	TEST_EQ_MEM (msg->data->buf, ("\0\0\0\x64\x98\x76\x54\x32"
				      "\0\0\0\x0cstring value"
				      "\xff\xff\xff\xd6"), 28);


	/* Check that we can write a series of different values onto the
	 * end of an existing buffer, without smashing what was already
	 * there.
	 */
	TEST_FEATURE ("with used buffer");
	ret = upstart_push_pack (msg, "ii", 98, 100);

	TEST_EQ (ret, 0);
	TEST_EQ (msg->data->len, 36);
	TEST_EQ_MEM (msg->data->buf, ("\0\0\0\x64\x98\x76\x54\x32"
				      "\0\0\0\x0cstring value"
				      "\xff\xff\xff\xd6"
				      "\0\0\0\x62\0\0\0\x64"), 36);


	nih_free (msg);
}

void
test_pop_pack (void)
{
	NihIoMessage  *msg;
	char          *str;
	unsigned int   uint;
	int            ret, int1, int2;

	TEST_FUNCTION ("upstart_pop_pack");
	msg = nih_io_message_new (NULL);
	nih_io_buffer_push (msg->data, ("\0\0\0\x64\x98\x76\x54\x32"
					"\0\0\0\x0cstring value"
					"\xff\xff\xff\xd6"
					"\0\0\0\x62\0\0\0\x64"
					"\0\0\0\x13\0\0\0\x04te"), 46);


	/* Check that we can read a series of different values in a single
	 * function call, removing them all from the buffer.
	 */
	TEST_FEATURE ("with variables at start of buffer");
	ret = upstart_pop_pack (msg, NULL, "iusi", &int1, &uint, &str, &int2);

	TEST_EQ (ret, 0);
	TEST_EQ (int1, 100);
	TEST_EQ_U (uint, 0x98765432);
	TEST_ALLOC_SIZE (str, 13);
	TEST_EQ (str[12], '\0');
	TEST_EQ_STR (str, "string value");
	TEST_EQ (int2, -42);

	TEST_EQ (msg->data->len, 18);
	TEST_EQ_MEM (msg->data->buf, ("\0\0\0\x62\0\0\0\x64"
				      "\0\0\0\x13\0\0\0\x04te"), 18);

	nih_free (str);


	/* Check that we can read a series of different values from a
	 * point already inside the buffer.
	 */
	TEST_FEATURE ("with variables inside buffer");
	ret = upstart_pop_pack (msg, NULL, "ii", &int1, &int2);

	TEST_EQ (ret, 0);
	TEST_EQ (int1, 98);
	TEST_EQ (int2, 100);

	TEST_EQ (msg->data->len, 10);
	TEST_EQ_MEM (msg->data->buf, ("\0\0\0\x13\0\0\0\x04te"), 10);


	/* Check that -1 is returned if there's not enough space in the
	 * buffer to the entire pack to exist.
	 */
	TEST_FEATURE ("with insufficient space");
	str = NULL;
	ret = upstart_pop_pack (msg, NULL, "is", &int1, &str);

	TEST_LT (ret, 0);
	TEST_EQ_P (str, NULL);


	nih_free (msg);
}


int
main (int   argc,
      char *argv[])
{
	test_push_int ();
	test_pop_int ();
	test_push_unsigned ();
	test_pop_unsigned ();
	test_push_string ();
	test_pop_string ();
	test_push_header ();
	test_pop_header ();
	test_push_pack ();
	test_pop_pack ();

	return 0;
}
