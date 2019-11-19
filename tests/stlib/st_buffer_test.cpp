#include "stlib/st_buffer.h"
#include "stlib/st_util.h"
#include "stlib/st_test.h"

using namespace stlib;

TEST(StStatus, StBuffer)
{
	StBuffer *buf1 = GetInstance<StBufferPool>()->GetBuffer(100);
	LOG_DEBUG("buf1: %p, addr : %p", buf1, GetInstance<StBufferPool>());

	StBuffer *buf2 = GetInstance<StBufferPool>()->GetBuffer(1000);
	LOG_DEBUG("buf2: %p, addr : %p", buf2, GetInstance<StBufferPool>());

	StBuffer *buf3 = GetInstance<StBufferPool>()->GetBuffer(10000);
	LOG_DEBUG("buf3: %p, addr : %p", buf3, GetInstance<StBufferPool>());

	StBuffer *buf4 = GetInstance<StBufferPool>()->GetBuffer(100);
	LOG_DEBUG("buf4: %p, addr : %p", buf4, GetInstance<StBufferPool>());

	GetInstance<StBufferPool>()->FreeBuffer(buf1);
	GetInstance<StBufferPool>()->FreeBuffer(buf4);

	StBuffer *buf5 = GetInstance<StBufferPool>()->GetBuffer(100);
	LOG_DEBUG("buf5 : %p, addr : %p", buf5, GetInstance<StBufferPool>());
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    return RUN_ALL_TESTS();
}