/*
 * Copyright (C) zhoulv2000@163.com
 */

#include "mt_connection.h"
#include "mt_action.h"
#include "mt_frame.h"

MTHREAD_NAMESPACE_USING
using namespace std;

// Udp的短连接
int UdpIMtConnection::CreateSocket(int fd)
{
    if (fd > 0)
    {
        m_osfd_ = fd;
        LOG_TRACE("set fd: %d", fd);
    }
    else
    {
        m_osfd_ = mt_socket(AF_INET, SOCK_DGRAM, 0);
        if (m_osfd_ < 0)
        {
            LOG_ERROR("socket create failed, errno %d(%s)", errno, strerror(errno));
            return -1;
        }
    }

    int flags = 1;
    // 设置为非阻塞
    if (mt_ioctl(m_osfd_, FIONBIO, &flags) < 0)
    {
        LOG_ERROR("socket unblock failed, errno %d(%s)", errno, strerror(errno));
        mt_close(m_osfd_);
        m_osfd_ = -1;
        return -2;
    }

    LOG_TRACE("m_ev_ : %p", m_ev_);
    if (NULL != m_ev_)
    {
        m_ev_->SetOsfd(m_osfd_);
        m_ev_->EnableInput();
        m_ev_->EnableOutput();
    }
    else
    {
        LOG_WARN("m_ev_ is NULL, cannot set m_osfd_: %d", m_osfd_);
    }

    return m_osfd_;
}

int UdpIMtConnection::SendData()
{
    if (!m_msg_buff_)
    {
        LOG_ERROR("SendData conn msg %p, error", m_msg_buff_);
        return -100;
    }

    int ret = Frame::sendto(m_osfd_, m_msg_buff_->GetMsgBuffer(), m_msg_buff_->GetMsgLen(), 0,
            (struct sockaddr*)&m_dst_addr_, sizeof(struct sockaddr_in), m_timeout_);
    if (ret == -1)
    {
        if ((errno == EINTR) || (errno == EAGAIN) || (errno == EINPROGRESS))
        {
            return 0;
        }
        else
        {
            LOG_ERROR("socket send failed, fd %d, errno %d(%s)", m_osfd_, errno, strerror(errno));
            return -2;
        }
    }
    else
    {
        m_msg_buff_->SetHaveSendLen(ret);
        return ret;
    }
}

int UdpIMtConnection::RecvData()
{
    if (!m_msg_buff_)
    {
        LOG_ERROR("RecvData conn msg %p, error", m_msg_buff_);
        return -100;
    }

    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    int ret = Frame::recvfrom(m_osfd_, m_msg_buff_->GetMsgBuffer(), m_msg_buff_->GetMaxLen(), 0, 
        (struct sockaddr*)&from, &from_len, m_timeout_);
    if (ret < 0)
    {
        if ((errno == EINTR) || (errno == EAGAIN) || (errno == EINPROGRESS))
        {
            return 0;
        }
        else
        {
            LOG_ERROR("socket recv failed, fd %d, errno %d(%s)", m_osfd_, errno, strerror(errno));
            return -2;
        }
    }
    else if (ret == 0)
    {
        LOG_DEBUG("remote close connection, fd %d", m_osfd_);
        return -3;
    }
    else
    {
        m_msg_buff_->SetHaveRecvLen(ret);
    }

    ret = m_action_->DoInput(); // 处理输入数据
    if (ret > 0)
    {
        m_msg_buff_->SetMsgLen(ret);
        return ret;
    }
    else if (ret == 0)
    {
        return 0;
    }
    else if (ret == -65535)
    {
        m_msg_buff_->SetHaveRecvLen(0); // 丢弃所有的数据
        LOG_WARN("ret: %d, set recv length 0", ret);
        return 0;
    }
    else
    {
        return -4;
    }
}

int TcpAcceptIMtConnection::CreateSocket(int fd)
{
    if (fd > 0)
    {
        m_osfd_ = fd;
        LOG_TRACE("set fd : %d", fd);
    }
    else
    {
        m_osfd_ = mt_socket(AF_INET, SOCK_STREAM, 0);
        if (m_osfd_ < 0)
        {
            LOG_ERROR("socket create failed, errno %d(%s)", errno, strerror(errno));
            return -1;
        }
    }
    
    int flags = 1;
    // 设置为非阻塞
    if (mt_ioctl(m_osfd_, FIONBIO, &flags) < 0)
    {
        LOG_ERROR("socket unblock failed, errno %d(%s)", errno, strerror(errno));
        mt_close(m_osfd_);
        m_osfd_ = -1;
        return -2;
    }

    LOG_TRACE("m_ev_ : %p", m_ev_);
    if (NULL != m_ev_)
    {
        m_ev_->SetOsfd(m_osfd_);
        m_ev_->EnableInput();
        m_ev_->EnableOutput();
    }
    else
    {
        LOG_WARN("m_ev_ is NULL, cannot set m_osfd_: %d", m_osfd_);
    }

    int ret = ::bind(m_osfd_, (struct sockaddr *)&m_dst_addr_, sizeof(m_dst_addr_));
    if (ret < 0)
    {
        LOG_ERROR("m_osfd_ bind error, ret : %d", ret);
        return -3;
    }

    ret = ::listen(m_osfd_, 1024);
    if (ret < 0)
    {
        LOG_ERROR("m_osfd_ listen error, ret : %d", ret);
        return -4;
    }

    return m_osfd_;
}

int TcpShortIMtConnection::CreateSocket(int fd)
{
    if (fd > 0)
    {
        m_osfd_ = fd;
        LOG_TRACE("set fd : %d", fd);
    }
    else
    {
        m_osfd_ = mt_socket(AF_INET, SOCK_STREAM, 0);
        if (m_osfd_ < 0)
        {
            LOG_ERROR("socket create failed, errno %d(%s)", errno, strerror(errno));
            return -1;
        }
    }

    int flags = 1;
    // 设置为非阻塞
    if (mt_ioctl(m_osfd_, FIONBIO, &flags) < 0)
    {
        LOG_ERROR("socket unblock failed, errno %d(%s)", errno, strerror(errno));
        mt_close(m_osfd_);
        m_osfd_ = -1;
        return -2;
    }

    LOG_TRACE("m_ev_ : %p", m_ev_);
    if (NULL != m_ev_)
    {
        m_ev_->SetOsfd(m_osfd_);
        m_ev_->EnableInput();
        m_ev_->EnableOutput();
    }
    else
    {
        LOG_WARN("m_ev_ is NULL, cannot set m_osfd_: %d", m_osfd_);
    }

    return m_osfd_;
}

// 发送数据
int TcpShortIMtConnection::SendData()
{
    if (!m_action_ || !m_msg_buff_)
    {
        LOG_ERROR("conn not set action %p, or msg %p, error", m_action_, m_msg_buff_);
        return -100;
    }

    char* msg_ptr = (char*)m_msg_buff_->GetMsgBuffer();
    int msg_len = m_msg_buff_->GetMsgLen();
    // 已经发送的数据长度
    int have_send_len = m_msg_buff_->GetHaveSendLen(); 
    int ret = Frame::send(m_osfd_, msg_ptr + have_send_len, msg_len - have_send_len, 0, m_timeout_);

    LOG_TRACE("send: %s", msg_ptr + have_send_len);
    
    if (ret == -1)
    {
        if ((errno == EINTR) || (errno == EAGAIN) || (errno == EINPROGRESS))
        {
            return 0;
        }
        else
        {
            LOG_ERROR("send tcp socket failed, error: %d", errno);
            return -1;
        }
    }
    else
    {
        have_send_len += ret;
        m_msg_buff_->SetHaveSendLen(have_send_len);
    }

    if (have_send_len >= msg_len)
    {
        return msg_len;
    }
    else
    {
        return 0;
    }
}

// 接收数据
int TcpShortIMtConnection::RecvData()
{
    if (!m_msg_buff_)
    {
        LOG_ERROR("conn msg %p, error", m_msg_buff_);
        return -100;
    }

    char* msg_ptr = (char*)m_msg_buff_->GetMsgBuffer();
    int max_len = m_msg_buff_->GetMaxLen();
    int have_recv_len = m_msg_buff_->GetHaveRecvLen();
    int ret = Frame::recv(m_osfd_, (char*)msg_ptr + have_recv_len, max_len - have_recv_len, 0, m_timeout_);
    if (ret < 0)
    {
        if ((errno == EINTR) || (errno == EAGAIN) || (errno == EINPROGRESS))
        {
            return 0;
        }
        else
        {
            LOG_ERROR("recv tcp socket failed, error: %d", errno);
            return -2;
        }
    }
    else if (ret == 0)
    {
        LOG_ERROR("tcp remote close, address: %s[%d]", inet_ntoa(m_dst_addr_.sin_addr), 
            ntohs(m_dst_addr_.sin_port));
        return -1;
    }
    else
    {
        have_recv_len += ret;
        m_msg_buff_->SetHaveRecvLen(have_recv_len);
    }

    ret = m_action_->DoInput(); // 处理输入请求
    if (ret > 0)
    {
        m_msg_buff_->SetMsgLen(have_recv_len);
        return ret;
    }
    else if (ret == 0)
    {
        return 0;
    }
    else
    {
        return -1;
    }
}

int TcpShortIMtConnection::OpenConnect()
{
    if (!m_msg_buff_)
    {
        LOG_ERROR("conn msg: %p, error", m_msg_buff_);
        return -1;
    }
    
    int err = 0;
    int ret = Frame::connect(m_osfd_, (struct sockaddr*)&m_dst_addr_, 
        sizeof(struct sockaddr_in), m_timeout_);
    if (ret < 0)
    {
        err = errno;
        if (err == EISCONN)
        {
            return 0;
        }
        else
        {
            if ((err == EINPROGRESS) || (err == EALREADY) || (err == EINTR))
            {
                LOG_DEBUG("open connect not ok, sock: %d, errno: %d, strerr: %s", 
                    m_osfd_, err, strerror(err));
                return -1;
            }
            else
            {
                LOG_ERROR("open connect not ok, sock: %d, errno: %d, strerr: %s", 
                    m_osfd_, err, strerror(err));
                return -2;
            }
        }
    }
    else
    {
        return 0;
    }
}

int TcpLongIMtConnection::CreateSocket(int fd)
{
    if (m_osfd_ <= 0)
    {
        TcpShortIMtConnection::CreateSocket(fd);
    }
    else
    {
        LOG_TRACE("m_ev_ : %p", m_ev_);
        if (NULL != m_ev_)
        {
            m_ev_->SetOsfd(m_osfd_);
            m_ev_->EnableInput();
            m_ev_->EnableOutput();
        }
        else
        {
            LOG_WARN("m_ev_ is NULL, cannot set m_osfd_: %d", m_osfd_);
        }
    }

    return m_osfd_;
}

bool TcpLongIMtConnection::IdleAttach()
{
    if (m_osfd_ < 0)
    {
        LOG_ERROR("this %p attach failed, fd %d error", this, m_osfd_);
        return false;
    }
    if (m_keep_flag_ & eKEEP_IN_POLL)
    {
        LOG_ERROR("this %p repeat attach, error", this);
        return true;
    }

    m_ev_->DisableOutput();
    m_ev_->EnableInput();
    if ((NULL == m_timer_) || !m_timer_->StartTimer(this, m_keep_time_))
    {
        LOG_ERROR("m_timer_: %p, this %p attach timer failed, error", m_timer_, this);
        return false;
    }
    
    Frame *frame = GetInstance<Frame>();
    EventDriver *driver = frame->GetEventDriver();
    if (driver->AddEventer(m_ev_))
    {
        m_keep_flag_ |= eKEEP_IN_POLL;
        return true;
    }
    else
    {
        LOG_ERROR("this %p attach failed, error", this);
        return false;
    }
}

bool TcpLongIMtConnection::IdleDetach()
{
    if (m_osfd_ < 0)
    {
        LOG_ERROR("this %p detach failed, fd %d error", this, m_osfd_);
        return false;
    }

    if (!(m_keep_flag_ & eKEEP_IN_POLL))
    {
        LOG_DEBUG("this %p repeat detach, error", this);
        return true;
    }
    if (NULL != m_timer_)
    {
        m_timer_->StopTimer(this);
    }

    if (NULL != m_ev_)
    {
        m_ev_->DisableOutput();
        m_ev_->EnableInput();
        Frame *frame = GetInstance<Frame>();
        EventDriver *driver = frame->GetEventDriver();
        if (driver->DelEventer(m_ev_))
        {
            m_keep_flag_ &= ~eKEEP_IN_POLL;
            return true;
        }
        else
        {
            LOG_ERROR("this: %p detach failed", this);
            return false;
        }
        // 释放eventer
        IMtConnection::ResetEventer();
    }

    return true;
}

// 超时处理
void TcpLongIMtConnection::Notify(eEventType type)
{
    switch (type)
    {
        eEVENT_TIMEOUT: // 超时处理
            LOG_TRACE("keep timeout[%u], fd %d, close connection", m_keep_time_, m_osfd_);
            GetInstance<ConnectionPool>()->CloseIdleTcpLong(this);
        default:
            break;
    }
}

// 找到对应的连接
TcpLongIMtConnection* TcpLongPool::GetConnection(struct sockaddr_in* dst)
{
    TcpLongIMtConnection* conn = NULL;
    if (NULL == dst)
    {
        LOG_ERROR("input param dst NULL");
        return NULL;
    }

    KeepKey key(dst);
    KeepKey* keep_list = (KeepKey*)(m_keep_map_->HashFindData(&key));
    LOG_TRACE("keep_list : %p", keep_list);
    if ((NULL == keep_list) || (NULL == keep_list->GetFirstConnection()))
    {
        conn = m_tcplong_pool_.AllocPtr();
        if (conn)
        {
            conn->SetMsgDstAddr(dst);
            // 设置定时器监听
            HeapTimer* timer = GetInstance<Frame>()->GetHeapTimer();
            if (NULL != timer)
            {
                conn->SetHeapTimer(timer);
            }
        }
    }
    else
    {
        conn = (TcpLongIMtConnection*)(keep_list->GetFirstConnection());
        keep_list->RemoveConnection(conn);
        conn->IdleDetach(); // 如果存在链接则释放绑定事件
    }

    return conn;
}

// 移除tcp的conn链接
bool TcpLongPool::RemoveConnection(TcpLongIMtConnection* conn)
{
    struct sockaddr_in* dst = conn->GetMsgDstAddr();
    if ((dst->sin_addr.s_addr == 0) || (dst->sin_port == 0))
    {
        LOG_ERROR("sock addr invalid, %x:%d", dst->sin_addr.s_addr, dst->sin_port);
        return false;
    }

    KeepKey key(dst);
    KeepKey* keep_list = (KeepKey*)m_keep_map_->HashFindData(&key);
    if (!keep_list)
    {
        LOG_ERROR("conn cache list invalid, [%x:%d]", dst->sin_addr.s_addr, dst->sin_port);
        return false;
    }
    conn->IdleDetach();
    keep_list->RemoveConnection(conn);

    return true;
}

bool TcpLongPool::CacheConnection(TcpLongIMtConnection* conn)
{
    struct sockaddr_in* dst = conn->GetMsgDstAddr();
    if ((dst->sin_addr.s_addr == 0) || (dst->sin_port == 0))
    {
        LOG_ERROR("sock addr invalid, %x:%d", dst->sin_addr.s_addr, dst->sin_port);
        return false;
    }

    KeepKey key(dst);
    KeepKey* keep_list = (KeepKey*)(m_keep_map_->HashFindData(&key));
    if (!keep_list)
    {
        keep_list = new KeepKey(conn->GetMsgDstAddr());
        if (!keep_list)
        {
            LOG_ERROR("new keep_list failed");
            return false;
        }
        m_keep_map_->HashInsert(keep_list);
    }

    if (!conn->IdleAttach())
    {
        LOG_ERROR("conn IdleAttach failed, error");
        return false;
    }
    // 重置链接
    conn->Reset();
    keep_list->InsertConnection(conn);

    return true;
}

void TcpLongPool::FreeConnection(TcpLongIMtConnection* conn, bool force_free)
{
    if (force_free)
    {
        conn->Reset();
        m_tcplong_pool_.FreePtr(conn);
        return ;
    }
    else
    {
        if (!CacheConnection(conn))
        {
            conn->Reset();
            m_tcplong_pool_.FreePtr(conn);
            return ;
        }
    }
}

IMtConnection* ConnectionPool::GetConnection(eConnType type, struct sockaddr_in* dst)
{
    switch (type)
    {
        case eUDP_CONN: return m_udp_pool_.AllocPtr();
        case eTCP_SHORT_CONN: return m_tcpshort_pool_.AllocPtr(); // tcp短连接
        case eTCP_LONG_CONN: return m_tcplong_pool_.GetConnection(dst); // tcp长连接
        case eTCP_ACCEPT_CONN: return (new TcpAcceptIMtConnection()); // accept
        default: return NULL;
    }
}

void ConnectionPool::FreeConnection(IMtConnection* conn, bool force_free)
{
    if (!conn)
    {
        return;
    }

    eConnType type = conn->GetConnType();
    conn->Reset();
    switch (type)
    {
        case eUDP_CONN: return m_udp_pool_.FreePtr(dynamic_cast<UdpIMtConnection*>(conn));
        case eTCP_SHORT_CONN: return m_tcpshort_pool_.FreePtr(dynamic_cast<TcpShortIMtConnection*>(conn));
        case eTCP_LONG_CONN: return m_tcplong_pool_.FreeConnection(dynamic_cast<TcpLongIMtConnection*>(conn), force_free);
        case eTCP_ACCEPT_CONN: safe_delete(conn);
        default: break;
    }

    return ;
}

void ConnectionPool::CloseIdleTcpLong(IMtConnection* conn)
{
    TcpLongIMtConnection* _conn = (TcpLongIMtConnection *)conn;
    m_tcplong_pool_.RemoveConnection(_conn);
    m_tcplong_pool_.FreeConnection(_conn, true);
}