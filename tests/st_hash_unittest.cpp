#include "stlib/st_hash_list.h"
#include "stlib/st_netaddr.h"
#include "tests/st_test_compat.h"

ST_NAMESPACE_USING

TEST(StStatus, HashList) {
  StHashList<StNetAddrKey> hl;
  StNetAddr dest, src;
  dest.SetAddr("127.0.0.1", 1);
  src.SetAddr("127.0.0.1", 2);

  /* HashInsert stores the key pointer; HashRemove returns it for caller free.
   */
  StNetAddrKey *k1 = new StNetAddrKey();
  k1->SetDestAddr(dest);
  k1->SetSrcAddr(src);
  int dummy = 42;
  k1->SetDataPtr(&dummy);
  ASSERT_TRUE(hl.HashInsert(k1) >= 0);

  StNetAddrKey probe;
  probe.SetDestAddr(dest);
  probe.SetSrcAddr(src);
  void *p = hl.HashFindData(&probe);
  ASSERT_TRUE(p == &dummy);
  ASSERT_TRUE(hl.HashSize() == 1);

  StNetAddrKey *dead = hl.HashRemove(&probe);
  ASSERT_TRUE(dead == k1);
  st_safe_delete(dead);
  ASSERT_TRUE(hl.HashFindData(&probe) == NULL);
  ASSERT_TRUE(hl.HashSize() == 0);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
