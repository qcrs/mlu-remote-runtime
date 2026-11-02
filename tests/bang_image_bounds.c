#include "bang_image.h"
#include <assert.h>
#include <elf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exercise the untrusted image boundary under ASan/UBSan, using a real CNCC
 * image plus truncated and mutated copies. No native driver is loaded. */
static void variable_bounds(const char *path) {
  FILE *f = fopen(path, "rb"); assert(f);
  assert(!fseek(f, 0, SEEK_END)); long length = ftell(f); assert(length > 0);
  rewind(f); size_t n = (size_t)length;
  unsigned char *image = malloc(n), *copy = malloc(n); assert(image && copy);
  assert(fread(image, 1, n, f) == n); fclose(f);
  assert(!bang_image_device_variable(image, n, "module_state", 16));
  assert(!bang_image_validate(image,n));
  assert(bang_image_device_variable(image, n, "module_state", 0));
  assert(bang_image_device_variable(image, n, "module_state", 65));
  assert(bang_image_device_variable(image, n, "module_state", SIZE_MAX));
  assert(bang_image_device_variable(image, n, "absent", 16));
  for (size_t cut = 0; cut < n; cut += 17) {
    assert(bang_image_device_variable(image, cut, "module_state", 16));
    assert(bang_image_validate(image,cut));
  }
  uint32_t seed = 19;
  for (unsigned i = 0; i < 2000; ++i) {
    memcpy(copy, image, n);
    for (unsigned j = 0; j < 5; ++j) {
      seed = seed * 1664525u + 1013904223u;
      copy[seed % n] ^= (unsigned char)(1u << (seed % 8));
    }
    bang_image_device_variable(copy, n, "module_state", 16);
    bang_image_validate(copy,n);
  }
  free(copy); free(image);
  puts("PASS device variable metadata, sizes, truncation and 2000 mutations");
}

int main(int argc, char **argv) {
  assert(argc == 3 || argc == 5);
  FILE *f = fopen(argv[1], "rb"); assert(f);
  assert(!fseek(f, 0, SEEK_END)); long length = ftell(f); assert(length > 0);
  rewind(f); size_t n = (size_t)length;
  unsigned char *image = malloc(n), *copy = malloc(n); assert(image && copy);
  assert(fread(image, 1, n, f) == n); fclose(f);
  size_t span = 0; bang_param_layout layout;
  assert(!bang_image_span(image, n, &span) && span == n);
  assert(!bang_image_validate(image,n));
  assert(!bang_image_params(image, n, argv[2], &layout));
  assert(layout.count == 4 && layout.sizes[0] == 8 && layout.sizes[3] == 4);
  assert(bang_image_params(image, n, "missing_symbol", &layout));
  for (size_t cut = 0; cut < n; cut += 17) {
    unsigned char *short_image = malloc(cut ? cut : 1); assert(short_image);
    memcpy(short_image, image, cut);
    assert(bang_image_span(short_image, cut, &span));
    assert(bang_image_params(short_image, cut, argv[2], &layout));
    assert(bang_image_validate(short_image,cut));
    free(short_image);
  }
  /* Corrupt every section's extent and the string table's type. */
  Elf64_Ehdr e; memcpy(&e, image, sizeof(e));
  for (unsigned i = 0; i < e.e_shnum; ++i) {
    memcpy(copy, image, n);
    Elf64_Shdr s; size_t off = e.e_shoff + i * sizeof(s);
    memcpy(&s, copy + off, sizeof(s)); s.sh_type = SHT_PROGBITS;
    s.sh_offset = UINT64_MAX - 4; s.sh_size = 16;
    memcpy(copy + off, &s, sizeof(s));
    assert(bang_image_span(copy, n, &span));
    assert(bang_image_params(copy, n, argv[2], &layout));
    assert(bang_image_validate(copy,n));
  }
  memcpy(copy, image, n);
  Elf64_Shdr s; size_t off = e.e_shoff + e.e_shstrndx * sizeof(s);
  memcpy(&s, copy + off, sizeof(s)); s.sh_type = SHT_NOBITS;
  memcpy(copy + off, &s, sizeof(s));
  assert(bang_image_params(copy, n, argv[2], &layout));
  assert(bang_image_validate(copy,n));
  uint32_t seed = 42;
  for (unsigned i = 0; i < 2000; ++i) {
    memcpy(copy, image, n);
    for (unsigned j = 0; j < 5; ++j) {
      seed = seed * 1664525u + 1013904223u;
      copy[seed % n] ^= (unsigned char)(1u << (seed % 8));
    }
    /* Some mutations affect irrelevant bytes and are valid. Only memory
     * safety is asserted here; no malformed image is given to CNDrv. */
    bang_image_span(copy, n, &span);
    bang_image_params(copy, n, argv[2], &layout);
    bang_image_validate(copy,n);
  }
  free(copy); free(image);
  puts("PASS bounded ELF/CNFB parser: native image, truncation, extents, NOBITS, 2000 mutations");
  if (argc == 5) { variable_bounds(argv[3]); variable_bounds(argv[4]); }
}
