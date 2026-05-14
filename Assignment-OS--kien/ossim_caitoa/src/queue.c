#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

int empty(struct queue_t *q)
{
        if (q == NULL)
                return 1;
        return (q->size == 0);
}

void enqueue(struct queue_t *q, struct pcb_t *proc)
{
        /* TODO: put a new process to queue [q] */
if (q == NULL || proc == NULL)
{
        return;
}
if (q->size >= MAX_QUEUE_SIZE)
        return;
q->proc[q->size] = proc;
    q->size++;
}
void enqueue_front(struct queue_t *q, struct pcb_t *proc)
{
    if (q == NULL || proc == NULL)
        return;

    if (q->size >= MAX_QUEUE_SIZE)
        return;

    for (int i = q->size; i > 0; i--) {
        q->proc[i] = q->proc[i - 1];
    }

    q->proc[0] = proc;
    q->size++;
}


struct pcb_t *dequeue(struct queue_t *q)
{
    if (q == NULL || q->size == 0)
        return NULL;

    struct pcb_t *proc = q->proc[0];

    for (int i = 1; i < q->size; i++) {
        q->proc[i - 1] = q->proc[i];
    }

    q->size--;

    q->proc[q->size] = NULL;

    return proc;
}
struct pcb_t *purgequeue(struct queue_t *q, struct pcb_t *proc)
{
        /* TODO: remove a specific item from queue
         * */
if (q == NULL || proc == NULL || q->size == 0)
        return NULL;
for (int i = 0; i < q->size; i++) {
        if (q->proc[i] == proc) {

            struct pcb_t *ret = q->proc[i];

            for (int j = i + 1; j < q->size; j++) {
                q->proc[j - 1] = q->proc[j];
            }

            q->size--;
q->proc[q->size] = NULL;
            return ret;
        }
    }

    return NULL;
}
