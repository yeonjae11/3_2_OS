// Long-term locks for processes
struct sleeplock {
  uint locked;       // Is the lock held?
  struct spinlock lk; // spinlock protecting this sleep lock
  
  // For debugging:
  char *name;        // Name of lock.
  int pid;           // Process holding lock
  int head;  // 첫 노드 인덱스
  int tail;  // 마지막 노드 인덱스

  int next[NPROC];
};