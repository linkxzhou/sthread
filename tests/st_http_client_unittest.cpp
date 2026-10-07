/*
 * Copyright (C) zhoulv2000@163.com
 *
 * Content-Length（StServer）和 chunked（裸 socket）两条解析路径。
 */

#include "app/st_httpclient/http_client.h"
#include "src/st_server.h"
#include "tests/st_test_compat.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

ST_NAMESPACE_USING

class HttpTestConn : public StServerConnection<HttpTestConn> {
public:
  virtual int32_t DoInput(void *buf, int32_t len) {
    char *p;
    int32_t i;
    if (buf == NULL || len <= 0) {
      return -1;
    }
    p = (char *)buf;
    for (i = 0; i + 3 < len; i++) {
      if (p[i] == '\r' && p[i + 1] == '\n' && p[i + 2] == '\r' &&
          p[i + 3] == '\n') {
        return i + 4;
      }
    }
    return 0;
  }
  virtual int32_t DoOutput(void *buf, int32_t &len) {
    int n = snprintf((char *)buf, (size_t)len,
                     "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n"
                     "Connection: close\r\n\r\nok");
    if (n < 0 || n >= len) {
      return -1;
    }
    len = n;
    return 0;
  }
  virtual int32_t DoProcess() { return 0; }
  virtual int32_t DoError(int32_t err) {
    (void)err;
    return 0;
  }
};

static void fill_req(StHttpRequest *req, int port) {
  memset(req, 0, sizeof(*req));
  req->method = "GET";
  req->host = "127.0.0.1";
  req->port = port;
  req->path = "/";
  req->timeout_ms = 2000;
  req->ip_be = htonl(INADDR_LOOPBACK);
}

TEST(StStatus, HttpClientContentLength) {
  int port = 19321;
  pid_t pid = fork();
  StHttpRequest req;
  StHttpConn io;
  StHttpResponse resp;
  int rc;
  ASSERT_TRUE(pid >= 0);
  if (pid == 0) {
    st_init_frame();
    st_set_hook_flag();
    StServer<HttpTestConn, eTCP_CONN> *server =
        new StServer<HttpTestConn, eTCP_CONN>();
    server->SetHookFlag();
    StNetAddr addr;
    addr.SetAddr("127.0.0.1", port);
    if (server->CreateSocket(addr) < 0 || !server->Listen()) {
      _exit(2);
    }
    alarm(4);
    server->Loop();
    _exit(0);
  }

  Util::USleep(200000);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  fill_req(&req, port);
  st_http_conn_init(&io);
  memset(&resp, 0, sizeof(resp));
  rc = st_http_exchange(&req, &io, &resp);
  kill(pid, SIGTERM);
  waitpid(pid, NULL, 0);
  ASSERT_TRUE(rc == 0);
  ASSERT_TRUE(resp.status == 200);
  ASSERT_TRUE(resp.body_len == 2);
  ASSERT_TRUE(resp.body != NULL);
  ASSERT_TRUE(memcmp(resp.body, "ok", 2) == 0);
  st_http_response_free(&resp);
  st_http_conn_close(&io);
}

TEST(StStatus, HttpClientChunked) {
  int port = 19322;
  pid_t pid;
  int lfd = -1;
  StHttpRequest req;
  StHttpConn io;
  StHttpResponse resp;
  int rc;

  pid = fork();
  ASSERT_TRUE(pid >= 0);
  if (pid == 0) {
    struct sockaddr_in addr;
    int yes = 1;
    int c;
    char tmp[1024];
    int got = 0;
    const char *msg = "HTTP/1.1 200 OK\r\n"
                      "Transfer-Encoding: chunked\r\n"
                      "Connection: close\r\n"
                      "\r\n"
                      "5\r\nhello\r\n"
                      "0\r\n\r\n";
    lfd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(lfd, 1) != 0) {
      _exit(2);
    }
    alarm(4);
    c = accept(lfd, NULL, NULL);
    if (c < 0) {
      _exit(3);
    }
    while (got < (int)sizeof(tmp) - 1) {
      int n = (int)read(c, tmp + got, sizeof(tmp) - 1 - (size_t)got);
      if (n <= 0) {
        break;
      }
      got += n;
      tmp[got] = '\0';
      if (strstr(tmp, "\r\n\r\n") != NULL) {
        break;
      }
    }
    if (write(c, msg, strlen(msg)) < 0) {
      _exit(4);
    }
    close(c);
    close(lfd);
    _exit(0);
  }

  Util::USleep(200000);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  fill_req(&req, port);
  st_http_conn_init(&io);
  memset(&resp, 0, sizeof(resp));
  rc = st_http_exchange(&req, &io, &resp);
  kill(pid, SIGTERM);
  waitpid(pid, NULL, 0);
  ASSERT_TRUE(rc == 0);
  ASSERT_TRUE(resp.status == 200);
  ASSERT_TRUE(resp.body_len == 5);
  ASSERT_TRUE(resp.body != NULL);
  ASSERT_TRUE(memcmp(resp.body, "hello", 5) == 0);
  st_http_response_free(&resp);
  st_http_conn_close(&io);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  signal(SIGPIPE, SIG_IGN);
  return RUN_ALL_TESTS();
}
