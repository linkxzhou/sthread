#include "../include/st_server.h"

ST_NAMESPACE_USING

TEST(StStatus, accept)
{
    StServer<Thread, 
        StEventItem, 
        StServerConnection, 
        TCP_SERVER> *server = 
        new StServer<Thread, 
            StEventItem, 
            StServerConnection, 
            TCP_SERVER>();
    server->SetHookFlag();

    StNetAddress addr;
    addr.SetAddr("127.0.0.1", 8001);
    int fd = server->CreateSocket(addr);
    LOG_TRACE("fd: %d", fd);
    if (!server->Listen())
    {
        LOG_ERROR("listen error!!!");
        return ;
    }
    
    server->Loop();
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    return RUN_ALL_TESTS();
}