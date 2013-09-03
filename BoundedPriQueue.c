#include "BoundedPriQueue.h"

#include <stdlib.h>

void* (*RealMalloc)(size_t size);
void (*RealFree)(void *ptr);

typedef struct _Item Item;

struct _Item
{
    int priority;
    void* context;
};

struct _BOUNDED_PRI_QUEUE
{
    int limit;
    int count;
    Item* items;
};

#define MAX_ITEMS 0x10000

// <-- Helper functions
int GetParent(int index)
{
    if(index <= 1) return -1;
    return index/2;
}

int GetFirstChild(int index)
{
    return 2*index;
}

void Swap(BOUNDED_PRI_QUEUE queue, int i, int j)
{
        int tmp;
        void* tmpContext;
        tmp = queue->items[i].priority;
        tmpContext = queue->items[i].context;
        queue->items[i].priority = queue->items[j].priority;
        queue->items[i].context = queue->items[j].context;
        queue->items[j].priority = tmp;
        queue->items[j].context = tmpContext;
}
// Helper functions -->

BOUNDED_PRI_QUEUE CreateBoundedPriQueue(int limit)
{
    int i;
    BOUNDED_PRI_QUEUE queue;
    if(limit <= 0 || limit > MAX_ITEMS)
    {
        return 0;
    }
    queue = RealMalloc(sizeof(struct _BOUNDED_PRI_QUEUE));
    if(queue)
    {
        queue->items = RealMalloc((limit+2)*sizeof(Item));
        if(queue->items)
        {
            for(i=0; i<limit+2; i++)
            {
                queue->items[i].priority = -1;
                queue->items[i].context = 0;
            }
            queue->count = 0;
            queue->limit = limit;
        }
        else
        {
            RealFree(queue);
            queue = 0;
        }
    }
    return queue;
}

void DestroyBoundedPriQueue(BOUNDED_PRI_QUEUE queue)
{
    RealFree(queue->items);
    RealFree(queue);
}

void Enqueue(BOUNDED_PRI_QUEUE queue, int elem, void* context)
{
    int index;
    int parent;

    index = queue->count+1;
    queue->items[index].priority = elem;
    queue->items[index].context = context;
    queue->count++;
    parent = GetParent(index);
    while(parent != -1 &&
          queue->items[parent].priority > queue->items[index].priority)
    {
        Swap(queue, index, parent);
        index = parent;
        parent = GetParent(index);
    }

    while(queue->count > queue->limit)
    {
        Dequeue(queue);
    }
}

void* Dequeue(BOUNDED_PRI_QUEUE queue)
{
    void* result;
    int index;
    int child;
    int minChild;
    if(queue->count <= 0)
    {
        return 0;
    }

    result = queue->items[1].context;
    index = 1;
    queue->items[1].context = 0;
    queue->items[1].priority = -1;
    Swap(queue, index, queue->count);
    queue->count--;
    
    while(index <= queue->count)
    {
        minChild = -1;
        child = GetFirstChild(index);
        if(child > queue->count) break;
        minChild = child;
        if(child+1 <= queue->count)
        {
            if(queue->items[child+1].priority < queue->items[child].priority)
            {
                minChild = child+1;
            }
        }
        if(queue->items[index].priority <= queue->items[minChild].priority)
        {
            break;
        }
        Swap(queue, index, minChild);
        index = minChild;
    }

    return result;
}
