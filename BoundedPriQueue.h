#ifndef _BOUNDED_PRI_QUEUE_
#define _BOUNDED_PRI_QUEUE_

struct _BOUNDED_PRI_QUEUE;

typedef struct _BOUNDED_PRI_QUEUE* BOUNDED_PRI_QUEUE;

BOUNDED_PRI_QUEUE CreateBoundedPriQueue(int limit);
void DestroyBoundedPriQueue(BOUNDED_PRI_QUEUE queue);
void Enqueue(BOUNDED_PRI_QUEUE queue, int elem, void* context);
void* Dequeue(BOUNDED_PRI_QUEUE queue);

#endif
