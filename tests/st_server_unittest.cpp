#include "../include/st_server.h"

ST_NAMESPACE_USING

class StTcpServerConnection : public StServerConnection
{
    virtual int Process(void *manager)
    {
        int ret = RecvData();
        LOG_TRACE("ret: %d", ret);
        if (ret < 0)
        {
            return -1;
        }

        char buf[1024];
        sprintf(buf, "HTTP/1.1 200 OK\r\n"
            "Content-Length: 1\r\n"
            "Content-Type: text/html\r\n"
            "Date: Mon, 12 Aug 2019 15:48:13 GMT\r\n"
            "Server: sthread/1.0.1\r\n\r\n1");

        GetSendBuffer()->SetBuffer(buf, strlen(buf) + 1);
        ret = SendData();
        LOG_TRACE("ret: %d", ret);
        if (ret < 0)
        {
            return -2;
        }

        return 1;
    }
};

TEST(StStatus, accept)
{
    // LOG_LEVEL(LLOG_WARN);

    StServer<StTcpServerConnection, eTCP_CONN> *server = 
        new StServer<StTcpServerConnection, eTCP_CONN>();
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