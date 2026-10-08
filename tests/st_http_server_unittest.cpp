#include "app/st_c.h"
#include "src/st_server.h"
#include "stlib/st_log.h"
#include "tests/st_test_compat.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
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

static long pid_vmsize_kb(pid_t pid) {
  char path[64];
  FILE *fp;
  char line[256];
  long kb;

  kb = -1;
  snprintf(path, sizeof(path), "/proc/%d/status", (int)pid);
  fp = fopen(path, "r");
  if (fp == NULL) {
    return -1;
  }
  while (fgets(line, sizeof(line), fp) != NULL) {
    if (strncmp(line, "VmSize:", 7) == 0) {
      kb = atol(line + 7);
      break;
    }
  }
  fclose(fp);
  return kb;
}

/* plan/12：5 万短连接之后服务端 VmSize 增量必须小于 64MB。
 * 不是 QPS 门禁。非 Linux 没有 /proc，跳过。 */
TEST(StStatus, HttpServerVmSizeAfter50k) {
#if !defined(__linux__)
  return;
#else
  const int nreq = 50000;
  const char *req = "GET / HTTP/1.1\r\nHost: localhost\r\n"
                    "Connection: close\r\n\r\n";
  int port = 19321;
  pid_t pid;
  long vm0;
  long vm1;
  int i;
  int ok;
  struct sockaddr_in dst;

  signal(SIGPIPE, SIG_IGN);
  pid = fork();
  ASSERT_TRUE(pid >= 0);
  if (pid == 0) {
    LOG_LEVEL(LLOG_ERR);
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
    alarm(180);
    server->Loop();
    _exit(0);
  }

  usleep(200000);
  vm0 = pid_vmsize_kb(pid);
  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = htons((uint16_t)port);
  dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ok = 0;
  for (i = 0; i < nreq; i++) {
    int fd;
    char buf[512];
    int n;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
      break;
    }
    if (connect(fd, (struct sockaddr *)&dst, sizeof(dst)) != 0) {
      close(fd);
      break;
    }
    if (send(fd, req, strlen(req), 0) < 0) {
      close(fd);
      break;
    }
    n = recv(fd, buf, sizeof(buf), 0);
    close(fd);
    if (n <= 0) {
      break;
    }
    ok++;
  }
  vm1 = pid_vmsize_kb(pid);
  kill(pid, SIGTERM);
  waitpid(pid, NULL, 0);
  ASSERT_TRUE(ok == nreq);
  ASSERT_TRUE(vm0 > 0 && vm1 > 0);
  ASSERT_TRUE(vm1 - vm0 < 64 * 1024);
#endif
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
