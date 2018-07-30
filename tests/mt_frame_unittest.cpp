#include "gtest/googletest/include/gtest/gtest.h"
#include "../include/mt_frame.h"

MTHREAD_NAMESPACE_USING

TEST(FrameTest, frame)
{
	// Frame* frame = GetInstance<Frame>();
	// frame->InitFrame();

	// Frame::DaemonRun(NULL);
}

TEST(FrameTest, frame_async)
{
	Frame* frame = GetInstance<Frame>();
	frame->InitFrame();
    frame->SetHookFlag();

    EventProxyer* proxyer = frame->GetEventProxyer();
	Eventer* ev = GetInstance<ISessionEventerPool>()->GetEventer(eEVENT_THREAD);
    ev->SetOwnerProxyer(proxyer);
    LOG_TRACE("ev node :%p", ev);
    if (NULL == ev)
    {
        LOG_ERROR("get ev failed");
        return ;
    }

    //定义sockaddr_in
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    // servaddr.sin_port = htons(80);  ///服务器端口
    // servaddr.sin_addr.s_addr = inet_addr("14.215.177.39");  ///服务器ip
    servaddr.sin_port = htons(4312);  ///服务器端口
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");  ///服务器ip

    TcpShortClientConnection* conn = dynamic_cast<TcpShortClientConnection*>(GetInstance<ConnectionPool>()->GetConnection(eTCP_SHORT_CLIENT_CONN, &servaddr));
    LOG_TRACE("TcpKeepClientConnection conn : %p", conn);
    if (NULL == conn)
    {
        LOG_ERROR("get connection failed, dst[%p]", &servaddr);
        GetInstance<ISessionEventerPool>()->FreeEventer(ev);
        return ;
    }
    conn->SetEventer(ev);
    LOG_TRACE("ev : %p", ev);

    int socket_fd = conn->CreateSocket();
    proxyer->AddEventer(ev);

    // int nSendBuf = 20;// 设置为20字节
    // int ret = mt_setsockopt(socket_fd, SOL_SOCKET, SO_SNDBUF,(const char*)&nSendBuf, sizeof(int));
    // LOG_TRACE("[mt_setsockopt]socket_fd : %d, ret : %d", socket_fd, ret);
    
    LOG_TRACE("socket_fd :%d", socket_fd);
    if (socket_fd < 0)
    {
        GetInstance<ConnectionPool>()->FreeConnection(conn, true);
        GetInstance<ISessionEventerPool>()->FreeEventer(ev);
        LOG_ERROR("create socket failed, ret[%d]", socket_fd);
        return ;
    }

    int ret = Frame::connect(socket_fd, (struct sockaddr *)&servaddr, sizeof(servaddr), 10000);
    LOG_TRACE("socket_fd : %d, ret : %d", socket_fd, ret);

    // proxyer->Dispatch();

    char buf1[10240] = "GET / HTTP/1.1\r\nHost: www.baidu.com\r\nUser-Agent: curl/7.54.0\r\nAccept: */*\r\n";
    char buf2[10240] = {'\0'};
    ret = Frame::send(socket_fd, buf1, strlen(buf1), 0, 10000);
    LOG_TRACE("socket_fd : %d, ret : %d, buf : %s", socket_fd, ret, buf1);
    // ret = Frame::send(socket_fd, buf1, 0, 0, 10000);
    // LOG_TRACE("socket_fd : %d, ret : %d, buf : %s", socket_fd, ret, buf1);

    // proxyer->Dispatch();
 	
 	ret = Frame::recv(socket_fd, buf2, sizeof(buf2), 0, 10000);
    
    LOG_TRACE("recved socket_fd : %d, ret : %d, buf : %s", socket_fd, ret, buf2);

    // proxyer->Dispatch();
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}