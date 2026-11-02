#include <cnrt.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

static void check(cnrtRet_t status, const char *what) {
  if (!status) return;
  fprintf(stderr, "%s: %d\n", what, (int)status); exit(2);
}
static void *open_module(const char *path) {
  void *module = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!module) { fprintf(stderr, "dlopen: %s\n", dlerror()); exit(3); }
  return module;
}
static void close_module(void *module) {
  if (dlclose(module)) { fprintf(stderr, "dlclose: %s\n", dlerror()); exit(4); }
}
int main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : "./build/libnative_bang_module.so";
  for (int round = 0; round < 20; ++round) {
    int *out = NULL, host[4]; cnrtQueue_t queue;
    check(cnrtMalloc((void **)&out, sizeof(host)), "malloc");
    check(cnrtQueueCreate(&queue), "queue create");
    void *a = open_module(path), *b = open_module(path);
    if (a != b) return 5;
    int (*submit)(int *, int, cnrtQueue_t) = dlsym(b, "module_submit");
    if (!submit) return 6;
    close_module(a); /* One loader reference must keep registration live. */
    if (submit(out, round + 9, queue)) return 7;
    close_module(b); /* Pending native work must finish before module unload. */
    check(cnrtGetLastError(), "module unregister");
    check(cnrtQueueSync(queue), "sync after dlclose");
    check(cnrtMemcpy(host, out, sizeof(host), cnrtMemcpyDevToHost), "copy");
    for (int i = 0; i < 4; ++i) if (host[i] != round + 9 + i + 37) return 8;
    check(cnrtQueueDestroy(queue), "queue destroy");
    check(cnrtFree(out), "free");
    if (round == 9) check(cnrtDeviceReset(), "reset between registrations");
  }
  puts("PASS 20 DSO reloads, retained loader reference, pending-work unload and reset");
  return 0;
}
