#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#ifdef PART3
#include "snule.h"
#endif

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  #ifdef PART3
  struct proc *p = myproc();
  p->start_sleep = ticks0;
  p->sleep_time = n;
  p->tick_sleep += (n << TICK_SHIFT);
  if(p->tick_run + p->tick_sleep > SCHED_SLP_RUN_MAX){
    p->tick_run /= 2;
    p->tick_sleep /= 2;
  }
  #endif
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

#ifdef SNU
uint64
sys_nice(void)
{
  // Part 2: FILL HERE
  struct proc *p = myproc();

  int n;
  argint(0,&n);

  n += p->nice;

  if(n>19) n = 19;
  if(n<-20) n = -20;

  p->nice = n;
  return n;
}
#endif
