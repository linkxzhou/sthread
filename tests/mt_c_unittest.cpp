#include "gtest/googletest/include/gtest/gtest.h"
#include "../include/mt_c.h"
#include "../include/mt_frame.h"

MTHREAD_NAMESPACE_USING

static unsigned int check(void* buf, int len)
{
    LOG_TRACE("buf : %s, len : %d", buf, len);
    return len;
}

TEST(C1, c_tcpshort)
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

    int try_count = 1;
    while (try_count-- > 0)
    {
        char buf1[10240] = "GET / HTTP/1.1\r\nHost: www.baidu.com\r\nUser-Agent: curl/7.54.0\r\nAccept: */*\r\n\r\n";
        char buf2[10240] = {'\0'};
        int buf2_recv = sizeof(buf2);
        LOG_TRACE("send : %s", buf1);
        ret = tcp_sendrecv(&servaddr, buf1, strlen(buf1), (void *)buf2, buf2_recv, 10000, check);
        LOG_TRACE("recv : %s, buf2_recv : %d", buf2, buf2_recv);
        LOG_TRACE("tcp_sendrecv ret : %d", ret);
        // Frame::sleep(5000);
    }

    LOG_TRACE("end ...");
}

// TEST(C2, c_tcpkeep)
// {
// 	//定义sockaddr_in
//     struct sockaddr_in servaddr;
//     memset(&servaddr, 0, sizeof(servaddr));
//     servaddr.sin_family = AF_INET;
//     servaddr.sin_port = htons(4312);  ///服务器端口
//     servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");  ///服务器ip
//     // servaddr.sin_port = htons(80);  ///服务器端口
//     // servaddr.sin_addr.s_addr = inet_addr("117.185.16.31");  ///服务器ip

//     int ret = mt_init_frame();
//     LOG_TRACE("init ret : %d", ret);
//     mt_set_hook_flag();

//     int try_count = 5;
//     while (try_count-- > 0)
//     {
//         char buf1[10240] = "GET / HTTP/1.1\r\nHost: www.baidu.com\r\nUser-Agent: curl/7.54.0\r\nAccept: */*\r\n\r\n";
//         char buf2[10240] = {'\0'};
//         int buf2_recv = sizeof(buf2);
//         LOG_TRACE("send : %s", buf1);
//         ret = tcp_sendrecv(&servaddr, buf1, strlen(buf1), (void *)buf2, buf2_recv, 10000, check, true);
//         LOG_TRACE("recv : %s, buf2_recv : %d", buf2, buf2_recv);
//         LOG_TRACE("tcp_sendrecv ret : %d", ret);
//         // Frame::sleep(5000);
//     }

//     LOG_TRACE("end ...");
// }

// 测试所有的功能
int main(int argc, char* argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}