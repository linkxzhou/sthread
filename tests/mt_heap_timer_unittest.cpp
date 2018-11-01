#include "gtest/googletest/include/gtest/gtest.h"
#include "../include/mt_heap_timer.h"

MTHREAD_NAMESPACE_USING

TEST(TimerEntryTest, notify)
{
	HeapTimer c;

	c.StartTimer(new TimerEntry(), 100);
	c.StartTimer(new TimerEntry(), 160);
	c.StartTimer(new TimerEntry(), 1500);
	c.StartTimer(new TimerEntry(), 99);
	c.StartTimer(new TimerEntry(), 20);
	c.StartTimer(new TimerEntry(), 5000);
	c.StartTimer(new TimerEntry(), 999);
	c.StartTimer(new TimerEntry(), 900);
	c.StartTimer(new TimerEntry(), 10000);

	LOG_TRACE("==========================");
	int count = 10;
	while(count-- > 0)
	{
		c.CheckExpired();

		time64_t now_ms = Utils::system_ms();
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