/*
 * dbf.h
 *
 *  Created on: Apr 3, 2018
 *      Author: henrik
 */

#ifndef DBF_H_
#define DBF_H_

#include <stddef.h>
#include <stdint.h>
#include <ctype.h>


// https://www.linuxquestions.org/questions/programming-9/c-preprocessor-define-for-32-vs-64-bit-long-int-4175658579/
#if defined(_MSC_VER) || (defined(__INTEL_COMPILER) && defined(_WIN32))
   #if defined(_M_X64)
      #define BITNESS 64
      #define LONG_SIZE 4
   #else
      #define BITNESS 32
      #define LONG_SIZE 4
   #endif
#elif defined(__clang__) || defined(__GNUC__)
   #if defined(__x86_64)
      #define BITNESS 64
   #else
      #define BITNESS 32
   #endif
   #if __LONG_MAX__ == 2147483647L
      #define LONG_SIZE 4
   #else
      #define LONG_SIZE 8
   #endif
#endif

#if BITNESS == 32
#define DBF_OPTIMIZE_FOR_32_BITS
#endif


#if (!defined __linux__) && (!defined __WIN32)
#define DBF_FIXED_MSG_SIZE 120
#endif

#if (defined __linux__) || (defined __WIN32)
#define DBF_AND_ASCII
#endif


// The extend code.
// In addition to the code, 7 data bits can be stored in a byte.
#define DBF_EXT_CODEID 0x80
#define DBF_EXT_CODEMASK 0x80
#define DBF_EXT_DATANBITS 7
#define DBF_EXT_DATAMASK ((1 << 7) - 1)

// Code used to encode positive integer
// In addition to the code, 6 data bits can be stored in first byte.
#define DBF_PINT_CODEID 0x40
#define DBF_PINT_DATANBITS 6
#define DBF_PINT_DATAMASK ((1 << 6) - 1)

// Code used to encode negative integer
// In addition to the code, 5 data bits can be stored in first byte.
#define DBF_NINT_CODEID 0x20
#define DBF_NINT_DATANBITS 5
#define DBF_NINT_DATAMASK ((1 << 5) - 1)

// Format or CRC code.
// If this is the last code in message then it is the CRC.
// In addition to the code, 4 data bits can be stored in first byte.
// So using this a 32 bit CRC can be encoded into 5 bytes.
#define DBF_FMTCRC_CODEID 0x10
#define DBF_FMTCRC_DATANBITS 4
#define DBF_FMTCRC_DATAMASK ((1 << 4) - 1)

#define DBF_REPEAT_CODEID 0x08
#define DBF_REPEAT_DATANBITS 3
#define DBF_REPEAT_DATAMASK ((1 << 3) - 1)

// The codes 0x04 and 0x02 are not currently in use.

// TODO perhaps swap these two so that start i 0x01?
#define DBF_END_CODEID 0x01
#define DBF_BEGIN_CODEID 0x00

typedef struct DbfUnserializer DbfUnserializer;
typedef struct DbfSerializer DbfSerializer;
typedef struct DbfReceiver DbfReceiver;

void dbfDebugLog(const char *str);

// When the 4 bit CRC code is not last then it is used to tell what format follows.
// This are the format codes currently supported.
// These are sent in a FMTCRC code.
typedef enum
{
	DBF_INT_BEGIN_CODE = 0,
	DBF_WORD_BEGIN_CODE = 1,
	DBF_STR_BEGIN_CODE = 2,
} dbf_format_codes;

// States for the DBF serializer encoderState.
typedef enum encoder_states_type encoder_states_type;
enum encoder_states_type
{
	DBF_ENCODER_IDLE = 0, // Perhaps remove this and use DBF_ENCODING_INT as initial value.
	DBF_ENCODING_INT = 1,
	DBF_ENCODING_WORD = 2,
	DBF_ENCODER_ERROR = 3,
	#ifdef DBF_AND_ASCII
	DBF_ENCODER_ASCII_MODE = 4,
	#endif
	DBF_ENCODING_STR = 5,
};

struct DbfSerializer {
	#if !defined DBF_FIXED_MSG_SIZE
	unsigned char *buffer;
	unsigned int capacity;
	#else
	unsigned char buffer[DBF_FIXED_MSG_SIZE];
	#endif
	unsigned int pos;
	encoder_states_type encoderState;
	#ifdef DBF_REPEAT_CODEID
	int64_t prev_code; // also used as word separator in ascii mode.
	unsigned long repeat_counter; // In ascii mode this is used to know if an end quote is needed.
	#endif
};

// TODO Some way to know/check after if we tried to write more than there was room for in the message.
void DbfSerializerDebug();
void DbfSerializerInit(DbfSerializer *dbfSerializer);
#ifdef DBF_AND_ASCII
void DbfSerializerInitAscii(DbfSerializer *dbfSerializer);
void DbfSerializerSetAsciiSeparator(DbfSerializer *dbfSerializer, int64_t ch);
void DbfSerializerInitSeparator(DbfSerializer *s, int64_t ch);
#endif


void DbfSerializerWriteInt32(DbfSerializer *dbfSerializer, int32_t i);
void DbfSerializerWriteInt64(DbfSerializer *dbfSerializer, int64_t i);
void DbfSerializerWriteString(DbfSerializer *dbfSerializer, const char *str);
void DbfSerializerWriteWord(DbfSerializer *dbfSerializer, const char *str);

void DbfSerializerReset(DbfSerializer *dbfSerializer);

// Add CRC and finalize.
void DbfSerializerWriteCrc(DbfSerializer *dbfSerializer);

// Finalize without adding CRC.
void DbfSerializerFinalize(DbfSerializer *dbfSerializer);

const unsigned char* DbfSerializerGetMsgPtr(DbfSerializer *dbfSerializer);

unsigned int DbfSerializerGetMsgLen(const DbfSerializer *dbfSerializer);

void DbfSerializerDeinit(DbfSerializer *dbfSerializer);

void DbfSerializerAllToString(const DbfSerializer *s, char *bufPtr, size_t bufSize);

typedef enum
{
	DbfNextIsIntegerState,
	DbfNextIsWordState, // A word is an unquoted string (may not contain space or slash).
	DbfNextIsStringState,
	DbfEndOfMsgState,
	#ifdef DBF_AND_ASCII
	DbfAsciiNumberState,
	DbfAsciiWordState,
	DbfAsciiStringState,
	#endif
	DbfUnserializerErrorState,
} DbfDecodingStateEnum;

typedef enum
{
	DbfNct, // NOTHING_CODE_TYPE
	DbfExt, // EXTENSION_CODE_TYPE, see also DBF_EXT_CODEID
	DbfPnc, // POSITIVE_NUMBER_CODE_TYPE, see also DBF_PINT_CODEID
	DbfNnc, // NEGATIVE_NUMBER_CODE_TYPE, see also DBF_NINT_CODEID
	DbfFoC, // FMTCRC_CODE_TYPE, format or CRC
	DbfRcc, // See also DBF_REPEAT_CODEID
	DbfEom, // End of message code
} DbfCodeTypesEnum;


typedef enum
{
	DBF_OK_CRC=0,
	DBF_NO_CRC=1,
	DBF_BAD_CRC=-1,
} DBF_CRC_RESULT;


struct DbfUnserializer
{
	const unsigned char *msgPtr;
	unsigned int msgSize;
	DbfDecodingStateEnum decodeState;
	unsigned int readPos;
	#ifdef DBF_REPEAT_CODEID
	int64_t current_code;
	unsigned long repeat_counter;
	#endif
};

// TODO Some way to know/check after if we tried to read more than there was in the message.

void DbfUnserializerInitNoCRC(DbfUnserializer *dbfUnserializer, const unsigned char *msgPtr, unsigned int msgSize);
DBF_CRC_RESULT DbfUnserializerInitTakeCrc(DbfUnserializer *dbfUnserializer, const unsigned char *msgPtr, unsigned int msgSize);
DBF_CRC_RESULT DbfUnserializerInitFromSerializer(DbfUnserializer *dbfUnserializer, const DbfSerializer *dbfSerializer);
DBF_CRC_RESULT DbfUnserializerInitCopyUnserializer(DbfUnserializer *, const DbfUnserializer *);
#ifdef DBF_AND_ASCII
DBF_CRC_RESULT DbfUnserializerInitAscii(DbfUnserializer *dbfUnserializer, const unsigned char *msgPtr, unsigned int msgSize);
DBF_CRC_RESULT DbfUnserializerInitAsciiSerializer(DbfUnserializer *u, const DbfSerializer *dbfSerializer);
#endif
DBF_CRC_RESULT DbfUnserializerInitEncoding(DbfUnserializer *u, const unsigned char *msgPtr, unsigned int msgSize, encoder_states_type encoding);
DBF_CRC_RESULT DbfUnserializerInitReceiver(DbfUnserializer *u, const DbfReceiver *receiver);
//DBF_CRC_RESULT DbfUnserializerInit(DbfUnserializer *dbfUnserializer, const unsigned char *msgPtr, unsigned int msgSize);

DbfDecodingStateEnum DbfUnserializerReadCodeState(DbfUnserializer *dbfUnserializer);

int32_t DbfUnserializerReadInt32(DbfUnserializer *dbfUnserializer);
int64_t DbfUnserializerReadInt64(DbfUnserializer *dbfUnserializer);


int DbfUnserializerRead(DbfUnserializer *dbfUnserializer, char* bufPtr, size_t bufLen);
long DbfUnserializerStringLength(const DbfUnserializer *dbfUnserializer);

int DbfUnserializerReadIsNextString(const DbfUnserializer *dbfUnserializer);

int DbfUnserializerReadIsNextInt(const DbfUnserializer *dbfUnserializer);

int DbfUnserializerReadIsNextEnd(const DbfUnserializer *dbfUnserializer);

DBF_CRC_RESULT DbfUnserializerReadCrc(DbfUnserializer *dbfUnserializer);

DbfCodeTypesEnum DbfUnserializerGetNextType(const DbfUnserializer *dbfUnserializer, unsigned int idx);

int DbfUnserializerGetIntRev(const DbfUnserializer *dbfUnserializer, unsigned int e);

void DbfUnserializerReadCrcAndLog(DbfUnserializer *dbfUnserializer);

char DbfUnserializerIsOk(DbfUnserializer *dbfUnserializer);

size_t DbfUnserializerReadAllToString(DbfUnserializer *u, char *bufPtr, size_t bufSize);
size_t DbfUnserializerCopyAllToString(const DbfUnserializer *u, char *bufPtr, size_t bufSize);

void DbfUnserializerDeinit(DbfUnserializer *dbfUnserializer);

#define DBF_RCV_TIMEOUT_MS 5000

// Buffer size must be an even number of 32 bit words,
// We depend on that in other parts of the program
// when messages are copied.
#define BUFFER_SIZE_IN_BYTES 1024

typedef enum
{
	DbfRcvInitialState,
	DbfRcvReceivingTxtState,
	DbfRcvReceivingMessageState,
	DbfRcvMessageReadyState,
	DbfRcvTxtReceivedState,
	DbfRcvDbfReceivedState,
	DbfRcvDbfReceivedMoreExpectedState,
	DbfRcvIgnoreInputState,
	DbfRcvErrorState,
} DbfReveiverCodeStateEnum;


struct DbfReceiver
{
	unsigned char buffer[BUFFER_SIZE_IN_BYTES];
	unsigned int msgSize;
	DbfReveiverCodeStateEnum receiverState;
	uint64_t msgtimestamp;
};

// This must be called any other DbfReceiver functions.
// Can also be used to reset the buffer so next message can be received.
void DbfReceiverInit(DbfReceiver *dbfReceiver);
void DbfReceiverDeinit(DbfReceiver *dbfReceiver);
void DbfReceiverReset(DbfReceiver *dbfReceiver);

// Call this at every character received. Returns >0 when there is a message to process.
int DbfReceiverProcessCh(DbfReceiver *dbfReceiver, unsigned char ch);

int DbfReceiverIsDbf(const DbfReceiver *dbfReceiver);
int DbfReceiverIsTxt(const DbfReceiver *dbfReceiver);

// This is intended to be called at a regular interval. It is used to check for timeouts in the receiving of messages.
void DbfReceiverCheckTimeout(DbfReceiver *dbfReceiver, int timout_ms);
void DbfReceiverTick(DbfReceiver *dbfReceiver);

// Note, this is not same as DbfUnserializerReadString. This gives the entire message in ascii.
int DbfReceiverToString(DbfReceiver *dbfReceiver, const char* bufPtr, int bufLen);
int DbfReceiverLogRawData(const DbfReceiver *dbfReceiver);

encoder_states_type DbfReceiverGetEncoding(const DbfReceiver *receiver);

#if defined __linux__ || defined __WIN32
void DbfLogBuffer(const char* prefix, const unsigned char *bufPtr, int bufLen);
void DbfLogBufferNoCrc(const char* prefix, const unsigned char *bufPtr, int bufLen);
#endif

extern DbfSerializer dbfTmpMessage;
void dbfSendMessage(DbfSerializer *bytePacket);

void dbfSendShortMessage(int32_t code);

int DbfUnserializerToSerializerAll(DbfUnserializer *u, DbfSerializer* s);


#endif /* DBF_H_ */
