#include "gtest/googletest/include/gtest/gtest.h"
#include "../include/st_thread.h"

ST_NAMESPACE_USING

TEST(ThreadTest, thread)
{
    Thread *t1 = new Thread(), *t2 = new Thread(), *t3 = new Thread();
    LOG_TRACE("(t1, t2, t3 : %p, %p, %p)", t1, t2, t3);
    t1->SetName("TEST");
    t2->SetName("Daemon");
    t3->SetName("Primo");
    EventScheduler *s1 = new EventScheduler();
    ThreadScheduler *s2 = new ThreadScheduler(100, t2, t3);
    t1->Create(s1, s2);
    t2->Create(s1, s2);
    t3->Create(s1, s2);

    t1->Run();
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}