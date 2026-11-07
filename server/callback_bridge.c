#define _GNU_SOURCE
#include "callback_bridge.h"
#include "remote_callback.h"
#include "remote_io.h"
#include <pthread.h>
#include <stdlib.h>
#include <sys/random.h>
#include <unistd.h>
/* The registry owns no reference: the primary session removes its entry before
 * dropping its reference. Native destructors retain a service, never a session. */
struct cb_event {
  struct cb_event *next;
  unsigned kind;uint64_t run,token;void *data;uint32_t bytes;
  int wait,done,status;
};
struct mlu_callback_service {
  struct mlu_callback_service *next;
  pthread_mutex_t lock;pthread_cond_t changed;
  unsigned refs,pending,users;int aborted,attached,primary_fd,fd;
  uint32_t timeout_ms;uint64_t secret[2];
  struct cb_event *head,**tail;
};
static pthread_mutex_t registry_lock=PTHREAD_MUTEX_INITIALIZER;
static mlu_callback_service *registry;
void mlu_callback_retain(mlu_callback_service *s) {pthread_mutex_lock(&s->lock);s->refs++;pthread_mutex_unlock(&s->lock);}
void mlu_callback_release(mlu_callback_service *s) {
  if(!s) return;
  pthread_mutex_lock(&s->lock);int last=!--s->refs;pthread_mutex_unlock(&s->lock);
  if(last) {pthread_cond_destroy(&s->changed);pthread_mutex_destroy(&s->lock);free(s);}
}
mlu_callback_service *mlu_callback_create(int fd,uint32_t ms,uint64_t secret[2]) {
  mlu_callback_service *s=calloc(1,sizeof(*s));if(!s) return NULL;
  if(getrandom(s->secret,16,0)!=16) {free(s);return NULL;}
  pthread_mutex_init(&s->lock,NULL);pthread_cond_init(&s->changed,NULL);
  s->refs=1;s->fd=-1;s->primary_fd=fd;s->timeout_ms=ms;s->tail=&s->head;memcpy(secret,s->secret,16);
  pthread_mutex_lock(&registry_lock);s->next=registry;registry=s;pthread_mutex_unlock(&registry_lock);return s;
}
void mlu_callback_abort(mlu_callback_service *s,int poison) {
  if(!s) return;
  pthread_mutex_lock(&s->lock);s->aborted=1;
  if(s->fd>=0) shutdown(s->fd,SHUT_RDWR);
  if(poison && s->primary_fd>=0) shutdown(s->primary_fd,SHUT_RDWR);
  pthread_cond_broadcast(&s->changed);pthread_mutex_unlock(&s->lock);
}
static struct timespec cb_until(uint32_t ms) {
  struct timespec t;clock_gettime(CLOCK_REALTIME,&t);t.tv_sec+=ms/1000;t.tv_nsec+=(ms%1000)*1000000L;
  if(t.tv_nsec>=1000000000L) {t.tv_sec++;t.tv_nsec-=1000000000L;}return t;
}
void mlu_callback_user_add(mlu_callback_service *s) {mlu_callback_retain(s);pthread_mutex_lock(&s->lock);s->users++;pthread_mutex_unlock(&s->lock);}
void mlu_callback_user_done(mlu_callback_service *s) {pthread_mutex_lock(&s->lock);s->users--;pthread_cond_broadcast(&s->changed);pthread_mutex_unlock(&s->lock);mlu_callback_release(s);}
int mlu_callback_exchange(mlu_callback_service *s,unsigned kind,uint64_t run,uint64_t token,void *data,uint32_t bytes,int wait) {
  struct cb_event *e=calloc(1,sizeof(*e));if(!e) {mlu_callback_abort(s,1);return -1;}
  *e=(struct cb_event){.kind=kind,.run=run,.token=token,.data=data,.bytes=bytes,.wait=wait};
  pthread_mutex_lock(&s->lock);
  if(s->aborted || !s->attached || s->pending>=8192) {pthread_mutex_unlock(&s->lock);free(e);mlu_callback_abort(s,1);return -1;}
  *s->tail=e;s->tail=&e->next;s->pending++;pthread_cond_broadcast(&s->changed);
  if(!wait) {pthread_mutex_unlock(&s->lock);return 0;}
  /* Worker retains ownership until done, including after a channel abort. */
  while(!e->done) pthread_cond_wait(&s->changed,&s->lock);
  int st=e->status;pthread_mutex_unlock(&s->lock);free(e);return st;
}
int mlu_callback_finish(mlu_callback_service *s) {
  if(!s) return 0;
  struct timespec until=cb_until(s->timeout_ms);int st=0;
  pthread_mutex_lock(&s->lock);
  while(!s->aborted && (s->users || s->pending)) if(pthread_cond_timedwait(&s->changed,&s->lock,&until)) {st=-1;break;}
  if(s->aborted) st=-1;
  pthread_mutex_unlock(&s->lock);
  if(!st)st=mlu_callback_exchange(s,MLU_CALLBACK_CLOSE,0,0,NULL,0,1);
  pthread_mutex_lock(&s->lock);s->primary_fd=-1;pthread_mutex_unlock(&s->lock);
  pthread_mutex_lock(&registry_lock);mlu_callback_service **p=&registry;while(*p && *p!=s)p=&(*p)->next;if(*p)*p=s->next;pthread_mutex_unlock(&registry_lock);
  mlu_callback_abort(s,0);mlu_callback_release(s);return st;
}
int mlu_callback_attach(int fd,const unsigned char *secret,uint32_t bytes,uint64_t request,uint32_t ms) {
  mlu_callback_service *s=NULL;
  pthread_mutex_lock(&registry_lock);
  for(mlu_callback_service *p=registry;p && bytes==16;p=p->next) if(cb_get64(secret)==p->secret[0] && cb_get64(secret+8)==p->secret[1]) {
    pthread_mutex_lock(&p->lock);
    if(!p->aborted && !p->attached) {p->attached=1;p->fd=fd;p->refs++;s=p;}
    pthread_mutex_unlock(&p->lock);break;
  }
  pthread_mutex_unlock(&registry_lock);
  struct mlu_remote_wire_header h=cb_header(request,8,s ? 0 : MLU_REMOTE_BAD_OBJECT);unsigned char prefix[8]={0};
  int64_t deadline=mlu_deadline_after(ms);
  int ok=mlu_io_full(fd,&h,sizeof(h),1,deadline)==1 && mlu_io_full(fd,prefix,8,1,deadline)==1;
  if(!s) return -1;
  uint64_t seq=request;
  while(ok) {
    pthread_mutex_lock(&s->lock);
    while(!s->head && !s->aborted) {
      struct timespec until=cb_until(250);
      pthread_cond_timedwait(&s->changed,&s->lock,&until);
      /* Detect EOF while idle, without imposing an idle lifetime limit. */
      unsigned char c;ssize_t n=recv(fd,&c,1,MSG_PEEK|MSG_DONTWAIT);
      if(!n || (n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR) || n>0) {s->aborted=1;break;}
    }
    struct cb_event *e=s->head;
    if(!e || s->aborted) {pthread_mutex_unlock(&s->lock);break;}
    s->head=e->next;if(!s->head)s->tail=&s->head;pthread_mutex_unlock(&s->lock);
    unsigned char event[32]={0};cb_put64(event,e->kind);cb_put64(event+8,e->run);cb_put64(event+16,e->token);cb_put64(event+24,e->bytes);
    uint32_t send_bytes=(e->kind==MLU_CALLBACK_D2H || e->kind==MLU_CALLBACK_QUEUE) ? e->bytes : 0;
    h=cb_header(++seq,32+send_bytes,0);deadline=mlu_deadline_after(s->timeout_ms);
    ok=mlu_io_full(fd,&h,sizeof(h),1,deadline)==1 && mlu_io_full(fd,event,32,1,deadline)==1 &&
      (!send_bytes || mlu_io_full(fd,e->data,send_bytes,1,deadline)==1) && mlu_io_full(fd,&h,sizeof(h),0,deadline)==1;
    uint32_t receive=e->kind==MLU_CALLBACK_H2D ? e->bytes : 0;
    if(ok) ok=cb_header_valid(&h,seq) && !ntohl(h.status) && ntohl(h.payload_len)==receive;
    if(ok && receive) ok=mlu_io_full(fd,e->data,receive,0,deadline)==1;
    int waiting=e->wait,closing=e->kind==MLU_CALLBACK_CLOSE;
    pthread_mutex_lock(&s->lock);if(ok && closing)s->primary_fd=-1;e->status=ok ? 0 : -1;e->done=1;s->pending--;pthread_cond_broadcast(&s->changed);pthread_mutex_unlock(&s->lock);
    if(!waiting) free(e);
    if(closing)break;
  }
  mlu_callback_abort(s,1);
  pthread_mutex_lock(&s->lock);
  while(s->head) {struct cb_event *e=s->head;s->head=e->next;e->status=-1;e->done=1;s->pending--;if(!e->wait)free(e);}
  s->tail=&s->head;s->fd=-1;pthread_cond_broadcast(&s->changed);pthread_mutex_unlock(&s->lock);
  mlu_callback_release(s);return 0;
}
