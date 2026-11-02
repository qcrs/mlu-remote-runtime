#include "remote_cnnl.h"

extern int mlu_remote_cnnl_request(mlu_remote_context *, uint64_t, uint64_t,
                                  uint64_t, uint64_t, uint64_t);

int mlu_remote_cnnl_elementwise_add_f32(mlu_remote_context *ctx, uint64_t queue,
                                        uint64_t dst, uint64_t a, uint64_t b,
                                        uint64_t elements) {
  return mlu_remote_cnnl_request(ctx, queue, dst, a, b, elements);
}
