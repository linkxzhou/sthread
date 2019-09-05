#include "../include/st_thread.h"
#include "../include/st_manager.h"

ST_NAMESPACE_USING

TEST(StStatus, ThreadScheduler)
{
    Thread *t1 = new Thread(), *t2 = new Thread(), *t3 = new Thread();
    LOG_TRACE("(t1, t2, t3 : %p, %p, %p)", t1, t2, t3);

    t1->SetName("TEST1");
    t2->SetName("Daemon");
    t3->SetName("Primo");

    ThreadScheduler *s2 = GetThreadScheduler();
    s2->SetDaemonThread(t2);
    s2->SetPrimoThread(t3);
    s2->ResetHeapSize(100);

    // 休眠3s
    t1->Sleep(3000);

    LOG_TRACE("Schedule ###");
    s2->Yield(t1);
    LOG_TRACE("Sleep ###");
    s2->Sleep(t1);
    LOG_TRACE("Pend ###");
    s2->Pend(t1);
}

TEST(StStatus, EventScheduler)
{
    Thread *t1 = new Thread(), *t2 = new Thread(), *t3 = new Thread();
    LOG_TRACE("(t1, t2, t3 : %p, %p, %p)", t1, t2, t3);

    t1->SetName("TEST1");
    t2->SetName("Daemon");
    t3->SetName("Primo");

    ThreadScheduler *s2 = GetThreadScheduler();
    s2->SetDaemonThread(t2);
    s2->SetPrimoThread(t3);
    s2->ResetHeapSize(100);

    EventScheduler *s3 = GetEventScheduler();
    s3->Wait(3000);
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    return RUN_ALL_TESTS();
}