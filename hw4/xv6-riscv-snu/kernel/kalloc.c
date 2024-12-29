// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "xswap.h"
#include <stddef.h>


void freerange(void *pa_start, void *pa_end, int zone);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

int kfreestart = 0;
#define FIXED_MEM_PAGES ((NORMAL_START - KERNBASE) / PGSIZE)
char fixed_page_allocated[FIXED_MEM_PAGES];

int
pa2idx_fixed(uint64 pa) {
  return ((pa - KERNBASE) / PGSIZE);
}


struct run {
  struct run *next;
};

struct page_info {
  int next;
  int prev;
  int in_queue;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem_normal;

struct spinlock* a_lock = &kmem_normal.lock;

void acquire_normal_lock(){
  
  if(!holding(&kmem_normal.lock))
  {
    acquire(&kmem_normal.lock);
  }
    
  
}

void release_normal_lock(){
  
  release(&kmem_normal.lock);
  
}

struct {
  int head;
  int tail;
  struct page_info pages[MEM];
} kmem_fifo;

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem_fixed;

void init_fifo(){
  for(int i = 0; i<MEM ;i++){
    kmem_fifo.pages[i].next = -1;
    kmem_fifo.pages[i].prev = -1;
    kmem_fifo.pages[i].in_queue = 0;
  }
  kmem_fifo.head = -1;
  kmem_fifo.tail = -1;
}

int
pa2idx_fifo(uint64 pa) {
  if (pa < NORMAL_START || pa >= NORMAL_START + MEM * PGSIZE)
    return -1;
  return (pa - NORMAL_START) / PGSIZE;
}

uint64
idx2pa_fifo(int idx) {
  if (idx < 0 || idx >= MEM)
    return -1;
  return NORMAL_START + idx * PGSIZE;
}

void
enqueue(uint64 pa) {
  int idx = pa2idx_fifo(pa);
  if (idx == -1)
    panic("enqueue: invalid pa");


  if (kmem_fifo.pages[idx].in_queue) {
    return;
  }

  kmem_fifo.pages[idx].in_queue = 1;
  kmem_fifo.pages[idx].next = -1;
  kmem_fifo.pages[idx].prev = kmem_fifo.tail;

  if (kmem_fifo.tail != -1)
    kmem_fifo.pages[kmem_fifo.tail].next = idx;

  kmem_fifo.tail = idx;

  if (kmem_fifo.head == -1)
    kmem_fifo.head = idx;

}

uint64
dequeue(void) {

  if (kmem_fifo.head == -1) {
    return 0;
  }

  int idx = kmem_fifo.head;
  uint64 pa = idx2pa_fifo(idx);

  kmem_fifo.head = kmem_fifo.pages[idx].next;
  if (kmem_fifo.head != -1)
    kmem_fifo.pages[kmem_fifo.head].prev = -1;
  else
    kmem_fifo.tail = -1;

  kmem_fifo.pages[idx].next = -1;
  kmem_fifo.pages[idx].prev = -1;
  kmem_fifo.pages[idx].in_queue = 0;

  return pa;
}

int 
delete(uint64 pa) {
  int idx = pa2idx_normal(pa);
  if (idx == -1)
    return -1;


  if (!kmem_fifo.pages[idx].in_queue) {
    return -1;
  }

  int prev_idx = kmem_fifo.pages[idx].prev;
  int next_idx = kmem_fifo.pages[idx].next;

  if (prev_idx != -1)
    kmem_fifo.pages[prev_idx].next = next_idx;
  else
    kmem_fifo.head = next_idx;

  if (next_idx != -1)
    kmem_fifo.pages[next_idx].prev = prev_idx;
  else
    kmem_fifo.tail = prev_idx;

  kmem_fifo.pages[idx].next = -1;
  kmem_fifo.pages[idx].prev = -1;
  kmem_fifo.pages[idx].in_queue = 0;

  return 0;
}

void
kinit()
{
  printf("Physical memory layout:\n");
  printf("Kernel:      0x%lx - 0x%lx (%d MB, %d pages)\n",
      (uint64) KERNBASE, (uint64) PGROUNDUP((uint64)end),
      (int) ((PGROUNDUP((uint64)end) - KERNBASE) >> 20),
      (int) ((PGROUNDUP((uint64)end) - KERNBASE) >> 12));
  printf("ZONE_FIXED:  0x%lx - 0x%lx (%d MB, %d pages)\n",
      (uint64) PGROUNDUP((uint64)end), (uint64) NORMAL_START,
      (int) ((NORMAL_START - PGROUNDUP((uint64)end)) >> 20),
      (int) ((NORMAL_START - PGROUNDUP((uint64)end)) >> 12));
  printf("ZONE_NORMAL: 0x%lx - 0x%lx (%d MB, %d pages)\n",
      (uint64) NORMAL_START, (uint64) PHYSTOP,
      (int) ((PHYSTOP - NORMAL_START) >> 20),
      (int) ((PHYSTOP - NORMAL_START) >> 12));
  printf("ZONE_ZMEM:   0x%lx - 0x%lx (%d MB, %d pages)\n",
      (uint64) PHYSTOP, (uint64) ZMEMSTOP,
      (int) ((ZMEMSTOP - PHYSTOP) >> 20),
      (int) ((ZMEMSTOP - PHYSTOP) >> 12));
      initlock(&kmem_fixed.lock, "kmem_fixed");
      initlock(&kmem_normal.lock, "kmem_normal");
      init_fifo();

  // ZONE_NORMAL, ZONE_ZMEM should be initialized separately
  #ifdef PART3
  freerange(end, (void*)WRKMEM, ZONE_FIXED);
  #else
  freerange(end, (void*)NORMAL_START, ZONE_FIXED);
  #endif
  freerange((void*)NORMAL_START, (void*)PHYSTOP, ZONE_NORMAL);
  kfreestart = 1;
  init_zmem();
  zfreerange((void*)PHYSTOP, (void*)ZMEMSTOP);
  initAlloc();
}

void
freerange(void *pa_start, void *pa_end, int zone)
{
  char *p;
  if(zone == ZONE_FIXED){
    p = (char*)PGROUNDUP((uint64)pa_start);
    for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
      kfree(p, ZONE_FIXED);
  }
  if(zone == ZONE_NORMAL){
    p = (char*)PGROUNDUP((uint64)pa_start);
    for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
      kfree(p, ZONE_NORMAL);
    release_normal_lock();
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa, int zone)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP || (zone != ZONE_FIXED && zone != ZONE_NORMAL)){
    panic("kfree");
  }

  if(zone == ZONE_FIXED && (uint64)pa >= NORMAL_START) 
    panic("kfree");
  else if(zone == ZONE_NORMAL && (uint64)pa < NORMAL_START) 
    panic("kfree");

  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  if(zone == ZONE_FIXED){
    acquire(&kmem_fixed.lock);
    int idx = pa2idx_fixed((uint64)pa);
    if(kfreestart && fixed_page_allocated[idx] == 0){
      release(&kmem_fixed.lock);
      panic("kfree");
    }
    fixed_page_allocated[idx] = 0;
    r->next = kmem_fixed.freelist;
    kmem_fixed.freelist = r;
    release(&kmem_fixed.lock);
  }
  else if(zone == ZONE_NORMAL){
    acquire_normal_lock();
    if(kfreestart && delete((uint64)r) < 0){
      panic("kfree");
    }

    nalloc4k--;

    r->next = kmem_normal.freelist;
    kmem_normal.freelist = r;
  }
  
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(int zone)
{
  struct run *r;
  if(zone == ZONE_FIXED){
    acquire(&kmem_fixed.lock);
    r = kmem_fixed.freelist;
    if(r)
      kmem_fixed.freelist = r->next;

    if(r)
    {
      int idx = pa2idx_fixed((uint64)r);
      fixed_page_allocated[idx] = 1;
      memset((char*)r, 5, PGSIZE); // fill with junk
    }
    release(&kmem_fixed.lock);
    return (void*)r;
  }
  else if(zone == ZONE_NORMAL){
    acquire_normal_lock();
    r = kmem_normal.freelist;
    if(r){
      nalloc4k++;
      kmem_normal.freelist = r->next;
    }
    else{
      r = (struct run*) swapout();
    }
    
    if(r)
    {
      memset((char*)r, 5, PGSIZE);
      enqueue((uint64)r);
    }
      
    return (void*)r;
  }
  else{
    return 0;
  }
}
