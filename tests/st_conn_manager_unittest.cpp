#include "app/st_c.h"
#include "src/st_connection.h"
#include "stlib/st_netaddr.h"
#include "tests/st_test_compat.h"

ST_NAMESPACE_USING

class CovConn : public StConnection {
public:
  virtual void Reset() { StConnection::Reset(); }
};

TEST(StStatus, ConnManagerKeepaliveReuse) {
  StConnectionManager<CovConn> mgr;
  StNetAddr dest, src;
  dest.SetAddr("127.0.0.1", 19001);
  src.SetAddr("127.0.0.1", 19002);

  CovConn *c1 = mgr.AllocPtr(eTCP_KEEPLIVE_CONN, &dest, &src);
  ASSERT_TRUE(c1 != NULL);
  ASSERT_TRUE(c1->Keeplive());
  CovConn *c2 = mgr.AllocPtr(eTCP_KEEPLIVE_CONN, &dest, &src);
  ASSERT_TRUE(c2 == c1);

  CovConn *c3 = mgr.AllocPtr(eTCP_CONN, &dest);
  ASSERT_TRUE(c3 != NULL);
  ASSERT_TRUE(c3 != c1);
  mgr.FreePtr(c3);
  mgr.FreePtr(c1);
}

TEST(StStatus, ClientCreateClose) {
  ASSERT_TRUE(st_init_frame());
  st_set_hook_flag();
  StClientConnection<StEventItem> *conn = new StClientConnection<StEventItem>();
  conn->SetConnType(eTCP_CONN);
  StNetAddr addr;
  addr.SetAddr("127.0.0.1", 1);
  int fd = conn->Create(addr);
  LOG_TRACE("Create fd=%d", fd);
  conn->Close();
  delete conn;
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
