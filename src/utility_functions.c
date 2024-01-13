/*
 * misc_util.c
 *
 *  Created on: Sep 28, 2022
 *      Author: henrik
 */


#include "assert.h"
#include "utility_functions.h"


static const int hex[]={'0','1','2','3','4','5','6','7', '8','9','A','B','C','D','E','F'};

int utility_encode_digit(uint8_t d)
{
	//return (d > 9) ? (d - 10) + 'a' : d + '0';
	return hex[d & 0xF];
}

// TODO Could use a table to make this faster.
int utility_decode_digit(int ch)
{
	if ((ch >= '0') && (ch <= '9'))
	{
		return ch - '0';
	}
	if ((ch >= 'a') && (ch <= 'f'))
	{
		return ch + 10 - 'a';
	}
	if ((ch >= 'A') && (ch <= 'F'))
	{
		return ch + 10 - 'A';
	}
	return -1;
}

int utility_is_char_part_of_word(int ch)
{
	return ((ch > ' ') && (ch <= '~'));
}

const char* utility_find_next_word(const char* str)
{
    // Skip leading spaces if any
    while ((*str) && (*str <= ' '))
    {
    	str++;
    }

    // Find end of word
    while (utility_is_char_part_of_word(*str))
    {
    	str++;
    }

    // Skip trailing spaces if any
    while ((*str) && (*str <= ' '))
    {
    	str++;
    }

    return str;
}

int utility_word_length(const char* str)
{
	int n = 0;

    // Find end of word
    while (utility_is_char_part_of_word(*str))
    {
    	str++;
    	n++;
    }

    return n;
}

int64_t utility_atoll(const char* str)
{
    assert(str);

    int64_t value = 0;

    // Skip leading spaces if any
    while (*str == ' ')
    {
    	str++;
    }

    switch(*str)
    {
    	case '-':
        	str++;
        	return -utility_atoll(str);
    	case '+':
        	str++;
        	return utility_atoll(str);
    	case '0':
        	str++;
        	if ((*str == 'x') || (*str == 'X'))
        	{
        		// hexadecimal
            	str++;
        		for(;;)
        		{
        			int d = utility_decode_digit(*str);
        			if (d<0) {break;}
        			value = value*16 + d;
        			str++;
        		}
        	}
        	else
        	{
        		// octal
        		while ((*str >= '0') && (*str <= '7'))
        		{
        			value = value*8 + (*str -'0');
        			str++;
        		}
        	}
        	break;
    	default:
    		// decimal
    		while ((*str >= '0') && (*str <= '9'))
    		{
    			value = value*10 + (*str -'0');
    			str++;
    		}
    		break;
    }

    return value;
}

// Returns number of characters written.
int utility_encode_into_hex(char *dst_ptr, int dst_size, const uint8_t* bin_ptr, int bin_len)
{
	int n = 0;
	// There must be room in destination area including trailing zero.
	assert((dst_size * 2 + 1) >= bin_len);
	for (int i = 0; i < bin_len; ++i)
	{
		const uint8_t d = *bin_ptr;
		dst_ptr[n++] = utility_encode_digit((d >> 4));
		dst_ptr[n++] = utility_encode_digit(d);
		++bin_ptr;
	}
	dst_ptr[n] = 0;
	return n;
}

// Returns number of bytes decoded.
// A negative number if failed.
int utility_decode_from_hex(uint8_t* dst_ptr, int dst_size, const char* hex_str)
{
	int n = 0;
	while ((n<dst_size) && (*hex_str != 0))
	{
		const int h = utility_decode_digit(*hex_str++);
		if (h<0) {return n;}
		const int l = utility_decode_digit(*hex_str++);
		if (l<0) {return -1;}
		const uint8_t d = (h << 4) | l;
		dst_ptr[n++] = d;
	}
	if (utility_decode_digit(*hex_str) >= 0)
	{
		return -1;
	}
	return n;
}


int is_cmd(const char* str, const char* cmd)
{
	while ((*str == *cmd) && (*cmd != 0))
	{
		++str;
		++cmd;
	}

	if (((*str == 0) || (*str == ' ') || (*str == '\t')) && (*cmd == 0))
	{
		return 1;
	}

	return 0;
}
