#include "../include/st_connection.h"
#include "../include/st_manager.h"

ST_NAMESPACE_USING

TEST(StStatus, TCP)
{
    Manager *manager = GetInstance< Manager >();
    manager->SetHookFlag();

	StClientConnection<StEventBase> *conn = new StClientConnection<StEventBase>();
    StNetAddress addr;
    addr.SetAddr("112.80.248.75", 80);
    int fd = conn->CreateSocket(addr);
    LOG_TRACE("fd: %d", fd);
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    return RUN_ALL_TESTS();
}