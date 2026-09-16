#include "stlib/st_heap.h"
#include "stlib/st_test.h"
#include "stlib/st_util.h"

#include <stdio.h>
#include <sys/time.h>

using namespace stlib;

class TestHeap : public StHeap {
public:
  virtual int64_t HeapValue() { return m_data_; }

  virtual void HeapIterate() { fprintf(stdout, "%lld ", (long long)m_data_); }

public:
  int64_t m_data_;
};

typedef TestHeap *TestHeapNode;

static uint64_t NowUs() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

TEST(StStatus, compare) {
  StHeapList<TestHeap> *h = new StHeapList<TestHeap>();
  TestHeapNode n1 = new TestHeap();
  n1->m_data_ = 19;
  ASSERT_TRUE(h->HeapPush(n1) == 0);

  TestHeapNode n2 = new TestHeap();
  n2->m_data_ = 1;
  ASSERT_TRUE(h->HeapPush(n2) == 0);

  TestHeapNode n3 = new TestHeap();
  n3->m_data_ = 10;
  ASSERT_TRUE(h->HeapPush(n3) == 0);

  TestHeapNode n4 = new TestHeap();
  n4->m_data_ = 4;
  ASSERT_TRUE(h->HeapPush(n4) == 0);

  TestHeapNode n5 = new TestHeap();
  n5->m_data_ = 400;
  ASSERT_TRUE(h->HeapPush(n5) == 0);

  TestHeapNode n6 = new TestHeap();
  n6->m_data_ = 2;
  ASSERT_TRUE(h->HeapPush(n6) == 0);

  ASSERT_TRUE(h->HeapTop()->m_data_ == 1);

  h->HeapForeach();
  fprintf(stdout, "\n");

  ASSERT_TRUE(h->HeapDelete(n4) == 0);

  TestHeapNode n7 = new TestHeap();
  n7->m_data_ = 500;
  ASSERT_TRUE(h->HeapPush(n7) == 0);

  TestHeapNode n8 = new TestHeap();
  n8->m_data_ = 13;
  ASSERT_TRUE(h->HeapPush(n8) == 0);

  TestHeapNode n9 = new TestHeap();
  n9->m_data_ = 15;
  ASSERT_TRUE(h->HeapPush(n9) == 0);

  ASSERT_TRUE(h->HeapDelete(n7) == 0);

  TestHeapNode n10 = new TestHeap();
  n10->m_data_ = -17;
  ASSERT_TRUE(h->HeapPush(n10) == 0);
  ASSERT_TRUE(h->HeapTop()->m_data_ == -17);

  h->HeapForeach();
  fprintf(stdout, "\n");

  ASSERT_TRUE(h->HeapDelete(n10) == 0);
  ASSERT_TRUE(h->HeapTop()->m_data_ == 1);

  h->HeapForeach();
  fprintf(stdout, "\n");

  int64_t last = -0x7fffffffffffffffLL - 1;
  while (!h->HeapEmpty()) {
    TestHeap *p = h->HeapPop();
    ASSERT_TRUE(p != NULL);
    ASSERT_TRUE(p->HeapValue() >= last);
    last = p->HeapValue();
  }
}

TEST(StStatus, HeapMonotonic100k) {
  const int N = 100000;
  StHeapList<TestHeap> h(N + 16);
  ASSERT_TRUE(h.HeapResize(N + 16) >= 0);

  TestHeap *nodes = new TestHeap[N];
  uint64_t t0 = NowUs();
  for (int i = 0; i < N; i++) {
    nodes[i].m_data_ = (int64_t)((i * 2654435761u) % 1000003);
    ASSERT_TRUE(h.HeapPush(&nodes[i]) == 0);
  }
  uint64_t t1 = NowUs();

  int64_t last = -0x7fffffffffffffffLL - 1;
  for (int i = 0; i < N; i++) {
    TestHeap *p = h.HeapPop();
    ASSERT_TRUE(p != NULL);
    ASSERT_TRUE(p->HeapValue() >= last);
    last = p->HeapValue();
  }
  uint64_t t2 = NowUs();
  ASSERT_TRUE(h.HeapEmpty());

  fprintf(stdout, "heap_bench N=%d push_us=%llu pop_us=%llu\n", N,
          (unsigned long long)(t1 - t0), (unsigned long long)(t2 - t1));
  delete[] nodes;
}

int main(int argc, char *argv[]) { return RUN_ALL_TESTS(); }
