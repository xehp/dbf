/*
 * dbf.cpp
 *
 * DrekkarBinaryFormat (DBF)
 *
 *  Created on: Apr 3, 2018
 *      Author: henrik
 */

#include <stdio.h>
#include <ctype.h>



#if ((defined __arm__) && (!defined __linux__))
#include "serialDev.h"
#elif defined __linux__ || defined __WIN32
#include <stdint.h>
#include <inttypes.h>
#include <assert.h>
#include <string.h>
#include "sys_time.h"
#endif


#include "crc32.h"
#include "dbf.h"

#ifdef DBF_AND_ASCII
#include "utility_functions.h"
#endif


#define LOG_PREFIX "Dbf:  "
#define LOG_SUFIX "\n"

#ifndef DBF_FIXED_MSG_SIZE
#define INITIAL_BUFFER_SIZE 256
#endif
#define ASCII_OFFSET 64

#define IGNORE_UNTIL_SILENCE_MS 100

/*
DebugableBinaryFormat (DBF) AKA DrekkarBinaryFormat

The purpose is to encode messages containing numbers and strings into a compact binary format.
Sort of like LEB128 but this encodes strings also and will tell if next value is string or int.
A requirement is easy debugging. It must be possible to unpack a message into its components
called codes. So some basic formatting information is also part of the encoding.
Small numbers and common ASCII characters must be encoded with a single byte.

The message is composed of codes.
A code is made up of a start sub code and zero or more extension sub codes.
The start code will tell if it is a positive or negative number or some special code.
The range depends on implementation (if 32 bit or 64 bit variables are used).
It is recommended to use 64 bits and that is assumed below.

Characters are sent as n = Unicode - 64. By subtracting the unicde code with 64
characters "@ABCDE...xyz~" etc are sent as 0-63 and characters space to '?' are sent
as -1 to -32. These are then coded same as numbers and for those ranges these fit
in a byte. Which was our requirement for compactness.



Below bits in a sub code are shown as 'b'. All bits together are called n.


When displayed as readable text numbers and strings etc are separated by ',' or space.
Strings are quoted. Example message:
0, 1, -1, 63, 64, -1000, 65536, "hello?  abc!", "one, more", 4711


If the data did not fit in one byte (what is left after formatting info)
then the least significant bits are written first and the more significant
are added in extension codes (as many as needed).

A "code" is made up of one or more sub codes. 
First a start code (any sub code except the extension code) followed
by zero or more extension codes.


Sub codes:

1bbbbbbb
	Extension code with 7 more bits of data.
	Sent after one of the other sub codes below if there was not room for all data.
	least significant is sent in first sub code and then more and more significant
	until only zeroes remain in the more significant part of the number.
	Those zeroes do not need to be sent.

01bbbbbb
	Number sub code. This is for a positive number n. Range 0 <= n < 2^63.
	the 6 least significant data bits of n are written to the bbbbbb bits.
	If the number was larger than 63 some Extension codes will be needed.
	The number can be an integer to be shown as decimal, hexadecimal or
	part of a string. Depending on any preceding format codes.

001bbbbb
	Number sub code. This is for a negative number n. Range -2^63 <= n < 0.
	Note that Unicode characters might also be encoded as negative numbers
	since they are subtracted with 64 before being encoded. This so that the most
	common 7 bit ascii characters will fit without using extension codes.

0001bbbb
	Format code or CRC
	If this appears last in a message it is a CRC. Otherwise it is format code.

	CRC
		Where n is the CRC, it is typically 32 bits using extension bits.
		The DbfBegin, DbfEnd & networking options are typically not included in the CRC.

	Format codes:

		0
			DBF_INT_BEGIN_CODE
			One or more signed integers follow. To be displayed as positive or negative decimal.
			This is also default in a each message.

		1
			DBF_STR_BEGIN_CODE
			A string with Unicode characters follow. To be displayed as quoted string.
			The string continues until next non number sub code.
			Display using same escape sequences as JSON if needed.

		3, 4, 5...
			Reserved, up to 2^64 special codes are possible so they should not run out.
			But only 0-15 can be encoded without extension sub codes so it is preferred to keep within that.
			Suggested additions:
				3
				  One or more unsigned numbers to be displayed in hexadecimal follows.
				  These can still be encoded using 001bbbbb. If all bits are one that can be
				  encoded as -1 and then displayed as 0xFFFFFFFFFFFFFFFF.
				4
				  embedded byte buffer follows, to be displayed in hex.
				  Perhaps first number after this shall be used to tell how
				  many bytes there is in the byte buffer. In each of the following
				  number codes 4 bytes of binary data is then stored.
				5
				  binary64 floating point format.
				  64-bit IEEE 754 floating point, 1 sign, 11 exponent and 52 mantissa bits
				  mantissa is perhaps better called coefficient or significand.
				  Probably it shall be encoded into 2 codes.
				  	  1) sign and significand/mantissa
				  	  2) exponent
				  Note that the exponent is then for power of 2 not power of 10 as in Scientific notation.
				  If that can be changed to 10 without to much complexity it would make a log so much more readable.
				6 and 7
				  Begin and end of a JSON style object '{' '}' respectively.
                                  Remember to diplay with the ':' as delimiter also.
				8 and 9
				  Begin and end of a JSON style array '[' ']' respectively.
				15
				  Do nothing. Do not change format.


00001bbb
	repetition code

	This is to use this for repeating codes.
	Such as if previous code is repeated a number of times then this is sent instead
	to say how many times.

	Only number codes (positive or negative) can be repeated this way.
	n will tell how many extra repetitions (additional to the first in the sequence).

	Exception It can happen that this code is first in a message.
	That is reserved, perhaps it will be used as version code.

000001bb
	Reserved

0000001b
	Reserved.

	Perhaps to be used as below:
		00000010 Start of sub message/text
		00000011 End of sub message/text

00000001
	End of DBF.	No extension subcode is allowed together with this sub code.
	Any data sent after this is considered non DBF until a "Begin of a message"
	is received.
	It is OK to repeat this code until there is a message send.
	Note that this is not same as "End of sub message", see above.

00000000
	Begin of DBF message.
	Also used as separator between messages.
	No extension subcode is allowed together with this code.
	The message shall end with an "End of message" code.

TODO
These are from ASCII table:
  0  NUL (null)
  1  SOH (start of heading)
  2  STX (start of text)
  3  ETX (end of text)
  4  EOT (end of transmission)
Would have been nice to be compatible with those.

*/

#if defined __linux__ || defined __WIN32
void debug_log(const char *str)
{
	fprintf(stdout, "dbf: %s\n",str);
	fflush(stdout);
}
#elif (defined __arm__)
void debug_log(const char *str)
{
	usartPrint(DEV_USART2, str);
	usartPrint(DEV_USART2, "\n");
}
#else
#define debug_log(str)
#endif

static long debug_counter = 0;


static int is_char_part_of_number(int ch)
{
	return isdigit(ch) || ch=='.' || ch=='x' || ch=='X';
}

static int is_char_part_of_word(int ch)
{
	return ((ch!='\"') && (ch!='\\') && (isgraph(ch)));
}

// Gives the length of a word in a string. A word in this case is
// sequence of characters not including space and or '"' or '\'.
static int word_length(const char* str)
{
	int n = 0;

    // Find end of word
    while (is_char_part_of_word(*str))
    {
    	str++;
    	n++;
    }

    return n;
}


//struct DbfSerializer dbfSerializer;
/*
static void logHex(FILE* stream, const char *prefix, const unsigned char *ptr, unsigned int len, const char *sufix)
{
	fprintf(stream, "%s", prefix);
	for(unsigned int i = 0; i < len; i++)
	{
		fprintf(stream, "%02x", ptr[i]);
	}
	fprintf(stream, "%s", sufix);
	fflush(stream);
}
*/
void DbfSerializerDebug()
{
	if (debug_counter)
	{
		printf("debug_counter FAIL %ld\n", debug_counter);
	}
}

// Remember that DbfSerializerDeinit must be called when the serializer is no longer
// needed otherwise there will be a memory leak.
static void DbfSerializerInitBase(DbfSerializer *s)
{
	s->pos = 0;
	s->repeat_counter = 0;
	s->prev_code = 0;
	#if (!defined DBF_FIXED_MSG_SIZE)
	s->capacity = INITIAL_BUFFER_SIZE;
	s->buffer = ST_MALLOC(s->capacity);
	debug_counter++;
	#endif
};

// Remember that DbfSerializerDeinit must be called when the serializer is no longer
// needed otherwise there will be a memory leak.
void DbfSerializerInit(DbfSerializer *s)
{
	DbfSerializerInitBase(s);
	s->encoderState = DBF_ENCODER_IDLE;
};

#ifdef DBF_AND_ASCII
// TODO add parameter for separator.
void DbfSerializerInitAscii(DbfSerializer *s)
{
	DbfSerializerInitBase(s);
	s->encoderState = DBF_ENCODER_ASCII_MODE;
	s->prev_code = ' '; // This is the default separator character.
}

void DbfSerializerInitSeparator(DbfSerializer *s, int64_t ch)
{
	DbfSerializerInitAscii(s);
	DbfSerializerSetAsciiSeparator(s, ch);
}

void DbfSerializerSetAsciiSeparator(DbfSerializer *s, int64_t ch)
{
	assert(s);
	#if (!defined DBF_FIXED_MSG_SIZE)
	ST_ASSERT_SIZE(s->buffer, s->capacity);
	#endif

	// prev_code is not used in ascii mode so will use it as separator character instead.
	s->prev_code = ch;
}
#endif

// Give the serializer a chance to free memory that it allocated.
void DbfSerializerDeinit(DbfSerializer *s)
{
	//debug_log("DbfSerializerInit");
	assert(s);
	s->pos = 0;
	s->encoderState = DBF_ENCODER_IDLE;
	s->repeat_counter = 0;
	s->prev_code = 0;
	#if (!defined DBF_FIXED_MSG_SIZE)
	ST_FREE_SIZE(s->buffer, s->capacity);
	s->buffer = NULL;
	s->capacity = 0;
	debug_counter--;
	#endif
};

// Reuse a serializer to serialize a new message.
void DbfSerializerReset(DbfSerializer *s)
{
	assert(s);
	#if (!defined DBF_FIXED_MSG_SIZE)
	ST_ASSERT_SIZE(s->buffer, s->capacity);
	#endif
	s->pos = 0;
	s->encoderState = DBF_ENCODER_IDLE;
	s->repeat_counter = 0;
	s->prev_code = 0;
}

static void DbfSerializerResizeIfNeeded(DbfSerializer* s, long needed_capacity)
{
	assert(s);
	#if (!defined DBF_FIXED_MSG_SIZE)
	ST_ASSERT_SIZE(s->buffer, s->capacity);
	if (needed_capacity >= s->capacity)
	{
		s->buffer = ST_RESIZE(s->buffer, s->capacity, s->capacity*2);
		s->capacity *= 2;
	}
	#endif
}

static void DbfSerializerPutByte(DbfSerializer *s, char b)
{
	assert(s);

	#if (!defined DBF_FIXED_MSG_SIZE)
	ST_ASSERT_SIZE(s->buffer, s->capacity);

	// If the below is commented out, make sure the buffer is big enough before calling this.
	//DbfSerializerResize(s, dbfSerializer->pos);

	s->buffer[s->pos] = b;
	s->pos++;

	assert(s->pos <= s->capacity);

	#else

	if (s->pos<sizeof(s->buffer))
	{
		s->buffer[s->pos] = b;
		s->pos++;
	}
	else
	{
		debug_log("DbfSerializerPutByte full");
		s->encoderState = DBF_ENCODER_ERROR;
	}

	#endif
}

/**
 * Parameters:
 * dbfSerializer: pointer to the struct that the data is written to.
 * code: Tells the type of data to be written.
 * nofb: The number of data bits that will fit in one byte together with c.
 * data: The actual data
 */
static void DbfSerializerEncodeData64_step2(DbfSerializer *s, unsigned int code, unsigned int nofb, uint64_t data)
{
	// Make sure there is room in the buffer. Make it bigger if needed.
	DbfSerializerResizeIfNeeded(s, s->pos + 12);

	const unsigned int m = (1<<nofb)-1; // mask for data to be written together with format code.

	// Send the type of code part and as many bits as will fit in first byte.
	DbfSerializerPutByte(s, code + (data & m));
	data = data >> nofb;

	// Then as many extension sub codes as needed for the more significant bits.
	while (data > 0)
	{
		DbfSerializerPutByte(s, DBF_EXT_CODEID + (data & DBF_EXT_DATAMASK));
		data = data >> DBF_EXT_DATANBITS;
	}
}

static void DbfSerializerEncodeData32_step2(DbfSerializer *s, unsigned int code, unsigned int nofb, uint32_t data)
{
	// Make sure there is room in the buffer. Make it bigger if needed.
	DbfSerializerResizeIfNeeded(s, s->pos + 8);

	const unsigned int m = (1<<nofb)-1; // mask for data to be written together with format code.

	// Send the type of code part and as many bits as will fit in first byte.
	DbfSerializerPutByte(s, code + (data & m));
	data = data >> nofb;

	// Then as many extension sub codes as needed for the more significant bits.
	while (data > 0)
	{
		DbfSerializerPutByte(s, DBF_EXT_CODEID + (data & DBF_EXT_DATAMASK));
		data = data >> DBF_EXT_DATANBITS;
	}
}

static void DbfSerializerWriteRepeat(DbfSerializer *s)
{
	if (s->repeat_counter > 0)
	{
		DbfSerializerEncodeData64_step2(s, DBF_REPEAT_CODEID, DBF_REPEAT_DATANBITS, s->repeat_counter);
		s->repeat_counter = 0;
		s->prev_code = 0;
	}
}

static void DbfSerializerEncodeData64(DbfSerializer *s, unsigned int code, unsigned int nofb, uint64_t data)
{
	DbfSerializerWriteRepeat(s);
	DbfSerializerEncodeData64_step2(s, code, nofb, data);
}

static void DbfSerializerEncodeData32(DbfSerializer *s, unsigned int code, unsigned int nofb, uint32_t data)
{
	DbfSerializerWriteRepeat(s);
	DbfSerializerEncodeData32_step2(s, code, nofb, data);
}

void DbfSerializerWriteCrc(DbfSerializer *s)
{
	assert(s);
	#if (!defined DBF_FIXED_MSG_SIZE)
	ST_ASSERT_SIZE(s->buffer, s->capacity);
	#endif

	DbfSerializerWriteRepeat(s);
	const uint32_t crc = crc32_calculate((const unsigned char *)s->buffer, s->pos);
	DbfSerializerEncodeData32_step2(s, DBF_FMTCRC_CODEID, DBF_FMTCRC_DATANBITS, crc);
}

static void DbfSerializerWriteCode64(DbfSerializer *s, int64_t i)
{
	if (i == s->prev_code)
	{
		s->repeat_counter++;
	}
	else
	{
		if (i>=0)
		{
			DbfSerializerEncodeData64(s, DBF_PINT_CODEID, DBF_PINT_DATANBITS, i);
		}
		else
		{
			DbfSerializerEncodeData64(s, DBF_NINT_CODEID, DBF_NINT_DATANBITS, -1LL-i);
		}
		s->prev_code = i;
	}
}

static void DbfSerializerWriteCode32(DbfSerializer *s, int32_t i)
{
	#ifdef DBF_OPTIMIZE_FOR_32_BITS
	if (i == s->prev_code)
	{
		s->repeat_counter++;
	}
	else
	{
		if (i>=0)
		{
			DbfSerializerEncodeData32(s, DBF_PINT_CODEID, DBF_PINT_DATANBITS, i);
		}
		else
		{
			DbfSerializerEncodeData32(s, DBF_NINT_CODEID, DBF_NINT_DATANBITS, -1L-i);
		}
		s->prev_code = i;
	}
	#else
	DbfSerializerWriteCode64(s, i);
	#endif
}

void DbfSerializerWriteInt32(DbfSerializer *s, int32_t i)
{
	assert(s);
	#if (!defined DBF_FIXED_MSG_SIZE)
	ST_ASSERT_SIZE(s->buffer, s->capacity);
	#endif

	#ifdef DBF_OPTIMIZE_FOR_32_BITS
	//printf("DbfSerializerWriteInt %d\n", i);
	// If format is not already numeric then send the code "INT_BEGIN_CODE".
	switch(s->encoderState)
	{
		case DBF_ENCODING_INT:
			// Do nothing
			break;
		case DBF_ENCODER_IDLE:
			// Int is default so no need to send formating code.
			s->encoderState = DBF_ENCODING_INT;
			break;
		case DBF_ENCODER_ERROR:
			return;
		default:
			// Previous code was not an integer so we must first send the format code.
			// Using the FMTCRC code to send the format.
			DbfSerializerEncodeData32(s, DBF_FMTCRC_CODEID, DBF_FMTCRC_DATANBITS, DBF_INT_BEGIN_CODE);
			s->encoderState = DBF_ENCODING_INT;
			break;
	}
	DbfSerializerWriteCode32(s, i);
	#else
	DbfSerializerWriteInt64(s, i);
	#endif
}

void DbfSerializerWriteInt64(DbfSerializer *s, int64_t i)
{
	//printf("DbfSerializerWriteInt %d\n", i);
	// If format is not already numeric then send the code "INT_BEGIN_CODE".
	switch(s->encoderState)
	{
		case DBF_ENCODING_INT:
			// Do nothing
			break;
		case DBF_ENCODER_IDLE:
			// Int is default so no need to send formating code.
			s->encoderState = DBF_ENCODING_INT;
			break;
		case DBF_ENCODER_ERROR:
			return;
		#ifdef DBF_AND_ASCII
		case DBF_ENCODER_ASCII_MODE:
		{
			// Make buffer bigger if needed.
			DbfSerializerResizeIfNeeded(s, s->pos + 32);

			// Add word separator character (typically space or slash) if needed.
			if (s->pos != 0)
			{
				s->buffer[s->pos++] = s->prev_code;
			}

			// Write the number in ascii.
			s->pos += snprintf((char*)s->buffer+s->pos, s->capacity-s->pos, "%lld", (long long int)i);
			return;
		}
		#endif
		default:
			// Previous code was not an integer so we must first send the format code.
			// Using the FMTCRC code to send the format.
			DbfSerializerEncodeData32(s, DBF_FMTCRC_CODEID, DBF_FMTCRC_DATANBITS, DBF_INT_BEGIN_CODE);
			s->encoderState = DBF_ENCODING_INT;
			break;
	}
	DbfSerializerWriteCode64(s, i);
}


static void DbfSerializerBeginWriteNumber(DbfSerializer *s)
{
	switch(s->encoderState)
	{
		case DBF_ENCODER_ERROR:
			return;
		#ifdef DBF_AND_ASCII
		case DBF_ENCODER_ASCII_MODE:
		{
			// Make sure there is room in the buffer.
			DbfSerializerResizeIfNeeded(s, s->pos + 8);

			// Add word separator character (typically space or slash) if needed.
			if (s->pos != 0)
			{
				s->buffer[s->pos] = s->prev_code;
				++s->pos;
			}

			return;
		}
		#endif
		default:
			//printf("DbfSerializerWriteString '%s'\n", str);

			// Write a string format code to tell receiver that it is a string that follows.
			// It is needed also if previous parameter was a string since this also separates strings.
			DbfSerializerEncodeData32(s, DBF_FMTCRC_CODEID, DBF_FMTCRC_DATANBITS, DBF_INT_BEGIN_CODE);
			s->encoderState = DBF_ENCODING_STR;
			break;
	}
}

static void DbfSerializerBeginWriteWord(DbfSerializer *s)
{
	switch(s->encoderState)
	{
		case DBF_ENCODER_ERROR:
			return;
		#ifdef DBF_AND_ASCII
		case DBF_ENCODER_ASCII_MODE:
		{
			// Make sure there is room in the buffer.
			DbfSerializerResizeIfNeeded(s, s->pos + 8);

			// Add word separator character (typically space or slash) if needed.
			if (s->pos != 0)
			{
				s->buffer[s->pos] = s->prev_code;
				++s->pos;
			}

			return;
		}
		#endif
		default:
			//printf("DbfSerializerWriteString '%s'\n", str);

			// Write a string format code to tell receiver that it is a string that follows.
			// It is needed also if previous parameter was a string since this also separates strings.
			DbfSerializerEncodeData32(s, DBF_FMTCRC_CODEID, DBF_FMTCRC_DATANBITS, DBF_WORD_BEGIN_CODE);
			s->encoderState = DBF_ENCODING_STR;
			break;
	}
}


static void DbfSerializerBeginWriteString(DbfSerializer *s)
{
	switch(s->encoderState)
	{
		case DBF_ENCODER_ERROR:
			return;
		#ifdef DBF_AND_ASCII
		case DBF_ENCODER_ASCII_MODE:
		{
			// Make sure there is room in the buffer.
			DbfSerializerResizeIfNeeded(s, s->pos + 8);

			// Add word separator character (typically space or slash) if needed.
			if (s->pos != 0)
			{
				s->buffer[s->pos] = s->prev_code;
				++s->pos;
			}

			// String is different from word in that they have quotes.
			// So add the begin quote.
			DbfSerializerPutByte(s, '\"');

			// Use this variable to know that an end quote shall also be added.
			s->repeat_counter = 1;

			return;
		}
		#endif
		default:
			//printf("DbfSerializerWriteString '%s'\n", str);

			// Write a string format code to tell receiver that it is a string that follows.
			// It is needed also if previous parameter was a string since this also separates strings.
			DbfSerializerEncodeData32(s, DBF_FMTCRC_CODEID, DBF_FMTCRC_DATANBITS, DBF_STR_BEGIN_CODE);
			s->encoderState = DBF_ENCODING_STR;
			break;
	}
}

static void DbfSerializerEndWrite(DbfSerializer *s)
{
	switch(s->encoderState)
	{
		case DBF_ENCODER_ERROR:
			return;
		#ifdef DBF_AND_ASCII
		case DBF_ENCODER_ASCII_MODE:
		{
			// Make sure there is room in buffer for end quote and terminating zero.
			DbfSerializerResizeIfNeeded(s, s->pos+1);

			// Add the end quote if needed.
			if (s->repeat_counter)
			{
				DbfSerializerPutByte(s, '\"');
				s->repeat_counter = 0;
			}

			s->buffer[s->pos] = 0;
			return;
		}
		#endif
		case DBF_ENCODING_STR:
		default:
			DbfSerializerWriteRepeat(s);
			break;
	}
	assert(s->pos <= s->capacity);
}

// This writes 7 bit ascii strings.
// TODO A function to encode Unicode strings.
static void serializerWrite(DbfSerializer *s, const char *str, size_t len, long code)
{
	// Check some stuff first.
	switch(s->encoderState)
	{
		case DBF_ENCODER_ERROR:
			return;
		#ifdef DBF_AND_ASCII
		case DBF_ENCODER_ASCII_MODE:
		{
			// Make sure there is room in the buffer for string, quotes and terminating zero.
			while ((s->pos + len + 8) > s->capacity)
			{
				// Get a bigger buffer.
				s->buffer = ST_RESIZE(s->buffer, s->capacity, s->capacity*2);
				s->capacity *= 2;
			}

			// Add word separator character (typically space or slash) if needed.
			if (s->pos != 0)
			{
				s->buffer[s->pos] = s->prev_code;
				++s->pos;
			}

			switch(code)
			{
				case DBF_WORD_BEGIN_CODE:
				{
					// Copy the string into the buffer.
					memcpy(s->buffer+s->pos, str, len);
					s->pos += len;
					break;
				}
				case DBF_STR_BEGIN_CODE:
				{
					// Add the quotes and copy the string into the buffer.
					s->buffer[s->pos] = '\"';
					++s->pos;

					// Copy the string into the buffer
					#if 1
					for(int n=0; n<len; ++n)
					{
						int ch = str[n];
						//if (isgraph(ch) && (ch!='\"') && (ch!='\\'))
						if (isprint(ch) && (ch!='\"') && (ch!='\\'))
						{
							s->buffer[s->pos++] = ch;
						}
						else
						{
							s->buffer[s->pos++] = '\\';
							s->buffer[s->pos++] = 'x';
							s->buffer[s->pos++] = utility_encode_digit(ch>>4);
							s->buffer[s->pos++] = utility_encode_digit(ch&0xF);
						}
					}
					#else
					memcpy(s->buffer+s->pos, str, len);
					s->pos += len;
					#endif

					s->buffer[s->pos] = '\"';
					++s->pos;

					break;
				}
				default:
					break;
			}

			// temporarily add a trailing zero.
			s->buffer[s->pos] = 0;

			return;
		}
		#endif
		default:
			//printf("DbfSerializerWriteString '%s'\n", str);

			// Send a string format code to tell receiver that it is a string that follows.
			// It is needed also if previous parameter was a string since this also separates strings.
			// Using DBF_CRC shall be used for format code.
			DbfSerializerEncodeData32(s, DBF_FMTCRC_CODEID, DBF_FMTCRC_DATANBITS, code);
			s->encoderState = DBF_ENCODING_WORD;

			while(*str)
			{
				int i = *str;
				DbfSerializerWriteCode32(s, i-ASCII_OFFSET);
				str++;
			}
			break;
	}
}


// This writes 7 bit ascii strings.
// TODO A function to encode Unicode strings.
void DbfSerializerWriteWord(DbfSerializer *s, const char *str)
{
	assert(s);
	#if (!defined DBF_FIXED_MSG_SIZE)
	ST_ASSERT_SIZE(s->buffer, s->capacity);
	#endif

	// For now only characters larger than ' ' and not over '~' are written.
	const int n = word_length(str);

	if (n==0)
	{
		printf("Zero length word is not allowed.\n");
		serializerWrite(s, str, n, DBF_STR_BEGIN_CODE);
	}
	else
	{
		serializerWrite(s, str, n, DBF_WORD_BEGIN_CODE);
	}
}

// This writes 7 bit ascii strings.
// TODO A function to encode Unicode strings.
void DbfSerializerWriteString(DbfSerializer *s, const char *str)
{
	assert(s);
	#if (!defined DBF_FIXED_MSG_SIZE)
	ST_ASSERT_SIZE(s->buffer, s->capacity);
	#endif

	// For now only characters larger than ' ' and not over '~' are written.
	const int n = strlen(str);

	serializerWrite(s, str, n, DBF_STR_BEGIN_CODE);
}

// If CRC is not needed then call this instead to make sure repeat codes are also written.
void DbfSerializerFinalize(DbfSerializer *s)
{
	assert(s);
	#if (!defined DBF_FIXED_MSG_SIZE)
	ST_ASSERT_SIZE(s->buffer, s->capacity);
	#endif

	DbfSerializerEndWrite(s);
}

// Remember that DbfSerializerWriteCrc or DbfSerializerFinalize must be called before calling this.
const unsigned char* DbfSerializerGetMsgPtr(DbfSerializer *s)
{
	DbfSerializerEndWrite(s);
	return s->buffer;
}

// Remember that DbfSerializerWriteCrc or DbfSerializerFinalize must be called before calling this.
unsigned int DbfSerializerGetMsgLen(const DbfSerializer *s)
{
	assert(s->repeat_counter == 0);
	return s->pos;
}



#if defined __linux__ || defined __WIN32 || __arm__
//#if 1
// Table for faster decoding of first byte in a code.
/*DbfCodeTypesEnum*/ uint8_t codeTypeTable[256] = {
		DbfNct,DbfNct,DbfNct,DbfNct,DbfNct,DbfNct,DbfNct,DbfNct,DbfRcc,DbfRcc,DbfRcc,DbfRcc,DbfRcc,DbfRcc,DbfRcc,DbfRcc,
		DbfFoC,DbfFoC,DbfFoC,DbfFoC,DbfFoC,DbfFoC,DbfFoC,DbfFoC,DbfFoC,DbfFoC,DbfFoC,DbfFoC,DbfFoC,DbfFoC,DbfFoC,DbfFoC,
		DbfNnc,DbfNnc,DbfNnc,DbfNnc,DbfNnc,DbfNnc,DbfNnc,DbfNnc,DbfNnc,DbfNnc,DbfNnc,DbfNnc,DbfNnc,DbfNnc,DbfNnc,DbfNnc,
		DbfNnc,DbfNnc,DbfNnc,DbfNnc,DbfNnc,DbfNnc,DbfNnc,DbfNnc,DbfNnc,DbfNnc,DbfNnc,DbfNnc,DbfNnc,DbfNnc,DbfNnc,DbfNnc,
		DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,
		DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,
		DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,
		DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,DbfPnc,
		DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,
		DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,
		DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,
		DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,
		DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,
		DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,
		DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,
		DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt,DbfExt};

#define GET_CODE_TYPE(i) codeTypeTable[i]
#else
// Doing same as above but using more CPU time and using less memory.
static /*DbfCodeTypesEnum*/ uint8_t getCodeType(uint8_t i)
{
	if (i>=128)
	{
		return DbfExt; // EXTENSION_CODE_TYPE
	}
	else if (i>=64)
	{
		return DbfPnc; // Positive INTEGER_CODE_TYPE
	}
	else if (i>=32)
	{
		return DbfNnc; // NEGATIVE_CODE_TYPE
	}
	else if (i>=16)
	{
		return DbfFoC; // CRC_CODE_TYPE
	}
	else if (i>=8)
	{
		return DbfRcc; // SPECIAL_CODE_TYPE
	}
	return DbfNct; // NOTHING_CODE_TYPE
}
#define GET_CODE_TYPE(i) getCodeType(i)
#endif
/*
static uint8_t GET_CODE_TYPE(uint8_t i)
{
	uint8_t c = getCodeType(i);
	uint8_t t = codeTypeTable[i];
	assert(c==t);
	return t;
}
*/

/**
 * The return value will point on the beginning of next code if any.
 * Caller need to check if that index is larger then message size.
 */
unsigned int DbfUnserializerFindNextCode(const DbfUnserializer *u, unsigned int idx)
{
	assert(u);

	for(;;)
	{
		idx++;
		if (idx >= u->msgSize)
		{
			break;
		}
		const unsigned char ch = u->msgPtr[idx];
		if ((ch & DBF_EXT_CODEMASK) == DBF_EXT_CODEID)
		{
			// This extends current code
		}
		else
		{
			// This is part of next code.
			break;
		}
	}
	return idx;
}

// The code is decoded backwards, from its end to its beginning.
// codeEndIndex shall be the index of the first character in next code.
static int64_t DbfUnserializerDecodeData64(const DbfUnserializer *u, int codeEndIndex)
{
	int64_t i = 0;

	while (codeEndIndex>0)
	{
		codeEndIndex--;
		int64_t ch = u->msgPtr[codeEndIndex];

		const DbfCodeTypesEnum ct = GET_CODE_TYPE(ch);

		switch (ct)
		{
			case DbfNct:
				// nothing
				return i;
				break;
			case DbfExt:
				i = (i << DBF_EXT_DATANBITS) | (ch & DBF_EXT_DATAMASK);
				// This was an extension code, there must be more bytes.
				break;
			case DbfPnc:
				i = (i << DBF_PINT_DATANBITS) | (ch & DBF_PINT_DATAMASK);
				return i;
			case DbfNnc:
				// This is a negative number.
				i = (i << DBF_NINT_DATANBITS) | (ch & DBF_NINT_DATAMASK);
				return i;
			case DbfFoC:
				// This is a format code.
				i = (i << DBF_FMTCRC_DATANBITS) | (ch & DBF_FMTCRC_DATAMASK);
				return i;
			case DbfRcc:
				i = (i << DBF_REPEAT_DATANBITS) | (ch & DBF_REPEAT_DATAMASK);
				return i;
			case DbfEom:
				return i;
			default:
				#if defined __linux__ || defined __WIN32
				printf("Unknown code 0x%llx\n", (long long)ch);
				#endif
				return i;
		}
	}
	return i;
}

static unsigned int DbfUnserializerFindBeginOfCode(const DbfUnserializer *u, unsigned int idx)
{
	for(;;)
	{
		if (idx==0)
		{
			break;
		}
		idx--;
		unsigned char ch = u->msgPtr[idx];
		if ((ch & DBF_EXT_CODEMASK) == DBF_EXT_CODEID)
		{
			// This extends current code
		}
		else
		{
			// This is the beginning of code.
			break;
		}
	}
	return idx;
}

/**
 * Get the last code in the buffer.
 */
int DbfUnserializerDecodeDataRev64(const DbfUnserializer *u, unsigned int endIdx, int *nextCodeType, int64_t *nextCodeData)
{
	unsigned int beginIdx = DbfUnserializerFindBeginOfCode(u, endIdx);
	*nextCodeType = DbfUnserializerGetNextType(u, beginIdx);
	*nextCodeData = DbfUnserializerDecodeData64(u, endIdx);
	return beginIdx;
}


static int64_t take_next_code(DbfUnserializer *u)
{
	const unsigned int nextIndex = DbfUnserializerFindNextCode(u, u->readPos);
	const int64_t code = DbfUnserializerDecodeData64(u, nextIndex);
	u->readPos = nextIndex;
	return code;
}

/**
 * This will check if next code is a formating code. One that
 * tells the type of following data, if it is a number or string.
 */
static void DbfUnserializerTakeSpecial(DbfUnserializer *u)
{
	while ((u->repeat_counter == 0))
	{
		if (u->readPos >= u->msgSize)
		{
			u->decodeState = DbfEndOfMsgState;
			return;
		}

		const unsigned char ch = u->msgPtr[u->readPos];
		const DbfCodeTypesEnum t = GET_CODE_TYPE(ch);

		switch(t)
		{
			case DbfPnc:
			{
				return;
			}
			case DbfNnc:
			{
				return;
			}
			case DbfFoC:
			{
				// This is a format code. More codes should follow.
				const int64_t code = take_next_code(u);
				switch(code)
				{
					case DBF_INT_BEGIN_CODE:
						u->decodeState = DbfNextIsIntegerState;
						break;
					case DBF_STR_BEGIN_CODE:
						u->decodeState = DbfNextIsStringState;
						break;
					case DBF_WORD_BEGIN_CODE:
						u->decodeState = DbfNextIsWordState;
						break;
					default:
						u->decodeState = DbfEndOfMsgState;
						break;
				}

				u->current_code = 0;
				break;
			}
			case DbfEom:
			{
				u->decodeState = DbfEndOfMsgState;
				return;
			}
			case DbfRcc:
			{
				// This is a repeat on previous code.
				const int64_t code = take_next_code(u);
				u->repeat_counter = code;
				return;
			}
			default:
				// This was not a special etc code
				// positive and negative integer codes are to be left.
				printf("Unknown code 0x%x\n", t);
				return;
		}
	}
}


#ifdef DBF_AND_ASCII
static void DbfUnserializerTakeAsciiSpace(DbfUnserializer *u)
{
	// Skip all space.
	while ((!isgraph(u->msgPtr[u->readPos])) && (u->readPos < u->msgSize))
	{
		u->readPos++;
	}

	if (u->readPos == u->msgSize)
	{
		u->decodeState = DbfEndOfMsgState;
	}
	else
	{
		const int ch = u->msgPtr[u->readPos];

		if ((isdigit(ch)) || (ch=='-'))
		{
			u->decodeState = DbfAsciiNumberState;
		}
		else if (ch == '\"')
		{
			u->readPos++;
			u->decodeState = DbfAsciiStringState;
		}
		else if (is_char_part_of_word(ch))
		{
			u->decodeState = DbfAsciiWordState;
		}
		else if (ch == 0)
		{
			u->decodeState = DbfEndOfMsgState;
		}
		else
		{
			// Unknown
			printf("Unknown input %d\n", ch);
			u->decodeState = DbfEndOfMsgState;
		}
	}
}
#endif

static void DbfUnserializerInitGeneric(DbfUnserializer *u, const unsigned char *msgPtr, unsigned int msgSize)
{
	u->msgPtr = msgPtr;
	u->msgSize = msgSize;
	u->readPos = 0;
	u->current_code = 0;
	u->repeat_counter = 0;
}

void DbfUnserializerInitNoCRC(DbfUnserializer *u, const unsigned char *msgPtr, unsigned int msgSize)
{
	u->decodeState = DbfNextIsIntegerState;
	DbfUnserializerInitGeneric(u, msgPtr, msgSize);
	DbfUnserializerTakeSpecial(u);
}

// Take input from a serializer. Write in compressed binary .
DBF_CRC_RESULT DbfUnserializerInitFromSerializer(DbfUnserializer *u, const DbfSerializer *s)
{
	assert(u && s && s->repeat_counter == 0);
	u->decodeState = DbfNextIsIntegerState;
	DbfUnserializerInitGeneric(u, s->buffer, s->pos);
	DbfUnserializerTakeSpecial(u);
	return DBF_OK_CRC;
}

DBF_CRC_RESULT DbfUnserializerInitCopyUnserializer(DbfUnserializer *u, const DbfUnserializer *src)
{
	assert(u && src);
	u->msgPtr = src->msgPtr;
	u->msgSize = src->msgSize;
	u->readPos = src->readPos;
	u->current_code = src->current_code;
	u->repeat_counter = src->repeat_counter;
	u->decodeState = src->decodeState;
	return DBF_OK_CRC;
}


#ifdef DBF_AND_ASCII
DBF_CRC_RESULT DbfUnserializerInitAscii(DbfUnserializer *u, const unsigned char *msgPtr, unsigned int msgSize)
{
	assert(u);
	u->decodeState = DbfAsciiNumberState;
	DbfUnserializerInitGeneric(u, msgPtr, msgSize);
	DbfUnserializerTakeAsciiSpace(u);
	return DBF_OK_CRC;
}

// Take input from a serializer. Write in ascii.
DBF_CRC_RESULT DbfUnserializerInitAsciiSerializer(DbfUnserializer *u, const DbfSerializer *dbfSerializer)
{
	assert(dbfSerializer->repeat_counter == 0);
	u->msgPtr = dbfSerializer->buffer;
	u->msgSize = dbfSerializer->pos;
	u->decodeState = DbfNextIsIntegerState;
	u->readPos = 0;
	u->current_code = 0;
	u->repeat_counter = 0;
	DbfUnserializerTakeAsciiSpace(u);
	return DBF_OK_CRC;
}
#endif

/**
 * Returns DBF_OK_CRC if OK. A non zero error code if not OK (there was a CRC error).
 */
DBF_CRC_RESULT DbfUnserializerInitTakeCrc(DbfUnserializer *u, const unsigned char *msgPtr, unsigned int msgSize)
{
	assert(u);
	DbfUnserializerInitNoCRC(u, msgPtr, msgSize);

	DBF_CRC_RESULT r = DbfUnserializerReadCrc(u);
	switch(r)
	{
		case DBF_OK_CRC:
			return DBF_OK_CRC;
		default:
		case DBF_NO_CRC:
		case DBF_BAD_CRC:
			//debug_log("Bad or missing CRC");
			// Ignore this faulty message.
			u->decodeState = DbfEndOfMsgState;
			u->msgSize = 0;
			return r;
	}
}

DBF_CRC_RESULT DbfUnserializerInitEncoding(DbfUnserializer *u, const unsigned char *msgPtr, unsigned int msgSize, int encoding)
{
	assert(u);
	switch(encoding)
	{
		case 0:
			return DbfUnserializerInitAscii(u, msgPtr, msgSize);
		case 1:
			#if 1
			DbfUnserializerInitNoCRC(u, msgPtr, msgSize);
			return DBF_NO_CRC;
			#else
			return DbfUnserializerInitTakeCrc(u, msgPtr, msgSize);
			#endif
		default:
			break;
	}
	u->decodeState = DbfEndOfMsgState;
	u->msgSize = 0;
	return DBF_BAD_CRC;
}


DBF_CRC_RESULT DbfUnserializerInitReceiver(DbfUnserializer *u, const DbfReceiver *receiver)
{
	assert(u && receiver);
	return DbfUnserializerInitEncoding(u, receiver->buffer, receiver->msgSize, DbfReceiverGetEncoding(receiver));
}

/*static int is_ascii_message(const unsigned char *msgPtr, unsigned int msgSize)
{
	while(msgSize)
	{
		if (*msgPtr > '~') {return 0;}
		if ((*msgPtr < ' ') && ((*msgPtr != '\n') || (*msgPtr != '\r'))) {
			printf("  %d\n", *msgPtr);
			return 0;
		}
		++msgPtr;
		--msgSize;
	}
	return 1;
}

// This one is not recommended since it might get it wrong if a compressed message
// happens to be made with only ascii characters.
DBF_CRC_RESULT DbfUnserializerInit(DbfUnserializer *dbfUnserializer, const unsigned char *msgPtr, unsigned int msgSize)
{
	// TODO For short DBF messages this might fail since these might look like an ascii message.
	// Should change the codes used so that compressed DBF codes use
	// codes 128 ... 255. That will be DBF2 since the change will not be backward compatible.

	#ifdef DBF_AND_ASCII
	if (is_ascii_message(msgPtr, msgSize))
	{
		return DbfUnserializerInitAscii(dbfUnserializer, msgPtr, msgSize);
	}
	else
	#endif
	{
		DbfUnserializerInitNoCRC(dbfUnserializer, msgPtr, msgSize);
		return DBF_NO_CRC;
	}
}*/





DbfCodeTypesEnum DbfUnserializerGetNextType(const DbfUnserializer *u, unsigned int idx)
{
	if (idx >= u->msgSize)
	{
		return DbfEom; // ENDOFMSG_CODE_TYPE
	}
	else
	{
		const unsigned char ch = u->msgPtr[idx];

		return GET_CODE_TYPE(ch);
	}
	//return DbfECT; // ERROR_CODE_TYPE
}





int64_t DbfUnserializerReadInt64(DbfUnserializer *u)
{
	switch (u->decodeState)
	{
		#ifdef DBF_AND_ASCII
		case DbfAsciiNumberState:
		{
		//case DbfAsciiStringState:
			const char* str = (const char*)&u->msgPtr[u->readPos];
			const int64_t i = utility_atoll(str);
			while (isalnum(u->msgPtr[u->readPos]))
			{
				u->readPos++;
			}
			DbfUnserializerTakeAsciiSpace(u);
			return i;
		}
		#endif
		case DbfNextIsIntegerState:
		{
			if (u->repeat_counter > 0)
			{
				u->repeat_counter--;
				const int64_t r =  u->current_code;
				DbfUnserializerTakeSpecial(u);
				return r;
			}

			const unsigned char ch = u->msgPtr[u->readPos];
			const DbfCodeTypesEnum t = GET_CODE_TYPE(ch);

			switch(t)
			{
				case DbfPnc:
				{
					// An integer is about to follow.
					const int64_t code = take_next_code(u);
					u->repeat_counter = 0;
					u->current_code = code;
					DbfUnserializerTakeSpecial(u);
					return code;
				}
				case DbfNnc:
				{
					// An integer is about to follow.
					const int64_t code = -take_next_code(u)-1;
					u->repeat_counter = 0;
					u->current_code = code;
					DbfUnserializerTakeSpecial(u);
					return code;
				}
				case DbfFoC:
				case DbfEom:
				case DbfRcc:
				default:
					// This was not a special etc code
					// positive and negative integer codes are to be left.
					printf("unexpected code 0x%x\n", t);
					u->repeat_counter = 0;
					u->current_code = 0;
					u->decodeState = DbfEndOfMsgState;
					break;
			}

			DbfUnserializerTakeSpecial(u);
			break;
		}
		default:
			printf("Not integer\n");
			return -1;
	}

	return 0;
}

int32_t DbfUnserializerReadInt32(DbfUnserializer *u)
{
	// TODO Optimize for 32 bit CPU.
	return DbfUnserializerReadInt64(u);
}


// Returns the length of received string.
// A negative value if it failed.
int DbfUnserializerRead(DbfUnserializer *u, char* bufPtr, size_t bufCap)
{
	assert(u!=NULL);
	assert((bufPtr!=NULL) || (bufCap==0));

	switch (u->decodeState)
	{
		#ifdef DBF_AND_ASCII
		case DbfAsciiNumberState:
		{
			int n = 0;
			while (is_char_part_of_number(u->msgPtr[u->readPos]))
			{
				if (n < bufCap)
				{
					bufPtr[n] = u->msgPtr[u->readPos];
				}
				u->readPos++;
				n++;
			}
			if (n < bufCap) {bufPtr[n] = 0;} else if (bufCap>0)	{bufPtr[bufCap-1] = 0;}
			DbfUnserializerTakeAsciiSpace(u);
			return n;
		}
		case DbfAsciiWordState:
		{
			int n = 0;
			while (is_char_part_of_word(u->msgPtr[u->readPos]))
			{
				if (n < bufCap)
				{
					bufPtr[n] = u->msgPtr[u->readPos];
				}
				u->readPos++;
				n++;
			}
			if (n < bufCap) {bufPtr[n] = 0;} else if (bufCap>0)	{bufPtr[bufCap-1] = 0;}
			DbfUnserializerTakeAsciiSpace(u);
			return n;
		}
		case DbfAsciiStringState:
		{
			int n = 0;
			// Strings may contain quotes and if so these must be replaced with an escape sequence.
			// We should read until end of string (endquote)  or end of message
			for(;;)
			{
				int ch = u->msgPtr[u->readPos];
				++u->readPos;

				if ((ch == 0) || (ch == '\"') || (u->readPos >= u->msgSize))
				{
					DbfUnserializerTakeAsciiSpace(u);
					return n;
				}

				if (ch == '\\')
				{
					int x = u->msgPtr[u->readPos++];
					if (x == 'x')
					{
						int h1 = u->msgPtr[u->readPos++];
						int h2 = u->msgPtr[u->readPos++];
						int h = (utility_decode_digit(h1) << 4) + utility_decode_digit(h2);
						if (n < bufCap)
						{
							bufPtr[n] = h;
						}
						n++;
					}
					assert(u->readPos <= u->msgSize);
				}
				else
				{
					if (n < bufCap)
					{
						bufPtr[n] = ch; //u->msgPtr[u->readPos];
					}
					n++;
				}
			}
			if (n < bufCap) {bufPtr[n] = 0;} else if (bufCap>0)	{bufPtr[bufCap-1] = 0;}
			DbfUnserializerTakeAsciiSpace(u);
			return n;
		}
		#endif
		case DbfNextIsWordState:
		case DbfNextIsStringState:
		{
			int n = 0;
			if (u->decodeState == DbfNextIsStringState) {if (n < bufCap) {bufPtr[n] = '\"';n++;}}
			while ((u->decodeState == DbfNextIsWordState) || (u->decodeState == DbfNextIsStringState))
			{
				// Read as long as it is a code that represents characters (that is positive or negative numbers)
				int t = DbfUnserializerGetNextType(u, u->readPos);

				switch(t)
				{
					case DbfNct:
					case DbfExt: // EXTENSION_CODE_TYPE, see also DBF_EXT_CODEID
						printf("Unexpected code 0x%x\n", t);
						if (u->decodeState == DbfNextIsStringState) {if (n < bufCap) {bufPtr[n] = '\"';n++;}}
						if (n < bufCap) {bufPtr[n] = 0;} else if (bufCap>0)	{bufPtr[bufCap-1] = 0;}
						DbfUnserializerTakeSpecial(u);
						return n;
					case DbfPnc:
					{
						u->current_code = ASCII_OFFSET + take_next_code(u);
						if (n < bufCap)
						{
							bufPtr[n] = u->current_code;
						}
						++n;
						break;
					}
					case DbfNnc: // NEGATIVE_NUMBER_CODE_TYPE, see also DBF_NINT_CODEID
					{
						u->current_code = ASCII_OFFSET - 1 - take_next_code(u);
						if (n < bufCap)
						{
							bufPtr[n] = u->current_code;
						}
						n++;
						break;
					}
					case DbfFoC:
						// This means that the string ended.
						if (u->decodeState == DbfNextIsStringState) {if (n < bufCap) {bufPtr[n] = '\"';n++;}}
						if (n < bufCap) {bufPtr[n] = 0;} else if (bufCap>0)	{bufPtr[bufCap-1] = 0;}

						// Check if we have a repeat in progress. We should not.
						assert(u->repeat_counter==0);

						// Check what comes after.
						DbfUnserializerTakeSpecial(u);
						return n;
					case DbfRcc:
					{
						// This is a repeat on previous code.
						int64_t code = take_next_code(u);
						while (code > 0)
						{
							if (n < bufCap)
							{
								bufPtr[n] = u->current_code;
							}
							code--;
							n++;
						}
						u->repeat_counter = 0;
						break;
					}
					case DbfEom:
						if (u->decodeState == DbfNextIsStringState) {if (n < bufCap) {bufPtr[n] = '\"';n++;}}
						if (n < bufCap) {bufPtr[n] = 0;} else if (bufCap>0)	{bufPtr[bufCap-1] = 0;}
						assert(u->repeat_counter==0);
						u->decodeState = DbfEndOfMsgState;
						return n;
					default:
						printf("Unknown code 0x%x\n", t);
						if (u->decodeState == DbfNextIsStringState) {if (n < bufCap) {bufPtr[n] = '\"';n++;}}
						if (n < bufCap) {bufPtr[n] = 0;} else if (bufCap>0)	{bufPtr[bufCap-1] = 0;}
						DbfUnserializerTakeSpecial(u);
						return n;
				}
			}

			// Read and skip next code if it is a special code for message formating etc.
			DbfUnserializerTakeSpecial(u);
			break;
		}
		default:
			break;
	}

	printf("Not string\n");
	if (bufCap>0) {bufPtr[0] = 0;}
	return -1;
}

// Same as DbfUnserializerReadString but only reads the string.
// Returns the length of received string.
// A negative value if it failed.
// TODO Perhaps let DbfUnserializerReadInt64 return string length if it is a string.
// Then this method will not be needed.
long DbfUnserializerStringLength(const DbfUnserializer *u)
{
	assert(u!=NULL);

	// Use a copy of DbfUnserializer;
	DbfUnserializer uc;
	uc = *u;

	switch (uc.decodeState)
	{
		#ifdef DBF_AND_ASCII
		case DbfAsciiNumberState:
		{
			int n = 0;
			while ((is_char_part_of_number(uc.msgPtr[uc.readPos])))
			{
				uc.readPos++;
				n++;
			}
			return n;
		}
		case DbfAsciiWordState:
		{
			int n = 0;
			while ((is_char_part_of_word(uc.msgPtr[uc.readPos])))
			{
				uc.readPos++;
				n++;
			}
			return n;
		}
		case DbfAsciiStringState:
		{
			int n = 0;
			// Strings may contain quotes and if so these must be replaced with an escape sequence.
			// We should read until end of string (endquote)  or end of message
			for(;;)
			{
				int ch = uc.msgPtr[uc.readPos++];

				if ((ch == 0) || (ch == '\"') || (uc.readPos >= uc.msgSize))
				{
					DbfUnserializerTakeAsciiSpace(&uc);
					return n;
				}

				if (ch == '\\')
				{
					uc.readPos += 4;
					assert(uc.readPos <= uc.msgSize);
				}
				else
				{
					uc.readPos++;
				}
				n++;
			}
			return n;
		}
		#endif
		case DbfNextIsWordState:
		case DbfNextIsStringState:
		{
			int n = 0;
			while ((uc.decodeState == DbfNextIsStringState) || (uc.decodeState == DbfNextIsWordState))
			{
				// Read as long as it is a code that represents characters (that is positive or negative numbers)
				int t = DbfUnserializerGetNextType(&uc, uc.readPos);

				switch(t)
				{
					case DbfNct:
					case DbfExt: // EXTENSION_CODE_TYPE, see also DBF_EXT_CODEID
						printf("Unexpected code 0x%x\n", t);
						return n;
					case DbfPnc:
					{
						take_next_code(&uc);
						++n;
						break;
					}
					case DbfNnc: // NEGATIVE_NUMBER_CODE_TYPE, see also DBF_NINT_CODEID
					{
						take_next_code(&uc);
						n++;
						break;
					}
					case DbfFoC:
						// This means that the string ended.
						return n;
					case DbfRcc:
					{
						// This is a repeat on previous code.
						n += take_next_code(&uc);
						break;
					}
					case DbfEom:
						return n;
					default:
						printf("Unknown code 0x%x\n", t);
						return n;
				}
			}
			break;
		}
		case DbfNextIsIntegerState:
			return 32;
		default:
			printf("Illegal state %d\n", uc.decodeState);
			break;
	}
	printf("Not string\n");
	return -1;
}

// Returns the length of received string.
// A negative value if it failed.
int DbfUnserializerToSerializer(DbfUnserializer *u, DbfSerializer* s)
{
	assert((u!=NULL) && (s!=NULL));

	switch (u->decodeState)
	{
		#ifdef DBF_AND_ASCII
		case DbfAsciiNumberState:
		{
			DbfSerializerBeginWriteNumber(s);

			int n = 0;
			while ((u->readPos < u->msgSize) && (is_char_part_of_number(u->msgPtr[u->readPos])))
			{
				DbfSerializerResizeIfNeeded(s, s->pos);
				DbfSerializerPutByte(s, u->msgPtr[u->readPos]);
				u->readPos++;
				n++;
			}

			DbfSerializerEndWrite(s);

			DbfUnserializerTakeAsciiSpace(u);
			return n;
		}
		case DbfAsciiWordState:
		{
			DbfSerializerBeginWriteWord(s);

			int n = 0;
			while ((u->readPos < u->msgSize) && ((is_char_part_of_word(u->msgPtr[u->readPos]))))
			{
				DbfSerializerResizeIfNeeded(s, s->pos);
				DbfSerializerPutByte(s, u->msgPtr[u->readPos]);
				u->readPos++;
				n++;
			}

			DbfSerializerEndWrite(s);

			DbfUnserializerTakeAsciiSpace(u);
			return n;
		}
		case DbfAsciiStringState:
		{
			DbfSerializerBeginWriteString(s);

			int n = 0;
			// Strings may contain quotes and if so these must be replaced with an escape sequence.
			// We should read until end of string (endquote) or end of message
			while (u->readPos < u->msgSize)
			{
				int ch = u->msgPtr[u->readPos++];

				if ((ch == 0) || (ch == '\"'))
				{
					break;
				}

				if (ch == '\\')
				{
					if ((u->readPos+3) <= u->msgSize)
					{
						int x = u->msgPtr[u->readPos++];
						if (x == 'x')
						{
							int h1 = u->msgPtr[u->readPos++];
							int h2 = u->msgPtr[u->readPos++];
							int h = (utility_decode_digit(h1) << 4) + utility_decode_digit(h2);
							DbfSerializerResizeIfNeeded(s, s->pos);
							DbfSerializerPutByte(s, h);
						}
						else
						{
							printf("not supported sequence");
							u->decodeState = DbfEndOfMsgState;
							return n;
						}
						assert(u->readPos <= u->msgSize);
					}
					else
					{
						printf("incorrect sequence");
						u->decodeState = DbfEndOfMsgState;
						return n;
					}
				}
				else
				{
					DbfSerializerResizeIfNeeded(s, s->pos);
					DbfSerializerPutByte(s, ch);
				}
				n++;
			}

			// Add end quote. And a temporary terminating zero?
			DbfSerializerEndWrite(s);
			DbfUnserializerTakeAsciiSpace(u);
			return n;
		}
		#endif
		case DbfNextIsWordState:
		{
			DbfSerializerBeginWriteWord(s);

			int n = 0;
			while (u->decodeState == DbfNextIsWordState)
			{
				// Read as long as it is a code that represents characters (that is positive or negative numbers)
				int t = DbfUnserializerGetNextType(u, u->readPos);

				switch(t)
				{
					case DbfNct:
					case DbfExt: // EXTENSION_CODE_TYPE, see also DBF_EXT_CODEID
						printf("Unexpected code 0x%x\n", t);
						DbfSerializerEndWrite(s);
						DbfUnserializerTakeSpecial(u);
						return n;
					case DbfPnc:
					{
						u->current_code = ASCII_OFFSET + take_next_code(u);
						DbfSerializerResizeIfNeeded(s, s->pos);
						DbfSerializerPutByte(s, u->current_code);
						++n;
						break;
					}
					case DbfNnc: // NEGATIVE_NUMBER_CODE_TYPE, see also DBF_NINT_CODEID
					{
						u->current_code = ASCII_OFFSET - 1 - take_next_code(u);
						DbfSerializerResizeIfNeeded(s, s->pos);
						DbfSerializerPutByte(s, u->current_code);
						n++;
						break;
					}
					case DbfFoC:
						// This means that the string ended.
						DbfSerializerEndWrite(s);

						// Check if we have a repeat in progress. We should not.
						assert(u->repeat_counter==0);

						// Check what comes after.
						DbfUnserializerTakeSpecial(u);
						return n;
					case DbfRcc:
					{
						// This is a repeat on previous code.
						int64_t code = take_next_code(u);
						while (code > 0)
						{
							DbfSerializerResizeIfNeeded(s, s->pos);
							DbfSerializerPutByte(s, u->current_code);
							code--;
							n++;
						}
						u->repeat_counter = 0;
						break;
					}
					case DbfEom:
						DbfSerializerEndWrite(s);
						DbfUnserializerTakeSpecial(u);
						return n;
					default:
						printf("Unknown code 0x%x\n", t);
						DbfSerializerEndWrite(s);
						DbfUnserializerTakeSpecial(u);
						return n;
				}
			}

			// Read and skip next code if it is a special code for message formating etc.
			DbfUnserializerTakeSpecial(u);
			break;
		}
		case DbfNextIsStringState:
		{
			DbfSerializerBeginWriteString(s);

			int n = 0;
			while (u->decodeState == DbfNextIsStringState)
			{
				// Read as long as it is a code that represents characters (that is positive or negative numbers)
				int t = DbfUnserializerGetNextType(u, u->readPos);

				switch(t)
				{
					case DbfNct:
					case DbfExt: // EXTENSION_CODE_TYPE, see also DBF_EXT_CODEID
						printf("Unexpected code 0x%x\n", t);
						DbfSerializerEndWrite(s);
						DbfUnserializerTakeSpecial(u);
						return n;
					case DbfPnc:
					{
						u->current_code = ASCII_OFFSET + take_next_code(u);
						DbfSerializerResizeIfNeeded(s, s->pos);
						DbfSerializerPutByte(s, u->current_code);
						++n;
						break;
					}
					case DbfNnc: // NEGATIVE_NUMBER_CODE_TYPE, see also DBF_NINT_CODEID
					{
						u->current_code = ASCII_OFFSET - 1 - take_next_code(u);
						DbfSerializerResizeIfNeeded(s, s->pos);
						DbfSerializerPutByte(s, u->current_code);
						n++;
						break;
					}
					case DbfFoC:
						// This means that the string ended.
						DbfSerializerEndWrite(s);

						// Check if we have a repeat in progress. We should not.
						assert(u->repeat_counter==0);

						// Check what comes after.
						DbfUnserializerTakeSpecial(u);
						return n;
					case DbfRcc:
					{
						// This is a repeat on previous code.
						int64_t code = take_next_code(u);
						while (code > 0)
						{
							DbfSerializerResizeIfNeeded(s, s->pos);
							DbfSerializerPutByte(s, u->current_code);
							code--;
							n++;
						}
						u->repeat_counter = 0;
						break;
					}
					case DbfEom:
						DbfSerializerEndWrite(s);
						DbfUnserializerTakeSpecial(u);
						return n;
					default:
						printf("Unknown code 0x%x\n", t);
						DbfSerializerEndWrite(s);
						DbfUnserializerTakeSpecial(u);
						return n;
				}
			}

			// Read and skip next code if it is a special code for message formating etc.
			DbfUnserializerTakeSpecial(u);
			break;
		}
		case DbfNextIsIntegerState:
		{
			int64_t n = DbfUnserializerReadInt64(u);
			DbfSerializerWriteInt64(s, n);
			break;
		}
		default:
			printf("Illegal decodeState %d",u->decodeState);
			u->decodeState = DbfEndOfMsgState;
			break;
	}

	return -1;
}

int DbfUnserializerToSerializerAll(DbfUnserializer *u, DbfSerializer* s)
{
	assert((u!=NULL) && (s!=NULL));
	int n = 0;
	while(!DbfUnserializerReadIsNextEnd(u))
	{
		DbfUnserializerToSerializer(u, s);
		++n;
	}
	return n;
}

int DbfUnserializerReadIsNextString(const DbfUnserializer *u)
{
	assert(u!=NULL);
	switch (u->decodeState)
	{
		#ifdef DBF_AND_ASCII
		case DbfAsciiStringState:
		case DbfAsciiWordState:
			return 1;
		#endif
		case DbfNextIsStringState:
			return 1;
		default:
			break;
	}
	return 0;
}

int DbfUnserializerReadIsNextInt(const DbfUnserializer *u)
{
	assert(u!=NULL);
	switch (u->decodeState)
	{
		#ifdef DBF_AND_ASCII
		case DbfAsciiNumberState:
			return 1;
		#endif
		case DbfNextIsIntegerState:
			return 1;
		default:
			break;
	}
	return 0;
}

int DbfUnserializerReadIsNextEnd(const DbfUnserializer *u)
{
	if (u->decodeState == DbfEndOfMsgState)
	{
		return 1;
	}
	assert(!((u->repeat_counter == 0) && (u->readPos >= u->msgSize)));
	return 0;
}

/**
 * This will take the last code in the buffer, decode and check if CRC is OK.
 */
DBF_CRC_RESULT DbfUnserializerReadCrc(DbfUnserializer *u)
{
	assert(u!=NULL);
	if (u->msgSize==0)
	{
		// No message
		return DBF_NO_CRC;
	}

	int nextCodeType;
	int64_t nextCodeData;
	int lastCodePos = DbfUnserializerDecodeDataRev64(u, u->msgSize, &nextCodeType, &nextCodeData);
	// TODO Shall we allow using numbers code to send the CRC?
	//if ((nextCodeType != DbfFoC) && (nextCodeType != DbfINT) && (nextCodeType != DbfNnr))
	if (nextCodeType != DbfFoC)
	{
		//debug_log("Last code was not a CRC");
		return DBF_NO_CRC;
	}
	else
	{
		// the CRC has been read, shorten the message.
		u->msgSize = lastCodePos;
		uint32_t receivedCrc=nextCodeData;
		uint32_t calculatedCrc = crc32_calculate(u->msgPtr, u->msgSize);
		if (receivedCrc != calculatedCrc)
		{
			/*#if defined __linux__ || defined __WIN32
			printf(LOG_PREFIX "Wrong CRC: got %04x, expected %04x\n", receivedCrc, calculatedCrc);
			#else
			debug_log("bad CRC");
			#endif*/
			return DBF_BAD_CRC;
		}
	}

	return DBF_OK_CRC;
}


#if defined __linux__ || defined __WIN32
void DbfUnserializerReadCrcAndLog(DbfUnserializer *u)
{
	DBF_CRC_RESULT r = DbfUnserializerReadCrc(u);
	switch(r)
	{
		case DBF_BAD_CRC: printf("Bad CRC\n");break;
		case DBF_OK_CRC: printf("OK CRC\n");break;
		default: printf("No CRC\n");break;
	}
}
#endif


static int8_t DbfReceiverIsFull(DbfReceiver * r)
{
	return (r->msgSize>=sizeof(r->buffer));
}

// This is an internal helper function. Not intended for users to call.
// Returns 0 if OK
static int8_t DbfReceiverStoreByte(DbfReceiver *r, char b)
{
	if (r->msgSize<sizeof(r->buffer))
	{
		r->buffer[r->msgSize] = b;
		r->msgSize++;
		return 0;
	}
	return -1;
}

// TODO Perhaps use an internal variable incremented at tick instead?
// To make receiver more deterministic.
static long get_sys_time_ms()
{
	return (st_get_posix_time_us() / 1000);
}

static void enterInitialState(DbfReceiver *r)
{
	r->msgSize = 0;
	r->msgtimestamp = 0;
	r->receiverState = DbfRcvInitialState;
}

static void enterReceivingTxtState(DbfReceiver *r, unsigned char ch)
{
	DbfReceiverStoreByte(r, ch);
	r->msgtimestamp = get_sys_time_ms();
	r->receiverState = DbfRcvReceivingTxtState;
	//debug_log("txt begin");
}

static void enterReceivingBinaryMessageState(DbfReceiver *r, unsigned char ch)
{
	r->msgSize = 0;
	r->msgtimestamp = get_sys_time_ms();
	r->receiverState = DbfRcvReceivingMessageState;
	//debug_log("DBF begin");
}

static void enterReceivingNoiseState(DbfReceiver *r, unsigned char ch)
{
	r->msgSize = 0;
	r->msgtimestamp = get_sys_time_ms();
	r->receiverState = DbfRcvIgnoreInputState;
}


void DbfReceiverInit(DbfReceiver *r)
{
	enterInitialState(r);
}

void DbfReceiverDeinit(DbfReceiver *r)
{
	enterInitialState(r);
}

void DbfReceiverReset(DbfReceiver *r)
{
	enterInitialState(r);
}

// Returns:
//  0 ASCII message
//  1 if compressed DBF
// -1 if unknown/faulty
int DbfReceiverGetEncoding(const DbfReceiver *r)
{
	assert(r);
	switch(r->receiverState)
	{
		case DbfRcvMessageReadyState:
		case DbfRcvDbfReceivedState:
		case DbfRcvDbfReceivedMoreExpectedState:
			return 1;

		case DbfRcvTxtReceivedState:
			return 0;

		case DbfRcvIgnoreInputState:
		case DbfRcvReceivingMessageState:
		case DbfRcvReceivingTxtState:
		case DbfRcvInitialState:
		case DbfRcvErrorState:
		default: break;
	}
	return -1;
}

static void processFirstChar(DbfReceiver *r, unsigned char ch)
{
	assert(r!=NULL);
	switch(ch)
	{
		case DBF_BEGIN_CODEID:
			// A DBF message begin.
			enterReceivingBinaryMessageState(r, ch);
			break;
		case DBF_END_CODEID:
			// this was the end of a BDF message.
			r->msgSize = 0;
			r->msgtimestamp = 0;
			break;
		case '\r':
		case '\n':
			// This was the end of an ascii text line.
			r->msgSize = 0;
			r->msgtimestamp = 0;
			break;
		case '\t':
			enterReceivingTxtState(r, ch);
			break;
		default:
			r->msgSize = 0;
			if ((ch>=' ') && (ch<='~'))
			{
				// This looks like the begin of an ascii string.
				enterReceivingTxtState(r, ch);
			}
			else
			{
				// Not DBF and not regular 7 bit ascii, ignore.
				// TODO we should count these, perhaps report some useful error message?
				//debug_log("unexpected char");
				enterReceivingNoiseState(r, ch);
			}
			break;
	}
}

// In this state just wait for line to be silent for a while.
static void processNoise(DbfReceiver *r, unsigned char ch)
{
	assert(r!=NULL);
	switch(ch)
	{
		case DBF_BEGIN_CODEID:
			// A DBF message begin.
			enterReceivingBinaryMessageState(r, ch);
			break;
		default:
		{
			// In this state just wait for line to be silent for a while.
			// see also DbfReceiverCheckTimeout.
			const int32_t d = get_sys_time_ms() - r->msgtimestamp;
			if (d > IGNORE_UNTIL_SILENCE_MS)
			{
				// It has been silent for a while now.
				processFirstChar(r, ch);
			}
			else if (((ch>=' ') && (ch<='~')) || (ch == '\n') || (ch == '\r') || (ch == '\t'))
			{
				// Keep ignoring input.
			}
			else
			{
				// more noise, extend time.
				r->msgtimestamp = get_sys_time_ms();
			}
			break;
		}
	}
}

/**
 * Returns
 *  <0 : Something wrong
 *   0 : Nothing to report, not yet a full message.
 *  >0 : A full message has been received (number of bytes received).
 */
int DbfReceiverProcessCh(DbfReceiver *r, unsigned char ch)
{
	assert(r!=NULL);
	switch (r->receiverState)
	{
		case DbfRcvInitialState:
		{
			// In this state: waiting for the first character of a message, DBF or ascii.
			processFirstChar(r, ch);
			break;
		}
		case DbfRcvReceivingTxtState:
		{
			switch(ch)
			{
				//case '\\':
				// TODO We could add decoding of escape sequences such as \n
				// and \040 to allow special characters to be embedded in the
				// ASCII line.

				case DBF_END_CODEID:
					// Unexpected data
					enterInitialState(r);
					break;
				case DBF_BEGIN_CODEID:
					// A DBF message begin while receiving ascii line?
					// Ascii lines are expected to end with LF (or CR).
					debug_log("DBF inside txt");
					enterReceivingBinaryMessageState(r, ch);
					break;
				case '\r':
				case '\n':
					// Adding a terminating zero (instead of the LF (or CR))
					if (r->msgSize<sizeof(r->buffer))
					{
						r->buffer[r->msgSize] = 0;
						r->receiverState = DbfRcvTxtReceivedState;
					}
					else
					{
						debug_log("txt buffer full");
					}
					return r->msgSize;
				default:
					if ((ch < ' ') || (ch > '~'))
					{
						// Unexpected data or noise
						enterReceivingNoiseState(r, ch);
					}
					else
					{
						DbfReceiverStoreByte(r, ch);
						r->msgtimestamp = get_sys_time_ms();
						if (DbfReceiverIsFull(r))
						{
							r->receiverState = DbfRcvTxtReceivedState;
							return r->msgSize;
						}
					}
					break;
			}
			break;
		}
		case DbfRcvReceivingMessageState:
		{
			switch(ch)
			{
				case DBF_BEGIN_CODEID:
				{
					if (r->msgSize == 0)
					{
						// Stay in this state
						//r->receiverState = DbfRcvReceivingMessageState;
					}
					else
					{
						r->receiverState = DbfRcvDbfReceivedMoreExpectedState;
						//debug_log("dbf end and begin");
						return r->msgSize;
					}
					break;
				}
				case DBF_END_CODEID:
				{
					if (r->msgSize == 0)
					{
						enterInitialState(r);
					}
					else
					{
						r->receiverState = DbfRcvDbfReceivedState;
						//debug_log("dbf end");
						return r->msgSize;
					}
					break;
				}
				default:
				{
					if (DbfReceiverStoreByte(r, ch) != 0)
					{
						// Discard the message, it was too long.
						debug_log("dbf buffer full");
						enterInitialState(r);
					}
					break;
				}
			}
			break;
		}
		case DbfRcvIgnoreInputState:
		{
			processNoise(r, ch);
			break;
		}
		default:
		case DbfRcvTxtReceivedState:
		case DbfRcvMessageReadyState:
		case DbfRcvErrorState:
			debug_log("msg cleared");
			r->msgSize = 0;
			return -1;
	}
	return 0;
}

int DbfReceiverIsDbf(const DbfReceiver *r)
{
	assert(r!=NULL);
	return ((r->receiverState == DbfRcvDbfReceivedState) || (r->receiverState == DbfRcvDbfReceivedMoreExpectedState));
}

int DbfReceiverIsTxt(const DbfReceiver *r)
{
	assert(r!=NULL);
	return (r->receiverState == DbfRcvTxtReceivedState);
}

// This will check for timeout in receiving of messages.
void DbfReceiverCheckTimeout(DbfReceiver *r, int timout_ms)
{
	assert(r!=NULL);
	switch (r->receiverState)
	{
		case DbfRcvReceivingMessageState:
		case DbfRcvIgnoreInputState:
		{
			const int32_t time_since_msg_begin = get_sys_time_ms() - r->msgtimestamp;
			if (time_since_msg_begin > timout_ms)
			{
				if (r->msgSize != 0)
				{
					debug_log("timeout");
					r->msgSize = 0;
				}
				enterInitialState(r);
			}
			break;
		}
		default:
			break;
	}
}

void DbfReceiverTick(DbfReceiver *r)
{
	DbfReceiverCheckTimeout(r, DBF_RCV_TIMEOUT_MS);
}

#if defined __linux__ || defined __WIN32

/*int DbfReceiverToString(DbfReceiver *dbfReceiver, const char* bufPtr, int bufLen)
{
	if (DbfReceiverIsTxt(dbfReceiver))
	{
		int n = ((bufLen-1) > dbfReceiver->msgSize) ? dbfReceiver->msgSize : (bufLen-1);
		memcpy(bufPtr, dbfReceiver->buffer, n);
		bufPtr[n] = 0;
	}
	else if (DbfReceiverIsDbf(dbfReceiver))
	{
		// Not implemented yet
		bufPtr[0] = 0;
	}
	return 0;
}*/


// Reads the remaining message and writs its contents into buffer in ascii.
size_t DbfUnserializerReadAllToString(DbfUnserializer *u, char *bufPtr, size_t bufSize)
{
	assert(u);
	if ((bufPtr == NULL) || (bufSize<=0) || (bufSize >= 0x70000000))
	{
		return 0;
	}
	#if 0
	int n = 0;
	const char* separator="";
	*bufPtr=0;
	while(!DbfUnserializerReadIsNextEnd(u) && bufSize>0)
	{
		if (DbfUnserializerReadIsNextString(u))
		{
			#if DBF_OPTIMIZE_FOR_32_BITS
			char tmp[80];
			#else
			char tmp[4096];
			#endif
			DbfUnserializerRead(u, tmp, sizeof(tmp));
			snprintf(bufPtr, bufSize, "%s\"%s\"", separator, tmp);
		}
		else if (DbfUnserializerReadIsNextInt(u))
		{
			int64_t i = DbfUnserializerReadInt64(u);
			snprintf(bufPtr, bufSize, "%s%lld", separator, (long long int)i);
		}
		else
		{
			snprintf(bufPtr, bufSize, " ?");
		}
		const int len = strlen(bufPtr);
		bufPtr += len;
		bufSize -=len;
		separator=" ";
		++n;
	}
	#else
	DbfSerializer s2;
	DbfSerializerInitAscii(&s2);
	DbfUnserializerToSerializerAll(u, &s2);
	DbfSerializerFinalize(&s2);
	//printf("s2 '%s'\n", s2.buffer);
	size_t n = snprintf(bufPtr, bufSize, "%s", s2.buffer);
	DbfSerializerDeinit(&s2);
	#endif
	return n;
}

size_t DbfUnserializerCopyAllToString(const DbfUnserializer *u, char *bufPtr, size_t bufSize)
{
	assert(u);
	DbfUnserializer u2;
	DbfUnserializerInitCopyUnserializer(&u2, u);
	size_t n = DbfUnserializerReadAllToString(&u2, bufPtr, bufSize);
	if ((n<0) || (n >= bufSize))
	{
		snprintf(bufPtr, bufSize, "log_message failed %zu", n);
	}
	return n;
}

void DbfSerializerAllToString(const DbfSerializer *s, char *bufPtr, size_t bufSize)
{
	assert(s);
	DbfUnserializer u;
	DbfUnserializerInitFromSerializer(&u, s);
	size_t n = DbfUnserializerReadAllToString(&u, bufPtr, bufSize);
	if ((n<0) || (n >= bufSize))
	{
		snprintf(bufPtr, bufSize, "log_message failed %zu", n);
	}
}


int DbfReceiverLogRawData(const DbfReceiver *dbfReceiver)
{
	assert(dbfReceiver);
	if (DbfReceiverIsTxt(dbfReceiver))
	{
		printf(LOG_PREFIX "DbfReceiverLogRawData: ");
		const unsigned char *ptr = dbfReceiver->buffer;
		int n = dbfReceiver->msgSize;
		for(int i=0; i<n; ++i)
		{
			int ch = ptr[i];
			if (isgraph(ch))
			{
				printf("%c", ch);
			}
			else if (isprint(ch))
			{
				printf("%c", ch);
			}
			else if (ch == 0)
			{
				// do nothing
			}
			else
			{
				printf("<%02x>", ch);
			}
		}
	}
	else if (DbfReceiverIsDbf(dbfReceiver))
	{
		DbfLogBuffer("", dbfReceiver->buffer, dbfReceiver->msgSize);
	}
	return 0;
}


void DbfLogBuffer(const char *prefix, const unsigned char *bufPtr, int bufLen)
{
	char str[4096];
	DbfUnserializer dbfUnserializer;
	DbfUnserializerInitTakeCrc(&dbfUnserializer, bufPtr, bufLen);
	DbfUnserializerReadAllToString(&dbfUnserializer, str, sizeof(str));
	printf("%s\n",prefix);
	printf("    dbf: %s\n",str);

	// Logging data in hex also.
	printf("    hex:");
	for(int i=0;i<bufLen;i++)
	{
		printf(" %02x", (unsigned char)bufPtr[i]);
	}
	printf("\n");
}

void DbfLogBufferNoCrc(const char *prefix, const unsigned char *bufPtr, int bufLen)
{
	char str[4096];
	DbfUnserializer dbfUnserializer;
	DbfUnserializerInitNoCRC(&dbfUnserializer, bufPtr, bufLen);
	DbfUnserializerReadAllToString(&dbfUnserializer, str, sizeof(str));
	printf("%s\n",prefix);
	printf("    dbf: %s\n",str);

	// Logging data in hex also.
	printf("    hex:");
	for(int i=0;i<bufLen;i++)
	{
		printf(" %02x", (unsigned char)bufPtr[i]);
	}
	printf("\n");
}
#endif


DbfSerializer dbfTmpMessage = 
{
#if (!defined DBF_FIXED_MSG_SIZE)
		0, 0,
#else
		{0},
#endif
		0,
		0
};

#if (defined __linux__) || (defined __WIN32)

void dbfSendMessage(DbfSerializer *bytePacket)
{
	// TODO
}

void dbfSendShortMessage(int32_t code)
{
	// TODO
}


#elif (defined __arm__)

void dbfSendMessage(DbfSerializer *bytePacket)
{
	DbfSerializerWriteCrc(bytePacket);
	const char *msgPtr=DbfSerializerGetMsgPtr(bytePacket);
	const int msgLen=DbfSerializerGetMsgLen(bytePacket);
	usartPutChar(DEV_USART1, DBF_BEGIN_CODEID);
	usartWrite(DEV_USART1, msgPtr, msgLen);
	usartPutChar(DEV_USART1, DBF_END_CODEID);
	#ifdef COMMAND_ON_LPUART1
	usartPutChar(DEV_LPUART1, DBF_BEGIN_CODEID);
	usartWrite(DEV_LPUART1, msgPtr, msgLen);
	usartPutChar(DEV_LPUART1, DBF_END_CODEID);
	#endif
	#ifdef COMMAND_ON_USART2
	usartPutChar(DEV_USART2, DBF_BEGIN_CODEID);
	usartWrite(DEV_USART2, msgPtr, msgLen);
	usartPutChar(DEV_USART2, DBF_END_CODEID);
	#endif
	#ifdef DEBUG_DECODE_DBF
	char buf[1024];
	decodeDbfToText(msgPtr, msgLen, buf, sizeof(buf));
	uart_print("dbf ");
	uart_print(buf);
	uart_print("\n");
	#endif
	DbfSerializerReset(bytePacket);
}

void dbfSendShortMessage(int32_t code)
{
	DbfSerializerInit(&dbfTmpMessage);
	DbfSerializerWriteInt32(&dbfTmpMessage, code);
	dbfSendMessage(&dbfTmpMessage);
	DbfSerializerDeinit(&dbfTmpMessage);
}

#else
#error
#endif
