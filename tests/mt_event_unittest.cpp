#include "gtest/googletest/include/gtest/gtest.h"
#include "../include/mt_core.h"
#include "../include/mt_ext.h"
#include "../include/mt_thread.h"
#include "../include/mt_frame.h"

ST_NAMESPACE_USING

TEST(EventerTest, event)
{
	int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	LOG_TRACE("socket_fd : %d", socket_fd);

    struct sockaddr_in clientaddr;  
    bzero(&clientaddr, sizeof(clientaddr)); //把一段内存区的内容全部设置为0  
    clientaddr.sin_family = AF_INET;    //internet协议族  
    clientaddr.sin_addr.s_addr = htons(INADDR_ANY);//INADDR_ANY表示自动获取本机地址  
    clientaddr.sin_port = htons(0);    //0表示让系统自动分配一个空闲端口  
    if (bind(socket_fd,(struct sockaddr*)&clientaddr, sizeof(clientaddr)))  
    {
        LOG_ERROR("client bind port error");   
        return ;
    }  

    //定义sockaddr_in
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(4312);  ///服务器端口
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");  ///服务器ip

    int flags = 1;
    flags = fcntl(socket_fd, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(socket_fd, F_SETFL, flags);

    ThreadPool* m_thead_pool_ = new ThreadPool();
    Thread* m_primo_ = m_thead_pool_->AllocThread();
    LOG_TRACE("m_primo_ : %p", m_primo_);
    m_primo_->SetType(ePRIMORDIAL);

    EventDriver* proxyer = GetInstance<Frame>()->GetEventDriver();
    proxyer->Init(10000);
    // proxyer->SetTimeout(10);
    // proxyer->AddFd(socket_fd, ST_READABLE|ST_WRITEABLE);
    Eventer* ev = GetInstance<EventerPool>()->GetEventer(eEVENT_THREAD);
    ev->SetOsfd(socket_fd);
    ev->EnableOutput();
    ev->EnableInput();
    ev->SetOwnerThread(m_primo_);
    ev->SetOwnerDriver(proxyer);
    proxyer->AddEventer(ev);

    int ret = ::connect(socket_fd, (struct sockaddr *)&servaddr, sizeof(servaddr));
    LOG_TRACE("socket_fd : %d, ret : %d, [errno : %d, strerror : %s]", socket_fd,
    	ret, errno, strerror(errno));

    LOG_TRACE("add m_primo_ : %p , proxyer : %p ok", m_primo_, proxyer);
    proxyer->Dispatch();
    ev->EnableOutput();
    ev->DisableInput();
    proxyer->AddEventer(ev);
    LOG_TRACE("connect ok ... ");

    do {
    	proxyer->Dispatch();
    	char buf1[10240] = "GET / HTTP/1.1\r\nHost: www.baidu.com\r\nUser-Agent: curl/7.54.0\r\nAccept: */*\r\n";
	    char buf2[10240] = {'\0'};
	    ret = ::send(socket_fd, buf1, strlen(buf1) + 1, 0);
	    LOG_TRACE("[send]socket_fd : %d, ret : %d, buf : %s, [errno : %d, strerror : %s]", 
	    	socket_fd, ret, buf1, errno, strerror(errno));
        ev->EnableInput();
        ev->DisableOutput();
        proxyer->AddEventer(ev);
	    proxyer->Dispatch();
	    ret = ::recv(socket_fd, buf2, sizeof(buf2), 0);
        LOG_TRACE("[recv]socket_fd : %d, ret : %d, buf : %s, [errno : %d, strerror : %s]", 
            socket_fd, ret, buf2, errno, strerror(errno));
	    proxyer->Dispatch();
    } while(0);
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}