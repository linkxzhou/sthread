#include "../include/st_manager.h"
#include "../include/st_sys.h"
#include "../include/st_netaddr.h"

ST_NAMESPACE_USING

static void socket_callback(Manager *manager, int i)
{
    ::_sleep(3000);
    LOG_TRACE("waiting 3000ms ...");

    StEventBase *item = new StEventBase();
    LOG_TRACE("item node :%p, events :%d", item, item->GetEvents());
    ASSERT(item != NULL);

    // 定义sockaddr_in
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    // servaddr.sin_port = htons(80);  ///服务器端口
    // servaddr.sin_addr.s_addr = inet_addr("14.215.177.39");  ///服务器ip
    servaddr.sin_port = htons(8000);  ///服务器端口
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");  ///服务器ip

    int socket_fd = st_socket(AF_INET, SOCK_STREAM, 0);
    item->SetOsfd(socket_fd);
    LOG_TRACE("item node :%p, events :%d", item, item->GetEvents());
    GetEventScheduler()->Add(item);

    // int nSendBuf = 20; // 设置为20字节
    // int ret = st_setsockopt(socket_fd, SOL_SOCKET, SO_SNDBUF,(const char*)&nSendBuf, sizeof(int));
    // LOG_TRACE("[st_setsockopt]socket_fd : %d, ret : %d", socket_fd, ret);
    
    LOG_TRACE("socket_fd :%d", socket_fd);
    if (socket_fd < 0)
    {
        LOG_ERROR("create socket failed, ret[%d]", socket_fd);
        return ;
    }

    int ret = ::_connect(socket_fd, (struct sockaddr *)&servaddr, sizeof(servaddr), 3000);
    LOG_TRACE("connect socket_fd: %d, ret: %d", socket_fd, ret);

    char buf1[10240];
    sprintf(buf1, "GET / HTTP/1.1\r\n"
        "Host: www.baidu.com\r\n"
        "User-Agent: curl/7.54.0\r\n"
        "Accept: */*\r\n\r\n%d", i);
    char buf2[10240] = {'\0'};
    LOG_TRACE("############# -1- ##############");
    ret = ::_send(socket_fd, buf1, strlen(buf1), 0, 3000);
    LOG_TRACE("socket_fd : %d, ret : %d, buf : %s", socket_fd, ret, buf1); 	
    ret = ::_recv(socket_fd, buf2, sizeof(buf2), 0, 3000);
    LOG_TRACE("############# -2- ##############");
    ret = ::_send(socket_fd, buf1, strlen(buf1), 0, 3000);
    LOG_TRACE("socket_fd : %d, ret : %d, buf : %s", socket_fd, ret, buf1); 	
    ret = ::_recv(socket_fd, buf2, sizeof(buf2), 0, 3000);

    // 测试重新发送请求
    // manager->CreateThread(NewClosure(socket_callback, manager, 99));
    // context_exit();
}

TEST(StStatus, socket)
{
	Manager *manager = GetInstance< Manager >();
    manager->SetHookFlag();
    for (int i = 0; i < 2; i++)
    {
        manager->CreateThread(NewClosure(socket_callback, manager, i));
    }

    Manager::StartDaemonThread(manager);
    // StNetAddress addr;
    // addr.SetAddr("www.baidu.com");
    // LOG_TRACE("addr: %s", addr.IP());
    // LOG_TRACE("addr port: %s", addr.IPPort());
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    return RUN_ALL_TESTS();
}