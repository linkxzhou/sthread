#include "gtest/googletest/include/gtest/gtest.h"
#include "../include/mt_thread.h"
#include "../include/mt_frame.h"

ST_NAMESPACE_USING

Context	gcontext;
Thread *t1, *t2, *t3;

TEST(ThreadTest, thread)
{
    Frame* frame = GetInstance<Frame>();
	frame->InitFrame();

    GetInstance<ThreadPool>()->InitialPool(10000);
	t1 = GetInstance<ThreadPool>()->AllocThread();
    t2 = GetInstance<ThreadPool>()->AllocThread();
    t3 = GetInstance<ThreadPool>()->AllocThread();

    LOG_TRACE("t1 : %p, t2 : %p, t3 : %p", t1, t2, t3);
    contextswitch(&t1->GetStack()->m_context_, &t2->GetStack()->m_context_);
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}