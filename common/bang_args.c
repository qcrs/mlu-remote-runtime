#include "bang_args.h"
#include <string.h>
int bang_pack_args(const bang_param_layout *layout,const bang_param_types *types,
                    void **values,bang_resolve_pointer resolve,void *owner,
                    mlu_remote_kernel_arg args[BANG_MAX_ARGS],
                    mlu_remote_kernel_relocation relocations[BANG_MAX_RELOCS]) {
  if(layout->count && !values) return -1;
  memset(args,0,sizeof(*args)*BANG_MAX_ARGS);
  unsigned field=0;
  for(unsigned i=0;i<layout->count;++i) {
    if(!values[i]) return -1;
    args[i].size=layout->sizes[i];
    if(types->kind[i]==1) {
      uint64_t ptr=0; memcpy(&ptr,values[i],8); args[i].kind=1;
      if(ptr && resolve(owner,ptr,&args[i].allocation,&args[i].offset)) return -2;
    } else { args[i].kind=2; args[i].scalar=values[i]; }
    while(field<types->count && types->fields[field].argument==i) {
      mlu_remote_kernel_relocation *r=&relocations[field];
      *r=(mlu_remote_kernel_relocation){.offset=types->fields[field].offset};
      uint64_t ptr=0; memcpy(&ptr,(const unsigned char *)values[i]+r->offset,8);
      if(ptr && resolve(owner,ptr,&r->allocation,&r->allocation_offset)) return -2;
      if(!args[i].relocation_count) args[i].relocations=r;
      ++args[i].relocation_count; args[i].kind=3; ++field;
    }
  }
  return 0;
}
