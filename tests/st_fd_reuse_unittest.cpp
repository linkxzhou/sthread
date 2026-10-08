/*
 * close 之后 fd 号会复用。兴趣缓存没清掉时，第二次 udp_sendrecv / tcp_sendrecv
 * 会在 WaitFdReady 里失败（0141a94 的 "item delete failed" 同类问题）。
 */
#include "app/st_c.h"
#include "tests/st_test_compat.h"
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

ST_NAMESPACE_USING

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

static int pong_check(void *buf, int len) {
  if (len >= 4 && memcmp(buf, "PONG", 4) == 0) {
    return 4;
  }
  if (len >= 4) {
    return -1;
  }
  return 0;
}

TEST(StStatus, UdpEightSequential) {
  int sp[2];
  int port = 0;
  pid_t pid;
  int i;
  int ok = 1;
  struct sockaddr_in dst;
  ASSERT_TRUE(pipe(sp) == 0);
  pid = fork();
  ASSERT_TRUE(pid >= 0);
  if (pid == 0) {
    int fd;
    int yes = 1;
    struct sockaddr_in addr;
    socklen_t alen;
    close(sp[0]);
    alarm(4);
    fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (fd < 0 || ::bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
      _exit(2);
    }
    alen = sizeof(addr);
    if (::getsockname(fd, (struct sockaddr *)&addr, &alen) != 0) {
      _exit(2);
    }
    write_port(sp[1], ntohs(addr.sin_port));
    close(sp[1]);
    for (i = 0; i < 8; i++) {
      char buf[16];
      struct sockaddr_in from;
      socklen_t fl = sizeof(from);
      int n = (int)::recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&from,
                              &fl);
      if (n != 4 || memcmp(buf, "PING", 4) != 0 ||
          ::sendto(fd, "PONG", 4, 0, (struct sockaddr *)&from, fl) != 4) {
        _exit(3);
      }
    }
    ::close(fd);
    _exit(0);
  }
  close(sp[1]);
  ASSERT_TRUE(read_port(sp[0], &port) == 0);
  close(sp[0]);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = htons((uint16_t)port);
  dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  for (i = 0; i < 8; i++) {
    char recvbuf[16];
    int bufsize = (int)sizeof(recvbuf);
    int rc = udp_sendrecv(&dst, (void *)"PING", 4, recvbuf, bufsize, 500);
    if (rc != 0 || bufsize != 4 || memcmp(recvbuf, "PONG", 4) != 0) {
      ok = 0;
      break;
    }
  }
  {
    int st = 0;
    if (!ok) {
      kill(pid, SIGTERM);
    }
    waitpid(pid, &st, 0);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);
  }
}

TEST(StStatus, TcpTwoSequential) {
  int sp[2];
  int port = 0;
  pid_t pid;
  int i;
  int ok = 1;
  struct sockaddr_in dst;
  ASSERT_TRUE(pipe(sp) == 0);
  pid = fork();
  ASSERT_TRUE(pid >= 0);
  if (pid == 0) {
    int lfd;
    int yes = 1;
    struct sockaddr_in addr;
    socklen_t alen;
    close(sp[0]);
    alarm(4);
    lfd = ::socket(AF_INET, SOCK_STREAM, 0);
    ::setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (lfd < 0 || ::bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        ::listen(lfd, 2) != 0) {
      _exit(2);
    }
    alen = sizeof(addr);
    if (::getsockname(lfd, (struct sockaddr *)&addr, &alen) != 0) {
      _exit(2);
    }
    write_port(sp[1], ntohs(addr.sin_port));
    close(sp[1]);
    for (i = 0; i < 2; i++) {
      int c = ::accept(lfd, NULL, NULL);
      char buf[16];
      if (c < 0) {
        _exit(3);
      }
      if (::recv(c, buf, sizeof(buf), 0) <= 0 || ::send(c, "PONG", 4, 0) != 4) {
        ::close(c);
        _exit(4);
      }
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
  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = htons((uint16_t)port);
  dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  for (i = 0; i < 2; i++) {
    char recvbuf[16];
    int bufsize = (int)sizeof(recvbuf);
    int rc = tcp_sendrecv(&dst, (void *)"PING", 4, recvbuf, bufsize, 500,
                          pong_check, false);
    if (rc != 0 || bufsize != 4 || memcmp(recvbuf, "PONG", 4) != 0) {
      ok = 0;
      break;
    }
  }
  {
    int st = 0;
    if (!ok) {
      kill(pid, SIGTERM);
    }
    waitpid(pid, &st, 0);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);
  }
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  signal(SIGPIPE, SIG_IGN);
  return RUN_ALL_TESTS();
}
