#define _GNU_SOURCE
#include "bang_image.h"
#include <dlfcn.h>
#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Local compiler metadata only. The fingerprint detects stale compilation
 * artifacts; it is not an authentication mechanism. Native ELF sizes/offsets
 * are independently checked before any argument is copied. */
static uint32_t u32(const void *p) { uint32_t v; memcpy(&v,p,4); return v; }
static uint64_t u64(const void *p) { uint64_t v; memcpy(&v,p,8); return v; }
static int fits(uint64_t off,uint64_t size,size_t n) { return off<=n && size<=n-off; }
int bang_metadata_classify(const void *host,const void *image,size_t size,const char *symbol,
                           const bang_param_layout *layout,unsigned char pointer[BANG_MAX_ARGS]) {
  Dl_info info;
  if(!dladdr(host,&info) || !info.dli_fname) return 1;
  FILE *f=fopen(info.dli_fname,"rb"); if(!f) return -1;
  unsigned char *data=NULL; int result=-1;
  if(fseek(f,0,SEEK_END)) goto done;
  long length=ftell(f);
  if(length<(long)sizeof(Elf64_Ehdr) || length>256*1024*1024 || fseek(f,0,SEEK_SET)) goto done;
  data=malloc((size_t)length); if(!data || fread(data,1,length,f)!=(size_t)length) goto done;
  Elf64_Ehdr e; memcpy(&e,data,sizeof(e));
  if(memcmp(e.e_ident,ELFMAG,4) || e.e_ident[EI_CLASS]!=ELFCLASS64 || e.e_ident[EI_DATA]!=ELFDATA2LSB ||
     e.e_shentsize!=sizeof(Elf64_Shdr) || e.e_shstrndx>=e.e_shnum ||
     !fits(e.e_shoff,(uint64_t)e.e_shnum*sizeof(Elf64_Shdr),length)) goto done;
  Elf64_Shdr strings; memcpy(&strings,data+e.e_shoff+e.e_shstrndx*sizeof(strings),sizeof(strings));
  if(!fits(strings.sh_offset,strings.sh_size,length)) goto done;
  result=1;
  for(unsigned i=0;i<e.e_shnum;++i) {
    Elf64_Shdr s; memcpy(&s,data+e.e_shoff+i*sizeof(s),sizeof(s));
    if(s.sh_name>=strings.sh_size) { result=-1; break; }
    const char *name=(const char *)data+strings.sh_offset+s.sh_name;
    if(!memchr(name,0,strings.sh_size-s.sh_name)) { result=-1; break; }
    if(strcmp(name,".mlu.remote.abi")) continue;
    result=-1;
    if(!fits(s.sh_offset,s.sh_size,length) || s.sh_size<12) break;
    const unsigned char *p=data+s.sh_offset;
    const size_t record=276+BANG_MAX_ARGS*5;
    uint32_t count=u32(p+8);
    if(memcmp(p,"MLUABI01",8) || count>4096 || s.sh_size!=12+(uint64_t)count*record) break;
    uint64_t hash=14695981039346656037ull;
    for(size_t j=0;j<size;++j) hash=(hash^((const unsigned char *)image)[j])*1099511628211ull;
    result=1;
    for(unsigned j=0;j<count;++j) {
      const unsigned char *r=p+12+j*record;
      if(!memchr(r+16,0,256)) { result=-1; break; }
      if(strcmp((const char *)r+16,symbol)) continue;
      result=-1;
      /* Different modules may contain the same symbol. Find the image/symbol
       * pair, while keeping stale same-name metadata an explicit rejection. */
      if(u64(r)!=size || u64(r+8)!=hash) continue;
      if(u32(r+272)!=layout->count) break;
      result=0;
      for(unsigned k=0;k<layout->count;++k) {
        uint32_t n=u32(r+276+k*5); unsigned char kind=r[280+k*5];
        if(n!=layout->sizes[k] || !n || n>4096 || kind>2 || (kind==1 && n!=8)) { result=-1; break; }
        pointer[k]=kind;
      }
      break;
    }
    break;
  }
done:
  free(data); fclose(f); return result;
}
