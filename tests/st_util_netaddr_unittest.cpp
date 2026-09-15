#include "stlib/st_util.h"
#include "stlib/st_netaddr.h"
#include "stlib/st_closure.h"
#include "stlib/st_log.h"
#include "tests/st_test_compat.h"

ST_NAMESPACE_USING

static int g_clos_n = 0;
static void clos_fn(int x) { g_clos_n += x; }

class PoolItem {
public:
  PoolItem() : m_v_(0) {}
  void Reset() { m_v_ = 0; }
  int m_v_;
};

TEST(StStatus, UtilBasics) {
  uint64_t t1 = Util::TimeMs();
  ASSERT_TRUE(t1 > 0);
  uint64_t id1 = Util::GetUniqid();
  uint64_t id2 = Util::GetUniqid();
  ASSERT_TRUE(id1 != id2);
  Util::USleep(1000);

  UtilPtrPool<PoolItem> pool(2);
  PoolItem *a = pool.AllocPtr();
  PoolItem *b = pool.AllocPtr();
  ASSERT_TRUE(a != NULL && b != NULL);
  a->m_v_ = 1;
  b->m_v_ = 2;
  UtilPtrPoolFree(a);
  UtilPtrPoolFree(b);
  PoolItem *c = pool.AllocPtr();
  ASSERT_TRUE(c != NULL);
  UtilPtrPoolFree(c);
}

TEST(StStatus, NetAddr) {
  StNetAddr a;
  a.SetAddr("127.0.0.1", 8080);
  ASSERT_TRUE(!a.IsError());
  ASSERT_TRUE(!a.IsIPV6());
  ASSERT_TRUE(a.Port() == 8080);
  ASSERT_TRUE(strstr(a.IP(), "127.0.0.1") != NULL);
  ASSERT_TRUE(a.GetSockAddr() != NULL);
  ASSERT_TRUE(a.IPPort() != NULL);

  StNetAddr b;
  b.SetAddr((uint16_t)0, true, false);
  ASSERT_TRUE(!b.IsError());

  StNetAddr c;
  c.SetAddr("localhost");
  (void)c.IsError();

  StNetAddr d(*(struct sockaddr_in *)a.GetSockAddr());
  ASSERT_TRUE(d.Port() == 8080);

  StNetAddr e;
  e.SetAddr("::1", 443, true);
  (void)e.IsIPV6();
  (void)e.GetSock6Addr();
  (void)e.IP();
  (void)e.IPPort();
}

TEST(StStatus, Closure) {
  g_clos_n = 0;
  StClosure *cl = NewStClosure(clos_fn, 3);
  ASSERT_TRUE(cl != NULL);
  cl->Run();
  ASSERT_TRUE(g_clos_n == 3);
  delete cl;
}

TEST(StStatus, LogLevels) {
  StLogger::Instance().SetLevel(LLOG_PVERB);
  LOG_TRACE("trace-cover");
  LOG_DEBUG("debug-cover");
  LOG_WARN("warn-cover");
  LOG_ERROR("error-cover");
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
