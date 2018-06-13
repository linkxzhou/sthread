#include "gtest/googletest/include/gtest/gtest.h"
#include "../include/mt_heap.h"

MTHREAD_NAMESPACE_USING

class TestHeap : public HeapEntry
{
public:
	virtual unsigned long long HeapValue()
	{
		return data;
	}

public:
	int data;
};

typedef TestHeap * TestHeapNode;

TEST(HeapListTest, compare)
{
	HeapList<TestHeap> *h = new HeapList<TestHeap>();
	TestHeapNode n1 = new TestHeap();
	n1->data = 19;
	h->HeapPush(n1);

	TestHeapNode n2 = new TestHeap();
	n2->data = 1;
	h->HeapPush(n2);

	TestHeapNode n3 = new TestHeap();
	n3->data = 10;
	h->HeapPush(n3);

	TestHeapNode n4 = new TestHeap();
	n4->data = 4;
	h->HeapPush(n4);

	TestHeapNode n5 = new TestHeap();
	n5->data = 400;
	h->HeapPush(n5);

	TestHeapNode n6 = new TestHeap();
	n6->data = 2;
	h->HeapPush(n6);

	h->HeapForeach();

	h->HeapDelete(n4);

	TestHeapNode n7 = new TestHeap();
	n7->data = 500;
	h->HeapPush(n7);

	TestHeapNode n8 = new TestHeap();
	n8->data = 13;
	h->HeapPush(n8);

	TestHeapNode n9 = new TestHeap();
	n9->data = 15;
	h->HeapPush(n9);

	h->HeapDelete(n7);

	TestHeapNode n10 = new TestHeap();
	n10->data = 17;
	h->HeapPush(n10);

	h->HeapForeach();

	h->HeapDelete(n10);

	h->HeapForeach();
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}