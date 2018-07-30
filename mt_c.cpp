/*
 * Copyright (C) zhoulv2000@163.com
 */

#include "mt_c.h"
#include "mt_frame.h"

MTHREAD_NAMESPACE_USING

// 获取连接的句柄信息
static IMtConnection* s_get_conn(struct sockaddr_in* dst, int& sock, eConnType type)
{
    EventProxyer* proxyer = GetInstance<Frame>()->GetEventProxyer();
	Eventer* ev = GetInstance<ISessionEventerPool>()->GetEventer(eEVENT_THREAD);
    ev->SetOwnerProxyer(proxyer);
    LOG_TRACE("ev node :%p", ev);
    if (NULL == ev)
    {
        LOG_ERROR("get ev failed");
        return NULL;
    }

    IMtConnection* conn = NULL;
    conn = GetInstance<ConnectionPool>()->GetConnection(type, dst);
    LOG_TRACE("IMtConnection conn : %p", conn);
    if (NULL == conn)
    {
        LOG_ERROR("get connection failed, dst[%p]", dst);
        GetInstance<ISessionEventerPool>()->FreeEventer(ev);
        return NULL;
    }
    conn->SetEventer(ev);
    conn->SetMsgDstAddr(dst);

    int osfd = conn->CreateSocket();
    LOG_TRACE("osfd : %d", osfd);
    if (osfd < 0)
    {
        GetInstance<ConnectionPool>()->FreeConnection(conn, true);
        GetInstance<ISessionEventerPool>()->FreeEventer(ev);
        LOG_ERROR("create socket failed, ret[%d]", osfd);
        return NULL;
    }
    proxyer->AddEventer(ev);
    sock = osfd;

    return conn;
}
// 获取tcp的接收信息
static int s_tcp_check_recv(int sock, char* recv_buf, int &len, int flags, 
    int timeout, TcpCheckMsgLenFunction func)
{
    int recv_len = 0;
    utime64_t start_ms = GetInstance<Frame>()->GetLastClock();
    do
    {
        utime64_t cost_time = GetInstance<Frame>()->GetLastClock() - start_ms;
        LOG_TRACE("current cost_time : %ld", cost_time);
        if (cost_time > (utime64_t)timeout)
        {
            errno = ETIME;
            LOG_ERROR("tcp socket[%d] recv not ok, timeout", sock);
            return -3;
        }
        int rc = Frame::recv(sock, (recv_buf + recv_len), (len - recv_len), 0, 
            (timeout - (int)cost_time));
        LOG_TRACE("sock : %d, rc : %d, recv_len : %d", sock, rc, recv_len);
        if (rc < 0)
        {
            LOG_ERROR("tcp socket[%d] recv failed ret[%d][%m]", sock, rc);
            return -4;
        }
        else if (rc == 0)
        {
            LOG_ERROR("tcp socket[%d] remote close", sock);
            return -7;
        }
        recv_len += rc;

        rc = func(recv_buf, recv_len);
        if (rc < 0)
        {
            LOG_ERROR("tcp socket[%d] user check pkg error[%d]", sock, rc);
            return -5;
        }
        else if (rc == 0)
        {
            if (len == recv_len)
            {
                LOG_ERROR("tcp socket[%d] user check pkg not ok, but no more buff", sock);
                return -6;
            }
            continue;
        }
        else
        {
            if (rc > recv_len)
            {
                continue;
            }
            else
            {
                len = rc;
                break;
            }
        }
    } while (true);

    return 0;
}

int udp_sendrecv(struct sockaddr_in* dst, void* pkg, int len, void* recv_buf, 
    int& buf_size, int timeout)
{
    if (!dst || !pkg || !recv_buf || buf_size <= 0)
    {
        LOG_ERROR("input params invalid, dst[%p], pkg[%p], recv_buf[%p], buf_size[%d]",
            dst, pkg, recv_buf, buf_size);
        return -9;
    }

    int ret = 0, rc = 0;
    struct sockaddr_in from_addr = {0};
    int addr_len = sizeof(from_addr);
    // 获取frame的时间戳
    utime64_t start_ms = GetInstance<Frame>()->GetLastClock();
    utime64_t cost_time = 0;
    // 连接超时时间
    int time_left = timeout;
    int sock = -1;

    IMtConnection* conn = s_get_conn(dst, sock, eUDP_CLIENT_CONN);
    LOG_TRACE("current socket :%d, conn : %p", sock, conn);
    if ((conn == NULL) || (sock < 0))
    {
        LOG_ERROR("socket[%d] get conn failed, ret", sock);
        ret = -1;
        goto EXIT_LABEL1;
    }

    // 发送数据
    rc = Frame::sendto(sock, pkg, len, 0, (struct sockaddr*)dst, (int)sizeof(*dst), time_left);
    if (rc < 0)
    {
        LOG_ERROR("udp_sendrecv send failed, rc: %d, errno: %d", rc, errno);
        ret = -2;
        goto EXIT_LABEL1;
    }

    cost_time = GetInstance<Frame>()->GetLastClock() - start_ms;
    time_left = (timeout > (int)cost_time) ? (timeout - (int)cost_time) : 0;
    // 接收数据
    rc = Frame::recvfrom(sock, recv_buf, buf_size, 0, (struct sockaddr*)&from_addr, 
        (socklen_t*)&addr_len, time_left);
    LOG_TRACE("from_addr %s:%d, time_left : %d", inet_ntoa(from_addr.sin_addr), 
        ntohs(from_addr.sin_port), time_left);
    if (rc < 0)
    {
        LOG_ERROR("udp_sendrecv recv failed, rc: %d, errno: %d", rc, errno);
        ret = -3;
        goto EXIT_LABEL1;
    }
    buf_size = rc;

EXIT_LABEL1:
    if (sock > 0)
    {
        mt_close(sock);
        sock = -1;
    }
    // 释放连接
    if (conn != NULL)
    {
        GetInstance<ConnectionPool>()->FreeConnection(conn, true);
    }

    return ret;
}
// tcp的发送和接收信息(长连接)
int tcp_sendrecv(struct sockaddr_in* dst, void* pkg, int len, void* recv_buf, int& buf_size, int timeout, 
        TcpCheckMsgLenFunction func, bool is_keep)
{
    if (!dst || !pkg || !recv_buf || !func || buf_size <= 0)
    {
        LOG_ERROR("input params invalid, dst[%p], pkg[%p], recv_buf[%p], func[%p], buf_size[%d]",
            dst, pkg, recv_buf, func, buf_size);
        return -10;
    }

    int ret = 0, rc = 0;
    int addr_len = sizeof(struct sockaddr_in);
    // 获取frame的时间戳
    utime64_t start_ms = GetInstance<Frame>()->GetLastClock();
    utime64_t cost_time = 0;
    // 连接超时时间
    int time_left = timeout;
    int sock = -1;

    // 判断是否为长连接
    IMtConnection* conn = s_get_conn(dst, sock, is_keep ? eTCP_KEEP_CLIENT_CONN : eTCP_SHORT_CLIENT_CONN);
    LOG_TRACE("current socket :%d, conn : %p", sock, conn);
    if ((conn == NULL) || (sock < 0))
    {
        LOG_ERROR("socket[%d] get conn failed, ret", sock);
        ret = -1;
        goto EXIT_LABEL2;
    }

    // 建立tcp链接
    rc = Frame::connect(sock, (struct sockaddr *)dst, addr_len, time_left);
    LOG_TRACE("connect socket:%d, time_left:%d, rc:%d", sock, time_left, rc);
    if (rc < 0)
    {
        LOG_ERROR("socket[%d] connect failed, ret[%d]", sock, rc);
        ret = -4;
        goto EXIT_LABEL2;
    }

    cost_time = GetInstance<Frame>()->GetLastClock() - start_ms;
    time_left = (timeout > (int)cost_time) ? (timeout - (int)cost_time) : 0;
    // 先将数据包发送
    rc = Frame::send(sock, pkg, len, 0, time_left);
    if (rc < 0)
    {
        LOG_ERROR("socket[%d] send failed, ret[%d]", sock, rc);
        ret = -2;
        goto EXIT_LABEL2;
    }

    cost_time = GetInstance<Frame>()->GetLastClock() - start_ms;
    time_left = (timeout > (int)cost_time) ? (timeout - (int)cost_time) : 0;
    // 循环的收取数据
    rc = s_tcp_check_recv(sock, (char*)recv_buf, buf_size, 0, time_left, func);
    if (rc < 0)
    {
        LOG_ERROR("socket[%d] recv failed, ret[%d]", sock, rc);
        ret = rc;
        goto EXIT_LABEL2;
    }
    // 短连接close
    if (!is_keep)
    {
        mt_close(sock);
    }
    ret = 0;

EXIT_LABEL2:
    // 释放连接(条件 : 如果ret<0，则强制释放，否则判断是否为短连接)
    if (conn != NULL)
    {
        bool force_free = (ret < 0) ? true : false;
        force_free = (!is_keep) ? true : force_free;
        GetInstance<ConnectionPool>()->FreeConnection(conn, force_free);
    }

    return ret;
}
// 设置私有数据
void mt_set_private(void *data)
{
    Thread *thread = (Thread*)(GetInstance<Frame>()->GetRootThread());
    thread->SetPrivate(data);
}
// 获取私有数据
void* mt_get_private()
{
    Thread *thread = (Thread*)(GetInstance<Frame>()->GetRootThread());
    return thread->GetPrivate();
}
// 初始化frame
bool mt_init_frame()
{
    return GetInstance<Frame>()->InitFrame();
}
void mt_set_stack_size(unsigned int bytes)
{
    ThreadPool::SetDefaultStackSize(bytes);
}
void mt_set_hook_flag()
{
    return GetInstance<Frame>()->SetHookFlag();
}
void mt_set_timeout(int timeout_ms)
{
    GetInstance<Frame>()->SetTimeout(timeout_ms);
}