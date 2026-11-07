static cnrtRet_t symbol_address(const void *symbol,void **out,size_t *bytes) {
  if(!symbol)return cnrtErrorArgsInvalid;
  registration *owner=NULL;variable *var=NULL;
  for(registration *r=registrations;r;r=r->next) if(r->live)
    for(variable *v=r->variables;v;v=v->next)if(v->host==symbol){owner=r;var=v;break;}
  if(!owner)return cnrtErrorNotFound;
  cnrtRet_t status_=connect_remote();if(status_)return status_;
  if(!owner->mid){int st=mlu_remote_module_load_binary(ctx,owner->image,owner->size,&owner->mid);if(st)return status(st);}
  mlu_api_module_symbol_request in={.module=owner->mid};mlu_api_module_symbol_response value={0};
  snprintf(in.name,sizeof(in.name),"%s",var->name);
  int st=mlu_remote_api_module_symbol(ctx,&in,&value);if(st)return status(st);
  for(allocation *a=allocations;a;a=a->next)if(a->live && a->id==value.allocation){*out=a->base;*bytes=a->bytes;return cnrtSuccess;}
  if(!value.bytes || value.bytes>SIZE_MAX-8191)return cnrtErrorArgsInvalid;
  allocation *a=calloc(1,sizeof(*a));if(!a)return cnrtErrorSysNoMem;
  a->mapped=((value.bytes+4095)&~(size_t)4095)+4096;
  a->base=mmap(NULL,a->mapped,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
  if(a->base==MAP_FAILED){free(a);return cnrtErrorSysNoMem;}
  a->id=value.allocation;a->bytes=value.bytes;a->symbol_module=owner->mid;a->live=1;a->next=allocations;allocations=a;
  *out=a->base;*bytes=a->bytes;return cnrtSuccess;
}
cnrtRet_t cnrtGetSymbolAddress(void **out,const void *symbol) {
  ENTER();if(!out)RETURN(cnrtErrorArgsInvalid);void *address;size_t bytes;
  cnrtRet_t st=symbol_address(symbol,&address,&bytes);if(!st)*out=address;RETURN(st);
}
static cnrtRet_t symbol_copy(void *host,const void *symbol,size_t bytes,size_t offset,cnrtMemTransDir_t dir,cnrtQueue_t queue,int to,int sync) {
  void *address=NULL;size_t size=0;cnrtRet_t st=symbol_address(symbol,&address,&size);if(st)return st;
  if(offset>size || bytes>size-offset)return cnrtErrorArgsInvalid;
  if(dir!=cnrtMemcpyNoDirection && dir!=cnrtMemcpyDevToDev && dir!=(to ? cnrtMemcpyHostToDev : cnrtMemcpyDevToHost))return cnrtErrorMemcpyDirectionInvalid;
  address=(unsigned char*)address+offset;
  return to ? copy(address,host,bytes,queue,dir,sync) : copy(host,address,bytes,queue,dir,sync);
}
cnrtRet_t cnrtMemcpyFromSymbol(void *dst,const void *symbol,size_t bytes,size_t offset,cnrtMemTransDir_t dir) {
  ENTER();RETURN(symbol_copy(dst,symbol,bytes,offset,dir,NULL,0,1));
}
cnrtRet_t cnrtMemcpyToSymbol(const void *symbol,const void *src,size_t bytes,size_t offset,cnrtMemTransDir_t dir) {
  ENTER();RETURN(symbol_copy((void*)src,symbol,bytes,offset,dir,NULL,1,1));
}
cnrtRet_t cnrtMemcpyFromSymbolAsync(void *dst,const void *symbol,size_t bytes,size_t offset,cnrtMemTransDir_t dir,cnrtQueue_t queue) {
  ENTER();RETURN(symbol_copy(dst,symbol,bytes,offset,dir,queue,0,0));
}
cnrtRet_t cnrtMemcpyToSymbolAsync(const void *symbol,const void *src,size_t bytes,size_t offset,cnrtMemTransDir_t dir,cnrtQueue_t queue) {
  ENTER();RETURN(symbol_copy((void*)src,symbol,bytes,offset,dir,queue,1,0));
}
