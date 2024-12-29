//----------------------------------------------------------------
//
//  4190.307 Operating Systems (Fall 2024)
//
//  Project #5: FullFS: A File System with Full-Path Indexing
//
//  December 3, 2024
//
//  Jin-Soo Kim (jinsoo.kim@snu.ac.kr)
//  Systems Software & Architecture Laboratory
//  Dept. of Computer Science and Engineering
//  Seoul National University
//
//----------------------------------------------------------------

#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

char buf[MAXPATH];

int
main(int argc, char *argv[])
{
  if (pwd(buf) < 0)
    printf("pwd() failed\n");
  else
    printf("%s\n", buf);
  exit(0);
}
