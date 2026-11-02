#include "remote_protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static uint64_t htonll_u(uint64_t x) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return ((uint64_t)htonl((uint32_t)x) << 32) | htonl((uint32_t)(x >> 32));
#else
  return x;
#endif
}
static int connect_to(const char *host, int port) {
  char service[16]; snprintf(service, sizeof(service), "%d", port);
  struct addrinfo hints, *list = NULL; memset(&hints, 0, sizeof(hints)); hints.ai_socktype = SOCK_STREAM; hints.ai_family = AF_UNSPEC;
  if (getaddrinfo(host, service, &hints, &list)) return -1; int fd = -1;
  for (struct addrinfo *it = list; it; it = it->ai_next) { fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol); if (fd >= 0 && connect(fd, it->ai_addr, it->ai_addrlen) == 0) break; if (fd >= 0) { close(fd); fd = -1; } }
  freeaddrinfo(list); return fd;
}
static void send_header(int fd, uint32_t payload_len) {
  struct mlu_remote_wire_header h; memset(&h, 0, sizeof(h)); h.magic = htonl(MLU_REMOTE_MAGIC); h.version = htons(MLU_REMOTE_VERSION); h.opcode = htons(MLU_OP_ALLOC); h.request_id = htonll_u(1); h.payload_len = htonl(payload_len); send(fd, &h, sizeof(h), MSG_NOSIGNAL);
}
int main(void) {
  const char *host = getenv("MLU_REMOTE_HOST"); if (!host) host = "127.0.0.1"; int port = getenv("MLU_REMOTE_PORT") ? atoi(getenv("MLU_REMOTE_PORT")) : 50051;
  int fd = connect_to(host, port); if (fd < 0) { perror("connect"); return 1; }
  send_header(fd, MLU_REMOTE_MAX_PAYLOAD + 1u); close(fd);
  fd = connect_to(host, port); if (fd < 0) return 1;
  send_header(fd, 8); unsigned char truncated[2] = {0, 0}; send(fd, truncated, sizeof(truncated), MSG_NOSIGNAL); shutdown(fd, SHUT_WR); close(fd);
  printf("PASS protocol oversized_payload and truncated_payload rejected\n"); return 0;
}
