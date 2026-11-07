/* Physical allocation handles, VA reservations and mappings have independent
 * lifetimes. Only mapped views enter the ordinary AllocationID registry. */
static mlu_vmm_handle *vmm_handle(mlu_backend_session *s,uint64_t id) {
  for(unsigned i=0;i<MLU_BACKEND_MAX_OBJECTS;i++)if(s->vmm_handles[i].live && s->vmm_handles[i].id==id)return &s->vmm_handles[i];return NULL;
}
static mlu_vmm_range *vmm_range(mlu_backend_session *s,uint64_t id) {
  for(unsigned i=0;i<MLU_BACKEND_MAX_OBJECTS;i++)if(s->vmm_ranges[i].live && s->vmm_ranges[i].id==id)return &s->vmm_ranges[i];return NULL;
}
static int vmm_reap(mlu_backend_session *s,mlu_vmm_handle *h) {
  if(h && !h->references && !h->mappings) {
    int st=ipc_vmm_drop(s,h);if(st)return st;
    mlu_object_registry_release(&s->objects,MLU_OBJECT_VMM_HANDLE,h->id);memset(h,0,sizeof(*h));
  }
  return 0;
}
static int vmm_unmap_allocation(mlu_backend_session *s,mlu_backend_allocation *a) {
  int st=cnMemUnmap(a->address,a->bytes);if(st)return st;
  mlu_vmm_handle *h=vmm_handle(s,a->vmm_handle);if(h && h->mappings)h->mappings--;
  mlu_object_registry_release(&s->objects,MLU_OBJECT_ALLOCATION,a->id);memset(a,0,sizeof(*a));return vmm_reap(s,h);
}
static int vmm_cleanup(mlu_backend_session *s) {
  for(unsigned i=0;i<MLU_BACKEND_MAX_OBJECTS;i++) {
    mlu_backend_allocation *a=&s->allocations[i];
    if(a->live && a->vmm_handle) {if(a->graph_refs)return CN_ERROR_INVALID_HANDLE;int st=vmm_unmap_allocation(s,a);if(st)return st;}
  }
  for(unsigned i=0;i<MLU_BACKEND_MAX_OBJECTS;i++) {
    mlu_vmm_handle *h=&s->vmm_handles[i];
    while(h->live && h->references){int st=cnMemRelease(h->handle);if(st)return st;h->references--;}
    int reaped=vmm_reap(s,h);if(reaped)return reaped;
    mlu_vmm_range *r=&s->vmm_ranges[i];if(r->live){int st=cnMemAddressFree(r->address,r->bytes);if(st)return st;r->live=0;}
  }
  return 0;
}
static CNmemAllocationProp vmm_prop(const uint32_t *v) {
  CNmemAllocationProp p={.type=v[0],.requestedHandleTypes=v[1],.location={v[2],v[3]}};
  p.allocFlags.compressionType=v[4];p.allocFlags.securityType=v[5];return p;
}
static void vmm_prop_values(uint32_t *v,const CNmemAllocationProp *p) {
  v[0]=p->type;v[1]=p->requestedHandleTypes;v[2]=p->location.type;v[3]=p->location.id;v[4]=p->allocFlags.compressionType;v[5]=p->allocFlags.securityType;
}
static int vmm_control(mlu_backend_session *s,const mlu_api_vmm_request *in,mlu_api_vmm_response *out) {
  mlu_vmm_handle *h=vmm_handle(s,in->object);mlu_vmm_range *r=vmm_range(s,in->object);
  CNmemAllocationProp prop=vmm_prop(in->values);int st;
  switch(in->action) {
    case 1: {cn_uint64_t g=0;st=cnMemGetAllocationGranularity(&g,&prop,in->flags);out->bytes=g;return st;}
    case 2: {
      mlu_vmm_handle *slot=NULL;for(unsigned i=0;i<MLU_BACKEND_MAX_OBJECTS;i++)if(!s->vmm_handles[i].live){slot=&s->vmm_handles[i];break;}
      if(!slot)return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
      if(mlu_resource_reserve(&s->usage,MLU_RESOURCE_DEVICE,in->bytes))return MLU_REMOTE_RESOURCE_LIMIT;
      CNmemGenericAllocationHandle handle=0;st=cnMemCreate(&handle,in->bytes,&prop,in->flags);
      if(st){mlu_resource_release(&s->usage,MLU_RESOURCE_DEVICE,in->bytes);return st;}
      uint64_t id;uint32_t generation;st=register_object(s,MLU_OBJECT_VMM_HANDLE,&id,&generation);
      if(st){cnMemRelease(handle);mlu_resource_release(&s->usage,MLU_RESOURCE_DEVICE,in->bytes);return st;}
      *slot=(mlu_vmm_handle){.id=id,.bytes=in->bytes,.handle=handle,.references=1,.live=1};out->id=id;return 0;
    }
    case 3:
      if(!h || !h->references)return CN_ERROR_INVALID_VALUE;
      st=cnMemRelease(h->handle);if(!st){h->references--;st=vmm_reap(s,h);}return st;
    case 4:
      if(!h || !h->references)return CN_ERROR_INVALID_VALUE;
      st=cnMemGetAllocationPropertiesFromHandle(&prop,h->handle);if(!st)vmm_prop_values(out->values,&prop);return st;
    case 5: {
      mlu_vmm_range *slot=NULL;for(unsigned i=0;i<MLU_BACKEND_MAX_OBJECTS;i++)if(!s->vmm_ranges[i].live){slot=&s->vmm_ranges[i];break;}
      if(!slot)return CN_ERROR_SYSTEM_OUT_OF_MEMORY;CNaddr address=0;
      /* Client VA hint is local only; it must never enter Server's VA space. */
      st=cnMemAddressReserve(&address,in->bytes,in->alignment,0,in->flags);if(st)return st;
      uint64_t id;uint32_t generation;st=register_object(s,MLU_OBJECT_VMM_RANGE,&id,&generation);
      if(st){cnMemAddressFree(address,in->bytes);return st;}
      *slot=(mlu_vmm_range){.id=id,.bytes=in->bytes,.address=address,.live=1};out->id=id;return 0;
    }
    case 6:
      if(!r || in->bytes!=r->bytes)return CN_ERROR_INVALID_VALUE;
      for(unsigned i=0;i<MLU_BACKEND_MAX_OBJECTS;i++){mlu_backend_allocation *a=&s->allocations[i];if(a->live && a->vmm_handle && a->address>=r->address && a->address-r->address<r->bytes)return CN_ERROR_INVALID_VALUE;}
      st=cnMemAddressFree(r->address,r->bytes);if(!st){r->live=0;mlu_object_registry_release(&s->objects,MLU_OBJECT_VMM_RANGE,r->id);}return st;
    case 7: {
      h=vmm_handle(s,in->other);
      if(!r || !h || !h->references || in->offset>r->bytes || in->bytes>r->bytes-in->offset)return CN_ERROR_INVALID_VALUE;
      mlu_backend_allocation *slot=NULL;for(unsigned i=0;i<MLU_BACKEND_MAX_OBJECTS;i++)if(!s->allocations[i].live){slot=&s->allocations[i];break;}
      if(!slot)return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
      CNaddr address=r->address+in->offset;st=cnMemMap(address,in->bytes,in->alignment,h->handle,in->flags);if(st)return st;
      uint64_t id;uint32_t generation;st=register_object(s,MLU_OBJECT_ALLOCATION,&id,&generation);
      if(st){cnMemUnmap(address,in->bytes);return st;}
      *slot=(mlu_backend_allocation){.id=id,.address=address,.bytes=in->bytes,.vmm_handle=h->id,.owner=s->owner_token,.generation=generation,.live=1};h->mappings++;out->id=id;return 0;
    }
    case 8: {
      mlu_backend_allocation *a=find_alloc(s,in->object);
      if(!a || !a->vmm_handle || in->offset || in->bytes!=a->bytes)return CN_ERROR_INVALID_VALUE;
      st=backend_context_wait(s);if(st)return st;
      if(a->graph_refs){a->retired=1;mlu_object_registry_release(&s->objects,MLU_OBJECT_ALLOCATION,a->id);return 0;}
      return vmm_unmap_allocation(s,a);
    }
    case 9: case 10: case 11: {
      mlu_backend_allocation *a=find_alloc(s,in->object);CNaddr addr;
      if(!a || range(a,in->offset,in->action==9 ? in->bytes : 0,&addr))return CN_ERROR_INVALID_VALUE;
      CNmemAccessDesc d={.location={in->values[0],in->values[1]},.accessFlags=in->values[2],.cacheType=in->values[3],.pinnedType=in->values[4]};
      if(in->action==9)return cnMemSetAccess(addr,in->bytes,&d,in->flags);
      if(in->action==10){CNmemLocation location=d.location;st=cnMemGetAccess(&d,&location,(void*)(uintptr_t)addr);if(!st){out->values[0]=d.location.type;out->values[1]=d.location.id;out->values[2]=d.accessFlags;out->values[3]=d.cacheType;out->values[4]=d.pinnedType;}return st;}
      h=vmm_handle(s,a->vmm_handle);if(!h || h->references==UINT_MAX)return CN_ERROR_INVALID_VALUE;
      CNmemGenericAllocationHandle native=0;st=cnMemRetainAllocationHandle(&native,addr);if(st)return st;
      if(native!=h->handle){cnMemRelease(native);return CN_OPS_ERROR_NOT_SUPPORTED;}h->references++;out->id=h->id;return 0;
    }
    case 12: case 13: {
      CNaddr address;mlu_backend_allocation *a=find_alloc(s,in->object);
      if(range(a,in->offset,in->action==13 ? in->bytes : 1,&address))return CN_ERROR_INVALID_VALUE;
      return in->action==12 ? cnSetMemAttribute(in->flags,in->values[0],address) : cnSetMemRangeAttribute(in->flags,in->values[0],address,in->bytes);
    }
    default:return CN_ERROR_INVALID_VALUE;
  }
}
