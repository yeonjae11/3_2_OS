//----------------------------------------------------------------
//
//  4190.307 Operating Systems (Fall 2024)
//
//  Project #3: SNULE: A Simplified Nerdy ULE Scheduler
//
//  October 10, 2024
//
//  Jin-Soo Kim (jinsoo.kim@snu.ac.kr)
//  Systems Software & Architecture Laboratory
//  Dept. of Computer Science and Engineering
//  Seoul National University
//
//----------------------------------------------------------------
// #define SNU 
#ifdef SNU
#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "snule.h"

int sysload = 0;

extern struct proc proc[NPROC];

int max(int a, int b){
    return a > b ? a : b;
}

int
is(struct proc *p){
    int sleep = p->tick_sleep;
    int run = p->tick_run;
    if(sleep > run){
        int score = (run)/(max(1,sleep/SCHED_INTERACT_MAX));
        return score > SCHED_INTERACT_MAX ? SCHED_INTERACT_MAX : score;
    }
    return SCHED_INTERACT_MAX;
}

void
computePriority(struct proc *p){
    #ifdef PART2
    p->prio = p->nice + 120;
    #elif PART3
    int score = is(p);
    if(score < SCHED_INTERACT_THRESH)
    {
        p->prio = PRIO_MIN_INTERACT + 
          ((PRIO_INTERACT_RANGE * score) / SCHED_INTERACT_THRESH);
    }
    else{
        p->prio = p->nice + 120;
    }
    #endif
}

int
computeTimeSlice(void)
{
    if(sysload >= SCHED_SLICE_MIN_DIVISOR) return SCHED_SLICE_MIN;
    if(sysload < 1) return SCHED_SLICE_DEFAULT;
    return SCHED_SLICE_DEFAULT / sysload;
}

int compareProc(struct proc* a, struct proc* b){
    // if(a->prio > b->prio) return -1;
    // else if(a->prio < b->prio) return 1;
    // else if(a->etick < b->etick) return 1;
    // return -1;
    if(a->prio > b->prio) return -1;
    return 1;
}

void insertProc(struct procHeap* h, struct proc* p) {
    if (h->size >= NPROC) {
        panic("procHeap is full\n");
        return;
    }

    computePriority(p);  
    // p->etick = r_time();

    h->data[h->size] = p;
    int i = h->size++;
    
    while (i > 0 && compareProc(h->data[(i - 1) / 2], h->data[i]) < 0) {
        struct proc* temp = h->data[i];
        h->data[i] = h->data[(i - 1) / 2];
        h->data[(i - 1) / 2] = temp;
        i = (i - 1) / 2;
    }
}

struct proc* priorityMax(struct procHeap* h) {
    if (h->size == 0) {
        return (struct proc*)-1;
    }

    struct proc* max_proc = h->data[0];
    h->data[0] = h->data[--(h->size)];
    
    int i = 0;
    while (2 * i + 1 < h->size) {
        int left = 2 * i + 1;
        int right = 2 * i + 2;
        int largest = i;

        if (left < h->size && compareProc(h->data[left], h->data[largest]) > 0) {
            largest = left;
        }

        if (right < h->size && compareProc(h->data[right], h->data[largest]) > 0) {
            largest = right;
        }

        if (largest != i) {
            struct proc* temp = h->data[i];
            h->data[i] = h->data[largest];
            h->data[largest] = temp;
            i = largest;
        } else {
            break;
        }
    }
    return max_proc;
}

struct procHeap queue1;
struct procHeap queue2;

struct procHeap* currentQueue;
struct procHeap* nextQueue;


void
initHeap(void)
{
    queue1.size=0;
    queue2.size=0;
    currentQueue = &queue1;
    nextQueue = &queue2;
}





#endif
