#include <limits.h>
#include <errno.h>
#include <stdatomic.h>
#include <sys/stat.h>
typedef struct mlu_graph_user {
  struct mlu_graph_user *next;
  _Atomic unsigned refs;_Atomic int destroyed;
  uint64_t id,token,caller;
  CNuserObject handle;mlu_callback_service *service;
} mlu_graph_user;
static void graph_user_drop(mlu_graph_user *u) {if(atomic_fetch_sub(&u->refs,1)==1)free(u);}
static void graph_user_destructor(void *opaque) {
  mlu_graph_user *u=opaque;mlu_callback_service *service=u->service;
  /* Never block inside the native destructor; the channel owns the event. */
  (void)mlu_callback_exchange(service,MLU_CALLBACK_DESTROY,0,u->token,NULL,0,0);
  atomic_store(&u->destroyed,1);mlu_callback_user_done(service);graph_user_drop(u);
}
static void graph_users_reap(mlu_backend_session *s) {
  mlu_graph_user **link=&s->graph_users;
  while(*link) {mlu_graph_user *u=*link;if(!atomic_load(&u->destroyed)){link=&u->next;continue;}
    *link=u->next;mlu_object_registry_release(&s->objects,MLU_OBJECT_USER_OBJECT,u->id);graph_user_drop(u);}
}
static int graph_users_cleanup(mlu_backend_session *s) {
  int first=0;
  while(s->graph_users) {mlu_graph_user *u=s->graph_users;s->graph_users=u->next;
    if(u->caller) {int st=cnUserObjectRelease(u->handle,(unsigned)u->caller);if(st && !first)first=st;}
    mlu_object_registry_release(&s->objects,MLU_OBJECT_USER_OBJECT,u->id);graph_user_drop(u);
  }
  return first;
}
static mlu_graph_user *graph_user_find(mlu_backend_session *s,uint64_t id) {
  graph_users_reap(s);if(!mlu_object_registry_contains(&s->objects,MLU_OBJECT_USER_OBJECT,id))return NULL;
  for(mlu_graph_user *u=s->graph_users;u;u=u->next)if(u->id==id)return u;
  return NULL;
}
static int graph_control(mlu_backend_session *s,const mlu_api_graph_control_request *in,mlu_api_graph_control_response *out) {
  if(in->action==1) {
    if(s->callbacks || in->input_bytes || in->capacity)return CN_ERROR_INVALID_VALUE;
    uint64_t secret[2];s->callbacks=mlu_callback_create(s->transport_fd,s->transport_timeout_ms ? s->transport_timeout_ms : 120000,secret);
    if(!s->callbacks)return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
    out->id=secret[0];out->token=secret[1];return 0;
  }
  if(in->action==12 || in->action==13)return graph_callbacks(s,in,out);
  int g=graph_index(s,in->graph),n=graph_node_index(s,in->node);
  if(in->action>=2 && in->action<=5) {
    mlu_graph_entity *e=in->action==5 ? graph_find_entity(s,in->entity) : NULL;size_t slot=0;
    if(in->action==2) {
      if(g<0 || in->input_bytes%8 || in->input_bytes/8>MLU_GRAPH_MAX_NODES)return CN_ERROR_INVALID_VALUE;
    } else if(in->action==5) {
      if(!e)return CN_ERROR_INVALID_VALUE;
      while(slot<e->count && e->source_ids[slot]!=in->node)slot++;
      if(slot==e->count || e->host_slots[slot]<0)return CN_ERROR_INVALID_VALUE;
      mlu_graph_payload *old=e->revision->entries[e->host_slots[slot]].payload;
      if(!old || !old->callback_token)return CN_ERROR_INVALID_VALUE;
    } else if(n<0 || !s->graph_nodes[n].payload || !s->graph_nodes[n].payload->callback_token)return CN_ERROR_INVALID_VALUE;
    if(in->action==3) {out->token=s->graph_nodes[n].payload->callback_token;return 0;}
    if(!s->callbacks || !in->token)return CN_ERROR_INVALID_VALUE;
    mlu_graph_payload *p=calloc(1,sizeof(*p));if(!p)return CN_ERROR_SYSTEM_OUT_OF_MEMORY;p->refs=1;p->callback_token=in->token;
    CN_HOST_NODE_PARAMS params={graph_callback_placeholder,NULL};int st=0;
    if(in->action==2) {
      CNtaskTopoNode deps[MLU_GRAPH_MAX_NODES];size_t count=in->input_bytes/8;
      for(size_t i=0;i<count;i++) {int d=graph_node_index(s,gk_u64(in->data+8*i));if(d<0 || s->graph_nodes[d].parent!=in->graph){st=CN_ERROR_INVALID_VALUE;break;}deps[i]=s->graph_nodes[d].handle;}
      int idx=-1;if(!st){idx=graph_reserve(s,1);if(idx<0)st=MLU_REMOTE_RESOURCE_LIMIT;}
      if(!st) {s->graph_nodes[idx].parent=in->graph;st=cnTaskTopoAddHostNode(&s->graph_nodes[idx].handle,s->graphs[g].handle,deps,count,&params);}
      if(!st){s->graph_nodes[idx].payload=p;out->id=s->graph_nodes[idx].id;}
      else if(idx>=0)graph_forget_node(s,(unsigned)idx);
    } else if(in->action==4) {
      st=cnTaskTopoHostNodeSetParams(s->graph_nodes[n].handle,&params);
      if(!st){graph_payload_release(s->graph_nodes[n].payload);s->graph_nodes[n].payload=p;}
    } else {
      st=cnTaskTopoEntityHostNodeSetParams(e->handle,e->nodes[slot],&params);
      if(!st){graph_payload_release(e->overrides[slot]);e->overrides[slot]=p;}
    }
    if(st)graph_payload_release(p);
    return st;
  }
  if(in->action==6) {
    graph_users_reap(s);unsigned count=0;for(mlu_graph_user *u=s->graph_users;u;u=u->next)count++;
    if(count==MLU_BACKEND_MAX_OBJECTS)return MLU_REMOTE_RESOURCE_LIMIT;
    if(!s->callbacks || !in->token || !in->value || in->value>INT_MAX || in->flags!=1)return CN_ERROR_INVALID_VALUE;
    mlu_graph_user *u=calloc(1,sizeof(*u));if(!u)return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
    atomic_init(&u->refs,2);atomic_init(&u->destroyed,0);u->service=s->callbacks;u->token=in->token;u->caller=in->value;
    uint32_t generation;int st=register_object(s,MLU_OBJECT_USER_OBJECT,&u->id,&generation);if(st){free(u);return st;}
    mlu_callback_user_add(u->service);st=cnUserObjectCreate(&u->handle,u,graph_user_destructor,(unsigned)in->value,in->flags);
    if(st){mlu_callback_user_done(u->service);mlu_object_registry_release(&s->objects,MLU_OBJECT_USER_OBJECT,u->id);free(u);return st;}
    u->next=s->graph_users;s->graph_users=u;out->id=u->id;return 0;
  }
  if(in->action>=7 && in->action<=10) {
    mlu_graph_user *u=graph_user_find(s,in->node);
    if(!u || !in->value || in->value>INT_MAX)return CN_ERROR_INVALID_VALUE;
    if(in->action>=9 && g<0)return CN_ERROR_INVALID_VALUE;
    unsigned count=(unsigned)in->value;int st;
    if(in->action==7) {if(in->value>INT_MAX-u->caller)return CN_ERROR_INVALID_VALUE;st=cnUserObjectAcquire(u->handle,count);if(!st)u->caller+=count;}
    else if(in->action==8) {if(count>u->caller)return CN_ERROR_INVALID_VALUE;st=cnUserObjectRelease(u->handle,count);if(!st)u->caller-=count;}
    else if(in->action==9) {if(in->flags>1 || (in->flags && count>u->caller))return CN_ERROR_INVALID_VALUE;
      st=cnTaskTopoAcquireUserObject(s->graphs[g].handle,u->handle,count,in->flags);if(!st && in->flags)u->caller-=count;}
    else st=cnTaskTopoReleaseUserObject(s->graphs[g].handle,u->handle,count);
    return st;
  }
  if(in->action==11) {
    if(g<0 || in->input_bytes || !in->capacity)return CN_ERROR_INVALID_VALUE;
    char path[]="/tmp/mlur-dot-XXXXXX";int fd=mkstemp(path);if(fd<0)return CN_ERROR_OPERATING_SYSTEM;
    int st=cnTaskTopoDebugDotPrint(s->graphs[g].handle,path,in->flags);struct stat sb;
    if(!st && (fstat(fd,&sb) || sb.st_size<0))st=CN_ERROR_OPERATING_SYSTEM;
    if(!st && (uint64_t)sb.st_size>in->capacity)st=MLU_REMOTE_RESOURCE_LIMIT;
    if(!st) {out->bytes=(uint64_t)sb.st_size;size_t offset=0;
      while(offset<out->bytes) {ssize_t nread=pread(fd,out->data+offset,out->bytes-offset,(off_t)offset);if(nread<0 && errno==EINTR)continue;if(nread<=0){st=CN_ERROR_OPERATING_SYSTEM;break;}offset+=(size_t)nread;}}
    close(fd);unlink(path);return st;
  }
  return CN_ERROR_INVALID_VALUE;
}
