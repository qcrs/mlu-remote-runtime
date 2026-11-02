/* Operator RPC is kept in runtime_context.c so it can share the serialized
 * transport lock and framing helpers. This TU is the stable build seam for a
 * future split without changing the public API. */
#include "remote_operator.h"
