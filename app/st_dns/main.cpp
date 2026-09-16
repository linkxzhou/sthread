/*
 * Copyright (C) zhoulv2000@163.com
 *
 * DNS 客户端：单次查询或协程压测后主动退出（不再 Frame::Loop(true) 挂死）。
 *
 *   ./main -s 127.0.0.1 -p 5353 www.1.bench.local
 *   ./main -s 127.0.0.1 -p 5353 -c 100 -n 1000
 */

#include "dns.h"
#include "app/st_frame.h"
#include "src/st_sys.h"
#include "stlib/st_log.h"
#include "stlib/st_util.h"
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

ST_DNS_NAMESPACE_USING

struct BenchArg {
  int index;
  int queries;
  int port;
  int timeout;
  char server[64];
  char name[DOMAIN_MAX_SIZE];
  int use_fixed_name;
};

static volatile int g_pending = 0;
static volatile int g_ok = 0;
static volatile int g_fail = 0;

static void usage(const char *prog) {
  fprintf(stderr,
          "Usage: %s [options] [name]\n"
          "  -s <server>     DNS server IP (default 8.8.8.8)\n"
          "  -p <port>       DNS server port (default 53; local bench 5353)\n"
          "  -c <N>          coroutines (default 1)\n"
          "  -n <N>          total queries (default = coroutines)\n"
          "  -t <ms>         per-query timeout (default %d)\n"
          "  -q              quiet (raise log threshold)\n"
          "  -h              help\n"
          "\n"
          "If name is omitted, queries www.{i}.bench.local (use with local "
          "st_dnsserver).\n"
          "Process always exits after queries finish.\n",
          prog, DNS_TIMEOUT);
}

static void worker(void *args) {
  BenchArg *a = (BenchArg *)args;
  DNS dns;
  dns.set_dns_svr(a->server);
  dns.set_dns_port(a->port);

  int k;
  for (k = 0; k < a->queries; k++) {
    char name[DOMAIN_MAX_SIZE];
    if (a->use_fixed_name) {
      snprintf(name, sizeof(name), "%s", a->name);
    } else {
      snprintf(name, sizeof(name), "www.%d.bench.local",
               a->index * a->queries + k);
    }

    std::vector<int32_t> vc;
    time_t ttl = 0;
    int ret = dns.dns_lookup(name, vc, &ttl, a->timeout);
    if (ret == 0 && !vc.empty()) {
      __sync_fetch_and_add(&g_ok, 1);
      if (a->queries == 1 && a->index == 0) {
        std::vector<int32_t>::iterator it;
        for (it = vc.begin(); it != vc.end(); ++it) {
          struct in_addr addr;
          memcpy(&addr, &(*it), sizeof(struct in_addr));
          printf("%s %s ttl=%ld\n", name, inet_ntoa(addr), (long)ttl);
        }
      }
    } else {
      __sync_fetch_and_add(&g_fail, 1);
    }
  }
  __sync_fetch_and_sub(&g_pending, 1);
}

int main(int argc, char *argv[]) {
  const char *server = PUBLIC_DNS_DEFAULT_SERVER;
  int port = PUBLIC_DNS_DEFAULT_PORT;
  int ncoro = 1;
  int nquery = -1;
  int timeout = DNS_TIMEOUT;
  int quiet = 0;
  const char *qname = NULL;
  int c;

  while ((c = getopt(argc, argv, "s:p:c:n:t:qh")) != -1) {
    switch (c) {
    case 's':
      server = optarg;
      break;
    case 'p':
      port = atoi(optarg);
      break;
    case 'c':
      ncoro = atoi(optarg);
      break;
    case 'n':
      nquery = atoi(optarg);
      break;
    case 't':
      timeout = atoi(optarg);
      break;
    case 'q':
      quiet = 1;
      break;
    case 'h':
    default:
      usage(argv[0]);
      return (c == 'h') ? 0 : 1;
    }
  }
  if (optind < argc) {
    qname = argv[optind];
  }

  if (ncoro <= 0) {
    fprintf(stderr, "coroutines must be > 0\n");
    return 1;
  }
  if (port <= 0 || port > 65535) {
    fprintf(stderr, "invalid port\n");
    return 1;
  }
  if (timeout <= 0) {
    timeout = DNS_TIMEOUT;
  }
  if (nquery < 0) {
    nquery = ncoro;
  }
  if (nquery <= 0) {
    fprintf(stderr, "query count must be > 0\n");
    return 1;
  }
  if (ncoro > nquery) {
    ncoro = nquery;
  }

  if (quiet) {
    LOG_LEVEL(LLOG_CRIT);
  }

  if (!st_init_frame()) {
    fprintf(stderr, "st_init_frame failed\n");
    return 1;
  }
  st_set_hook_flag();

  int base = nquery / ncoro;
  int rem = nquery % ncoro;
  BenchArg *args = new BenchArg[ncoro];
  int i;
  for (i = 0; i < ncoro; i++) {
    args[i].index = i;
    args[i].queries = base + (i < rem ? 1 : 0);
    args[i].port = port;
    args[i].timeout = timeout;
    snprintf(args[i].server, sizeof(args[i].server), "%s", server);
    args[i].use_fixed_name = (qname != NULL) ? 1 : 0;
    if (qname != NULL) {
      snprintf(args[i].name, sizeof(args[i].name), "%s", qname);
    } else {
      args[i].name[0] = '\0';
    }
  }

  g_pending = ncoro;
  g_ok = 0;
  g_fail = 0;

  uint64_t start = Util::TimeMs();

  if (ncoro == 1) {
    /* 单协程：直接在 primordial 上跑，udp_sendrecv 内部 Yield */
    worker(&args[0]);
  } else {
    for (i = 0; i < ncoro; i++) {
      if (Frame::CreateThread(worker, &args[i]) == NULL) {
        fprintf(stderr, "CreateThread failed at %d\n", i);
        __sync_fetch_and_sub(&g_pending, 1);
        __sync_fetch_and_add(&g_fail, args[i].queries);
      }
    }
    /* 等待 worker 结束：st_sleep Yield 给 daemon / 业务协程，避免 Loop(true) */
    int64_t deadline =
        (int64_t)start + (int64_t)timeout * (base + 1) + 5000;
    while (g_pending > 0 && (int64_t)Util::TimeMs() < deadline) {
      st_sleep(10);
    }
  }

  uint64_t elapsed = Util::TimeMs() - start;
  if (elapsed == 0) {
    elapsed = 1;
  }
  double qps = (double)g_ok * 1000.0 / (double)elapsed;
  if (g_pending > 0) {
    fprintf(stderr, "warning: %d coroutines still pending\n", g_pending);
  }

  printf("SUMMARY success=%d fail=%d qps=%.2f elapsed_ms=%lu pending=%d\n",
         g_ok, g_fail, qps, (unsigned long)elapsed, g_pending);

  delete[] args;
  return (g_ok > 0 && g_pending == 0) ? 0 : 1;
}
