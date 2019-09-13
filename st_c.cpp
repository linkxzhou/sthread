/*
 * Copyright (C) zhoulv2000@163.com
 */

#include "st_c.h"
#include "st_connection.h"
#include "st_manager.h"

ST_NAMESPACE_USING

// 获取连接的句柄信息
static StClientConnection* s_get_conn(struct sockaddr_in *dst, int32_t &sock, eConnType type)
{
    StNetAddress addr(*dst);
    StClientConnection *conn = GetInstance< 
            StConnectionManager<StClientConnection>
        >()->AllocPtr(type, &addr);
    if (NULL == conn)
    {
        LOG_ERROR("get connection failed, dst[%p]", dst);
        return NULL;
    }

    int32_t osfd = conn->CreateSocket(addr);
    LOG_TRACE("osfd: %d", osfd);
    if (osfd < 0)
    {
        LOG_ERROR("create socket failed, ret[%d]", osfd);
        // GetInstance< 
        //     StConnectionManager<StClientConnection>
        // >()->FreePtr(conn);
        return NULL;
    }

    sock = osfd;

    return conn;
}

// 获取tcp的接收信息
// static int32_t s_tcp_check_recv(int32_t sock, char* recv_buf, int32_t &len, int32_t flags, 
//     int32_t timeout, TcpCheckMsgLenCallback func)
// {
//     int32_t recv_len = 0;
//     time64_t start_ms = GetInstance<Frame>()->GetLastClock();
//     do
//     {
//         time64_t cost_time = GetInstance<Frame>()->GetLastClock() - start_ms;
//         LOG_TRACE("current cost_time : %ld", cost_time);
//         if (cost_time > (time64_t)timeout)
//         {
//             errno = ETIME;
//             LOG_ERROR("tcp socket[%d] recv not ok, timeout", sock);
//             return -3;
//         }
//         int32_t rc = Frame::recv(sock, (recv_buf + recv_len), (len - recv_len), 0, 
//             (timeout - (int32_t)cost_time));
//         LOG_TRACE("sock : %d, rc : %d, recv_len : %d", sock, rc, recv_len);
//         if (rc < 0)
//         {
//             LOG_ERROR("tcp socket[%d] recv failed ret[%d][%m]", sock, rc);
//             return -4;
//         }
//         else if (rc == 0)
//         {
//             LOG_ERROR("tcp socket[%d] remote close", sock);
//             return -7;
//         }
//         recv_len += rc;

//         rc = func(recv_buf, recv_len);
//         if (rc < 0)
//         {
//             LOG_ERROR("tcp socket[%d] user check pkg error[%d]", sock, rc);
//             return -5;
//         }
//         else if (rc == 0)
//         {
//             if (len == recv_len)
//             {
//                 LOG_ERROR("tcp socket[%d] user check pkg not ok, but no more buff", sock);
//                 return -6;
//             }
//             continue;
//         }
//         else
//         {
//             if (rc > recv_len)
//             {
//                 continue;
//             }
//             else
//             {
//                 len = rc;
//                 break;
//             }
//         }
//     } while (true);

//     return 0;
// }

int32_t udp_sendrecv(struct sockaddr_in *dst, 
    void *pkg, int32_t len, 
    void *recvbuf, int32_t &bufsize, 
    int32_t timeout)
{
    if (!dst || !pkg || !recvbuf || bufsize <= 0)
    {
        LOG_ERROR("input params invalid, dst[%p], pkg[%p], recvbuf[%p], bufsize[%d]",
            dst, pkg, recvbuf, bufsize);
        return -1;
    }

    int32_t ret = 0, rc = 0;
    struct sockaddr_in from_addr;
    int32_t addr_len = sizeof(from_addr);

    // 获取时间戳
    time64_t start_ms = Util::SysMs(), cost_time = 0;
    int32_t time_left = 0;
    int32_t sock = -1;

    StClientConnection *conn = s_get_conn(dst, sock, eUDP_CONN);
    LOG_TRACE("socket: %d, conn: %p", sock, conn);
    if ((conn == NULL) || (sock < 0))
    {
        LOG_ERROR("socket[%d] get conn failed, ret", sock);
        ret = -2;
        goto UDP_SENDRECV_EXIT_LABEL;
    }

    // 发送数据
    rc = ::_sendto(sock, pkg, len, 0, 
        (struct sockaddr*)dst, (int32_t)sizeof(*dst), timeout);
    if (rc < 0)
    {
        LOG_ERROR("udp_sendrecv send failed, rc: %d, errno: %d", rc, errno);
        ret = -3;
        goto UDP_SENDRECV_EXIT_LABEL;
    }

    cost_time = Util::SysMs() - start_ms;
    time_left = (timeout > cost_time) ? (timeout - (int32_t)cost_time) : 0;
    // 接收数据
    rc = ::_recvfrom(sock, recvbuf, bufsize, 0, 
        (struct sockaddr*)&from_addr, (socklen_t*)&addr_len, time_left);
    LOG_TRACE("from_addr %s: %d, time_left: %d", inet_ntoa(from_addr.sin_addr), 
        ntohs(from_addr.sin_port), time_left);
    if (rc < 0)
    {
        LOG_ERROR("udp_sendrecv recv failed, rc: %d, errno: %d", rc, errno);
        ret = -4;
        goto UDP_SENDRECV_EXIT_LABEL;
    }

    bufsize = rc;

UDP_SENDRECV_EXIT_LABEL:
    if (sock > 0)
    {
        st_close(sock);
        sock = -1;
    }
    
    // 释放连接
    if (conn != NULL)
    {
        // GetInstance< 
        //     StConnectionManager<StClientConnection>
        // >()->FreePtr(conn);
    }

    return ret;
}

// tcp的发送和接收信息(长连接)
// int32_t tcp_sendrecv(struct sockaddr_in *dst, 
//     void *pkg, int32_t len, 
//     void *recvbuf, int32_t &bufsize, 
//     int32_t timeout, 
//     TcpCheckMsgLenCallback callback, bool keeplive)
// {
//     if (!dst || !pkg || !recvbuf || !callback || bufsize <= 0)
//     {
//         LOG_ERROR("input params invalid, dst[%p], pkg[%p], recvbuf[%p], callback[%p], bufsize[%d]",
//             dst, pkg, recvbuf, callback, bufsize);
//         return -10;
//     }

//     int32_t ret = 0, rc = 0;
//     int32_t addr_len = sizeof(struct sockaddr_in);
//     // 获取时间戳
//     time64_t start_ms = Util::SysMs(), cost_time = 0;
//     // 连接超时时间
//     int32_t time_left = timeout;
//     int32_t sock = -1;

//     // 判断是否为长连接
//     StClientConnection *conn = s_get_conn(dst, sock, keeplive ? eTCP_KEEPLIVE_CONN : eTCP_CONN);
//     LOG_TRACE("socket :%d, conn : %p", sock, conn);
//     if ((conn == NULL) || (sock < 0))
//     {
//         LOG_ERROR("socket[%d] get conn failed, ret", sock);
//         ret = -1;
//         goto EXIT_LABEL2;
//     }

//     cost_time = Util::SysMs() - start_ms;
//     time_left = (timeout > cost_time) ? (timeout - cost_time) : 0;
//     // 先将数据包发送
//     rc = ::_send(sock, pkg, len, 0, time_left);
//     if (rc < 0)
//     {
//         LOG_ERROR("socket[%d] send failed, ret[%d]", sock, rc);
//         ret = -2;
//         goto EXIT_LABEL2;
//     }

//     cost_time = Util::SysMs() - start_ms;
//     time_left = (timeout > cost_time) ? (timeout - cost_time) : 0;
//     rc = s_tcp_check_recv(sock, (char*)recv_buf, buf_size, 0, time_left, func);
//     if (rc < 0)
//     {
//         LOG_ERROR("socket[%d] recv failed, ret[%d]", sock, rc);
//         ret = rc;
//         goto EXIT_LABEL2;
//     }
//     // 短连接close
//     if (!tcp_long)
//     {
//         mt_close(sock);
//     }

// TCP_SENDRECV_EXIT_LABEL:
//     // 释放连接(条件 : 如果ret<0，则强制释放，否则判断是否为短连接)
//     if (conn != NULL)
//     {
//         bool force_free = ((ret < 0) ? true : false);
//         force_free = ((!tcp_long) ? true : force_free);
//         GetInstance<ConnectionPool>()->FreeConnection(conn, force_free);
//     }

//     return ret;
// }

// 设置私有数据
void mt_set_private(void *data)
{
    Thread *athread = (Thread*)(GetThreadScheduler()->GetActiveThread());
    ASSERT(athread == NULL);
    Thread *thread = (Thread*)(athread->GetRootThread());

    if (thread != NULL)
    {
        thread->SetPrivate(data);
    }
}

// 获取私有数据
void* mt_get_private()
{
    Thread *athread = (Thread*)(GetThreadScheduler()->GetActiveThread());
    ASSERT(athread == NULL);
    Thread *thread = (Thread*)(athread->GetRootThread());

    if (thread != NULL)
    {
        return thread->GetPrivate();
    }

    return NULL;
}

void st_set_hook_flag()
{
    GetInstance< Manager >()->SetHookFlag();
}