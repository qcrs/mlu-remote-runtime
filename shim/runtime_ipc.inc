cnrtRet_t cnrtAcquireMemHandle(cnrtIpcMemHandle *out,void *ptr) {
  ENTER();if(!out)RETURN(cnrtErrorArgsInvalid);
  if(mlu_host_range(ptr,1)==1){uint64_t token;int st=mlu_host_ipc_export(ptr,&token);if(!st)*out=(void*)(uintptr_t)token;RETURN(status(st));}
  CONNECT();uint64_t off;allocation *a=lookup(ptr,&off);if(!a || off)RETURN(cnrtErrorArgsInvalid);
  mlu_api_ipc_request in={.action=1,.object=a->id};mlu_api_ipc_response value={0};int st=mlu_remote_api_ipc(ctx,&in,&value);if(!st)*out=(void*)(uintptr_t)value.token;RETURN(status(st));
}
cnrtRet_t cnrtMapMemHandle(void **out,cnrtIpcMemHandle handle,int flags) {
  ENTER();if(!out || flags)RETURN(cnrtErrorArgsInvalid);
  uint64_t token=(uintptr_t)handle;
  if(token>>48==0x484d)RETURN(status(mlu_host_ipc_import(out,token)));
  CONNECT();mlu_api_ipc_request in={.action=2,.token=token};mlu_api_ipc_response value={0};int st=mlu_remote_api_ipc(ctx,&in,&value);if(st)RETURN(status(st));
  allocation *a=calloc(1,sizeof(*a));if(a){a->mapped=((value.bytes+4095)&~(size_t)4095)+4096;a->base=mmap(NULL,a->mapped,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);}
  if(!a || a->base==MAP_FAILED){free(a);in=(mlu_api_ipc_request){.action=3,.object=value.id};mlu_remote_api_ipc(ctx,&in,&value);RETURN(cnrtErrorSysNoMem);}
  a->bytes=value.bytes;a->id=value.id;a->live=1;a->imported=1;a->next=allocations;allocations=a;*out=a->base;RETURN(cnrtSuccess);
}
cnrtRet_t cnrtUnMapMemHandle(void *ptr) {
  ENTER();if(mlu_host_range(ptr,1)==1)RETURN(status(mlu_host_ipc_close(ptr)));
  CONNECT();uint64_t off;allocation *a=lookup(ptr,&off);if(!a || off || !a->imported)RETURN(cnrtErrorArgsInvalid);
  int st=mlu_remote_context_sync(ctx);mlu_api_ipc_request in={.action=3,.object=a->id};mlu_api_ipc_response value={0};if(!st)st=mlu_remote_api_ipc(ctx,&in,&value);if(!st)a->live=0;RETURN(status(st));
}
