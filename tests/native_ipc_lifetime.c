/* Remote lifetime policy checks; ordinary IPC source-free behavior is not a Native guarantee. */
#define main contract_probe_main
#include "../probes/probe_graph_ipc.c"
#undef main
static void ready(const char *tag) {printf("{\"ready\":\"%s\"}\n",tag);fflush(stdout);}
static void await(void) {char line[32];expect("coordinator_signal",fgets(line,sizeof(line),stdin)!=NULL);}
int main(int argc,char **argv) {
 if(argc<3)return 1;setvbuf(stdout,NULL,_IOLBF,0);init();
 int host=!strcmp(argv[2],"host");
 if(!strcmp(argv[1],"export")) {
  Addr a=0;void *ptr=NULL;MemHandle handle;
  if(host){CHECK(host_alloc(&ptr,N));a=(Addr)(uintptr_t)ptr;memset(ptr,17,N);}else {CHECK(alloc(&a,N));CHECK(fill(a,17,N));}
  CHECK(export_mem(&handle,a));printf("{\"ready\":\"export\",\"handle\":\"%" PRIxPTR "\"}\n",(uintptr_t)handle);await();
  if(!strcmp(argv[3],"eof"))_exit(0);
  if(!strcmp(argv[3],"reset")){finish();ready("reset");return 0;}
  if(host)CHECK(host_free(ptr));else CHECK(release(a));ready("freed");await();finish();return 0;
 }
 MemHandle token=(MemHandle)(uintptr_t)strtoull(argv[3],NULL,16);Addr a=0;
 if(!strcmp(argv[1],"reopen")) {int st=import_mem(&a,token);observe("reopen_retired_export",st);expect("retired_export_rejects_new_import",st!=0);finish();return 0;}
 CHECK(import_mem(&a,token));unsigned char data[N];
 if(host)memcpy(data,(void*)(uintptr_t)a,N);else CHECK(readback(data,a,N));expect("initial_backing_bytes",bytes_are(data,N,17));
 if(!strcmp(argv[1],"quota")) {
  Addr extra=0;int st=alloc(&extra,1);observe("allocation_while_exporter_charged",st);expect("backing_charged_once_at_total_limit",st!=0);
  ready("import-held");await();CHECK(readback(data,a,N));expect("detached_backing_still_live",bytes_are(data,64,17));
  st=alloc(&extra,1);observe("allocation_while_detached_backing_charged",st);expect("retired_source_keeps_single_charge",st!=0);
  CHECK(close_mem(a));CHECK(alloc(&extra,N));CHECK(release(extra));expect("final_import_releases_entire_charge",1);finish();return 0;
 }
 if(!strcmp(argv[1],"graph")) {
  Addr dst;CHECK(alloc(&dst,N));Topo graph;Node node;Entity entity;Queue queue;CHECK(queue_create(&queue));CHECK(API(TaskTopoCreate)(&graph,0));
  CopyParams copy=copy_params(dst,a);CHECK(API(TaskTopoAddMemcpyNode)(&node,graph,NULL,0,&copy));CHECK(API(TaskTopoInstantiate)(&entity,graph,NULL,NULL,0));CHECK(API(TaskTopoDestroy)(graph));CHECK(close_mem(a));ready("graph-held");await();
  CHECK(API(TaskTopoEntityInvoke)(entity,queue));CHECK(queue_sync(queue));CHECK(readback(data,dst,N));expect("graph_retains_closed_import_backing",bytes_are(data,N,17));
  CHECK(API(TaskTopoEntityDestroy)(entity));CHECK(queue_destroy(queue));CHECK(release(dst));finish();return 0;
 }
 ready("import-held");await();
 if(host)memcpy(data,(void*)(uintptr_t)a,N);else CHECK(readback(data,a,N));expect("backing_survives_exporter_retirement",bytes_are(data,64,17) && bytes_are(data+N-64,64,17));
 if(host)memset((char*)(uintptr_t)a+64,85,N-128);else CHECK(fill((Addr)((uintptr_t)a+64),85,N-128));
 if(argc>4 && !strcmp(argv[4],"eof"))_exit(0);
 if(argc>4 && !strcmp(argv[4],"reset")){finish();return 0;}
 CHECK(close_mem(a));finish();return 0;
}
