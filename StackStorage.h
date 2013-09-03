#ifndef _STACK_STORAGE_
#define _STACK_STORAGE_

#include <stdio.h>
#include <stdlib.h>

typedef int STACK_ID;

STACK_ID ReferenceStack();
void DereferenceStack(STACK_ID stackId);
void DumpPopularStacks(FILE* f);
void InitStackStorage();

#endif

