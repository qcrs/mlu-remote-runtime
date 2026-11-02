/* Native pointers never cross the wire. These semantic adapters are shared by
 * generated wrappers/dispatch; lifecycle and reverse relocation stay manual. */
static int backend_memory_attribute(mlu_backend_session *s,
    const mlu_api_memory_attribute_request *in, mlu_api_memory_attribute_response *out) {
  mlu_backend_allocation *a=find_alloc(s,in->allocation); CNaddr p=0;
  if(!a || in->offset>=a->bytes || range(a,in->offset,1,&p)) return CN_MEMORY_ERROR_INVALID_ADDRESS;
  union { uint64_t u; CNcontext context; CNaddr address; int integer; unsigned char boolean; } value={0};
  int st=cnGetMemAttribute(&value,(CNmem_attribute)in->attr,p); if(st) return st;
  out->width=4;
  switch(in->attr) {
    case CN_MEM_ATTRIBUTE_DEVICE_POINTER:
      if(value.address!=p) return MLU_REMOTE_UNSUPPORTED;
      out->value=in->offset; out->width=8; break;
    case CN_MEM_ATTRIBUTE_HOST_POINTER:
      if(value.u) return MLU_REMOTE_UNSUPPORTED;
      out->value=0; out->width=8; break;
    case CN_MEM_ATTRIBUTE_CONTEXT:
      if(value.context!=s->context) return MLU_REMOTE_BAD_OBJECT;
      out->value=1; out->width=8; break;
    case CN_MEM_ATTRIBUTE_RANGE_START_ADDR:
      if(value.address!=a->address) return MLU_REMOTE_UNSUPPORTED;
      out->value=0; out->width=8; break;
    case CN_MEM_ATTRIBUTE_RANGE_SIZE: out->value=value.u; out->width=8; break;
    case CN_MEM_ATTRIBUTE_MAPPED: out->value=value.boolean; out->width=1; break;
    case CN_MEM_ATTRIBUTE_TYPE: case CN_MEM_ATTRIBUTE_IS_CACHE_OPS:
    case CN_MEM_ATTRIBUTE_DEVICE_ORDINAL: case CN_MEM_ATTRIBUTE_HOST_CACHE_TYPE:
    case CN_MEM_ATTRIBUTE_ALLOWED_HANDLE_TYPES: case CN_MEM_ATTRIBUTE_ISLINEAR:
      out->value=(uint32_t)value.integer; break;
    default: return MLU_REMOTE_UNSUPPORTED;
  }
  return 0;
}
static int backend_pointer_attributes(mlu_backend_session *s,
    const mlu_api_pointer_attributes_request *in, mlu_api_pointer_attributes_response *out) {
  mlu_backend_allocation *a=find_alloc(s,in->allocation); CNaddr p=0;
  if(!a || in->offset>=a->bytes || range(a,in->offset,1,&p)) return CN_MEMORY_ERROR_INVALID_ADDRESS;
  cnrtPointerAttributes_t v={0}; int st=cnrtPointerGetAttributes(&v,(void *)(uintptr_t)p); if(st) return st;
  if(v.devicePointer!=(void *)(uintptr_t)p || v.deviceBasePointer!=(void *)(uintptr_t)a->address || v.hostPointer)
    return MLU_REMOTE_UNSUPPORTED;
  out->type=v.type; out->device=v.device; out->bytes=v.size; out->cache=v.cacheMode; return 0;
}
static int backend_queue_attribute_set(mlu_backend_session *s,
    const mlu_api_queue_attribute_set_request *in, mlu_api_queue_attribute_set_response *out) {
  (void)out;
  mlu_backend_queue *q=find_queue(s,in->queue); if(!q) return CN_QUEUE_ERROR_INVALID;
  if(in->attr!=CN_QUEUE_ATTRIBUTE_ACCESS_POLICY_WINDOW) return CN_ERROR_INVALID_VALUE;
  CNaddr base=0;
  if(in->allocation) {
    mlu_backend_allocation *a=find_alloc(s,in->allocation);
    if(!a || in->offset>=a->bytes || range(a,in->offset,in->bytes,&base)) return CN_MEMORY_ERROR_INVALID_ADDRESS;
  } else if(in->offset || in->bytes) return CN_ERROR_INVALID_VALUE;
  CNqueueAttrValue value={.accessPolicyWindow={base,in->bytes,in->ratio,in->hit,in->miss}};
  return cnQueueSetAttribute(q->handle,(CNqueueAttrID)in->attr,&value);
}
static int backend_queue_attribute_get(mlu_backend_session *s,
    const mlu_api_queue_attribute_get_request *in, mlu_api_queue_attribute_get_response *out) {
  mlu_backend_queue *q=find_queue(s,in->queue); if(!q) return CN_QUEUE_ERROR_INVALID;
  if(in->attr!=CN_QUEUE_ATTRIBUTE_ACCESS_POLICY_WINDOW) return CN_ERROR_INVALID_VALUE;
  CNqueueAttrValue value={0}; int st=cnQueueGetAttribute(q->handle,(CNqueueAttrID)in->attr,&value); if(st) return st;
  CNaccessPolicyWindow *v=&value.accessPolicyWindow;
  if(v->baseAddr) {
    mlu_backend_allocation *found=NULL;
    for(size_t i=0;i<MLU_BACKEND_MAX_OBJECTS;++i) {
      mlu_backend_allocation *a=&s->allocations[i];
      if(a->live && a->owner==s->owner_token && v->baseAddr>=a->address && v->baseAddr-a->address<a->bytes &&
         v->numBytes<=a->bytes-(v->baseAddr-a->address)) { found=a; break; }
    }
    if(!found) return MLU_REMOTE_BAD_OBJECT;
    out->allocation=found->id; out->offset=v->baseAddr-found->address;
  }
  out->bytes=v->numBytes; out->ratio=v->hitRatio; out->hit=v->hitProp; out->miss=v->missProp; return 0;
}
static int backend_queue_attribute_copy(mlu_backend_session *s,
    const mlu_api_queue_attribute_copy_request *in, mlu_api_queue_attribute_copy_response *out) {
  (void)out;
  mlu_backend_queue *dst=find_queue(s,in->queue), *src=find_queue(s,in->src);
  if(!dst || !src) return CN_QUEUE_ERROR_INVALID;
  return cnQueueCopyAttributes(dst->handle,src->handle);
}
