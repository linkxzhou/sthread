/*
 * Copyright (C) zhoulv2000@163.com
 *
 * 把一份阻塞 POSIX 客户端放进协程。慢的 read 在 sys_read → st_read 里 Yield。
 */

#include "app/st_frame.h"
#include "stlib/st_log.h"
#include "stlib/st_util.h"
#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

using namespace stlib;

extern "C" int blocking_exchange(const char *ip, int port, const char *payload,
                                 int timeout_ms);

struct BenchArg {
  int count;
  int timeout;
  int port;
  char ip[64];
  char payload[1024];
};

static volatile int g_pending = 0;
static volatile int g_ok = 0;
static volatile int g_fail = 0;

static void usage(const char *argv0) {
  fprintf(stderr,
          "usage: %s [-c CONC] [-n TOTAL] [-t MS] [-s PAYLOAD] host port\n"
          "  default -c 4 -n 4 -t 3000 payload \"hook\"\n"
          "exit: 0 all ok; 1 failure; 2 bad args\n",
          argv0);
}

static void worker(void *arg) {
  BenchArg *a = (BenchArg *)arg;
  int i;
  for (i = 0; i < a->count; i++) {
    /* 协程里不要把回显打到 stdout，避免和 SUMMARY 缠在一起。 */
    int rc = blocking_exchange(a->ip, a->port, a->payload, a->timeout);
    if (rc == 0) {
      __sync_fetch_and_add(&g_ok, 1);
    } else {
      __sync_fetch_and_add(&g_fail, 1);
    }
  }
  __sync_fetch_and_sub(&g_pending, 1);
}

int main(int argc, char *argv[]) {
  int ncoro = 4;
  int total = -1;
  int timeout = 3000;
  const char *payload = "hook";
  const char *host = NULL;
  int port = 0;
  int c;
  struct in_addr ina;
  BenchArg *args;
  int base;
  int rem;
  int i;
  uint64_t start;
  uint64_t elapsed;

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
  if (inet_pton(AF_INET, host, &ina) != 1) {
    fprintf(stderr, "host must be an IPv4 literal\n");
    return 2;
  }
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
  if (total <= 0 || ncoro > total) {
    if (total <= 0) {
      usage(argv[0]);
      return 2;
    }
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
    args[i].count = base + (i < rem ? 1 : 0);
    args[i].timeout = timeout;
    args[i].port = port;
    snprintf(args[i].ip, sizeof(args[i].ip), "%s", host);
    snprintf(args[i].payload, sizeof(args[i].payload), "%s", payload);
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
  printf("SUMMARY ok=%d fail=%d elapsed_ms=%llu\n", g_ok, g_fail,
         (unsigned long long)elapsed);
  fflush(stdout);
  delete[] args;
  if (g_pending > 0 || g_fail > 0 || g_ok != total) {
    return 1;
  }
  return 0;
}
