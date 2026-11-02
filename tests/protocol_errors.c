#include "remote_protocol.h"
#include "remote_io.h"

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
static int send_header(int fd, uint32_t payload_len) {
  struct mlu_remote_wire_header h; memset(&h, 0, sizeof(h)); h.magic = htonl(MLU_REMOTE_MAGIC); h.version = htons(MLU_REMOTE_VERSION); h.opcode = htons(MLU_OP_HELLO); h.request_id = htonll_u(1); h.payload_len = htonl(payload_len);
  return mlu_io_full(fd, &h, sizeof(h), 1, mlu_deadline_after(2000)) == 1 ? 0 : -1;
}
int main(void) {
  const char *host = getenv("MLU_REMOTE_HOST"); if (!host) host = "127.0.0.1"; int port = getenv("MLU_REMOTE_PORT") ? atoi(getenv("MLU_REMOTE_PORT")) : 50051;
  int fd = connect_to(host, port); if (fd < 0) { perror("connect"); return 1; }
  if (send_header(fd, MLU_REMOTE_MAX_PAYLOAD + 1u)) return 1;
  struct mlu_remote_wire_header response;
  int64_t deadline = mlu_deadline_after(2000);
  if (mlu_io_full(fd, &response, sizeof(response), 0, deadline) != 1 ||
      ntohl(response.magic) != MLU_REMOTE_MAGIC || ntohs(response.version) != MLU_REMOTE_VERSION ||
      ntohs(response.opcode) != MLU_OP_HELLO || htonll_u(response.request_id) != 1 ||
      ntohl(response.status) != MLU_REMOTE_BAD_LENGTH) return 1;
  uint32_t n = ntohl(response.payload_len); unsigned char body[2048], byte;
  if (n < 8 || n > sizeof(body) || mlu_io_full(fd, body, n, 0, deadline) != 1 ||
      mlu_io_full(fd, &byte, 1, 0, deadline) != 0) return 1;
  close(fd);
  fd = connect_to(host, port); if (fd < 0) return 1;
  if (send_header(fd, 12)) return 1;
  unsigned char truncated[2] = {0, 0};
  if (mlu_io_full(fd, truncated, sizeof(truncated), 1, mlu_deadline_after(2000)) != 1) return 1;
  shutdown(fd, SHUT_WR);
  if (mlu_io_full(fd, &byte, 1, 0, mlu_deadline_after(2000)) != 0) return 1;
  close(fd);
  printf("PASS protocol oversized HELLO: BAD_LENGTH plus EOF; truncated HELLO: EOF without success\n"); return 0;
}
