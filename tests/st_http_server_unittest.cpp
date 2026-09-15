#include "app/st_c.h"
#include "src/st_server.h"
#include "tests/st_test_compat.h"
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

ST_NAMESPACE_USING

class HttpTestConn : public StServerConnection<HttpTestConn> {
public:
  virtual int32_t DoInput(void *buf, int32_t len) {
    if (buf == NULL || len <= 0) {
      return -1;
    }
    char *p = (char *)buf;
    for (int32_t i = 0; i + 3 < len; i++) {
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

static int http_check(void *buf, int len) {
  if (len >= 4 && memcmp(buf, "HTTP", 4) == 0) {
    /* need full headers+body; look for \r\n\r\n then body */
    char *p = (char *)buf;
    for (int i = 0; i + 3 < len; i++) {
      if (p[i] == '\r' && p[i + 1] == '\n' && p[i + 2] == '\r' &&
          p[i + 3] == '\n') {
        int body = len - (i + 4);
        if (body >= 2) {
          return len;
        }
        return 0;
      }
    }
    return 0;
  }
  if (len > 16) {
    return -1;
  }
  return 0;
}

TEST(StStatus, HttpServerOnce) {
  int port = 19301;
  pid_t pid = fork();
  ASSERT_TRUE(pid >= 0);
  if (pid == 0) {
    st_init_frame();
    st_set_hook_flag();
    StServer<HttpTestConn, eTCP_CONN> *server =
        new StServer<HttpTestConn, eTCP_CONN>();
    server->SetHookFlag();
    StNetAddr addr;
    addr.SetAddr("127.0.0.1", port);
    ASSERT_TRUE(server->CreateSocket(addr) >= 0);
    ASSERT_TRUE(server->Listen());
    alarm(4);
    server->Loop();
    _exit(0);
  }

  Util::USleep(200000);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  struct sockaddr_in dst;
  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = htons(port);
  dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  const char *req = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
  char recvbuf[512];
  int bufsize = sizeof(recvbuf);
  int rc = tcp_sendrecv(&dst, (void *)req, (int)strlen(req), recvbuf, bufsize,
                        2000, http_check, false);
  LOG_TRACE("http smoke rc=%d bufsize=%d", rc, bufsize);
  kill(pid, SIGTERM);
  int st = 0;
  waitpid(pid, &st, 0);
  ASSERT_TRUE(rc == 0);
  ASSERT_TRUE(bufsize >= 2);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
