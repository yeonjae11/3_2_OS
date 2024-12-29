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
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"


#define N       150
#define NOPEN   5
#define STRIDE  5

char name[N][MAXPATH];
char *dirs[] = {"fstests", "snu", "operating", "systems", "2024", "fall", "project5", 0 };

char *
strcat(char *s, char *t)
{
  char *l = s + strlen(s);
  while ((*l++ = *t++));
  return s;
}

void
make_dirs()
{
  int fd;
  char **d = dirs;
  char path[MAXPATH];

  path[0] = 0;
  while (*d) {
    strcat(path, "/");
    strcat(path, *d);
    if ((fd = open(path, O_RDONLY)) >= 0)
      close(fd);
    else if (mkdir(path) < 0) {
      printf("creating directory %s failed", path);
      exit(1);
    }
    d++;
  }
}

void
make_files()
{
  int i, fd;

  for (i = 0; i < N; i++) {
    if ((fd = open(name[i], O_RDONLY)) < 0) {
      if ((fd = open(name[i], O_CREATE) < 0)) {
        printf("creating file %s failed\n", name[i]);
        exit(1);
      }
    }
    close(fd);
  }
}

void 
delete_dir(char *path, char **dir)
{
  char p[MAXPATH]; 

  if (*dir == 0) 
    return; 

  strcpy(p, path);
  strcat(p, "/");
  strcat(p, *dir);
  delete_dir(p, ++dir);

  if (unlink(p) != 0) {
    printf("deleting directory %s failed\n", p);
    return;
  }
}

void 
fsdelete()
{
  int i;

  printf("deleting files...\n");
  for (i = 0; i < N; i++) {
    if (unlink(name[i]) != 0) {
      printf("deleting file %s failed\n", name[i]);
      continue;
    }
  }

  delete_dir("", dirs);
}

void
gen_pathnames()
{
  char path[MAXPATH];
  char **d = dirs;

  path[0] = 0;
  while (*d) {
    strcat(path, "/");
    strcat(path, *d++);
  }

  for (int i = 0; i < N; i++) {
    char *prefix = "/helloworld_";
    int k;

    strcpy(name[i], path);
    strcat(name[i], prefix);
    k = strlen(name[i]);
    name[i][k]   = '0' + (i / 64);
    name[i][k+1] = '0' + (i % 64);
    name[i][k+2] = '\0';
  }
}

void
fscreate()
{
  printf("creating files...\n");
  make_dirs();
  make_files();
}

void
fsopen()
{
  int i, k, fd;

  for (k = 0; k < NOPEN; k++) {
    for (i = 0; i < N; i += STRIDE) {
      if ((fd = open(name[i], O_RDONLY)) < 0) {
        printf("file %s open failed\n", name[i]);
        exit(1);
      }
      close(fd);
    }
  }
}

void
main(int argc, char *argv[])
{
  uint64 start, end;

  if (argc != 2) {
    printf("Usage: %s [[c]reate|[o]pen|[d]elete]\n", argv[0]);
    exit(1);
  }

  gen_pathnames();
  if (argv[1][0] == 'o') {
    printf("opening files...\n");
    sleep(100);
    fsopen();
    start = rdtime();
    fsopen();
    end = rdtime();
    printf("open: %ld cycles/file\n", 
      (end - start)/((N/STRIDE) * NOPEN));
  }
  else if (argv[1][0] == 'c')
    fscreate();
  else if (argv[1][0] == 'd')
    fsdelete();
}
