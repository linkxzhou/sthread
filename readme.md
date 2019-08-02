mthread
---

# 简介

[sthread]是一个基于协程的高性能网络库，目前提供支持TCP/UDP等协议的非阻塞式的客户端和服务端库

# 特性

1. 不用依赖任何第三方库  
2. 基于支持多个平台的协程调度  
3. 支持epoll，kequeue  
3. 不用写异步调度代码，全部代码同步，但是框架内部是异步处理  
4. 提供非阻塞TCP客户端  
5. 提供非阻塞UDP客户端  
6. 优秀的跨平台特性和高性能（理论上只要系统内存足够大，句柄没有限制，可以无限创建无限个协程）  
7. 使用简单，只需要引入一个libmthread.a或者libmthread.so  
  
除此之外，基于该库之上正在开发各个客户端库：memcache,redis,wrk等  

# 快速开始

## 编译

编译.a或者.so，到当前目录下执行：
```
make 
```
编译测试代码，到tests目录下执行：
```
make
```
或者只运行某个单元测试：
```
make event
```

## An dns client

```cpp
void func(void *args)
{
    std::string *str = (std::string*)args;
    time_t ttl = 0;

    std::vector<int32_t> vc;
    int ret = GetInstance<DNS>()->dns_lookup(str->c_str(), vc, &ttl);
    if (ret < 0) 
    {
        LOG_ERROR("%s, make dns query failed", str->c_str());
        return ;
    }
    for (std::vector<int32_t>::iterator iter = vc.begin(); 
        iter != vc.end(); iter++) 
    {
        struct in_addr addr;
        memcpy(&addr, &(*iter), sizeof(struct in_addr));
        LOG_DEBUG("%s, %d, %s", str->c_str(), *iter, inet_ntoa(addr));
    }
    LOG_DEBUG("ttl : %ld", ttl);
}

int main(int argc, char* argv[])
{
    int ret = mt_init_frame();
    LOG_DEBUG("init ret : %d", ret);
    mt_set_hook_flag();
    mt_set_timeout(200);

    Frame *frame = GetInstance<Frame>();

    // 测试 : 使用协程请求耗时
    LOG_DEBUG("--- start time : %ld", Util::system_ms());
    for (int i = 2000; i < 2005; i++)
    {
        std::stringstream ss;
        ss << "www." << i << ".com";
        std::string *s = new std::string(ss.str());
        Frame::CreateThread(func, s);
    }
    Frame::Loop(true);
    LOG_DEBUG("--- end time : %ld", Util::system_ms());

    return 0;
}
```
说明其中dns内部修改调用接口
```cpp
...

    addr.sin_family = AF_INET;
    if (m_dns_svr_ == NULL)
    {
        addr.sin_addr.s_addr = inet_addr(PUBLIC_DNS_DEFAULT_SERVER);  ///服务器ip
    }
    else
    {
        addr.sin_addr.s_addr = inet_addr(m_dns_svr_);  ///服务器ip
    }
    addr.sin_port = htons((uint16_t)PUBLIC_DNS_DEFAULT_PORT);

    int query_len = make_dns_query_format();
    if (query_len <= 0) 
    {
        return -1;
    }

    int recv_len = sizeof(m_recv_buf_);
    ret = udp_sendrecv(&addr, m_send_buf_, query_len, m_recv_buf_, recv_len, m_timeout_);
    LOG_DEBUG("ret : %d, recv_buf : %s, recv_len : %d", ret, m_recv_buf_, recv_len);

...

```

## http server

```cpp
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

int main(int argc, char* argv[])
{
    //定义sockaddr_in
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(4312);  ///服务器端口
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");  ///服务器ip

    int ret = mt_init_frame();
    mt_set_hook_flag();

    IMtActionServer *server = new IMtActionServer(&servaddr, eTCP_ACCEPT_CONN);
    server->SetCallBack(construct, destruct);
    ret = server->Loop(1000);
    if (ret < 0)
    {
        LOG_ERROR("server error");
    }

    LOG_TRACE("end ...");
}
```


====================实现逻辑

TCPServer   UDPServer   
         |
         |
Recv    Error    Send

TCPClient  UDPClient
         |
         |
Recv    Error    Send

