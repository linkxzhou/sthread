#include "gtest/googletest/include/gtest/gtest.h"
#include "../include/mt_heap_timer.h"

MTHREAD_NAMESPACE_USING

TEST(TimerNotifyTest, notify)
{
	TimerCtrl c;

	c.StartTimer(new TimerNotify(), 100);
	c.StartTimer(new TimerNotify(), 160);
	c.StartTimer(new TimerNotify(), 1500);
	c.StartTimer(new TimerNotify(), 99);
	c.StartTimer(new TimerNotify(), 20);
	c.StartTimer(new TimerNotify(), 5000);
	c.StartTimer(new TimerNotify(), 999);
	c.StartTimer(new TimerNotify(), 900);
	c.StartTimer(new TimerNotify(), 10000);

	LOG_TRACE("==========================");
	int count = 10;
	while(count-- > 0)
	{
		c.CheckExpired();

		utime64_t now_ms = Utils::system_ms();
        LOG_TRACE("now_ms = %llu", now_ms);

		Utils::system_sleep(1);
	}
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}