#ifdef SNU
#include "kernel/param.h"
#endif
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

#define BUFSIZE 512

// void resolve_absolute_path(char *path, char *fullPath) {
//   char tempPath[BUFSIZE];
//   char *stack[MAXPATH];
//   char buf[MAXPATH];
//   int top = 0;

//   pwd(buf);
//   printf("buf: %s\n", buf);
//   int len;
//   printf("path: %s\n", path);
//   if (path[0] != '/') {
//     len = strlen(buf);
//     printf("len: %d\n", len);
//     memmove(tempPath, buf, len);
//     printf("1tempPath: %s\n", tempPath);
//     tempPath[len] = '\0';
//     if (len < BUFSIZE - 1) {
//       tempPath[len] = '/';
//       len++;
//     }
//     printf("2tempPath: %s\n", tempPath);
//     int remaining = BUFSIZE - len - 1;
//     memmove(tempPath + len, path, remaining);
//     printf("3tempPath: %s\n", tempPath);
//     tempPath[BUFSIZE - 1] = '\0';
//   } else {
//     printf("path: %s\n", path);
//     memmove(tempPath, path, BUFSIZE - 1);
//     printf("4tempPath: %s\n", tempPath);
//     tempPath[BUFSIZE - 1] = '\0';
//     printf("5tempPath: %s\n", tempPath);
//   }
//   printf("tempPath: %s\n", tempPath);
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

//     if (len == 1 && memcmp(token, ".", 1) == 0) {
//     } else if (len == 2 && memcmp(token, "..", 2) == 0) {
//       if (top > 0) top--;
//     } else {
//       stack[top++] = token;
//     }

//     if (*next == '\0') break;
//     token = next;
//   }

//   memmove(fullPath, "/", 1);
//   fullPath[1] = '\0';
//   len = 1;

//   for (int i = 0; i < top; i++) {
//     int partLen = strlen(stack[i]);
//     if (len + partLen + 1 < MAXPATH) {
//       memmove(fullPath + len, stack[i], partLen);
//       len += partLen;
//       if (i < top - 1) {
//         fullPath[len] = '/';
//         len++;
//       }
//     }
//   }

//   if (len == 0) {
//     memmove(fullPath, "/", 1);
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
  char tempPath[MAXPATH];

  char t_pwd[MAXPATH];

  pwd(t_pwd);
  const char *src = path;
  char *dst = tempPath;

  if (*src == '/') {
  
    dst[len++] = '/';
    slash_index[++top] = 0;
    src++;
  } else {
    int i = 0;
    while (t_pwd[i] != '\0' && len < MAXPATH - 1) {
      dst[len] = t_pwd[i];
      if (t_pwd[i] == '/') {
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

    if (*src == '.') {
      if (*(src+1) == '.') {
        src += 2; 
        if (*src == '/') {
          src++;
          top--;
          len = top > -1 ? slash_index[top--] : 0;
          len++;
          continue;
        }
        else if(*src == '\0'){
          top--;
          len = top > -1 ? slash_index[top--] : 0;
          len++;
          break;
        }
      }
      else if (*(src+1) == '/' || *(src+1) == '\0') {
        src++;
        if (*src == '/') src++;
      }
      else {
        while (*src != '\0' && *src != '/' && len < MAXPATH - 1) {
          dst[len++] = *src;
          src++;
        }
        if (*src == '/' && len < MAXPATH - 1) {
          dst[len++] = '/';
          slash_index[top] = (len-1);
          src++;
        }
      }
    }
    else {
      slash_index[++top] = len;
      while (*src != '\0' && *src != '/' && len < MAXPATH - 1) {
        dst[len++] = *src;
        src++;
      }
      if (*src == '/' && len < MAXPATH - 1) {
        dst[len++] = '/';
        slash_index[top] = (len-1);
        src++;
      }
    }
  }

  if(len == 1){
    dst[0] = '/';
    dst[1] = '\0';
  }
  else if(dst[len-1] == '/'){
    dst[len - 1] = '\0';
  }
  else{
    dst[len] = '\0';
  }

  memcpy(fullPath, tempPath, MAXPATH);
  // printf("path: %s fullPath: %s\n", path, fullPath);
}



char*
my_fmtname(const char *dirPath, const char *name){
  static char buf[MAXPATH];
  const char *p;
  int i = 0;
  int len = strlen(dirPath);

  if(memcmp(dirPath, name, len) != 0){
    return 0;
  }
  
  if(len == 1) len--;
  p = name + len;
  if(*p != '/') return 0;
  p++;

  while(*p != '\0'){
    if(*p == '/') return 0;
    buf[i++] = *p++;
  }
  buf[i] = '\0';
  return buf;
}

char*
fmtname(char *path)
{
  static char buf[MAXPATH+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= MAXPATH)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
  return buf;
}

void
ls(char *path)
{
  char *p;
  char dirPath[MAXPATH];
  int fd;
  struct dirent de;
  struct stat st;

  if(memcmp(path, "//", 3) == 0){
    if((fd = open("/", O_RDONLY)) < 0){
      fprintf(2, "ls: cannot open %s\n", "/");
      return;
    }
    if(fstat(fd, &st) < 0){
      fprintf(2, "ls: cannot stat %s\n", "/");
      close(fd);
      return;
    }
    printf("%d %d %d %d %s\n",st.type,st.ino, st.nlink, (int) st.size, "/");

    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;
      
      stat(de.name, &st);
      
      printf("%d %d %d %d %s\n",st.type,st.ino, st.nlink, (int) st.size, de.name);
    }
    close(fd);
    return;
  }

  if((fd = open(path, O_RDONLY)) < 0){
    fprintf(2, "ls: cannot open %s\n", path);
    return;
  } 
  if(fstat(fd, &st) < 0){
    fprintf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  case T_DEVICE:
  case T_FILE:
    printf("%d %d %d %d %s\n",st.type,st.ino, st.nlink, (int) st.size, fmtname(path));
    break;

  case T_DIR:
    resolve_absolute_path(path, dirPath);
    int root_fd = open("/", O_RDONLY);
    if(root_fd < 0){
      fprintf(2, "ls: cannot open root directory\n");
      close(fd);
      return;
    }
    
    while(read(root_fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;
      if((p = my_fmtname(dirPath, de.name)) == 0)
        continue;
      if(stat(de.name, &st) < 0){
        printf("ls: cannot stat %s\n", p);
        continue;
      }
      printf("%d %d %d %d %s\n",st.type,st.ino, st.nlink, (int) st.size, p);
    }
    break;
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
  int i;

  if(argc < 2){
    ls(".");
    exit(0);
  }
  for(i=1; i<argc; i++)
    ls(argv[i]);
  exit(0);
}