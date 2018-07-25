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
        char buf1[10240] = "GET / HTTP/1.1\r\nHost: www.baidu.com\r\nUser-Agent: curl/7.54.0\r\nAccept: */*\r\n\r\n";
        len = strlen(buf1);
        memcpy(buf, buf1, len);
        ((char *)buf)[len] = '\0';

        LOG_DEBUG("send : %s", buf1);

        // //定义sockaddr_in
        // struct sockaddr_in servaddr;
        // memset(&servaddr, 0, sizeof(servaddr));
        // servaddr.sin_family = AF_INET;
        // servaddr.sin_port = htons(80);  ///服务器端口
        // servaddr.sin_addr.s_addr = inet_addr("117.185.16.31");  ///服务器ip
        // // 发起其他请求
        // char buf2[10240] = {'\0'};
        // int buf2_recv = sizeof(buf2);
        // LOG_TRACE("send : %s", buf1);
        // int ret = tcp_sendrecv(&servaddr, buf1, strlen(buf1), (void *)buf2, buf2_recv, 10000, check);
        // LOG_TRACE("recv : %s, buf2_recv : %d", buf2, buf2_recv);
        // LOG_TRACE("tcp_sendrecv ret : %d", ret);
        
        return 0;
    }
    virtual int HandleInput(void* buf, int len, IMessage* msg)
    {
        LOG_DEBUG("buf : %s", (char*)buf);

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

static void* thread_func(void *)
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

    IMtActionClient *actionframe = GetInstance<IMtActionClient>();

    IMtAction* action1 = new TestIMtAction();
    action1->SetMsgDstAddr(&servaddr);
    action1->SetConnType(eTCP_SHORT_CLIENT_CONN);
    actionframe->Add(action1);

    IMtAction* action2 = new TestIMtAction();
    action2->SetMsgDstAddr(&servaddr);
    action2->SetConnType(eTCP_SHORT_CLIENT_CONN);
    actionframe->Add(action2);
    
    LOG_TRACE("wait thread : %d", GetInstance<Frame>()->m_wait_num_);

    int count = 1;
    while (count-- > 0)
    {
        ret = actionframe->SendRecv(1000);
        LOG_TRACE("ret : %d", ret);
    }

    LOG_TRACE("thread id : %d, frame id : %p", pthread_self(), GetInstance<Frame>());

    return NULL;
}

// TEST(ActionTest, action)
// {
//     //定义sockaddr_in
//     struct sockaddr_in servaddr;
//     memset(&servaddr, 0, sizeof(servaddr));
//     servaddr.sin_family = AF_INET;
//     // servaddr.sin_port = htons(4312);  ///服务器端口
//     // servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");  ///服务器ip
//     servaddr.sin_port = htons(80);  ///服务器端口
//     servaddr.sin_addr.s_addr = inet_addr("117.185.16.31");  ///服务器ip

//     int ret = mt_init_frame();
//     LOG_TRACE("init ret : %d", ret);
//     mt_set_hook_flag();

// 	IMtAction* action = new IMtAction();

//     LOG_TRACE("action : %p", action);

//     ret = action->InitConnection();

//     LOG_TRACE("InitConnection ret : %d", ret);

//     ret = action->DoEncode();

//     LOG_TRACE("DoEncode ret : %d", ret);
// }

TEST(ActionListTest1, action)
{
    thread_func(NULL);
}

// TEST(ActionListTest2, action)
// {
//     pthread_t tid1, tid2;
//     int err = pthread_create(&tid1, NULL, thread_func, NULL);  
//     if (err != 0)
//     {  
//         LOG_ERROR("fail to create thread");  
//         return ;
//     }
//     LOG_TRACE("pthread_create err : %d", err);

//     err = pthread_create(&tid2, NULL, thread_func, NULL);  
//     if (err != 0)
//     {  
//         LOG_ERROR("fail to create thread");    
//         return ;
//     }
//     LOG_TRACE("pthread_create err : %d", err);

//     pthread_join(tid1, NULL);
//     pthread_join(tid2, NULL);
// }

// TEST(ActionListTest3, server1)
// {
//     struct sockaddr_in servaddr;
//     memset(&servaddr, 0, sizeof(servaddr));
//     servaddr.sin_family = AF_INET;
//     servaddr.sin_port = htons(4312);  ///服务器端口
//     servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");  ///服务器ip

//     int ret = mt_init_frame();
//     LOG_TRACE("init ret : %d, servaddr : %p", ret, &servaddr);
//     mt_set_hook_flag();
//     mt_set_timeout(30000);

//     IMtAction* action = new IMtAction();

//     IMtActionServer *actionframe = GetInstance<IMtActionServer>();
//     actionframe->SetLocalAddr(servaddr);
//     actionframe->SetIMtAction(action);
//     ret = actionframe->NewSock();
//     LOG_TRACE("ret : %d, strerr : %s", ret, strerror(errno));
//     Frame::FrontRun();
// }

// 测试所有的功能
int main(int argc, char* argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}