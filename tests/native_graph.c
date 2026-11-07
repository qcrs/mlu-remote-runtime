#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef TEST_CNRT
#include <cnrt.h>
#define F(name) cnrtTaskTopo##name
typedef cnrtTaskTopo_t Graph;
typedef cnrtTaskTopoNode_t Node;
typedef cnrtTaskTopoNodeType_t Type;
#else
#include <cn_api.h>
#define F(name) cnTaskTopo##name
typedef CNtaskTopo Graph;
typedef CNtaskTopoNode Node;
typedef CNtaskTopoNodeType Type;
#endif
#define OK(call) do { int st=(int)(call); if(st) { fprintf(stderr,"%s=%d line=%d\n",#call,st,__LINE__); exit(1); } } while(0)
#define CHECK(test) do { if(!(test)) { fprintf(stderr,"failed %s line=%d\n",#test,__LINE__); exit(2); } } while(0)
static unsigned mask(Node *got,size_t n,Node *known,size_t m) {
  unsigned bits=0;
  for(size_t i=0;i<n;i++) { size_t j=0; for(;j<m;j++) if(got[i]==known[j]) break; CHECK(j<m); bits|=1u<<j; }
  return bits;
}
int main(int argc,char **argv) {
#ifdef TEST_CNRT
  OK(cnrtSetDevice(0));
#else
  CNcontext context; OK(cnInit(0)); OK(cnCtxCreate(&context,0,0));
#endif
  int remote=getenv("MLU_TEST_SHIM")!=NULL;
  Graph g,clone; Node n[4],mapped[4],q[8],from[8],to[8]; size_t count;
  OK(F(Create)(&g,0));
  OK(F(AddDependencies)(g,NULL,NULL,0));
  OK(F(RemoveDependencies)(g,NULL,NULL,0));
  OK(F(AddEmptyNode)(&n[0],g,NULL,0));
  OK(F(AddEmptyNode)(&n[1],g,n,1));
  OK(F(AddEmptyNode)(&n[2],g,n,1));
  OK(F(AddEmptyNode)(&n[3],g,n+1,2));
  count=0; OK(F(GetNodes)(g,NULL,&count)); CHECK(count==4);
  count=7; memset(q,0xff,sizeof(q)); OK(F(GetNodes)(g,q,&count)); CHECK(count==4 && mask(q,count,n,4)==15 && !q[4] && !q[6]);
  count=1; OK(F(GetNodes)(g,q,&count)); printf("nodes_truncated=%zu\n",count); CHECK(mask(q,1,n,4)!=0);
  count=0; OK(F(GetEdges)(g,NULL,NULL,&count)); CHECK(count==4);
  count=7; OK(F(GetEdges)(g,from,to,&count)); CHECK(count==4 && !from[4] && !to[6]);
  unsigned edges=0; for(size_t i=0;i<count;i++) edges |= mask(from+i,1,n,4)<<(4*(__builtin_ctz(mask(to+i,1,n,4)))); CHECK(edges==0x6110);
  count=1; OK(F(GetEdges)(g,from,to,&count)); printf("edges_truncated=%zu\n",count);
  count=7; OK(F(GetRootNodes)(g,q,&count)); CHECK(count==1 && q[0]==n[0] && !q[1] && !q[6]);
  count=7; OK(F(NodeGetDependencies)(n[3],q,&count)); CHECK(count==0 && !q[0] && !q[6]);
  count=1; OK(F(NodeGetDependencies)(n[3],q,&count)); printf("dependencies_truncated=%zu\n",count);
  count=0; OK(F(NodeGetDependentNodes)(n[0],NULL,&count)); CHECK(count==0);
  count=7; OK(F(NodeGetDependentNodes)(n[0],q,&count)); CHECK(count==0 && !q[0] && !q[6]);
  /* This SDK names the directions opposite to its observed DAG edges:
   * Dependencies returns successors; DependentNodes returns predecessors. */
  count=7; OK(F(NodeGetDependencies)(n[0],q,&count)); CHECK(count==2 && mask(q,count,n,4)==6 && !q[2] && !q[6]);
  count=7; OK(F(NodeGetDependentNodes)(n[3],q,&count)); CHECK(count==2 && mask(q,count,n,4)==6 && !q[2] && !q[6]);
  Type type; OK(F(NodeGetType)(n[0],&type)); printf("empty_type=%d\n",(int)type);
  OK(F(Clone)(&clone,g));
  for(size_t i=0;i<4;i++) { OK(F(NodeFindInClone)(&mapped[i],n[i],clone)); CHECK(mapped[i]!=n[i]); }
  count=7; OK(F(GetNodes)(clone,q,&count)); CHECK(count==4 && mask(q,count,mapped,4)==15);
  OK(F(RemoveDependencies)(g,n,n+1,1));
  count=7; OK(F(GetRootNodes)(g,q,&count)); CHECK(count==2 && mask(q,count,n,4)==3);
  OK(F(AddDependencies)(g,n+1,n+2,1));
  count=0; OK(F(NodeGetDependencies)(n[2],NULL,&count)); CHECK(count==1);
  count=7; OK(F(NodeGetDependencies)(n[2],q,&count)); CHECK(count==1 && q[0]==n[3] && !q[1] && !q[6]);
  count=7; OK(F(NodeGetDependentNodes)(n[1],q,&count)); CHECK(count==0 && !q[0] && !q[6]);
  OK(F(DestroyNode)(n[1]));
  count=7; OK(F(GetNodes)(g,q,&count)); CHECK(count==3 && mask(q,count,n,4)==13 && !q[3]);
  count=0; OK(F(GetEdges)(g,NULL,NULL,&count)); CHECK(count==2);
  count=0; OK(F(GetEdges)(clone,NULL,NULL,&count)); CHECK(count==4);
  if(remote) CHECK(F(NodeGetType)(n[1],&type)!=0);
  Graph other; Node foreign; OK(F(Create)(&other,0)); OK(F(AddEmptyNode)(&foreign,other,NULL,0));
  CHECK((int)F(AddDependencies)(g,&foreign,n,1)==100006);
  OK(F(Destroy)(other)); OK(F(Destroy)(g));
  count=7; OK(F(GetNodes)(clone,q,&count)); CHECK(count==4 && mask(q,count,mapped,4)==15);
  OK(F(Destroy)(clone));
  if(remote) CHECK(F(GetNodes)(g,NULL,&count)!=0);
  if(argc>1 && !strcmp(argv[1],"abrupt")) {
    OK(F(Create)(&g,0)); OK(F(AddEmptyNode)(&n[0],g,NULL,0));
    puts("PASS graph abrupt"); fflush(stdout); _exit(0);
  }
  if(argc>1 && !strcmp(argv[1],"reset")) {
    OK(F(Create)(&g,0)); OK(F(AddEmptyNode)(&n[0],g,NULL,0));
    if(!remote) OK(F(Destroy)(g));
#ifdef TEST_CNRT
    OK(cnrtDeviceReset()); OK(cnrtSetDevice(0));
#else
    OK(cnCtxDestroy(context)); OK(cnCtxCreate(&context,0,0));
#endif
    if(remote) { CHECK(F(NodeGetType)(n[0],&type)!=0); CHECK(F(GetNodes)(g,NULL,&count)!=0); }
    OK(F(Create)(&g,0)); OK(F(Destroy)(g));
  }
#ifndef TEST_CNRT
  OK(cnCtxDestroy(context));
#endif
  puts("PASS graph G1"); return 0;
}
