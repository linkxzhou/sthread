#include "src/st_server.h"
#include "app/st_c.h"
#include "tests/st_test_compat.h"
#include <signal.h>
#include <sys/wait.h>

ST_NAMESPACE_USING

class EchoConn : public StServerConnection<EchoConn> {
public:
  virtual int32_t DoInput(void *buf, int32_t len) {
    (void)buf;
    return len > 0 ? len : -1;
  }
  virtual int32_t DoOutput(void *buf, int32_t &len) {
    memcpy(buf, "PONG", 4);
    len = 4;
    return 0;
  }
  virtual int32_t DoProcess() { return 0; }
  virtual int32_t DoError(int32_t err) {
    (void)err;
    return 0;
  }
};

static int tcp_check(void *buf, int len) {
  if (len >= 4 && memcmp(buf, "PONG", 4) == 0)
    return 4;
  if (len >= 4)
    return -1;
  return 0;
}

TEST(StStatus, ServerAcceptOnce) {
  int port = 19021;
  pid_t pid = fork();
  ASSERT_TRUE(pid >= 0);
  if (pid == 0) {
    st_init_frame();
    st_set_hook_flag();
    StServer<EchoConn, eTCP_CONN> *server = new StServer<EchoConn, eTCP_CONN>();
    server->SetHookFlag();
    StNetAddr addr;
    addr.SetAddr("127.0.0.1", port);
    ASSERT_TRUE(server->CreateSocket(addr) >= 0);
    ASSERT_TRUE(server->Listen());
    alarm(3);
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
  char recvbuf[64];
  int bufsize = sizeof(recvbuf);
  int rc = tcp_sendrecv(&dst, (void *)"PING", 4, recvbuf, bufsize, 1500,
                        tcp_check, false);
  LOG_TRACE("accept smoke rc=%d", rc);
  kill(pid, SIGTERM);
  int st;
  waitpid(pid, &st, 0);
  ASSERT_TRUE(rc == 0);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
