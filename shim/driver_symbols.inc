CNresult cnModuleGetSymbol(CNmodule handle,const char *name,CNaddr *out,cn_uint64_t *bytes) {
  ENTER();CURRENT();if(!name || !*name || strlen(name)>=256 || !out || !bytes)RETURN(CN_ERROR_INVALID_VALUE);
  object *m=find_object(c,MODULE,(uintptr_t)handle);if(!m)RETURN(CN_ERROR_INVALID_HANDLE);
  mlu_api_module_symbol_request in={.module=m->id};mlu_api_module_symbol_response value={0};
  snprintf(in.name,sizeof(in.name),"%s",name);
  int st=mlu_remote_api_module_symbol(c->remote,&in,&value);if(st)RETURN(result(st));
  for(allocation *a=allocations;a;a=a->next)if(a->live && a->owner==c && a->id==value.allocation) {*out=(uintptr_t)a->base;*bytes=a->bytes;RETURN(CN_SUCCESS);}
  if(!value.bytes || value.bytes>SIZE_MAX-8191)RETURN(CN_ERROR_INVALID_VALUE);
  allocation *a=calloc(1,sizeof(*a));if(!a)RETURN(CN_ERROR_SYSTEM_OUT_OF_MEMORY);
  a->mapped=((value.bytes+4095)&~(size_t)4095)+4096;
  a->base=mmap(NULL,a->mapped,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
  if(a->base==MAP_FAILED){free(a);RETURN(CN_ERROR_SYSTEM_OUT_OF_MEMORY);}
  a->owner=c;a->bytes=value.bytes;a->id=value.allocation;a->symbol_module=m->id;a->live=1;a->next=allocations;allocations=a;
  *out=(uintptr_t)a->base;*bytes=a->bytes;RETURN(CN_SUCCESS);
}
