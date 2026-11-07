/* One logical shared context per device in this client process. The server uses
 * a dedicated native context so unrelated clients cannot reset each other. */
static context *shared_context(CNdev dev,int create,CNresult *status) {
  dev=(uint32_t)dev;
  uint32_t count=0;int st=mlu_remote_device_count(control,&count);
  if(st || dev>=count) { *status=st ? result(st) : CN_ERROR_INVALID_DEVICE;return NULL; }
  for(context *c=contexts;c;c=c->next) if(c->shared && c->device==dev) return c;
  if(!create)return NULL;
  context *c=calloc(1,sizeof(*c));
  if(!c || next_token==UINT64_MAX) {free(c);*status=CN_ERROR_SYSTEM_OUT_OF_MEMORY;return NULL;}
  c->token=++next_token;c->live=1;c->shared=1;c->device=dev;c->flags=CN_CTX_SCHED_SYNC_AUTO;
  c->next=contexts;contexts=c;return c;
}
static int shared_inactivate(context *c,unsigned flags) {
  int st=0;
  if(c->remote) {
    st=mlu_remote_session_close(c->remote);
    graph_kernel_cache_drop(c->remote,0);
    atomic_retire(c);mlu_remote_disconnect(c->remote);c->remote=NULL;vmm_invalidate(c);
    for(object *o=objects;o;o=o->next) if(o->owner==c) {o->live=0;free(o->image);o->image=NULL;}
    for(allocation *a=allocations;a;a=a->next) if(a->owner==c)a->live=0;
  }
  c->references=0;c->flags=flags;return st;
}
CNresult cnSharedContextAcquire(CNcontext *out,CNdev dev) {
  ENTER();INIT();if(!out)RETURN(CN_ERROR_INVALID_VALUE);
  CNresult st=0;context *c=shared_context(dev,1,&st);if(!c)RETURN(st);
  if(c->references==UINT_MAX)RETURN(CN_CONTEXT_ERROR_OUT_OF_RESOURCES);
  if(!c->references) {st=connect_session((unsigned)c->device,c->flags,&c->remote);if(st)RETURN(st);}
  c->references++;*out=(CNcontext)(uintptr_t)c->token;RETURN(CN_SUCCESS);
}
CNresult cnSharedContextGetState(CNdev dev,unsigned *flags,int *state) {
  ENTER();INIT();if(!flags || !state)RETURN(CN_ERROR_INVALID_VALUE);
  CNresult st=0;context *c=shared_context(dev,1,&st);if(!c)RETURN(st);
  *flags=c->flags;*state=!!c->references;RETURN(CN_SUCCESS);
}
CNresult cnSharedContextSetFlags(CNdev dev,unsigned flags) {
  ENTER();INIT();if(flags>=CN_CTX_SCHED_SYNC_NUM)RETURN(CN_ERROR_INVALID_VALUE);
  CNresult st=0;context *c=shared_context(dev,1,&st);if(!c)RETURN(st);
  if(c->references)RETURN(CN_CONTEXT_ERROR_INVALID);c->flags=flags;RETURN(CN_SUCCESS);
}
CNresult cnSharedContextRelease(CNdev dev) {
  ENTER();INIT();CNresult st=0;context *c=shared_context(dev,1,&st);if(!c)RETURN(st);
  if(c->references && !--c->references)st=result(shared_inactivate(c,CN_CTX_SCHED_SYNC_AUTO));
  RETURN(st);
}
CNresult cnSharedContextReset(CNdev dev) {
  ENTER();INIT();CNresult st=0;context *c=shared_context(dev,1,&st);if(!c)RETURN(st);
  RETURN(result(shared_inactivate(c,0)));
}
