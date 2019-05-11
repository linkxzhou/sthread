#include "gtest/googletest/include/gtest/gtest.h"
#include "../include/mt_action.h"

MTHREAD_NAMESPACE_USING

static unsigned int check(void* buf, int len)
{
    LOG_TRACE("buf : %s, len : %d", buf, len);
    return len;
}

class TestIMtAction: public IMtAction
{
public:
    virtual int HandleEncode(void* buf, int& len, IMessage* msg)
    {
        // ================== 直接发送回包信息 ==================
        char send_buf[8192] = "HTTP/1.1 200 OK\r\n"
            "Accept-Ranges: bytes\r\n"
            "Cache-Control: private, no-cache, no-store, proxy-revalidate, no-transform\r\n"
            "Connection: Keep-Alive\r\n"
            "Content-Length: 1\r\n"
            "Content-Type: text/html\r\n"
            "Date: Mon, 20 Aug 2018 14:54:06 GMT\r\n"
            "Etag: 588604eb-94d\r\n"
            "Last-Modified: Mon, 23 Jan 2017 13:28:11 GMT\r\n"
            "Pragma: no-cache\r\n\r\n1";

        len = strlen(send_buf);
        memcpy(buf, send_buf, len);
        ((char *)buf)[len] = '\0';
        LOG_DEBUG("[send] buf : %s", send_buf);

        // ================== proxy的方式 ==================
        // struct sockaddr_in servaddr;
        // memset(&servaddr, 0, sizeof(servaddr));
        // servaddr.sin_family = AF_INET;
        // servaddr.sin_port = htons(80);  ///服务器端口
        // servaddr.sin_addr.s_addr = inet_addr("117.185.16.31");  ///服务器ip
        // char buf1[10240] = "GET / HTTP/1.1\r\nHost: www.baidu.com\r\nUser-Agent: curl/7.54.0\r\nAccept: */*\r\n\r\n";
        // char buf2[10240] = {'\0'};
        // int buf2_recv = sizeof(buf2);
        // LOG_TRACE("send: %s", buf1);
        // int ret = tcp_sendrecv(&servaddr, buf1, strlen(buf1), (void *)buf2, buf2_recv, 10000, check);
        // LOG_TRACE("recv: %s, buf2_recv: %d", buf2, buf2_recv);
        // LOG_TRACE("tcp_sendrecv ret : %d", ret);

        // len = strlen(buf2);
        // memcpy(buf, buf2, len);
        // ((char *)buf)[len] = '\0';

        return 0;
    }
    virtual int HandleInput(void* buf, int len, IMessage* msg)
    {
        LOG_DEBUG("[recv] buf: %s", (char*)buf);

        return len;
    }
    virtual int HandleProcess(void* buf, int len, IMessage* msg)
    {
        return 0;
    }
    virtual int HandleError(int err, IMessage* msg)
    {
        return 0;
    }
};

// 回调函数
static IMtAction* construct()
{
    IMtAction *action = new TestIMtAction();
    action->SetConnType(eTCP_SHORT_CONN);
    return action;
}

static void destruct(IMtAction *action)
{
    if (action)
    {
        action->Reset(true);
        safe_delete(action);
    }
}

TEST(C1, c_tcpshort)
{
	//定义sockaddr_in
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(4312);  ///服务器端口
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");  ///服务器ip

    int ret = mt_init_frame();
    LOG_TRACE("============= init ret : %d =============", ret);
    mt_set_hook_flag();
    // mt_set_timeout(1000);

    IMtActionServer *server = new IMtActionServer(&servaddr, eTCP_ACCEPT_CONN);
    server->SetCallBack(construct, destruct);
    ret = server->Loop(1000);
    if (ret < 0)
    {
        LOG_ERROR("server error");
    }

    LOG_TRACE("end ...");
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}