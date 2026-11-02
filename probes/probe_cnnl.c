#include <stdio.h>
#include <dlfcn.h>

int main(void) { void *h=dlopen("libcnnl.so",RTLD_LAZY); printf("libcnnl=%s\n",h?"present":"absent"); if(h)dlclose(h); return h ? 0 : 77; }
