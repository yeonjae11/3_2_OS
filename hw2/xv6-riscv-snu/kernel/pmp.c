//----------------------------------------------------------------
//
//  4190.307 Operating Systems (Fall 2024)
//
//  Project #2: System calls
//
//  September 24, 2024
//
//  Jin-Soo Kim (jinsoo.kim@snu.ac.kr)
//  Systems Software & Architecture Laboratory
//  Dept. of Computer Science and Engineering
//  Seoul National University
//
//----------------------------------------------------------------

// PA2: DO NOT MODIFY THIS FILE

#ifdef SNU
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"

void
setpmp(void)
{
  // configure Physical Memory Protection to give supervisor mode
  // access to all of physical memory.
  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);
}
#endif
