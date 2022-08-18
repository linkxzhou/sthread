#include "stlib/st_buffer.h"
#include "stlib/st_test.h"
#include "stlib/st_util.h"

using namespace stlib;

TEST(StStatus, StBuffer) {
  StBuffer *buf1 = Instance<StBufferPool>()->GetBuffer(100);
  LOG_DEBUG("buf1: %p, addr : %p", buf1, Instance<StBufferPool>());

  StBuffer *buf2 = Instance<StBufferPool>()->GetBuffer(1000);
  LOG_DEBUG("buf2: %p, addr : %p", buf2, Instance<StBufferPool>());

  StBuffer *buf3 = Instance<StBufferPool>()->GetBuffer(10000);
  LOG_DEBUG("buf3: %p, addr : %p", buf3, Instance<StBufferPool>());

  StBuffer *buf4 = Instance<StBufferPool>()->GetBuffer(100);
  LOG_DEBUG("buf4: %p, addr : %p", buf4, Instance<StBufferPool>());

  Instance<StBufferPool>()->FreeBuffer(buf1);
  Instance<StBufferPool>()->FreeBuffer(buf4);

  StBuffer *buf5 = Instance<StBufferPool>()->GetBuffer(100);
  LOG_DEBUG("buf5 : %p, addr : %p", buf5, Instance<StBufferPool>());
}

// 测试所有的功能
int main(int argc, char *argv[]) { return RUN_ALL_TESTS(); }