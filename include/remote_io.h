#ifndef MLU_REMOTE_IO_H
#define MLU_REMOTE_IO_H

/* SDK-independent, absolute CLOCK_MONOTONIC socket deadlines. A short read or
 * EINTR never renews the budget; MSG_DONTWAIT also bounds a ready fd's I/O. */
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>

static inline int mlu_timeout_env(const char *name, uint32_t fallback, uint32_t *out) {
  const char *p = getenv(name);
  if (!p) { *out = fallback; return 0; }
  uint32_t value = 0;
  if (!*p) { errno = EINVAL; return -1; }
  for (; *p; ++p) {
    if (*p < '0' || *p > '9' || value > 360000u) { errno = EINVAL; return -1; }
    value = value * 10u + (uint32_t)(*p - '0');
    if (value > 3600000u) { errno = EINVAL; return -1; }
  }
  if (!value) { errno = EINVAL; return -1; }
  *out = value; return 0;
}

static inline int64_t mlu_monotonic_ns(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts)) return -1;
  return (int64_t)ts.tv_sec * 1000000000ll + ts.tv_nsec;
}
static inline int64_t mlu_deadline_after(uint32_t milliseconds) {
  int64_t now = mlu_monotonic_ns();
  return now < 0 ? -1 : now + (int64_t)milliseconds * 1000000ll;
}
static inline int mlu_deadline_remaining(int64_t deadline) {
  int64_t now = mlu_monotonic_ns();
  if (now < 0 || deadline < 0) { errno = EIO; return -1; }
  if (now >= deadline) { errno = ETIMEDOUT; return -1; }
  return (int)((deadline - now + 999999ll) / 1000000ll);
}
static inline int mlu_wait_socket(int fd, short events, int64_t deadline) {
  for (;;) {
    int remaining = mlu_deadline_remaining(deadline);
    if (remaining < 0) return -1;
    struct pollfd p = {.fd = fd, .events = events};
    int r = poll(&p, 1, remaining);
    if (r > 0) {
      if (p.revents & POLLNVAL) { errno = EBADF; return -1; }
      return 0; /* HUP/ERR are resolved by recv/send or SO_ERROR. */
    }
    if (r < 0 && errno != EINTR) return -1;
  }
}
/* 1: complete; 0: peer EOF (including partial frame); -1: error/timeout. */
static inline int mlu_io_full(int fd, void *buffer, size_t bytes, int writing,
                              int64_t deadline) {
  unsigned char *p = buffer;
  while (bytes) {
    if (mlu_deadline_remaining(deadline) < 0) return -1;
    ssize_t n = writing ? send(fd, p, bytes, MSG_DONTWAIT | MSG_NOSIGNAL)
                        : recv(fd, p, bytes, MSG_DONTWAIT);
    if (n > 0) { p += n; bytes -= (size_t)n; continue; }
    if (!n) { errno = writing ? EPIPE : ECONNRESET; return writing ? -1 : 0; }
    if (errno == EINTR) continue;
    if (errno != EAGAIN && errno != EWOULDBLOCK) return -1;
    if (mlu_wait_socket(fd, writing ? POLLOUT : POLLIN, deadline)) return -1;
  }
  return 1;
}
#endif
