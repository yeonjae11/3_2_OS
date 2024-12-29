#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

// start() jumps here in supervisor mode on all CPUs.
void
main()
{
  if(cpuid() == 0){
    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\nSNUOS2024\n");
    // printf("\033[31m2\033[0m");    // 빨간색
    // printf("\033[32m0\033[0m");    // 초록색
    // printf("\033[33m2\033[0m");    // 노랑색
    // printf("\033[34m0\033[0m");    // 파란색
    // printf("\033[35m-\033[0m");    // 보라색
    // printf("\033[36m1\033[0m");    // 청록색
    // printf("\033[31m5\033[0m");    // 빨간색
    // printf("\033[32m6\033[0m");    // 초록색
    // printf("\033[33m0\033[0m");    // 노랑색
    // printf("\033[34m7\033[0m");    // 파란색
    printf("\n"); 
    printf("김김김김   김김김김   김김김김   김김김김                                  김     김김김김   김김김김     김김김김   김김김김\n");
    printf("연    연  연      연  연    연  연      연                                 연     연         연          연      연        연\n");
    printf("    연    연      연       연   연      연                                 연     연         연          연      연        연\n");
    printf("   연     연      연      연    연      연        \033[35m김연재\033[32m김연재\033[34m김연재\033[0m       연     연연연연   연연연연    연      연        연 \n");
    printf("  연      연      연     연     연      연                                 연           연   연    연    연      연        연\n");
    printf(" 연       연      연    연      연      연                                 연           연   연    연    연      연        연\n");
    printf("재재재재   재재재재   재재재재   재재재재                                  재     재재재재   재재재재     재재재재         재 \n");
    // printf("\n\n\n김연재김연재김연재\n\n\n\n\n");
    // printf("    김     김김김김   김김김김     김김김김   김김김김\n");
    // printf("    연     연         연          연      연        연\n");
    // printf("    재     연         연연연연    연      연        연\n");
    // printf("    김     연연연연   연    연    연      연        연\n");
    // printf("    연           연   연    연    연      연        연\n");
    // printf("    재     재재재재   재재재재     재재재재         재\n");
    
    printf("\n");
    kinit();         // physical page allocator
    kvminit();       // create kernel page table
    kvminithart();   // turn on paging
    procinit();      // process table
    trapinit();      // trap vectors
    trapinithart();  // install kernel trap vector
    plicinit();      // set up interrupt controller
    plicinithart();  // ask PLIC for device interrupts
    binit();         // buffer cache
    iinit();         // inode table
    fileinit();      // file table
    virtio_disk_init(); // emulated hard disk
    userinit();      // first user process
    __sync_synchronize();
    started = 1;
  } else {
    while(started == 0)
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    plicinithart();   // ask PLIC for device interrupts
  }

  scheduler();        
}
