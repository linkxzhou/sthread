/*
 * Copyright (C) zhoulv2000@163.com
 *
 * loopback 上 open 与 refused。Create 失败后 errno 仍是 ECONNREFUSED，
 * 不能被 ReleaseItem / Close 盖成别的值。两种都不是 ETIME。
 */

#include "app/st_c.h"
#include "src/st_sys.h"
#include "stlib/st_netaddr.h"
#include "tests/st_test_compat.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace sthread;
using namespace stlib;

static int listen_port(int *port) {
  int fd;
  int yes = 1;
  struct sockaddr_in addr;
  socklen_t alen;
  fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (::bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
      ::listen(fd, 1) != 0) {
    ::close(fd);
    return -1;
  }
  alen = sizeof(addr);
  if (::getsockname(fd, (struct sockaddr *)&addr, &alen) != 0) {
    ::close(fd);
    return -1;
  }
  *port = ntohs(addr.sin_port);
  return fd;
}

TEST(StStatus, ConnectOpenAndRefusedErrno) {
  int lfd;
  int port = 0;
  int fd;
  int flags;
  StEventItem *item;
  struct sockaddr_in dst;
  int rc;
  int err;
  StExecClientConnection *conn;
  StNetAddr addr;
  int closed;

  lfd = listen_port(&port);
  ASSERT_TRUE(lfd >= 0);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();

  fd = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_TRUE(fd >= 0);
  flags = ::fcntl(fd, F_GETFL, 0);
  ASSERT_TRUE(::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
  item = Instance<UtilPtrPool<StEventItem> >()->AllocPtr();
  ASSERT_TRUE(item != NULL);
  item->SetOsfd(fd);
  item->EnableOutput();
  item->DisableInput();
  ASSERT_TRUE(GlobalEventSchedule()->Add(item));
  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = htons((uint16_t)port);
  dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  errno = 0;
  rc = st_connect(fd, (struct sockaddr *)&dst, (int)sizeof(dst), 500);
  err = errno;
  GlobalEventSchedule()->ClearItem(item);
  UtilPtrPoolFree(item);
  ::close(fd);
  ASSERT_TRUE(rc == 0);
  ASSERT_TRUE(err != ETIME);

  ::close(lfd);
  /* 刚关掉的端口：拒绝，不是超时，也不是成功。 */
  closed = port;
  fd = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_TRUE(fd >= 0);
  flags = ::fcntl(fd, F_GETFL, 0);
  ASSERT_TRUE(::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
  item = Instance<UtilPtrPool<StEventItem> >()->AllocPtr();
  ASSERT_TRUE(item != NULL);
  item->SetOsfd(fd);
  item->EnableOutput();
  ASSERT_TRUE(GlobalEventSchedule()->Add(item));
  dst.sin_port = htons((uint16_t)closed);
  errno = 0;
  rc = st_connect(fd, (struct sockaddr *)&dst, (int)sizeof(dst), 500);
  err = errno;
  GlobalEventSchedule()->ClearItem(item);
  UtilPtrPoolFree(item);
  ::close(fd);
  ASSERT_TRUE(rc < 0);
  ASSERT_TRUE(err == ECONNREFUSED);
  ASSERT_TRUE(err != ETIME);

  lfd = listen_port(&port);
  ASSERT_TRUE(lfd >= 0);
  conn = Instance<StConnectionManager<StExecClientConnection> >()->AllocPtr(
      eTCP_CONN);
  ASSERT_TRUE(conn != NULL);
  conn->SetTimeout(500);
  addr.SetAddr("127.0.0.1", (uint16_t)port);
  errno = 0;
  rc = conn->Create(addr);
  Instance<StConnectionManager<StExecClientConnection> >()->FreePtr(conn);
  ASSERT_TRUE(rc >= 0);
  ::close(lfd);

  conn = Instance<StConnectionManager<StExecClientConnection> >()->AllocPtr(
      eTCP_CONN);
  ASSERT_TRUE(conn != NULL);
  conn->SetTimeout(300);
  addr.SetAddr("127.0.0.1", (uint16_t)port);
  errno = 0;
  rc = conn->Create(addr);
  err = errno;
  Instance<StConnectionManager<StExecClientConnection> >()->FreePtr(conn);
  ASSERT_TRUE(rc == -2);
  ASSERT_TRUE(err == ECONNREFUSED);
  ASSERT_TRUE(err != ETIME);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  signal(SIGPIPE, SIG_IGN);
  return RUN_ALL_TESTS();
}
