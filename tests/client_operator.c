#include "remote_operator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int report(mlu_remote_context *ctx, const char *where, int st) {
  const mlu_remote_error *e = mlu_remote_last_error(ctx);
  fprintf(stderr, "FAIL %s status=%d %s: %s\n", where, st, e ? e->name : "", e ? e->message : "");
  return 1;
}
#define CHECK(ctx, expr, label) do { int _st = (expr); if (_st) return report((ctx), (label), _st); } while (0)

int main(void) {
  const char *host = getenv("MLU_REMOTE_HOST"); int port = getenv("MLU_REMOTE_PORT") ? atoi(getenv("MLU_REMOTE_PORT")) : 50051;
  mlu_remote_context *ctx = mlu_remote_connect(host ? host : "127.0.0.1", (uint16_t)port);
  if (!ctx) { fprintf(stderr, "connect failed\n"); return 1; }
  mlu_remote_queue_id q = 0; CHECK(ctx, mlu_remote_queue_create(ctx, &q), "queue_create");

  const uint32_t n = 64; const size_t bytes = n * sizeof(float); float *a = malloc(bytes), *b = malloc(bytes), *out = calloc(n, sizeof(float));
  mlu_remote_allocation_id da = 0, db = 0, dout = 0; CHECK(ctx, mlu_remote_alloc(ctx, bytes, &da), "alloc_a"); CHECK(ctx, mlu_remote_alloc(ctx, bytes, &db), "alloc_b"); CHECK(ctx, mlu_remote_alloc(ctx, bytes, &dout), "alloc_out");
  for (uint32_t i = 0; i < n; ++i) { a[i] = (float)i; b[i] = (float)(2 * i); }
  CHECK(ctx, mlu_remote_memcpy_h2d(ctx, da, 0, a, bytes, q, NULL), "h2d_a"); CHECK(ctx, mlu_remote_memcpy_h2d(ctx, db, 0, b, bytes, q, NULL), "h2d_b");

  uint64_t module = 0, kernel = 0, transfer = 0;
  CHECK(ctx, mlu_remote_operator_create(ctx, MLU_REMOTE_OPERATOR_VECTOR_ADD, &module, &kernel), "create_vector_add");
  CHECK(ctx, mlu_remote_operator_launch_vector_add(ctx, kernel, q, dout, da, db, n, 1, &transfer), "launch_vector_add");
  int pending = mlu_remote_operator_query(ctx, transfer);
  if (pending != 0 && pending != 1) return report(ctx, "query_vector_add", pending);
  CHECK(ctx, mlu_remote_operator_sync(ctx, transfer), "sync_vector_add");
  CHECK(ctx, mlu_remote_operator_query(ctx, transfer), "query_completed_vector_add");
  CHECK(ctx, mlu_remote_memcpy_d2h(ctx, out, dout, 0, bytes, q, NULL), "d2h_vector_add");
  for (uint32_t i = 0; i < n; ++i) if (out[i] != a[i] + b[i]) return report(ctx, "vector_add_value", -1);
  printf("PASS operator vector_add module=%llu kernel=%llu transfer=%llu\n", (unsigned long long)module, (unsigned long long)kernel, (unsigned long long)transfer);
  int bad = mlu_remote_operator_create(ctx, 99, &module, &kernel);
  if (!bad) return report(ctx, "unregistered_operator_rejected", bad);
  mlu_remote_operator_tensor bad_in[2], bad_out; memset(bad_in, 0, sizeof(bad_in)); memset(&bad_out, 0, sizeof(bad_out));
  for (int i = 0; i < 2; ++i) { bad_in[i].allocation = i ? db : da; bad_in[i].rank = 1; bad_in[i].layout = MLU_REMOTE_LAYOUT_ROW_MAJOR; bad_in[i].datatype = MLU_REMOTE_DTYPE_F32; bad_in[i].shape[0] = n; bad_in[i].stride[0] = 1; }
  bad_out = bad_in[0]; bad_out.allocation = dout; bad_in[0].datatype = 99;
  bad = mlu_remote_operator_launch(ctx, MLU_REMOTE_OPERATOR_VECTOR_ADD, kernel, q, bad_in, 2, &bad_out, 1, NULL, 0, NULL, NULL);
  if (!bad) return report(ctx, "wrong_datatype_rejected", bad);
  bad_in[0].datatype = MLU_REMOTE_DTYPE_F32; bad_out.offset = bytes;
  bad = mlu_remote_operator_launch(ctx, MLU_REMOTE_OPERATOR_VECTOR_ADD, kernel, q, bad_in, 2, &bad_out, 1, NULL, 0, NULL, NULL);
  if (!bad) return report(ctx, "out_of_bounds_offset_rejected", bad);
  printf("PASS operator parameter and registry errors\n");
  CHECK(ctx, mlu_remote_operator_destroy(ctx, MLU_REMOTE_OPERATOR_VECTOR_ADD), "destroy_vector_add");

  memset(out, 0, bytes); CHECK(ctx, mlu_remote_operator_create(ctx, MLU_REMOTE_OPERATOR_SCALAR_ADD, &module, &kernel), "create_scalar_add");
  CHECK(ctx, mlu_remote_operator_launch_scalar_add(ctx, kernel, q, dout, da, n, 3.5f, 1, &transfer), "launch_scalar_add"); CHECK(ctx, mlu_remote_operator_sync(ctx, transfer), "sync_scalar_add"); CHECK(ctx, mlu_remote_memcpy_d2h(ctx, out, dout, 0, bytes, q, NULL), "d2h_scalar_add");
  for (uint32_t i = 0; i < n; ++i) if (out[i] != a[i] + 3.5f) { fprintf(stderr, "scalar mismatch %u out=%f expected=%f next=%f\n", i, out[i], a[i] + 3.5f, i + 1 < n ? out[i + 1] : -1.0f); return report(ctx, "scalar_add_value", -1); }
  printf("PASS operator scalar_add transfer=%llu\n", (unsigned long long)transfer); CHECK(ctx, mlu_remote_operator_destroy(ctx, MLU_REMOTE_OPERATOR_SCALAR_ADD), "destroy_scalar_add");

  memset(out, 0, bytes); CHECK(ctx, mlu_remote_operator_create(ctx, MLU_REMOTE_OPERATOR_SCALAR_MUL, &module, &kernel), "create_scalar_mul");
  CHECK(ctx, mlu_remote_operator_launch_scalar_mul(ctx, kernel, q, dout, da, n, 2.5f, 1, &transfer), "launch_scalar_mul"); CHECK(ctx, mlu_remote_operator_sync(ctx, transfer), "sync_scalar_mul"); CHECK(ctx, mlu_remote_memcpy_d2h(ctx, out, dout, 0, bytes, q, NULL), "d2h_scalar_mul");
  for (uint32_t i = 0; i < n; ++i) if (out[i] != a[i] * 2.5f) return report(ctx, "scalar_mul_value", -1);
  printf("PASS operator scalar_mul transfer=%llu\n", (unsigned long long)transfer); CHECK(ctx, mlu_remote_operator_destroy(ctx, MLU_REMOTE_OPERATOR_SCALAR_MUL), "destroy_scalar_mul");

  for (uint32_t i = 0; i < n; ++i) a[i] = i % 3 == 0 ? -(float)i : (float)i;
  CHECK(ctx, mlu_remote_memcpy_h2d(ctx, da, 0, a, bytes, q, NULL), "h2d_relu"); memset(out, 0, bytes);
  CHECK(ctx, mlu_remote_operator_create(ctx, MLU_REMOTE_OPERATOR_RELU, &module, &kernel), "create_relu"); CHECK(ctx, mlu_remote_operator_launch_relu(ctx, kernel, q, dout, da, n, 1, &transfer), "launch_relu"); CHECK(ctx, mlu_remote_operator_sync(ctx, transfer), "sync_relu"); CHECK(ctx, mlu_remote_memcpy_d2h(ctx, out, dout, 0, bytes, q, NULL), "d2h_relu");
  for (uint32_t i = 0; i < n; ++i) { float expected = a[i] > 0 ? a[i] : 0; if (out[i] != expected) return report(ctx, "relu_value", -1); }
  printf("PASS operator relu transfer=%llu\n", (unsigned long long)transfer); CHECK(ctx, mlu_remote_operator_destroy(ctx, MLU_REMOTE_OPERATOR_RELU), "destroy_relu");

  const uint32_t rows = 3, cols = 4, stride = 6, span = (rows - 1) * stride + cols; const size_t tensor_bytes = span * sizeof(float); float *ta = calloc(span, sizeof(float)), *tb = calloc(span, sizeof(float)), *to = calloc(span, sizeof(float));
  mlu_remote_allocation_id tda = 0, tdb = 0, tdo = 0; CHECK(ctx, mlu_remote_alloc(ctx, tensor_bytes, &tda), "alloc_tensor_a"); CHECK(ctx, mlu_remote_alloc(ctx, tensor_bytes, &tdb), "alloc_tensor_b"); CHECK(ctx, mlu_remote_alloc(ctx, tensor_bytes, &tdo), "alloc_tensor_out");
  for (uint32_t r = 0; r < rows; ++r) for (uint32_t c = 0; c < cols; ++c) { ta[r * stride + c] = (float)(r * 10 + c); tb[r * stride + c] = (float)(r + c); }
  CHECK(ctx, mlu_remote_memcpy_h2d(ctx, tda, 0, ta, tensor_bytes, q, NULL), "h2d_tensor_a"); CHECK(ctx, mlu_remote_memcpy_h2d(ctx, tdb, 0, tb, tensor_bytes, q, NULL), "h2d_tensor_b"); CHECK(ctx, mlu_remote_operator_create(ctx, MLU_REMOTE_OPERATOR_TENSOR_ADD_STRIDED, &module, &kernel), "create_tensor_add_strided");
  CHECK(ctx, mlu_remote_operator_launch_tensor_add_strided(ctx, kernel, q, tdo, tda, tdb, rows, cols, stride, 1, stride, 1, stride, 1, 1, &transfer), "launch_tensor_add_strided"); CHECK(ctx, mlu_remote_operator_sync(ctx, transfer), "sync_tensor_add_strided"); CHECK(ctx, mlu_remote_memcpy_d2h(ctx, to, tdo, 0, tensor_bytes, q, NULL), "d2h_tensor_add_strided");
  for (uint32_t r = 0; r < rows; ++r) for (uint32_t c = 0; c < cols; ++c) if (to[r * stride + c] != ta[r * stride + c] + tb[r * stride + c]) return report(ctx, "tensor_add_strided_value", -1);
  printf("PASS operator tensor_add_strided rank=2 stride=%u transfer=%llu\n", stride, (unsigned long long)transfer); CHECK(ctx, mlu_remote_operator_destroy(ctx, MLU_REMOTE_OPERATOR_TENSOR_ADD_STRIDED), "destroy_tensor_add_strided");

  CHECK(ctx, mlu_remote_operator_create(ctx, MLU_REMOTE_OPERATOR_TENSOR_MUL_STRIDED, &module, &kernel), "create_tensor_mul_strided");
  CHECK(ctx, mlu_remote_operator_launch_tensor_mul_strided(ctx, kernel, q, tdo, tda, tdb, rows, cols, stride, 1, stride, 1, stride, 1, 1, &transfer), "launch_tensor_mul_strided"); CHECK(ctx, mlu_remote_operator_sync(ctx, transfer), "sync_tensor_mul_strided"); CHECK(ctx, mlu_remote_memcpy_d2h(ctx, to, tdo, 0, tensor_bytes, q, NULL), "d2h_tensor_mul_strided");
  for (uint32_t r = 0; r < rows; ++r) for (uint32_t c = 0; c < cols; ++c) if (to[r * stride + c] != ta[r * stride + c] * tb[r * stride + c]) return report(ctx, "tensor_mul_strided_value", -1);
  printf("PASS operator tensor_mul_strided rank=2 stride=%u transfer=%llu\n", stride, (unsigned long long)transfer); CHECK(ctx, mlu_remote_operator_destroy(ctx, MLU_REMOTE_OPERATOR_TENSOR_MUL_STRIDED), "destroy_tensor_mul_strided");

  mlu_remote_queue_id q2 = 0; CHECK(ctx, mlu_remote_queue_create(ctx, &q2), "queue_create_second");
  CHECK(ctx, mlu_remote_memcpy_h2d(ctx, da, 0, a, bytes, q2, NULL), "h2d_second_queue_a"); CHECK(ctx, mlu_remote_memcpy_h2d(ctx, db, 0, b, bytes, q2, NULL), "h2d_second_queue_b");
  CHECK(ctx, mlu_remote_operator_create(ctx, MLU_REMOTE_OPERATOR_VECTOR_ADD, &module, &kernel), "create_vector_add_second_queue"); CHECK(ctx, mlu_remote_operator_launch_vector_add(ctx, kernel, q2, dout, da, db, n, 1, &transfer), "launch_vector_add_second_queue"); CHECK(ctx, mlu_remote_operator_sync(ctx, transfer), "sync_vector_add_second_queue"); CHECK(ctx, mlu_remote_memcpy_d2h(ctx, out, dout, 0, bytes, q2, NULL), "d2h_vector_add_second_queue");
  for (uint32_t i = 0; i < n; ++i) if (out[i] != a[i] + b[i]) return report(ctx, "second_queue_value", -1);
  printf("PASS operator two_queue_ordering queue1=%llu queue2=%llu\n", (unsigned long long)q, (unsigned long long)q2); CHECK(ctx, mlu_remote_operator_destroy(ctx, MLU_REMOTE_OPERATOR_VECTOR_ADD), "destroy_vector_add_second_queue"); CHECK(ctx, mlu_remote_queue_destroy(ctx, q2), "queue_destroy_second");

  int stale = mlu_remote_operator_launch_vector_add(ctx, kernel, q, dout, da, db, n, 1, NULL); if (!stale) return report(ctx, "stale_kernel_rejected", stale);
  mlu_remote_free(ctx, da); mlu_remote_free(ctx, db); mlu_remote_free(ctx, dout); mlu_remote_free(ctx, tda); mlu_remote_free(ctx, tdb); mlu_remote_free(ctx, tdo); mlu_remote_queue_destroy(ctx, q); free(a); free(b); free(out); free(ta); free(tb); free(to); mlu_remote_disconnect(ctx);
  return 0;
}
