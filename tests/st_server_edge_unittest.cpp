/*
 * StServer：没套接字就 Listen、UDP、端口占用、以及 Loop 里 DoError。
 */
#include "app/st_c.h"
#include "src/st_server.h"
#include "tests/st_test_compat.h"
#include <errno.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

ST_NAMESPACE_USING

static int g_errpipe = -1;

class EdgeErrConn : public StServerConnection<EdgeErrConn> {
public:
  virtual int32_t DoInput(void *buf, int32_t len) {
    (void)buf;
    if (len > 0) {
      return -1;
    }
    return 0;
  }
  virtual int32_t DoProcess() { return 0; }
  virtual int32_t DoOutput(void *buf, int32_t &len) {
    (void)buf;
    len = 0;
    return 0;
  }
  virtual int32_t DoError(int32_t err) {
    char c = (char)(err < 0 ? 'E' : 'e');
    if (g_errpipe >= 0) {
      (void)::write(g_errpipe, &c, 1);
    }
    return 0;
  }
};

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

TEST(StStatus, LoopReportsDoError) {
  int sp[2];
  int ep[2];
  int port = 0;
  pid_t pid;
  char mark = 0;
  int saw = 0;
  /* 必须在本进程第一次 st_init_frame 之前 fork，否则父子共享同一 epoll。 */
  ASSERT_TRUE(pipe(sp) == 0);
  ASSERT_TRUE(pipe(ep) == 0);
  pid = fork();
  ASSERT_TRUE(pid >= 0);
  if (pid == 0) {
    StServer<EdgeErrConn, eTCP_CONN> *server;
    StNetAddr addr;
    int fd;
    struct sockaddr_in sa;
    socklen_t sl;
    close(sp[0]);
    close(ep[0]);
    alarm(4);
    st_init_frame();
    st_set_hook_flag();
    g_errpipe = ep[1];
    server = new StServer<EdgeErrConn, eTCP_CONN>();
    server->SetHookFlag();
    addr.SetAddr("127.0.0.1", 0);
    fd = server->CreateSocket(addr);
    if (fd < 0 || !server->Listen()) {
      _exit(2);
    }
    sl = sizeof(sa);
    if (::getsockname(fd, (struct sockaddr *)&sa, &sl) != 0) {
      _exit(2);
    }
    write_port(sp[1], ntohs(sa.sin_port));
    close(sp[1]);
    server->Loop();
    _exit(0);
  }
  close(sp[1]);
  close(ep[1]);
  ASSERT_TRUE(read_port(sp[0], &port) == 0);
  close(sp[0]);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  {
    int cfd = sys_socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in dst;
    StEventItem *item;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons((uint16_t)port);
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    item = Instance<UtilPtrPool<StEventItem> >()->AllocPtr();
    ASSERT_TRUE(cfd >= 0 && item != NULL);
    item->SetOsfd(cfd);
    item->EnableOutput();
    item->DisableInput();
    ASSERT_TRUE(GlobalEventSchedule()->Add(item));
    ASSERT_TRUE(
        st_connect(cfd, (struct sockaddr *)&dst, (int)sizeof(dst), 500) == 0);
    ASSERT_TRUE(st_send(cfd, "PING", 4, 0, 500) == 4);
    GlobalEventSchedule()->ClearItem(item);
    UtilPtrPoolFree(item);
    sys_close(cfd);
  }
  {
    /* DoError 写一个字节。给子协程一点调度时间，但不要干等整秒。 */
    fd_set rfds;
    struct timeval tv;
    FD_ZERO(&rfds);
    FD_SET(ep[0], &rfds);
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    if (::select(ep[0] + 1, &rfds, NULL, NULL, &tv) > 0) {
      if (::read(ep[0], &mark, 1) == 1) {
        saw = 1;
      }
    }
  }
  close(ep[0]);
  kill(pid, SIGTERM);
  waitpid(pid, NULL, 0);
  ASSERT_TRUE(saw == 1);
  ASSERT_TRUE(mark == 'E');
}

TEST(StStatus, ListenWithoutSocket) {
  StServer<EdgeErrConn, eTCP_CONN> *server;
  ASSERT_TRUE(st_init_frame());
  server = new StServer<EdgeErrConn, eTCP_CONN>();
  ASSERT_TRUE(server->Listen() == false);
  delete server;
}

TEST(StStatus, UdpCreateListenFails) {
  StServer<EdgeErrConn, eUDP_CONN> *server;
  StNetAddr addr;
  int fd;
  ASSERT_TRUE(st_init_frame());
  server = new StServer<EdgeErrConn, eUDP_CONN>();
  addr.SetAddr("127.0.0.1", 0);
  fd = server->CreateSocket(addr);
  ASSERT_TRUE(fd >= 0);
  ASSERT_TRUE(server->Listen() == false);
  delete server;
}

TEST(StStatus, BindInUse) {
  StServer<EdgeErrConn, eTCP_CONN> *a;
  StServer<EdgeErrConn, eTCP_CONN> *b;
  StNetAddr addr;
  StNetAddr again;
  int fd;
  struct sockaddr_in sa;
  socklen_t sl;
  int port;
  ASSERT_TRUE(st_init_frame());
  a = new StServer<EdgeErrConn, eTCP_CONN>();
  addr.SetAddr("127.0.0.1", 0);
  fd = a->CreateSocket(addr);
  ASSERT_TRUE(fd >= 0);
  sl = sizeof(sa);
  ASSERT_TRUE(::getsockname(fd, (struct sockaddr *)&sa, &sl) == 0);
  port = ntohs(sa.sin_port);
  /* 只 bind、还没 listen 时，SO_REUSEADDR 允许第二个套接字再绑上。 */
  ASSERT_TRUE(a->Listen());
  b = new StServer<EdgeErrConn, eTCP_CONN>();
  again.SetAddr("127.0.0.1", (uint16_t)port);
  ASSERT_TRUE(b->CreateSocket(again) == -2);
  delete b;
  delete a;
}

TEST(StStatus, Ipv6CreateIfAvailable) {
  int probe;
  probe = ::socket(AF_INET6, SOCK_STREAM, 0);
  if (probe < 0) {
    return;
  }
  ::close(probe);
  ASSERT_TRUE(st_init_frame());
  StServer<EdgeErrConn, eTCP_CONN> *server =
      new StServer<EdgeErrConn, eTCP_CONN>();
  StNetAddr addr;
  addr.SetAddr("::1", 0, true);
  int fd = server->CreateSocket(addr);
  ASSERT_TRUE(fd >= 0);
  delete server;
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  signal(SIGPIPE, SIG_IGN);
  return RUN_ALL_TESTS();
}
