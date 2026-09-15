#include "src/st_connection.h"
#include "src/st_public.h"
#include "tests/st_test_compat.h"

ST_NAMESPACE_USING

TEST(StStatus, KeepliveEnumValues) {
  LOG_ASSERT(eUNDEF_CONN == 0x0);
  LOG_ASSERT(eUDP_CONN == 0x10);
  LOG_ASSERT(eTCP_CONN == 0x20);
  LOG_ASSERT(eTCP_KEEPLIVE_CONN == 0x11);
  LOG_ASSERT(eUDP_UDPSESSION_CONN == 0x21);

  LOG_ASSERT(IS_KEEPLIVE(eTCP_KEEPLIVE_CONN));
  LOG_ASSERT(IS_KEEPLIVE(eUDP_UDPSESSION_CONN));
  LOG_ASSERT(!IS_KEEPLIVE(eTCP_CONN));
  LOG_ASSERT(!IS_KEEPLIVE(eUDP_CONN));
  LOG_ASSERT(!IS_KEEPLIVE(eUNDEF_CONN));

  LOG_ASSERT(IS_TCP_CONN(eTCP_CONN));
  LOG_ASSERT(IS_TCP_CONN(eTCP_KEEPLIVE_CONN));
  LOG_ASSERT(!IS_TCP_CONN(eUDP_CONN));

  LOG_ASSERT(IS_UDP_CONN(eUDP_CONN));
  LOG_ASSERT(IS_UDP_CONN(eUDP_UDPSESSION_CONN));
  LOG_ASSERT(!IS_UDP_CONN(eTCP_CONN));
}

TEST(StStatus, KeepliveMethod) {
  StConnection c;
  c.SetConnType(eTCP_CONN);
  LOG_ASSERT(!c.Keeplive());
  c.SetConnType(eTCP_KEEPLIVE_CONN);
  LOG_ASSERT(c.Keeplive());
  c.SetConnType(eUDP_UDPSESSION_CONN);
  LOG_ASSERT(c.Keeplive());
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
