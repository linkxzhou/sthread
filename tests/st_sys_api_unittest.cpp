#include "app/st_c.h"
#include "src/st_connection.h"
#include "src/st_sys.h"
#include "src/st_thread.h"
#include "stlib/st_netaddr.h"
#include "stlib/st_util.h"
#include "tests/st_test_compat.h"
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

ST_NAMESPACE_USING

TEST(StStatus, SysApiTcpReadWriteSendRecv) {
  int port = 19101;
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
      recv(c, buf, sizeof(buf), 0);
      send(c, "ABCD", 4, 0);
      close(c);
    }
    close(fd);
    _exit(0);
  }

  Util::USleep(100000);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();

  StClientConnection<StEventItem> *conn = new StClientConnection<StEventItem>();
  conn->SetConnType(eTCP_CONN);
  conn->SetTimeout(2000);
  StNetAddr addr;
  addr.SetAddr("127.0.0.1", port);
  int fd = conn->Create(addr);
  ASSERT_TRUE(fd >= 0);

  ASSERT_TRUE(st_write(fd, "PING", 4, 2000) == 4);

  char rbuf[32];
  memset(rbuf, 0, sizeof(rbuf));
  ASSERT_TRUE(st_read(fd, rbuf, sizeof(rbuf), 2000) == 4);
  ASSERT_TRUE(memcmp(rbuf, "ABCD", 4) == 0);
  st_sleep(1);

  conn->Close();
  delete conn;
  int st = 0;
  waitpid(pid, &st, 0);
}

TEST(StStatus, SysApiTcpSendRecv) {
  int port = 19102;
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
      recv(c, buf, sizeof(buf), 0);
      send(c, "SROK", 4, 0);
      Util::USleep(30000);
      close(c);
    }
    close(fd);
    _exit(0);
  }

  Util::USleep(100000);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();

  StClientConnection<StEventItem> *conn = new StClientConnection<StEventItem>();
  conn->SetConnType(eTCP_CONN);
  conn->SetTimeout(2000);
  StNetAddr addr;
  addr.SetAddr("127.0.0.1", port);
  int fd = conn->Create(addr);
  ASSERT_TRUE(fd >= 0);

  ASSERT_TRUE(st_send(fd, "PING", 4, 0, 2000) == 4);
  char rbuf[32];
  memset(rbuf, 0, sizeof(rbuf));
  ASSERT_TRUE(st_recv(fd, rbuf, sizeof(rbuf), 0, 2000) == 4);
  ASSERT_TRUE(memcmp(rbuf, "SROK", 4) == 0);

  conn->Close();
  delete conn;
  int st = 0;
  waitpid(pid, &st, 0);
}

TEST(StStatus, SysApiUdpAndErrors) {
  int port = 19103;
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
      sendto(fd, "UPOK", 4, 0, (struct sockaddr *)&from, fl);
    }
    close(fd);
    _exit(0);
  }

  Util::USleep(100000);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();

  StClientConnection<StEventItem> *conn = new StClientConnection<StEventItem>();
  conn->SetConnType(eUDP_CONN);
  conn->SetTimeout(2000);
  StNetAddr addr;
  addr.SetAddr("127.0.0.1", port);
  int fd = conn->Create(addr);
  ASSERT_TRUE(fd >= 0);

  struct sockaddr_in dst;
  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = htons(port);
  dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_TRUE(st_sendto(fd, "PING", 4, 0, (struct sockaddr *)&dst, sizeof(dst),
                        2000) == 4);
  char rbuf[32];
  struct sockaddr_in from;
  socklen_t fl = sizeof(from);
  memset(rbuf, 0, sizeof(rbuf));
  ASSERT_TRUE(st_recvfrom(fd, rbuf, sizeof(rbuf), 0, (struct sockaddr *)&from,
                          &fl, 2000) == 4);
  ASSERT_TRUE(memcmp(rbuf, "UPOK", 4) == 0);

  /* timeout path */
  int to = st_recvfrom(fd, rbuf, sizeof(rbuf), 0, (struct sockaddr *)&from, &fl,
                       5);
  ASSERT_TRUE(to == -1 && errno == ETIME);

  /* item NULL path: nonblocking pipe with no StEventItem */
  int pfd[2];
  ASSERT_TRUE(pipe(pfd) == 0);
  int pfl = fcntl(pfd[0], F_GETFL, 0);
  fcntl(pfd[0], F_SETFL, pfl | O_NONBLOCK);
  int miss = st_read(pfd[0], rbuf, sizeof(rbuf), 10);
  ASSERT_TRUE(miss == -2);
  close(pfd[0]);
  close(pfd[1]);

  conn->Close();
  delete conn;
  int st = 0;
  waitpid(pid, &st, 0);
}

TEST(StStatus, SysApiAccept) {
  int port = 19104;
  pid_t pid = fork();
  ASSERT_TRUE(pid >= 0);
  if (pid == 0) {
    Util::USleep(150000);
    int c = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(port);
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    connect(c, (struct sockaddr *)&dst, sizeof(dst));
    Util::USleep(50000);
    close(c);
    _exit(0);
  }

  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();

  int lfd = sys_socket(AF_INET, SOCK_STREAM, 0);
  int yes = 1;
  setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_TRUE(bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
  ASSERT_TRUE(listen(lfd, 1) == 0);

  StEventItem *item = Instance<UtilPtrPool<StEventItem> >()->AllocPtr();
  item->SetOsfd(lfd);
  item->EnableInput();
  GlobalEventSchedule()->Add(item);

  struct sockaddr_in peer;
  socklen_t pl = sizeof(peer);
  int cfd = st_accept(lfd, (struct sockaddr *)&peer, &pl);
  ASSERT_TRUE(cfd >= 0);
  close(cfd);

  GlobalEventSchedule()->ClearItem(item);
  UtilPtrPoolFree(item);
  sys_close(lfd);
  int st = 0;
  waitpid(pid, &st, 0);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
