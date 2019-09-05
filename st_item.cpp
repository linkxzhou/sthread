/*
 * Copyright (C) zhoulv2000@163.com
 */

#include "st_item.h"
#include "st_manager.h"

ST_NAMESPACE_USING

int StEventItem::InputNotify()
{
    LOG_TRACE("InputNotify ###, thread : %p", m_thread_);
    if (unlikely(NULL == m_thread_))
    {
        LOG_ERROR("m_thread_ is NULL");
        return -1;
    }

    return 0;
}

int StEventItem::OutputNotify()
{
    LOG_TRACE("OutputNotify ###, thread : %p", m_thread_);
    if (unlikely(NULL == m_thread_))
    {
        LOG_ERROR("m_thread_ is NULL");
        return -1;
    }

    return 0;
}

int StEventItem::HangupNotify()
{
    LOG_TRACE("HangupNotify ###, thread : %p", m_thread_);

    return 0;
}

void StConnectionItem::SetRecvBuffer(uint32_t len)
{
    if (m_recvbuf_ != NULL)
    {
        GetInstance< StBufferPool<> >()->FreeBuffer(m_recvbuf_);
    }

    m_recvbuf_ = GetInstance< StBufferPool<> >()->GetBuffer(len);
}

void StConnectionItem::SetSendBuffer(uint32_t len)
{
    if (m_sendbuf_ != NULL)
    {
        GetInstance< StBufferPool<> >()->FreeBuffer(m_sendbuf_);
    }

    m_sendbuf_ = GetInstance< StBufferPool<> >()->GetBuffer(len);
}

int StConnectionItem::SendData()
{
    ASSERT(m_sendbuf_ != NULL);

    char *buf = (char*)m_sendbuf_->GetBuffer();
    uint32_t buf_len = m_sendbuf_->GetMsgLen();

    // 已经发送的数据长度
    int have_send_len = m_sendbuf_->GetHaveSendLen(); 
    int ret = ::_send(m_osfd_, buf + have_send_len, 
        buf_len - have_send_len, 0, m_timeout_);

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

int StConnectionItem::RecvData()
{
    if (m_recvbuf_ == NULL)
    {
        SetRecvBuffer();
    }

    char *buf = (char*)m_recvbuf_->GetBuffer();
    int buf_maxlen = m_recvbuf_->GetMaxLen();

    // 已经收到的数据长度
    int have_recv_len = m_recvbuf_->GetHaveRecvLen();
    LOG_TRACE("m_osfd_: %d, buf: %p, have_recv_len: %d, "
        "m_timeout_: %d, buf_maxlen: %d", 
        m_osfd_, buf, have_recv_len, m_timeout_, buf_maxlen);
    int ret = ::_recv(m_osfd_, (char*)buf + have_recv_len, 
        buf_maxlen - have_recv_len, 0, m_timeout_);
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

    return have_recv_len;
}