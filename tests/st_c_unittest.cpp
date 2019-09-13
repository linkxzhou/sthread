#include "../include/st_c.h"
#include "../include/st_manager.h"

ST_NAMESPACE_USING

TEST(StStatus, UDP)
{
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(4312);  ///服务器端口
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");  ///服务器ip

    st_set_hook_flag();

    int try_count = 1;
    while (try_count-- > 0)
    {
        char buf1[10240] = "GET / HTTP/1.1\r\nHost: www.baidu.com\r\nUser-Agent: curl/7.54.0\r\nAccept: */*\r\n\r\n";
        char buf2[10240] = {'\0'};
        int buf2_recv = sizeof(buf2);
        LOG_TRACE("send : %s", buf1);

        int rc = udp_sendrecv(&servaddr, 
            buf1, strlen(buf1), 
            (void *)buf2, buf2_recv, 10000);
        LOG_TRACE("recv : %s, buf2_recv : %d", buf2, buf2_recv);
        LOG_TRACE("udp_sendrecv rc : %d", rc);
        ::_sleep(5000);
    }

    LOG_TRACE("end ...");
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    return RUN_ALL_TESTS();
}