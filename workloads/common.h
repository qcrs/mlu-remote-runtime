#pragma once
#include <bang.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#define CHECK(call) do { cnrtRet_t status=(call); if(status!=cnrtSuccess){fprintf(stderr,"%s failed: %d\n",#call,(int)status);exit(2);} } while(0)

static int dimension(const char *text, int maximum) {
  char *end = NULL;
  errno = 0;
  long value = strtol(text, &end, 10);
  if (errno || end == text || *end || value < 1 || value > maximum) {
    fprintf(stderr, "invalid dimension: %s (expected 1..%d)\n", text, maximum);
    exit(2);
  }
  return (int)value;
}

static float *host_buffer(size_t count) {
  float *ptr = (float *)malloc(count * sizeof(float));
  if (!ptr) { fprintf(stderr, "host allocation failed\n"); exit(2); }
  /* An unwritten result must fail even if its reference happens to be zero. */
  for (size_t i = 0; i < count; ++i) ptr[i] = NAN;
  return ptr;
}

static float pixel(int x, int y) {
  /* Includes pixels below, at, and above the threshold in the first row. */
  if (y == 0 && x < 3) return (float)(126 + x);
  return (float)((x * 37 + y * 53 + (x * y) % 97) % 256);
}

static float *device_copy(const float *host, size_t count) {
  float *ptr = NULL;
  CHECK(cnrtMalloc((void **)&ptr, count * sizeof(float)));
  CHECK(cnrtMemcpy(ptr, (void *)host, count * sizeof(float), cnrtMemcpyHostToDev));
  return ptr;
}

static int report(const char *name, const char *shape, const float *reference,
                  const float *actual, size_t count, float tolerance, size_t extra_mismatch = 0) {
  double cpu = 0, mlu = 0;
  float error = 0;
  size_t mismatch = extra_mismatch;
  for (size_t i = 0; i < count; ++i) {
    cpu += reference[i]; mlu += actual[i];
    float delta = fabsf(reference[i] - actual[i]);
    if (!isfinite(reference[i]) || !isfinite(actual[i])) delta = INFINITY;
    if (delta > tolerance) ++mismatch;
    if (delta > error) error = delta;
  }
  printf("WORKLOAD=%s\nSHAPE=%s\nCPU_CHECKSUM=%.17g\nMLU_CHECKSUM=%.17g\n"
         "MAX_ABS_ERROR=%.9g\nTOLERANCE=%.9g\nMISMATCH=%zu\nRESULT=%s\n",
         name, shape, cpu, mlu, error, tolerance, mismatch, mismatch ? "FAIL" : "PASS");
  return mismatch ? 1 : 0;
}
