#include "gtest/googletest/include/gtest/gtest.h"
#include "../include/st_heap_timer.h"

ST_NAMESPACE_USING

TEST(TimerEntryTest, notify)
{
	HeapTimer c;

	c.Start(new TimerEntry(), 100);
	c.Start(new TimerEntry(), 160);
	c.Start(new TimerEntry(), 1500);
	c.Start(new TimerEntry(), 99);
	c.Start(new TimerEntry(), 20);
	c.Start(new TimerEntry(), 5000);
	c.Start(new TimerEntry(), 999);
	c.Start(new TimerEntry(), 900);
	c.Start(new TimerEntry(), 10000);

	LOG_TRACE("==========================");
	int count = 10;
	while(count-- > 0)
	{
		c.CheckExpired();

		int64_t now_ms = Util::SysMs();
        LOG_TRACE("now_ms = %llu", now_ms);

		Util::SysSleep(1);
	}
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}