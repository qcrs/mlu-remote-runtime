#ifndef MLU_BANG_IMAGE_H
#define MLU_BANG_IMAGE_H
#include <stddef.h>
#include <stdint.h>
#define BANG_MAX_ARGS 256
#define BANG_MAX_RELOCS 512
typedef struct {
  uint32_t count;
  uint32_t sizes[BANG_MAX_ARGS];
  uint32_t offsets[BANG_MAX_ARGS];
} bang_param_layout;
typedef struct {
  uint32_t argument;
  uint32_t offset;
} bang_pointer_field;
typedef struct {
  unsigned char kind[BANG_MAX_ARGS];
  uint32_t count;
  bang_pointer_field fields[BANG_MAX_RELOCS];
} bang_param_types;
/* CNCC 4.5.1 ELF64 LE / CNFB v2. No vendor runtime dependency. */
int bang_image_span(const void *image, size_t available, size_t *span);
int bang_image_validate(const void *image, size_t size);
int bang_classify_params(const char *symbol, const bang_param_layout *layout,
                           unsigned char pointer[BANG_MAX_ARGS]);
/* 0 = compiler types found, 1 = absent (legacy demangler allowed), -1 = invalid. */
int bang_metadata_classify(const void *host, const void *image, size_t size,
                           const char *symbol, const bang_param_layout *layout,
                           bang_param_types *types);
int bang_metadata_classify_image(const void *image,size_t size,const char *symbol,
                                  const bang_param_layout *layout,bang_param_types *types);
/* Top-level, bounded ELF section lookup: 0 found, 1 absent, -1 invalid. */
int bang_image_section(const void *image,size_t n,const char *name,
                         const unsigned char **data,size_t *size);
int bang_image_params(const void *image, size_t size, const char *symbol,
                      bang_param_layout *layout);
/* Measured initialized/uninitialized GDRAM object; no address API implied. */
int bang_image_device_variable(const void *image, size_t size, const char *symbol,
                                size_t host_size);
#endif
