/*
 * Copyright (C) zhoulv2000@163.com
 */

#include "mt_connection.h"
#include "mt_action.h"
#include "mt_frame.h"

MTHREAD_NAMESPACE_USING
using namespace std;

// Udp的短连接
int UdpClientConnection::CreateSocket()
{
    m_osfd_ = mt_socket(AF_INET, SOCK_DGRAM, 0);
    if (m_osfd_ < 0)
    {
        LOG_ERROR("socket create failed, errno %d(%s)", errno, strerror(errno));
        return -101;
    }

    int flags = 1;
    // 设置为非阻塞
    if (mt_ioctl(m_osfd_, FIONBIO, &flags) < 0)
    {
        LOG_ERROR("socket unblock failed, errno %d(%s)", errno, strerror(errno));
        mt_close(m_osfd_);
        m_osfd_ = -1;
        return -102;
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
        LOG_WARN("m_ev_ is NULL, cannot set m_osfd_ : %d", m_osfd_);
    }

    return m_osfd_;
}

// TODO : udp的短连接
int UdpClientConnection::SendData()
{
    if (!m_msg_buff_)
    {
        LOG_ERROR("SendData conn msg %p, error", m_msg_buff_);
        return -100;
    }

    int ret = mt_sendto(m_osfd_, m_msg_buff_->GetMsgBuffer(), m_msg_buff_->GetMsgLen(), 0,
                (struct sockaddr*)&m_dst_addr_, sizeof(struct sockaddr_in));
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

// TODO : 接收数据
int UdpClientConnection::RecvData()
{
    if (!m_msg_buff_)
    {
        LOG_ERROR("RecvData conn msg %p, error", m_msg_buff_);
        return -101;
    }

    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    int ret = mt_recvfrom(m_osfd_, m_msg_buff_->GetMsgBuffer(), m_msg_buff_->GetMaxLen(),
                       0, (struct sockaddr*)&from, &from_len);
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
        return -1;
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
        LOG_WARN("ret : %d, set recv length 0", ret);
        return 0;
    }
    else
    {
        return -1;
    }
}

// Tcp的短链接
int TcpShortClientConnection::CreateSocket()
{
    m_osfd_ = mt_socket(AF_INET, SOCK_STREAM, 0);
    if (m_osfd_ < 0)
    {
        LOG_ERROR("socket create failed, errno %d(%s)", errno, strerror(errno));
        return -1;
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
        LOG_WARN("m_ev_ is NULL, cannot set m_osfd_ : %d", m_osfd_);
    }

    return m_osfd_;
}

// 发送数据
int TcpShortClientConnection::SendData()
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
    int ret = mt_send(m_osfd_, msg_ptr + have_send_len, msg_len - have_send_len, 0);
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
int TcpShortClientConnection::RecvData()
{
    if (!m_msg_buff_)
    {
        LOG_ERROR("conn msg %p, error", m_msg_buff_);
        return -100;
    }

    char* msg_ptr = (char*)m_msg_buff_->GetMsgBuffer();
    int max_len = m_msg_buff_->GetMaxLen();
    int have_recv_len = m_msg_buff_->GetHaveRecvLen();
    int ret = mt_recv(m_osfd_, (char*)msg_ptr + have_recv_len, max_len - have_recv_len, 0);
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

int TcpShortClientConnection::OpenConnect()
{
    if (!m_msg_buff_)
    {
        LOG_ERROR("conn msg %p, error", m_msg_buff_);
        return -100;
    }

    LOG_TRACE("m_dst_addr_ : %p, port : %d", &m_dst_addr_, ntohs(m_dst_addr_.sin_port));

    int err = 0;
    int ret = mt_connect(m_osfd_, (struct sockaddr*)&m_dst_addr_, sizeof(struct sockaddr_in));
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
                LOG_DEBUG("open connect not ok, sock %d, errno %d, strerr %s", 
                    m_osfd_, err, strerror(err));
                return -1;
            }
            else
            {
                LOG_ERROR("open connect not ok, sock %d, errno %d, strerr %s", 
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

int TcpKeepClientConnection::CreateSocket()
{
    LOG_TRACE("m_osfd_ : %d", m_osfd_);
    if (m_osfd_ <= 0)
    {
        TcpShortClientConnection::CreateSocket();
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
            LOG_WARN("m_ev_ is NULL, cannot set m_osfd_ : %d", m_osfd_);
        }
    }

    return m_osfd_;
}

bool TcpKeepClientConnection::IdleAttach()
{
    if (m_osfd_ < 0)
    {
        LOG_ERROR("this %p attach failed, fd %d error", this, m_osfd_);
        return false;
    }
    if (m_keep_flag_ & eTCP_KEEP_IN_POLL)
    {
        LOG_ERROR("this %p repeat attach, error", this);
        return true;
    }

    m_ev_->DisableOutput();
    m_ev_->EnableInput();
    if ((NULL == m_timer_) || !m_timer_->StartTimer(this, m_keep_time_))
    {
        LOG_ERROR("m_timer_ %p, this %p attach timer failed, error", m_timer_, this);
        return false;
    }
    
    Frame *frame = GetInstance<Frame>();
    EventProxyer *proxyer = frame->GetEventProxyer();
    if (proxyer->AddEventer(m_ev_))
    {
        m_keep_flag_ |= eTCP_KEEP_IN_POLL;
        return true;
    }
    else
    {
        LOG_ERROR("this %p attach failed, error", this);
        return false;
    }
}

bool TcpKeepClientConnection::IdleDetach()
{
    if (m_osfd_ < 0)
    {
        LOG_ERROR("this %p detach failed, fd %d error", this, m_osfd_);
        return false;
    }

    if (!(m_keep_flag_ & eTCP_KEEP_IN_POLL))
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
        EventProxyer *proxyer = frame->GetEventProxyer();
        if (proxyer->DelEventer(m_ev_))
        {
            m_keep_flag_ &= ~eTCP_KEEP_IN_POLL;
            return true;
        }
        else
        {
            LOG_ERROR("this %p detach failed, error", this);
            return false;
        }
        // 释放eventer
        IMtConnection::ResetEventer();
    }

    return true;
}

// 超时处理
void TcpKeepClientConnection::Notify(eEventType type)
{
    switch (type)
    {
        eEVENT_TIMEOUT: // 超时处理
            LOG_TRACE("keep timeout[%u], fd %d, close connection", m_keep_time_, m_osfd_);
            GetInstance<ConnectionPool>()->CloseIdleTcpKeep(this);
        default:
            break;
    }
}

// 找到对应的连接
TcpKeepClientConnection* TcpKeepPool::GetConnection(struct sockaddr_in* dst)
{
    LOG_TRACE("dst : %p", dst);
    TcpKeepClientConnection* conn = NULL;
    if (NULL == dst)
    {
        LOG_ERROR("input param dst null, error");
        return NULL;
    }

    TcpKeepKey key(dst);
    TcpKeepKey* conn_list = (TcpKeepKey*)(m_keep_map_->HashFindData(&key));
    LOG_TRACE("conn_list : %p", conn_list);
    if ((NULL == conn_list) || (NULL == conn_list->GetFirstConn()))
    {
        conn = m_keep_queue_.AllocPtr();
        if (conn)
        {
            conn->SetDestAddr(dst);
            // 设置监听
            HeapTimer* timer = GetInstance<Frame>()->GetHeapTimer();
            if (NULL != timer)
            {
                conn->SetHeapTimer(timer);
            }
        }
    }
    else
    {
        conn = conn_list->GetFirstConn();
        conn_list->RemoveConn(conn);
        conn->IdleDetach(); // 如果存在链接则释放绑定事件
    }

    return conn;
}
// 移除tcp的conn链接
bool TcpKeepPool::RemoveConnection(TcpKeepClientConnection* conn)
{
    struct sockaddr_in* dst = conn->GetDestAddr();
    if ((dst->sin_addr.s_addr == 0) || (dst->sin_port == 0))
    {
        LOG_ERROR("sock addr invalid, %x:%d", dst->sin_addr.s_addr, dst->sin_port);
        return false;
    }

    TcpKeepKey key(dst);
    TcpKeepKey* conn_list = (TcpKeepKey*)m_keep_map_->HashFindData(&key);
    if (!conn_list)
    {
        LOG_ERROR("no conn cache list invalid, %x:%d", dst->sin_addr.s_addr, dst->sin_port);
        return false;
    }
    conn->IdleDetach();
    conn_list->RemoveConn(conn);

    return true;
}
bool TcpKeepPool::CacheConnection(TcpKeepClientConnection* conn)
{
    struct sockaddr_in* dst = conn->GetDestAddr();
    if ((dst->sin_addr.s_addr == 0) || (dst->sin_port == 0))
    {
        LOG_ERROR("sock addr invalid, %x:%d", dst->sin_addr.s_addr, dst->sin_port);
        return false;
    }

    TcpKeepKey key(dst);
    TcpKeepKey* conn_list = (TcpKeepKey*)(m_keep_map_->HashFindData(&key));
    if (!conn_list)
    {
        conn_list = new TcpKeepKey(conn->GetDestAddr());
        if (!conn_list)
        {
            LOG_ERROR("new conn list failed, error");
            return false;
        }
        m_keep_map_->HashInsert(conn_list);
    }

    if (!conn->IdleAttach())
    {
        LOG_ERROR("conn IdleAttach failed, error");
        return false;
    }
    conn->ConnReuseClean();
    conn_list->InsertConn(conn);

    return true;
}

void TcpKeepPool::FreeConnection(TcpKeepClientConnection* conn, bool force_free)
{
    if (force_free)
    {
        conn->Reset();
        m_keep_queue_.FreePtr(conn);
        return ;
    }
    else
    {
        if (!CacheConnection(conn))
        {
            conn->Reset();
            m_keep_queue_.FreePtr(conn);
            return ;
        }
    }
}

IMtConnection* ConnectionPool::GetConnection(eConnType type, struct sockaddr_in* dst)
{
    switch (type)
    {
        case eUDP_CLIENT_CONN: return m_udp_queue_.AllocPtr();
        case eTCP_SHORT_CLIENT_CONN: return m_tcp_short_queue_.AllocPtr(); // tcp短连接
        case eTCP_KEEP_CLIENT_CONN: return m_tcp_keep_.GetConnection(dst); // tcp长连接
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
    switch (type)
    {
        case eUDP_CLIENT_CONN: return m_udp_queue_.FreePtr(dynamic_cast<UdpClientConnection*>(conn));
        case eTCP_SHORT_CLIENT_CONN: return m_tcp_short_queue_.FreePtr(dynamic_cast<TcpShortClientConnection*>(conn));
        case eTCP_KEEP_CLIENT_CONN: return m_tcp_keep_.FreeConnection(dynamic_cast<TcpKeepClientConnection*>(conn), force_free);
        default: break;
    }
    // 重置数据
    conn->Reset();

    return ;
}

void ConnectionPool::CloseIdleTcpKeep(IMtConnection* conn)
{
    TcpKeepClientConnection* _conn = (TcpKeepClientConnection *)conn;
    m_tcp_keep_.RemoveConnection(_conn);
    m_tcp_keep_.FreeConnection(_conn, true);
}