/*
 * Copyright (C) zhoulv2000@163.com
 */

#include "st_connection.h"
#include "st_manager.h"

ST_NAMESPACE_USING

int32_t StConnection::SendData()
{
    ASSERT(m_sendbuf_ != NULL);

    int32_t len = m_sendbuf_->GetMaxLen();
    int ret = HandleOutput(m_sendbuf_->GetBuffer(), len);
    if (ret < 0)
    {
        LOG_ERROR("HandleOutput failed, ret: %d", ret);
        return ret;
    }

    // 设置消息的长度
    m_sendbuf_->SetMsgLen(len);

    char *buf = (char*)m_sendbuf_->GetBuffer();
    uint32_t buf_len = m_sendbuf_->GetMsgLen();

    int have_send_len = m_sendbuf_->GetHaveSendLen(); 
    if (IS_UDP_CONN(m_type_))
    {
        struct sockaddr *servaddr;
        m_destaddr_.GetSockAddr(servaddr);
        ret = ::_sendto(m_osfd_, buf + have_send_len, buf_len - have_send_len, 
            0, servaddr, sizeof(struct sockaddr), m_timeout_);
    }
    else
    {
        ret = ::_send(m_osfd_, buf + have_send_len, buf_len - have_send_len, 
            0, m_timeout_);
    }

    LOG_TRACE("send: %s", buf + have_send_len);

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
        m_sendbuf_->SetHaveSendLen(have_send_len);
    }

    if (have_send_len >= buf_len)
    {
        return buf_len;
    }
    else
    {
        return 0;
    }
}

int32_t StConnection::RecvData()
{
    ASSERT(m_recvbuf_ != NULL);

    char *buf = (char*)m_recvbuf_->GetBuffer();
    int buf_maxlen = m_recvbuf_->GetMaxLen();

    // 已经收到的数据长度
    int have_recv_len = m_recvbuf_->GetHaveRecvLen();
    LOG_TRACE("osfd: %d, buf: %p, have_recv_len: %d, "
        "timeout: %d, buf_maxlen: %d", 
        m_osfd_, buf, have_recv_len, m_timeout_, buf_maxlen);

    int ret = 0;
    if (IS_UDP_CONN(m_type_))
    {
        // 设置目的IP地址
        struct sockaddr clientaddr;
        socklen_t addrlen = sizeof(struct sockaddr);
        ret = ::_sendto(m_osfd_, (char*)buf + have_recv_len, 
            buf_maxlen - have_recv_len, 0, &clientaddr, addrlen, m_timeout_);
        m_destaddr_ = StNetAddress(*((struct sockaddr_in*)&clientaddr));
    }
    else
    {
        ret = ::_recv(m_osfd_, (char*)buf + have_recv_len, 
            buf_maxlen - have_recv_len, 0, m_timeout_);
    }

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
        LOG_ERROR("tcp remote close, address: %s", m_destaddr_.IPPort());
        return -1;
    }
    else
    {
        have_recv_len += ret;
        m_recvbuf_->SetHaveRecvLen(have_recv_len);
    }

    // 处理收到的buffer数据
    ret = HandleInput(m_recvbuf_->GetBuffer(), m_recvbuf_->GetHaveRecvLen());
    if (ret > 0)
    {
        m_recvbuf_->SetMsgLen(ret);
    }
    else if (ret == 0)
    {
        return 0;
    }
    else if (ret == -65535)
    {
        m_recvbuf_->SetHaveRecvLen(0);
        return 0;
    }
    else
    {
        LOG_ERROR("HandleInput failed, ret: %d", ret);
        return -1;
    }

    return 0;
}