/* Session cleanup is implemented by mlu_backend_session_cleanup. The server
 * owns one backend session per accepted socket and never exposes native IDs. */
#include "mlu_backend.h"
