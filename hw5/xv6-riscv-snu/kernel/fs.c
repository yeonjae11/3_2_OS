// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Log: crash recovery for multi-step updates.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
//
// This file contains the low-level file system manipulation
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
// there should be one superblock per disk device, but we run with
// only one device

// #define HASH_SIZE 20000
// #define HASH_COUNT 4
// #define SET_ASSOCIATIVE 4

// struct {
//   struct dirent table[HASH_COUNT][HASH_SIZE * SET_ASSOCIATIVE];
//   struct spinlock lock[HASH_COUNT];
// } hash_table;

// void init_hash_table() {
//   for (int i = 0; i < HASH_COUNT; i++) {
//     initlock(&hash_table.lock[i], "hash_lock");
//     for (int j = 0; j < HASH_SIZE * SET_ASSOCIATIVE; j++) {
//       hash_table.table[i][j].inum  = 0;
//       hash_table.table[i][j].valid = 0;
//       hash_table.table[i][j].count = 0;
//       hash_table.table[i][j].off   = 0;
//       memset(hash_table.table[i][j].name, 0, sizeof(hash_table.table[i][j].name));
//     }
//   }
// }

// int hash_function(char *path, int* bucket) {
//   char *p;
//   int count = 0;
//   unsigned int hash = 2166136261u;
//   p = path;
//   while(*p){
//     if(*p == '/'){
//       count++;
//     }
//     hash ^= (unsigned char)(*p);
//     hash *= 16777619u;
//     p++;
//   }
//   count--;
//   *bucket = count > HASH_COUNT - 1 ? HASH_COUNT - 1 : count;
//   hash = hash % HASH_SIZE;
//   if(hash < 0 || hash >= HASH_SIZE)
//     panic("hash function error");
//   if(*bucket < 0 || *bucket >= HASH_COUNT)
//     panic("bucket error");
//   return (int)(hash * SET_ASSOCIATIVE);
// }

// struct dirent* hash_lookup(char *path) {
//   int bucket, idx;
//   struct dirent *entry;
  
//   idx = hash_function(path, &bucket);
//   acquire(&hash_table.lock[bucket]);

//   for (int i = 0; i < SET_ASSOCIATIVE; i++) {
//     entry = &hash_table.table[bucket][idx + i];
//     if (entry->valid && strncmp(entry->name, path, MAXPATH) == 0) {
//       entry->count++;
//       release(&hash_table.lock[bucket]);
//       return entry;
//     }
//   }

//   release(&hash_table.lock[bucket]);
//   return 0;
// }

// void
// hash_insert(char *path, ushort inum, uint off)
// {
//   int bucket, idx;
//   idx = hash_function(path, &bucket);
//   acquire(&hash_table.lock[bucket]);

//   struct dirent *entries = hash_table.table[bucket];

//   struct dirent *victim = 0;
//   int min_count = 0x7fffffff;

//   for (int i = 0; i < SET_ASSOCIATIVE; i++) {
//     struct dirent *ent = &entries[idx + i];

//     if (!ent->valid) {
//       victim = ent;
//       break;
//     }

//     if (ent->count < min_count) {
//       min_count = ent->count;
//       victim = ent;
//     }
//   }

//   if (victim) {
//     victim->valid = 1;
//     victim->inum  = inum;
//     victim->count = 1;
//     victim->off   = off;
//     safestrcpy(victim->name, path, MAXPATH);
//   }

//   release(&hash_table.lock[bucket]);
// }


// void hash_invalidate(char *path) {
//   int bucket, idx;
//   struct dirent *entry;

//   idx = hash_function(path, &bucket);
//   acquire(&hash_table.lock[bucket]);

//   for (int i = 0; i < SET_ASSOCIATIVE; i++) {
//     entry = &hash_table.table[bucket][idx + i];
//     if (entry->valid && strncmp(entry->name, path, MAXPATH) == 0) {
//       entry->valid = 0;
//       break;
//     }
//   }

//   release(&hash_table.lock[bucket]);
// }

#define EMPTY 0
#define OCCUPIED 1
#define DELETED 2

#define HASH_SIZE  20000
void
hash_insert(char *path, ushort inum, uint off);

struct {
  struct dirent table[HASH_SIZE];
  struct spinlock lock;
} hash_table;

static unsigned int
primary_hash(char *path)
{
  unsigned int h = 2166136261u; 
  while(*path){
    h ^= (unsigned char)(*path);
    h *= 16777619u;
    path+=2;
  }
  return h; 
}

// static unsigned int
// secondary_hash(unsigned int h)
// {
//   unsigned int h2 = ( (h >> 16) & 0x7FFF ) | 1;  
//   return h2;
// }

static unsigned int
secondary_hash(unsigned int h)
{
  unsigned int x = (h >> 16) ^ (h & 0xffff);

  x *= 0x9e3779b1u;

  x ^= x >> 16;
  x |= 1;

  if (x == 0) {
    x = 1;
  }

  return x;
}

int isFull;

void
init_hash_table()
{
  initlock(&hash_table.lock, "hash_lock");
  memset(hash_table.table, 0, sizeof(hash_table.table));
  isFull = 0;
}

struct dirent*
hash_lookup(char *path)
{
  unsigned int h = primary_hash(path);

  unsigned int h1 = h % HASH_SIZE;
  unsigned int h2 = secondary_hash(h) % HASH_SIZE;
  if(h2 == 0)
    h2 = 1;

  acquire(&hash_table.lock);
  struct dirent *entries = hash_table.table;

  for (int i = 0; i < HASH_SIZE; i++) {
    int probe = (h1 + i * h2) % HASH_SIZE;
    struct dirent *ent = &entries[probe];

    if (ent->valid == EMPTY) {
      release(&hash_table.lock);
      return 0;
    }
    if (ent->valid == OCCUPIED && strncmp(ent->name, path, MAXPATH) == 0) {
      ent->count++;
      release(&hash_table.lock);
      return ent;
    }
  }

  release(&hash_table.lock);
  return 0;
}

void
hash_insert(char *path, ushort inum, uint off)
{
  unsigned int h = primary_hash(path);

  unsigned int h1 = h % HASH_SIZE;
  unsigned int h2 = secondary_hash(h) % HASH_SIZE;
  if(h2 == 0)
    h2 = 1;

  acquire(&hash_table.lock);
  struct dirent *entries = hash_table.table;

  for (int i = 0; i < HASH_SIZE; i++) {
    int probe = (h1 + i * h2) % HASH_SIZE;
    struct dirent *ent = &entries[probe];

    if (ent->valid != OCCUPIED) {
      ent->valid = 1;
      ent->inum  = inum;
      ent->count = 1;
      ent->off   = off;
      safestrcpy(ent->name, path, MAXPATH);
      release(&hash_table.lock);
      return;
    }
  }

  release(&hash_table.lock);
}

void
hash_invalidate(char *path)
{
  unsigned int h = primary_hash(path);

  unsigned int h1 = h % HASH_SIZE;
  unsigned int h2 = secondary_hash(h) % HASH_SIZE;
  if(h2 == 0)
    h2 = 1;

  acquire(&hash_table.lock);
  struct dirent *entries = hash_table.table;

  for (int i = 0; i < HASH_SIZE; i++) {
    int probe = (h1 + i * h2) % HASH_SIZE;
    struct dirent *ent = &entries[probe];

    if (ent->valid == EMPTY) {
      break;
    }
    if (strncmp(ent->name, path, MAXPATH) == 0) {
      ent->valid = DELETED;
      break;
    }
  }

  release(&hash_table.lock);
}



struct superblock sb; 

// Read the super block.
static void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp;

  bp = bread(dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}

// Init fs
void
fsinit(int dev) {
  readsb(dev, &sb);
  if(sb.magic != FSMAGIC)
    panic("invalid file system");
  initlog(dev, &sb);
}

// Zero a block.
static void
bzero(int dev, int bno)
{
  struct buf *bp;

  bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  log_write(bp);
  brelse(bp);
}

// Blocks.

// Allocate a zeroed disk block.
// returns 0 if out of disk space.
static uint
balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  for(b = 0; b < sb.size; b += BPB){
    bp = bread(dev, BBLOCK(b, sb));
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
      m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0){  // Is block free?
        bp->data[bi/8] |= m;  // Mark block in use.
        log_write(bp);
        brelse(bp);
        bzero(dev, b + bi);
        return b + bi;
      }
    }
    brelse(bp);
  }
  printf("balloc: out of blocks\n");
  return 0;
}

// Free a disk block.
static void
bfree(int dev, uint b)
{
  struct buf *bp;
  int bi, m;

  bp = bread(dev, BBLOCK(b, sb));
  bi = b % BPB;
  m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("freeing free block");
  bp->data[bi/8] &= ~m;
  log_write(bp);
  brelse(bp);
}

// Inodes.
//
// An inode describes a single unnamed file.
// The inode disk structure holds metadata: the file's type,
// its size, the number of links referring to it, and the
// list of blocks holding the file's content.
//
// The inodes are laid out sequentially on disk at block
// sb.inodestart. Each inode has a number, indicating its
// position on the disk.
//
// The kernel keeps a table of in-use inodes in memory
// to provide a place for synchronizing access
// to inodes used by multiple processes. The in-memory
// inodes include book-keeping information that is
// not stored on disk: ip->ref and ip->valid.
//
// An inode and its in-memory representation go through a
// sequence of states before they can be used by the
// rest of the file system code.
//
// * Allocation: an inode is allocated if its type (on disk)
//   is non-zero. ialloc() allocates, and iput() frees if
//   the reference and link counts have fallen to zero.
//
// * Referencing in table: an entry in the inode table
//   is free if ip->ref is zero. Otherwise ip->ref tracks
//   the number of in-memory pointers to the entry (open
//   files and current directories). iget() finds or
//   creates a table entry and increments its ref; iput()
//   decrements ref.
//
// * Valid: the information (type, size, &c) in an inode
//   table entry is only correct when ip->valid is 1.
//   ilock() reads the inode from
//   the disk and sets ip->valid, while iput() clears
//   ip->valid if ip->ref has fallen to zero.
//
// * Locked: file system code may only examine and modify
//   the information in an inode and its content if it
//   has first locked the inode.
//
// Thus a typical sequence is:
//   ip = iget(dev, inum)
//   ilock(ip)
//   ... examine and modify ip->xxx ...
//   iunlock(ip)
//   iput(ip)
//
// ilock() is separate from iget() so that system calls can
// get a long-term reference to an inode (as for an open file)
// and only lock it for short periods (e.g., in read()).
// The separation also helps avoid deadlock and races during
// pathname lookup. iget() increments ip->ref so that the inode
// stays in the table and pointers to it remain valid.
//
// Many internal file system functions expect the caller to
// have locked the inodes involved; this lets callers create
// multi-step atomic operations.
//
// The itable.lock spin-lock protects the allocation of itable
// entries. Since ip->ref indicates whether an entry is free,
// and ip->dev and ip->inum indicate which i-node an entry
// holds, one must hold itable.lock while using any of those fields.
//
// An ip->lock sleep-lock protects all ip-> fields other than ref,
// dev, and inum.  One must hold ip->lock in order to
// read or write that inode's ip->valid, ip->size, ip->type, &c.

struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} itable;

void
iinit()
{
  int i = 0;
  
  initlock(&itable.lock, "itable");
  for(i = 0; i < NINODE; i++) {
    initsleeplock(&itable.inode[i].lock, "inode");
  }
}

static struct inode* iget(uint dev, uint inum);

inline struct inode*
get_root_inode()
{
  return iget(ROOTDEV, ROOTINO);
}

// Allocate an inode on device dev.
// Mark it as allocated by  giving it type type.
// Returns an unlocked but allocated and referenced inode,
// or NULL if there is no free inode.
struct inode*
ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;

  for(inum = 1; inum < sb.ninodes; inum++){
    bp = bread(dev, IBLOCK(inum, sb));
    dip = (struct dinode*)bp->data + inum%IPB;
    if(dip->type == 0){  // a free inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      log_write(bp);   // mark it allocated on the disk
      brelse(bp);
      return iget(dev, inum);
    }
    brelse(bp);
  }
  printf("ialloc: no inodes\n");
  return 0;
}

// Copy a modified in-memory inode to disk.
// Must be called after every change to an ip->xxx field
// that lives on disk.
// Caller must hold ip->lock.
void
iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  bp = bread(ip->dev, IBLOCK(ip->inum, sb));
  dip = (struct dinode*)bp->data + ip->inum%IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  log_write(bp);
  brelse(bp);
}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.
static struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&itable.lock);

  // Is the inode already in the table?
  empty = 0;
  for(ip = &itable.inode[0]; ip < &itable.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++;
      release(&itable.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)    // Remember empty slot.
      empty = ip;
  }

  // Recycle an inode entry.
  if(empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0;
  release(&itable.lock);

  return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode*
idup(struct inode *ip)
{
  acquire(&itable.lock);
  ip->ref++;
  release(&itable.lock);
  return ip;
}

// Lock the given inode.
// Reads the inode from disk if necessary.
void
ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquiresleep(&ip->lock);

  if(ip->valid == 0){
    bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->valid = 1;
    if(ip->type == 0){
      printf("nlink: %d size: %d inum: %d\n", ip->nlink, ip->size, ip->inum);
      panic("ilock: no type");
    }
      
  }
}

// Unlock the given inode.
void
iunlock(struct inode *ip)
{
  if(ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    panic("iunlock");
  // #ifdef PROFILE
  // uint64 start_time, end_time;
  // start_time = r_time();
  // #endif
  releasesleep(&ip->lock);
//   #ifdef PROFILE
//   end_time = r_time();
//   printf("      release sleep: %ld\n\n", end_time - start_time);
//   #endif
}

// Drop a reference to an in-memory inode.
// If that was the last reference, the inode table entry can
// be recycled.
// If that was the last reference and the inode has no links
// to it, free the inode (and its content) on disk.
// All calls to iput() must be inside a transaction in
// case it has to free the inode.
void
iput(struct inode *ip)
{
  acquire(&itable.lock);

  if(ip->ref == 1 && ip->valid && ip->nlink == 0){
    // inode has no links and no other references: truncate and free.

    // ip->ref == 1 means no other process can have ip locked,
    // so this acquiresleep() won't block (or deadlock).
    acquiresleep(&ip->lock);

    release(&itable.lock);

    itrunc(ip);
    ip->type = 0;
    iupdate(ip);
    ip->valid = 0;

    releasesleep(&ip->lock);

    acquire(&itable.lock);
  }

  ip->ref--;
  release(&itable.lock);
}

// Common idiom: unlock, then put.
void
iunlockput(struct inode *ip)
{
  // #ifdef PROFILE
  // uint64 start_time, end_time;
  // start_time = r_time();
  // #endif

  iunlock(ip);
  // #ifdef PROFILE
  // end_time = r_time();
  // printf("      iunlock: %ld\n", end_time - start_time);
  // start_time = end_time;
  // #endif

  iput(ip);

  // #ifdef PROFILE
  // end_time = r_time();
  // printf("      iput: %ld\n\n", end_time - start_time);
  // #endif
}

// Inode content
//
// The content (data) associated with each inode is stored
// in blocks on the disk. The first NDIRECT block numbers
// are listed in ip->addrs[].  The next NINDIRECT blocks are
// listed in block ip->addrs[NDIRECT].

// Return the disk block address of the nth block in inode ip.
// If there is no such block, bmap allocates one.
// returns 0 if out of disk space.
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0)
        return 0;
      ip->addrs[bn] = addr;
    }
    return addr;
  }
  bn -= NDIRECT;

  if(bn < NINDIRECT){
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0)
        return 0;
      ip->addrs[NDIRECT] = addr;
    }
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      addr = balloc(ip->dev);
      if(addr){
        a[bn] = addr;
        log_write(bp);
      }
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}

// Truncate inode (discard contents).
// Caller must hold ip->lock.
void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;
  if(ip->size == 0){
    return;
  }

  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}

// Copy stat information from inode.
// Caller must hold ip->lock.
void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  if(ip->type == T_DIR && ip->inum != ROOTINO){
    st->size = 0;
  } else {
    st->size = ip->size;
  }
}

// Read data from inode.
// Caller must hold ip->lock.
// If user_dst==1, then dst is a user virtual address;
// otherwise, dst is a kernel address.
int
readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(off > ip->size || off + n < off)
    return 0;
  if(off + n > ip->size)
    n = ip->size - off;

  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
    uint addr = bmap(ip, off/BSIZE);
    if(addr == 0)
      break;
    bp = bread(ip->dev, addr);
    m = min(n - tot, BSIZE - off%BSIZE);
    if(either_copyout(user_dst, dst, bp->data + (off % BSIZE), m) == -1) {
      brelse(bp);
      tot = -1;
      break;
    }
    brelse(bp);
  }
  return tot;
}

// Write data to inode.
// Caller must hold ip->lock.
// If user_src==1, then src is a user virtual address;
// otherwise, src is a kernel address.
// Returns the number of bytes successfully written.
// If the return value is less than the requested n,
// there was an error of some kind.
int
writei(struct inode *ip, int user_src, uint64 src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > MAXFILE*BSIZE)
    return -1;

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    uint addr = bmap(ip, off/BSIZE);
    if(addr == 0)
      break;
    bp = bread(ip->dev, addr);
    m = min(n - tot, BSIZE - off%BSIZE);
    if(either_copyin(bp->data + (off % BSIZE), user_src, src, m) == -1) {
      brelse(bp);
      break;
    }
    log_write(bp);
    brelse(bp);
  }

  if(off > ip->size)
    ip->size = off;

  // write the i-node back to disk even if the size didn't change
  // because the loop above might have called bmap() and added a new
  // block to ip->addrs[].
  iupdate(ip);

  return tot;
}

// Directories

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, MAXPATH);
}

// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  static int first = 0;
  // uint off, inum;
  uint off;
  struct dirent de;
  struct dirent *cache_de;
  // printf("dirlookup %d\n", first);
  if(first == 0){
    first = 1;
    isFull = 1;
    for(off = 0; off < dp->size; off += sizeof(struct dirent)){
      if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
        panic("init hash table");
      if(de.inum == 0){
        isFull = 0;
        continue;
      }
      hash_insert(de.name, de.inum, off);
    }
  }
  // #ifdef PROFILE
  // uint64 start_time, end_time;
  // start_time = r_time();
  // #endif
  cache_de = hash_lookup(name);
  // #ifdef PROFILE
  // end_time = r_time();
  // printf("      hash_lookup: %ld\n", end_time - start_time);
  // #endif
  if (cache_de) {
    if (poff) {
      *poff = cache_de->off;
    }
    return iget(dp->dev, cache_de->inum);
  }
  // printf("not found name: %s\n", name);

  // for(off = 0; off < dp->size; off += sizeof(de)){
  //   if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
  //     panic("dirlookup read");
  //   if(de.inum == 0)
  //     continue;
  //   if(namecmp(name, de.name) == 0){
  //     // entry matches path element
  //     if(poff)
  //       *poff = off;
  //     inum = de.inum;
  //     hash_insert(name, inum, off);
  //     return iget(dp->dev, inum);
  //   }
  // }

  return 0;
}

// Write a new directory entry (name, inum) into the directory dp.
// Returns 0 on success, -1 on failure (e.g. out of disk blocks).
int
dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;

  // Check that name is not present.
  if((ip = dirlookup(dp, name, 0)) != 0){
    iput(ip);
    return -1;
  }
  if(!isFull){
    // Look for an empty dirent.
    for(off = 0; off < dp->size; off += sizeof(de)){
      if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
        panic("dirlink read");
      if(de.inum == 0)
        break;
    }
    if(off == dp->size){
      isFull = 1;
    }
  }
  else{
    off = dp->size;
  }

  strncpy(de.name, name, MAXPATH);
  de.inum = inum;
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    return -1;
  hash_insert(name, inum, off);

  return 0;
}

// Paths

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
// static char*
// skipelem(char *path, char *name)
// {
//   char *s;
//   int len;

//   while(*path == '/')
//     path++;
//   if(*path == 0)
//     return 0;
//   s = path;
//   while(*path != '/' && *path != 0)
//     path++;
//   len = path - s;
//   if(len >= DIRSIZ)
//     memmove(name, s, DIRSIZ);
//   else {
//     memmove(name, s, len);
//     name[len] = 0;
//   }
//   while(*path == '/')
//     path++;
//   return path;
// }

// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
// #define PROFILE
struct inode*
namex(char *path)
{
  struct inode* root, *ip;
  // #ifdef PROFILE
  // uint64 s = r_time();
  // uint64 t = s;
  // #endif
  root = get_root_inode();
  if(namecmp("/", path) == 0){
    return root;
  }
  ilock(root);
  // #ifdef PROFILE
  // uint64 e = r_time();
  // printf("    ilock: %ld\n", e - s);
  // s = e;
  // #endif
  ip = dirlookup(root,path, 0);
  // #ifdef PROFILE
  // e = r_time();
  // printf("    dirlookup: %ld\n", e - s);
  // s = e;
  // #endif
  iunlockput(root);
  // #ifdef PROFILE
  // e = r_time();
  // printf("    iunlockput: %ld\n", e - s);
  // printf("    full namex time: %ld\n", e - t);
  // s = e;
  // #endif
  return ip;
}

//namei를 사용하기 전에는 항상 full path를 전달해야 함
struct inode*
namei(char *fullPath)
{
  return namex(fullPath);
}

struct inode*
nameiparent(char *fullPath)
{
  char parentPath [MAXPATH];
  get_parent_path(fullPath, parentPath);
  return namex(parentPath);
}

// inline void resolve_absolute_path(char *path, char *fullPath) {
//   char tempPath[MAXPATH];
//   char *stack[MAXPATH];
//   int top = 0;

//   const char *pwd = myproc()->pwd;

//   int len;
//   if (path[0] != '/') {
//     len = strlen(pwd);
//     strncpy(tempPath, pwd, len);
//     tempPath[len] = '\0';
//     if (len < MAXPATH - 1) {
//       tempPath[len] = '/';
//       len++;
//     }

//     int remaining = MAXPATH - len - 1;
//     strncpy(tempPath + len, path, remaining);
//     tempPath[MAXPATH - 1] = '\0';
//   } else {
//     strncpy(tempPath, path, MAXPATH - 1);
//     tempPath[MAXPATH - 1] = '\0';
//   }

//   char *token = tempPath;
//   char *next;

//   while (1) {
//     while (*token == '/') {
//       *token = '\0';
//       token++;
//     }
//     if (*token == '\0') break;

//     next = token;
//     while (*next != '/' && *next != '\0') next++;
//     len = next - token;

//     if (len == 1 && strncmp(token, ".", 1) == 0) {
//     } else if (len == 2 && strncmp(token, "..", 2) == 0) {
//       if (top > 0) top--;
//     } else {
//       stack[top++] = token;
//     }

//     if (*next == '\0') break;
//     token = next;
//   }

//   strncpy(fullPath, "/", 1);
//   fullPath[1] = '\0';
//   len = 1;

//   for (int i = 0; i < top; i++) {
//     int partLen = strlen(stack[i]);
//     if (len + partLen + 1 < MAXPATH) {
//       strncpy(fullPath + len, stack[i], partLen);
//       len += partLen;
//       if (i < top - 1) {
//         fullPath[len] = '/';
//         len++;
//       }
//     }
//   }

//   if (len == 0) {
//     strncpy(fullPath, "/", 1);
//     fullPath[1] = '\0';
//   } else {
//     fullPath[len] = '\0';
//   }
// }

void
resolve_absolute_path(char *path, char *fullPath)
{
  int slash_index[MAXPATH];
  int top = -1;
  int len = 0;
  // char tempPath[MAXPATH];
  int check = 0 ;

  const char *pwd = myproc()->pwd;
  const char *src = path;
  // char *dst = tempPath;
  char *dst = fullPath;

  if (*src == '/') {
  
    dst[len++] = '/';
    slash_index[++top] = 0;
    src++;
  } else {
    int i = 0;
    while (pwd[i] != '\0') {
      dst[len] = pwd[i];
      if (pwd[i] == '/') {
        slash_index[++top] = len;
      }
      len++;
      i++;
    }
    if (dst[len-1] != '/' && len < MAXPATH - 1) {
      dst[len++] = '/';
      slash_index[++top] = (len-1);
    }
  }

  while (*src != '\0' && len < MAXPATH - 1) {
    if (*src == '/') {
      src++;
      continue;
    }

    if (*src == '.' && *(src+1) == '.' && *(src + 2) =='/') {
        src += 3; 
        top = top > 0 ? top - 1 : 0;
        len = slash_index[top];
        len++;
        continue;
    }
    else if(*src == '.' && *(src+1) == '.' && *(src + 2) =='\0') {
        top = top > 0 ? top - 1 : 0;
        len = slash_index[top];
        if(len == 0){
          dst[++len] = '\0';
          check = 1;
          break;
        }
        else{
          dst[len] = '\0';
        }
        check = 1;
        break;
    }
    else if(*src == '.' && *(src+1) == '/') {
        src += 2; 
        continue;
    }
    else if(*src == '.' && *(src+1) == '\0') {
        len = slash_index[top];
        if(len == 0){
          dst[++len] = '\0';
          check = 1;
          break;
        }
        else{
          dst[len] = '\0';
        }
        check = 1;
        break;
    }
    else {
      while (*src != '\0' && *src != '/') {
        dst[len++] = *src;
        src++;
      }
      if (*src == '/') {
        dst[len++] = '/';
        slash_index[++top] = (len-1);
        src++;
      }
      else if(*src == '\0'){
        dst[len] = '\0';
        check = 1;
        break;
      }
    }
  }

  if(len == 1){
    dst[0] = '/';
    dst[1] = '\0';
  }
  else if (check == 1){
  }
  else{
    if(dst[len-1] == '/'){
      dst[len-1] = '\0';
    }
    else{
      dst[len] = '\0';
    }
  }

  // strncpy(fullPath, tempPath, MAXPATH);
  // printf("path: %s fullPath: %s\n", path, fullPath);
}




inline void get_parent_path(const char *path, char *parent) {
    int len = strlen(path);

    if (len == 0 || strncmp(path, "/", MAXPATH) == 0) {
        strncpy(parent, "/", MAXPATH - 1);
        parent[MAXPATH - 1] = '\0';
        return;
    }

    int i = len - 1;
    while (i >= 0 && path[i] == '/') {
        i--;
    }
    while (i >= 0 && path[i] != '/') {
        i--;
    }

    if (i <= 0) {
        strncpy(parent, "/", MAXPATH - 1);
        parent[MAXPATH - 1] = '\0';
    } else {
        strncpy(parent, path, i);
        parent[i] = '\0';
    }
}