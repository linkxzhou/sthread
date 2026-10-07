/*
 * Copyright (C) zhoulv2000@163.com
 *
 * 同步写法的 HTTP/1.1 单次交换。样例用，不是通用客户端（无 TLS）。
 */

#ifndef _ST_HTTP_CLIENT_H_
#define _ST_HTTP_CLIENT_H_

#include "app/st_c.h"

struct StHttpHeader {
  const char *text; /* "Name: value"，不含 \r\n */
};

struct StHttpRequest {
  const char *method; /* "GET" 或 "POST" */
  const char *host;
  int port;
  const char *path; /* 以 / 开头，可含 ?query */
  const char *body;
  int body_len;
  int timeout_ms;
  int keepalive; /* 1：本协程内尽量复用 io->conn */
  int verbose;
  const StHttpHeader *headers;
  int header_count;
  unsigned int ip_be; /* IPv4，网络序 */
};

struct StHttpResponse {
  int status;          /* 没解析到状态行时为 0 */
  int transport_error; /* 1：没收到完整报文 */
  int keep_alive;      /* 服务端允许复用 */
  long long body_bytes;
  char *body; /* malloc，可为 NULL；调用方 st_http_response_free */
  int body_len;
};

struct StHttpConn {
  StExecClientConnection *conn;
  int fd;
};

void st_http_conn_init(StHttpConn *io);
void st_http_conn_close(StHttpConn *io);
void st_http_response_free(StHttpResponse *resp);

/* 0：收到完整响应（状态码在 resp->status）。-1：连接/读写/解析失败。 */
int st_http_exchange(const StHttpRequest *req, StHttpConn *io,
                     StHttpResponse *resp);

#endif /* _ST_HTTP_CLIENT_H_ */
