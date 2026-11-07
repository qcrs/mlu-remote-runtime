/* Client reservations are inaccessible VA tokens; only mapped views carry IDs. */
typedef struct driver_vmm_range {
  struct driver_vmm_range *next;
  context *owner;
  CNaddr base;
  uint64_t id,bytes;
  void *raw;
  size_t mapped;
  int live;
} driver_vmm_range;
static driver_vmm_range *vmm_ranges;
static void vmm_invalidate(context *c) {
  for(driver_vmm_range *r=vmm_ranges;r;r=r->next)if(r->owner==c)r->live=0;
}
static int vmm_reserved(CNaddr p,uint64_t bytes) {
  for(driver_vmm_range *r=vmm_ranges;r;r=r->next)
    if(p>=r->base && p-r->base<=r->bytes)return 1;
  return 0;
}
static driver_vmm_range *vmm_find_range(context *c,CNaddr addr,uint64_t bytes) {
  for(driver_vmm_range *r=vmm_ranges;r;r=r->next)
    if(r->live && r->owner==c && addr>=r->base && addr-r->base<=r->bytes && bytes<=r->bytes-(addr-r->base))return r;
  return NULL;
}
static void vmm_encode_prop(uint32_t *v,const CNmemAllocationProp *p) {
  v[0]=p->type;v[1]=p->requestedHandleTypes;v[2]=p->location.type;v[3]=p->location.id;
  v[4]=p->allocFlags.compressionType;v[5]=p->allocFlags.securityType;
}
CNresult cnMemGetAllocationGranularity(cn_uint64_t *out,CNmemAllocationProp *p,CNmemAllocationGranularity mode) {
  ENTER();CURRENT();if(!out || !p)RETURN(CN_ERROR_INVALID_VALUE);
  mlu_api_vmm_request in={.action=1,.flags=mode};mlu_api_vmm_response value={0};vmm_encode_prop(in.values,p);
  int st=mlu_remote_api_vmm(c->remote,&in,&value);if(!st)*out=value.bytes;RETURN(result(st));
}
CNresult cnMemCreate(CNmemGenericAllocationHandle *out,cn_uint64_t bytes,CNmemAllocationProp *p,cn_uint64_t flags) {
  ENTER();CURRENT();if(!out || !p)RETURN(CN_ERROR_INVALID_VALUE);
  object *h=new_object(c,5);if(!h)RETURN(CN_ERROR_SYSTEM_OUT_OF_MEMORY);
  mlu_api_vmm_request in={.action=2,.bytes=bytes,.flags=flags};mlu_api_vmm_response value={0};vmm_encode_prop(in.values,p);
  int st=mlu_remote_api_vmm(c->remote,&in,&value);if(st){discard_object(h);RETURN(result(st));}
  h->id=value.id;h->size=1;h->live=1;*out=h->token;RETURN(CN_SUCCESS);
}
CNresult cnMemRelease(CNmemGenericAllocationHandle handle) {
  ENTER();CURRENT();object *h=find_object(c,5,handle);if(!h || !h->size)RETURN(CN_ERROR_INVALID_VALUE);
  mlu_api_vmm_request in={.action=3,.object=h->id};mlu_api_vmm_response value={0};int st=mlu_remote_api_vmm(c->remote,&in,&value);
  if(!st)h->size--;RETURN(result(st));
}
CNresult cnMemGetAllocationPropertiesFromHandle(CNmemAllocationProp *p,CNmemGenericAllocationHandle handle) {
  ENTER();CURRENT();object *h=find_object(c,5,handle);if(!p || !h || !h->size)RETURN(CN_ERROR_INVALID_VALUE);
  mlu_api_vmm_request in={.action=4,.object=h->id};mlu_api_vmm_response value={0};int st=mlu_remote_api_vmm(c->remote,&in,&value);
  if(!st){memset(p,0,sizeof(*p));p->type=value.values[0];p->requestedHandleTypes=value.values[1];p->location.type=value.values[2];p->location.id=value.values[3];p->allocFlags.compressionType=value.values[4];p->allocFlags.securityType=value.values[5];}RETURN(result(st));
}
CNresult cnMemAddressReserve(CNaddr *out,cn_uint64_t bytes,cn_uint64_t alignment,CNaddr hint,cn_uint64_t flags) {
  ENTER();CURRENT();if(!out || !bytes || bytes>SIZE_MAX/2)RETURN(CN_ERROR_INVALID_VALUE);
  CNmemAllocationProp p={.type=CN_MEM_ALLOCATION_TYPE_DEFAULT,.location={CN_MEM_LOCATION_TYPE_DEVICE,c->device}};
  mlu_api_vmm_request gi={.action=1};mlu_api_vmm_response gr={0};vmm_encode_prop(gi.values,&p);
  int st=mlu_remote_api_vmm(c->remote,&gi,&gr);if(st)RETURN(result(st));
  uint64_t align=alignment ? alignment : gr.bytes;
  if(!align || align&(align-1) || align<gr.bytes || bytes%gr.bytes || align>SIZE_MAX-bytes-4096 || (hint && hint%align))RETURN(CN_ERROR_INVALID_VALUE);
  driver_vmm_range *r=calloc(1,sizeof(*r));if(!r)RETURN(CN_ERROR_SYSTEM_OUT_OF_MEMORY);
  r->mapped=bytes+align+4096;
  r->raw=mmap(hint ? (void*)(uintptr_t)hint : NULL,r->mapped,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
  if(r->raw==MAP_FAILED){free(r);RETURN(CN_ERROR_SYSTEM_OUT_OF_MEMORY);}
  r->base=((uintptr_t)r->raw+align-1)&~(align-1);
  if(hint && r->base!=hint){munmap(r->raw,r->mapped);free(r);RETURN(CN_MEMORY_ERROR_OUT_OF_MEMORY);}
  mlu_api_vmm_request in={.action=5,.bytes=bytes,.alignment=alignment,.flags=flags};mlu_api_vmm_response value={0};
  st=mlu_remote_api_vmm(c->remote,&in,&value);if(st){munmap(r->raw,r->mapped);free(r);RETURN(result(st));}
  r->owner=c;r->bytes=bytes;r->id=value.id;r->live=1;r->next=vmm_ranges;vmm_ranges=r;*out=r->base;RETURN(CN_SUCCESS);
}
CNresult cnMemAddressFree(CNaddr addr,cn_uint64_t bytes) {
  ENTER();CURRENT();driver_vmm_range *r=vmm_find_range(c,addr,bytes);if(!r || r->base!=addr || r->bytes!=bytes)RETURN(CN_ERROR_INVALID_VALUE);
  mlu_api_vmm_request in={.action=6,.object=r->id,.bytes=bytes};mlu_api_vmm_response value={0};int st=mlu_remote_api_vmm(c->remote,&in,&value);
  if(!st)r->live=0;RETURN(result(st));
}
CNresult cnMemMap(CNaddr addr,cn_uint64_t bytes,cn_uint64_t offset,CNmemGenericAllocationHandle handle,cn_uint64_t flags) {
  ENTER();CURRENT();driver_vmm_range *r=vmm_find_range(c,addr,bytes);object *h=find_object(c,5,handle);
  if(!r || !h || !h->size)RETURN(CN_ERROR_INVALID_VALUE);
  allocation *a=calloc(1,sizeof(*a));if(!a)RETURN(CN_ERROR_SYSTEM_OUT_OF_MEMORY);
  mlu_api_vmm_request in={.action=7,.object=r->id,.other=h->id,.offset=addr-r->base,.bytes=bytes,.alignment=offset,.flags=flags};mlu_api_vmm_response value={0};
  int st=mlu_remote_api_vmm(c->remote,&in,&value);if(st){free(a);RETURN(result(st));}
  a->owner=c;a->base=(void*)(uintptr_t)addr;a->bytes=bytes;a->id=value.id;a->live=1;a->vmm=1;a->next=allocations;allocations=a;RETURN(CN_SUCCESS);
}
CNresult cnMemUnmap(CNaddr addr,cn_uint64_t bytes) {
  ENTER();CURRENT();uint64_t off;allocation *a=lookup(c,addr,&off);if(!a || !a->vmm || off || a->bytes!=bytes)RETURN(CN_ERROR_INVALID_VALUE);
  mlu_api_vmm_request in={.action=8,.object=a->id,.bytes=bytes};mlu_api_vmm_response value={0};int st=mlu_remote_api_vmm(c->remote,&in,&value);
  if(!st)a->live=0;RETURN(result(st));
}
CNresult cnMemSetAccess(CNaddr addr,cn_uint64_t bytes,CNmemAccessDesc *d,cn_uint64_t count) {
  ENTER();CURRENT();mlu_api_vmm_request in={.action=9,.bytes=bytes,.flags=count};mlu_api_vmm_response value={0};
  if(!d || count!=1 || bounds(c,addr,bytes,&in.object,&in.offset))RETURN(CN_ERROR_INVALID_VALUE);
  in.values[0]=d->location.type;in.values[1]=d->location.id;in.values[2]=d->accessFlags;in.values[3]=d->cacheType;in.values[4]=d->pinnedType;
  RETURN(result(mlu_remote_api_vmm(c->remote,&in,&value)));
}
CNresult cnMemGetAccess(CNmemAccessDesc *d,const CNmemLocation *location,void *addr) {
  ENTER();CURRENT();mlu_api_vmm_request in={.action=10};mlu_api_vmm_response value={0};
  if(!d || !location || bounds(c,(uintptr_t)addr,1,&in.object,&in.offset))RETURN(CN_ERROR_INVALID_VALUE);
  in.values[0]=location->type;in.values[1]=location->id;int st=mlu_remote_api_vmm(c->remote,&in,&value);
  if(!st){memset(d,0,sizeof(*d));d->location.type=value.values[0];d->location.id=value.values[1];d->accessFlags=value.values[2];d->cacheType=value.values[3];d->pinnedType=value.values[4];}RETURN(result(st));
}
CNresult cnMemRetainAllocationHandle(CNmemGenericAllocationHandle *out,CNaddr addr) {
  ENTER();CURRENT();mlu_api_vmm_request in={.action=11};mlu_api_vmm_response value={0};
  if(!out || bounds(c,addr,1,&in.object,&in.offset))RETURN(CN_ERROR_INVALID_VALUE);
  int st=mlu_remote_api_vmm(c->remote,&in,&value);if(st)RETURN(result(st));
  for(object *h=objects;h;h=h->next)if(h->live && h->owner==c && h->kind==5 && h->id==value.id){h->size++;*out=h->token;RETURN(CN_SUCCESS);}
  RETURN(CN_ERROR_OPERATING_SYSTEM);
}
