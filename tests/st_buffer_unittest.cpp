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

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
