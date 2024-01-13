/*****************************************************************************\ 
crc32.h

Functions to calculate 32 bit crc checksums.


History:

1.0 Created by Henrik Bjorkman 1996-04-30

\*****************************************************************************/

#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>

/* returns a 4 byte checksum for the buffer. */
uint32_t crc32_calculate(const unsigned char *buf, int size);

#endif
