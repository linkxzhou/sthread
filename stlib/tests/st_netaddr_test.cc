#include "stlib/st_netaddr.h"
#include "stlib/st_test.h"

using namespace stlib;

TEST(StStatus, netaddr_eq_port_not_assign) {
  StNetAddr a, b;
  a.SetAddr("10.0.0.1", 1000);
  b.SetAddr("10.0.0.1", 2000);
  ASSERT_TRUE(!(a == b));
  /* regression: old operator== assigned port and mutated `a` */
  ASSERT_TRUE(a.Port() == 1000);
  ASSERT_TRUE(b.Port() == 2000);
}

TEST(StStatus, netaddr_ipv6_pton) {
  StNetAddr a;
  a.SetAddr("::1", 53, true);
  ASSERT_TRUE(!a.IsError());
  ASSERT_TRUE(a.IsIPV6());
  ASSERT_TRUE(a.Port() == 53);
  StNetAddr b;
  b.SetAddr("::1", 53, true);
  ASSERT_TRUE(a == b);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
