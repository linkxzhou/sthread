#include "app/st_c.h"
#include "src/st_connection.h"
#include "src/st_sys.h"
#include "stlib/st_netaddr.h"
#include "tests/st_test_compat.h"
#include <sys/wait.h>
#include <unistd.h>

ST_NAMESPACE_USING

class EchoClient : public StClientConnection<StEventItem> {
public:
  virtual int32_t DoOutput(void *buf, int32_t &len) {
    memcpy(buf, "PING", 4);
    len = 4;
    return 0;
  }
  virtual int32_t DoInput(void *buf, int32_t len) {
    (void)buf;
    if (len >= 4) {
      return 4;
    }
    return 0;
  }
};

class UdpEchoClient : public StClientConnection<StEventItem> {
public:
  virtual int32_t DoOutput(void *buf, int32_t &len) {
    memcpy(buf, "UPING", 5);
    len = 5;
    return 0;
  }
  virtual int32_t DoInput(void *buf, int32_t len) {
    (void)buf;
    return len > 0 ? len : -1;
  }
};

TEST(StStatus, ConnSendRecvTcp) {
  int port = 19111;
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
      send(c, "PONG", 4, 0);
      close(c);
    }
    close(fd);
    _exit(0);
  }

  Util::USleep(100000);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();

  EchoClient *conn = new EchoClient();
  conn->SetConnType(eTCP_CONN);
  conn->SetTimeout(2000);
  StNetAddr addr;
  addr.SetAddr("127.0.0.1", port);
  int fd = conn->Create(addr);
  ASSERT_TRUE(fd >= 0);

  int32_t sn = conn->SendData();
  ASSERT_TRUE(sn == 4);

  int32_t rn = conn->RecvData();
  ASSERT_TRUE(rn == 0);
  ASSERT_TRUE(conn->GetRecvBuffer()->GetMsgLen() >= 4);
  ASSERT_TRUE(memcmp(conn->GetRecvBuffer()->GetBuffer(), "PONG", 4) == 0);

  conn->Close();
  delete conn;
  int st = 0;
  waitpid(pid, &st, 0);
}

TEST(StStatus, ConnSendRecvUdp) {
  int port = 19112;
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
      sendto(fd, "UPONG", 5, 0, (struct sockaddr *)&from, fl);
    }
    close(fd);
    _exit(0);
  }

  Util::USleep(100000);
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();

  UdpEchoClient *conn = new UdpEchoClient();
  conn->SetConnType(eUDP_CONN);
  conn->SetTimeout(2000);
  StNetAddr addr;
  addr.SetAddr("127.0.0.1", port);
  int fd = conn->Create(addr);
  ASSERT_TRUE(fd >= 0);

  int32_t sn = conn->SendData();
  ASSERT_TRUE(sn == 5);
  int32_t rn = conn->RecvData();
  ASSERT_TRUE(rn == 0);
  ASSERT_TRUE(conn->GetRecvBuffer()->GetHaveRecvLen() >= 5);

  conn->Close();
  delete conn;
  int st = 0;
  waitpid(pid, &st, 0);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
