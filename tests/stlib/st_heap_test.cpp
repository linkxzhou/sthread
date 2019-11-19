#include "stlib/st_heap.h"
#include "stlib/st_test.h"

using namespace stlib;

class TestHeap : public StHeap
{
public:
	virtual int64_t HeapValue()
	{
		return m_data_;
	}

    virtual void HeapIterate()
    {
        fprintf(stdout, "%lld ", m_data_);
        return ;
    }

public:
	int64_t m_data_;
};

typedef TestHeap * TestHeapNode;

TEST(StStatus, compare)
{
	StHeapList<TestHeap> *h = new StHeapList<TestHeap>();
	TestHeapNode n1 = new TestHeap();
	n1->m_data_ = 19;
	h->HeapPush(n1);

	TestHeapNode n2 = new TestHeap();
	n2->m_data_ = 1;
	h->HeapPush(n2);

	TestHeapNode n3 = new TestHeap();
	n3->m_data_ = 10;
	h->HeapPush(n3);

	TestHeapNode n4 = new TestHeap();
	n4->m_data_ = 4;
	h->HeapPush(n4);

	TestHeapNode n5 = new TestHeap();
	n5->m_data_ = 400;
	h->HeapPush(n5);

	TestHeapNode n6 = new TestHeap();
	n6->m_data_ = 2;
	h->HeapPush(n6);

	h->HeapForeach();
    fprintf(stdout, "\n");

	h->HeapDelete(n4);

	TestHeapNode n7 = new TestHeap();
	n7->m_data_ = 500;
	h->HeapPush(n7);

	TestHeapNode n8 = new TestHeap();
	n8->m_data_ = 13;
	h->HeapPush(n8);

	TestHeapNode n9 = new TestHeap();
	n9->m_data_ = 15;
	h->HeapPush(n9);

	h->HeapDelete(n7);

	TestHeapNode n10 = new TestHeap();
	n10->m_data_ = -17;
	h->HeapPush(n10);

	h->HeapForeach();
    fprintf(stdout, "\n");

	h->HeapDelete(n10);

	h->HeapForeach();
    fprintf(stdout, "\n");
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    return RUN_ALL_TESTS();
}