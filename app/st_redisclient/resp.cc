/*
 * Copyright (C) zhoulv2000@163.com
 *
 * 增量 RESP 解析。第一次只拿到类型字节时返回 0，下一次把剩下的喂进来。
 */

#include "resp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int kRespMaxBulk = 1024 * 1024;
static const int kRespMaxArray = 32;
static const int kRespMaxDepth = 4;

void resp_parser_init(RespParser *p) {
  if (p == NULL) {
    return;
  }
  p->buf = NULL;
  p->len = 0;
  p->cap = 0;
}

void resp_parser_free(RespParser *p) {
  if (p == NULL) {
    return;
  }
  free(p->buf);
  p->buf = NULL;
  p->len = 0;
  p->cap = 0;
}

void resp_free(RespValue *v) {
  int i;
  if (v == NULL) {
    return;
  }
  if (v->elem != NULL) {
    for (i = 0; i < v->n; i++) {
      resp_free(&v->elem[i]);
    }
    free(v->elem);
  }
  free(v->buf);
  v->buf = NULL;
  v->elem = NULL;
  v->n = 0;
  v->len = 0;
  v->type = RESP_NONE;
  v->i = 0;
}

static int find_crlf(const char *s, int n) {
  int i;
  for (i = 0; i + 1 < n; i++) {
    if (s[i] == '\r' && s[i + 1] == '\n') {
      return i;
    }
  }
  return -1;
}

static int copy_buf(RespValue *out, const char *s, int n) {
  out->buf = (char *)malloc((size_t)n + 1);
  if (out->buf == NULL) {
    return -1;
  }
  if (n > 0) {
    memcpy(out->buf, s, (size_t)n);
  }
  out->buf[n] = '\0';
  out->len = n;
  return 0;
}

static int parse_one(const char *s, int n, RespValue *out, int *used,
                     int depth);

static int parse_line_value(const char *s, int n, RespValue *out, int *used,
                            int type) {
  int cr;
  if (n < 1) {
    return 0;
  }
  cr = find_crlf(s, n);
  if (cr < 0) {
    return 0;
  }
  out->type = type;
  if (type == RESP_INT) {
    out->i = strtoll(s + 1, NULL, 10);
  } else if (copy_buf(out, s + 1, cr - 1) != 0) {
    return -1;
  }
  *used = cr + 2;
  return 1;
}

static int parse_bulk(const char *s, int n, RespValue *out, int *used) {
  int cr;
  int blen;
  int body;
  if (n < 1) {
    return 0;
  }
  cr = find_crlf(s, n);
  if (cr < 0) {
    return 0;
  }
  blen = atoi(s + 1);
  if (blen == -1) {
    out->type = RESP_NULL;
    *used = cr + 2;
    return 1;
  }
  if (blen < 0 || blen > kRespMaxBulk) {
    return -1;
  }
  body = cr + 2;
  if (n < body + blen + 2) {
    return 0;
  }
  if (s[body + blen] != '\r' || s[body + blen + 1] != '\n') {
    return -1;
  }
  out->type = RESP_BULK;
  if (copy_buf(out, s + body, blen) != 0) {
    return -1;
  }
  *used = body + blen + 2;
  return 1;
}

static int parse_array(const char *s, int n, RespValue *out, int *used,
                       int depth) {
  int cr;
  int count;
  int pos;
  int i;
  if (depth > kRespMaxDepth) {
    return -1;
  }
  cr = find_crlf(s, n);
  if (cr < 0) {
    return 0;
  }
  count = atoi(s + 1);
  if (count < 0 || count > kRespMaxArray) {
    return -1;
  }
  out->type = RESP_ARRAY;
  out->n = count;
  if (count > 0) {
    out->elem = (RespValue *)calloc((size_t)count, sizeof(RespValue));
    if (out->elem == NULL) {
      return -1;
    }
  }
  pos = cr + 2;
  for (i = 0; i < count; i++) {
    int u = 0;
    int rc = parse_one(s + pos, n - pos, &out->elem[i], &u, depth + 1);
    if (rc != 1) {
      resp_free(out);
      return rc;
    }
    pos += u;
  }
  *used = pos;
  return 1;
}

static int parse_one(const char *s, int n, RespValue *out, int *used,
                     int depth) {
  int rc;
  memset(out, 0, sizeof(*out));
  *used = 0;
  if (n < 1) {
    return 0;
  }
  if (s[0] == '+' || s[0] == '-' || s[0] == ':') {
    int type = RESP_SIMPLE;
    if (s[0] == '-') {
      type = RESP_ERROR;
    } else if (s[0] == ':') {
      type = RESP_INT;
    }
    rc = parse_line_value(s, n, out, used, type);
  } else if (s[0] == '$') {
    rc = parse_bulk(s, n, out, used);
  } else if (s[0] == '*') {
    rc = parse_array(s, n, out, used, depth);
  } else {
    return -1;
  }
  if (rc != 1) {
    resp_free(out);
  }
  return rc;
}

static int parser_append(RespParser *p, const char *data, int n) {
  int cap;
  char *nb;
  if (n <= 0) {
    return 0;
  }
  cap = p->cap;
  if (cap <= 0) {
    cap = 256;
  }
  while (p->len + n + 1 > cap) {
    if (cap > 1024 * 1024) {
      return -1;
    }
    cap *= 2;
  }
  if (cap != p->cap) {
    nb = (char *)realloc(p->buf, (size_t)cap);
    if (nb == NULL) {
      return -1;
    }
    p->buf = nb;
    p->cap = cap;
  }
  memcpy(p->buf + p->len, data, (size_t)n);
  p->len += n;
  p->buf[p->len] = '\0';
  return 0;
}

int resp_parse(RespParser *p, const char *data, int n, RespValue *out) {
  int used = 0;
  int rc;
  if (p == NULL || out == NULL) {
    return -1;
  }
  memset(out, 0, sizeof(*out));
  if (data != NULL && n > 0) {
    if (parser_append(p, data, n) != 0) {
      return -1;
    }
  }
  if (p->len <= 0) {
    return 0;
  }
  rc = parse_one(p->buf, p->len, out, &used, 0);
  if (rc == 1) {
    memmove(p->buf, p->buf + used, (size_t)(p->len - used));
    p->len -= used;
    if (p->buf != NULL) {
      p->buf[p->len] = '\0';
    }
  }
  return rc;
}

int resp_encode(char *out, int cap, const char *const *argv, int argc) {
  int i;
  int used = 0;
  int n;
  if (out == NULL || cap <= 0 || argv == NULL || argc <= 0) {
    return -1;
  }
  n = snprintf(out, (size_t)cap, "*%d\r\n", argc);
  if (n < 0 || n >= cap) {
    return -1;
  }
  used = n;
  for (i = 0; i < argc; i++) {
    int alen;
    if (argv[i] == NULL) {
      return -1;
    }
    alen = (int)strlen(argv[i]);
    n = snprintf(out + used, (size_t)(cap - used), "$%d\r\n", alen);
    if (n < 0 || used + n >= cap) {
      return -1;
    }
    used += n;
    if (used + alen + 2 >= cap) {
      return -1;
    }
    memcpy(out + used, argv[i], (size_t)alen);
    used += alen;
    out[used++] = '\r';
    out[used++] = '\n';
    out[used] = '\0';
  }
  return used;
}
