#ifndef MLU_REMOTE_OPERATOR_H
#define MLU_REMOTE_OPERATOR_H

#include <stddef.h>
#include <stdint.h>
#include "remote_cnrt.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t mlu_remote_operator_id;

typedef struct mlu_remote_operator_tensor {
  mlu_remote_allocation_id allocation;
  uint64_t offset;
  uint32_t rank;
  uint32_t layout;
  uint32_t datatype;
  uint64_t shape[MLU_REMOTE_MAX_RANK];
  uint64_t stride[MLU_REMOTE_MAX_RANK];
} mlu_remote_operator_tensor;

typedef struct mlu_remote_operator_grid {
  uint32_t grid_x, grid_y, grid_z;
  uint32_t block_x, block_y, block_z;
} mlu_remote_operator_grid;

int mlu_remote_operator_create(mlu_remote_context *ctx,
                               mlu_remote_operator_id operator_id,
                               mlu_remote_module_id *module,
                               mlu_remote_kernel_id *kernel);
int mlu_remote_operator_destroy(mlu_remote_context *ctx,
                                mlu_remote_operator_id operator_id);
int mlu_remote_operator_launch(mlu_remote_context *ctx,
                               mlu_remote_operator_id operator_id,
                               mlu_remote_kernel_id kernel,
                               mlu_remote_queue_id queue,
                               const mlu_remote_operator_tensor *inputs,
                               uint32_t input_count,
                               const mlu_remote_operator_tensor *outputs,
                               uint32_t output_count,
                               const void *scalar_bytes,
                               size_t scalar_size,
                               const mlu_remote_operator_grid *grid,
                               mlu_remote_transfer_id *transfer);
int mlu_remote_operator_query(mlu_remote_context *ctx,
                              mlu_remote_transfer_id transfer);
int mlu_remote_operator_sync(mlu_remote_context *ctx,
                             mlu_remote_transfer_id transfer);

int mlu_remote_operator_launch_vector_add(mlu_remote_context *ctx,
                                          mlu_remote_kernel_id kernel,
                                          mlu_remote_queue_id queue,
                                          mlu_remote_allocation_id dst,
                                          mlu_remote_allocation_id a,
                                          mlu_remote_allocation_id b,
                                          uint32_t elements,
                                          uint32_t grid_x,
                                          mlu_remote_transfer_id *transfer);
int mlu_remote_operator_launch_scalar_add(mlu_remote_context *ctx,
                                          mlu_remote_kernel_id kernel,
                                          mlu_remote_queue_id queue,
                                          mlu_remote_allocation_id dst,
                                          mlu_remote_allocation_id src,
                                          uint32_t elements,
                                          float scalar,
                                          uint32_t grid_x,
                                          mlu_remote_transfer_id *transfer);
int mlu_remote_operator_launch_scalar_mul(mlu_remote_context *ctx,
                                          mlu_remote_kernel_id kernel,
                                          mlu_remote_queue_id queue,
                                          mlu_remote_allocation_id dst,
                                          mlu_remote_allocation_id src,
                                          uint32_t elements,
                                          float scalar,
                                          uint32_t grid_x,
                                          mlu_remote_transfer_id *transfer);
int mlu_remote_operator_launch_relu(mlu_remote_context *ctx,
                                    mlu_remote_kernel_id kernel,
                                    mlu_remote_queue_id queue,
                                    mlu_remote_allocation_id dst,
                                    mlu_remote_allocation_id src,
                                    uint32_t elements,
                                    uint32_t grid_x,
                                    mlu_remote_transfer_id *transfer);
int mlu_remote_operator_launch_tensor_add_strided(
    mlu_remote_context *ctx, mlu_remote_kernel_id kernel,
    mlu_remote_queue_id queue, mlu_remote_allocation_id dst,
    mlu_remote_allocation_id a, mlu_remote_allocation_id b,
    uint32_t rows, uint32_t cols, uint32_t dst_row_stride,
    uint32_t dst_col_stride, uint32_t a_row_stride, uint32_t a_col_stride,
    uint32_t b_row_stride, uint32_t b_col_stride, uint32_t grid_x,
    mlu_remote_transfer_id *transfer);
int mlu_remote_operator_launch_tensor_mul_strided(
    mlu_remote_context *ctx, mlu_remote_kernel_id kernel,
    mlu_remote_queue_id queue, mlu_remote_allocation_id dst,
    mlu_remote_allocation_id a, mlu_remote_allocation_id b,
    uint32_t rows, uint32_t cols, uint32_t dst_row_stride,
    uint32_t dst_col_stride, uint32_t a_row_stride, uint32_t a_col_stride,
    uint32_t b_row_stride, uint32_t b_col_stride, uint32_t grid_x,
    mlu_remote_transfer_id *transfer);

#ifdef __cplusplus
}
#endif
#endif
