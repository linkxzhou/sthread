#include "gtest/googletest/include/gtest/gtest.h"
#include "../include/mt_action.h"
#include "../include/mt_c.h"

MTHREAD_NAMESPACE_USING

class TestIMtAction: public IMtAction
{
public:
    virtual int HandleEncode(void* buf, int& len, IMessage* msg)
    {
        char buf1[10240] = "GET / HTTP/1.1\r\nHost: www.baidu.com\r\nUser-Agent: curl/7.54.0\r\nAccept: */*\r\n\r\n";
        len = strlen(buf1);
        memcpy(buf, buf1, len);
        ((char *)buf)[len] = '\0';

        LOG_TRACE("send : %s", buf1);
        
        return 0;
    }
    virtual int HandleInput(void* buf, int len, IMessage* msg)
    {
        LOG_TRACE("buf : %s", (char*)buf);
        return strlen((char*)buf);
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

TEST(ActionTest, action)
{
    //定义sockaddr_in
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    // servaddr.sin_port = htons(4312);  ///服务器端口
    // servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");  ///服务器ip
    servaddr.sin_port = htons(80);  ///服务器端口
    servaddr.sin_addr.s_addr = inet_addr("117.185.16.31");  ///服务器ip

    int ret = mt_init_frame();
    LOG_TRACE("init ret : %d", ret);
    mt_set_hook_flag();

	IMtAction* action = new IMtAction();

    LOG_TRACE("action : %p", action);

    ret = action->InitConnection();

    LOG_TRACE("InitConnection ret : %d", ret);

    ret = action->DoEncode();

    LOG_TRACE("DoEncode ret : %d", ret);
}

TEST(ActionListTest, action)
{
    //定义sockaddr_in
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    // servaddr.sin_port = htons(4312);  ///服务器端口
    // servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");  ///服务器ip
    servaddr.sin_port = htons(80);  ///服务器端口
    servaddr.sin_addr.s_addr = inet_addr("117.185.16.31");  ///服务器ip

    int ret = mt_init_frame();
    LOG_TRACE("init ret : %d, servaddr : %p", ret, &servaddr);
    mt_set_hook_flag();

    IMtActionFrame *actionframe = GetInstance<IMtActionFrame>();

	IMtAction* action1 = new TestIMtAction();
    action1->SetMsgDstAddr(&servaddr);
    action1->SetConnType(eTCP_SHORT_CONN);
    actionframe->Add(action1);

    IMtAction* action2 = new TestIMtAction();
    action2->SetMsgDstAddr(&servaddr);
    action2->SetConnType(eTCP_SHORT_CONN);
    actionframe->Add(action2);

    int count = 5;
    while (count-- > 0)
    {
        ret = actionframe->SendRecv(1000);
        LOG_TRACE("ret : %d", ret);
    }
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}