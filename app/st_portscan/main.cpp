/*
 * Copyright (C) zhoulv2000@163.com
 *
 * 并发 st_connect。超时（ETIME）和拒绝（ECONNREFUSED）分开计。
 * 分类看 Create 失败后保存下来的 errno，不看 -1 / -2。
 */

#include "app/st_c.h"
#include "app/st_frame.h"
#include "src/st_sys.h"
#include "stlib/st_log.h"
#include "stlib/st_netaddr.h"
#include "stlib/st_util.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

using namespace sthread;
using namespace stlib;

static int *g_ports = NULL;
static int g_nports = 0;
static volatile int g_next = 0;
static volatile int g_open = 0;
static volatile int g_refused = 0;
static volatile int g_timeout = 0;
static volatile int g_other = 0;
static volatile int g_pending = 0;
static int g_timeout_ms = 300;
static char g_host[64];

static void usage(const char *argv0) {
  fprintf(stderr,
          "usage: %s [-c CONC] [-t MS] [-p SPEC] host\n"
          "  -c  in-flight coroutines (default 64)\n"
          "  -t  connect timeout ms (default 300)\n"
          "  -p  ports, e.g. 22,80,8000-8010 (default 80,443)\n"
          "  host is an IPv4 literal\n"
          "exit: 0 scan finished (open may be 0); 2 bad args\n",
          argv0);
}

static int add_port(int port) {
  if (port <= 0 || port > 65535) {
    return -1;
  }
  if (g_nports >= 65535) {
    return -1;
  }
  g_ports[g_nports++] = port;
  return 0;
}

static int parse_range(const char *tok) {
  const char *dash = strchr(tok, '-');
  int a;
  int b;
  int p;
  if (dash == NULL) {
    return add_port(atoi(tok));
  }
  a = atoi(tok);
  b = atoi(dash + 1);
  if (a <= 0 || b < a || b > 65535) {
    return -1;
  }
  if (g_nports + (b - a + 1) > 65535) {
    return -1;
  }
  for (p = a; p <= b; p++) {
    if (add_port(p) != 0) {
      return -1;
    }
  }
  return 0;
}

static int parse_spec(const char *spec) {
  char buf[4096];
  char *p;
  char *comma;
  int n;
  if (spec == NULL || spec[0] == '\0') {
    return -1;
  }
  n = (int)strlen(spec);
  if (n >= (int)sizeof(buf)) {
    return -1;
  }
  memcpy(buf, spec, (size_t)n + 1);
  p = buf;
  while (*p != '\0') {
    comma = strchr(p, ',');
    if (comma != NULL) {
      *comma = '\0';
    }
    if (p[0] == '\0' || parse_range(p) != 0) {
      return -1;
    }
    if (comma == NULL) {
      break;
    }
    p = comma + 1;
  }
  return g_nports > 0 ? 0 : -1;
}

static void worker(void *arg) {
  (void)arg;
  for (;;) {
    int i = __sync_fetch_and_add(&g_next, 1);
    StExecClientConnection *conn;
    StNetAddr addr;
    int32_t rc;
    int err;
    if (i >= g_nports) {
      break;
    }
    conn = Instance<StConnectionManager<StExecClientConnection> >()->AllocPtr(
        eTCP_CONN);
    if (conn == NULL) {
      __sync_fetch_and_add(&g_other, 1);
      continue;
    }
    conn->SetTimeout(g_timeout_ms);
    addr.SetAddr(g_host, (uint16_t)g_ports[i]);
    errno = 0;
    rc = conn->Create(addr);
    err = errno;
    Instance<StConnectionManager<StExecClientConnection> >()->FreePtr(conn);
    if (rc >= 0) {
      __sync_fetch_and_add(&g_open, 1);
      printf("OPEN %d\n", g_ports[i]);
      fflush(stdout);
    } else if (err == ETIME) {
      __sync_fetch_and_add(&g_timeout, 1);
    } else if (err == ECONNREFUSED) {
      __sync_fetch_and_add(&g_refused, 1);
    } else {
      __sync_fetch_and_add(&g_other, 1);
    }
  }
  __sync_fetch_and_sub(&g_pending, 1);
}

int main(int argc, char *argv[]) {
  int ncoro = 64;
  int timeout = 300;
  const char *spec = "80,443";
  const char *host = NULL;
  int c;
  struct in_addr ina;
  int i;
  uint64_t start;
  uint64_t elapsed;

  signal(SIGPIPE, SIG_IGN);
  LOG_LEVEL(LLOG_CRIT);

  while ((c = getopt(argc, argv, "c:t:p:h")) != -1) {
    switch (c) {
    case 'c':
      ncoro = atoi(optarg);
      break;
    case 't':
      timeout = atoi(optarg);
      break;
    case 'p':
      spec = optarg;
      break;
    case 'h':
      usage(argv[0]);
      return 0;
    default:
      usage(argv[0]);
      return 2;
    }
  }
  if (optind + 1 != argc) {
    usage(argv[0]);
    return 2;
  }
  host = argv[optind];
  if (inet_pton(AF_INET, host, &ina) != 1) {
    fprintf(stderr, "host must be an IPv4 literal\n");
    return 2;
  }
  if (ncoro <= 0 || timeout <= 0) {
    usage(argv[0]);
    return 2;
  }
  g_ports = new int[65535];
  g_nports = 0;
  if (parse_spec(spec) != 0) {
    fprintf(stderr, "bad -p spec\n");
    delete[] g_ports;
    return 2;
  }
  if (ncoro > g_nports) {
    ncoro = g_nports;
  }
  snprintf(g_host, sizeof(g_host), "%s", host);
  (void)ina;
  g_timeout_ms = timeout;

  if (!st_init_frame()) {
    fprintf(stderr, "st_init_frame failed\n");
    delete[] g_ports;
    return 1;
  }
  st_set_hook_flag();

  g_pending = ncoro;
  start = Util::TimeMs();
  if (ncoro == 1) {
    worker(NULL);
  } else {
    for (i = 0; i < ncoro; i++) {
      if (Frame::CreateThread(worker, NULL) == NULL) {
        __sync_fetch_and_sub(&g_pending, 1);
      }
    }
    {
      int64_t deadline =
          (int64_t)start + (int64_t)timeout * (int64_t)g_nports + 5000;
      while (g_pending > 0 && (int64_t)Util::TimeMs() < deadline) {
        st_sleep(10);
      }
    }
  }
  elapsed = Util::TimeMs() - start;
  printf("SUMMARY open=%d refused=%d timeout=%d other=%d elapsed_ms=%llu\n",
         g_open, g_refused, g_timeout, g_other, (unsigned long long)elapsed);
  fflush(stdout);
  delete[] g_ports;
  if (g_pending > 0) {
    return 1;
  }
  return 0;
}
