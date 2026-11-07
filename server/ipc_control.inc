static int ipc_control(mlu_backend_session *s,const mlu_api_ipc_request *in,mlu_api_ipc_response *out) {
  if(in->action==1) {
    mlu_backend_allocation *a=find_alloc(s,in->object);
    if(!a || a->ipc_imported || a->symbol_module || a->vmm_handle)return CN_ERROR_INVALID_VALUE;
    CNIpcMemHandle native=NULL;int st=cnIpcGetMemHandle(&native,a->address);if(st)return st;
    pthread_mutex_lock(&ipc_lock);
    if(!a->ipc)a->ipc=ipc_backing_new(s,a->bytes,0,a->address);
    if(!a->ipc)st=CN_ERROR_SYSTEM_OUT_OF_MEMORY;
    else {out->token=a->ipc->token;out->bytes=a->bytes;}
    pthread_mutex_unlock(&ipc_lock);return st;
  }
  if(in->action==2) {
    if(in->flags)return CN_ERROR_INVALID_VALUE;
    mlu_backend_allocation *a=NULL;for(unsigned i=0;i<MLU_BACKEND_MAX_OBJECTS;i++)if(!s->allocations[i].live){a=&s->allocations[i];break;}
    if(!a)return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
    uint64_t id;uint32_t generation;int st=register_object(s,MLU_OBJECT_ALLOCATION,&id,&generation);if(st)return st;
    pthread_mutex_lock(&ipc_lock);mlu_ipc_backing *b=ipc_backings;
    while(b && (b->token!=in->token || b->kind || !b->opens))b=b->next;
    if(!b)st=CN_ERROR_INVALID_HANDLE;
    else {b->refs++;*a=(mlu_backend_allocation){.id=id,.address=b->address,.bytes=b->bytes,.ipc=b,.ipc_imported=1,.owner=s->owner_token,.generation=generation,.live=1};out->id=id;out->bytes=b->bytes;}
    pthread_mutex_unlock(&ipc_lock);if(st)mlu_object_registry_release(&s->objects,MLU_OBJECT_ALLOCATION,id);return st;
  }
  if(in->action==3) {
    mlu_backend_allocation *a=find_alloc(s,in->object);if(!a || !a->ipc_imported)return CN_ERROR_INVALID_VALUE;
    int st=backend_context_wait(s);if(st)return st;
    if(a->graph_refs){a->retired=1;mlu_object_registry_release(&s->objects,MLU_OBJECT_ALLOCATION,a->id);return 0;}
    return ipc_release_allocation(s,a);
  }
  if(in->action==4) {
    mlu_vmm_handle *h=vmm_handle(s,in->object);if(!h || !h->references || in->flags)return CN_ERROR_INVALID_VALUE;
    ipc_descriptor *d=calloc(1,sizeof(*d));if(!d)return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
    cn_uint64_t fd=UINT64_MAX;int st=cnMemExportToShareableHandle(&fd,h->handle,CN_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR,0);
    if(st){free(d);return st;}
    d->native_fd=(int)fd;d->token=ipc_random();d->pending_owner=s;
    pthread_mutex_lock(&ipc_lock);
    if(!h->ipc){
      CNmemGenericAllocationHandle retained=0;st=ipc_retain_vmm(h->handle,h->bytes,&retained);
      if(!st){h->ipc=ipc_backing_new(s,h->bytes,1,0);if(h->ipc)h->ipc->native_handle=retained;else {cnMemRelease(retained);st=CN_ERROR_SYSTEM_OUT_OF_MEMORY;}}
    }
    if(st || !h->ipc || !d->token){close(d->native_fd);free(d);if(!st)st=CN_ERROR_SYSTEM_OUT_OF_MEMORY;}
    else {d->backing=h->ipc;h->ipc->refs++;d->next=ipc_descriptors;ipc_descriptors=d;out->token=d->token;}
    pthread_mutex_unlock(&ipc_lock);return st;
  }
  if(in->action==5) {
    mlu_vmm_handle *slot=NULL;for(unsigned i=0;i<MLU_BACKEND_MAX_OBJECTS;i++)if(!s->vmm_handles[i].live){slot=&s->vmm_handles[i];break;}
    if(!slot)return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
    pthread_mutex_lock(&ipc_lock);ipc_descriptor *d=ipc_descriptors;while(d && (d->token!=in->token || !d->attached))d=d->next;
    int st=d ? 0 : CN_ERROR_INVALID_HANDLE;CNmemGenericAllocationHandle handle=0;
    if(!st)st=ipc_retain_vmm(d->backing->native_handle,d->backing->bytes,&handle);
    uint64_t id=0;uint32_t generation;
    if(!st){st=register_object(s,MLU_OBJECT_VMM_HANDLE,&id,&generation);if(st)cnMemRelease(handle);}
    if(!st){d->backing->refs++;*slot=(mlu_vmm_handle){.id=id,.bytes=d->backing->bytes,.handle=handle,.ipc=d->backing,.references=1,.live=1};out->id=id;out->bytes=slot->bytes;}
    pthread_mutex_unlock(&ipc_lock);return st;
  }
  return CN_ERROR_INVALID_VALUE;
}
