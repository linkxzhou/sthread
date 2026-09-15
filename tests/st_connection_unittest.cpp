#include "src/st_connection.h"
#include "src/st_sys.h"
#include "app/st_c.h"
#include "stlib/st_netaddr.h"
#include "tests/st_test_compat.h"

ST_NAMESPACE_USING

TEST(StStatus, TCP) {
  st_init_frame();
  st_set_hook_flag();

  StClientConnection<StEventItem> *conn = new StClientConnection<StEventItem>();
  StNetAddr addr;
  addr.SetAddr("127.0.0.1", 80);
  int fd = conn->Create(addr);
  LOG_TRACE("fd: %d", fd);
  conn->Close();
  delete conn;
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
