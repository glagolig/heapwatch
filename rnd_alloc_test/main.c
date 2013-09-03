#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <time.h>
#include <unistd.h>

typedef char*(*FPTR)();
FPTR fptrs[110];
void InitFptrs();

#define ALLOCCOUNT 1000000
#define FREECOUNT  300000

int GetRandomSeed()
{
    clock_t c = clock();
    return (int)(c % 0x7FFFFFFF);
}

void
main()
{
    char** allocs;
    int i,j;
    int counts[110];
    int indices[110];
 
    InitFptrs(fptrs);
    allocs = malloc(sizeof(char*)*ALLOCCOUNT);

    srand(GetRandomSeed());

    for(i=0; i<ALLOCCOUNT; i++)
    {
        int rndIndex;
        rndIndex = rand() % 110;
        allocs[i] = fptrs[rndIndex]();
    }

    for(i=0; i<FREECOUNT; i++)
    {
        int rndIndex;
        int searchCount;
        rndIndex = (((rand()&0x7FFF) << 15) ^ (rand()&0x7FFF)) % ALLOCCOUNT;
        searchCount = 0;
        while(allocs[rndIndex] == NULL && searchCount < 10)
        {
            rndIndex = (rndIndex + 1) % ALLOCCOUNT;
            searchCount++;
        }
        if(allocs[rndIndex] == NULL) continue;

        free(allocs[rndIndex]);
        allocs[rndIndex] = NULL;
    }

    memset(counts, 0, sizeof(counts));

    for(i=0; i<ALLOCCOUNT; i++)
    {
        if(allocs[i] != NULL)
        {
            int idx;
            idx = atoi(allocs[i]+2);
            counts[idx]++;
        }
    }

    for(i=0; i<110; i++)
    {
        indices[i] = i;
    }

    for(i=0; i<109; i++) for(j=i+1; j<110; j++)
    {
        if(counts[i] < counts[j])
        {
            int tmp = counts[i];
            counts[i] = counts[j];
            counts[j] = tmp;
            tmp = indices[i];
            indices[i] = indices[j];
            indices[j] = tmp;
        }
    }

    /*
    for(i=1; i<11; i++)
    {
        if(counts[i-1] == counts[i])
        {
            for(j=i-1; j>=0; j--)
            {
                counts[j]++;
                fptrs[indices[j]]();
            }
        }
    }
    */

    printf("Expected result:\n");
    for(i=9; i>=0; i--)
    {
        printf("fn%d %d hits\n", indices[i], counts[i]);
    }

    while(1)
    {
        void* dummy;
        dummy = malloc(5);
        sleep(1);
        free(dummy);
    }
}
