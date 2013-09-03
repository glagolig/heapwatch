#define _GNU_SOURCE

#include <signal.h>
#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <execinfo.h>
#include <memory.h>
#include <stdarg.h>

#include "StackStorage.h"

static int gLogCount = 0;
static int gIsLogEnabled = 1;
#define LOG_COUNT_LIMIT 10
void PreloadLog(const char* format, ...)
{
    if(gIsLogEnabled)
    {
        gLogCount++;
        if(gLogCount <= LOG_COUNT_LIMIT)
        {
            va_list args;
            va_start (args, format);
            vprintf (format, args);
            va_end (args);            
        }
    }
}

static int gIsDumpRequested = 0;
static int gMallocCount = 0;
static int gCallocCount = 0;
static int gFreeCount = 0;
static int gReallocCount = 0;
static int gMemalignCount = 0;
static int gVallocCount = 0;
static int gMismatchedFreeCount = 0;
static int gMismatchedReallocCount = 0;

static pthread_mutex_t gMutex;
__thread int inCall;

static char dummyBuf[8192];
static unsigned long dummyPos = 0;
static unsigned long dummyAllocs = 0;

void UserSignalHandler(int sig);

void* (*RealMalloc)(size_t size);
void* (*RealCalloc)(size_t nmemb, size_t size);
void* (*RealRealloc)(void *ptr, size_t size);
void* (*RealValloc)(size_t size);
int   (*RealPosixMemalign)(void** memptr, size_t alignment, size_t size);
void  (*RealFree)(void *ptr);

static void* (*TempMalloc)(size_t size);
static void* (*TempCalloc)(size_t nmemb, size_t size);
static void* (*TempRealloc)(void *ptr, size_t size);
static void* (*TempValloc)(size_t size);
static int   (*TempPosixMemalign)(void** memptr, size_t alignment, size_t size);
static void  (*TempFree)(void *ptr);

int   (*RealBacktrace)(void**,int);
char**(*RealBacktraceSymbols)(void*const*,int);

typedef struct _BLOCK_HDR
{
    STACK_ID magic;
    STACK_ID stackId;

} BLOCK_HDR;

#define MAGIC 0xA0BC0DEF

void* DummyMalloc(size_t size)
{
    if (dummyPos + size >= sizeof(dummyBuf))
    {
        exit(1);
    }
    void *retptr = dummyBuf + dummyPos;
    dummyPos += size;
    ++dummyAllocs;
    return retptr;
}

void* DummyCalloc(size_t nmemb, size_t size)
{
    void *ptr = DummyMalloc(nmemb * size);
    unsigned int i = 0;
    for (; i < nmemb * size; ++i)
    {
        ((char*)ptr)[i] = 0;
    }
    return ptr;
}

void DummyFree(void *ptr)
{
}

void __attribute__((constructor)) Init()
{
    inCall = 1;
    // This runs on startup
    PreloadLog("Initializing memory traces...\n");

    // init lock
     pthread_mutex_init(&gMutex, NULL);

    // set signal handler
    signal(31, UserSignalHandler);

    // hook malloc
    // use Dummy* implementation on initialization (may be called from dlsym etc)
    RealMalloc         = DummyMalloc;
    RealCalloc         = DummyCalloc;
    RealRealloc        = NULL;
    RealFree           = DummyFree;
    RealValloc         = NULL;
    RealPosixMemalign  = NULL;

    // get addresses of real malloc, free, etc
    TempMalloc         = dlsym(RTLD_NEXT, "malloc");
    TempCalloc         = dlsym(RTLD_NEXT, "calloc");
    TempRealloc        = dlsym(RTLD_NEXT, "realloc");
    TempFree           = dlsym(RTLD_NEXT, "free");
    TempValloc         = dlsym(RTLD_NEXT, "valloc");
    TempPosixMemalign  = dlsym(RTLD_NEXT, "posix_memalign");

    if (!TempMalloc || !TempCalloc || !TempRealloc || !TempFree ||
        !TempValloc || !TempPosixMemalign)
    {
        fprintf(stderr, "Error in `dlsym`: %s\n", dlerror());
        exit(1);
    }

    // 
    RealMalloc         = TempMalloc;
    RealCalloc         = TempCalloc;
    RealRealloc        = TempRealloc;
    RealFree           = TempFree;
    RealValloc         = TempValloc;
    RealPosixMemalign  = TempPosixMemalign;

    RealBacktrace = dlsym(RTLD_NEXT, "backtrace");
    RealBacktraceSymbols = dlsym(RTLD_NEXT, "backtrace_symbols");
    if(!RealBacktrace || !RealBacktraceSymbols)
    {
        fprintf(stderr, "error in `dlsym`(2): %s\n", dlerror());
    }

    PreloadLog("Initializing stack storage...\n");
    InitStackStorage();
    PreloadLog("Done\n");
    inCall = 0;
}

void UserSignalHandler(int sig)
{
    if(sig == 31)
    {
        gIsDumpRequested = 1;
    }
}

void DumpInfo();

void* malloc(size_t size)
{
    STACK_ID stackId;
    BLOCK_HDR* blockHdr;

    if(inCall)
    {
        return RealMalloc(size);
    }
    inCall = 1;

    __sync_add_and_fetch(&gMallocCount, 1);
    int isDumpRequested = 0;
    if(gIsDumpRequested)
    {
        pthread_mutex_lock(&gMutex);
        if(gIsDumpRequested)
        {
            gIsDumpRequested = 0;
            isDumpRequested = 1;
        }
        pthread_mutex_unlock(&gMutex);
    }

    if(isDumpRequested)
    {
        DumpInfo();
    }

    stackId = ReferenceStack();
    blockHdr = RealMalloc(size + sizeof(BLOCK_HDR));
    blockHdr->magic = MAGIC;
    blockHdr->stackId = stackId;

    PreloadLog("malloc stackId: %08x, orig.size: %d, act.size: %d, ptr: %p, user ptr: %p\n",
               stackId,
               size,
               size+sizeof(BLOCK_HDR),
               blockHdr,
               (char*)blockHdr + sizeof(BLOCK_HDR));

    inCall = 0;
    return (void*)((char*)blockHdr + sizeof(BLOCK_HDR));
}

void* calloc(size_t nmemb, size_t size)
{
    STACK_ID stackId;
    BLOCK_HDR* blockHdr;
    void* result;

    if(inCall)
    {
        return RealCalloc(nmemb, size);
    }
    inCall = 1;

    __sync_add_and_fetch(&gCallocCount, 1);
    int isDumpRequested = 0;
    if(gIsDumpRequested)
    {
        pthread_mutex_lock(&gMutex);
        if(gIsDumpRequested)
        {
            gIsDumpRequested = 0;
            isDumpRequested = 1;
        }
        pthread_mutex_unlock(&gMutex);
    }

    if(isDumpRequested)
    {
        DumpInfo();
    }

    stackId = ReferenceStack();
    blockHdr = RealMalloc(nmemb*size + sizeof(BLOCK_HDR));
    blockHdr->magic = MAGIC;
    blockHdr->stackId = stackId;
    result = (void*)((char*)blockHdr + sizeof(BLOCK_HDR));
    memset(result, 0, nmemb*size);

    PreloadLog("calloc stackId: %08x, orig.size: %d, act.size: %d, ptr: %p, user ptr: %p\n",
               stackId,
               size*nmemb,
               size*nmemb+sizeof(BLOCK_HDR),
               blockHdr,
               result);

    inCall = 0;
    return result;
}

void* realloc(void *ptr, size_t size)
{
    BLOCK_HDR* blockHdr;
    void* result;
    if(inCall)
    {
        return RealRealloc(ptr, size);
    }

    blockHdr = (BLOCK_HDR*)((char*)ptr - sizeof(BLOCK_HDR));
    if(blockHdr->magic != MAGIC)
    {
        __sync_add_and_fetch(&gMismatchedReallocCount, 1);
        return RealRealloc(ptr, size);
    }
    
    inCall = 1;
    __sync_add_and_fetch(&gReallocCount, 1);

    PreloadLog("realloc stackId: %08x, orig.size: %d, act.size: %d, ptr: %p, user ptr: %p\n",
               blockHdr->stackId,
               size,
               size+sizeof(BLOCK_HDR),
               blockHdr,
               ptr);

    result = RealRealloc(blockHdr, size+sizeof(BLOCK_HDR));
    inCall = 0;
    return (void*)((char*)result+sizeof(BLOCK_HDR));
}

void free(void *ptr)
{
    BLOCK_HDR* blockHdr;
    if(ptr == NULL) return;
    if(inCall)
    {
        return RealFree(ptr);
    }

    blockHdr = (BLOCK_HDR*)((char*)ptr - sizeof(BLOCK_HDR));
    if(blockHdr->magic != MAGIC)
    {
        __sync_add_and_fetch(&gMismatchedFreeCount, 1);
        return RealFree(ptr);
    }

    inCall = 1;
    __sync_add_and_fetch(&gFreeCount, 1);
    int isDumpRequested = 0;
    if(gIsDumpRequested)
    {
        pthread_mutex_lock(&gMutex);
        if(gIsDumpRequested)
        {
            gIsDumpRequested = 0;
            isDumpRequested = 1;
        }
        pthread_mutex_unlock(&gMutex);
    }

    if(isDumpRequested)
    {
        DumpInfo();
    }

    DereferenceStack(blockHdr->stackId);

    PreloadLog("free stackId: %08x, ptr: %p, user ptr: %p\n",
               blockHdr->stackId,
               blockHdr,
               ptr);

    RealFree(blockHdr);
    inCall = 0;
}

int posix_memalign(void** memptr, size_t alignment, size_t size)
{
    __sync_add_and_fetch(&gMemalignCount, 1);
    return RealPosixMemalign(memptr, alignment, size);
}

void* valloc(size_t size)
{
    __sync_add_and_fetch(&gVallocCount, 1);
    return RealValloc(size);
}

void
DumpInfo()
{
    FILE* result = fopen("heapwatch.dump", "w");
    if(result != NULL)
    {
        fprintf(result, "malloc count: %d\n", gMallocCount);
        fprintf(result, "calloc count: %d\n", gCallocCount);
        fprintf(result, "realloc count: %d\n", gReallocCount);
        fprintf(result, "free count: %d\n", gFreeCount);
        fprintf(result, " *** non-zero numbers below mean trouble *** \n");        
        fprintf(result, "valloc count: %d\n", gVallocCount);
        fprintf(result, "memalign count: %d\n", gMemalignCount);
        fprintf(result, "mismatched free count: %d\n", gMismatchedFreeCount);
        fprintf(result, "mismatched realloc count: %d\n", gMismatchedReallocCount);
        fprintf(result, " ***   ***   ***\n\n");        
        DumpPopularStacks(result);
        fclose(result);
    }
}

