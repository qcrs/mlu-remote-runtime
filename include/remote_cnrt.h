#ifndef MLU_REMOTE_CNRT_H
#define MLU_REMOTE_CNRT_H

#include <stddef.h>
#include <stdint.h>
#include "remote_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mlu_remote_context mlu_remote_context;
#include "generated/typed_api.h"
typedef uint64_t mlu_remote_device_id;
typedef uint64_t mlu_remote_allocation_id;
typedef uint64_t mlu_remote_queue_id;
typedef uint64_t mlu_remote_notifier_id;
typedef uint64_t mlu_remote_transfer_id;
typedef uint64_t mlu_remote_module_id;
typedef uint64_t mlu_remote_kernel_id;

typedef struct mlu_remote_kernel_relocation {
  uint32_t offset;
  uint64_t allocation;
  uint64_t allocation_offset;
} mlu_remote_kernel_relocation;
typedef struct mlu_remote_kernel_arg {
  /* 1 allocation, 2 scalar bytes, 3 bytes plus explicit pointer relocations. */
  uint32_t kind;
  uint32_t size;
  uint64_t allocation;
  uint64_t offset;
  const void *scalar;
  uint32_t relocation_count;
  const mlu_remote_kernel_relocation *relocations;
} mlu_remote_kernel_arg;

typedef struct mlu_remote_error {
  uint32_t code;
  char name[96];
  char message[256];
} mlu_remote_error;

mlu_remote_context *mlu_remote_connect(const char *host, uint16_t port);
mlu_remote_context *mlu_remote_connect_device(const char *host, uint16_t port,
                                              uint32_t device);
/* Extended HELLO preserves native CNDrv context scheduling flags. */
mlu_remote_context *mlu_remote_connect_driver(const char *host, uint16_t port,
                                              uint32_t device, uint32_t flags);
int mlu_remote_context_sync(mlu_remote_context *ctx);
/* Deferred D2H owns a Host lease until a successful completion frontier.
 * Queue/Notifier/Transfer/context waits and queries publish ready results. */
int mlu_remote_memcpy_d2h_async(mlu_remote_context *ctx, void *dst,
    uint64_t src, uint64_t offset, uint64_t bytes, uint64_t queue, uint64_t *transfer);
/* Acknowledges native cleanup before closing; no context is recreated. */
int mlu_remote_session_close(mlu_remote_context *ctx);
/* Thread-local diagnostics for the last connect attempt. Successful connect
 * clears it. DNS is not bounded; TCP connect and HELLO have separate budgets.
 * Configure CONNECT_TIMEOUT_MS (5000) / RPC_TIMEOUT_MS (120000) with the
 * MLU_REMOTE_ prefix before connecting, each in [1, 3600000]. RPC budget starts
 * after acquiring the context lock. Disconnect must not race another call. */
const mlu_remote_error *mlu_remote_last_connect_error(void);
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
/* Explicit asynchronous variants are needed for the native NULL/default
 * queue.  QueueID zero otherwise denotes a synchronous native copy on the
 * wire, while an explicit queue has historically implied async work. */
int mlu_remote_memcpy_h2d_async(mlu_remote_context *ctx, mlu_remote_allocation_id dst,
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
int mlu_remote_memcpy_d2d_async(mlu_remote_context *ctx, mlu_remote_allocation_id dst,
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
/* Drop a completion token without blocking its queue. Reaped on completion. */
int mlu_remote_transfer_release(mlu_remote_context *ctx, mlu_remote_transfer_id transfer);
int mlu_remote_session_reset(mlu_remote_context *ctx);

int mlu_remote_module_load_preset(mlu_remote_context *ctx, const char *name,
                                  mlu_remote_module_id *module,
                                  mlu_remote_kernel_id *kernel);
int mlu_remote_module_unload_preset(mlu_remote_context *ctx,
                                    mlu_remote_module_id module);
int mlu_remote_module_load_image(mlu_remote_context *ctx, const void *image,
                                 size_t image_size, const char *kernel_name,
                                 mlu_remote_module_id *module,
                                 mlu_remote_kernel_id *kernel);
int mlu_remote_module_get_kernel(mlu_remote_context *ctx, uint64_t module,
                                 const char *name, uint64_t *kernel);
int mlu_remote_module_load_binary(mlu_remote_context *ctx, const void *image,
                                  size_t size, uint64_t *module);
int mlu_remote_module_query_fatbinary_memory(mlu_remote_context *ctx,
                                             const void *image, size_t size,
                                             uint64_t *bytes);
int mlu_remote_kernel_launch_generic(mlu_remote_context *ctx,
                                     mlu_remote_module_id module,
                                     mlu_remote_kernel_id kernel,
                                     mlu_remote_queue_id queue,
                                     uint32_t dim_x, uint32_t dim_y,
                                     uint32_t dim_z, uint32_t kernel_class,
                                     const mlu_remote_kernel_arg *args,
                                     uint32_t arg_count,
                                     mlu_remote_transfer_id *transfer);
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
