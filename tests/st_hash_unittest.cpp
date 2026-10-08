#include "stlib/st_hash_list.h"
#include "stlib/st_netaddr.h"
#include "tests/st_test_compat.h"

ST_NAMESPACE_USING

static int g_hash_visits = 0;

class IntKey : public StHashKey {
public:
  IntKey(uint32_t h, int id) : m_h_(h), m_id_(id) {}
  virtual uint32_t HashValue() { return m_h_; }
  virtual int32_t HashCmp(StHashKey *rhs) {
    IntKey *other = dynamic_cast<IntKey *>(rhs);
    if (other == NULL) {
      return -1;
    }
    return m_id_ - other->m_id_;
  }
  virtual void HashIterate() { g_hash_visits++; }
  uint32_t m_h_;
  int m_id_;
};

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

TEST(StStatus, HashChainAndReject) {
  StHashList<IntKey> hl(4);
  IntKey a(7, 1);
  IntKey b(7, 2);
  IntKey c(7, 3);
  IntKey probe(7, 2);
  IntKey missing(7, 9);
  IntKey *gone;
  g_hash_visits = 0;
  ASSERT_TRUE(hl.HashInsert(NULL) < 0);
  ASSERT_TRUE(hl.HashFind(NULL) == NULL);
  ASSERT_TRUE(hl.HashRemove(NULL) == NULL);
  ASSERT_TRUE(hl.HashInsert(&a) == 0);
  ASSERT_TRUE(hl.HashInsert(&b) == 0);
  ASSERT_TRUE(hl.HashInsert(&c) == 0);
  ASSERT_TRUE(hl.HashInsert(&a) < 0);
  ASSERT_TRUE(hl.HashSize() == 3);
  ASSERT_TRUE(hl.HashFind(&probe) == &b);
  gone = hl.HashRemove(&probe);
  ASSERT_TRUE(gone == &b);
  ASSERT_TRUE(hl.HashFind(&missing) == NULL);
  ASSERT_TRUE(hl.HashRemove(&missing) == NULL);
  ASSERT_TRUE(hl.HashFind(&a) == &a);
  ASSERT_TRUE(hl.HashFind(&c) == &c);
  gone = hl.HashRemove(&a);
  ASSERT_TRUE(gone == &a);
  ASSERT_TRUE(hl.HashGetFirst() == &c);
  hl.HashForeach();
  ASSERT_TRUE(g_hash_visits == 1);
  ASSERT_TRUE(hl.HashInsert(&b) == 0);
  ASSERT_TRUE(hl.HashSize() == 2);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
