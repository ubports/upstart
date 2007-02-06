/* libupstart
 *
 * test_wire.c - test suite for upstart/wire.c
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

#include <nih/test.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/io.h>

#include <upstart/message.h>
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
	TEST_ALLOC_FAIL {
		msg->data->len = 0;
		msg->data->size = 0;

		ret = upstart_push_int (msg, 42);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);
			continue;
		}

		TEST_EQ (ret, 0);
		TEST_EQ (msg->data->len, 5);
		TEST_EQ_MEM (msg->data->buf, "i\0\0\0\x2a", 5);
	}


	/* Check that we can write an integer into a message that already has
	 * some thing in it, it should be appended and the buffer increased
	 * in length to include both.
	 */
	TEST_FEATURE ("with space in used buffer");
	TEST_ALLOC_FAIL {
		msg->data->len = 5;
		msg->data->size = BUFSIZ;

		ret = upstart_push_int (msg, 1234567);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);
			continue;
		}

		TEST_EQ (ret, 0);
		TEST_EQ (msg->data->len, 10);
		TEST_EQ_MEM (msg->data->buf, "i\0\0\0\x2ai\0\x12\xd6\x87", 10);
	}


	/* Check that we can place a negative number into the iovec. */
	TEST_FEATURE ("with negative number");
	TEST_ALLOC_FAIL {
		msg->data->len = 10;
		msg->data->size = BUFSIZ;

		ret = upstart_push_int (msg, -42);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);
			continue;
		}

		TEST_EQ (ret, 0);
		TEST_EQ (msg->data->len, 15);
		TEST_EQ_MEM (msg->data->buf + 10, "i\xff\xff\xff\xd6", 5);
	}


	nih_free (msg);
}

void
test_pop_int (void)
{
	NihIoMessage *msg;
	int           ret, value;

	TEST_FUNCTION ("upstart_pop_int");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data, ("i\0\0\0\x2ai\0\x12\xd6\x87"
						 "i\xff\xff\xff\xd6xi\0\0"),
				     19));


	/* Check that we can read an integer from the start of a message;
	 * the integer should be returned in host byte order from the start
	 * of the buffer, and then should be removed from it.
	 */
	TEST_FEATURE ("with integer at start of buffer");
	ret = upstart_pop_int (msg, &value);

	TEST_EQ (ret, 0);
	TEST_EQ (value, 42);

	TEST_EQ (msg->data->len, 14);
	TEST_EQ_MEM (msg->data->buf, "i\0\x12\xd6\x87i\xff\xff\xff\xd6xi\0\0",
		     14);


	/* Check that we can read an integer from a position inside the
	 * message, shrinking the buffer further.
	 */
	TEST_FEATURE ("with integer inside buffer");
	ret = upstart_pop_int (msg, &value);

	TEST_EQ (ret, 0);
	TEST_EQ (value, 1234567);

	TEST_EQ (msg->data->len, 9);
	TEST_EQ_MEM (msg->data->buf, "i\xff\xff\xff\xd6xi\0\0", 9);


	/* Check that we can read a negative number from a message. */
	TEST_FEATURE ("with negative number");
	ret = upstart_pop_int (msg, &value);

	TEST_EQ (ret, 0);
	TEST_EQ (value, -42);

	TEST_EQ (msg->data->len, 4);
	TEST_EQ_MEM (msg->data->buf, "xi\0\0", 4);


	/* Check that -1 is returned if the type in the buffer is not
	 * correct.
	 */
	TEST_FEATURE ("with incorrect type in buffer");
	ret = upstart_pop_int (msg, &value);

	TEST_LT (ret, 0);
	TEST_EQ (value, -42);

	TEST_EQ (msg->data->len, 4);
	TEST_EQ_MEM (msg->data->buf, "xi\0\0", 4);

	nih_io_buffer_shrink (msg->data, 1);


	/* Check that -1 is returned if there is not enough space in the
	 * buffer for an integer.
	 */
	TEST_FEATURE ("with insufficient space in buffer");
	ret = upstart_pop_int (msg, &value);

	TEST_LT (ret, 0);
	TEST_EQ (value, -42);

	TEST_EQ (msg->data->len, 3);
	TEST_EQ_MEM (msg->data->buf, "i\0\0", 3);


	/* Check that -1 is returned if there is not enough space for the
	 * type. */
	TEST_FEATURE ("with insufficient space in buffer for type");
	msg->data->len = 0;

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
	TEST_ALLOC_FAIL {
		msg->data->len = 0;
		msg->data->size = 0;

		ret = upstart_push_unsigned (msg, 42);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);
			continue;
		}

		TEST_EQ (ret, 0);
		TEST_EQ (msg->data->len, 5);
		TEST_EQ_MEM (msg->data->buf, "u\0\0\0\x2a", 5);
	}


	/* Check that we can write an integer into a message that already has
	 * some thing in it, it should be appended and the buffer increased
	 * in length to include both.
	 */
	TEST_FEATURE ("with space in used buffer");
	TEST_ALLOC_FAIL {
		msg->data->len = 5;
		msg->data->size = BUFSIZ;

		ret = upstart_push_unsigned (msg, 1234567);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);
			continue;
		}

		TEST_EQ (ret, 0);
		TEST_EQ (msg->data->len, 10);
		TEST_EQ_MEM (msg->data->buf, "u\0\0\0\x2au\0\x12\xd6\x87", 10);
	}


	/* Check that we can write a very large number into the message. */
	TEST_FEATURE ("with very large number");
	TEST_ALLOC_FAIL {
		msg->data->len = 10;
		msg->data->size = BUFSIZ;

		ret = upstart_push_unsigned (msg, 0xfedcba98);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);
			continue;
		}

		TEST_EQ (ret, 0);
		TEST_EQ (msg->data->len, 15);
		TEST_EQ_MEM (msg->data->buf + 10, "u\xfe\xdc\xba\x98", 5);
	}


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
	assert0 (nih_io_buffer_push (msg->data, ("u\0\0\0\x2au\0\x12\xd6\x87"
						 "u\xfe\xdc\xba\x98xu\0\0"),
				     19));


	/* Check that we can read an integer from the start of a message;
	 * the integer should be returned in host byte order from the start
	 * of the buffer, and removed from it.
	 */
	TEST_FEATURE ("with integer at start of buffer");
	ret = upstart_pop_unsigned (msg, &value);

	TEST_EQ (ret, 0);
	TEST_EQ_U (value, 42);

	TEST_EQ (msg->data->len, 14);
	TEST_EQ_MEM (msg->data->buf,
		     "u\0\x12\xd6\x87u\xfe\xdc\xba\x98xu\0\0", 14);


	/* Check that we can read an integer from a position inside the
	 * message.  Again it should be removed from it.
	 */
	TEST_FEATURE ("with integer inside buffer");
	ret = upstart_pop_unsigned (msg, &value);

	TEST_EQ (ret, 0);
	TEST_EQ_U (value, 1234567);

	TEST_EQ (msg->data->len, 9);
	TEST_EQ_MEM (msg->data->buf, "u\xfe\xdc\xba\x98xu\0\0", 9);


	/* Check that we can read a very large number from a message. */
	TEST_FEATURE ("with very large number");
	ret = upstart_pop_unsigned (msg, &value);

	TEST_EQ (ret, 0);
	TEST_EQ_U (value, 0xfedcba98);

	TEST_EQ (msg->data->len, 4);
	TEST_EQ_MEM (msg->data->buf, "xu\0\0", 4);


	/* Check that -1 is returned if the type in the buffer is not
	 * correct.
	 */
	TEST_FEATURE ("with incorrect type in buffer");
	ret = upstart_pop_unsigned (msg, &value);

	TEST_LT (ret, 0);
	TEST_EQ_U (value, 0xfedcba98);

	TEST_EQ (msg->data->len, 4);
	TEST_EQ_MEM (msg->data->buf, "xu\0\0", 4);

	nih_io_buffer_shrink (msg->data, 1);


	/* Check that -1 is returned if there is not enough space in the
	 * buffer for an integer.
	 */
	TEST_FEATURE ("with insufficient space in buffer");
	ret = upstart_pop_unsigned (msg, &value);

	TEST_LT (ret, 0);
	TEST_EQ_U (value, 0xfedcba98);

	TEST_EQ (msg->data->len, 3);
	TEST_EQ_MEM (msg->data->buf, "u\0\0", 3);


	/* Check that -1 is returned if there is not enough space for the
	 * type.
	 */
	TEST_FEATURE ("with insufficient space in buffer for type");
	msg->data->len = 0;

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
	TEST_ALLOC_FAIL {
		msg->data->len = 0;
		msg->data->size = 0;

		ret = upstart_push_string (msg, "hello");

		if (test_alloc_failed) {
			TEST_LT (ret, 0);
			continue;
		}

		TEST_EQ (ret, 0);
		TEST_EQ (msg->data->len, 9);
		TEST_EQ_MEM (msg->data->buf, "\0\0\0\x05hello", 9);
	}


	/* Check that we can write a string into a message that already has
	 * some thing in it, it should be appended and the buffer increased
	 * in length to include both.
	 */
	TEST_FEATURE ("with space in used buffer");
	TEST_ALLOC_FAIL {
		msg->data->len = 9;
		msg->data->size = BUFSIZ;

		ret = upstart_push_string (msg, "goodbye");

		if (test_alloc_failed) {
			TEST_LT (ret, 0);
			continue;
		}

		TEST_EQ (ret, 0);
		TEST_EQ (msg->data->len, 20);
		TEST_EQ_MEM (msg->data->buf,
			     "\0\0\0\x05hello\0\0\0\x07goodbye", 20);
	}


	/* Check that we can write the empty string into the message. */
	TEST_FEATURE ("with empty string");
	TEST_ALLOC_FAIL {
		msg->data->len = 20;
		msg->data->size = BUFSIZ;

		ret = upstart_push_string (msg, "");

		if (test_alloc_failed) {
			TEST_LT (ret, 0);
			continue;
		}

		TEST_EQ (ret, 0);
		TEST_EQ (msg->data->len, 24);
		TEST_EQ_MEM (msg->data->buf + 20, "\0\0\0\0", 4);
	}


	/* Check that we can write NULL into the message. */
	TEST_FEATURE ("with NULL string");
	TEST_ALLOC_FAIL {
		msg->data->len = 24;
		msg->data->size = BUFSIZ;

		ret = upstart_push_string (msg, NULL);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);
			continue;
		}

		TEST_EQ (ret, 0);
		TEST_EQ (msg->data->len, 28);
		TEST_EQ_MEM (msg->data->buf + 24, "\xff\xff\xff\xff", 4);
	}


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
	assert0 (nih_io_buffer_push (msg->data,
				     ("\0\0\0\x05hello\0\0\0\x07goodbye"
				      "\0\0\0\0\xff\xff\xff\xff"
				      "\0\0\0\x04te"), 34));


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
test_push_array (void)
{
	NihIoMessage  *msg;
	char         **array;
	int            ret;

	TEST_FUNCTION ("upstart_push_array");
	msg = nih_io_message_new (NULL);


	/* Check that we can write an array into an empty message that has
	 * room; the array should show up as an 'a' record with each element
	 * following as a string until we hit the end.
	 */
	TEST_FEATURE ("with space in empty buffer");
	array = nih_str_array_new (NULL);
	NIH_MUST (nih_str_array_add (&array, NULL, NULL, "foo"));
	NIH_MUST (nih_str_array_add (&array, NULL, NULL, "bar"));

	TEST_ALLOC_FAIL {
		msg->data->len = 0;
		msg->data->size = 0;

		ret = upstart_push_array (msg, array);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);
			continue;
		}

		TEST_EQ (ret, 0);
		TEST_EQ (msg->data->len, 19);
		TEST_EQ_MEM (msg->data->buf, ("a\0\0\0\03foo\0\0\0\03bar"
					      "\xff\xff\xff\xff"), 19);
	}

	nih_free (array);


	/* Check that we can write an array into a message that already has
	 * some thing in it, it should be appended and the buffer increased
	 * in length to include both.
	 */
	TEST_FEATURE ("with space in used buffer");
	array = nih_str_array_new (NULL);
	NIH_MUST (nih_str_array_add (&array, NULL, NULL, "frodo"));
	NIH_MUST (nih_str_array_add (&array, NULL, NULL, "bilbo"));

	TEST_ALLOC_FAIL {
		msg->data->len = 19;
		msg->data->size = BUFSIZ;

		ret = upstart_push_array (msg, array);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);
			continue;
		}

		TEST_EQ (ret, 0);
		TEST_EQ (msg->data->len, 42);
		TEST_EQ_MEM (msg->data->buf,
			     ("a\0\0\0\03foo\0\0\0\03bar\xff\xff\xff\xff"
			      "a\0\0\0\05frodo\0\0\0\05bilbo\xff\xff\xff\xff"),
			     42);
	}

	nih_free (array);


	/* Check that we can write an empty array into the message. */
	TEST_FEATURE ("with empty array");
	array = nih_str_array_new (NULL);

	TEST_ALLOC_FAIL {
		msg->data->len = 42;
		msg->data->size = BUFSIZ;

		ret = upstart_push_array (msg, array);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);
			continue;
		}

		TEST_EQ (ret, 0);
		TEST_EQ (msg->data->len, 47);
		TEST_EQ_MEM (msg->data->buf + 42, "a\xff\xff\xff\xff", 5);
	}

	nih_free (array);


	/* Check that we can write NULL into the message. */
	TEST_FEATURE ("with NULL array");
	TEST_ALLOC_FAIL {
		msg->data->len = 47;
		msg->data->size = BUFSIZ;

		ret = upstart_push_array (msg, NULL);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);
			continue;
		}

		TEST_EQ (ret, 0);
		TEST_EQ (msg->data->len, 48);
		TEST_EQ_MEM (msg->data->buf + 47, "A", 1);
	}


	nih_free (msg);
}

void
test_pop_array (void)
{
	NihIoMessage  *msg;
	char         **value;
	int            ret;

	TEST_FUNCTION ("upstart_pop_array");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     ("a\0\0\0\03foo\0\0\0\03bar"
				      "\xff\xff\xff\xff"
				      "a\0\0\0\05frodo\0\0\0\05bilbo"
				      "\xff\xff\xff\xff"
				      "a\xff\xff\xff\xff"
				      "Axa\0\0\0\04te"), 56));


	/* Check that we can read an array from the start of a message;
	 * the array should be allocated with nih_alloc, and each string
	 * copied from the start of the buffer, with a final NULL entry
	 * appended.  It should then be removed from the message.
	 */
	TEST_FEATURE ("with array at start of buffer");
	ret = upstart_pop_array (msg, NULL, &value);

	TEST_EQ (ret, 0);
	TEST_ALLOC_SIZE (value, sizeof (char *) * 3);
	TEST_ALLOC_PARENT (value[0], value);
	TEST_ALLOC_PARENT (value[1], value);
	TEST_EQ_STR (value[0], "foo");
	TEST_EQ_STR (value[1], "bar");
	TEST_EQ_P (value[2], NULL);

	TEST_EQ (msg->data->len, 37);
	TEST_EQ_MEM (msg->data->buf,
		     ("a\0\0\0\05frodo\0\0\0\05bilbo\xff\xff\xff\xff"
		      "a\xff\xff\xff\xff"
		      "Axa\0\0\0\04te"), 37);

	nih_free (value);


	/* Check that we can read an array from a position inside the
	 * message, and then removed.
	 */
	TEST_FEATURE ("with array inside buffer");
	ret = upstart_pop_array (msg, NULL, &value);

	TEST_EQ (ret, 0);
	TEST_ALLOC_SIZE (value, sizeof (char *) * 3);
	TEST_ALLOC_PARENT (value[0], value);
	TEST_ALLOC_PARENT (value[1], value);
	TEST_EQ_STR (value[0], "frodo");
	TEST_EQ_STR (value[1], "bilbo");
	TEST_EQ_P (value[2], NULL);

	TEST_EQ (msg->data->len, 14);
	TEST_EQ_MEM (msg->data->buf, ("a\xff\xff\xff\xff"
				      "Axa\0\0\0\04te"), 14);

	nih_free (value);


	/* Check that we can read the empty array from the message. */
	TEST_FEATURE ("with empty array in buffer");
	ret = upstart_pop_array (msg, NULL, &value);

	TEST_EQ (ret, 0);
	TEST_ALLOC_SIZE (value, sizeof (char *));
	TEST_EQ_P (value[0], NULL);

	TEST_EQ (msg->data->len, 9);
	TEST_EQ_MEM (msg->data->buf, "Axa\0\0\0\04te", 9);

	nih_free (value);


	/* Check that we can read NULL from the message. */
	TEST_FEATURE ("with NULL array in buffer");
	ret = upstart_pop_array (msg, NULL, &value);

	TEST_EQ (ret, 0);
	TEST_EQ_P (value, NULL);

	TEST_EQ (msg->data->len, 8);
	TEST_EQ_MEM (msg->data->buf, "xa\0\0\0\04te", 8);


	/* Check that -1 is returned if the type of the following item is
	 * not an array.
	 */
	TEST_FEATURE ("with wrong type in buffer");
	ret = upstart_pop_array (msg, NULL, &value);

	TEST_LT (ret, 0);
	TEST_EQ_P (value, NULL);

	TEST_EQ (msg->data->len, 8);
	TEST_EQ_MEM (msg->data->buf, "xa\0\0\0\04te", 8);

	nih_io_buffer_shrink (msg->data, 1);


	/* Check that -1 is returned if there is enough space in the buffer
	 * for the length of a component string, but not the string.
	 */
	TEST_FEATURE ("with insufficient space for element");
	ret = upstart_pop_array (msg, NULL, &value);

	TEST_LT (ret, 0);
	TEST_EQ_P (value, NULL);

	TEST_EQ (msg->data->len, 2);
	TEST_EQ_MEM (msg->data->buf, "te", 2);


	/* Check that -1 is returned if there is not enough space in the
	 * buffer for the length of the string.
	 */
	TEST_FEATURE ("with insufficient space in buffer for element length");
	strcpy (msg->data->buf, "a\0\0");
	msg->data->len = 3;

	ret = upstart_pop_array (msg, NULL, &value);

	TEST_LT (ret, 0);
	TEST_EQ_P (value, NULL);


	/* Check that -1 is returned if there is not enough space in the
	 * buffer for the type.
	 */
	TEST_FEATURE ("with insufficient space in buffer for type");
	msg->data->len = 0;

	ret = upstart_pop_array (msg, NULL, &value);

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
	TEST_ALLOC_FAIL {
		msg->data->len = 0;
		msg->data->size = 0;

		ret = upstart_push_header (msg, UPSTART_NO_OP);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);
			continue;
		}

		TEST_EQ (ret, 0);
		TEST_EQ (msg->data->len, 12);
		TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0\0\0", 12);
	}


	/* Check that we can write a header into a message that already has
	 * something in it, it should be appended and the buffer increased
	 * in length to include both.
	 */
	TEST_FEATURE ("with space in used buffer");
	TEST_ALLOC_FAIL {
		msg->data->len = 12;
		msg->data->size = BUFSIZ;

		ret = upstart_push_header (msg, UPSTART_NO_OP);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);
			continue;
		}

		TEST_EQ (ret, 0);
		TEST_EQ (msg->data->len, 24);
		TEST_EQ_MEM (msg->data->buf,
			     "upstart\n\0\0\0\0upstart\n\0\0\0\0", 24);
	}


	nih_free (msg);
}

void
test_pop_header (void)
{
	NihIoMessage       *msg;
	UpstartMessageType  value;
	int                 ret;

	TEST_FUNCTION ("upstart_pop_header");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     ("upstart\n\0\0\0\0upstart\n\0\0\0\0"
				      "upstart\n\0\0"), 34));


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

	TEST_EQ (msg->data->len, 10);
	TEST_EQ_MEM (msg->data->buf, "upstart\n\0\0", 10);


	/* Check that -1 is returned if there is not enough space in the
	 * buffer for the magic string.
	 */
	TEST_FEATURE ("with insufficient space in buffer for magic");
	msg->data->len = 5;
	ret = upstart_pop_header (msg, &value);

	TEST_LT (ret, 0);
	TEST_EQ (value, -1);

	TEST_EQ (msg->data->len, 5);
	TEST_EQ_MEM (msg->data->buf, "upsta", 5);


	/* Check that -1 is returned if the magic string doesn't match.
	 */
	TEST_FEATURE ("with wrong magic string in buffer");
	msg->data->len = 0;
	assert0 (nih_io_buffer_push (msg->data, "downstop\0\0\0\0", 12));

	ret = upstart_pop_header (msg, &value);

	TEST_LT (ret, 0);
	TEST_EQ (value, -1);

	TEST_EQ (msg->data->len, 12);
	TEST_EQ_MEM (msg->data->buf, "downstop\0\0\0\0", 12);


	nih_free (msg);
}


void
test_push_pack (void)
{
	NihIoMessage  *msg;
	char         **array;
	int            ret;

	TEST_FUNCTION ("upstart_push_pack");
	msg = nih_io_message_new (NULL);


	/* Check that we can write a series of different values in a single
	 * function call, resulting in them being placed at the start of the
	 * message in order.
	 */
	TEST_FEATURE ("with empty buffer");
	array = nih_str_array_new (NULL);
	NIH_MUST (nih_str_array_add (&array, NULL, NULL, "foo"));
	NIH_MUST (nih_str_array_add (&array, NULL, NULL, "bar"));

	TEST_ALLOC_FAIL {
		msg->data->len = 0;
		msg->data->size = 0;

		ret = upstart_push_pack (msg, "iusai", 100, 0x98765432,
					 "string value", array, -42);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);
			continue;
		}

		TEST_EQ (ret, 0);
		TEST_EQ (msg->data->len, 50);
		TEST_EQ_MEM (msg->data->buf, ("i\0\0\0\x64u\x98\x76\x54\x32"
					      "\0\0\0\x0cstring value"
					      "a\0\0\0\03foo\0\0\0\03bar"
					      "\xff\xff\xff\xff"
					      "i\xff\xff\xff\xd6"), 50);
	}

	nih_free (array);


	/* Check that we can write a series of different values onto the
	 * end of an existing buffer, without smashing what was already
	 * there.
	 */
	TEST_FEATURE ("with used buffer");
	TEST_ALLOC_FAIL {
		msg->data->len = 50;
		msg->data->size = BUFSIZ;

		ret = upstart_push_pack (msg, "ii", 98, 100);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);
			continue;
		}

		TEST_EQ (ret, 0);
		TEST_EQ (msg->data->len, 60);
		TEST_EQ_MEM (msg->data->buf, ("i\0\0\0\x64u\x98\x76\x54\x32"
					      "\0\0\0\x0cstring value"
					      "a\0\0\0\03foo\0\0\0\03bar"
					      "\xff\xff\xff\xff"
					      "i\xff\xff\xff\xd6"
					      "i\0\0\0\x62i\0\0\0\x64"), 60);
	}

	nih_free (msg);
}

void
test_pop_pack (void)
{
	NihIoMessage  *msg;
	char          *str, **array;
	unsigned int   uint;
	int            ret, int1, int2;

	TEST_FUNCTION ("upstart_pop_pack");
	msg = nih_io_message_new (NULL);
	assert0 (nih_io_buffer_push (msg->data,
				     ("i\0\0\0\x64u\x98\x76\x54\x32"
				      "\0\0\0\x0cstring value"
				      "a\0\0\0\05frodo\0\0\0\05bilbo"
				      "\xff\xff\xff\xff"
				      "i\xff\xff\xff\xd6"
				      "i\0\0\0\x62i\0\0\0\x64"
				      "i\0\0\0\x13\0\0\0\x04te"), 75));


	/* Check that we can read a series of different values in a single
	 * function call, removing them all from the buffer.
	 */
	TEST_FEATURE ("with variables at start of buffer");
	ret = upstart_pop_pack (msg, NULL, "iusai", &int1, &uint,
				&str, &array, &int2);

	TEST_EQ (ret, 0);
	TEST_EQ (int1, 100);
	TEST_EQ_U (uint, 0x98765432);
	TEST_ALLOC_SIZE (str, 13);
	TEST_EQ (str[12], '\0');
	TEST_EQ_STR (str, "string value");
	TEST_ALLOC_SIZE (array, sizeof (char *) * 3);
	TEST_ALLOC_PARENT (array[0], array);
	TEST_ALLOC_PARENT (array[1], array);
	TEST_EQ_STR (array[0], "frodo");
	TEST_EQ_STR (array[1], "bilbo");
	TEST_EQ_P (array[2], NULL);
	TEST_EQ (int2, -42);

	TEST_EQ (msg->data->len, 21);
	TEST_EQ_MEM (msg->data->buf, ("i\0\0\0\x62i\0\0\0\x64"
				      "i\0\0\0\x13\0\0\0\x04te"), 21);

	nih_free (str);


	/* Check that we can read a series of different values from a
	 * point already inside the buffer.
	 */
	TEST_FEATURE ("with variables inside buffer");
	ret = upstart_pop_pack (msg, NULL, "ii", &int1, &int2);

	TEST_EQ (ret, 0);
	TEST_EQ (int1, 98);
	TEST_EQ (int2, 100);

	TEST_EQ (msg->data->len, 11);
	TEST_EQ_MEM (msg->data->buf, ("i\0\0\0\x13\0\0\0\x04te"), 11);


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
	test_push_array ();
	test_pop_array ();
	test_push_header ();
	test_pop_header ();
	test_push_pack ();
	test_pop_pack ();

	return 0;
}
