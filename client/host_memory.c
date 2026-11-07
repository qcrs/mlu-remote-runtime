#define _GNU_SOURCE
#include "host_memory.h"
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <errno.h>

struct mlu_host_allocation {
  struct mlu_host_allocation *next;
  void *base;
  size_t bytes, pages, mapped, references;
  int live,imported,backing_fd,listener;
  uint64_t ipc_token;
};
static pthread_mutex_t host_lock = PTHREAD_MUTEX_INITIALIZER;
static mlu_host_allocation *hosts;
static void retire(mlu_host_allocation *a) {
  if (a->live || a->references || !a->pages) return;
  munlock(a->base, a->pages);
  /* Drop the shared mapping itself so the memfd's final reference can die. */
  mmap(a->base,a->pages,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
  if(a->backing_fd>=0){close(a->backing_fd);a->backing_fd=-1;}
  a->pages = 0; /* Keep a VA tombstone, as for remote device allocations. */
}
int mlu_host_alloc(void **out, size_t bytes) {
  if (!out) return 100006;
  if (!bytes) { *out = NULL; return 0; }
  size_t page = (size_t)sysconf(_SC_PAGESIZE);
  if (bytes > SIZE_MAX - 2 * page) return 100006;
  mlu_host_allocation *a = calloc(1, sizeof(*a));
  if (!a) return 100010;
  a->pages = (bytes + page - 1) / page * page;
  a->mapped = a->pages + page;
  a->base = mmap(NULL, a->mapped, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (a->base == MAP_FAILED) { free(a); return 100010; }
  a->backing_fd=memfd_create("mlu-host",MFD_CLOEXEC);a->listener=-1;
  if (a->backing_fd<0 || ftruncate(a->backing_fd,a->pages) || mmap(a->base,a->pages,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_FIXED,a->backing_fd,0)==MAP_FAILED || mlock(a->base, a->pages)) {
    if(a->backing_fd>=0)close(a->backing_fd);
    munmap(a->base, a->mapped); free(a); return 100010;
  }
  a->bytes = bytes; a->live = 1;
  pthread_mutex_lock(&host_lock);
  a->next = hosts; hosts = a; *out = a->base;
  pthread_mutex_unlock(&host_lock);
  return 0;
}
int mlu_host_free(void *ptr) {
  if (!ptr) return 0;
  pthread_mutex_lock(&host_lock);
  int st = 100006;
  for (mlu_host_allocation *a = hosts; a; a = a->next) if (a->base == ptr && a->live) {
    a->live = 0; if(a->listener>=0)shutdown(a->listener,SHUT_RDWR); retire(a); st = 0; break;
  }
  pthread_mutex_unlock(&host_lock);
  return st;
}
static int acquire(const void *ptr, size_t bytes, mlu_host_allocation **lease) {
  if (lease) *lease = NULL;
  int kind = 0;
  pthread_mutex_lock(&host_lock);
  uintptr_t value = (uintptr_t)ptr;
  for (mlu_host_allocation *a = hosts; a; a = a->next) {
    if (value < (uintptr_t)a->base || value - (uintptr_t)a->base >= a->mapped) continue;
    size_t off = value - (uintptr_t)a->base;
    kind = a->live && off < a->bytes && bytes <= a->bytes - off ? 1 : -1;
    if (kind == 1 && lease) { a->references++; *lease = a; }
    break;
  }
  pthread_mutex_unlock(&host_lock);
  return kind;
}
int mlu_host_range(const void *ptr, size_t bytes) { return acquire(ptr, bytes, NULL); }
int mlu_host_acquire(void *ptr, size_t bytes, mlu_host_allocation **lease) { return acquire(ptr, bytes, lease); }
void mlu_host_release(mlu_host_allocation *lease) {
  if (!lease) return;
  pthread_mutex_lock(&host_lock);
  if (lease->references) lease->references--;
  retire(lease);
  pthread_mutex_unlock(&host_lock);
}

/* Linux abstract Unix sockets carry the actual memfd via SCM_RIGHTS. They
 * disappear on exit (including SIGKILL); no named shared-memory files leak.
 * Host IPC is intentionally confined to clients on the same host / UID. */
static socklen_t host_ipc_address(struct sockaddr_un *addr,uint64_t token) {
  memset(addr,0,sizeof(*addr));addr->sun_family=AF_UNIX;
  int n=snprintf(addr->sun_path+1,sizeof(addr->sun_path)-1,"mlu-host-%u-%016llx",(unsigned)getuid(),(unsigned long long)token);
  return offsetof(struct sockaddr_un,sun_path)+1+n;
}
static void *host_ipc_broker(void *opaque) {
  mlu_host_allocation *a=opaque;
  for(;;) {
    int peer=accept4(a->listener,NULL,NULL,SOCK_CLOEXEC);
    if(peer<0){if(errno==EINTR)continue;break;}
    struct ucred cred;socklen_t clen=sizeof(cred);int backing=-1;uint64_t bytes=0;
    if(!getsockopt(peer,SOL_SOCKET,SO_PEERCRED,&cred,&clen) && cred.uid==getuid()) {
      pthread_mutex_lock(&host_lock);if(a->live){backing=dup(a->backing_fd);bytes=a->bytes;}pthread_mutex_unlock(&host_lock);
    }
    if(backing>=0) {
      struct timeval timeout={.tv_sec=5};setsockopt(peer,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
      char control[CMSG_SPACE(sizeof(int))]={0};struct iovec io={&bytes,8};
      struct msghdr msg={.msg_iov=&io,.msg_iovlen=1,.msg_control=control,.msg_controllen=sizeof(control)};
      struct cmsghdr *c=CMSG_FIRSTHDR(&msg);c->cmsg_level=SOL_SOCKET;c->cmsg_type=SCM_RIGHTS;c->cmsg_len=CMSG_LEN(sizeof(int));memcpy(CMSG_DATA(c),&backing,sizeof(int));
      sendmsg(peer,&msg,MSG_NOSIGNAL);close(backing);
    }
    close(peer);
  }
  pthread_mutex_lock(&host_lock);close(a->listener);a->listener=-1;pthread_mutex_unlock(&host_lock);return NULL;
}
int mlu_host_ipc_export(void *ptr,uint64_t *token) {
  pthread_mutex_lock(&host_lock);int st=100006;mlu_host_allocation *a=hosts;
  while(a && (a->base!=ptr || !a->live || a->imported))a=a->next;
  if(a) {
    st=a->ipc_token ? 100102 : 0;
    if(!a->ipc_token) {
      uint64_t t=0;int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);
      if(getrandom(&t,8,0)!=8 || fd<0)st=100007;
      t=(t&UINT64_C(0x0000ffffffffffff))|UINT64_C(0x484d000000000000);
      struct sockaddr_un address;socklen_t len=host_ipc_address(&address,t);
      if(!st && (bind(fd,(struct sockaddr *)&address,len) || listen(fd,16)))st=100007;
      if(!st){a->listener=fd;pthread_t thread;if(pthread_create(&thread,NULL,host_ipc_broker,a))st=100007;else {pthread_detach(thread);a->ipc_token=t;}}
      if(st && fd>=0){close(fd);a->listener=-1;}
    }
    if(!st)*token=a->ipc_token;
  }
  pthread_mutex_unlock(&host_lock);return st;
}
int mlu_host_ipc_import(void **out,uint64_t token) {
  int peer=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(peer<0)return 100007;
  struct sockaddr_un address;socklen_t len=host_ipc_address(&address,token);
  struct timeval timeout={.tv_sec=5};setsockopt(peer,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
  if(connect(peer,(struct sockaddr *)&address,len)){close(peer);return 100050;}
  char control[CMSG_SPACE(sizeof(int))]={0};uint64_t bytes=0;struct iovec io={&bytes,8};
  struct msghdr msg={.msg_iov=&io,.msg_iovlen=1,.msg_control=control,.msg_controllen=sizeof(control)};
  ssize_t n=recvmsg(peer,&msg,MSG_CMSG_CLOEXEC|MSG_WAITALL);close(peer);
  int fd=-1;struct cmsghdr *c=CMSG_FIRSTHDR(&msg);
  if(c && c->cmsg_level==SOL_SOCKET && c->cmsg_type==SCM_RIGHTS && c->cmsg_len==CMSG_LEN(sizeof(int)))memcpy(&fd,CMSG_DATA(c),sizeof(fd));
  if(n!=8 || msg.msg_flags&MSG_CTRUNC || fd<0 || !bytes || bytes>SIZE_MAX-8192){if(fd>=0)close(fd);return 100006;}
  mlu_host_allocation *a=calloc(1,sizeof(*a));if(!a){close(fd);return 100010;}
  size_t page=sysconf(_SC_PAGESIZE);a->pages=(bytes+page-1)/page*page;a->mapped=a->pages+page;
  a->base=mmap(NULL,a->mapped,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
  if(a->base==MAP_FAILED){close(fd);free(a);return 100010;}
  if(mmap(a->base,a->pages,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_FIXED,fd,0)==MAP_FAILED || mlock(a->base,a->pages)){munmap(a->base,a->mapped);close(fd);free(a);return 100010;}
  a->backing_fd=fd;a->listener=-1;a->bytes=bytes;a->live=1;a->imported=1;
  pthread_mutex_lock(&host_lock);a->next=hosts;hosts=a;*out=a->base;pthread_mutex_unlock(&host_lock);return 0;
}
int mlu_host_ipc_close(void *ptr) {
  pthread_mutex_lock(&host_lock);int st=100006;
  for(mlu_host_allocation *a=hosts;a;a=a->next)if(a->base==ptr && a->live && a->imported){a->live=0;retire(a);st=0;break;}
  pthread_mutex_unlock(&host_lock);return st;
}
