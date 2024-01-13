/*
 * misc_util.h
 *
 *  Created on: Sep 28, 2022
 *      Author: henrik
 */

#ifndef SRC_UTILITY_FUNCTIONS_H_
#define SRC_UTILITY_FUNCTIONS_H_

// TODO Do we need both of these includes?
#include <stdint.h>
#include <stddef.h>
#include <inttypes.h>

// NOTE This is for positive values only! See DIV_ROUND_INT for alternative.
#define DIV_ROUND_UINT(a,b) (((a)+((b)/2))/(b))

// This shall give a/b but result is rounded up not down as normal
// NOTE This is for positive values only!
#define DIV_ROUND_UINT_UP(a,b) (((a)+((b)-1))/(b))



// This should correctly round also combinations of positive and negative numbers.
#define DIV_ROUND_INT(a,b) ((a>=0) && (b>=0)) ? (DIV_ROUND_UINT(a, b)) :  (((a<0) && (b>=0)) ? (-DIV_ROUND_UINT(-a, b)) : (((a>=0) && (b<0)) ? (-DIV_ROUND_UINT(a, -b)) : ((DIV_ROUND_UINT(-a, -b)))))


int utility_decode_digit(int ch);
int utility_encode_digit(uint8_t d);
int utility_is_char_part_of_word(int ch);
const char* utility_find_next_word(const char* str);
int utility_word_length(const char* str);
int64_t utility_atoll(const char* str);
int utility_encode_into_hex(char *dst_ptr, int dst_size, const uint8_t* bin_ptr, int bin_len);
int utility_decode_from_hex(uint8_t *dst_ptr, int dst_size, const char* hex_str);
int is_cmd(const char* str, const char* cmd);

#endif /* SRC_UTILITY_FUNCTIONS_H_ */
