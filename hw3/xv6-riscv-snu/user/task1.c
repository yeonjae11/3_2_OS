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

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define NP          4

uint64 
simpleHash(uint64 x)
{
  for (int i = 0; i < 100000; i++)
  {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
  }
  return x;
}

uint64 
run_task(int n)
{
  uint64 x = 0;

  for (int i = 0; i < n; i++)
    x += simpleHash(i);
  return x;
}

int
do_nerdythings(int n)
{
  int x = 0;

  for (int i = 0; i < 10; i++)
    x += run_task(n);
  return x;
}

int
main(int argc, char *argv[])
{
  int sum = 0;

  sleep(1);
  for (int i = 0; i < NP; i++)
  {
    // Increment the nice value by one
    nice(1);
    if (fork() == 0)
    {
      sum += do_nerdythings(240*(i+1));
      exit(sum);
    }
  }

  // Parent becomes the lowest-priority process
  nice(40);
  for (int i = 0; i < NP; i++)
    wait(0);
  exit(0);
}

