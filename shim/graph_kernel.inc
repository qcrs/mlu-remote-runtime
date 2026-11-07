/* Shared Graph kernel ABI adapter. Only compiler-classified pointer slots are
 * relocated. Getter storage belongs to the node and follows its ancestry. */
#include "graph_kernel_wire.h"
#ifdef MLU_GRAPH_RUNTIME
#define GK_OWNER void *owner
#define GK_PARAMS cnrtKernelNodeParams_t
#define GK_SESSION ctx
#define GK_X(p) ((p)->dim.x)
#define GK_Y(p) ((p)->dim.y)
#define GK_Z(p) ((p)->dim.z)
#define GK_CLASS(p) ((p)->type)
#else
#define GK_OWNER context *owner
#define GK_PARAMS CN_KERNEL_NODE_PARAMS
#define GK_SESSION owner->remote
#define GK_X(p) ((p)->dimx)
#define GK_Y(p) ((p)->dimy)
#define GK_Z(p) ((p)->dimz)
#define GK_CLASS(p) ((p)->c)
#endif
/* Driver status numbers are mapped by the common public-library status path. */
#define GK_INVALID 100006
#define GK_NOMEM 100010
#define GK_UNSUPPORTED 100050
typedef struct graph_kernel_identity {
  struct graph_kernel_identity *next;
  mlu_remote_context *session;
  uint64_t id;
  uintptr_t function;
} graph_kernel_identity;
static graph_kernel_identity *graph_kernel_identities;
typedef struct graph_kernel_cache {
  struct graph_kernel_cache *next;
  mlu_remote_context *session;
  uint64_t node,ancestors[128];
  mlu_api_graph_kernel_response info;
  unsigned char *wire,*storage;
  void *values[BANG_MAX_ARGS];
} graph_kernel_cache;
static graph_kernel_cache *graph_kernel_caches;
static void graph_kernel_cache_drop(mlu_remote_context *session,uint64_t id) {
  graph_kernel_cache **link=&graph_kernel_caches;
  while(*link) {
    graph_kernel_cache *c=*link;int found=!id;
    for(unsigned i=0;i<128 && !found;i++) found=c->ancestors[i]==id;
    if(c->session!=session || !found) {link=&c->next;continue;}
    *link=c->next;free(c->wire);free(c->storage);free(c);
  }
  if(!id) {
    graph_kernel_identity **identity=&graph_kernel_identities;
    while(*identity) {graph_kernel_identity *k=*identity;if(k->session!=session) {identity=&k->next;continue;}*identity=k->next;free(k);}
  }
}
static int graph_kernel_remember(mlu_remote_context *session,uint64_t id,uintptr_t function) {
  graph_kernel_identity *v;
  for(v=graph_kernel_identities;v;v=v->next) if(v->session==session && v->id==id) return 0;
  v=calloc(1,sizeof(*v));if(!v) return GK_NOMEM;
  v->session=session;v->id=id;v->function=function;v->next=graph_kernel_identities;graph_kernel_identities=v;return 0;
}
static int graph_kernel_encode(GK_OWNER,const GK_PARAMS *p,const void *dependencies,size_t count,mlu_api_graph_kernel_request *out) {
  if(count>MLU_GRAPH_MAX_NODES || (count && !dependencies) || p->reserve || p->extra ||
     !GK_X(p) || !GK_Y(p) || !GK_Z(p)) return GK_INVALID;
  const bang_param_layout *layout;const bang_param_types *types;uintptr_t function;
#ifdef MLU_GRAPH_RUNTIME
  kernel *k;for(k=kernels;k;k=k->next) if(k->host==p->func && k->reg->live) break;
  if(!k) return 100302;
  if(!k->valid || k->reg->unsupported) return GK_UNSUPPORTED;
  int st=0;
  if(!k->reg->mid) st=mlu_remote_module_load_image(ctx,k->reg->image,k->reg->size,k->name,&k->reg->mid,&k->kid);
  else if(!k->kid) st=mlu_remote_module_get_kernel(ctx,k->reg->mid,k->name,&k->kid);
  if(st) return st;
  out->module=k->reg->mid;out->kernel=k->kid;layout=&k->layout;types=&k->types;function=(uintptr_t)p->func;
#else
  object *k=find_object(owner,KERNEL,(uintptr_t)p->hkernel);
  object *m=k ? find_object(owner,MODULE,k->parent) : NULL;
  if(!k || !m) return CN_ERROR_INVALID_KERNEL;
  out->module=m->id;out->kernel=k->id;layout=&k->layout;types=&k->types;function=(uintptr_t)p->hkernel;
#endif
  mlu_remote_kernel_arg args[BANG_MAX_ARGS];mlu_remote_kernel_relocation relocations[BANG_MAX_RELOCS];
  if(bang_pack_args(layout,types,p->kernelParams,resolve_kernel_pointer,owner,args,relocations)) return GK_INVALID;
  size_t bytes=8*count;
  for(unsigned i=0;i<layout->count;i++) bytes+=32+args[i].size+24*args[i].relocation_count;
  if(bytes>MLU_GRAPH_KERNEL_BYTES) return GK_UNSUPPORTED;
  unsigned char *data=calloc(bytes ? bytes : 1,1);if(!data) return GK_NOMEM;
  /* Handles have pointer width on the declared AArch64 ABI; copy, don't alias. */
  for(size_t i=0;i<count;i++) {uintptr_t id=0;memcpy(&id,(const unsigned char *)dependencies+i*sizeof(void *),sizeof(id));gk_put64(data+8*i,id);}
  size_t cursor=8*count;
  for(unsigned i=0;i<layout->count;i++) {
    mlu_remote_kernel_arg *a=&args[i];unsigned char *v=data+cursor;
    gk_put32(v,a->kind);gk_put32(v+4,a->size);gk_put64(v+8,a->allocation);gk_put64(v+16,a->offset);gk_put32(v+24,a->relocation_count);
    if(a->kind!=1) memcpy(v+32,a->scalar,a->size);
    for(unsigned j=0;j<a->relocation_count;j++) {
      const mlu_remote_kernel_relocation *r=&a->relocations[j];unsigned char *dst=v+32+a->size+24*j;
      memset(v+32+r->offset,0,8);gk_put32(dst,r->offset);gk_put64(dst+8,r->allocation);gk_put64(dst+16,r->allocation_offset);
    }
    cursor+=32+a->size+24*a->relocation_count;
  }
  int remembered=graph_kernel_remember(GK_SESSION,out->kernel,function);
  if(remembered) {free(data);return remembered;}
  out->x=GK_X(p);out->y=GK_Y(p);out->z=GK_Z(p);out->klass=GK_CLASS(p);out->arg_count=layout->count;
  out->input_bytes=bytes;out->data=data;out->count=count;return 0;
}
static int graph_kernel_client_address(GK_OWNER,uint64_t id,uint64_t offset,uint64_t *value) {
  if(!id) {if(offset) return GK_INVALID;*value=0;return 0;}
  for(allocation *a=allocations;a;a=a->next) {
#ifndef MLU_GRAPH_RUNTIME
    if(a->owner!=owner) continue;
#endif
    if(a->id==id && offset<=a->bytes) {*value=(uintptr_t)a->base+offset;return 0;}
  }
  return GK_INVALID;
}
static int graph_kernel_decode(GK_OWNER,uint64_t node,const mlu_api_graph_kernel_response *in,GK_PARAMS *out) {
  if(in->bytes>MLU_GRAPH_KERNEL_BYTES) return GK_INVALID;
  graph_kernel_identity *identity;
  for(identity=graph_kernel_identities;identity;identity=identity->next) if(identity->session==GK_SESSION && identity->id==in->kernel) break;
  if(!identity) return GK_INVALID;
  graph_kernel_cache *cache;
  for(cache=graph_kernel_caches;cache;cache=cache->next) if(cache->session==GK_SESSION && cache->node==node) break;
  if(cache && (cache->info.bytes!=in->bytes || cache->info.kernel!=in->kernel || memcmp(cache->wire,in->data,in->bytes))) {
    graph_kernel_cache_drop(GK_SESSION,node);cache=NULL;
  }
  if(!cache) {
    gk_arg args[BANG_MAX_ARGS];if(gk_parse(in->data,in->bytes,in->arg_count,args)) return GK_INVALID;
    size_t storage=0;for(unsigned i=0;i<in->arg_count;i++) storage+=(args[i].size+15u)&~15u;
    cache=calloc(1,sizeof(*cache));if(!cache) return GK_NOMEM;
    cache->wire=malloc(in->bytes ? in->bytes : 1);cache->storage=calloc(storage ? storage : 1,1);
    if(!cache->wire || !cache->storage) {free(cache->wire);free(cache->storage);free(cache);return GK_NOMEM;}
    cache->session=GK_SESSION;cache->node=node;cache->info=*in;cache->info.data=NULL;memcpy(cache->ancestors,in->ancestors,sizeof(cache->ancestors));memcpy(cache->wire,in->data,in->bytes);
    size_t cursor=0;int st=0;
    for(unsigned i=0;i<in->arg_count && !st;i++) {
      gk_arg *a=&args[i];cache->values[i]=cache->storage+cursor;memcpy(cache->values[i],a->scalar,a->size);
      if(a->kind==1) {uint64_t value=0;st=graph_kernel_client_address(owner,a->allocation,a->offset,&value);memcpy(cache->values[i],&value,8);}
      for(unsigned j=0;j<a->relocations && !st;j++) {
        const unsigned char *r=a->relocs+24*j;uint64_t value=0;st=graph_kernel_client_address(owner,gk_u64(r+8),gk_u64(r+16),&value);memcpy((unsigned char *)cache->values[i]+gk_u32(r),&value,8);
      }
      cursor+=(a->size+15u)&~15u;
    }
    if(st) {free(cache->wire);free(cache->storage);free(cache);return st;}
    cache->next=graph_kernel_caches;graph_kernel_caches=cache;
  }
  memset(out,0,sizeof(*out));GK_X(out)=in->x;GK_Y(out)=in->y;GK_Z(out)=in->z;GK_CLASS(out)=in->klass;out->reserve=in->reserve;
#ifdef MLU_GRAPH_RUNTIME
  out->func=(void *)identity->function;
#else
  out->hkernel=(CNkernel)identity->function;
#endif
  out->kernelParams=in->arg_count ? cache->values : NULL;return 0;
}
#ifdef MLU_GRAPH_RUNTIME
#define GK_ATTR cnrtKernelNodeAttrValue_t
#else
#define GK_ATTR CNkernelNodeAttrValue
#endif
static int graph_kernel_attr_encode(GK_OWNER,const GK_ATTR *v,mlu_api_graph_kernel_attr_request *out) {
  uint64_t id=0,offset=0;
  if(v->accessPolicyWindow.baseAddr && resolve_kernel_pointer(owner,(uintptr_t)v->accessPolicyWindow.baseAddr,&id,&offset)) return GK_INVALID;
  out->allocation=id;out->offset=offset;out->bytes=v->accessPolicyWindow.numBytes;
  out->hit_ratio=v->accessPolicyWindow.hitRatio;out->hit=v->accessPolicyWindow.hitProp;out->miss=v->accessPolicyWindow.missProp;return 0;
}
static int graph_kernel_attr_decode(GK_OWNER,const mlu_api_graph_kernel_attr_response *in,GK_ATTR *v) {
  uint64_t address=0;int st=graph_kernel_client_address(owner,in->allocation,in->offset,&address);if(st) return st;
  memset(v,0,sizeof(*v));
#ifdef MLU_GRAPH_RUNTIME
  v->accessPolicyWindow.baseAddr=(void *)(uintptr_t)address;
#else
  v->accessPolicyWindow.baseAddr=address;
#endif
  v->accessPolicyWindow.numBytes=in->bytes;v->accessPolicyWindow.hitRatio=in->hit_ratio;
  v->accessPolicyWindow.hitProp=in->hit;v->accessPolicyWindow.missProp=in->miss;return 0;
}
#undef GK_ATTR
#undef GK_OWNER
#undef GK_PARAMS
#undef GK_SESSION
#undef GK_X
#undef GK_Y
#undef GK_Z
#undef GK_CLASS
#undef GK_INVALID
#undef GK_NOMEM
#undef GK_UNSUPPORTED
