//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"



// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.

extern int isFull;
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  argint(n, &fd);
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;
  
  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  argaddr(1, &st);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char new[MAXPATH], old[MAXPATH];
  char oldPath[MAXPATH], newPath[MAXPATH];
  struct inode *dp, *ip, *root;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  resolve_absolute_path(old, oldPath);
  resolve_absolute_path(new, newPath);

  if(namecmp(oldPath, newPath) == 0){
    end_op();
    return -1;
  }

  if((ip = namei(oldPath)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);
  
  if((dp = nameiparent(newPath)) == 0)
    goto bad;
  ilock(dp);
  if(dp->type != T_DIR){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  
  root = get_root_inode();
  ilock(root);
  if(dirlink(root, newPath, ip->inum) < 0){
    iunlockput(root);
    goto bad;
  }

  iunlockput(root);

  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp, char *path)
{
  int off;
  struct dirent de;
  
  for(off=0; off < dp->size; off += sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    int len = strlen(path);
    if (strncmp(de.name, path, len) == 0) {
        if (de.name[len] == '/') {
            return 0;
        }
    }
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *root;
  struct dirent de;
  char fullPath[MAXPATH], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;
  resolve_absolute_path(path, fullPath);
  if(namecmp(fullPath, "/") == 0){
    return -1;
  }

  begin_op();
  root = get_root_inode();

  ilock(root);

  if((ip = dirlookup(root, fullPath, &off)) == 0)
    goto bad;
  ilock(ip);
  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(root, fullPath)){
    iunlockput(ip);
    goto bad;
  }
  
  memset(&de, 0, sizeof(de));
  if(writei(root, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  iunlockput(root);

  isFull = 0;
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  
  end_op();
  hash_invalidate(fullPath);
  return 0;

bad:
  iunlockput(root);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp, *root;
  char fullPath[MAXPATH];

  resolve_absolute_path(path, fullPath);
  if((dp = nameiparent(fullPath)) == 0)
  {
    return 0;
  }
  ilock(dp);
  if(dp->type != T_DIR){
    iunlockput(dp);
    return 0;
  }
  iunlockput(dp);
  
  root = get_root_inode();

  ilock(root);
  
  if((ip = dirlookup(root, fullPath, 0)) != 0){
    iunlockput(root);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(ROOTDEV, type)) == 0){
    iunlockput(root);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(dirlink(root, fullPath, ip->inum) < 0){
    iunlockput(root);
    goto fail;
  }

  iunlockput(root);
  
  return ip;

 fail:
  // something went wrong. de-allocate ip.
  ip->nlink = 0;
  iupdate(ip);
  iunlockput(ip); 
  iunlockput(root);
  return 0;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  char fullPath[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;
  #ifdef PROFILE
  uint64 start_time, end_time;
  uint64 total_start;
  #endif

  #ifdef PROFILE
  start_time = r_time();
  total_start = start_time;
  printf("\n\n#####");
  #endif

  argint(1, &omode);
  if((n = argstr(0, path, MAXPATH)) < 0)
    return -1;
  
  #ifdef PROFILE
  end_time = r_time();
  printf("argstr: %ld\n", end_time - start_time);
  start_time = end_time;
  #endif
  

  resolve_absolute_path(path, fullPath);

  // #ifdef PROFILE
  // end_time = r_time();
  // printf("resolve_absolute_path: %ld\n", end_time - start_time);
  // start_time = end_time;
  // #endif

  begin_op();

  if(omode & O_CREATE){
    ip = create(fullPath, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(fullPath)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  #ifdef PROFILE
  end_time = r_time();
  printf("resolve + namei: %ld\n", end_time - start_time);
  start_time = end_time;
  #endif

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }
  // #ifdef PROFILE
  // end_time = r_time();
  // printf("isdevice: %ld\n", end_time - start_time);
  // start_time = end_time;
  // #endif

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  // #ifdef PROFILE
  // end_time = r_time();
  // printf("filealloc: %ld\n", end_time - start_time);
  // start_time = end_time;
  // #endif

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }
  // #ifdef PROFILE
  // end_time = r_time();
  // printf("itrunc: %ld\n", end_time - start_time);
  // start_time = end_time;
  // #endif

  iunlock(ip);
  // #ifdef PROFILE
  // end_time = r_time();
  // printf("iunlock: %ld\n", end_time - start_time);
  // start_time = end_time;
  // #endif

  end_op();
  #ifdef PROFILE
  end_time = r_time();
  printf("end_op: %ld\n", end_time - start_time);
  start_time = end_time;
  #endif

  #ifdef PROFILE
  end_time = r_time();
  printf("total: %ld\n", end_time - total_start);
  printf("#####\n\n");
  #endif

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  argint(1, &major);
  argint(2, &minor);
  if((argstr(0, path, MAXPATH)) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  char fullPath[MAXPATH];

  begin_op();
  if(argstr(0, path, MAXPATH) < 0){
    end_op();
    return -1;
  }
  resolve_absolute_path(path, fullPath);
  if((ip = namei(fullPath)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  #ifdef SNU
  strncpy(p->pwd, fullPath, MAXPATH);
  #endif
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  char fullPath[MAXPATH];
  int i;
  uint64 uargv, uarg;

  argaddr(1, &uargv);
  if(argstr(0, path, MAXPATH) < 0) {
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  resolve_absolute_path(path, fullPath);
  int ret = exec(fullPath, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  argaddr(0, &fdarray);
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

uint64
sys_pwd(void)
{

  // FILL HERE
  struct proc *p = myproc();
  uint64 va;
  argaddr(0, &va);
  if(va == 0)
    return -1;
  if(copyout(p->pagetable, va, (char*)p->pwd, MAXPATH) < 0)
    return -1;

  return 0;
}

