/*
 * stlib 最小用法示例（可编译、可运行）。
 * 构建：make -C stlib/tests st_demo_usage_test
 * 运行：./stlib/tests/st_demo_usage_test
 *
 * 说明：完整 TCP echo / HTTP server 需要 libmthread（见 app/st_httpserver）。
 * 本文件只演示 stlib 内可独立使用的堆、缓冲池与时间工具。
 */

#include "stlib/st_buffer.h"
#include "stlib/st_heap.h"
#include "stlib/st_test.h"
#include "stlib/st_util.h"

#include <stdio.h>
#include <string.h>

using namespace stlib;

class DemoNode : public StHeap {
public:
  explicit DemoNode(int64_t v) : m_v_(v) {}
  virtual int64_t HeapValue() { return m_v_; }
  int64_t m_v_;
};

TEST(StStatus, DemoHeapAndBuffer) {
  /* 1) 最小堆：到期时间越小越靠前（定时器 / sleep 队列同构）。 */
  StHeapList<DemoNode> heap(16);
  DemoNode a(30), b(10), c(20);
  ASSERT_TRUE(heap.HeapPush(&a) == 0);
  ASSERT_TRUE(heap.HeapPush(&b) == 0);
  ASSERT_TRUE(heap.HeapPush(&c) == 0);
  ASSERT_TRUE(heap.HeapTop() != NULL && heap.HeapTop()->m_v_ == 10);
  ASSERT_TRUE(heap.HeapPop()->m_v_ == 10);

  /* 2) 缓冲池：按长度对齐分桶，用完 FreeBuffer 归还。 */
  StBufferPool pool;
  StBuffer *buf = pool.GetBuffer(64);
  ASSERT_TRUE(buf != NULL);
  ASSERT_TRUE(buf->GetMaxLen() >= 64);
  void *p = buf->GetBuffer();
  ASSERT_TRUE(p != NULL);
  memcpy(p, "stlib", 5);
  buf->SetMsgLen(5);
  ASSERT_TRUE(buf->GetMsgLen() == 5);
  pool.FreeBuffer(buf);

  /* 3) 毫秒时钟（单测里只确认可调用）。 */
  uint64_t ms = Util::TimeMs();
  ASSERT_TRUE(ms > 0);
  fprintf(stdout, "stlib demo ok, TimeMs=%llu\n", (unsigned long long)ms);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
