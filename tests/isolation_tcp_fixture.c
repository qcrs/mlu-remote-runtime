#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int parse_port(const char *text) {
  char *end = NULL;
  long value = strtol(text, &end, 10);
  if (!end || *end || value < 1 || value > 65535) return -1;
  return (int)value;
}

static int listen_file(int port, const char *path) {
  int listener = socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0) return 1;
  int reuse = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons((uint16_t)port),
                                .sin_addr.s_addr = htonl(INADDR_ANY)};
  if (bind(listener, (struct sockaddr *)&address, sizeof(address)) < 0 || listen(listener, 1) < 0) {
    perror("listen"); close(listener); return 1;
  }
  for (;;) {
    int connection = accept(listener, NULL, NULL);
    if (connection < 0) { if (errno == EINTR) continue; perror("accept"); close(listener); return 1; }
    char buffer[65536];
    ssize_t received = read(connection, buffer, sizeof(buffer));
    if (received == 0) { close(connection); continue; }
    if (received < 0) {
      if (errno == EINTR) { close(connection); continue; }
      perror("read socket"); return 1;
    }
    int output = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (output < 0) { perror("open output"); close(connection); return 1; }
    for (;;) {
      for (ssize_t offset = 0; offset < received;) {
        ssize_t written = write(output, buffer + offset, (size_t)(received - offset));
        if (written < 0) { if (errno == EINTR) continue; perror("write file"); return 1; }
        offset += written;
      }
      do { received = read(connection, buffer, sizeof(buffer)); } while (received < 0 && errno == EINTR);
      if (received < 0) { perror("read socket"); return 1; }
      if (received == 0) break;
    }
    close(output); close(connection); close(listener);
    return 0;
  }
}

static int send_file(const char *host, int port, const char *path) {
  int input = open(path, O_RDONLY);
  if (input < 0) { perror("open client input"); return 1; }
  int connection = socket(AF_INET, SOCK_STREAM, 0);
  if (connection < 0) { perror("socket"); close(input); return 1; }
  struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons((uint16_t)port)};
  if (inet_pton(AF_INET, host, &address.sin_addr) != 1 ||
      connect(connection, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("connect"); close(connection); close(input); return 1;
  }
  char buffer[65536];
  for (;;) {
    ssize_t received = read(input, buffer, sizeof(buffer));
    if (received == 0) break;
    if (received < 0) { if (errno == EINTR) continue; perror("read input"); return 1; }
    for (ssize_t offset = 0; offset < received;) {
      ssize_t sent = send(connection, buffer + offset, (size_t)(received - offset), 0);
      if (sent < 0) { if (errno == EINTR) continue; perror("send"); return 1; }
      offset += sent;
    }
  }
  shutdown(connection, SHUT_WR);
  close(connection); close(input);
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 4 && strcmp(argv[1], "listen") == 0) {
    int port = parse_port(argv[2]);
    return port < 0 ? 2 : listen_file(port, argv[3]);
  }
  if (argc == 5 && strcmp(argv[1], "send") == 0) {
    int port = parse_port(argv[3]);
    return port < 0 ? 2 : send_file(argv[2], port, argv[4]);
  }
  fprintf(stderr, "usage: %s listen PORT OUTPUT | send HOST PORT INPUT\n", argv[0]);
  return 2;
}
