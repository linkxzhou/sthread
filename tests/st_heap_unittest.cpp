#include "stlib/st_heap.h"
#include "stlib/st_heap_timer.h"
#include "tests/st_test_compat.h"

ST_NAMESPACE_USING

class CountTimer : public StTimer {
public:
  CountTimer() : n_(0) {}
  virtual void Timeout() { n_++; }
  int n_;
};

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

TEST(StStatus, HeapMonotonic) {
  const int N = 1000;
  StHeapList<Node> h(N + 8);
  ASSERT_TRUE(h.HeapResize(N + 8) >= 0);
  Node **nodes = new Node *[N];
  for (int i = 0; i < N; i++) {
    nodes[i] = new Node((int64_t)((i * 1103515245u + 12345u) % 9973));
    ASSERT_TRUE(h.HeapPush(nodes[i]) >= 0);
  }
  int64_t last = -0x7fffffffffffffffLL - 1;
  for (int i = 0; i < N; i++) {
    Node *p = h.HeapPop();
    ASSERT_TRUE(p != NULL && p->m_v_ >= last);
    last = p->m_v_;
  }
  ASSERT_TRUE(h.HeapEmpty());
  for (int i = 0; i < N; i++) {
    delete nodes[i];
  }
  delete[] nodes;
}

TEST(StStatus, HeapFullAndDoublePush) {
  StHeapList<Node> h(1);
  Node *nodes[512];
  Node extra(7);
  Node *top;
  int i;
  /* 构造下限是 512。更小的 Resize 直接返回。满堆时重复插入先返回 -1。 */
  ASSERT_TRUE(h.HeapResize(10) == 0);
  nodes[0] = new Node(0);
  ASSERT_TRUE(h.HeapPush(nodes[0]) == 0);
  ASSERT_TRUE(h.HeapPush(nodes[0]) == -2);
  for (i = 1; i < 512; i++) {
    nodes[i] = new Node((int64_t)i);
    ASSERT_TRUE(h.HeapPush(nodes[i]) == 0);
  }
  ASSERT_TRUE(h.HeapFull());
  ASSERT_TRUE(h.HeapPush(&extra) == -1);
  ASSERT_TRUE(h.HeapDelete(&extra) == -2);
  h.HeapForeach();
  top = h.HeapPop();
  ASSERT_TRUE(top == nodes[0]);
  ASSERT_TRUE(h.HeapDelete(nodes[511]) == 0);
  while (!h.HeapEmpty()) {
    h.HeapPop();
  }
  ASSERT_TRUE(h.HeapPop() == NULL);
  ASSERT_TRUE(h.HeapDelete(&extra) == -1);
  for (i = 0; i < 512; i++) {
    delete nodes[i];
  }
}

TEST(StStatus, TimerEdges) {
  StHeapTimer timer(16);
  CountTimer live;
  CountTimer future;
  ASSERT_TRUE(timer.Startup(NULL, 0) == false);
  timer.Stop(NULL);
  ASSERT_TRUE(live.IsExpired() == false);
  ASSERT_TRUE(timer.Startup(&live, 0));
  ASSERT_TRUE(timer.CheckExpired() == 1);
  ASSERT_TRUE(live.n_ == 1);
  ASSERT_TRUE(timer.Startup(&future, 60000));
  ASSERT_TRUE(timer.Startup(&future, 60000) == false);
  ASSERT_TRUE(timer.CheckExpired() == 0);
  ASSERT_TRUE(future.IsExpired() == false);
  timer.Stop(&future);
  timer.Stop(&future);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  return RUN_ALL_TESTS();
}
