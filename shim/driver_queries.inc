/* Queries may name an allocation or queue from a context different from TLS.
 * Route by owner, then verify the native result before publishing a token. */
static allocation *query_allocation(CNaddr ptr,uint64_t *off) {
  for(allocation *a=allocations;a;a=a->next) if(a->live && a->owner->live &&
      ptr>=(uintptr_t)a->base && ptr-(uintptr_t)a->base<a->bytes) {
    *off=ptr-(uintptr_t)a->base; return a;
  }
  return NULL;
}
static allocation *allocation_by_id(context *c,uint64_t id) {
  for(allocation *a=allocations;a;a=a->next) if(a->live && a->owner==c && a->id==id) return a;
  return NULL;
}
static object *query_queue(CNqueue queue) {
  for(object *q=objects;q;q=q->next) if(q->live && q->owner->live && q->kind==QUEUE && q->token==(uintptr_t)queue) return q;
  return NULL;
}
static void invalid_memory_attribute(void *pi,CNmem_attribute attr) {
  if(attr==CN_MEM_ATTRIBUTE_TYPE) { int unknown=CN_MEMORYTYPE_UNKNOWN; memcpy(pi,&unknown,4); }
  if(attr==CN_MEM_ATTRIBUTE_HOST_POINTER || attr==CN_MEM_ATTRIBUTE_DEVICE_POINTER) { uint64_t zero=0; memcpy(pi,&zero,8); }
}
static int publish_memory_attribute(void *pi,CNmem_attribute attr,CNaddr ptr,allocation *a,
                                    const mlu_api_memory_attribute_response *out) {
  uint64_t v=out->value; unsigned width=4;
  switch(attr) {
    case CN_MEM_ATTRIBUTE_DEVICE_POINTER: if(v!=ptr-(uintptr_t)a->base) return -1; v=ptr; width=8; break;
    case CN_MEM_ATTRIBUTE_HOST_POINTER: if(v) return -1; width=8; break;
    case CN_MEM_ATTRIBUTE_CONTEXT: if(v!=1) return -1; v=a->owner->token; width=8; break;
    case CN_MEM_ATTRIBUTE_RANGE_START_ADDR: if(v) return -1; v=(uintptr_t)a->base; width=8; break;
    case CN_MEM_ATTRIBUTE_RANGE_SIZE: width=8; break;
    case CN_MEM_ATTRIBUTE_MAPPED: width=1; break;
    case CN_MEM_ATTRIBUTE_TYPE: case CN_MEM_ATTRIBUTE_IS_CACHE_OPS:
    case CN_MEM_ATTRIBUTE_DEVICE_ORDINAL: case CN_MEM_ATTRIBUTE_HOST_CACHE_TYPE:
    case CN_MEM_ATTRIBUTE_ALLOWED_HANDLE_TYPES: case CN_MEM_ATTRIBUTE_ISLINEAR: break;
    default: return -1;
  }
  if(width!=out->width) return -1;
  /* Convert to the host ABI width before publishing. */
  if(width==1) { unsigned char x=v; memcpy(pi,&x,1); }
  else if(width==4) { uint32_t x=v; memcpy(pi,&x,4); }
  else memcpy(pi,&v,8);
  return 0;
}
CNresult cnGetMemAttributes(unsigned numAttributes,CNmem_attribute *attributes,void **data,CNaddr ptr) {
  if(!numAttributes || !attributes || !data) return CN_ERROR_INVALID_VALUE;
  /* This SDK attempts every entry and returns the final entry's status. */
  CNresult st=CN_SUCCESS;
  for(unsigned i=0;i<numAttributes;++i) {
    st=cnGetMemAttribute(data[i],attributes[i],ptr);
  }
  return st;
}
