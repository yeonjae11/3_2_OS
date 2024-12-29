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

#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "xswap.h"
// #include "lzo.c"

int nalloc4k, zalloc4k, zalloc2k;
int nswapin, nswapout;


uint64
sys_memstat()
{
  // FILL HERE
  uint64 va;
  struct proc *p = myproc();
  for(int i = 0 ; i < 5; i++){
    argaddr(i, &va);
    if(va != 0){
      switch(i){
        case 0: 
          copyout(p -> pagetable,va,(char*)&nalloc4k,4);
          break;
        case 1: 
          copyout(p -> pagetable,va,(char*)&zalloc4k,4);
          break;
        case 2: 
          copyout(p -> pagetable,va,(char*)&zalloc2k,4);
          break;
        case 3: 
          copyout(p -> pagetable,va,(char*)&nswapin,4);
          break;
        case 4: 
          copyout(p -> pagetable,va,(char*)&nswapout,4);
          break;
        default: break;
      }
    }
  }

  return nalloc4k + zalloc2k + zalloc4k;
}


// Called when ^x is pressed
void
mallocstat(void)
{
  printf("total: %d, nalloc4k: %d, zalloc4k: %d, zalloc2k: %d, swapin: %d, swapout: %d\n",
    nalloc4k+zalloc4k+zalloc2k, nalloc4k, zalloc4k, zalloc2k, nswapin, nswapout);
}
#ifdef PART3
struct spinlock lzo_lock;
#endif

//외부 함수
void enqueue(uint64);
struct run* dequeue(void);

int lzo1x_compress(const unsigned char *src, uint32 src_len, unsigned char *dst, uint32 *dst_len, void *wrkmem);
int lzo1x_decompress(const unsigned char *src, uint32 src_len, unsigned char *dst, uint32 *dst_len);
/////////


int pa2idx_normal(uint64 pa) {
  return (pa - NORMAL_START) / PGSIZE;
}

int pa2idx_zmem(uint64 pa) {
  return (pa - PHYSTOP) / HPGSIZE;
}

#define NUM_HPAGES ((ZMEMSTOP - PHYSTOP) / HPGSIZE)
int cp_length[NUM_HPAGES];
char zmem_page_allocated[NUM_HPAGES];

struct run {
  struct run *next;
};

struct inverted_pte {
  pagetable_t pagetable;
  uint64 va;
};

int zfreestart = 0;

struct spinlock ipt_lock;

#define NUM_PHYSPAGES ((PHYSTOP - NORMAL_START) / PGSIZE)
struct inverted_pte ipt[NUM_PHYSPAGES];

void update_ipt(uint64 pa, pagetable_t pagetable, uint64 va)
{
  int idx = pa2idx_normal(pa);
  if(idx < 0 || idx >= NUM_PHYSPAGES){
    panic("update_ipt: invalid index");
  }

  acquire(&ipt_lock);
  ipt[idx].pagetable = pagetable;
  ipt[idx].va = va;
  release(&ipt_lock);
}

void initAlloc(void){
  initlock(&ipt_lock, "ipt_lock");
  #ifdef PART3
  initlock(&lzo_lock, "lzo_lock");
  #endif
  nalloc4k = zalloc4k = zalloc2k = nswapin = nswapout = 0;
  memset(zmem_page_allocated,0,sizeof(zmem_page_allocated));
}

struct run* get_buddy(struct run* r, int size) {
  uint64 addr = (uint64)r;
  uint64 buddy = addr ^ size;
  return (struct run*)buddy;
}

struct {
  struct spinlock lock;
  struct run* freelist_2kb;
  struct run* freelist_4kb;
} zmem;

void init_zmem(void) {
  initlock(&zmem.lock, "zmem");
  zmem.freelist_2kb = 0;
  zmem.freelist_4kb = 0;
}

void zfreerange(void *pa_start, void *pa_end){
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    zfree(p, ZFULL);
  zfreestart = 1;
}

void* zalloc(int type) {
  struct run *r;

  acquire(&zmem.lock);
  if(type == ZHALF){
    if(zmem.freelist_2kb){
      r = zmem.freelist_2kb;
      zmem.freelist_2kb = r->next;
      zalloc2k++;
    } else if(zmem.freelist_4kb){
      r = zmem.freelist_4kb;
      zmem.freelist_4kb = r->next;
      zalloc2k++;
      struct run *buddy = (struct run*)((char*)r + HPGSIZE);

      buddy->next = zmem.freelist_2kb;
      zmem.freelist_2kb = buddy;
    } else {
      r = 0;
    }
    if(r){
      int idx = pa2idx_zmem((uint64)r);
      zmem_page_allocated[idx] = ZHALF;
    }
  } else {
    if(zmem.freelist_4kb){
      zalloc4k++;
      r = zmem.freelist_4kb;
      zmem.freelist_4kb = r->next;
    } else {
      r = 0;
    }
    if(r){
      int idx = pa2idx_zmem((uint64)r);
      zmem_page_allocated[idx] = ZFULL;
    }
  }
  release(&zmem.lock);

  return (void*)r;
}


void zfree(void* pa, int type){
  struct run* r; 
  int size = (type == ZHALF) ? HPGSIZE : PGSIZE;

  if(((uint64)pa % size) != 0 || (uint64)pa < PHYSTOP || (uint64)pa >= ZMEMSTOP)
    panic("zfree");

  acquire(&zmem.lock);
  int idx = pa2idx_zmem((uint64)pa);
  if(zfreestart && type != zmem_page_allocated[idx]){
    release(&zmem.lock);
    panic("zfree f");
  }
  zmem_page_allocated[idx] = 0;

  r = (struct run*) pa;

  if(type == ZHALF){
    struct run* buddy = get_buddy(r, HPGSIZE);
    struct run** prev = &zmem.freelist_2kb;
    struct run* cur = zmem.freelist_2kb;
    int merged = 0;
    int buddy_idx = pa2idx_zmem((uint64)buddy);
    if(zmem_page_allocated[buddy_idx] == 0){
      while(cur){
        if(cur == buddy){
          *prev = cur->next;

          struct run* block = (struct run*)((uint64)r & ~((uint64)(PGSIZE - 1)));
          block->next = zmem.freelist_4kb;
          zmem.freelist_4kb = block;
          merged = 1;
          break;
        }
        prev = &cur->next;
        cur = cur->next;
      }
    }
    if(!merged){
      r->next = zmem.freelist_2kb;
      zmem.freelist_2kb = r;
    }
    zalloc2k--;
    
  }
  else{
    zalloc4k--;
    r->next = zmem.freelist_4kb;
    zmem.freelist_4kb = r;
  }
  release(&zmem.lock);
}

void sfence_vma_page(uint64 va){
  asm volatile("sfence.vma %0" : : "r" (va) : "memory");
}

void* swapout(void)
{
  void* swap_pa;
  struct run* r = dequeue();
  if(!r) return 0;

  uint64 pa = (uint64)r;

  acquire(&ipt_lock);
  int idx = pa2idx_normal(pa);
  pagetable_t pagetable = ipt[idx].pagetable;
  uint64 va = ipt[idx].va;

  pte_t* pte = walk(pagetable, va, 0);
  if(pte == 0){
    return 0;
  }
  
  #ifdef PART3
  void* compress = kalloc(ZONE_FIXED);
  unsigned int length= 4096;
  
  acquire(&lzo_lock);
  
  lzo1x_compress((const unsigned char *)pa, PGSIZE , compress, &length, (void*) WRKMEM); 
  
  release(&lzo_lock);
  

  if(length <= HPGSIZE){

    swap_pa = zalloc(ZHALF);
    if(swap_pa == 0){
      kfree(compress,ZONE_FIXED);
      return 0;
    }
    memmove(swap_pa, compress, length);

    int h_idx = pa2idx_zmem((uint64)swap_pa);
    cp_length[h_idx] = length;

    swap_pa = (void*)((uint64)swap_pa << 1);

    *pte &= ~PTE_V;
    *pte |= PTE_S;
    *pte |= PTE_H;

    *pte = PTE_FLAGS(*pte) | PA2PTE(swap_pa);
  }
  else{
    swap_pa = zalloc(ZFULL);
    if(swap_pa == 0){
      kfree(compress,ZONE_FIXED);
      return 0;
    }
  
    memmove(swap_pa, compress, length);
    int h_idx = pa2idx_zmem((uint64)swap_pa);
    cp_length[h_idx] = length;

    *pte &= ~PTE_V;
    *pte |= PTE_S;
    *pte &= ~PTE_H;

    *pte = PTE_FLAGS(*pte) | PA2PTE(swap_pa);
  }
  
  kfree(compress,ZONE_FIXED);

  ipt[idx].pagetable = 0;
  ipt[idx].va = 0;

  sfence_vma_page(va);

  enqueue(pa);

  nswapout++;
  release(&ipt_lock);

  #else

  swap_pa = zalloc(ZFULL);
  if(swap_pa == 0){
    return 0;
  }
  memmove(swap_pa, (void*)pa, PGSIZE);

  *pte &= ~PTE_V;
  *pte |= PTE_S;

  *pte = PTE_FLAGS(*pte) | PA2PTE(swap_pa);

  ipt[idx].pagetable = 0;
  ipt[idx].va = 0;
  
  sfence_vma_page(va);
  enqueue(pa);

  nswapout++;
  release(&ipt_lock);
  #endif

  return (void*)pa;
}

void* swapin(pagetable_t pagetable, uint64 va){
  void *pa = kalloc(ZONE_NORMAL);
  if(pa == 0){
    panic("swapin: failed to allocate memory");
  }

  pte_t *pte = walk(pagetable, va, 0);
  if(pte == 0){
    panic("swapin: invalid page table entry");
  }

  #ifdef PART3
  uint64 swap_pa = PTE2PA(*pte);
  int size;
  if(*pte & PTE_H){
    // size = HPGSIZE;
    swap_pa = swap_pa >> 1;
  }
  else{
    // size = PGSIZE;
  }
  int h_idx = pa2idx_zmem((uint64)swap_pa);
  size = cp_length[h_idx];
  void* decompress = kalloc(ZONE_FIXED);
  unsigned int length = 4096;
  int error = lzo1x_decompress((const unsigned char *)swap_pa, size, decompress, &length);
  if(error < 0){
    printf("error: %d size: %d ",error,size);
    panic("decompress error");
  }
  memmove(pa, decompress, PGSIZE);
  kfree(decompress, ZONE_FIXED);
  int isHalf = (*pte) & PTE_H;
  *pte = PTE_FLAGS(*pte) | PA2PTE(pa);
  *pte |= PTE_V;
  *pte &= ~PTE_S;
  *pte &= ~PTE_H;

  sfence_vma_page(va);

  if(isHalf){
    zfree((void*)swap_pa, ZHALF);
  }
  else{
    zfree((void*)swap_pa, ZFULL);
  }
  update_ipt((uint64)pa,pagetable,va);
  nswapin++;
  #else
  uint64 swap_pa = PTE2PA(*pte);
  memmove(pa, (void*)swap_pa, PGSIZE);

  *pte = PTE_FLAGS(*pte) | PA2PTE(pa);
  *pte |= PTE_V;
  *pte &= ~PTE_S;

  sfence_vma_page(va);
  zfree((void*)swap_pa, ZFULL);
  update_ipt((uint64)pa,pagetable,va);
  nswapin++;
  #endif
  return pa;
  
}