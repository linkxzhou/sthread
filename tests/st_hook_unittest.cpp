/*
 * sys_* 在 hook 打开、且 fd 未标 O_NONBLOCK 时会进 st_*。
 * 若 LD_PRELOAD / DYLD_INSERT_LIBRARIES 挂了 dlsym_null_preload，
 * dlsym 返回 NULL，HOOK_SYSCALL 改走 libc 本体，调用仍然成功。
 */
#include "app/st_c.h"
#include "app/st_sys.h"
#include "src/st_sys.h"
#include "tests/st_test_compat.h"
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

ST_NAMESPACE_USING

static int bind_loopback(int type, int *port) {
  int fd;
  int yes = 1;
  struct sockaddr_in addr;
  socklen_t alen;
  fd = ::socket(AF_INET, type, 0);
  if (fd < 0) {
    return -1;
  }
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (::bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
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

TEST(StStatus, HookReadWriteSendRecv) {
  int sv[2];
  char buf[8];
  ssize_t n;
  ASSERT_TRUE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
  ASSERT_TRUE(::send(sv[1], "abcd", 4, 0) == 4);
  /* sys_socket 会顺手设 O_NONBLOCK，st_* 分支就走不到。这里只登记 fd。 */
  sys_new_fd(sv[0]);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  n = sys_read(sv[0], buf, 4);
  ASSERT_TRUE(n == 4);
  ASSERT_TRUE(memcmp(buf, "abcd", 4) == 0);
  n = sys_write(sv[0], "W", 1);
  ASSERT_TRUE(n == 1);
  memset(buf, 0, sizeof(buf));
  ASSERT_TRUE(::recv(sv[1], buf, 1, 0) == 1);
  ASSERT_TRUE(buf[0] == 'W');
  ASSERT_TRUE(::send(sv[1], "xyz", 3, 0) == 3);
  {
    /* st_recv 会先等可读。没登记事件时直接 -2，到不了内核 recv。 */
    StEventItem *item = Instance<UtilPtrPool<StEventItem> >()->AllocPtr();
    ASSERT_TRUE(item != NULL);
    item->SetOsfd(sv[0]);
    item->EnableInput();
    item->DisableOutput();
    ASSERT_TRUE(GlobalEventSchedule()->Add(item));
    n = sys_recv(sv[0], buf, 3, 0);
    GlobalEventSchedule()->ClearItem(item);
    UtilPtrPoolFree(item);
  }
  ASSERT_TRUE(n == 3);
  ASSERT_TRUE(memcmp(buf, "xyz", 3) == 0);
  n = sys_send(sv[0], "Q", 1, 0);
  ASSERT_TRUE(n == 1);
  /* 没登记的 fd 直接走真实 read，EBADF。 */
  n = sys_read(-1, buf, 1);
  ASSERT_TRUE(n < 0);
  sys_close(sv[0]);
  ::close(sv[1]);
}

TEST(StStatus, HookConnectAcceptAndTimeoutOpts) {
  int lfd;
  int port = 0;
  int raw;
  int cfd;
  int afd;
  int yes = 1;
  struct sockaddr_in dst;
  struct sockaddr_in bound;
  struct timeval tv;
  sys_fd *info;
  char ubuf[4];
  struct sockaddr_in from;
  socklen_t fl;
  int ufd;
  lfd = bind_loopback(SOCK_STREAM, &port);
  ASSERT_TRUE(lfd >= 0);
  ASSERT_TRUE(::listen(lfd, 1) == 0);
  /* 监听 fd 不在 sys 表里时 accept 走真实系统调用。 */
  cfd = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_TRUE(cfd >= 0);
  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = htons((uint16_t)port);
  dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_TRUE(::connect(cfd, (struct sockaddr *)&dst, sizeof(dst)) == 0);
  afd = sys_accept(lfd, NULL, NULL);
  ASSERT_TRUE(afd >= 0);
  ::close(cfd);
  sys_close(afd);
  ::close(lfd);

  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  raw = sys_socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_TRUE(raw >= 0);
  tv.tv_sec = 1;
  tv.tv_usec = 500000;
  ASSERT_TRUE(sys_setsockopt(raw, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) ==
              0);
  ASSERT_TRUE(sys_setsockopt(raw, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) ==
              0);
  info = sys_find_fd(raw);
  ASSERT_TRUE(info != NULL);
  ASSERT_TRUE(info->read_timeout == 1500);
  ASSERT_TRUE(info->write_timeout == 1500);
  ASSERT_TRUE(sys_fcntl(raw, F_GETFL, 0) >= 0);
  ASSERT_TRUE(sys_ioctl(raw, FIONBIO, &yes) == 0);
  ASSERT_TRUE(sys_shutdown(raw) == 0 || errno == ENOTCONN);
  /* 已连接前 shutdown 可能 ENOTCONN；两种都说明调用进了内核。 */
  sys_close(raw);

  /* 未注册事件的阻塞 UDP：st_recvfrom 得到 -2，hook 收成 libc 的 -1。 */
  ufd = ::socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_TRUE(ufd >= 0);
  sys_new_fd(ufd);
  fl = sizeof(from);
  errno = 0;
  ASSERT_TRUE(sys_recvfrom(ufd, ubuf, sizeof(ubuf), 0, (struct sockaddr *)&from,
                           &fl) == -1);
  ASSERT_TRUE(errno == EINVAL);
  lfd = bind_loopback(SOCK_DGRAM, &port);
  ASSERT_TRUE(lfd >= 0);
  memset(&bound, 0, sizeof(bound));
  bound.sin_family = AF_INET;
  bound.sin_port = htons((uint16_t)port);
  bound.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_TRUE(sys_sendto(ufd, "P", 1, 0, (struct sockaddr *)&bound,
                         sizeof(bound)) == 1);
  sys_close(ufd);
  ::close(lfd);
}

/* C 符号。macOS 上还要 Makefile 的
 * -Wl,-U,_st_dlsym_null_hits，否则弱引用也链不过。 */
#if defined(__APPLE__)
extern "C" int st_dlsym_null_hits __attribute__((weak_import));
#else
extern "C" int st_dlsym_null_hits __attribute__((weak));
#endif

TEST(StStatus, HookDlsymFallbackCounter) {
  /* 没预加载时弱符号地址是空。预加载时每次被拦下的 dlsym 都加一。
   * 不用 dlsym 取这个符号：macOS 的 interpose 会把查找指回自己并栈溢出。 */
  if (&st_dlsym_null_hits != 0) {
    ASSERT_TRUE(st_dlsym_null_hits > 0);
  }
}

TEST(StStatus, HookReadRecvTimeout) {
  int sv[2];
  char buf[8];
  ssize_t n;
  int flags;
  struct timeval tv;
  StEventItem *item;
  ASSERT_TRUE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
  flags = ::fcntl(sv[0], F_GETFL, 0);
  ASSERT_TRUE(::fcntl(sv[0], F_SETFL, flags | O_NONBLOCK) == 0);
  /* 内核非阻塞，但 fd 表不标 UNBLOCK，这样才会进 st_*。 */
  sys_new_fd(sv[0]);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  tv.tv_sec = 0;
  tv.tv_usec = 40000;
  ASSERT_TRUE(sys_setsockopt(sv[0], SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) ==
              0);
  item = Instance<UtilPtrPool<StEventItem> >()->AllocPtr();
  ASSERT_TRUE(item != NULL);
  item->SetOsfd(sv[0]);
  item->EnableInput();
  item->DisableOutput();
  ASSERT_TRUE(GlobalEventSchedule()->Add(item));
  errno = 0;
  n = sys_read(sv[0], buf, sizeof(buf));
  ASSERT_TRUE(n == -1);
  ASSERT_TRUE(errno == ETIME);
  errno = 0;
  n = sys_recv(sv[0], buf, sizeof(buf), 0);
  ASSERT_TRUE(n == -1);
  ASSERT_TRUE(errno == ETIME);
  GlobalEventSchedule()->ClearItem(item);
  UtilPtrPoolFree(item);
  sys_close(sv[0]);
  ::close(sv[1]);
}

TEST(StStatus, HookSendTimeout) {
  int sv[2];
  int sz = 1024;
  char junk[4096];
  char *big = NULL;
  const int big_n = 256 * 1024;
  int flags;
  struct timeval tv;
  StEventItem *item;
  ssize_t n;
  ASSERT_TRUE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
  flags = ::fcntl(sv[0], F_GETFL, 0);
  ASSERT_TRUE(::fcntl(sv[0], F_SETFL, flags | O_NONBLOCK) == 0);
  ASSERT_TRUE(::fcntl(sv[1], F_SETFL, flags | O_NONBLOCK) == 0);
  (void)::setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &sz, sizeof(sz));
  (void)::setsockopt(sv[1], SOL_SOCKET, SO_RCVBUF, &sz, sizeof(sz));
  memset(junk, 'x', sizeof(junk));
  for (;;) {
    ssize_t w = ::send(sv[0], junk, sizeof(junk), MSG_DONTWAIT);
    if (w < 0) {
      break;
    }
  }
  sys_new_fd(sv[0]);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  tv.tv_sec = 0;
  tv.tv_usec = 40000;
  ASSERT_TRUE(sys_setsockopt(sv[0], SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) ==
              0);
  item = Instance<UtilPtrPool<StEventItem> >()->AllocPtr();
  ASSERT_TRUE(item != NULL);
  item->SetOsfd(sv[0]);
  item->EnableOutput();
  item->DisableInput();
  ASSERT_TRUE(GlobalEventSchedule()->Add(item));
  big = (char *)malloc((size_t)big_n);
  ASSERT_TRUE(big != NULL);
  memset(big, 'y', (size_t)big_n);
  errno = 0;
  n = sys_send(sv[0], big, (size_t)big_n, 0);
  ASSERT_TRUE(n == -1);
  ASSERT_TRUE(errno == ETIME);
  free(big);
  GlobalEventSchedule()->ClearItem(item);
  UtilPtrPoolFree(item);
  sys_close(sv[0]);
  ::close(sv[1]);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  signal(SIGPIPE, SIG_IGN);
  return RUN_ALL_TESTS();
}
