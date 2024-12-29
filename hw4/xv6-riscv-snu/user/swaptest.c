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

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define N     64 
#define M     1024
#define R     64
#define LOOP  10


char dummy[4096-16] = "swaptest";
int a[N][M];                 // &a[0][0] = 0x2000

int 
stress_test(void)
{
  int i, j, k, r;
  int sum = 0;
  int ok = 1;

  for (k = 0; k < LOOP; k++) {
    printf("k = %d\n", k);
    for (r = 0; r < M; r += R) 
      for (i = 0; i < N; i++) 
        for (j = 0; j < R; j++) 
          a[i][r+j]++;
	}

  for (i = 0; i < N; i++)
    for (j = 0; j < M; j++) {
      if (a[i][j] != LOOP)
      {
        printf("a[%d][%d] = %d\n", i, j, a[i][j]);
        ok = 0;
      }
      sum += a[i][j];
    }
  printf("%s: %s\n", dummy, (ok)? "OK" : "WRONG");
  return sum;
}

int
main(void)
{
  int x;
  int n;

  if ((uint64) a & 0xfffUL)
  {
    printf("a[][] is not aligned to page boundary. (a=%lx)\n", (uint64) a);
    exit(0);
  }

  n = memstat(0, 0, 0, 0, 0);
  printf("Allocated frames (start): %d\n", n);
  sleep(1);
  x =  stress_test();

  n = memstat(0, 0, 0, 0, 0);
  printf("Allocated frames (end): %d\n", n);
  exit(x);
}
