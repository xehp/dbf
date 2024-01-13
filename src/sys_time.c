/*
sys_time.c

Created 2016-11-08 using code from comport_ip_server.
Henrik Bjorkman (www.eit.se)
*/

#ifdef __WIN32
#include <windows.h>
#include <stdint.h> // portable: uint64_t   MSVC: __int64
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
//#include <termios.h>
#include <inttypes.h>
#include <stdbool.h>
#include <ctype.h>
#include <assert.h>
#include <time.h>
#include <sys/stat.h>

#include "sys_time.h"

// Enable this macro if lots of debug logging is needed.
//#define DBG





#ifdef DBG
#define dbg(...) {fprintf(stdout, __VA_ARGS__);}
#else
#define dbg(...)
#endif


static int64_t start_time_us = 0;
static sig_atomic_t s_signal_received = 0;
static long alloc_counter = 0;
static long alloc_size = 0;
static int logged_alloc_counter = 0x10000;

/**
 * To shut down the server properly, call this.
 */
void st_deinit(int exitCode)
{
	printf("Shutting down server...\n");

	if ((alloc_counter) || (alloc_size))
	{
		printf("Memory leak detected: alloc_counter %ld, %ld bytes.\n", alloc_counter, alloc_size);
		#ifdef ST_DEBUG
		printf("Memory first allocated here:\n");
		st_log_linked_list();
		printf("\n");
		#else
		assert(alloc_counter == 0);
		#endif
	}

	/* Set to zero to ask all the other threads to stop their program loop. */
	s_signal_received = 1;

	/* Give them some time... */
	usleep(100000);

	// maintenance_cleanup(); // This is called from within maintenance_loop instead.

	printf("Good bye!\n");

	/* Make sure everything get's displayed. */
	fflush(stdout);

	exit(exitCode);
}


#ifndef __WIN32

// This is the signal callback that is registered by st_init.
static void st_signal_callback_handler(int sig_num)
{

	/* Reinstantiate signal handler. */
	signal(sig_num, st_signal_callback_handler);
	s_signal_received = sig_num;
	if (sig_num == SIGINT)
	{
		printf("SIGINT\n");
		fflush(stdout);
		//exit(SIGNAL_HANDLER_EXIT);
	}
}

#else

// https://docs.microsoft.com/en-us/windows/console/registering-a-control-handler-function
BOOL WINAPI CtrlHandler(DWORD fdwCtrlType)
{
    switch (fdwCtrlType)
    {
        // Handle the CTRL-C signal.
    case CTRL_C_EVENT:
        printf("Ctrl-C event\n\n");
        Beep(750, 300);
        return TRUE;

        // CTRL-CLOSE: confirm that the user wants to exit.
    case CTRL_CLOSE_EVENT:
        Beep(600, 200);
        printf("Ctrl-Close event\n\n");
        return TRUE;

        // Pass other signals to the next handler.
    case CTRL_BREAK_EVENT:
        Beep(900, 200);
        printf("Ctrl-Break event\n\n");
        return TRUE;

    case CTRL_LOGOFF_EVENT:
        Beep(1000, 200);
        printf("Ctrl-Logoff event\n\n");
        return FALSE;

    case CTRL_SHUTDOWN_EVENT:
        Beep(750, 500);
        printf("Ctrl-Shutdown event\n\n");
        return FALSE;

    default:
        return FALSE;
    }
    s_signal_received = fdwCtrlType;
}
#endif


int st_is_signal_received()
{
	return s_signal_received;
}

void st_set_signal_received(int sig)
{
	s_signal_received = sig;
}


#ifndef __WIN32
extern int gettimeofday (struct timeval *__restrict __tv,
			 void *) __THROW __nonnull ((1));


int64_t st_get_posix_time_us()
{
	struct timeval t;
	gettimeofday(&t, NULL);
	const int64_t m = 1000000LL;
	const int64_t us = (m*t.tv_sec)+t.tv_usec;
	return us;
}

int64_t st_get_sys_time_us()
{
	return st_get_posix_time_us() - start_time_us;
}


#else

// https://stackoverflow.com/questions/10905892/equivalent-of-gettimeday-for-windows

// MSVC defines this in winsock2.h!? If not uncomment this.
/*typedef struct timeval {
    long tv_sec;
    long tv_usec;
} timeval;*/

static int my_gettimeofday(struct timeval * tp, struct timezone * tzp)
{
    // Note: some broken versions only have 8 trailing zero's, the correct epoch has 9 trailing zero's
    // This magic number is the number of 100 nanosecond intervals since January 1, 1601 (UTC)
    // until 00:00:00 January 1, 1970
    static const uint64_t EPOCH = ((uint64_t) 116444736000000000ULL);

    SYSTEMTIME  system_time;
    FILETIME    file_time;
    uint64_t    time;

    GetSystemTime( &system_time );
    SystemTimeToFileTime( &system_time, &file_time );
    time =  ((uint64_t)file_time.dwLowDateTime )      ;
    time += ((uint64_t)file_time.dwHighDateTime) << 32;

    tp->tv_sec  = (long) ((time - EPOCH) / 10000000L);
    tp->tv_usec = (long) (system_time.wMilliseconds * 1000);
    return 0;
}

int64_t utime_us()
{
	struct timeval t;
	my_gettimeofday(&t, NULL);
	const int64_t us = t.tv_sec*1000000L+t.tv_usec;
	return us;
}

#endif

/**
 * @brief Kills all processes that use the port that this program wants to use. If it can.
 * May need additional package:
 * sudo apt-get install lsof
 */
int st_kill_rival(int tcpip_port, int auto_kill_rival)
{
	int n = 0;
#ifndef __WIN32
	int my_id = getpid();

	FILE *fp;
	char path[1035];

	char command [256];
	snprintf(command, sizeof(command), "lsof -t -i:%d", tcpip_port);

	printf("Trying command: '%s'\n", command);

	/* Open the command for reading. */
	fp = popen(command, "r");
	if (fp == NULL) {
		printf("Failed to run '%s'\n", command);
		exit(1);
	}

	/* Read the output a line at a time - output it. */
	while (fgets(path, sizeof(path)-1, fp) != NULL)
	{
		printf(" %s ", path);
		pid_t pid = strtol(path, NULL, 10);
		if (pid != my_id)
		{
			if (auto_kill_rival) {
				// It seems this will also kill clients such as firefox if the server is still up.
				// Perhaps not what we want.
				kill(pid, 9);
				printf("Killed: %u\n", (unsigned int)pid);
				++n;
			} else {
				printf("Unresolved conflict for port: '%d' %u\n", tcpip_port, (unsigned int)pid);
				exit(1);
				++n;
			}
		}
	}

	if (n > 0) {
		// Need a sleep here or else we will not get wanted port anyway.
		usleep(3000000);
	}
	else
	{
		printf("No rival found for port '%d'\n", tcpip_port);
	}

	/* Close. */
	pclose(fp);
#else
	// TODO Not implemented yet
#endif
	return n;
}

int st_kill_rival_s(const char* s_http_port, int auto_kill_rival)
{
	assert(s_http_port);
	return st_kill_rival(atoi(s_http_port), auto_kill_rival);
}


void st_init()
{
	#ifndef __WIN32
	/* Configure unix signal. */
	signal(SIGTERM, st_signal_callback_handler);
	signal(SIGINT, st_signal_callback_handler);

	/* Configure terminal. */
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IOLBF, 0);
	#else
	SetConsoleCtrlHandler(CtrlHandler, TRUE);
	#endif

	alloc_counter = 0;
	alloc_size = 0;

	start_time_us = st_get_posix_time_us();
}


// Uncomment if all memory shall be filled with pattern when allocated.
#define ST_DEBUG_FILL_PATTERN 0x00

#define ST_MAGIC_NUMBER 0x67

#if defined(__x86_64)
#define ST_MAX_SIZE 0x100000000000
#else
#define ST_MAX_SIZE 0xF0000000
#endif


typedef struct header header;

struct header
{
	size_t size; // including header and footer
	#ifdef ST_DEBUG
	header *prev;
	header *next;
	const char* file;
	size_t line;
	#endif
};

#define ST_HEADER_SIZE (sizeof(header))
#define ST_FOOTER_SIZE 1

#ifdef ST_DEBUG

// TODO These are not thread safe. So do not use ST_DEBUG if multithreading is used.
static header *head = NULL;
static header *tail = NULL;

static void add_linked_list(header *h, const char *file, unsigned int line)
{
	if (tail == NULL)
	{
		assert(head==NULL);
		head = h;
		tail = h;
		h->next = NULL;
		h->prev = NULL;
	}
	else
	{
		// New objects are added at tail.
		assert(head!=NULL);
		assert(tail->next==NULL);
		h->prev = tail;
		tail->next = h;
		tail = h;
		h->next = NULL;
	}
	h->file = file;
	h->line = line;
}

static void remove_from_linked_list(header *h, const char *file, unsigned int line)
{
	if (h->prev != NULL)
	{
		h->prev->next = h->next;
	}
	else
	{
		head = h->next;
	}
	if (h->next != NULL)
	{
		h->next->prev = h->prev;
	}
	else
	{
		tail = h->prev;
	}
}

void st_log_linked_list()
{
	header* ptr = head;
	while(ptr)
	{
		printf("%s:%zu (%zu bytes)\n", ptr->file, ptr->line, ptr->size-(ST_HEADER_SIZE+ST_FOOTER_SIZE));
		ptr = ptr->next;
	}
}
#endif

// To help debugging use these instead of malloc/free directly.
// This will add a size field to every allocated data area and
// when free is done it can check that data is valid.
// This will cause some overhead but it can easily be removed
// later when the program works perfectly.

// Allocate a block of memory, when no longer needed sys_free
// must be called.
#ifndef ST_DEBUG
void* st_malloc(size_t size)
#else
void* st_malloc(size_t size, const char *file, unsigned int line)
#endif
{
	assert(size<=ST_MAX_SIZE);

	const size_t size_inc_header_footer = size + (ST_HEADER_SIZE+ST_FOOTER_SIZE);
	uint8_t* p = malloc(size_inc_header_footer);
	assert(p != 0);
	#ifdef ST_DEBUG_FILL_PATTERN
	memset(p, ST_DEBUG_FILL_PATTERN, size_inc_header_footer);
	#endif
	header* h = (header*)p;

	h->size = size_inc_header_footer;
	//*(size_t*)(ptr+size_inc_header_footer-ST_FOOTER_SIZE) = ST_MAGIC_NUMBER;
	#if ST_FOOTER_SIZE == 1
	p[size_inc_header_footer-1] = ST_MAGIC_NUMBER;
	assert(p[size_inc_header_footer-1] == ST_MAGIC_NUMBER);
	#else
	memset(p + size + ST_HEADER_SIZE, ST_MAGIC_NUMBER, ST_FOOTER_SIZE);
	#endif
	++alloc_counter;
	alloc_size += size;
	#ifdef ST_DEBUG
	add_linked_list(h, file, line);
	#endif
	// Some logging (this can be removed later).
	if (alloc_counter >= (2*logged_alloc_counter))
	{
		dbg("sys_alloc %lu %ld\n", alloc_size, alloc_counter);
		logged_alloc_counter = alloc_counter;
	}

	return p + ST_HEADER_SIZE;
}

#ifndef ST_DEBUG
void* st_calloc(size_t num, size_t size)
#else
void* st_calloc(size_t num, size_t size, const char *file, unsigned int line)
#endif
{
	assert(size<ST_MAX_SIZE);
	const size_t size_inc_header_footer = (num * size) + (ST_HEADER_SIZE+ST_FOOTER_SIZE);
	uint8_t* p = malloc(size_inc_header_footer);
	assert(p != 0);
	memset(p, 0, size_inc_header_footer);
	header* h = (header*)p;
	h->size = size_inc_header_footer;
	//*(size_t*)(ptr+size_inc_header_footer-ST_FOOTER_SIZE) = ST_MAGIC_NUMBER;
	#if ST_FOOTER_SIZE == 1
	p[size_inc_header_footer-1] = ST_MAGIC_NUMBER;
	assert(p[size_inc_header_footer-1] == ST_MAGIC_NUMBER);
	#else
	memset(p+(num * size) + (ST_HEADER_SIZE), ST_MAGIC_NUMBER, ST_FOOTER_SIZE);
	#endif
	++alloc_counter;
	alloc_size += (num * size);
	#ifdef ST_DEBUG
	add_linked_list(h, file, line);
	#endif
	// Some logging (this can be removed later).
	if (alloc_counter >= (2*logged_alloc_counter))
	{
		dbg("sys_alloc %lu %ld\n", alloc_size, alloc_counter);
		logged_alloc_counter = alloc_counter;
	}

	return p + ST_HEADER_SIZE;
}

// This must be called for all memory blocks allocated using
// sys_alloc when the memory block is no longer needed.
// TODO Shall we allow free on a NULL pointer? Probably not but for now we do.
#ifndef ST_DEBUG
void st_free(void* ptr)
#else
void st_free(const void* ptr, const char *file, unsigned int line)
#endif
{
	//printf("sys_free %ld %d\n", (long)size, alloc_counter);
	if (ptr != NULL)
	{
		uint8_t *p = (uint8_t*)ptr - ST_HEADER_SIZE;
		header* h = (header*)p;
		const size_t size_inc_header_footer = h->size;
		assert((size_inc_header_footer>ST_HEADER_SIZE) && (size_inc_header_footer < (ST_MAX_SIZE + (ST_HEADER_SIZE + ST_FOOTER_SIZE))));
		#if ST_FOOTER_SIZE == 1
		assert(p[size_inc_header_footer-1] == ST_MAGIC_NUMBER);
		#else
		for(int i=0;i<ST_FOOTER_SIZE;i++) {assert(p[size_inc_header_footer-(1+i)] == ST_MAGIC_NUMBER);}
		#endif
		h->size = 0;
		#ifdef ST_DEBUG
		remove_from_linked_list(h, file, line);
		#endif
		#ifdef ST_DEBUG_FILL_PATTERN
		memset(p, 0, size_inc_header_footer);
		#else
		p[size_inc_header_footer-1] = 0;
		#endif
		free(p);
		--alloc_counter;
		alloc_size -= (size_inc_header_footer - (ST_HEADER_SIZE+ST_FOOTER_SIZE));
		assert(alloc_counter>=0);
	}
	else
	{
		//printf("sys_free NULL %zu\n", size);
	}
}

// This can be used to check that a memory block allocated by sys_alloc
// is still valid (at least points to an object of expected size).
int st_is_valid_size(const void* ptr, size_t size)
{
	assert(ptr);
	const uint8_t *p = (uint8_t*)ptr - ST_HEADER_SIZE;
	const header* h = (header*)p;
	const size_t size_inc_header_footer = h->size;
	return ((ptr != NULL) && (size_inc_header_footer == size + (ST_HEADER_SIZE+ST_FOOTER_SIZE)) && (p[size_inc_header_footer-1] == ST_MAGIC_NUMBER));
}

int st_is_valid_min(const void* ptr, size_t size)
{
	assert(ptr);
	const uint8_t *p = (uint8_t*)ptr - ST_HEADER_SIZE;
	const header* h = (header*)p;
	const size_t size_inc_header_footer = h->size;
	return (ptr != NULL) && (size_inc_header_footer >= size + (ST_HEADER_SIZE+ST_FOOTER_SIZE)) && (p[size_inc_header_footer-1] == ST_MAGIC_NUMBER);
}

int st_is_valid(const void* ptr)
{
	assert(ptr);
	const uint8_t *p = (uint8_t*)ptr - ST_HEADER_SIZE;
	const header* h = (header*)p;
	const size_t size_inc_header_footer = h->size;
	return (ptr != NULL) && (p[size_inc_header_footer-1] == ST_MAGIC_NUMBER);
}


#ifndef ST_DEBUG
void* st_resize(void* ptr, size_t old_size, size_t new_size)
#else
void* st_resize(void* ptr, size_t old_size, size_t new_size, const char *file, unsigned int line)
#endif
{
	assert((st_is_valid_size(ptr, old_size)) && (new_size > old_size));
	uint8_t* old_ptr = ptr - ST_HEADER_SIZE;
	const size_t new_size_inc_header_footer = new_size + (ST_HEADER_SIZE+ST_FOOTER_SIZE);
	#ifdef ST_DEBUG
	remove_from_linked_list((header*)old_ptr, file, line);
	#endif
	#if 0
	uint8_t *new_ptr = realloc(old_ptr, new_size_inc_header_footer); // Sometimes "corrupted size vs. prev_size" happen here.
	#else
	uint8_t *new_ptr = malloc(new_size_inc_header_footer);
	memcpy(new_ptr + ST_HEADER_SIZE, old_ptr + ST_HEADER_SIZE, old_size);
	free(old_ptr);
	old_ptr = NULL;
	#endif
	assert(new_ptr != 0);
	#ifdef ST_DEBUG_FILL_PATTERN
	memset(new_ptr + ST_HEADER_SIZE + old_size, ST_DEBUG_FILL_PATTERN, new_size - old_size);
	#endif
	#ifdef ST_DEBUG
	add_linked_list((header*)new_ptr, file, line);
	#endif

	*(size_t*)new_ptr = new_size_inc_header_footer;
	new_ptr[new_size_inc_header_footer-1] = ST_MAGIC_NUMBER;
	assert(new_ptr[new_size_inc_header_footer-1] == ST_MAGIC_NUMBER);
	alloc_size += (new_size - old_size);
	return new_ptr + ST_HEADER_SIZE;
}

// Shall be same as standard realloc but with our extra debugging checks.
#ifndef ST_DEBUG
void* st_realloc(void* ptr, size_t new_size)
#else
void* st_realloc(void* ptr, size_t new_size, const char *file, unsigned int line)
#endif
{
	if (ptr)
	{
		assert(*(size_t*)(ptr - ST_HEADER_SIZE) >= (ST_HEADER_SIZE+ST_FOOTER_SIZE));
		const uint8_t *old_ptr = (uint8_t*)ptr - ST_HEADER_SIZE;
		const size_t old_size_inc_header_footer = *(size_t*)old_ptr;
		const size_t old_size = old_size_inc_header_footer - (ST_HEADER_SIZE+ST_FOOTER_SIZE);
		#ifndef ST_DEBUG
		return st_resize(ptr, old_size, new_size);
		#else
		return st_resize(ptr, old_size, new_size, file, line);
		#endif
	}
	else
	{
		#ifdef ST_DEBUG
		return st_malloc(new_size, __FILE__, __LINE__);
		#else
		return st_malloc(new_size);
		#endif
	}
}


size_t st_size(const void *ptr)
{
	if (ptr)
	{
		assert(*(size_t*)(ptr - ST_HEADER_SIZE) >= (ST_HEADER_SIZE+ST_FOOTER_SIZE));
		uint8_t *p = (uint8_t*)ptr - ST_HEADER_SIZE;
		size_t old_size_inc_header_footer = *(size_t*)p;
		return old_size_inc_header_footer - (ST_HEADER_SIZE+ST_FOOTER_SIZE);
	}
	else
	{
		return 0;
	}
}
