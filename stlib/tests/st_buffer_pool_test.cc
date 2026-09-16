#include "stlib/st_buffer.h"
#include "stlib/st_test.h"
#include <string.h>

using namespace stlib;

TEST(StStatus, buffer_set_exact_maxlen) {
  StBuffer buf(16);
  char data[16];
  memset(data, 'x', sizeof(data));
  /* B20: exact fill must succeed (len == max) */
  ASSERT_TRUE(buf.SetBuffer(data, 16) == 16);
  ASSERT_TRUE(buf.GetMsgLen() == 16);
  ASSERT_TRUE(buf.SetBuffer(data, 17) == -1);
}

TEST(StStatus, buffer_pool_dtor_no_hang) {
  {
    StBufferPool pool(2);
    StBuffer *a = pool.GetBuffer(64);
    StBuffer *b = pool.GetBuffer(128);
    ASSERT_TRUE(a != NULL && b != NULL);
    pool.FreeBuffer(a);
    pool.FreeBuffer(b);
  }
  /* leaving scope must not double-free / hang */
  ASSERT_TRUE(1);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
