#include "bang_image.h"
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
/* Only complete primitive signatures validated against device parameter metadata. */
int bang_classify_params(const char *symbol, const bang_param_layout *layout, unsigned char pointer[BANG_MAX_ARGS]) {
  void *lib=dlopen("libstdc++.so.6",RTLD_LAZY|RTLD_LOCAL);
  if(!lib) return -1;
  char *(*demangle)(const char *,char *,size_t *,int *)=dlsym(lib,"__cxa_demangle");
  int st=-1; char *name=demangle ? demangle(symbol,NULL,NULL,&st) : NULL;
  if(!name || st) { free(name); dlclose(lib); return -1; }
  char *p=strchr(name,'('), *end=strrchr(name,')'); unsigned count=0;
  if(!p || !end || end[1] || strchr(name,'<')) goto bad;
  *end=0; ++p;
  while(*p) {
    if(count>=layout->count) goto bad;
    char *comma=strchr(p,','); if(comma) *comma=0;
    char t[100]; size_t used=0; unsigned stars=0;
    for(char *c=p;*c;++c) {
      if(*c=='*') { ++stars; continue; }
      if(*c==' ') continue;
      if(used+1>=sizeof(t)) goto bad;
      t[used++]=*c;
    }
    t[used]=0;
    /* const qualification is allowed only on primitive pointer pointees. */
    if(stars==1) {
      char *qual=strstr(t,"const"); if(qual) memmove(qual,qual+5,strlen(qual+5)+1);
      qual=strstr(t,"volatile"); if(qual) memmove(qual,qual+8,strlen(qual+8)+1);
    }
    unsigned size=0;
    if(!strcmp(t,"bool") || !strcmp(t,"char") || !strcmp(t,"signedchar") || !strcmp(t,"unsignedchar")) size=1;
    else if(!strcmp(t,"short") || !strcmp(t,"unsignedshort")) size=2;
    else if(!strcmp(t,"int") || !strcmp(t,"unsignedint") || !strcmp(t,"float")) size=4;
    else if(!strcmp(t,"long") || !strcmp(t,"unsignedlong") || !strcmp(t,"longlong") || !strcmp(t,"unsignedlonglong") || !strcmp(t,"double")) size=8;
    else if(!strcmp(t,"void") && stars==1) size=8;
    if(!size || stars>1) goto bad;
    if(stars) size=8;
    if(layout->sizes[count]!=size) goto bad;
    pointer[count++]=stars;
    if(!comma) break;
    p=comma+1;
  }
  free(name); dlclose(lib); return count==layout->count ? 0 : -1;
 bad:
  free(name); dlclose(lib); return -1;
}
