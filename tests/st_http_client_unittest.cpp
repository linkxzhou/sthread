/*
 * Copyright (C) zhoulv2000@163.com
 *
 * Content-Length（StServer）和 chunked（裸 socket）两条解析路径。
 */

#include "app/st_httpclient/http_client.h"
#include "src/st_server.h"
#include "tests/st_test_compat.h"
#include <arpa/inet.h>
#include <errno.h>
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

static int read_port(int fd, int *port) {
  unsigned char b[2];
  int got = 0;
  while (got < 2) {
    ssize_t n = ::read(fd, b + got, (size_t)(2 - got));
    if (n <= 0) {
      return -1;
    }
    got += (int)n;
  }
  *port = ((int)b[0] << 8) | (int)b[1];
  return 0;
}

static void write_port(int fd, int port) {
  unsigned char b[2];
  b[0] = (unsigned char)((port >> 8) & 0xff);
  b[1] = (unsigned char)(port & 0xff);
  if (::write(fd, b, 2) != 2) {
    _exit(1);
  }
}

/* nreq 次应答。same_conn 为 1 时复用同一个已接受的连接。 */
static pid_t fork_http(const char *resp, int nreq, int need_body, int same_conn,
                       int *port) {
  int sp[2];
  pid_t pid;
  if (pipe(sp) != 0) {
    return -1;
  }
  pid = fork();
  if (pid < 0) {
    return -1;
  }
  if (pid == 0) {
    int lfd;
    int yes = 1;
    int p = 0;
    int i;
    struct sockaddr_in addr;
    socklen_t alen;
    close(sp[0]);
    alarm(4);
    lfd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(lfd, 1) != 0) {
      _exit(2);
    }
    alen = sizeof(addr);
    if (getsockname(lfd, (struct sockaddr *)&addr, &alen) != 0) {
      _exit(2);
    }
    p = ntohs(addr.sin_port);
    write_port(sp[1], p);
    close(sp[1]);
    {
      int c = -1;
      for (i = 0; i < nreq; i++) {
        char tmp[2048];
        int got = 0;
        if (!same_conn || c < 0) {
          c = accept(lfd, NULL, NULL);
          if (c < 0) {
            _exit(3);
          }
        }
        while (got < (int)sizeof(tmp) - 1) {
          int n = (int)read(c, tmp + got, sizeof(tmp) - 1 - (size_t)got);
          if (n <= 0) {
            break;
          }
          got += n;
          tmp[got] = '\0';
          if (strstr(tmp, "\r\n\r\n") != NULL &&
              (!need_body || strstr(tmp, "a=1") != NULL)) {
            break;
          }
        }
        if (need_body &&
            (strstr(tmp, "POST") == NULL || strstr(tmp, "a=1") == NULL)) {
          _exit(4);
        }
        if (resp != NULL && strlen(resp) > 0) {
          if (write(c, resp, strlen(resp)) < 0) {
            _exit(5);
          }
        }
        if (!same_conn) {
          close(c);
          c = -1;
        }
      }
      if (c >= 0) {
        close(c);
      }
    }
    close(lfd);
    _exit(0);
  }
  close(sp[1]);
  if (read_port(sp[0], port) != 0) {
    close(sp[0]);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
    return -1;
  }
  close(sp[0]);
  return pid;
}

TEST(StStatus, HttpNullArgs) {
  StHttpConn io;
  StHttpResponse resp;
  StHttpRequest req;
  ASSERT_TRUE(st_http_exchange(NULL, &io, &resp) < 0);
  ASSERT_TRUE(st_http_exchange(&req, NULL, &resp) < 0);
  ASSERT_TRUE(st_http_exchange(&req, &io, NULL) < 0);
  st_http_conn_init(NULL);
  st_http_conn_close(NULL);
  st_http_response_free(NULL);
  st_http_conn_init(&io);
  st_http_conn_close(&io);
}

TEST(StStatus, HttpMalformed) {
  int port = 0;
  pid_t pid = fork_http("this is not http\r\n\r\n", 1, 0, 0, &port);
  StHttpRequest req;
  StHttpConn io;
  StHttpResponse resp;
  int rc;
  ASSERT_TRUE(pid > 0);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  fill_req(&req, port);
  st_http_conn_init(&io);
  memset(&resp, 0, sizeof(resp));
  rc = st_http_exchange(&req, &io, &resp);
  kill(pid, SIGTERM);
  waitpid(pid, NULL, 0);
  ASSERT_TRUE(rc < 0);
  ASSERT_TRUE(resp.transport_error == 1);
  st_http_response_free(&resp);
  st_http_conn_close(&io);
}

TEST(StStatus, HttpShortContentLength) {
  const char *msg = "HTTP/1.1 200 OK\r\nContent-Length: 20\r\n"
                    "Connection: close\r\n\r\nhi";
  int port = 0;
  pid_t pid = fork_http(msg, 1, 0, 0, &port);
  StHttpRequest req;
  StHttpConn io;
  StHttpResponse resp;
  int rc;
  ASSERT_TRUE(pid > 0);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  fill_req(&req, port);
  req.timeout_ms = 300;
  st_http_conn_init(&io);
  memset(&resp, 0, sizeof(resp));
  rc = st_http_exchange(&req, &io, &resp);
  kill(pid, SIGTERM);
  waitpid(pid, NULL, 0);
  ASSERT_TRUE(rc < 0);
  ASSERT_TRUE(resp.transport_error == 1);
  st_http_response_free(&resp);
  st_http_conn_close(&io);
}

TEST(StStatus, HttpBadChunk) {
  const char *msg = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n"
                    "Connection: close\r\n\r\nzzzz\r\n";
  int port = 0;
  pid_t pid = fork_http(msg, 1, 0, 0, &port);
  StHttpRequest req;
  StHttpConn io;
  StHttpResponse resp;
  int rc;
  ASSERT_TRUE(pid > 0);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  fill_req(&req, port);
  st_http_conn_init(&io);
  memset(&resp, 0, sizeof(resp));
  rc = st_http_exchange(&req, &io, &resp);
  kill(pid, SIGTERM);
  waitpid(pid, NULL, 0);
  ASSERT_TRUE(rc < 0);
  st_http_response_free(&resp);
  st_http_conn_close(&io);
}

TEST(StStatus, HttpPostAndHeaders) {
  const char *msg = "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n"
                    "Connection: close\r\n\r\n";
  int port = 0;
  pid_t pid;
  StHttpRequest req;
  StHttpConn io;
  StHttpResponse resp;
  StHttpHeader hs[3];
  int rc;
  int st = 0;
  pid = fork_http(msg, 1, 1, 0, &port);
  ASSERT_TRUE(pid > 0);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  fill_req(&req, port);
  req.method = "POST";
  req.body = "a=1";
  req.body_len = 3;
  hs[0].text = "X-Test: 1";
  hs[1].text = NULL;
  hs[2].text = "Accept: text/plain";
  req.headers = hs;
  req.header_count = 3;
  st_http_conn_init(&io);
  memset(&resp, 0, sizeof(resp));
  rc = st_http_exchange(&req, &io, &resp);
  waitpid(pid, &st, 0);
  ASSERT_TRUE(rc == 0);
  ASSERT_TRUE(resp.status == 204);
  ASSERT_TRUE(resp.body_len == 0);
  ASSERT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);
  st_http_response_free(&resp);
  st_http_conn_close(&io);
}

TEST(StStatus, HttpDefaultsVerboseAndLongHeader) {
  char msg[512];
  char name[181];
  int port = 0;
  pid_t pid;
  StHttpRequest req;
  StHttpConn io;
  StHttpResponse resp;
  int rc;
  memset(name, 'H', 180);
  name[180] = '\0';
  snprintf(msg, sizeof(msg),
           "HTTP/1.1 200 OK\r\n%s: v\r\nContent-Length: 1\r\n"
           "Connection: close\r\n\r\nZ",
           name);
  pid = fork_http(msg, 1, 0, 0, &port);
  ASSERT_TRUE(pid > 0);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  fill_req(&req, port);
  req.method = NULL;
  req.path = NULL;
  req.verbose = 1;
  st_http_conn_init(&io);
  memset(&resp, 0, sizeof(resp));
  rc = st_http_exchange(&req, &io, &resp);
  kill(pid, SIGTERM);
  waitpid(pid, NULL, 0);
  ASSERT_TRUE(rc == 0);
  ASSERT_TRUE(resp.status == 200);
  ASSERT_TRUE(resp.body_len == 1);
  ASSERT_TRUE(resp.body != NULL && resp.body[0] == 'Z');
  st_http_response_free(&resp);
  st_http_conn_close(&io);
}

TEST(StStatus, HttpKeepaliveReuse) {
  const char *msg = "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n"
                    "Connection: keep-alive\r\n\r\nA";
  int port = 0;
  pid_t pid = fork_http(msg, 2, 0, 1, &port);
  StHttpRequest req;
  StHttpConn io;
  StHttpResponse resp;
  int rc;
  int st = 0;
  ASSERT_TRUE(pid > 0);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  fill_req(&req, port);
  req.keepalive = 1;
  st_http_conn_init(&io);
  memset(&resp, 0, sizeof(resp));
  rc = st_http_exchange(&req, &io, &resp);
  ASSERT_TRUE(rc == 0);
  ASSERT_TRUE(resp.status == 200);
  ASSERT_TRUE(resp.keep_alive == 1);
  ASSERT_TRUE(io.conn != NULL);
  st_http_response_free(&resp);
  memset(&resp, 0, sizeof(resp));
  rc = st_http_exchange(&req, &io, &resp);
  waitpid(pid, &st, 0);
  ASSERT_TRUE(rc == 0);
  ASSERT_TRUE(resp.body_len == 1);
  ASSERT_TRUE(resp.body != NULL && resp.body[0] == 'A');
  ASSERT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);
  st_http_response_free(&resp);
  st_http_conn_close(&io);
}

TEST(StStatus, HttpRecvTimeout) {
  int sp[2];
  int port = 0;
  pid_t pid;
  StHttpRequest req;
  StHttpConn io;
  StHttpResponse resp;
  int rc;
  int err;
  ASSERT_TRUE(pipe(sp) == 0);
  pid = fork();
  ASSERT_TRUE(pid >= 0);
  if (pid == 0) {
    int lfd;
    int yes = 1;
    int p = 0;
    int c;
    char tmp[1024];
    struct sockaddr_in addr;
    socklen_t alen;
    close(sp[0]);
    alarm(3);
    lfd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(lfd, 1) != 0) {
      _exit(2);
    }
    alen = sizeof(addr);
    if (getsockname(lfd, (struct sockaddr *)&addr, &alen) != 0) {
      _exit(2);
    }
    p = ntohs(addr.sin_port);
    write_port(sp[1], p);
    close(sp[1]);
    c = accept(lfd, NULL, NULL);
    if (c >= 0) {
      (void)read(c, tmp, sizeof(tmp));
      sleep(2);
      close(c);
    }
    close(lfd);
    _exit(0);
  }
  close(sp[1]);
  ASSERT_TRUE(read_port(sp[0], &port) == 0);
  close(sp[0]);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  fill_req(&req, port);
  req.timeout_ms = 80;
  st_http_conn_init(&io);
  memset(&resp, 0, sizeof(resp));
  errno = 0;
  rc = st_http_exchange(&req, &io, &resp);
  err = errno;
  kill(pid, SIGTERM);
  waitpid(pid, NULL, 0);
  ASSERT_TRUE(rc < 0);
  ASSERT_TRUE(resp.transport_error == 1);
  ASSERT_TRUE(err == ETIME);
  st_http_response_free(&resp);
  st_http_conn_close(&io);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  signal(SIGPIPE, SIG_IGN);
  return RUN_ALL_TESTS();
}
