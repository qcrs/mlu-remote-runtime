/* Native architecture discovery only. The owner allocation remains alive.
 * A valid same-process address is tested from a distinct native Context. */
#include <cn_api.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define COUNT 16
#define BYTES (COUNT * sizeof(int))
static int observe(const char *call, int status) {
  fputs("{\"call\":\"", stdout);
  for (const char *p = call; *p; ++p) {
    if (*p == '"' || *p == '\\') putchar('\\');
    putchar(*p);
  }
  printf("\",\"status\":%d}\n", status);
  fflush(stdout);
  return status;
}
#define OK(e) do { if (observe(#e, (e))) exit(2); } while (0)
static void check(const char *name, int pass) {
  printf("{\"check\":\"%s\",\"pass\":%s}\n", name, pass ? "true" : "false");
  fflush(stdout);
  if (!pass) exit(3);
}
typedef struct work {
  CNaddr shared;
  const char *mode, *image;
  int same, supported, expected_owner;
} work;

static void *access_in_context(void *arg) {
  work *w = arg;
  CNcontext importer = NULL;
  if (!w->same) OK(cnCtxCreate(&importer, 0, 0));
  CNaddr local = 0, output = 0;
  int input[COUNT], values[COUNT];
  for (int i = 0; i < COUNT; ++i) input[i] = 100 + i;
  OK(cnMalloc(&local, BYTES));
  OK(cnMalloc(&output, BYTES));
  OK(cnMemcpyHtoD(local, input, BYTES));
  int status = 0;
  w->expected_owner = 17;
  if (!strcmp(w->mode, "read")) {
    status = observe("foreign_DtoH", cnMemcpyDtoH(values, w->shared, BYTES));
    if (!status) for (int i = 0; i < COUNT; ++i) check("foreign_read_value", values[i] == 17);
  } else if (!strcmp(w->mode, "write")) {
    for (int i = 0; i < COUNT; ++i) input[i] = 23;
    status = observe("foreign_HtoD", cnMemcpyHtoD(w->shared, input, BYTES));
    if (!status) w->expected_owner = 23;
  } else if (!strcmp(w->mode, "copy")) {
    status = observe("foreign_to_local_DtoD", cnMemcpyDtoD(output, w->shared, BYTES));
    if (!status) {
      OK(cnMemcpyDtoH(values, output, BYTES));
      for (int i = 0; i < COUNT; ++i) check("foreign_copy_value", values[i] == 17);
    }
  } else if (!strcmp(w->mode, "memset")) {
    status = observe("foreign_memset", cnMemsetD32(w->shared, 29, COUNT));
    if (!status) w->expected_owner = 29;
  } else {
    CNmodule module;
    CNkernel kernel;
    CNqueue queue;
    OK(cnModuleLoad(w->image, &module));
    OK(cnModuleGetKernel(module, "ipc_context_mix", &kernel));
    OK(cnCreateQueue(&queue, 0));
    void *params[] = {&w->shared, &local, &output};
    status = observe("mixed_kernel_submit", cnInvokeKernel(kernel, 1, 1, 1, CN_KERNEL_CLASS_BLOCK, 0, queue, params, NULL));
    if (!status) status = observe("mixed_kernel_complete", cnQueueSync(queue));
    if (!status) {
      OK(cnMemcpyDtoH(values, output, BYTES));
      for (int i = 0; i < COUNT; ++i) check("mixed_kernel_value", values[i] == 117 + i);
    }
    OK(cnDestroyQueue(queue));
    OK(cnModuleUnload(module));
  }
  w->supported = status == 0;
  printf("{\"access_supported\":%s,\"operation_status\":%d}\n", w->supported ? "true" : "false", status);
  OK(cnFree(output));
  OK(cnFree(local));
  if (importer) OK(cnCtxDestroy(importer));
  return NULL;
}

int main(int argc, char **argv) {
  if (argc != 3) return 64;
  const char *mode = argv[1];
  check("known_mode", !strcmp(mode, "read") || !strcmp(mode, "write") || !strcmp(mode, "copy") ||
        !strcmp(mode, "memset") || !strcmp(mode, "kernel") || !strcmp(mode, "thread-kernel") || !strcmp(mode, "same-kernel"));
  printf("{\"pid\":%ld,\"mode\":\"%s\",\"remote\":false}\n", (long)getpid(), mode);
  OK(cnInit(0));
  CNcontext owner;
  OK(cnCtxCreate(&owner, 0, 0));
  work w = {.mode = mode, .image = argv[2], .same = !strcmp(mode, "same-kernel")};
  OK(cnMalloc(&w.shared, BYTES));
  int values[COUNT];
  for (int i = 0; i < COUNT; ++i) values[i] = 17;
  OK(cnMemcpyHtoD(w.shared, values, BYTES));
  if (!strcmp(mode, "thread-kernel")) {
    pthread_t thread;
    check("pthread_create", pthread_create(&thread, NULL, access_in_context, &w) == 0);
    check("pthread_join", pthread_join(thread, NULL) == 0);
  } else access_in_context(&w);
  OK(cnCtxSetCurrent(owner));
  OK(cnMemcpyDtoH(values, w.shared, BYTES));
  for (int i = 0; i < COUNT; ++i) check("owner_value_after_importer_destroy", values[i] == w.expected_owner);
  if (w.same) check("same_context_positive_baseline", w.supported);
  OK(cnFree(w.shared));
  OK(cnCtxDestroy(owner));
  puts("{\"complete\":true}");
  return 0;
}
