typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  uchar;

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int  uint32;
typedef unsigned long uint64;

typedef uint64 pde_t;

#ifdef SNU
#define PMP_R   (1)
#define PMP_W   (1 << 1)
#define PMP_X   (1 << 2)
#endif
