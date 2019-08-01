#include "gtest/googletest/include/gtest/gtest.h"
#include "../include/st_buffer.h"

ST_NAMESPACE_USING

TEST(MessageTest, StMsgBuffer)
{
	StMsgBuffer* buf1 = GetInstance<StMsgBufferPool>()->GetMsgBuffer(100);
	LOG_DEBUG("buf1 : %p, addr : %p", buf1, GetInstance<StMsgBufferPool>());

	StMsgBuffer* buf2 = GetInstance<StMsgBufferPool>()->GetMsgBuffer(1000);
	LOG_DEBUG("buf2 : %p, addr : %p", buf2, GetInstance<StMsgBufferPool>());

	StMsgBuffer* buf3 = GetInstance<StMsgBufferPool>()->GetMsgBuffer(10000);
	LOG_DEBUG("buf3 : %p, addr : %p", buf3, GetInstance<StMsgBufferPool>());

	StMsgBuffer* buf4 = GetInstance<StMsgBufferPool>()->GetMsgBuffer(100);
	LOG_DEBUG("buf4 : %p, addr : %p", buf4, GetInstance<StMsgBufferPool>());

	GetInstance<StMsgBufferPool>()->FreeMsgBuffer(buf1);
	GetInstance<StMsgBufferPool>()->FreeMsgBuffer(buf4);

	StMsgBuffer* buf5 = GetInstance<StMsgBufferPool>()->GetMsgBuffer(100);
	LOG_DEBUG("buf5 : %p, addr : %p", buf5, GetInstance<StMsgBufferPool>());
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}