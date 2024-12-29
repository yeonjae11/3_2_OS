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

  for (int i = 0; i < 100; i++)
    x += run_task(n);
  return x;
}

int
my_test(void)
{
    int x = 0;
    // for(int i =0 ;i<100; i++){
    //     x+= run_task(100);
    // }
    x+=do_nerdythings(260);
    sleep(6);
    x+=do_nerdythings(900);
    return x;
}

int
my_test2(void)
{
    
    int x = 0;
    for(int i =0 ;i<10; i++){
        x+= run_task(100);
    }
    sleep(7);
    x+=do_nerdythings(900);
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
      if(i==0){
        sum+= my_test();
      }
      else if(i==1){
        sum+=my_test2();
      }
      else{
        sum += do_nerdythings(240*(i+1));
      }
      exit(sum);
    }
  }

  // Parent becomes the lowest-priority process
  nice(40);
  for (int i = 0; i < NP; i++)
    wait(0);
  exit(0);
}