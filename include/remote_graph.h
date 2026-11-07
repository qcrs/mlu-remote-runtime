#ifndef MLU_REMOTE_GRAPH_H
#define MLU_REMOTE_GRAPH_H
/* G1 uses one identity namespace for both public libraries. Values are opaque
 * session-owned IDs, never native pointers. Limits include all graphs/session. */
#define MLU_GRAPH_MAX_NODES 4096u
#define MLU_GRAPH_MAX_ARRAY 65536u
enum mlu_graph_action { MLU_GRAPH_CREATE=1, MLU_GRAPH_DESTROY, MLU_GRAPH_CLONE,
  MLU_GRAPH_FIND_CLONE, MLU_GRAPH_NODE_TYPE, MLU_GRAPH_DESTROY_NODE };
enum mlu_graph_edit { MLU_GRAPH_ADD_EMPTY=1, MLU_GRAPH_ADD_EDGES, MLU_GRAPH_REMOVE_EDGES };
enum mlu_graph_query { MLU_GRAPH_NODES=1, MLU_GRAPH_EDGES, MLU_GRAPH_ROOTS,
  MLU_GRAPH_DEPENDENCIES, MLU_GRAPH_DEPENDENTS };
#endif
