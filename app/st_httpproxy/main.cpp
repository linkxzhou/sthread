/*
 * Copyright (C) zhoulv2000@163.com
 *
 * HTTP 反代。槽位只活在这个样例里，不是 StConnectionManager 的连接池。
 * 不使用 eTCP_KEEPLIVE_CONN。上游走已经落地的 st_http_exchange。
 *
 * CallBack 在 RecvData 失败时不会走到 DoOutput。头太大时 DoInput 不返回 -1，
 * 置 overflow_ 并返回正长度，让 DoProcess 写出 400。
 */

#include "app/st_c.h"
#include "app/st_frame.h"
#include "http_client.h"
#include "src/st_server.h"
#include "stlib/st_log.h"
#include "stlib/st_util.h"
#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

using namespace sthread;
using namespace stlib;

struct Backend {
  char host[64];
  int port;
  unsigned int ip_be;
  int alive;
  StHttpConn slot[8];
  int in_use[8];
};

static Backend g_be[8];
static int g_nbackend = 0;
static int g_next = 0;
static int g_slots = 2;
static int g_timeout = 3000;
static int g_health_ms = 1000;

static int header_name_is(const char *p, const char *name) {
  while (*name != '\0') {
    char c = *p;
    char n = *name;
    if (c >= 'A' && c <= 'Z') {
      c = (char)(c - 'A' + 'a');
    }
    if (n >= 'A' && n <= 'Z') {
      n = (char)(n - 'A' + 'a');
    }
    if (c != n) {
      return 0;
    }
    p++;
    name++;
  }
  return 1;
}

static int find_headers_end(const char *p, int len) {
  int i;
  for (i = 0; i + 3 < len; i++) {
    if (p[i] == '\r' && p[i + 1] == '\n' && p[i + 2] == '\r' &&
        p[i + 3] == '\n') {
      return i + 4;
    }
  }
  return -1;
}

static int parse_hostport(const char *text, char *host, int hostcap, int *port,
                          unsigned int *ip_be) {
  const char *colon;
  char ip[64];
  struct in_addr ina;
  int hlen;
  if (text == NULL) {
    return -1;
  }
  colon = strrchr(text, ':');
  if (colon == NULL || colon == text) {
    return -1;
  }
  hlen = (int)(colon - text);
  if (hlen <= 0 || hlen >= hostcap || hlen >= (int)sizeof(ip)) {
    return -1;
  }
  memcpy(ip, text, (size_t)hlen);
  ip[hlen] = '\0';
  *port = atoi(colon + 1);
  if (*port <= 0 || *port > 65535) {
    return -1;
  }
  if (inet_pton(AF_INET, ip, &ina) != 1) {
    return -1;
  }
  snprintf(host, (size_t)hostcap, "%s", ip);
  *ip_be = ina.s_addr;
  return 0;
}

static Backend *pick_backend() {
  int start;
  int i;
  if (g_nbackend <= 0) {
    return NULL;
  }
  start = g_next;
  if (start < 0 || start >= g_nbackend) {
    start = 0;
  }
  /* 选中之后立刻拨号。这两步之间不 Yield，单 OS 线程不用锁。 */
  g_next = (start + 1) % g_nbackend;
  for (i = 0; i < g_nbackend; i++) {
    int idx = (start + i) % g_nbackend;
    if (g_be[idx].alive) {
      return &g_be[idx];
    }
  }
  return NULL;
}

static void probe_backend(Backend *be) {
  StHttpConn io;
  StHttpRequest req;
  StHttpResponse resp;
  int rc;
  st_http_conn_init(&io);
  memset(&req, 0, sizeof(req));
  memset(&resp, 0, sizeof(resp));
  req.method = "GET";
  req.host = be->host;
  req.port = be->port;
  req.path = "/";
  req.timeout_ms = g_timeout;
  req.keepalive = 0;
  req.ip_be = be->ip_be;
  rc = st_http_exchange(&req, &io, &resp);
  if (rc == 0 && resp.status >= 200 && resp.status < 300) {
    be->alive = 1;
  } else {
    be->alive = 0;
  }
  st_http_response_free(&resp);
  st_http_conn_close(&io);
}

static void health_loop(void *arg) {
  int i;
  (void)arg;
  for (;;) {
    st_sleep(g_health_ms);
    for (i = 0; i < g_nbackend; i++) {
      probe_backend(&g_be[i]);
    }
  }
}

class ProxyConn : public StServerConnection<ProxyConn> {
public:
  ProxyConn() : ready_(0), overflow_(0), out_len_(0) { out_[0] = '\0'; }

  virtual int32_t DoInput(void *buf, int32_t len) {
    ready_ = 0;
    overflow_ = 0;
    if (buf == NULL || len < 0) {
      return -1;
    }
    if (find_headers_end((char *)buf, len) >= 0) {
      ready_ = 1;
      return find_headers_end((char *)buf, len);
    }
    /* 头放不进 8192。返回正长度，CallBack 才会走到 DoOutput 写 400。 */
    if (len >= (int32_t)ST_RECV_BUFFSIZE) {
      overflow_ = 1;
      ready_ = 1;
      return len > 0 ? len : 1;
    }
    return 0;
  }

  virtual int32_t DoProcess() {
    if (GetSendBuffer() != NULL) {
      GetSendBuffer()->SetHaveSendLen(0);
    }
    out_len_ = 0;
    if (overflow_ || !ready_) {
      reply(400, "Bad Request", NULL, 0);
      return 0;
    }
    return forward();
  }

  virtual int32_t DoOutput(void *buf, int32_t &len) {
    if (out_len_ <= 0) {
      len = 0;
      return 0;
    }
    if (out_len_ > len) {
      return -1;
    }
    memcpy(buf, out_, (size_t)out_len_);
    len = out_len_;
    return 0;
  }

  virtual int32_t DoError(int32_t err) {
    LOG_ERROR("proxy conn error: %d", err);
    return 0;
  }

private:
  void reply(int status, const char *reason, const char *body, int body_len) {
    int n;
    if (body == NULL || body_len < 0) {
      body = "";
      body_len = 0;
    }
    n = snprintf(out_, sizeof(out_),
                 "HTTP/1.1 %d %s\r\n"
                 "Content-Length: %d\r\n"
                 "Connection: close\r\n"
                 "\r\n",
                 status, reason, body_len);
    if (n < 0 || n + body_len >= (int)sizeof(out_)) {
      n = snprintf(out_, sizeof(out_),
                   "HTTP/1.1 502 Bad Gateway\r\n"
                   "Content-Length: 0\r\n"
                   "Connection: close\r\n"
                   "\r\n");
      out_len_ = n > 0 ? n : 0;
      return;
    }
    if (body_len > 0) {
      memcpy(out_ + n, body, (size_t)body_len);
    }
    out_len_ = n + body_len;
  }

  int forward() {
    StBuffer *rb = GetRecvBuffer();
    char *buf;
    int have;
    int hend;
    char method[16];
    char path[1024];
    char *sp;
    char *sp2;
    int https = 0;
    int content_length = 0;
    const char *body;
    int body_have;
    Backend *be;
    int slot;
    uint64_t start;
    StHttpRequest req;
    StHttpResponse resp;
    int rc;
    int margin = 128;

    if (rb == NULL) {
      reply(400, "Bad Request", NULL, 0);
      return 0;
    }
    buf = (char *)rb->GetBuffer();
    have = (int)rb->GetHaveRecvLen();
    hend = find_headers_end(buf, have);
    if (hend < 0) {
      reply(400, "Bad Request", NULL, 0);
      return 0;
    }
    sp = strchr(buf, ' ');
    if (sp == NULL || (int)(sp - buf) <= 0 || (int)(sp - buf) >= 16) {
      reply(400, "Bad Request", NULL, 0);
      return 0;
    }
    memcpy(method, buf, (size_t)(sp - buf));
    method[sp - buf] = '\0';
    sp2 = strchr(sp + 1, ' ');
    if (sp2 == NULL || sp2 - (sp + 1) <= 0 || sp2 - (sp + 1) >= 1000) {
      reply(400, "Bad Request", NULL, 0);
      return 0;
    }
    memcpy(path, sp + 1, (size_t)(sp2 - (sp + 1)));
    path[sp2 - (sp + 1)] = '\0';
    if (strcmp(method, "CONNECT") == 0) {
      reply(400, "Bad Request", NULL, 0);
      return 0;
    }
    if (strncmp(path, "https://", 8) == 0) {
      reply(400, "Bad Request", NULL, 0);
      return 0;
    }
    if (strncmp(path, "http://", 7) == 0) {
      char *slash = strchr(path + 7, '/');
      if (slash == NULL) {
        snprintf(path, sizeof(path), "/");
      } else {
        memmove(path, slash, strlen(slash) + 1);
      }
      https = 0;
    } else if (path[0] != '/') {
      reply(400, "Bad Request", NULL, 0);
      return 0;
    }
    (void)https;
    content_length = header_content_length(buf, hend);
    body = buf + hend;
    body_have = have - hend;
    if (content_length < 0 || content_length > body_have) {
      /* 正文没有在第一次 RecvData 里到齐。 */
      reply(400, "Bad Request", NULL, 0);
      return 0;
    }

    be = pick_backend();
    if (be == NULL) {
      reply(502, "Bad Gateway", NULL, 0);
      return 0;
    }
    slot = -1;
    start = Util::TimeMs();
    while (slot < 0) {
      int i;
      for (i = 0; i < g_slots; i++) {
        if (!be->in_use[i]) {
          be->in_use[i] = 1;
          slot = i;
          break;
        }
      }
      if (slot >= 0) {
        break;
      }
      if ((int)(Util::TimeMs() - start) >= g_timeout) {
        reply(502, "Bad Gateway", NULL, 0);
        return 0;
      }
      st_sleep(1);
    }

    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));
    req.method = method;
    req.host = be->host;
    req.port = be->port;
    req.path = path;
    req.body = content_length > 0 ? body : NULL;
    req.body_len = content_length;
    req.timeout_ms = g_timeout;
    req.keepalive = 1;
    req.ip_be = be->ip_be;
    rc = st_http_exchange(&req, &be->slot[slot], &resp);
    be->in_use[slot] = 0;
    if (rc != 0 || resp.transport_error) {
      be->alive = 0;
      st_http_conn_close(&be->slot[slot]);
      st_http_response_free(&resp);
      reply(502, "Bad Gateway", NULL, 0);
      return 0;
    }
    if (resp.body_len > (int)ST_SEND_BUFFSIZE - margin) {
      st_http_response_free(&resp);
      reply(502, "Bad Gateway", NULL, 0);
      return 0;
    }
    reply(resp.status,
          resp.status >= 200 && resp.status < 300 ? "OK" : "Upstream",
          resp.body, resp.body_len);
    st_http_response_free(&resp);
    return 0;
  }

  int header_content_length(const char *buf, int hend) {
    int i = 0;
    while (i + 1 < hend) {
      if (header_name_is(buf + i, "content-length:")) {
        return atoi(buf + i + 15);
      }
      while (i + 1 < hend && !(buf[i] == '\r' && buf[i + 1] == '\n')) {
        i++;
      }
      i += 2;
    }
    return 0;
  }

  int ready_;
  int overflow_;
  char out_[ST_SEND_BUFFSIZE];
  int out_len_;
};

static void usage(const char *argv0) {
  fprintf(stderr,
          "usage: %s [-l HOST:PORT] [-b HOST:PORT]... [-t MS] [-s SLOTS] "
          "[-H MS]\n"
          "  -l listen, default 0.0.0.0:18080\n"
          "  -b backend, repeatable, 1..8, IPv4 literal\n"
          "  -t upstream timeout ms, default 3000\n"
          "  -s slots per backend, default 2, max 8\n"
          "  -H health interval ms, default 1000; 0 disables probe\n",
          argv0);
}

int main(int argc, char *argv[]) {
  char listen_host[64];
  int listen_port = 18080;
  int c;
  int i;
  StServer<ProxyConn, eTCP_CONN> *server;
  StNetAddr addr;
  int fd;

  signal(SIGPIPE, SIG_IGN);
  LOG_LEVEL(LLOG_CRIT);
  snprintf(listen_host, sizeof(listen_host), "0.0.0.0");

  while ((c = getopt(argc, argv, "l:b:t:s:H:h")) != -1) {
    switch (c) {
    case 'l': {
      unsigned int ignore_ip = 0;
      if (parse_hostport(optarg, listen_host, (int)sizeof(listen_host),
                         &listen_port, &ignore_ip) != 0) {
        usage(argv[0]);
        return 2;
      }
      break;
    }
    case 'b':
      if (g_nbackend >= 8) {
        fprintf(stderr, "at most 8 backends\n");
        return 2;
      }
      if (parse_hostport(
              optarg, g_be[g_nbackend].host, (int)sizeof(g_be[g_nbackend].host),
              &g_be[g_nbackend].port, &g_be[g_nbackend].ip_be) != 0) {
        usage(argv[0]);
        return 2;
      }
      g_be[g_nbackend].alive = 1;
      g_nbackend++;
      break;
    case 't':
      g_timeout = atoi(optarg);
      break;
    case 's':
      g_slots = atoi(optarg);
      break;
    case 'H':
      g_health_ms = atoi(optarg);
      break;
    case 'h':
      usage(argv[0]);
      return 0;
    default:
      usage(argv[0]);
      return 2;
    }
  }
  if (g_nbackend < 1 || g_timeout <= 0 || g_slots <= 0 || g_slots > 8 ||
      g_health_ms < 0) {
    usage(argv[0]);
    return 2;
  }
  for (i = 0; i < g_nbackend; i++) {
    int s;
    for (s = 0; s < 8; s++) {
      st_http_conn_init(&g_be[i].slot[s]);
      g_be[i].in_use[s] = 0;
    }
    g_be[i].alive = 1;
  }

  if (!st_init_frame()) {
    fprintf(stderr, "st_init_frame failed\n");
    return 1;
  }
  st_set_hook_flag();

  if (g_health_ms > 0) {
    if (Frame::CreateThread(health_loop, NULL) == NULL) {
      fprintf(stderr, "health thread failed\n");
      return 1;
    }
  }

  server = new StServer<ProxyConn, eTCP_CONN>();
  server->SetHookFlag();
  addr.SetAddr(listen_host, (uint16_t)listen_port);
  fd = server->CreateSocket(addr);
  if (fd < 0) {
    fprintf(stderr, "CreateSocket failed: %d\n", fd);
    return 1;
  }
  if (!server->Listen()) {
    fprintf(stderr, "Listen failed on %s:%d\n", listen_host, listen_port);
    return 1;
  }
  printf("listening %s:%d\n", listen_host, listen_port);
  fflush(stdout);
  server->Loop();
  return 0;
}
