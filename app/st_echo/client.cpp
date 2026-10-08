/*
 * Copyright (C) zhoulv2000@163.com
 *
 * echo 客户端。每个请求一次 tcp_sendrecv，短连接（keeplive=false）。
 * 接收超时是 tcp_sendrecv 的 -3，不是 st_recv 的 -1。
 */

#include "app/st_c.h"
#include "app/st_frame.h"
#include "src/st_sys.h"
#include "stlib/st_log.h"
#include "stlib/st_util.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

using namespace stlib;

struct BenchArg {
  int index;
  int count;
  int timeout;
  struct sockaddr_in dst;
  char payload[1024];
  int payload_len;
};

static volatile int g_pending = 0;
static volatile int g_ok = 0;
static volatile int g_fail = 0;
static char g_one_line[1200];
static int g_one_len = 0;

static int32_t check_line(void *buf, int len) {
  char *p = (char *)buf;
  int i;
  if (buf == NULL || len < 0) {
    return -1;
  }
  for (i = 0; i < len; i++) {
    if (p[i] == '\n') {
      return i + 1;
    }
  }
  return 0;
}

static void usage(const char *argv0) {
  fprintf(stderr,
          "usage: %s [-c CONC] [-n TOTAL] [-t MS] [-s PAYLOAD] host port\n"
          "  default -c 1, -n = -c, -t 3000, payload \"ping\"\n"
          "exit: 0 all ok; 1 failure; 2 bad args\n",
          argv0);
}

static void worker(void *arg) {
  BenchArg *a = (BenchArg *)arg;
  char sendbuf[1200];
  char recvbuf[2048];
  int i;
  int nsend;

  nsend = snprintf(sendbuf, sizeof(sendbuf), "%s\n", a->payload);
  if (nsend < 0 || nsend >= (int)sizeof(sendbuf)) {
    __sync_fetch_and_add(&g_fail, a->count);
    __sync_fetch_and_sub(&g_pending, 1);
    return;
  }
  for (i = 0; i < a->count; i++) {
    int bufsize = (int)sizeof(recvbuf);
    int rc = tcp_sendrecv(&a->dst, sendbuf, nsend, recvbuf, bufsize, a->timeout,
                          check_line, false);
    if (rc == 0 && bufsize == nsend &&
        memcmp(recvbuf, sendbuf, (size_t)nsend) == 0) {
      __sync_fetch_and_add(&g_ok, 1);
      if (a->index == 0 && a->count == 1 && g_one_len == 0) {
        memcpy(g_one_line, recvbuf, (size_t)bufsize);
        g_one_line[bufsize] = '\0';
        g_one_len = bufsize;
      }
    } else {
      __sync_fetch_and_add(&g_fail, 1);
    }
  }
  __sync_fetch_and_sub(&g_pending, 1);
}

int main(int argc, char *argv[]) {
  int ncoro = 1;
  int total = -1;
  int timeout = 3000;
  const char *payload = "ping";
  const char *host = NULL;
  int port = 0;
  int c;
  BenchArg *args;
  int base;
  int rem;
  int i;
  uint64_t start;
  uint64_t elapsed;
  double qps;

  signal(SIGPIPE, SIG_IGN);
  LOG_LEVEL(LLOG_CRIT);

  while ((c = getopt(argc, argv, "c:n:t:s:h")) != -1) {
    switch (c) {
    case 'c':
      ncoro = atoi(optarg);
      break;
    case 'n':
      total = atoi(optarg);
      break;
    case 't':
      timeout = atoi(optarg);
      break;
    case 's':
      payload = optarg;
      break;
    case 'h':
      usage(argv[0]);
      return 0;
    default:
      usage(argv[0]);
      return 2;
    }
  }
  if (optind + 2 != argc) {
    usage(argv[0]);
    return 2;
  }
  host = argv[optind];
  port = atoi(argv[optind + 1]);
  if (ncoro <= 0 || port <= 0 || port > 65535 || timeout <= 0) {
    usage(argv[0]);
    return 2;
  }
  if (strlen(payload) > 1000) {
    fprintf(stderr, "payload too long\n");
    return 2;
  }
  if (total < 0) {
    total = ncoro;
  }
  if (total <= 0) {
    usage(argv[0]);
    return 2;
  }
  if (ncoro > total) {
    ncoro = total;
  }

  if (!st_init_frame()) {
    fprintf(stderr, "st_init_frame failed\n");
    return 1;
  }
  st_set_hook_flag();

  base = total / ncoro;
  rem = total % ncoro;
  args = new BenchArg[ncoro];
  for (i = 0; i < ncoro; i++) {
    memset(&args[i], 0, sizeof(args[i]));
    args[i].index = i;
    args[i].count = base + (i < rem ? 1 : 0);
    args[i].timeout = timeout;
    args[i].dst.sin_family = AF_INET;
    args[i].dst.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &args[i].dst.sin_addr) != 1) {
      fprintf(stderr, "host must be an IPv4 literal\n");
      delete[] args;
      return 2;
    }
    snprintf(args[i].payload, sizeof(args[i].payload), "%s", payload);
    args[i].payload_len = (int)strlen(args[i].payload);
  }

  g_pending = ncoro;
  g_ok = 0;
  g_fail = 0;
  start = Util::TimeMs();
  if (ncoro == 1) {
    worker(&args[0]);
  } else {
    for (i = 0; i < ncoro; i++) {
      if (Frame::CreateThread(worker, &args[i]) == NULL) {
        __sync_fetch_and_sub(&g_pending, 1);
        __sync_fetch_and_add(&g_fail, args[i].count);
      }
    }
    {
      int64_t deadline =
          (int64_t)start + (int64_t)timeout * (int64_t)(base + 1) + 5000;
      while (g_pending > 0 && (int64_t)Util::TimeMs() < deadline) {
        st_sleep(10);
      }
    }
  }
  elapsed = Util::TimeMs() - start;
  if (elapsed == 0) {
    elapsed = 1;
  }
  qps = (double)g_ok * 1000.0 / (double)elapsed;
  if (ncoro == 1 && total == 1 && g_one_len > 0) {
    fwrite(g_one_line, 1, (size_t)g_one_len, stdout);
  }
  printf("SUMMARY ok=%d fail=%d qps=%.2f elapsed_ms=%llu\n", g_ok, g_fail, qps,
         (unsigned long long)elapsed);
  fflush(stdout);
  delete[] args;
  if (g_pending > 0 || g_fail > 0 || g_ok != total) {
    return 1;
  }
  return 0;
}
