CNresult cnIpcGetMemHandle(CNIpcMemHandle *out,CNaddr ptr) {
  ENTER();CURRENT();uint64_t off;allocation *a=lookup(c,ptr,&off);
  if(!out || !a || off)return leave(CN_ERROR_INVALID_VALUE);
  mlu_api_ipc_request in={.action=1,.object=a->id};mlu_api_ipc_response value={0};int st=mlu_remote_api_ipc(c->remote,&in,&value);
  if(!st)*out=(CNIpcMemHandle)(uintptr_t)value.token;RETURN(result(st));
}
CNresult cnIpcOpenMemHandle(CNaddr *out,CNIpcMemHandle token,unsigned flags) {
  ENTER();CURRENT();if(!out)RETURN(CN_ERROR_INVALID_VALUE);
  mlu_api_ipc_request in={.action=2,.token=(uintptr_t)token,.flags=flags};mlu_api_ipc_response value={0};
  int st=mlu_remote_api_ipc(c->remote,&in,&value);if(st)RETURN(result(st));
  allocation *a=calloc(1,sizeof(*a));if(a){a->mapped=((value.bytes+4095)&~(size_t)4095)+4096;a->base=mmap(NULL,a->mapped,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);}
  if(!a || a->base==MAP_FAILED){free(a);in=(mlu_api_ipc_request){.action=3,.object=value.id};mlu_remote_api_ipc(c->remote,&in,&value);RETURN(CN_ERROR_SYSTEM_OUT_OF_MEMORY);}
  a->owner=c;a->bytes=value.bytes;a->id=value.id;a->live=1;a->imported=1;a->next=allocations;allocations=a;*out=(uintptr_t)a->base;RETURN(CN_SUCCESS);
}
CNresult cnIpcCloseMemHandle(CNaddr ptr) {
  ENTER();CURRENT();uint64_t off;allocation *a=lookup(c,ptr,&off);if(!a || off || !a->imported)RETURN(CN_ERROR_INVALID_VALUE);
  int st=mlu_remote_context_sync(c->remote);
  mlu_api_ipc_request in={.action=3,.object=a->id};mlu_api_ipc_response out={0};if(!st)st=mlu_remote_api_ipc(c->remote,&in,&out);if(!st)a->live=0;RETURN(result(st));
}
CNresult cnMemExportToShareableHandle(void *out,CNmemGenericAllocationHandle handle,CNmemAllocationHandleType type,cn_uint64_t flags) {
  ENTER();CURRENT();object *h=find_object(c,5,handle);
  if(!out || !h || !h->size || type!=CN_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR)RETURN(CN_ERROR_INVALID_VALUE);
  mlu_api_ipc_request in={.action=4,.object=h->id,.flags=flags};mlu_api_ipc_response value={0};int st=mlu_remote_api_ipc(c->remote,&in,&value);
  if(!st){int fd=mlu_remote_ipc_export_fd(c->remote,value.token);if(fd<0)st=CN_ERROR_OPERATING_SYSTEM;else {cn_uint64_t slot=fd;memcpy(out,&slot,8);}}
  RETURN(result(st));
}
CNresult cnMemImportFromShareableHandle(CNmemGenericAllocationHandle *out,void *fd,CNmemAllocationHandleType type) {
  ENTER();CURRENT();if(!out || type!=CN_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR || (uintptr_t)fd>INT_MAX)RETURN(CN_ERROR_INVALID_VALUE);
  int held=dup((int)(uintptr_t)fd);if(held<0)RETURN(CN_ERROR_INVALID_HANDLE);
  mlu_api_ipc_request in={.action=5};mlu_api_ipc_response value={0};int st=mlu_remote_ipc_fd_token(held,&in.token) ? CN_ERROR_INVALID_HANDLE : 0;
  object *h=NULL;if(!st){h=new_object(c,5);if(!h)st=CN_ERROR_SYSTEM_OUT_OF_MEMORY;}
  if(!st)st=mlu_remote_api_ipc(c->remote,&in,&value);close(held);
  if(st){if(h)discard_object(h);}else {h->id=value.id;h->size=1;h->live=1;*out=h->token;}RETURN(result(st));
}
