#ifndef MLU_GRAPH_KERNEL_WIRE_H
#define MLU_GRAPH_KERNEL_WIRE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "bang_image.h"
#define MLU_GRAPH_KERNEL_BYTES (2u*1024u*1024u)
#define MLU_GRAPH_KERNEL_REFS (BANG_MAX_ARGS+BANG_MAX_RELOCS)
/* Arguments: kind:u32,size:u32,allocation:u64,offset:u64,relocs:u32,
 * reserved:u32, size scalar bytes, then relocs*(offset:u32,reserved:u32,
 * allocation:u64,offset:u64). All pointer slots in scalar bytes are zero. */
static inline uint32_t gk_u32(const unsigned char *p) {
  return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static inline uint64_t gk_u64(const unsigned char *p) { return ((uint64_t)gk_u32(p)<<32)|gk_u32(p+4); }
static inline void gk_put32(unsigned char *p,uint32_t v) { for(unsigned i=0;i<4;i++) p[i]=(unsigned char)(v>>(24-8*i)); }
static inline void gk_put64(unsigned char *p,uint64_t v) { gk_put32(p,(uint32_t)(v>>32));gk_put32(p+4,(uint32_t)v); }
typedef struct {
  uint32_t kind,size,relocations;
  uint64_t allocation,offset;
  const unsigned char *scalar,*relocs;
} gk_arg;
static inline int gk_parse(const unsigned char *data,size_t bytes,unsigned count,gk_arg *args) {
  if(count>BANG_MAX_ARGS || bytes>MLU_GRAPH_KERNEL_BYTES) return -1;
  unsigned relocations=0;
  for(unsigned i=0;i<count;i++) {
    if(bytes<32) return -1;
    gk_arg *a=&args[i];
    *a=(gk_arg){.kind=gk_u32(data),.size=gk_u32(data+4),.allocation=gk_u64(data+8),
      .offset=gk_u64(data+16),.relocations=gk_u32(data+24)};
    if(a->kind<1 || a->kind>3 || !a->size || a->size>4096 || gk_u32(data+28) ||
       (a->kind==1 ? a->size!=8 || a->relocations : a->allocation || a->offset) ||
       (a->kind==2 && a->relocations) || (a->kind==3 && !a->relocations) ||
       a->relocations>BANG_MAX_RELOCS-relocations) return -1;
    relocations+=a->relocations;
    size_t n=32+a->size+24*a->relocations; if(n>bytes) return -1;
    a->scalar=data+32; a->relocs=a->scalar+a->size;
    if(a->kind==1 && memcmp(a->scalar,(unsigned char[8]){0},8)) return -1;
    unsigned previous=0;
    for(unsigned j=0;j<a->relocations;j++) {
      const unsigned char *r=a->relocs+24*j;unsigned off=gk_u32(r);
      if(off>a->size || a->size-off<8 || off%8 || gk_u32(r+4) || (j && off<previous+8) ||
         memcmp(a->scalar+off,(unsigned char[8]){0},8)) return -1;
      previous=off;
    }
    data+=n;bytes-=n;
  }
  return bytes ? -1 : 0;
}
#endif
