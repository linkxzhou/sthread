/*
 * Copyright (C) zhoulv2000@163.com
 *
 * RESP 客户端。协议在样例里，传输用 StExecClientConnection + st_send /
 * st_recv。 每个请求一条短连接。同一协程复用连接不做，避免踩 FreePtr 仍关闭
 * fd。
 */

#include "app/st_c.h"
#include "app/st_frame.h"
#include "resp.h"
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

static const char *g_cmd_argv[8];
static int g_cmd_argc = 0;
static char g_host[64];
static int g_port = 6379;
static int g_timeout = 3000;
static int g_quiet = 0;
static int g_total = 0;
static volatile int g_pending = 0;
static volatile int g_ok = 0;
static volatile int g_fail = 0;
static volatile int g_lat_n = 0;
static int *g_lat = NULL;
static char g_reply[4096];
static int g_reply_len = 0;

static int time_left(uint64_t start, int timeout_ms) {
  int used = (int)(Util::TimeMs() - start);
  if (used >= timeout_ms) {
    return 0;
  }
  return timeout_ms - used;
}

static int send_all(int fd, const char *buf, int len, uint64_t start,
                    int timeout_ms) {
  int off = 0;
  while (off < len) {
    int left = time_left(start, timeout_ms);
    ssize_t n;
    if (left <= 0) {
      errno = ETIME;
      return -1;
    }
    n = st_send(fd, buf + off, (size_t)(len - off), 0, left);
    if (n < 0) {
      return -1;
    }
    if (n == 0) {
      errno = EPIPE;
      return -1;
    }
    off += (int)n;
  }
  return 0;
}

static int value_ok(const RespValue *v) {
  if (v == NULL) {
    return 0;
  }
  if (v->type == RESP_ERROR || v->type == RESP_NONE) {
    return 0;
  }
  return 1;
}

static void format_reply(const RespValue *v, char *out, int cap) {
  int n = 0;
  if (v == NULL || cap <= 0) {
    return;
  }
  out[0] = '\0';
  if (v->type == RESP_SIMPLE || v->type == RESP_ERROR || v->type == RESP_BULK) {
    if (v->buf != NULL) {
      n = snprintf(out, (size_t)cap, "%s", v->buf);
    }
  } else if (v->type == RESP_INT) {
    n = snprintf(out, (size_t)cap, "%lld", (long long)v->i);
  } else if (v->type == RESP_NULL) {
    n = snprintf(out, (size_t)cap, "(nil)");
  } else if (v->type == RESP_ARRAY) {
    int i;
    for (i = 0; i < v->n && n < cap - 1; i++) {
      char one[1024];
      int m;
      format_reply(&v->elem[i], one, (int)sizeof(one));
      m = snprintf(out + n, (size_t)(cap - n), "%s%s", i ? "\n" : "", one);
      if (m < 0) {
        break;
      }
      n += m;
    }
  }
  if (n < 0 || n >= cap) {
    out[cap - 1] = '\0';
  }
}

static int exchange_once(int record_reply) {
  StExecClientConnection *conn;
  StNetAddr addr;
  char req[4096];
  int req_len;
  uint64_t start;
  RespParser parser;
  RespValue value;
  int fd;
  int rc;
  int ok = 0;

  memset(&value, 0, sizeof(value));
  req_len = resp_encode(req, (int)sizeof(req), g_cmd_argv, g_cmd_argc);
  if (req_len < 0) {
    return 0;
  }
  conn = Instance<StConnectionManager<StExecClientConnection> >()->AllocPtr(
      eTCP_CONN);
  if (conn == NULL) {
    return 0;
  }
  conn->SetTimeout(g_timeout);
  addr.SetAddr(g_host, (uint16_t)g_port);
  fd = conn->Create(addr);
  if (fd < 0) {
    Instance<StConnectionManager<StExecClientConnection> >()->FreePtr(conn);
    return 0;
  }
  start = Util::TimeMs();
  if (send_all(fd, req, req_len, start, g_timeout) != 0) {
    Instance<StConnectionManager<StExecClientConnection> >()->FreePtr(conn);
    return 0;
  }
  resp_parser_init(&parser);
  for (;;) {
    char buf[512];
    int left = time_left(start, g_timeout);
    int n;
    if (left <= 0) {
      errno = ETIME;
      break;
    }
    n = st_recv(fd, buf, (int)sizeof(buf), 0, left);
    if (n < 0) {
      break;
    }
    if (n == 0) {
      rc = resp_parse(&parser, NULL, 0, &value);
      if (rc == 1) {
        ok = value_ok(&value);
      }
      break;
    }
    rc = resp_parse(&parser, buf, n, &value);
    if (rc == 1) {
      ok = value_ok(&value);
      break;
    }
    if (rc < 0) {
      break;
    }
  }
  if (ok && record_reply) {
    format_reply(&value, g_reply, (int)sizeof(g_reply));
    g_reply_len = (int)strlen(g_reply);
  }
  resp_free(&value);
  resp_parser_free(&parser);
  Instance<StConnectionManager<StExecClientConnection> >()->FreePtr(conn);
  return ok;
}

static void worker(void *arg) {
  int count = (int)(long)arg;
  int i;
  for (i = 0; i < count; i++) {
    uint64_t t0 = Util::TimeMs();
    int record = (!g_quiet && g_total == 1) ? 1 : 0;
    int ok = exchange_once(record);
    int slot = __sync_fetch_and_add(&g_lat_n, 1);
    int elapsed = (int)(Util::TimeMs() - t0);
    if (slot >= 0 && slot < g_total && g_lat != NULL) {
      g_lat[slot] = elapsed;
    }
    if (ok) {
      __sync_fetch_and_add(&g_ok, 1);
    } else {
      __sync_fetch_and_add(&g_fail, 1);
    }
  }
  __sync_fetch_and_sub(&g_pending, 1);
}

static int lat_cmp(const void *a, const void *b) {
  int ia = *(const int *)a;
  int ib = *(const int *)b;
  if (ia < ib) {
    return -1;
  }
  if (ia > ib) {
    return 1;
  }
  return 0;
}

static void usage(const char *argv0) {
  fprintf(stderr,
          "usage: %s [-h HOST] [-p PORT] [-c CONC] [-n TOTAL] [-t MS] [-q]\n"
          "          command [args...]\n"
          "  command: ping | set <key> <value> | get <key> | incr <key>\n"
          "  default 127.0.0.1:6379, -c 1, -n = -c, -t 3000\n"
          "exit: 0 all ok; 1 failure; 2 bad args\n",
          argv0);
}

int main(int argc, char *argv[]) {
  int ncoro = 1;
  int total = -1;
  int c;
  struct in_addr ina;
  int base;
  int rem;
  int i;
  uint64_t start;
  uint64_t elapsed;
  double qps;
  int p50 = 0;
  int p99 = 0;
  int samples;

  signal(SIGPIPE, SIG_IGN);
  LOG_LEVEL(LLOG_CRIT);
  snprintf(g_host, sizeof(g_host), "127.0.0.1");

  while ((c = getopt(argc, argv, "h:p:c:n:t:q")) != -1) {
    switch (c) {
    case 'h':
      snprintf(g_host, sizeof(g_host), "%s", optarg);
      break;
    case 'p':
      g_port = atoi(optarg);
      break;
    case 'c':
      ncoro = atoi(optarg);
      break;
    case 'n':
      total = atoi(optarg);
      break;
    case 't':
      g_timeout = atoi(optarg);
      break;
    case 'q':
      g_quiet = 1;
      break;
    default:
      usage(argv[0]);
      return 2;
    }
  }
  if (optind >= argc) {
    usage(argv[0]);
    return 2;
  }
  if (inet_pton(AF_INET, g_host, &ina) != 1 || g_port <= 0 || g_port > 65535 ||
      ncoro <= 0 || g_timeout <= 0) {
    usage(argv[0]);
    return 2;
  }
  g_cmd_argv[0] = NULL;
  if (strcmp(argv[optind], "ping") == 0) {
    if (optind + 1 != argc) {
      usage(argv[0]);
      return 2;
    }
    g_cmd_argv[0] = "PING";
    g_cmd_argc = 1;
  } else if (strcmp(argv[optind], "set") == 0) {
    if (optind + 3 != argc) {
      usage(argv[0]);
      return 2;
    }
    g_cmd_argv[0] = "SET";
    g_cmd_argv[1] = argv[optind + 1];
    g_cmd_argv[2] = argv[optind + 2];
    g_cmd_argc = 3;
  } else if (strcmp(argv[optind], "get") == 0) {
    if (optind + 2 != argc) {
      usage(argv[0]);
      return 2;
    }
    g_cmd_argv[0] = "GET";
    g_cmd_argv[1] = argv[optind + 1];
    g_cmd_argc = 2;
  } else if (strcmp(argv[optind], "incr") == 0) {
    if (optind + 2 != argc) {
      usage(argv[0]);
      return 2;
    }
    g_cmd_argv[0] = "INCR";
    g_cmd_argv[1] = argv[optind + 1];
    g_cmd_argc = 2;
  } else {
    fprintf(stderr, "unknown command\n");
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
  g_total = total;
  g_lat = new int[total];
  memset(g_lat, 0, sizeof(int) * (size_t)total);
  (void)ina;

  if (!st_init_frame()) {
    fprintf(stderr, "st_init_frame failed\n");
    delete[] g_lat;
    return 1;
  }
  st_set_hook_flag();

  base = total / ncoro;
  rem = total % ncoro;
  g_pending = ncoro;
  start = Util::TimeMs();
  if (ncoro == 1) {
    worker((void *)(long)total);
  } else {
    for (i = 0; i < ncoro; i++) {
      int count = base + (i < rem ? 1 : 0);
      if (Frame::CreateThread(worker, (void *)(long)count) == NULL) {
        __sync_fetch_and_sub(&g_pending, 1);
        __sync_fetch_and_add(&g_fail, count);
      }
    }
    {
      int64_t deadline = (int64_t)start + (int64_t)g_timeout * (int64_t)base +
                         (int64_t)g_timeout + 5000;
      while (g_pending > 0 && (int64_t)Util::TimeMs() < deadline) {
        st_sleep(10);
      }
    }
  }
  elapsed = Util::TimeMs() - start;
  if (elapsed == 0) {
    elapsed = 1;
  }
  samples = g_ok + g_fail;
  if (samples > total) {
    samples = total;
  }
  if (samples > 0) {
    qsort(g_lat, (size_t)samples, sizeof(int), lat_cmp);
    p50 = g_lat[(samples - 1) * 50 / 100];
    p99 = g_lat[(samples - 1) * 99 / 100];
  }
  qps = (double)g_ok * 1000.0 / (double)elapsed;
  if (!g_quiet && total == 1 && g_reply_len > 0) {
    fwrite(g_reply, 1, (size_t)g_reply_len, stdout);
    if (g_reply[g_reply_len - 1] != '\n') {
      fputc('\n', stdout);
    }
  }
  printf("SUMMARY ok=%d fail=%d qps=%.2f elapsed_ms=%llu p50_ms=%d "
         "p99_ms=%d\n",
         g_ok, g_fail, qps, (unsigned long long)elapsed, p50, p99);
  fflush(stdout);
  delete[] g_lat;
  if (g_pending > 0 || g_fail > 0 || g_ok != total) {
    return 1;
  }
  return 0;
}
