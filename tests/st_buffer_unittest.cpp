#include "stlib/st_buffer.h"
#include "tests/st_test_compat.h"

ST_NAMESPACE_USING

TEST(StStatus, BufferBasic) {
  StBuffer buf(64);
  ASSERT_TRUE(buf.GetMaxLen() >= 64);
  ASSERT_TRUE(buf.GetBuffer() != NULL);
  char s[] = "hello";
  ASSERT_TRUE(buf.SetBuffer(s, 5) >= 0);
  ASSERT_TRUE(buf.GetMsgLen() == 5);
  buf.SetHaveSendLen(2);
  ASSERT_TRUE(buf.GetHaveSendLen() == 2);
  buf.SetHaveRecvLen(3);
  ASSERT_TRUE(buf.GetHaveRecvLen() == 3);
  buf.SetMsgLen(4);
  ASSERT_TRUE(buf.GetMsgLen() == 4);
}

TEST(StStatus, BufferPool) {
  StBuffer *b1 = Instance<StBufferPool>()->GetBuffer(128);
  StBuffer *b2 = Instance<StBufferPool>()->GetBuffer(256);
  ASSERT_TRUE(b1 != NULL && b2 != NULL);
  Instance<StBufferPool>()->FreeBuffer(b1);
  Instance<StBufferPool>()->FreeBuffer(b2);
  StBuffer *b3 = Instance<StBufferPool>()->GetBuffer(128);
  ASSERT_TRUE(b3 != NULL);
  Instance<StBufferPool>()->FreeBuffer(b3);
}

TEST(StStatus, BufferBounds) {
  StBuffer buf(16);
  char small[4];
  char big[32];
  memset(small, 'a', sizeof(small));
  memset(big, 'b', sizeof(big));
  ASSERT_TRUE(buf.SetBuffer(NULL, 1) < 0);
  ASSERT_TRUE(buf.SetBuffer(big, 32) < 0);
  ASSERT_TRUE(buf.SetBuffer(small, 4) == 4);
  ASSERT_TRUE(buf.GetMsgLen() == 4);
  buf.Reset();
  ASSERT_TRUE(buf.GetMsgLen() == 0);
  ASSERT_TRUE(buf.GetHaveSendLen() == 0);
  ASSERT_TRUE(buf.GetHaveRecvLen() == 0);
}

TEST(StStatus, PoolMaxFreeAndAlign) {
  StBufferPool pool(1);
  StBuffer *a;
  StBuffer *b;
  StBuffer *c;
  uint32_t m;
  /* ST_ALGIN(8) 与 ST_ALGIN(9) 落在同一桶。 */
  a = pool.GetBuffer(8);
  b = pool.GetBuffer(9);
  ASSERT_TRUE(a != NULL && b != NULL);
  ASSERT_TRUE(a->GetMaxLen() == b->GetMaxLen());
  m = a->GetMaxLen();
  pool.FreeBuffer(a);
  /* max_free 是 1，第二块应被释放而不是再进空闲链。 */
  pool.FreeBuffer(b);
  c = pool.GetBuffer(8);
  ASSERT_TRUE(c != NULL);
  ASSERT_TRUE(c->GetMaxLen() == m);
  pool.FreeBuffer(c);
  pool.FreeBuffer(NULL);
  pool.SetMaxFreeNum(0);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
