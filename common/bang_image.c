#include "bang_image.h"
#include "remote_protocol.h"
#include <dlfcn.h>
#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "The measured BANG image ABI requires a little-endian host"
#endif
_Static_assert(sizeof(void *) == 8 && sizeof(long) == 8, "BANG image ABI requires LP64");

static uint32_t rd32(const unsigned char *p) { uint32_t v; memcpy(&v,p,4); return v; }
static uint64_t rd64(const unsigned char *p) { uint64_t v; memcpy(&v,p,8); return v; }
static int within(uint64_t off, uint64_t len, size_t n) { return off <= n && len <= n-off; }
static int header(const unsigned char *p, size_t n, Elf64_Ehdr *e) {
  if (n < sizeof(*e)) return -1;
  memcpy(e,p,sizeof(*e));
  if (memcmp(e->e_ident,ELFMAG,4) || e->e_ident[EI_CLASS]!=ELFCLASS64 ||
      e->e_ident[EI_DATA]!=ELFDATA2LSB || e->e_shentsize!=sizeof(Elf64_Shdr) ||
      !e->e_shnum || !within(e->e_shoff,(uint64_t)e->e_shnum*sizeof(Elf64_Shdr),n) ||
      e->e_shstrndx>=e->e_shnum) return -1;
  return 0;
}
static Elf64_Shdr section(const unsigned char *p, const Elf64_Ehdr *e, unsigned i) {
  Elf64_Shdr s; memcpy(&s,p+e->e_shoff+i*sizeof(s),sizeof(s)); return s;
}
int bang_image_span(const void *image, size_t n, size_t *span) {
  const unsigned char *p=image; Elf64_Ehdr e;
  if (!p || !span || header(p,n,&e)) return -1;
  size_t end=e.e_shoff+e.e_shnum*sizeof(Elf64_Shdr);
  for (unsigned i=0;i<e.e_shnum;++i) {
    Elf64_Shdr s=section(p,&e,i);
    if (s.sh_type==SHT_NOBITS) continue;
    if (!within(s.sh_offset,s.sh_size,n)) return -1;
    if (s.sh_offset+s.sh_size>end) end=s.sh_offset+s.sh_size;
  }
  *span=end; return 0;
}
static int params_elf(const unsigned char *p, size_t n, const char *symbol,
                       bang_param_layout *out, size_t host_size, unsigned depth) {
  Elf64_Ehdr e; size_t exact;
  if (depth>2 || bang_image_span(p,n,&exact) || header(p,n,&e)) return -1;
  Elf64_Shdr strings=section(p,&e,e.e_shstrndx);
  if (strings.sh_type!=SHT_STRTAB) return -1;
  char target[300]; unsigned layouts=0;
  if (symbol && snprintf(target,sizeof(target),".params.text.%s",symbol)>=(int)sizeof(target)) return -1;
  for (unsigned i=0;i<e.e_shnum;++i) {
    Elf64_Shdr s=section(p,&e,i);
    if (s.sh_name>=strings.sh_size) return -1;
    const char *name=(const char *)p+strings.sh_offset+s.sh_name;
    if (!memchr(name,0,strings.sh_size-s.sh_name)) return -1;
    if (host_size && s.sh_type==SHT_SYMTAB) {
      if (s.sh_entsize!=sizeof(Elf64_Sym) || s.sh_size%sizeof(Elf64_Sym) || s.sh_link>=e.e_shnum) return -1;
      Elf64_Shdr names=section(p,&e,s.sh_link);
      if (names.sh_type!=SHT_STRTAB) return -1;
      for (size_t off=0;off<s.sh_size;off+=sizeof(Elf64_Sym)) {
        Elf64_Sym v; memcpy(&v,p+s.sh_offset+off,sizeof(v));
        if (v.st_name>=names.sh_size) return -1;
        const char *vn=(const char *)p+names.sh_offset+v.st_name;
        if (!memchr(vn,0,names.sh_size-v.st_name)) return -1;
        if (strcmp(vn,symbol)) continue;
        if (ELF64_ST_TYPE(v.st_info)!=STT_OBJECT || !v.st_shndx || v.st_shndx>=e.e_shnum ||
            host_size>MLU_REMOTE_MAX_PAYLOAD || v.st_size!=((host_size+63u)&~63ull)) return -1;
        Elf64_Shdr data=section(p,&e,v.st_shndx);
        if (data.sh_name>=strings.sh_size || data.sh_type!=SHT_PROGBITS) return -1;
        const char *dn=(const char *)p+strings.sh_offset+data.sh_name;
        if (!memchr(dn,0,strings.sh_size-data.sh_name)) return -1;
        char expected[300];
        if (snprintf(expected,sizeof(expected),".data.gdram_uninit.%s",symbol)>=(int)sizeof(expected)) return -1;
        if (!strcmp(dn,expected)) return data.sh_size || v.st_value ? -1 : 0;
        if (snprintf(expected,sizeof(expected),".data.gdram_init.%s",symbol)>=(int)sizeof(expected)) return -1;
        return strcmp(dn,expected) || v.st_value || data.sh_size!=v.st_size ? -1 : 0;
      }
    }
    if (!host_size && (symbol ? !strcmp(name,target) : !strncmp(name,".params.text.",13))) {
      if (s.sh_type!=SHT_PROGBITS) return -1;
      if (s.sh_size%8 || s.sh_size/8>BANG_MAX_ARGS) return -1;
      out->count=s.sh_size/8;
      /* CNCC stores all sizes, then all offsets (verified with mixed ABI). */
      for (unsigned j=0;j<out->count;++j) {
        out->sizes[j]=rd32(p+s.sh_offset+4*j);
        out->offsets[j]=rd32(p+s.sh_offset+4*(out->count+j));
        if (!out->sizes[j] || out->sizes[j]>4096 || out->offsets[j]>65536 ||
            (j && out->offsets[j]<out->offsets[j-1]+out->sizes[j-1])) return -1;
      }
      if (symbol) return 0;
      ++layouts;
    }
    if (!strcmp(name,".cn_fatbin")) {
      if (s.sh_type!=SHT_PROGBITS) return -1;
      const unsigned char *f=p+s.sh_offset; size_t fn=s.sh_size;
      if (fn<16 || memcmp(f,"CNFB\002\000\020\000",8) || rd64(f+8)>fn-16) return -1;
      int found=0, images=0;
      /* Only the measured single-image format is accepted. A new architecture
       * bundle must get an explicit selection policy before it is supported. */
      for (size_t off=16;off+36<=fn;off+=8) {
        if (memcmp(f+off,"CNFB\002\200\020\000",8)) continue;
        if (++images > 1) return -1;
        uint64_t packed=rd64(f+off+8), expanded=rd64(f+off+24);
        uint32_t compressed=rd32(f+off+32);
        if (packed!=20ull+compressed || !within(off+16,packed,fn) ||
            !expanded || expanded>MLU_REMOTE_MAX_PAYLOAD ||
            /* Measured field +20 is the 64-byte padded device image span;
             * +24 is the actual LZ4 output size, not always aligned. */
            rd32(f+off+20)!=((expanded+63u)&~63ull)) return -1;
        void *lib=dlopen("liblz4.so.1",RTLD_LAZY|RTLD_LOCAL);
        if (!lib) return -1;
        int (*unpack)(const char *,char *,int,int)=dlsym(lib,"LZ4_decompress_safe");
        unsigned char *buf=malloc(expanded);
        int ok=buf && unpack && unpack((const char *)f+off+36,(char *)buf,compressed,expanded)==(int)expanded;
        bang_param_layout candidate={0};
        if (ok && !params_elf(buf,expanded,symbol,&candidate,host_size,depth+1)) {
          if (found++) { free(buf); dlclose(lib); return -1; }
          *out=candidate;
        }
        free(buf); dlclose(lib);
      }
      return found==1 ? 0 : -1;
    }
  }
  return !symbol && layouts ? 0 : -1;
}
int bang_image_validate(const void *image, size_t n) {
  bang_param_layout unused={0}; size_t exact=0;
  if (!image || n>MLU_REMOTE_MAX_PAYLOAD || bang_image_span(image,n,&exact) || exact!=n) return -1;
  return params_elf(image,n,NULL,&unused,0,0);
}
int bang_image_params(const void *image, size_t n, const char *symbol, bang_param_layout *out) {
  if (!image || !symbol || !out || n>MLU_REMOTE_MAX_PAYLOAD) return -1;
  memset(out,0,sizeof(*out)); return params_elf(image,n,symbol,out,0,0);
}
int bang_image_device_variable(const void *image, size_t n, const char *symbol,
                                size_t host_size) {
  if (!image || !symbol || !*symbol || !host_size || n>MLU_REMOTE_MAX_PAYLOAD) return -1;
  bang_param_layout unused={0};
  return params_elf(image,n,symbol,&unused,host_size,0);
}

int bang_image_section(const void *image,size_t n,const char *name,
                         const unsigned char **data,size_t *size) {
  const unsigned char *p=image; Elf64_Ehdr e; size_t exact;
  if(!p || !name || !data || !size || bang_image_span(p,n,&exact) || header(p,n,&e)) return -1;
  Elf64_Shdr strings=section(p,&e,e.e_shstrndx);
  if(strings.sh_type!=SHT_STRTAB) return -1;
  for(unsigned i=0;i<e.e_shnum;++i) {
    Elf64_Shdr s=section(p,&e,i);
    if(s.sh_name>=strings.sh_size || !memchr(p+strings.sh_offset+s.sh_name,0,strings.sh_size-s.sh_name)) return -1;
    if(strcmp((const char *)p+strings.sh_offset+s.sh_name,name)) continue;
    if(s.sh_type!=SHT_PROGBITS) return -1;
    *data=p+s.sh_offset; *size=s.sh_size; return 0;
  }
  return 1;
}
