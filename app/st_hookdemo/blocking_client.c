/*
 * Copyright (C) zhoulv2000@163.com
 *
 * 只调用 POSIX socket / connect / setsockopt / write / read / close。
 * 协程目标由 makefile -include st_posix_alias.h，把这些名字换成 sys_*。
 * 本文件文本里不出现 st_ / St。
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

int blocking_exchange(const char *ip, int port, const char *payload,
                      int timeout_ms) {
  int fd;
  struct sockaddr_in addr;
  struct timeval tv;
  char line[1200];
  int nsend;
  int off;
  int got;
  int n;
  char buf[256];

  if (ip == NULL || payload == NULL || port <= 0 || port > 65535 ||
      timeout_ms <= 0) {
    return 1;
  }
  if (strlen(payload) > 1000) {
    return 1;
  }
  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return 1;
  }
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
    close(fd);
    return 1;
  }
  if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0) {
    close(fd);
    return 1;
  }
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((unsigned short)port);
  if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
    close(fd);
    return 1;
  }
  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    close(fd);
    return 1;
  }
  nsend = snprintf(line, sizeof(line), "%s\n", payload);
  if (nsend < 0 || nsend >= (int)sizeof(line)) {
    close(fd);
    return 1;
  }
  off = 0;
  while (off < nsend) {
    n = (int)write(fd, line + off, (size_t)(nsend - off));
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      close(fd);
      return 1;
    }
    if (n == 0) {
      close(fd);
      return 1;
    }
    off += n;
  }
  got = 0;
  while (got < (int)sizeof(buf) - 1) {
    n = (int)read(fd, buf + got, (size_t)(sizeof(buf) - 1 - got));
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      close(fd);
      return 1;
    }
    if (n == 0) {
      break;
    }
    got += n;
    buf[got] = '\0';
    if (memchr(buf, '\n', (size_t)got) != NULL) {
      break;
    }
  }
  close(fd);
  if (got == nsend && memcmp(buf, line, (size_t)nsend) == 0) {
    fwrite(buf, 1, (size_t)got, stdout);
    return 0;
  }
  return 1;
}

#ifdef __cplusplus
}
#endif

#ifndef ST_BLOCKING_NO_MAIN
int main(int argc, char **argv) {
  if (argc != 4) {
    fprintf(stderr, "usage: %s host port payload\n", argv[0]);
    return 2;
  }
  if (blocking_exchange(argv[1], atoi(argv[2]), argv[3], 3000) != 0) {
    return 1;
  }
  return 0;
}
#endif
