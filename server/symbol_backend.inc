/* A symbol is a borrowed allocation. One reference keeps its native module
 * alive until the last Graph snapshot/invocation using its storage retires. */
static int module_symbol(mlu_backend_session *s,const mlu_api_module_symbol_request *in,mlu_api_module_symbol_response *out) {
  mlu_backend_module *m=find_module_by_id(s,in->module);
  if(!m)return CN_ERROR_INVALID_HANDLE;
  CNaddr address=0;cn_uint64_t bytes=0;
  int st=cnModuleGetSymbol(m->module,in->name,&address,&bytes);if(st)return st;
  mlu_backend_allocation *slot=NULL;
  for(unsigned i=0;i<MLU_BACKEND_MAX_OBJECTS;i++) {
    mlu_backend_allocation *a=&s->allocations[i];
    if(!a->live) {if(!slot)slot=a;continue;}
    if(!a->retired && a->symbol_module==m->module_id && a->address==address && a->bytes==bytes) {
      out->allocation=a->id;out->bytes=a->bytes;return 0;
    }
  }
  if(!slot)return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  uint64_t id=0;uint32_t generation=0;
  st=register_object(s,MLU_OBJECT_ALLOCATION,&id,&generation);if(st)return st;
  *slot=(mlu_backend_allocation){.id=id,.address=address,.bytes=bytes,.symbol_module=m->module_id,.owner=s->owner_token,.generation=generation,.live=1};
  m->graph_refs++;out->allocation=id;out->bytes=bytes;return 0;
}
