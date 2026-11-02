#ifndef MLU_BANG_IMAGE_H
#define MLU_BANG_IMAGE_H
#include <stddef.h>
#include <stdint.h>
#define BANG_MAX_ARGS 256
typedef struct {
  uint32_t count;
  uint32_t sizes[BANG_MAX_ARGS];
  uint32_t offsets[BANG_MAX_ARGS];
} bang_param_layout;
/* CNCC 4.5.1 ELF64 LE / CNFB v2. No vendor runtime dependency. */
int bang_image_span(const void *image, size_t available, size_t *span);
int bang_image_validate(const void *image, size_t size);
int bang_classify_params(const char *symbol, const bang_param_layout *layout,
                           unsigned char pointer[BANG_MAX_ARGS]);
int bang_image_params(const void *image, size_t size, const char *symbol,
                      bang_param_layout *layout);
/* Measured uninitialized device object; no host symbol address API implied. */
int bang_image_device_variable(const void *image, size_t size, const char *symbol,
                                size_t host_size);
#endif
