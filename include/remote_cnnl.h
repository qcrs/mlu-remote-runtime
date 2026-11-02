#ifndef MLU_REMOTE_CNNL_H
#define MLU_REMOTE_CNNL_H

#include "remote_cnrt.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The installed Neuware image was checked at build time. It has no CNNL header
 * or shared library, so this operation intentionally returns unsupported. */
int mlu_remote_cnnl_elementwise_add_f32(mlu_remote_context *ctx,
                                        mlu_remote_queue_id queue,
                                        mlu_remote_allocation_id dst,
                                        mlu_remote_allocation_id a,
                                        mlu_remote_allocation_id b,
                                        uint64_t elements);

#ifdef __cplusplus
}
#endif
#endif
