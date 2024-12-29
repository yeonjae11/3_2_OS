#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

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
extern int enterCount;
#ifdef SNU
uint64
sys_nenter(void)
{
  return enterCount;
}

uint64
sys_getpmpaddr(void) {
    int n;
    argint(0,&n);
    uint64 x;
    asm volatile("mv a0, %0" : : "r"(n));
    asm volatile("li a7, 1");
    asm volatile("ecall");
    asm volatile("mv %0, a0" :"=r"(x));
    
    return x;
}

uint64
sys_getpmpcfg(void)
{
  int n;
  argint(0,&n);
  asm volatile("mv a0, %0" : : "r"(n));
  asm volatile("li a7, 2");
  asm volatile("ecall");
  asm volatile("mv %0, a0" :"=r"(n));
  return n;
}
#endif