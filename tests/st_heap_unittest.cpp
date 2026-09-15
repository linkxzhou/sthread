#include "stlib/st_heap.h"
#include "stlib/st_heap_timer.h"
#include "tests/st_test_compat.h"

ST_NAMESPACE_USING

class Node : public StHeap {
public:
  explicit Node(int64_t v) : m_v_(v) {}
  virtual int64_t HeapValue() { return m_v_; }
  int64_t m_v_;
};

TEST(StStatus, HeapOps) {
  StHeapList<Node> h;
  ASSERT_TRUE(h.HeapResize(16) >= 0);
  Node n1(30), n2(10), n3(20);
  ASSERT_TRUE(h.HeapPush(&n1) >= 0);
  ASSERT_TRUE(h.HeapPush(&n2) >= 0);
  ASSERT_TRUE(h.HeapPush(&n3) >= 0);
  ASSERT_TRUE(h.HeapSize() == 3);
  ASSERT_TRUE(!h.HeapEmpty());
  Node *top = h.HeapTop();
  ASSERT_TRUE(top != NULL && top->m_v_ == 10);
  Node *p = h.HeapPop();
  ASSERT_TRUE(p != NULL && p->m_v_ == 10);
  ASSERT_TRUE(h.HeapDelete(&n1) >= 0);
  while (!h.HeapEmpty()) {
    h.HeapPop();
  }
  ASSERT_TRUE(h.HeapEmpty());
}

TEST(StStatus, HeapTimer) {
  StHeapTimer timer(16);
  StTimer *a = new StTimer();
  StTimer *b = new StTimer();
  ASSERT_TRUE(timer.Startup(a, 0));
  ASSERT_TRUE(timer.Startup(b, 60000));
  Util::USleep(2000);
  int n = timer.CheckExpired();
  ASSERT_TRUE(n >= 1);
  timer.Stop(b);
  delete a;
  delete b;
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
