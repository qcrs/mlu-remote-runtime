#ifndef MLU_BANG_ARGS_H
#define MLU_BANG_ARGS_H
#include "bang_image.h"
#include "remote_cnrt.h"
/* Resolve only fields identified as pointers by compiler metadata. */
typedef int (*bang_resolve_pointer)(void *,uint64_t,uint64_t *,uint64_t *);
int bang_pack_args(const bang_param_layout *layout,const bang_param_types *types,
                    void **values,bang_resolve_pointer resolve,void *owner,
                    mlu_remote_kernel_arg args[BANG_MAX_ARGS],
                    mlu_remote_kernel_relocation relocations[BANG_MAX_RELOCS]);
#endif
