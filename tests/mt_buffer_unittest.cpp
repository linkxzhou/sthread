#include "gtest/googletest/include/gtest/gtest.h"
#include "../include/mt_buffer.h"

MTHREAD_NAMESPACE_USING

TEST(MessageTest, IMtMsgBuffer)
{
	IMtMsgBuffer* buf1 = GetInstance<IMsgBufferPool>()->GetMsgBuffer(100);
	LOG_DEBUG("buf1 : %p, addr : %p", buf1, GetInstance<IMsgBufferPool>());

	IMtMsgBuffer* buf2 = GetInstance<IMsgBufferPool>()->GetMsgBuffer(1000);
	LOG_DEBUG("buf2 : %p, addr : %p", buf2, GetInstance<IMsgBufferPool>());

	IMtMsgBuffer* buf3 = GetInstance<IMsgBufferPool>()->GetMsgBuffer(10000);
	LOG_DEBUG("buf3 : %p, addr : %p", buf3, GetInstance<IMsgBufferPool>());

	IMtMsgBuffer* buf4 = GetInstance<IMsgBufferPool>()->GetMsgBuffer(100);
	LOG_DEBUG("buf4 : %p, addr : %p", buf4, GetInstance<IMsgBufferPool>());

	GetInstance<IMsgBufferPool>()->FreeMsgBuffer(buf1);
	GetInstance<IMsgBufferPool>()->FreeMsgBuffer(buf4);

	IMtMsgBuffer* buf5 = GetInstance<IMsgBufferPool>()->GetMsgBuffer(100);
	LOG_DEBUG("buf5 : %p, addr : %p", buf5, GetInstance<IMsgBufferPool>());
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}