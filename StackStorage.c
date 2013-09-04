#include "StackStorage.h"

#include <signal.h>
#ifndef _WIN32
#include <dlfcn.h>
#include <stddef.h>
#include <pthread.h>
#include <execinfo.h>
#endif
#include <string.h>

#include "BoundedPriQueue.h"

// note: __sync_add_and_fetch does not work
// if binary is built on Linux RHEL-4
static int atomic_add_return(int *v, int i)
{
int rc;
__asm__ (
"lock\n\t"
"xaddl %0,(%1)"
:"=r" (rc)
:"r" (v), "0" (i)
:"memory" );
return rc+i;
}

extern void* (*RealMalloc)(size_t size);
extern void* (*RealCalloc)(size_t nmemb, size_t size);
extern void* (*RealRealloc)(void *ptr, size_t size);
extern void* (*RealValloc)(size_t size);
extern int   (*RealPosixMemalign)(void** memptr, size_t alignment, size_t size);
extern void  (*RealFree)(void *ptr);

// Note: backtrace and backtrace_symbols APIs are not really overwritten,
// but we substitute fake ones for module-level testing.
extern int   (*RealBacktrace)(void**,int);
extern char**(*RealBacktraceSymbols)(void*const*,int);

typedef struct _StackStorageEntry StackStorageEntry;

struct _Dlink
{
    StackStorageEntry* next;
    StackStorageEntry* prev;
};

typedef struct _Dlink DLINK;

#define STORAGE_SIZE 1299721
//#define STORAGE_SIZE 10570841
#define STACK_LIMIT  30

// Number of top stacks to be printed (from 1 to 1000)
// Set by HEAPWATCH_SIZE env var, default is 10
#define ENV_DUMP_SIZE   "HEAPWATCH_SIZE"
#define MIN_DUMP 1
#define MAX_DUMP 1000
#define DEFAULT_DUMP 10
int heapWatchSize = 10;

// Method of stack dump ordering: by allocation count or by total size
// Set by HEAPWATCH_METHOD env var ("count" or "size"), default is by count
#define ENV_DUMP_METHOD "HEAPWATCH_METHOD"
typedef enum _HeapwatchMethod
{
    ByCount = 0,
    BySize  = 1

} HeapWatchMethod;
HeapWatchMethod heapWatchMethod = ByCount;

struct _StackStorageEntry
{
    StackStorageEntry* next;
    DLINK zomby;
    void* stackData[STACK_LIMIT];
    int stackDataSize;
    unsigned long long stackDataHash;
    STACK_ID stackId;
    unsigned int refCount;
    int allocSize;
};

StackStorageEntry** entriesById;
StackStorageEntry** entriesByDataHash;
DLINK zombyList;
static STACK_ID stackId = 0;
int storageOvf = 0;
#ifndef _WIN32
pthread_mutex_t storageMutex;
#endif

void
InitStorageLock()
{
#ifndef _WIN32
    pthread_mutex_init(&storageMutex, NULL);
#endif
}

void
LockStorage()
{
#ifndef _WIN32
    pthread_mutex_lock(&storageMutex);
#endif
}

void UnlockStorage()
{
#ifndef _WIN32
    pthread_mutex_unlock(&storageMutex);
#endif
}

STACK_ID GetNextStackId()
{
    return ++stackId;
}

// HASH_PRIME: 2,097,143 = 2^21 - 2^3 - 1      V
// HASH PRIME: 33,554,467 = 2^25 + 2^5 + 2^2   X
unsigned long HashFn(void** stackData, int stackDataSize)
{
    int i;
    long long result = 0;

    for(i=0; i<stackDataSize; i++)
    {
        long long tmp = (long long)stackData[i];
        tmp = (tmp << 21) - (tmp << 3) - tmp;
        result ^= tmp;
    }
    return (unsigned long)(result % STORAGE_SIZE);
}

void InitStackStorage()
{
    int i;
    char* envVar;
    entriesById = RealMalloc(sizeof(StackStorageEntry*)*STORAGE_SIZE);
    entriesByDataHash = RealMalloc(sizeof(StackStorageEntry*)*STORAGE_SIZE);
    if(entriesById == NULL || entriesByDataHash == NULL)
    {
        fprintf(stderr, "failure in InitStackStorage()\n");
        exit(1);
    }
    for(i=0; i<STORAGE_SIZE; i++)
    {
        entriesById[i] = NULL;
        entriesByDataHash[i] = NULL;
    }

    zombyList.prev = zombyList.next = NULL;

    InitStorageLock();

    envVar = getenv(ENV_DUMP_SIZE);
    if(envVar != NULL)
    {
        int size;
        size = atoi(envVar);
        if(size >= MIN_DUMP && size <= MAX_DUMP)
        {
            heapWatchSize = size;
        }
    }

    envVar = getenv(ENV_DUMP_METHOD);
    if(envVar != NULL)
    {
        if(strstr(envVar, "ize") != NULL ||
           strstr(envVar, "SIZE") != NULL)
        {
            heapWatchMethod = BySize;
        }
    }
}

STACK_ID ReferenceStack()
{
    int i;
    int stackId;
    StackStorageEntry* entry;
    void* currentStack[STACK_LIMIT];
    int stackSize;
    unsigned int stackHash;
    if(entriesById == NULL || entriesByDataHash == NULL) return 0;

    stackSize = RealBacktrace(currentStack, STACK_LIMIT);
    stackHash = HashFn(currentStack, stackSize);

    entry = entriesByDataHash[stackHash];
    while(entry != NULL)
    {
        if(IsStacksIdentical(currentStack,
                             stackSize,
                             entry->stackData,
                             entry->stackDataSize))
        {
            break;
        }
        entry = entry->next;
    }

    if(entry != NULL)
    {
#ifndef _WIN32
        atomic_add_return(&entry->refCount, 1);
#else
        entry->refCount++;
#endif
        return entry->stackId;
    }

    // new entry...
    entry = RealMalloc(sizeof(StackStorageEntry));
    entry->refCount = 1;
    entry->stackDataHash = stackHash;
    entry->stackDataSize = stackSize;
    for(i=0; i<stackSize; i++)
    {
        entry->stackData[i] = currentStack[i];
    }
    entry->next = NULL;
    entry->zomby.next = entry->zomby.prev = NULL;

    // race condition: if 2 threads have the same call stack they may
    // add 2 distinct entries for each stack
    LockStorage();
    stackId = entry->stackId = GetNextStackId();
    if(entry->stackId < STORAGE_SIZE)
    {
        entry->next = entriesByDataHash[stackHash];
        entriesByDataHash[stackHash] = entry;
        entriesById[stackId] = entry;
    }
    else
    {
        storageOvf = 1;
    }
    UnlockStorage();

    if(entry->stackId >= STORAGE_SIZE)
    {
        RealFree(entry);
    }

    return stackId;
}

void DereferenceStack(STACK_ID stackId)
{
    StackStorageEntry* entry;
    if(entriesById == NULL || entriesByDataHash == NULL) return;
    if(stackId >= STORAGE_SIZE || stackId <= 0) return;

    entry = entriesById[stackId];
#ifndef _WIN32
    atomic_add_return(&entry->refCount, -1);
#else
    entry->refCount--;
#endif
}

void DumpPopularStacks(FILE* f)
{
    int i,j;
    int entryCount = 0;
    int worstCollision = 0;
    int pri;
    BOUNDED_PRI_QUEUE q;
    StackStorageEntry* entry;

    if(entriesById == NULL || entriesByDataHash == NULL) return;

    q = CreateBoundedPriQueue(heapWatchSize);

    for(i=0; i<STORAGE_SIZE; i++)
    {
        int collisions = -1;
        StackStorageEntry* current = entriesByDataHash[i];
        while(current != NULL)
        {
            entryCount++;
            collisions++;
            if(collisions > worstCollision)
            {
                worstCollision = collisions;
            }
            Enqueue(q, current->refCount, current);
            current = current->next;
        }
    }

    fprintf(f, " *** StackStorage stats ***\n");
    fprintf(f, "StackStorage entry count: %d\n", entryCount);
    fprintf(f, "StackStorage worst collision count: %d\n", worstCollision);
    if(storageOvf)
    {
        fprintf(f, "Storage overflow (more than %d unique stacks), data may be invalid\n", STORAGE_SIZE);
    }
    fprintf(f, " ****************************\n\n");

    entry = (StackStorageEntry*)Dequeue(q);
    while(entry != NULL)
    {
        fprintf(f, "\nalloc count: %d\n", entry->refCount);
        char** strings = RealBacktraceSymbols(entry->stackData, entry->stackDataSize);
        fprintf(f, "stack: \n");
        for(j=0; j<entry->stackDataSize; j++)
        {
            fprintf(f, "%s\n", strings[j]);
        }
        RealFree(strings);
        entry = (StackStorageEntry*)Dequeue(q);
    }
}

int IsStacksIdentical(void** stack1,
                      int stackSize1,
                      void** stack2,
                      int stackSize2)
{
    int i;
    if(stackSize1 != stackSize2) return 0;
    for(i=0; i<stackSize1; i++)
    {
        if(stack1[i] != stack2[i]) return 0;
    }
    return 1;
}




