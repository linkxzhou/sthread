/*
 * st_* / tcp_sendrecv / udp_sendrecv 的失败与边界。
 * 只走 127.0.0.1 上 bind(port 0) 的端口，不访问外网。
 */
#include "app/st_c.h"
#include "app/st_sys.h"
#include "src/st_connection.h"
#include "src/st_sys.h"
#include "tests/st_test_compat.h"
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

ST_NAMESPACE_USING

static int g_slot = 1;

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

static void fill_dst(struct sockaddr_in *dst, int port) {
  memset(dst, 0, sizeof(*dst));
  dst->sin_family = AF_INET;
  dst->sin_port = htons((uint16_t)port);
  dst->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
}

static StEventItem *watch_fd(int fd, int readable) {
  StEventItem *item = Instance<UtilPtrPool<StEventItem> >()->AllocPtr();
  if (item == NULL) {
    return NULL;
  }
  item->SetOsfd(fd);
  if (readable) {
    item->EnableInput();
    item->DisableOutput();
  } else {
    item->EnableOutput();
    item->DisableInput();
  }
  if (!GlobalEventSchedule()->Add(item)) {
    UtilPtrPoolFree(item);
    return NULL;
  }
  return item;
}

static void drop_item(StEventItem *item) {
  if (item == NULL) {
    return;
  }
  GlobalEventSchedule()->ClearItem(item);
  UtilPtrPoolFree(item);
}

static int set_nonblock(int fd) {
  int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return -1;
  }
  return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int reject_pkg(void *buf, int len) {
  (void)buf;
  (void)len;
  return -1;
}

static int need_more(void *buf, int len) {
  (void)buf;
  (void)len;
  return 0;
}

static int slow_need_more(void *buf, int len) {
  (void)buf;
  (void)len;
  Util::USleep(80000);
  return 0;
}

static int take_pong(void *buf, int len) {
  if (len >= 4 && memcmp(buf, "PONG", 4) == 0) {
    return 4;
  }
  if (len >= 4) {
    return -1;
  }
  return 0;
}

TEST(StStatus, PrivateBeforeInit) {
  /* 活动协程还不存在时必须返回，不能 abort。 */
  st_set_private(&g_slot);
  ASSERT_TRUE(st_get_private() == NULL);
}

TEST(StStatus, PrivateAfterInit) {
  int local = 42;
  ASSERT_TRUE(st_init_frame());
  st_set_private(&local);
  ASSERT_TRUE(st_get_private() == &local);
  st_set_private(NULL);
  ASSERT_TRUE(st_get_private() == NULL);
}

TEST(StStatus, SendRecvNullArgs) {
  struct sockaddr_in dst;
  char pkg[4];
  char buf[4];
  int bufsize = 4;
  fill_dst(&dst, 1);
  ASSERT_TRUE(udp_sendrecv(NULL, pkg, 4, buf, bufsize, 50) == -1);
  ASSERT_TRUE(udp_sendrecv(&dst, NULL, 4, buf, bufsize, 50) == -1);
  ASSERT_TRUE(udp_sendrecv(&dst, pkg, 4, NULL, bufsize, 50) == -1);
  bufsize = 0;
  ASSERT_TRUE(udp_sendrecv(&dst, pkg, 4, buf, bufsize, 50) == -1);
  bufsize = 4;
  ASSERT_TRUE(tcp_sendrecv(NULL, pkg, 4, buf, bufsize, 50, take_pong, false) ==
              -10);
  ASSERT_TRUE(tcp_sendrecv(&dst, pkg, 4, buf, bufsize, 50, NULL, false) == -10);
  bufsize = 0;
  ASSERT_TRUE(tcp_sendrecv(&dst, pkg, 4, buf, bufsize, 50, take_pong, false) ==
              -10);
}

TEST(StStatus, ConnectRefused) {
  int lfd;
  int port = 0;
  int fd;
  struct sockaddr_in dst;
  int rc;
  int err;
  StEventItem *item;
  /* 只 bind 不 listen。非阻塞 connect 常常先 EINPROGRESS，完成后再是 RST。 */
  lfd = bind_loopback(SOCK_STREAM, &port);
  ASSERT_TRUE(lfd >= 0);
  ASSERT_TRUE(st_init_frame());
  fd = sys_socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_TRUE(fd >= 0);
  item = watch_fd(fd, 0);
  ASSERT_TRUE(item != NULL);
  fill_dst(&dst, port);
  rc = st_connect(fd, (struct sockaddr *)&dst, (int)sizeof(dst), 200);
  err = errno;
  drop_item(item);
  sys_close(fd);
  ::close(lfd);
  ASSERT_TRUE(rc < 0);
  ASSERT_TRUE(err == ECONNREFUSED);
}

TEST(StStatus, ConnectAlreadyConnected) {
  int sp[2];
  int port = 0;
  pid_t pid;
  int rc1 = -1;
  int rc2 = -1;
  int fd = -1;
  StEventItem *item = NULL;
  struct sockaddr_in dst;
  ASSERT_TRUE(pipe(sp) == 0);
  pid = fork();
  ASSERT_TRUE(pid >= 0);
  if (pid == 0) {
    int lfd;
    int c;
    char tmp;
    close(sp[0]);
    alarm(3);
    lfd = bind_loopback(SOCK_STREAM, &port);
    if (lfd < 0 || ::listen(lfd, 1) != 0) {
      _exit(2);
    }
    write_port(sp[1], port);
    close(sp[1]);
    c = ::accept(lfd, NULL, NULL);
    if (c >= 0) {
      (void)::recv(c, &tmp, 1, 0);
      ::close(c);
    }
    ::close(lfd);
    _exit(0);
  }
  close(sp[1]);
  ASSERT_TRUE(read_port(sp[0], &port) == 0);
  close(sp[0]);
  ASSERT_TRUE(st_init_frame());
  fd = sys_socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_TRUE(fd >= 0);
  item = watch_fd(fd, 0);
  fill_dst(&dst, port);
  if (item != NULL) {
    rc1 = st_connect(fd, (struct sockaddr *)&dst, (int)sizeof(dst), 500);
    rc2 = st_connect(fd, (struct sockaddr *)&dst, (int)sizeof(dst), 500);
  }
  if (fd >= 0) {
    char x = 'x';
    (void)::send(fd, &x, 1, 0);
    drop_item(item);
    sys_close(fd);
  }
  kill(pid, SIGTERM);
  waitpid(pid, NULL, 0);
  ASSERT_TRUE(item != NULL);
  ASSERT_TRUE(rc1 == 0);
  ASSERT_TRUE(rc2 == 0);
}

TEST(StStatus, RecvAndReadTimeout) {
  int sp[2];
  int port = 0;
  pid_t pid;
  int fd = -1;
  StEventItem *item = NULL;
  char buf[8];
  int rc_recv = 0;
  ssize_t rc_read = 0;
  int err_recv = 0;
  int err_read = 0;
  struct sockaddr_in dst;
  ASSERT_TRUE(pipe(sp) == 0);
  pid = fork();
  ASSERT_TRUE(pid >= 0);
  if (pid == 0) {
    int lfd;
    int c;
    close(sp[0]);
    alarm(3);
    lfd = bind_loopback(SOCK_STREAM, &port);
    if (lfd < 0 || ::listen(lfd, 1) != 0) {
      _exit(2);
    }
    write_port(sp[1], port);
    close(sp[1]);
    c = ::accept(lfd, NULL, NULL);
    if (c >= 0) {
      /* 握完手就停住，让对端读超时。 */
      sleep(2);
      ::close(c);
    }
    ::close(lfd);
    _exit(0);
  }
  close(sp[1]);
  ASSERT_TRUE(read_port(sp[0], &port) == 0);
  close(sp[0]);
  ASSERT_TRUE(st_init_frame());
  fd = sys_socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_TRUE(fd >= 0);
  item = watch_fd(fd, 0);
  fill_dst(&dst, port);
  ASSERT_TRUE(item != NULL);
  ASSERT_TRUE(st_connect(fd, (struct sockaddr *)&dst, (int)sizeof(dst), 500) ==
              0);
  drop_item(item);
  item = watch_fd(fd, 1);
  ASSERT_TRUE(item != NULL);
  rc_recv = st_recv(fd, buf, (int)sizeof(buf), 0, 40);
  err_recv = errno;
  rc_read = st_read(fd, buf, sizeof(buf), 40);
  err_read = errno;
  drop_item(item);
  sys_close(fd);
  kill(pid, SIGTERM);
  waitpid(pid, NULL, 0);
  ASSERT_TRUE(rc_recv < 0);
  ASSERT_TRUE(err_recv == ETIME);
  ASSERT_TRUE(rc_read < 0);
  ASSERT_TRUE(err_read == ETIME);
}

TEST(StStatus, RecvAndReadEof) {
  int sv[2];
  char buf[8];
  ssize_t n;
  int rn;
  StEventItem *item;
  ASSERT_TRUE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
  ASSERT_TRUE(set_nonblock(sv[0]) == 0);
  ASSERT_TRUE(::close(sv[1]) == 0);
  ASSERT_TRUE(st_init_frame());
  n = st_read(sv[0], buf, sizeof(buf), 200);
  ASSERT_TRUE(n == 0);
  /* st_recv 先等事件。对端已关闭时，登记后应立刻读到 EOF。 */
  item = watch_fd(sv[0], 1);
  ASSERT_TRUE(item != NULL);
  rn = st_recv(sv[0], buf, (int)sizeof(buf), 0, 200);
  drop_item(item);
  ::close(sv[0]);
  ASSERT_TRUE(rn == 0);
}

TEST(StStatus, SendPeerClose) {
  int sv[2];
  ssize_t n;
  ASSERT_TRUE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
  ASSERT_TRUE(set_nonblock(sv[0]) == 0);
  ASSERT_TRUE(::close(sv[1]) == 0);
  ASSERT_TRUE(st_init_frame());
  n = st_send(sv[0], "x", 1, 0, 200);
  ASSERT_TRUE(n < 0);
  ASSERT_TRUE(errno == EPIPE || errno == ECONNRESET);
  n = st_write(sv[0], "y", 1, 200);
  ASSERT_TRUE(n < 0);
  ::close(sv[0]);
}

TEST(StStatus, SendAndWriteTimeout) {
  int sv[2];
  int sz = 1024;
  char junk[4096];
  char *big = NULL;
  const int big_n = 256 * 1024;
  StEventItem *item = NULL;
  ssize_t ns = 1;
  ssize_t nw = 1;
  int es = 0;
  int ew = 0;
  ASSERT_TRUE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
  ASSERT_TRUE(set_nonblock(sv[0]) == 0);
  ASSERT_TRUE(set_nonblock(sv[1]) == 0);
  (void)::setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &sz, sizeof(sz));
  (void)::setsockopt(sv[1], SOL_SOCKET, SO_RCVBUF, &sz, sizeof(sz));
  memset(junk, 'x', sizeof(junk));
  for (;;) {
    ssize_t n = ::send(sv[0], junk, sizeof(junk), MSG_DONTWAIT);
    if (n < 0) {
      break;
    }
  }
  ASSERT_TRUE(st_init_frame());
  item = watch_fd(sv[0], 0);
  big = (char *)malloc((size_t)big_n);
  ASSERT_TRUE(item != NULL && big != NULL);
  memset(big, 'y', (size_t)big_n);
  ns = st_send(sv[0], big, (size_t)big_n, 0, 40);
  es = errno;
  nw = st_write(sv[0], big, (size_t)big_n, 40);
  ew = errno;
  drop_item(item);
  free(big);
  ::close(sv[0]);
  ::close(sv[1]);
  ASSERT_TRUE(ns < 0);
  ASSERT_TRUE(es == ETIME);
  ASSERT_TRUE(nw < 0);
  ASSERT_TRUE(ew == ETIME);
}

TEST(StStatus, TcpCallbackReject) {
  int sp[2];
  int port = 0;
  pid_t pid;
  int rc = 1;
  struct sockaddr_in dst;
  char buf[16];
  int bufsize = (int)sizeof(buf);
  ASSERT_TRUE(pipe(sp) == 0);
  pid = fork();
  ASSERT_TRUE(pid >= 0);
  if (pid == 0) {
    int lfd;
    int c;
    char tmp[32];
    close(sp[0]);
    alarm(3);
    lfd = bind_loopback(SOCK_STREAM, &port);
    if (lfd < 0 || ::listen(lfd, 1) != 0) {
      _exit(2);
    }
    write_port(sp[1], port);
    close(sp[1]);
    c = ::accept(lfd, NULL, NULL);
    if (c >= 0) {
      (void)::recv(c, tmp, sizeof(tmp), 0);
      (void)::send(c, "XXXX", 4, 0);
      ::close(c);
    }
    ::close(lfd);
    _exit(0);
  }
  close(sp[1]);
  ASSERT_TRUE(read_port(sp[0], &port) == 0);
  close(sp[0]);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  fill_dst(&dst, port);
  rc = tcp_sendrecv(&dst, (void *)"PING", 4, buf, bufsize, 500, reject_pkg,
                    false);
  kill(pid, SIGTERM);
  waitpid(pid, NULL, 0);
  ASSERT_TRUE(rc == -6);
}

TEST(StStatus, TcpRemoteClose) {
  int sp[2];
  int port = 0;
  pid_t pid;
  int rc = 1;
  struct sockaddr_in dst;
  char buf[16];
  int bufsize = (int)sizeof(buf);
  ASSERT_TRUE(pipe(sp) == 0);
  pid = fork();
  ASSERT_TRUE(pid >= 0);
  if (pid == 0) {
    int lfd;
    int c;
    close(sp[0]);
    alarm(3);
    lfd = bind_loopback(SOCK_STREAM, &port);
    if (lfd < 0 || ::listen(lfd, 1) != 0) {
      _exit(2);
    }
    write_port(sp[1], port);
    close(sp[1]);
    c = ::accept(lfd, NULL, NULL);
    if (c >= 0) {
      char tmp[8];
      /* 先读完请求再关，避免未读数据被丢弃时内核回 RST（-4）而不是 FIN（-5）。
       */
      (void)::recv(c, tmp, sizeof(tmp), 0);
      ::close(c);
    }
    ::close(lfd);
    _exit(0);
  }
  close(sp[1]);
  ASSERT_TRUE(read_port(sp[0], &port) == 0);
  close(sp[0]);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  fill_dst(&dst, port);
  rc = tcp_sendrecv(&dst, (void *)"PING", 4, buf, bufsize, 500, take_pong,
                    false);
  kill(pid, SIGTERM);
  waitpid(pid, NULL, 0);
  ASSERT_TRUE(rc == -5);
}

TEST(StStatus, TcpBufferFull) {
  int sp[2];
  int port = 0;
  pid_t pid;
  int rc = 1;
  struct sockaddr_in dst;
  char buf[8];
  int bufsize = (int)sizeof(buf);
  ASSERT_TRUE(pipe(sp) == 0);
  pid = fork();
  ASSERT_TRUE(pid >= 0);
  if (pid == 0) {
    int lfd;
    int c;
    close(sp[0]);
    alarm(3);
    lfd = bind_loopback(SOCK_STREAM, &port);
    if (lfd < 0 || ::listen(lfd, 1) != 0) {
      _exit(2);
    }
    write_port(sp[1], port);
    close(sp[1]);
    c = ::accept(lfd, NULL, NULL);
    if (c >= 0) {
      (void)::send(c, "12345678", 8, 0);
      ::close(c);
    }
    ::close(lfd);
    _exit(0);
  }
  close(sp[1]);
  ASSERT_TRUE(read_port(sp[0], &port) == 0);
  close(sp[0]);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  fill_dst(&dst, port);
  rc = tcp_sendrecv(&dst, (void *)"PING", 4, buf, bufsize, 500, need_more,
                    false);
  kill(pid, SIGTERM);
  waitpid(pid, NULL, 0);
  ASSERT_TRUE(rc == -7);
}

TEST(StStatus, TcpRecvTimeoutCode) {
  int sp[2];
  int port = 0;
  pid_t pid;
  int rc = 1;
  struct sockaddr_in dst;
  char buf[16];
  int bufsize = (int)sizeof(buf);
  ASSERT_TRUE(pipe(sp) == 0);
  pid = fork();
  ASSERT_TRUE(pid >= 0);
  if (pid == 0) {
    int lfd;
    int c;
    close(sp[0]);
    alarm(3);
    lfd = bind_loopback(SOCK_STREAM, &port);
    if (lfd < 0 || ::listen(lfd, 1) != 0) {
      _exit(2);
    }
    write_port(sp[1], port);
    close(sp[1]);
    c = ::accept(lfd, NULL, NULL);
    if (c >= 0) {
      (void)::send(c, "Z", 1, 0);
      sleep(2);
      ::close(c);
    }
    ::close(lfd);
    _exit(0);
  }
  close(sp[1]);
  ASSERT_TRUE(read_port(sp[0], &port) == 0);
  close(sp[0]);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  fill_dst(&dst, port);
  rc = tcp_sendrecv(&dst, (void *)"PING", 4, buf, bufsize, 40, slow_need_more,
                    false);
  kill(pid, SIGTERM);
  waitpid(pid, NULL, 0);
  ASSERT_TRUE(rc == -3);
}

TEST(StStatus, TcpKeepliveOnce) {
  int sp[2];
  int port = 0;
  pid_t pid;
  int rc = 1;
  struct sockaddr_in dst;
  char buf[16];
  int bufsize = (int)sizeof(buf);
  ASSERT_TRUE(pipe(sp) == 0);
  pid = fork();
  ASSERT_TRUE(pid >= 0);
  if (pid == 0) {
    int lfd;
    int c;
    char tmp[32];
    close(sp[0]);
    alarm(3);
    lfd = bind_loopback(SOCK_STREAM, &port);
    if (lfd < 0 || ::listen(lfd, 1) != 0) {
      _exit(2);
    }
    write_port(sp[1], port);
    close(sp[1]);
    c = ::accept(lfd, NULL, NULL);
    if (c >= 0) {
      (void)::recv(c, tmp, sizeof(tmp), 0);
      (void)::send(c, "PONG", 4, 0);
      ::close(c);
    }
    ::close(lfd);
    _exit(0);
  }
  close(sp[1]);
  ASSERT_TRUE(read_port(sp[0], &port) == 0);
  close(sp[0]);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  fill_dst(&dst, port);
  rc =
      tcp_sendrecv(&dst, (void *)"PING", 4, buf, bufsize, 500, take_pong, true);
  kill(pid, SIGTERM);
  waitpid(pid, NULL, 0);
  ASSERT_TRUE(rc == 0);
  ASSERT_TRUE(bufsize == 4);
  ASSERT_TRUE(memcmp(buf, "PONG", 4) == 0);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  signal(SIGPIPE, SIG_IGN);
  return RUN_ALL_TESTS();
}
