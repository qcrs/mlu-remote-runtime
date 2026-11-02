#ifndef MLU_REMOTE_CNRT_H
#define MLU_REMOTE_CNRT_H

#include <stddef.h>
#include <stdint.h>
#include "remote_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mlu_remote_context mlu_remote_context;
typedef uint64_t mlu_remote_device_id;
typedef uint64_t mlu_remote_allocation_id;
typedef uint64_t mlu_remote_queue_id;
typedef uint64_t mlu_remote_notifier_id;
typedef uint64_t mlu_remote_transfer_id;
typedef uint64_t mlu_remote_module_id;
typedef uint64_t mlu_remote_kernel_id;

typedef struct mlu_remote_error {
  uint32_t code;
  char name[96];
  char message[256];
} mlu_remote_error;

mlu_remote_context *mlu_remote_connect(const char *host, uint16_t port);
mlu_remote_context *mlu_remote_connect_device(const char *host, uint16_t port,
                                              uint32_t device);
void mlu_remote_disconnect(mlu_remote_context *ctx);
const mlu_remote_error *mlu_remote_last_error(const mlu_remote_context *ctx);

int mlu_remote_device_count(mlu_remote_context *ctx, uint32_t *count);
int mlu_remote_device_query(mlu_remote_context *ctx, uint32_t device,
                            char *name, size_t name_capacity,
                            uint64_t *total_memory);
int mlu_remote_select_device(mlu_remote_context *ctx, uint32_t device);

int mlu_remote_alloc(mlu_remote_context *ctx, uint64_t bytes,
                     mlu_remote_allocation_id *allocation);
int mlu_remote_free(mlu_remote_context *ctx, mlu_remote_allocation_id allocation);
int mlu_remote_memcpy_h2d(mlu_remote_context *ctx, mlu_remote_allocation_id dst,
                          uint64_t dst_offset, const void *src, uint64_t bytes,
                          mlu_remote_queue_id queue, mlu_remote_transfer_id *transfer);
int mlu_remote_memcpy_d2h(mlu_remote_context *ctx, void *dst,
                          mlu_remote_allocation_id src, uint64_t src_offset,
                          uint64_t bytes, mlu_remote_queue_id queue,
                          mlu_remote_transfer_id *transfer);
int mlu_remote_memcpy_d2d(mlu_remote_context *ctx, mlu_remote_allocation_id dst,
                          uint64_t dst_offset, mlu_remote_allocation_id src,
                          uint64_t src_offset, uint64_t bytes,
                          mlu_remote_queue_id queue, mlu_remote_transfer_id *transfer);

int mlu_remote_queue_create(mlu_remote_context *ctx, mlu_remote_queue_id *queue);
int mlu_remote_queue_destroy(mlu_remote_context *ctx, mlu_remote_queue_id queue);
int mlu_remote_queue_sync(mlu_remote_context *ctx, mlu_remote_queue_id queue);
int mlu_remote_queue_query(mlu_remote_context *ctx, mlu_remote_queue_id queue);

int mlu_remote_notifier_create(mlu_remote_context *ctx, mlu_remote_notifier_id *notifier);
int mlu_remote_notifier_destroy(mlu_remote_context *ctx, mlu_remote_notifier_id notifier);
int mlu_remote_notifier_record(mlu_remote_context *ctx, mlu_remote_notifier_id notifier,
                               mlu_remote_queue_id queue);
int mlu_remote_notifier_query(mlu_remote_context *ctx, mlu_remote_notifier_id notifier);
int mlu_remote_notifier_sync(mlu_remote_context *ctx, mlu_remote_notifier_id notifier);
int mlu_remote_transfer_query(mlu_remote_context *ctx, mlu_remote_transfer_id transfer);
int mlu_remote_transfer_sync(mlu_remote_context *ctx, mlu_remote_transfer_id transfer);
int mlu_remote_session_reset(mlu_remote_context *ctx);

int mlu_remote_module_load_preset(mlu_remote_context *ctx, const char *name,
                                  mlu_remote_module_id *module,
                                  mlu_remote_kernel_id *kernel);
int mlu_remote_module_unload_preset(mlu_remote_context *ctx,
                                    mlu_remote_module_id module);
int mlu_remote_kernel_launch_vector_add(mlu_remote_context *ctx,
                                        mlu_remote_kernel_id kernel,
                                        mlu_remote_queue_id queue,
                                        mlu_remote_allocation_id dst,
                                        mlu_remote_allocation_id a,
                                        mlu_remote_allocation_id b,
                                        uint32_t elements,
                                        uint32_t grid_x);

#ifdef __cplusplus
}
#endif
#endif
