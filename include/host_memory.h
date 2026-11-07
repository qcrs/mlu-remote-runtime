#ifndef MLU_HOST_MEMORY_H
#define MLU_HOST_MEMORY_H
#include <stddef.h>
#include <stdint.h>
/* Process-wide client pages, shared by both vendor shims. No SDK dependency. */
typedef struct mlu_host_allocation mlu_host_allocation;
int mlu_host_alloc(void **out, size_t bytes);
int mlu_host_free(void *ptr);
/* 1: live registered host range; 0: pageable; -1: dead/out of bounds. */
int mlu_host_range(const void *ptr, size_t bytes);
int mlu_host_acquire(void *ptr, size_t bytes, mlu_host_allocation **lease);
void mlu_host_release(mlu_host_allocation *lease);
int mlu_host_ipc_export(void *ptr,uint64_t *token);
int mlu_host_ipc_import(void **ptr,uint64_t token);
int mlu_host_ipc_close(void *ptr);
#endif
