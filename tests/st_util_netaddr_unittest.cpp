#include "stlib/st_closure.h"
#include "stlib/st_log.h"
#include "stlib/st_netaddr.h"
#include "stlib/st_platform.h"
#include "stlib/st_util.h"
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

TEST(StStatus, NetAddrEqualityAndIpv6) {
  StNetAddr a, b, c;
  a.SetAddr("127.0.0.1", 8080);
  b.SetAddr("127.0.0.1", 8080);
  c.SetAddr("127.0.0.1", 8081);
  ASSERT_TRUE(a == b);
  ASSERT_TRUE(!(a == c));

  StNetAddr v6a, v6b, v6c;
  v6a.SetAddr("::1", 443, true);
  v6b.SetAddr("::1", 443, true);
  v6c.SetAddr("::1", 444, true);
  ASSERT_TRUE(!v6a.IsError());
  ASSERT_TRUE(v6a.IsIPV6());
  ASSERT_TRUE(v6a.Port() == 443);
  ASSERT_TRUE(v6a == v6b);
  ASSERT_TRUE(!(v6a == v6c));
  ASSERT_TRUE(strstr(v6a.IP(), "::1") != NULL ||
              strstr(v6a.IP(), "0:0:0:0:0:0:0:1") != NULL);
  ASSERT_TRUE(strstr(v6a.IPPort(), "443") != NULL);
}

TEST(StStatus, Closure) {
  g_clos_n = 0;
  StClosure *cl = NewStClosure(clos_fn, 3);
  ASSERT_TRUE(cl != NULL);
  cl->Run();
  ASSERT_TRUE(g_clos_n == 3);
  delete cl;
}

TEST(StStatus, NetAddrParseErrors) {
  StNetAddr bad4;
  StNetAddr bad6;
  StNetAddr any4;
  StNetAddr any6;
  StNetAddr v4;
  StNetAddr v6;
  bad4.SetAddr("999.999.999.999", 9);
  ASSERT_TRUE(bad4.IsError());
  bad6.SetAddr("gggg::1", 9, true);
  ASSERT_TRUE(bad6.IsError());
  any4.SetAddr((uint16_t)0, false, false);
  ASSERT_TRUE(any4.IsError() == false);
  ASSERT_TRUE(any4.Port() == 0);
  ASSERT_TRUE(any4.IsIPV6() == false);
  any6.SetAddr((uint16_t)53, false, true);
  ASSERT_TRUE(any6.IsIPV6());
  ASSERT_TRUE(any6.Port() == 53);
  v4.SetAddr("127.0.0.1", 1);
  v6.SetAddr("::1", 1, true);
  ASSERT_TRUE((v4 == v6) == false);
}

TEST(StStatus, PlatformMacros) {
#if defined(__ANDROID__)
  ASSERT_TRUE(ST_OS_ANDROID == 1);
  ASSERT_TRUE(ST_OS_LINUX == 0);
  ASSERT_TRUE(ST_POLL_EPOLL == 1);
  ASSERT_TRUE(ST_POLL_KQUEUE == 0);
#elif defined(__APPLE__)
  ASSERT_TRUE(ST_OS_DARWIN == 1);
  ASSERT_TRUE(ST_OS_LINUX == 0);
  ASSERT_TRUE(ST_POLL_KQUEUE == 1);
  ASSERT_TRUE(ST_POLL_EPOLL == 0);
#elif defined(__linux__)
  ASSERT_TRUE(ST_OS_LINUX == 1);
  ASSERT_TRUE(ST_OS_ANDROID == 0);
  ASSERT_TRUE(ST_OS_DARWIN == 0);
  ASSERT_TRUE(ST_POLL_EPOLL == 1);
  ASSERT_TRUE(ST_POLL_KQUEUE == 0);
#else
  ASSERT_TRUE(ST_OS_LINUX + ST_OS_DARWIN + ST_OS_ANDROID + ST_OS_OPENBSD +
                  ST_OS_FREEBSD >=
              0);
#endif
  ASSERT_TRUE(ST_HOOK == 1);
  ASSERT_TRUE(ST_OS_OPENBSD == 0 || ST_POLL_KQUEUE == 1);
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
