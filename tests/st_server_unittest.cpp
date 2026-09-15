#include "src/st_server.h"
#include "app/st_c.h"
#include "tests/st_test_compat.h"

ST_NAMESPACE_USING

class StTcpServerConnection : public StServerConnection<StTcpServerConnection> {
public:
  virtual int32_t DoInput(void *buf, int32_t len) {
    LOG_TRACE("buf: %s, len: %d", (char *)buf, len);
    return len;
  }

  virtual int32_t DoOutput(void *buf, int32_t &len) {
    len = sprintf((char *)buf,
                  "HTTP/1.1 200 OK\r\n"
                  "Content-Length: 1\r\n"
                  "Content-Type: text/html\r\n"
                  "Server: sthread/1.0.1\r\n\r\n1");
    return 0;
  }

  virtual int32_t DoProcess() { return 0; }

  virtual int32_t DoError(int32_t err) {
    LOG_ERROR("err: %d", err);
    return 0;
  }
};

TEST(StStatus, accept_create) {
  st_init_frame();
  StServer<StTcpServerConnection, eTCP_CONN> *server =
      new StServer<StTcpServerConnection, eTCP_CONN>();
  server->SetHookFlag();

  StNetAddr addr;
  addr.SetAddr("127.0.0.1", 18001);
  int fd = server->CreateSocket(addr);
  LOG_TRACE("fd: %d", fd);
  ASSERT_TRUE(fd >= 0);
  ASSERT_TRUE(server->Listen());
  /* Loop() blocks forever; plan/04 only verifies listen path here.
   * Full accept smoke is a known follow-up (needs working context switch). */
  delete server;
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
