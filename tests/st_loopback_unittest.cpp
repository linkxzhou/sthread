#include "app/st_c.h"
#include "src/st_connection.h"
#include "src/st_sys.h"
#include "stlib/st_netaddr.h"
#include "tests/st_test_compat.h"
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

ST_NAMESPACE_USING

static int tcp_check(void *buf, int len) {
  if (len >= 4 && memcmp(buf, "PONG", 4) == 0) {
    return 4;
  }
  if (len >= 4) {
    return -1;
  }
  return 0;
}

/* TCP 与 UDP 共享同一调度器，回归时保留此顺序。 */
TEST(StStatus, TcpLoopback) {
  int port = 19012;
  pid_t pid = fork();
  ASSERT_TRUE(pid >= 0);
  if (pid == 0) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(fd, 1);
    int c = accept(fd, NULL, NULL);
    if (c >= 0) {
      char buf[64];
      int n = recv(c, buf, sizeof(buf), 0);
      (void)n;
      send(c, "PONG", 4, 0);
      close(c);
    }
    close(fd);
    _exit(0);
  }

  Util::USleep(100000);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  struct sockaddr_in dst;
  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = htons(port);
  dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  char recvbuf[64];
  int bufsize = sizeof(recvbuf);
  int rc = tcp_sendrecv(&dst, (void *)"PING", 4, recvbuf, bufsize, 2000,
                        tcp_check, false);
  LOG_TRACE("tcp_sendrecv rc=%d bufsize=%d", rc, bufsize);
  int st;
  waitpid(pid, &st, 0);
  ASSERT_TRUE(rc == 0);
  ASSERT_TRUE(bufsize == 4 && memcmp(recvbuf, "PONG", 4) == 0);
}

TEST(StStatus, UdpLoopback) {
  int port = 19011;
  pid_t pid = fork();
  ASSERT_TRUE(pid >= 0);
  if (pid == 0) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    char buf[128];
    struct sockaddr_in from;
    socklen_t fl = sizeof(from);
    int n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
    if (n > 0) {
      sendto(fd, "PONG", 4, 0, (struct sockaddr *)&from, fl);
    }
    close(fd);
    _exit(0);
  }

  Util::USleep(100000);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  struct sockaddr_in dst;
  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = htons(port);
  dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  char recvbuf[64];
  int bufsize = sizeof(recvbuf);
  int rc = udp_sendrecv(&dst, (void *)"PING", 4, recvbuf, bufsize, 1000);
  LOG_TRACE("udp_sendrecv rc=%d bufsize=%d", rc, bufsize);
  int st;
  waitpid(pid, &st, 0);
  ASSERT_TRUE(rc == 0);
  ASSERT_TRUE(bufsize >= 4 && memcmp(recvbuf, "PONG", 4) == 0);
}

TEST(StStatus, UdpSequentialLoopback) {
  int port = 19013;
  pid_t pid = fork();
  ASSERT_TRUE(pid >= 0);
  if (pid == 0) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (fd < 0 || bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
      _exit(1);
    for (int i = 0; i < 4; i++) {
      char buf[16];
      struct sockaddr_in from;
      socklen_t fromlen = sizeof(from);
      int n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&from,
                       &fromlen);
      if (n != 4 || memcmp(buf, "PING", 4) != 0 ||
          sendto(fd, "PONG", 4, 0, (struct sockaddr *)&from, fromlen) != 4)
        _exit(2);
    }
    close(fd);
    _exit(0);
  }

  Util::USleep(100000);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  struct sockaddr_in dst;
  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = htons(port);
  dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  stlib::UtilPtrPool<StExecClientConnection> *pool =
      stlib::Instance<stlib::UtilPtrPool<StExecClientConnection> >();
  unsigned int initial = pool->Size();
  for (int i = 0; i < 4; i++) {
    char recvbuf[16];
    int bufsize = sizeof(recvbuf);
    int rc = udp_sendrecv(&dst, (void *)"PING", 4, recvbuf, bufsize, 500);
    ASSERT_TRUE(rc == 0);
    ASSERT_TRUE(bufsize == 4 && memcmp(recvbuf, "PONG", 4) == 0);
    ASSERT_TRUE(pool->Size() <= initial + 1);
  }
  int st = 0;
  waitpid(pid, &st, 0);
  ASSERT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
