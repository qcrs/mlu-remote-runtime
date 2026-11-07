#include <cnrt.h>
#include <cn_api.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define OK(e) do { int st = (int)(e); if (st) { fprintf(stderr, "%s: %d at %d\n", #e, st, __LINE__); exit(2); } } while (0)
#define REQUIRE(e) do { if (!(e)) { fprintf(stderr, "%s at %d\n", #e, __LINE__); exit(3); } } while (0)

static size_t usage(const char *where, size_t expected) {
  size_t bytes = SIZE_MAX;
  OK(cnrtDeviceQueryKernelMemoryUsage(&bytes));
  REQUIRE(bytes == expected);
  printf("usage %s %zu\n", where, bytes);
  return bytes;
}
static size_t image_usage(const char *path) {
  size_t bytes = 0; OK(cnModuleQueryMemoryUsage(path, &bytes)); REQUIRE(bytes > 0);
  return bytes;
}
static void *open_module(const char *path) {
  void *m = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!m) { fprintf(stderr, "dlopen: %s\n", dlerror()); exit(4); }
  return m;
}
static void submit(void *module, int seed, int variant) {
  int (*launch)(int *, int) = dlsym(module, "module_submit"); REQUIRE(launch);
  int *device = NULL, host = -1; OK(cnrtMalloc((void **)&device, sizeof(host)));
  OK(launch(device, seed)); OK(cnrtSyncDevice());
  OK(cnrtMemcpy(&host, device, sizeof(host), cnrtMemcpyDevToHost));
  REQUIRE(host == 31 + seed + variant); OK(cnrtFree(device));
  printf("globals value %d\n", host);
}

int main(int argc, char **argv) {
  REQUIRE(argc == 6);
  OK(cnInit(0));
  CNdev dev; CNcontext ctx; OK(cnDeviceGet(&dev, 0)); OK(cnCtxCreate(&ctx, 0, dev));
  size_t a = image_usage(argv[2]), b = image_usage(argv[4]);
  if (!strcmp(argv[1], "driver")) {
    usage("empty", 0);
    CNmodule module; CNkernel kernel; CNaddr output;
    OK(cnModuleLoad(argv[2], &module)); usage("driver-load", 0);
    OK(cnModuleGetKernel(module, "_Z14module_globalsPii", &kernel));
    OK(cnMalloc(&output, sizeof(int))); int seed = 5, host = -1;
    void *params[] = {&output, &seed};
    OK(cnInvokeKernel(kernel, 1, 1, 1, CN_KERNEL_CLASS_BLOCK, 0, NULL, params, NULL));
    OK(cnCtxSync()); OK(cnMemcpyDtoH(&host, output, sizeof(host))); REQUIRE(host == 37);
    usage("driver-launch", 0);
    OK(cnFree(output)); OK(cnModuleUnload(module)); usage("driver-unload", 0);
  } else {
    REQUIRE(!strcmp(argv[1], "runtime"));
    usage("empty", 0);
    void *m = open_module(argv[3]); usage("registered", a);
    int (*symbols)(void) = dlsym(m, "module_symbols"); REQUIRE(symbols); OK(symbols());
    void *reference = open_module(argv[3]); REQUIRE(reference == m); usage("second-reference", a);
    OK(dlclose(reference)); usage("retained-reference", a);
    void *other = open_module(argv[5]); usage("two-images", a + b);
    submit(m, 5, 1); submit(other, 8, 2); usage("launched", a + b);
    OK(cnrtDeviceReset()); usage("reset", a + b);
    submit(m, 6, 1); usage("reloaded", a + b);
    OK(dlclose(other)); usage("one-unregistered", a);
    OK(dlclose(m)); usage("all-unregistered", 0);
    /* Re-register identical bytes after reset and an earlier final dlclose. */
    m = open_module(argv[3]); usage("registered-again", a);
    submit(m, 7, 1); OK(dlclose(m)); usage("closed-again", 0);
  }
  /* Runtime reset may invalidate the original native Driver context. */
  if (!strcmp(argv[1], "driver")) OK(cnCtxDestroy(ctx));
  puts("PASS registered-image accounting and module globals");
  return 0;
}
