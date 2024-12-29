// Sleeping locks

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"

void
initsleeplock(struct sleeplock *lk, char *name)
{
  initlock(&lk->lk, "sleep lock");
  lk->name = name;
  lk->locked = 0;
  lk->pid = 0;

  lk->head = -1;
  lk->tail = -1;

  for(int i=0; i<NPROC; i++){
    lk->next[i] = -1;
  }
}

static void
enqueue(struct sleeplock *lk, int pid)
{
  if(lk->head < 0){
    lk->head = pid;
    lk->tail = pid;
    lk->next[pid] = -1;
  } else {
    lk->next[lk->tail] = pid;
    lk->tail = pid;
    lk->next[pid] = -1;
  }
}

static int
dequeue(struct sleeplock *lk)
{
  int pid = lk->head;
  if(pid < 0){
    return -1;
  }

  lk->head = lk->next[pid];
  lk->next[pid] = -1;

  if(lk->head < 0){
    lk->tail = -1;
  }
  return pid;
}

inline void
acquiresleep(struct sleeplock *lk)
{
  struct proc *p = myproc();
  acquire(&lk->lk);
  while (lk->locked) {
    int index = proc_to_index(p);
    enqueue(lk, index);
    sleep(lk, &lk->lk);
  }
  lk->locked = 1;
  lk->pid = p->pid;
  
  release(&lk->lk);
}
extern struct proc proc[];
inline void
releasesleep(struct sleeplock *lk)
{
  // #ifdef PROFILE
  // uint64 start_time, end_time;
  // start_time = r_time();
  // #endif
  acquire(&lk->lk);
  lk->locked = 0;
  lk->pid = 0;
  
  // wakeup(lk);
  int index = dequeue(lk);
  if(index >= 0){
    struct proc *p = &proc[index];
    acquire(&p->lock);
    if(p->state == SLEEPING && p->chan == lk) {
      p->state = RUNNABLE;
    }
    release(&p->lock);
  }
  
  release(&lk->lk);
  // #ifdef PROFILE
  // end_time = r_time();
  // printf("        wakeup: %ld\n", end_time - start_time);
  // #endif
}

int
holdingsleep(struct sleeplock *lk)
{
  return 1;
  // int r;
  
  // acquire(&lk->lk);
  // r = lk->locked && (lk->pid == myproc()->pid);
  // release(&lk->lk);
  // return r;
}



