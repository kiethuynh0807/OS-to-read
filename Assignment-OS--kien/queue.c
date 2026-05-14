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
                return;
        if (q->size >= MAX_QUEUE_SIZE)
                return;
        q->proc[q->size] = proc;
        q->size++;      
        return;

}
struct pcb_t *dequeue(struct queue_t *q)
{
        /* Return a pcb whose priority is the highest (smallest numerical value)
         * in the queue [q] and remove it from q */
        
        if (q == NULL || q->size <= 0)
                return NULL;

        int res_idx = 0;
        
        // Start by assuming the first process has the highest priority
        #ifdef MLQ_SCHED
        uint32_t top_prio = q->proc[0]->prio;
        #else
        uint32_t top_prio = q->proc[0]->priority;
        #endif

        // Find the smallest numerical priority value
        for (int i = 1; i < q->size; i++) {
                #ifdef MLQ_SCHED
                uint32_t current_prio = q->proc[i]->prio;
                #else
                uint32_t current_prio = q->proc[i]->priority;
                #endif

                if (current_prio < top_prio) {
                        top_prio = current_prio;
                        res_idx = i;
                }
        }

        // Store the pointer to return
        struct pcb_t *proc = q->proc[res_idx];

        // Remove the element and shift the remaining processes left
        for (int i = res_idx; i < q->size - 1; i++) {
                q->proc[i] = q->proc[i + 1];
        }
        
        // Clean the last slot and update size
        q->proc[q->size - 1] = NULL;
        q->size--;

        return proc;
}


struct pcb_t *purgequeue(struct queue_t *q, struct pcb_t *proc)
{
        /* TODO: remove a specific item from queue
         * */
        if (q == NULL || proc == NULL || q->size == 0)
                return NULL;
        int idx = -1;
        for (int i = 0; i < q->size; i++) {
                if (q->proc[i] == proc) {
                        idx = i;
                        break;
                }
        }
        if (idx == -1)
                return NULL;
        for (int i = idx; i < q->size - 1; i++) {
                q->proc[i] = q->proc[i + 1];
        }
        q->size--;
        return proc;
}