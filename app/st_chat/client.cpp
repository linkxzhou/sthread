/*
 * Copyright (C) zhoulv2000@163.com
 *
 * 聊天室客户端。先发 nick，再把每条 message 发成一行，然后一直读到截止时间。
 * 不能在第一行（* joined）就退出，否则会错过随后的广播。
 */

#include "app/st_c.h"
#include "src/st_sys.h"
#include "stlib/st_log.h"
#include "stlib/st_netaddr.h"
#include "stlib/st_util.h"
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

using namespace sthread;
using namespace stlib;

static int send_all(int fd, const char *buf, int len, int timeout_ms) {
  int off = 0;
  uint64_t start = Util::TimeMs();
  while (off < len) {
    int used = (int)(Util::TimeMs() - start);
    int left = timeout_ms - used;
    ssize_t n;
    if (left <= 0) {
      errno = ETIME;
      return -1;
    }
    n = st_send(fd, buf + off, (size_t)(len - off), 0, left);
    if (n <= 0) {
      return -1;
    }
    off += (int)n;
  }
  return 0;
}

static int read_until(int fd, int timeout_ms) {
  char acc[1024];
  int acc_n = 0;
  int lines = 0;
  uint64_t deadline = Util::TimeMs() + (uint64_t)timeout_ms;
  while ((int64_t)Util::TimeMs() < (int64_t)deadline) {
    char buf[256];
    int left = (int)(deadline - Util::TimeMs());
    int n;
    int i;
    if (left <= 0) {
      break;
    }
    n = st_recv(fd, buf, (int)sizeof(buf), 0, left);
    if (n == 0) {
      break;
    }
    if (n < 0) {
      if (errno == ETIME) {
        break;
      }
      return lines > 0 ? lines : -1;
    }
    for (i = 0; i < n; i++) {
      if (buf[i] == '\n') {
        acc[acc_n] = '\0';
        fwrite(acc, 1, (size_t)acc_n, stdout);
        fputc('\n', stdout);
        fflush(stdout);
        acc_n = 0;
        lines++;
      } else if (acc_n < (int)sizeof(acc) - 1) {
        acc[acc_n++] = buf[i];
      }
    }
  }
  return lines;
}

static void usage(const char *argv0) {
  fprintf(stderr,
          "usage: %s [-t MS] [-n NICK] host port message...\n"
          "  default nick is pid, timeout 3000\n"
          "exit: 0 got at least one line; 1 timeout or connect failure; "
          "2 bad args\n",
          argv0);
}

int main(int argc, char *argv[]) {
  int timeout = 3000;
  char nick[32];
  const char *host;
  int port;
  int c;
  struct in_addr ina;
  StExecClientConnection *conn;
  StNetAddr addr;
  int fd;
  int i;
  char line[600];

  signal(SIGPIPE, SIG_IGN);
  LOG_LEVEL(LLOG_CRIT);
  snprintf(nick, sizeof(nick), "%d", (int)getpid());

  while ((c = getopt(argc, argv, "t:n:h")) != -1) {
    switch (c) {
    case 't':
      timeout = atoi(optarg);
      break;
    case 'n':
      snprintf(nick, sizeof(nick), "%s", optarg);
      break;
    case 'h':
      usage(argv[0]);
      return 0;
    default:
      usage(argv[0]);
      return 2;
    }
  }
  if (optind + 2 > argc) {
    usage(argv[0]);
    return 2;
  }
  host = argv[optind];
  port = atoi(argv[optind + 1]);
  if (timeout <= 0 || port <= 0 || port > 65535 || nick[0] == '\0' ||
      strlen(nick) > 31) {
    usage(argv[0]);
    return 2;
  }
  if (inet_pton(AF_INET, host, &ina) != 1) {
    fprintf(stderr, "host must be an IPv4 literal\n");
    return 2;
  }
  (void)ina;

  if (!st_init_frame()) {
    fprintf(stderr, "st_init_frame failed\n");
    return 1;
  }
  st_set_hook_flag();

  conn = Instance<StConnectionManager<StExecClientConnection> >()->AllocPtr(
      eTCP_CONN);
  if (conn == NULL) {
    return 1;
  }
  conn->SetTimeout(timeout);
  addr.SetAddr(host, (uint16_t)port);
  fd = conn->Create(addr);
  if (fd < 0) {
    Instance<StConnectionManager<StExecClientConnection> >()->FreePtr(conn);
    return 1;
  }
  snprintf(line, sizeof(line), "%s\n", nick);
  if (send_all(fd, line, (int)strlen(line), timeout) != 0) {
    Instance<StConnectionManager<StExecClientConnection> >()->FreePtr(conn);
    return 1;
  }
  for (i = optind + 2; i < argc; i++) {
    if (strlen(argv[i]) > 500) {
      Instance<StConnectionManager<StExecClientConnection> >()->FreePtr(conn);
      return 2;
    }
    snprintf(line, sizeof(line), "%s\n", argv[i]);
    if (send_all(fd, line, (int)strlen(line), timeout) != 0) {
      Instance<StConnectionManager<StExecClientConnection> >()->FreePtr(conn);
      return 1;
    }
  }
  i = read_until(fd, timeout);
  Instance<StConnectionManager<StExecClientConnection> >()->FreePtr(conn);
  if (i <= 0) {
    return 1;
  }
  return 0;
}
