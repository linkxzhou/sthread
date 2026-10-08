/*
 * Copyright (C) zhoulv2000@163.com
 */

#include "http_client.h"
#include "http_parser.h"
#include "stlib/st_netaddr.h"
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

using namespace stlib;

namespace {

struct ByteBuf {
  char *p;
  int len;
  int cap;
};

struct ParseState {
  int complete;
  int status;
  int error;
  int keep_alive;
  int verbose;
  char *body;
  int body_len;
  int body_cap;
  char field[128];
  int field_len;
  char value[256];
  int value_len;
};

int buf_add(ByteBuf *b, const char *s, int n) {
  char *np;
  int cap;
  if (s == NULL) {
    return -1;
  }
  if (n < 0) {
    n = (int)strlen(s);
  }
  cap = b->cap > 0 ? b->cap : 512;
  while (b->len + n + 1 > cap) {
    if (cap > 64 * 1024 * 1024) {
      return -1;
    }
    cap *= 2;
  }
  if (cap != b->cap) {
    np = (char *)realloc(b->p, (size_t)cap);
    if (np == NULL) {
      return -1;
    }
    b->p = np;
    b->cap = cap;
  }
  memcpy(b->p + b->len, s, (size_t)n);
  b->len += n;
  b->p[b->len] = '\0';
  return 0;
}

int header_supplied(const StHttpRequest *req, const char *name) {
  int nlen = (int)strlen(name);
  int i;
  for (i = 0; i < req->header_count; i++) {
    const char *h = req->headers[i].text;
    if (h == NULL) {
      continue;
    }
    if ((int)strlen(h) > nlen && strncasecmp(h, name, (size_t)nlen) == 0 &&
        h[nlen] == ':') {
      return 1;
    }
  }
  return 0;
}

int append_body(ParseState *st, const char *at, size_t len) {
  int need;
  int cap;
  char *np;
  if (len == 0) {
    return 0;
  }
  if (len > 64 * 1024 * 1024) {
    return -1;
  }
  need = st->body_len + (int)len + 1;
  cap = st->body_cap > 0 ? st->body_cap : 4096;
  while (cap < need) {
    if (cap > 64 * 1024 * 1024) {
      return -1;
    }
    cap *= 2;
  }
  if (cap != st->body_cap) {
    np = (char *)realloc(st->body, (size_t)cap);
    if (np == NULL) {
      return -1;
    }
    st->body = np;
    st->body_cap = cap;
  }
  memcpy(st->body + st->body_len, at, len);
  st->body_len += (int)len;
  st->body[st->body_len] = '\0';
  return 0;
}

void flush_header(ParseState *st) {
  if (!st->verbose) {
    st->field_len = 0;
    st->value_len = 0;
    return;
  }
  if (st->field_len == 0 && st->value_len == 0) {
    return;
  }
  fprintf(stderr, "< %.*s: %.*s\n", st->field_len, st->field, st->value_len,
          st->value);
  st->field_len = 0;
  st->value_len = 0;
}

extern "C" int on_header_field(http_parser *p, const char *at, size_t len) {
  ParseState *st = (ParseState *)p->data;
  size_t i;
  /* 上一对的 value 已经收齐，新的 field 到来时打出来。 */
  if (st->value_len > 0) {
    flush_header(st);
  }
  for (i = 0; i < len && st->field_len < (int)sizeof(st->field) - 1; i++) {
    st->field[st->field_len++] = at[i];
  }
  st->field[st->field_len] = '\0';
  return 0;
}

extern "C" int on_header_value(http_parser *p, const char *at, size_t len) {
  ParseState *st = (ParseState *)p->data;
  size_t i;
  for (i = 0; i < len && st->value_len < (int)sizeof(st->value) - 1; i++) {
    st->value[st->value_len++] = at[i];
  }
  st->value[st->value_len] = '\0';
  return 0;
}

extern "C" int on_headers_complete(http_parser *p) {
  ParseState *st = (ParseState *)p->data;
  flush_header(st);
  st->status = p->status_code;
  return 0;
}

extern "C" int on_body(http_parser *p, const char *at, size_t len) {
  ParseState *st = (ParseState *)p->data;
  if (append_body(st, at, len) != 0) {
    st->error = 1;
    return -1;
  }
  return 0;
}

extern "C" int on_message_complete(http_parser *p) {
  ParseState *st = (ParseState *)p->data;
  st->complete = 1;
  st->status = p->status_code;
  st->keep_alive = http_should_keep_alive(p);
  return 0;
}

int build_request(const StHttpRequest *req, ByteBuf *out) {
  char line[512];
  int n;
  int i;
  int is_post;
  const char *path = req->path;
  const char *method = req->method != NULL ? req->method : "GET";
  if (path == NULL || path[0] == '\0') {
    path = "/";
  }
  memset(out, 0, sizeof(*out));
  if (buf_add(out, method, -1) != 0 || buf_add(out, " ", 1) != 0 ||
      buf_add(out, path, -1) != 0 || buf_add(out, " HTTP/1.1\r\n", -1) != 0) {
    return -1;
  }
  if (!header_supplied(req, "Host")) {
    if (req->port == 80) {
      n = snprintf(line, sizeof(line), "Host: %s\r\n", req->host);
    } else {
      n = snprintf(line, sizeof(line), "Host: %s:%d\r\n", req->host, req->port);
    }
    if (n < 0 || n >= (int)sizeof(line) || buf_add(out, line, n) != 0) {
      return -1;
    }
  }
  if (!header_supplied(req, "User-Agent")) {
    if (buf_add(out, "User-Agent: sthread-httpclient/1.0\r\n", -1) != 0) {
      return -1;
    }
  }
  if (!header_supplied(req, "Accept")) {
    if (buf_add(out, "Accept: */*\r\n", -1) != 0) {
      return -1;
    }
  }
  if (!header_supplied(req, "Connection")) {
    if (req->keepalive) {
      if (buf_add(out, "Connection: keep-alive\r\n", -1) != 0) {
        return -1;
      }
    } else if (buf_add(out, "Connection: close\r\n", -1) != 0) {
      return -1;
    }
  }
  is_post = (strcmp(method, "POST") == 0);
  if (req->body_len > 0 || is_post) {
    if (!header_supplied(req, "Content-Type")) {
      if (buf_add(out, "Content-Type: application/x-www-form-urlencoded\r\n",
                  -1) != 0) {
        return -1;
      }
    }
    if (!header_supplied(req, "Content-Length")) {
      n = snprintf(line, sizeof(line), "Content-Length: %d\r\n", req->body_len);
      if (n < 0 || n >= (int)sizeof(line) || buf_add(out, line, n) != 0) {
        return -1;
      }
    }
  }
  for (i = 0; i < req->header_count; i++) {
    if (req->headers[i].text == NULL) {
      continue;
    }
    if (buf_add(out, req->headers[i].text, -1) != 0 ||
        buf_add(out, "\r\n", 2) != 0) {
      return -1;
    }
  }
  if (buf_add(out, "\r\n", 2) != 0) {
    return -1;
  }
  if (req->body_len > 0 && req->body != NULL) {
    if (buf_add(out, req->body, req->body_len) != 0) {
      return -1;
    }
  }
  return 0;
}

int remaining_ms(uint64_t start, int timeout_ms) {
  int used = (int)(Util::TimeMs() - start);
  if (used >= timeout_ms) {
    return 0;
  }
  return timeout_ms - used;
}

int ensure_conn(const StHttpRequest *req, StHttpConn *io, uint64_t start) {
  char ip[32];
  struct in_addr ina;
  StNetAddr addr;
  int fd;
  int left;
  if (io->conn != NULL && io->fd >= 0) {
    return 0;
  }
  ina.s_addr = req->ip_be;
  if (inet_ntop(AF_INET, &ina, ip, sizeof(ip)) == NULL) {
    return -1;
  }
  addr.SetAddr(ip, (uint16_t)req->port);
  io->conn = Instance<StConnectionManager<StExecClientConnection> >()->AllocPtr(
      eTCP_CONN, &addr);
  if (io->conn == NULL) {
    return -1;
  }
  left = remaining_ms(start, req->timeout_ms);
  if (left <= 0) {
    st_http_conn_close(io);
    errno = ETIME;
    return -1;
  }
  io->conn->SetTimeout(left);
  fd = io->conn->Create(addr);
  if (fd < 0) {
    int saved = errno;
    st_http_conn_close(io);
    errno = saved;
    return -1;
  }
  io->fd = fd;
  return 0;
}

} // namespace

void st_http_conn_init(StHttpConn *io) {
  if (io == NULL) {
    return;
  }
  io->conn = NULL;
  io->fd = -1;
}

void st_http_conn_close(StHttpConn *io) {
  if (io == NULL) {
    return;
  }
  if (io->conn != NULL) {
    Instance<StConnectionManager<StExecClientConnection> >()->FreePtr(io->conn);
    io->conn = NULL;
  }
  io->fd = -1;
}

void st_http_response_free(StHttpResponse *resp) {
  if (resp == NULL) {
    return;
  }
  free(resp->body);
  resp->body = NULL;
  resp->body_len = 0;
}

int st_http_exchange(const StHttpRequest *req, StHttpConn *io,
                     StHttpResponse *resp) {
  ByteBuf raw;
  ParseState st;
  http_parser parser;
  http_parser_settings settings;
  uint64_t start;
  char rbuf[4096];
  int left;
  ssize_t sent;
  int n;

  if (req == NULL || io == NULL || resp == NULL) {
    return -1;
  }
  memset(resp, 0, sizeof(*resp));
  memset(&raw, 0, sizeof(raw));
  memset(&st, 0, sizeof(st));
  st.verbose = req->verbose;
  start = Util::TimeMs();

  if (build_request(req, &raw) != 0) {
    free(raw.p);
    resp->transport_error = 1;
    return -1;
  }
  if (req->verbose) {
    fprintf(stderr, "> %s", raw.p);
  }
  if (ensure_conn(req, io, start) != 0) {
    free(raw.p);
    resp->transport_error = 1;
    return -1;
  }

  left = remaining_ms(start, req->timeout_ms);
  sent = st_send(io->fd, raw.p, (size_t)raw.len, 0, left);
  free(raw.p);
  raw.p = NULL;
  if (sent < 0 || (int)sent != raw.len) {
    st_http_conn_close(io);
    resp->transport_error = 1;
    return -1;
  }

  http_parser_init(&parser, HTTP_RESPONSE);
  http_parser_settings_init(&settings);
  parser.data = &st;
  settings.on_header_field = on_header_field;
  settings.on_header_value = on_header_value;
  settings.on_headers_complete = on_headers_complete;
  settings.on_body = on_body;
  settings.on_message_complete = on_message_complete;

  while (!st.complete && !st.error) {
    size_t parsed;
    left = remaining_ms(start, req->timeout_ms);
    if (left <= 0) {
      errno = ETIME;
      st.error = 1;
      break;
    }
    n = st_recv(io->fd, rbuf, (int)sizeof(rbuf), 0, left);
    if (n < 0) {
      st.error = 1;
      break;
    }
    if (n == 0) {
      parsed = http_parser_execute(&parser, &settings, rbuf, 0);
      (void)parsed;
      break;
    }
    parsed = http_parser_execute(&parser, &settings, rbuf, (size_t)n);
    if (parsed != (size_t)n || HTTP_PARSER_ERRNO(&parser) != HPE_OK) {
      st.error = 1;
      break;
    }
  }

  resp->status = st.status;
  resp->keep_alive = st.keep_alive;
  resp->body = st.body;
  resp->body_len = st.body_len;
  resp->body_bytes = st.body_len;
  if (!st.complete || st.error) {
    int saved = errno;
    resp->transport_error = 1;
    st_http_conn_close(io);
    errno = saved;
    return -1;
  }
  if (!req->keepalive || !st.keep_alive) {
    st_http_conn_close(io);
  }
  return 0;
}
