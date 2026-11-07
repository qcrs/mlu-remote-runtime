#ifndef MLU_REMOTE_BACKEND_H
#define MLU_REMOTE_BACKEND_H

#include <stdint.h>
#include <stddef.h>
#include <cn_api.h>
#include "remote_protocol.h"
#include "object_registry.h"
#include "bang_image.h"
#include "resource_limits.h"
#include "remote_cnrt.h"

#define MLU_BACKEND_MAX_OBJECTS 1024

typedef struct {
  uint64_t id;
  CNaddr address;
  uint64_t bytes;
  uint64_t owner;
  uint32_t generation;
  int live;
} mlu_backend_allocation;

typedef struct {
  uint64_t id;
  CNqueue handle;
  uint64_t owner;
  uint32_t generation;
  /* Public identity is gone; native work and staging still belong to session. */
  int retired;
  int live;
} mlu_backend_queue;

typedef struct {
  uint64_t id;
  CNnotifier handle;
  uint64_t owner;
  uint32_t generation;
  int live;
} mlu_backend_notifier;

typedef struct {
  uint64_t module_id;
  uint64_t kernel_id;
  uint32_t operator_id;
  CNmodule module;
  CNkernel kernel;
  char kernel_name[256];
  int generic_image;
  void *image;
  size_t image_size;
  uint64_t owner;
  uint32_t generation;
  int live;
} mlu_backend_module;

/* Generic kernels are children of one native module, not module copies. The
 * preset module's original primary kernel fields remain wire compatible. */
typedef struct {
  uint64_t id, module_id;
  CNkernel handle;
  char name[256];
  bang_param_layout params;
  uint64_t owner;
  uint32_t generation;
  int live;
} mlu_backend_kernel;

typedef struct {
  uint64_t queue_id;
  void *host;
  uint64_t bytes;
  uint64_t owner;
  uint32_t generation;
  int pinned;
  int live;
} mlu_backend_staging;

typedef struct {
  uint64_t allocation_id;
  uint64_t offset;
  uint32_t rank;
  uint32_t layout;
  uint32_t datatype;
  uint64_t shape[MLU_REMOTE_MAX_RANK];
  uint64_t stride[MLU_REMOTE_MAX_RANK];
} mlu_backend_tensor;

typedef struct {
  uint32_t operator_id;
  uint64_t kernel_id;
  uint64_t queue_id;
  uint32_t input_count;
  uint32_t output_count;
  uint32_t grid_x, grid_y, grid_z;
  uint32_t block_x, block_y, block_z;
  const mlu_backend_tensor *inputs;
  const mlu_backend_tensor *outputs;
  const unsigned char *scalars;
  uint32_t scalar_bytes;
} mlu_backend_operator_request;

enum mlu_backend_operation_state {
  MLU_BACKEND_OPERATION_SUBMITTED = 1,
  MLU_BACKEND_OPERATION_COMPLETE = 2,
  MLU_BACKEND_OPERATION_FAILED = 3,
};

typedef struct {
  uint64_t id;
  uint64_t queue_id;
  CNnotifier notifier;
  uint64_t sequence;
  uint64_t dependency;
  uint64_t owner;
  uint32_t generation;
  uint32_t state;
  void *host;
  uint64_t bytes;
  int notifier_live;
  int release_requested;
  int live;
} mlu_backend_transfer;

typedef struct mlu_backend_session {
  CNcontext context;
  CNdev device;
  int device_ordinal;
  int cleanup_failed;
  int default_queue_used;
  uint64_t owner_token;
  uint32_t generation;
  uint64_t next_operation_sequence;
  mlu_resource_usage usage;
  mlu_object_registry objects;
  mlu_backend_allocation allocations[MLU_BACKEND_MAX_OBJECTS];
  mlu_backend_queue queues[MLU_BACKEND_MAX_OBJECTS];
  mlu_backend_notifier notifiers[MLU_BACKEND_MAX_OBJECTS];
  mlu_backend_module modules[MLU_BACKEND_MAX_OBJECTS];
  mlu_backend_kernel kernels[MLU_BACKEND_MAX_OBJECTS];
  mlu_backend_staging staging[MLU_BACKEND_MAX_OBJECTS];
  mlu_backend_transfer transfers[MLU_BACKEND_MAX_OBJECTS];
} mlu_backend_session;

int mlu_backend_global_init(void);
int mlu_backend_device_count(int *count);
int mlu_backend_device_info(int ordinal, char *name, size_t capacity, uint64_t *memory);
int mlu_backend_session_init(mlu_backend_session *s, int ordinal);
int mlu_backend_session_init_flags(mlu_backend_session *s, int ordinal, unsigned flags);
int mlu_backend_context_sync(mlu_backend_session *s);
int mlu_backend_session_select_device(mlu_backend_session *s, int ordinal);
int mlu_backend_session_reset(mlu_backend_session *s);
int mlu_backend_session_cleanup(mlu_backend_session *s);

int mlu_backend_alloc(mlu_backend_session *s, uint64_t bytes, uint64_t *id);
int mlu_backend_free(mlu_backend_session *s, uint64_t id);
int mlu_backend_memcpy(mlu_backend_session *s, int direction,
                       uint64_t dst_id, uint64_t dst_offset,
                       uint64_t src_id, uint64_t src_offset,
                       const void *host_src, void *host_dst, uint64_t bytes,
                       uint64_t queue_id, uint64_t *transfer_id);
int mlu_backend_memcpy_ex(mlu_backend_session *s, int direction,
                          uint64_t dst_id, uint64_t dst_offset,
                          uint64_t src_id, uint64_t src_offset,
                          const void *host_src, void *host_dst, uint64_t bytes,
                          uint64_t queue_id, uint32_t asynchronous,
                          uint64_t *transfer_id);
int mlu_backend_queue_create(mlu_backend_session *s, uint64_t *id);
int mlu_backend_queue_destroy(mlu_backend_session *s, uint64_t id);
int mlu_backend_queue_sync(mlu_backend_session *s, uint64_t id);
int mlu_backend_queue_query(mlu_backend_session *s, uint64_t id);
int mlu_backend_notifier_create(mlu_backend_session *s, uint64_t *id);
int mlu_backend_notifier_destroy(mlu_backend_session *s, uint64_t id);
int mlu_backend_notifier_record(mlu_backend_session *s, uint64_t notifier, uint64_t queue);
int mlu_backend_notifier_query(mlu_backend_session *s, uint64_t id);
int mlu_backend_notifier_sync(mlu_backend_session *s, uint64_t id);
int mlu_backend_transfer_query(mlu_backend_session *s, uint64_t id);
int mlu_backend_transfer_sync(mlu_backend_session *s, uint64_t id);
int mlu_backend_transfer_release(mlu_backend_session *s, uint64_t id);
int mlu_backend_module_load(mlu_backend_session *s, const char *name,
                            uint64_t *module_id, uint64_t *kernel_id);
int mlu_backend_module_load_image(mlu_backend_session *s, const void *image,
                                  uint64_t image_size, const char *kernel_name,
                                  uint64_t *module_id, uint64_t *kernel_id);
int mlu_backend_module_load_binary(mlu_backend_session *s, const void *image,
                                   uint64_t size, uint64_t *module_id);
int mlu_backend_module_query_fatbinary_memory(mlu_backend_session *s,
                                              const void *image, uint64_t image_size,
                                              uint64_t *bytes);
int mlu_backend_operator_create(mlu_backend_session *s, uint32_t operator_id,
                                uint64_t *module_id, uint64_t *kernel_id);
int mlu_backend_module_get_kernel(mlu_backend_session *s, uint64_t module_id,
                                  const char *name, uint64_t *kernel_id);
int mlu_backend_operator_destroy(mlu_backend_session *s, uint32_t operator_id);
int mlu_backend_module_unload(mlu_backend_session *s, uint64_t module_id);
int mlu_backend_operator_launch(mlu_backend_session *s,
                                 const mlu_backend_operator_request *request,
                                 uint64_t *transfer_id);
typedef struct {
  uint32_t kind;
  uint32_t size;
  uint64_t allocation_id;
  uint64_t offset;
  const unsigned char *scalar;
} mlu_backend_kernel_arg;
int mlu_backend_kernel_launch_generic(mlu_backend_session *s,
                                       uint64_t module_id, uint64_t kernel_id,
                                       uint64_t queue_id,
                                       uint32_t dim_x, uint32_t dim_y,
                                       uint32_t dim_z, uint32_t kernel_class,
                                       const mlu_backend_kernel_arg *args,
                                       uint32_t arg_count, uint64_t *transfer_id);
int mlu_backend_kernel_launch_vector_add(mlu_backend_session *s, uint64_t kernel_id,
                                         uint64_t queue_id, uint64_t dst_id,
                                         uint64_t a_id, uint64_t b_id,
                                         uint32_t elements, uint32_t grid_x);
const char *mlu_backend_error_name(int code);
const char *mlu_backend_error_string(int code);

#include "typed_backend_decls.inc"
#endif
