#define _GNU_SOURCE
#include "bang_image.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Compiler metadata describes types, never pointer-shaped scalar values.
 * The CNFB payload fingerprint survives adding a non-allocated ELF section.
 * It binds a compiler artifact pair, not an authentication boundary. */
static uint32_t u32(const void *p) { uint32_t v; memcpy(&v,p,4); return v; }
static uint64_t u64(const void *p) { uint64_t v; memcpy(&v,p,8); return v; }
static uint64_t fingerprint(const void *data,size_t n) {
  uint64_t hash=14695981039346656037ull;
  for(size_t i=0;i<n;++i) hash=(hash^((const unsigned char *)data)[i])*1099511628211ull;
  return hash;
}
static int classify(const void *elf,size_t elf_size,const void *image,size_t image_size,
                    const char *symbol,const bang_param_layout *layout,bang_param_types *types) {
  const unsigned char *p=NULL; size_t n=0;
  int st=bang_image_section(elf,elf_size,".mlu.remote.abi",&p,&n);
  if(st) return st;
  if(n<12 || (memcmp(p,"MLUABI01",8) && memcmp(p,"MLUABI02",8))) return -1;
  int v2=!memcmp(p,"MLUABI02",8), found=0, stale=0;
  uint32_t count=u32(p+8); if(count>4096) return -1;
  const unsigned char *payload=image; size_t payload_size=image_size;
  if(v2 && bang_image_section(image,image_size,".cn_fatbin",&payload,&payload_size)) return -1;
  uint64_t hash=fingerprint(payload,payload_size);
  size_t cursor=12;
  bang_param_types candidate={0};
  for(unsigned j=0;j<count;++j) {
    size_t length=276+BANG_MAX_ARGS*5;
    if(v2) {
      if(n-cursor<4) return -1;
      length=u32(p+cursor); cursor+=4;
    }
    if(length>n-cursor || length<(v2 ? 280u : 276u)) return -1;
    const unsigned char *r=p+cursor; cursor+=length;
    if(!memchr(r+16,0,256)) return -1;
    uint32_t argc=u32(r+272), fields=v2 ? u32(r+276) : 0;
    if(argc>BANG_MAX_ARGS || fields>BANG_MAX_RELOCS ||
       (v2 && length!=280ull+argc*5ull+fields*8ull)) return -1;
    if(strcmp((const char *)r+16,symbol)) continue;
    stale=1;
    if(u64(r)!=payload_size || u64(r+8)!=hash) continue;
    if(found++ || argc!=layout->count) return -1;
    size_t start=v2 ? 280 : 276;
    for(unsigned k=0;k<argc;++k) {
      uint32_t size=u32(r+start+k*5); unsigned char kind=r[start+4+k*5];
      if(size!=layout->sizes[k] || !size || size>4096 || kind>2 || (kind==1 && size!=8)) return -1;
      candidate.kind[k]=kind;
    }
    candidate.count=fields;
    for(unsigned k=0;k<fields;++k) {
      const unsigned char *f=r+start+argc*5+k*8;
      uint32_t arg=u32(f), off=u32(f+4);
      if(arg>=argc || candidate.kind[arg]!=2 || off>layout->sizes[arg] || layout->sizes[arg]-off<8 || off%8 ||
         (k && (arg<candidate.fields[k-1].argument ||
          (arg==candidate.fields[k-1].argument && off<candidate.fields[k-1].offset+8)))) return -1;
      candidate.fields[k]=(bang_pointer_field){arg,off};
    }
  }
  if(cursor!=n) return -1;
  if(found) { *types=candidate; return 0; }
  return stale ? -1 : 1;
}
int bang_metadata_classify_image(const void *image,size_t size,const char *symbol,
                                  const bang_param_layout *layout,bang_param_types *types) {
  return classify(image,size,image,size,symbol,layout,types);
}
int bang_metadata_classify(const void *host,const void *image,size_t size,const char *symbol,
                           const bang_param_layout *layout,bang_param_types *types) {
  Dl_info info;
  if(!dladdr(host,&info) || !info.dli_fname) return 1;
  FILE *f=fopen(info.dli_fname,"rb"); if(!f) return -1;
  unsigned char *data=NULL; int result=-1;
  if(fseek(f,0,SEEK_END)) goto done;
  long length=ftell(f);
  if(length<64 || length>256*1024*1024 || fseek(f,0,SEEK_SET)) goto done;
  data=malloc((size_t)length); if(!data || fread(data,1,length,f)!=(size_t)length) goto done;
  result=classify(data,(size_t)length,image,size,symbol,layout,types);
done:
  free(data); fclose(f); return result;
}
