#include "../include/st_manager.h"

ST_NAMESPACE_USING

class TestA;

typedef CPP_TAILQ_HEAD<TestA>     TestAQueue;
typedef CPP_TAILQ_ENTRY<TestA>    TestANext;

class TestA
{
public:
    void print()
    {
        LOG_TRACE("print TestA:%p", this);
    }

public:
    TestANext m_next_;
};

TEST(StStatus, session)
{
	TestAQueue queue;
    CPP_TAILQ_INIT(&queue);

    TestA *a1 = new TestA();
    CPP_TAILQ_INSERT_TAIL(&queue, a1, m_next_);
    TestA *a2 = new TestA();
    CPP_TAILQ_REMOVE(&queue, a1, m_next_);
    CPP_TAILQ_INSERT_TAIL(&queue, a1, m_next_);
    TestA *a3 = new TestA();
    CPP_TAILQ_INSERT_TAIL(&queue, a3, m_next_);
    TestA *a4 = new TestA();
    CPP_TAILQ_INSERT_TAIL(&queue, a4, m_next_);

    TestA *item = NULL; 
    CPP_TAILQ_FOREACH(item, &queue, m_next_)
    {
        item->print();
    }

    int m_stack_size_ = MEM_PAGE_SIZE + 128;
    int memsize = MEM_PAGE_SIZE * 2 + (m_stack_size_ / MEM_PAGE_SIZE + 1) * MEM_PAGE_SIZE;
    int memsize1 = MEM_PAGE_SIZE * 2 + m_stack_size_;
    memsize1 = (memsize1 + MEM_PAGE_SIZE - 1) / MEM_PAGE_SIZE * MEM_PAGE_SIZE;

    LOG_TRACE("memsize : %d, memsize1 : %d", memsize, memsize1);
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    return RUN_ALL_TESTS();
}