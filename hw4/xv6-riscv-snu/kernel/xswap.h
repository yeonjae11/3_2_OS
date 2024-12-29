//----------------------------------------------------------------
//
//  4190.307 Operating Systems (Fall 2024)
//
//  Project #4: xSwap: Compressed Swap for xv6
//
//  November 7, 2024
//
//  Jin-Soo Kim (jinsoo.kim@snu.ac.kr)
//  Systems Software & Architecture Laboratory
//  Dept. of Computer Science and Engineering
//  Seoul National University
//
//----------------------------------------------------------------

// Memory zones
#include "memlayout.h"

#define ZONE_FIXED  0
#define ZONE_NORMAL 1
#define ZONE_ZMEM   2

// Types for zalloc()/zfree()
#define ZHALF       2
#define ZFULL       4


// LZO compression library
#define LZO1X_1_MEM_COMPRESS      (16*1024)



extern int nalloc4k, zalloc4k, zalloc2k;
extern int nswapin, nswapout;

void zfreerange(void *pa_start, void *pa_end);
void* zalloc(int type);
void zfree(void* pa, int type);
void mallocstat(void);
void initAlloc(void);
void* swapout(void);
void* swapin(pagetable_t, uint64 va);
void update_ipt(uint64 pa, pagetable_t pagetable, uint64 va);

int pa2idx_normal(uint64 pa);
void init_zmem(void);
int pa2idx_zmem(uint64 pa);
void acquire_ipt_lock(uint64 pa);
void release_ipt_lock(uint64 pa);
int holding_pa(uint64 pa);
extern struct spinlock ipt_lock;
