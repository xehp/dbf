/*
sys_time.h

Henrik Bjorkman (www.eit.se)

History
Created 2016-11-08 by Henrik using code from:
	http://stackoverflow.com/questions/421860/capture-characters-from-standard-input-without-waiting-for-enter-to-be-pressed
*/
#pragma once

#include <inttypes.h>
#include <signal.h>


// If using C++ there is a another way:
// http://www.cplusplus.com/faq/sequences/arrays/sizeof-array/#cpp
#ifndef SIZEOF_ARRAY
//#define SIZEOF_ARRAY( a ) (sizeof( a ) / sizeof( a[ 0 ] ))
#define SIZEOF_ARRAY(a) ((sizeof(a)/sizeof(0[a])) / ((size_t)(!(sizeof(a) % sizeof(0[a])))))
#endif


#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))


//extern sig_atomic_t s_signal_received;


/*************/
/* FUNCTIONS */
/*************/

// Or should we only use EXIT_SUCCESS / EXIT_FAILURE?
enum
{
	NORMAL_EXIT = 0,
	NO_SERIAL_PORT_EXIT = 1,
	SIGNAL_HANDLER_EXIT = 2,
	ERROR_CREATING_THREAD_EXIT = 3,
	FILE_SYSTEM_USAGE_FAIL_EXIT = 4,
	SHUTDOWN_ORDER_FROM_WEB = 5,
	SHUTDOWN_ORDER_FROM_CLI = 6,
};

int st_kill_rival(int tcpip_port, int auto_kill_rival);
int st_kill_rival_s(const char* s_http_port, int auto_kill_rival);
void st_deinit(int exitCode);
int st_is_signal_received();
void st_set_signal_received(int sig);
int64_t st_get_posix_time_us();
int64_t st_get_sys_time_us();
void st_init();



// Here are some functions and macros to help debugging memory leaks.
// Performance will be affected.
// Define macro ST_DEBUG to get more debugging info.
// Or define macro NDEBUG to get less.
// For medium level do not define any of the two.
// WARNING ST_DEBUG is not thread safe. So do not use ST_DEBUG if multi threading is used.
#define ST_DEBUG
//#define NDEBUG


#ifndef ST_DEBUG

void* st_malloc(size_t size);
void* st_calloc(size_t num, size_t size);
void st_free(void* ptr);
int st_is_valid_size(const void* ptr, size_t size);
void* st_resize(void* ptr, size_t old_size, size_t new_size);
void* st_realloc(void* ptr, size_t new_size);
void* st_recalloc(void *ptr, size_t old_num, size_t new_num, size_t size);
int st_is_valid_min(const void* ptr, size_t size);
size_t st_size(const void* ptr);
int st_is_valid(const void* ptr);

#ifndef NDEBUG
// Macros with memory leak and buffer overwrite detection.
#define ST_MALLOC(size) st_malloc(size)
#define ST_CALLOC(num, size) st_calloc(num, size)
#define ST_FREE(ptr) {st_free(ptr); ptr = NULL;}
#define ST_ASSERT_SIZE(ptr, size) assert(st_is_valid_size(ptr, size))
#define ST_RESIZE(ptr, old_size, new_size) st_resize(ptr, old_size, new_size);
#define ST_ASSERT_MIN(ptr, size) assert(st_is_valid_min(ptr, size))
#define ST_FREE_SIZE(ptr, size) {assert(st_is_valid_size(ptr, size)); st_free(ptr); ptr = NULL;}
#define ST_REALLOC(ptr, new_size) st_realloc(ptr, new_size);
#define ST_ASSERT(ptr) assert(st_is_valid(ptr, size))
#else
// Macros with no memory leak or buffer overwrite detection.
#define ST_MALLOC(size) malloc(size);
#define ST_CALLOC(num, size) calloc(num, size)
#define ST_FREE(ptr) free(ptr)
#define ST_ASSERT_SIZE(ptr, size)
#define ST_ASSERT_MIN(ptr, size)
#define ST_FREE_SIZE(ptr, size) free(ptr))
#define ST_ASSERT(ptr)
#endif

#else

#ifdef NDEBUG
#error
#endif

// Macros and alloc code with tracing of allocations.
void* st_malloc(size_t size, const char *file, unsigned int line);
void* st_calloc(size_t num, size_t size, const char *file, unsigned int line);
void st_free(const void* ptr, const char *file, unsigned int line);
int st_is_valid_size(const void* ptr, size_t size);
void* st_resize(void* ptr, size_t old_size, size_t new_size, const char *file, unsigned int line);
void* st_realloc(void* ptr, size_t new_size, const char *file, unsigned int line);
int st_is_valid_min(const void* ptr, size_t size);
size_t st_size(const void* ptr);
void st_log_linked_list();
int st_is_valid(const void* ptr);

#define ST_MALLOC(size) st_malloc(size, __FILE__, __LINE__)
#define ST_CALLOC(num, size) st_calloc(num, size, __FILE__, __LINE__)
#define ST_FREE(ptr) {st_free(ptr, __FILE__, __LINE__); ptr = NULL;}
#define ST_ASSERT_SIZE(ptr, size) assert(st_is_valid_size(ptr, size))
#define ST_RESIZE(ptr, old_size, new_size) st_resize(ptr, old_size, new_size, __FILE__, __LINE__);
#define ST_ASSERT_MIN(ptr, size) assert(st_is_valid_min(ptr, size))
#define ST_FREE_SIZE(ptr, size) {assert(st_is_valid_size(ptr, size)); st_free(ptr, __FILE__, __LINE__); ptr = NULL;}
#define ST_REALLOC(ptr, new_size) st_realloc(ptr, new_size, __FILE__, __LINE__);
#define ST_ASSERT(ptr) assert(st_is_valid(ptr))

#endif

