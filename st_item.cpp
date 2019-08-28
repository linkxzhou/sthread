/*
 * Copyright (C) zhoulv2000@163.com
 */

#include "st_item.h"

ST_NAMESPACE_USING

int StEventItem::InputNotify()
{
    LOG_TRACE("InputNotify ###, thread : %p", m_thread_);
    if (NULL == m_thread_)
    {
        LOG_ERROR("m_thread_ is NULL");
        return -1;
    }

    return 0;
}

int StEventItem::OutputNotify()
{
    LOG_TRACE("OutputNotify ###, thread : %p", m_thread_);
    if (NULL == m_thread_)
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

int StConnectionItem::SendData()
{
    ASSERT(m_buff_ != NULL);

    char* buff = (char*)m_buff_->GetBuffer();
    uint32_t buff_len = m_buff_->GetMsgLen();

    // 已经发送的数据长度
    int have_send_len = m_buff_->GetHaveSendLen(); 
    int ret = ::_send(m_osfd_, buff + have_send_len, 
        buff_len - have_send_len, 0, m_timeout_);

    LOG_TRACE("send: %s", buff + have_send_len);

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
        m_buff_->SetHaveSendLen(have_send_len);
    }

    if (have_send_len >= buff_len)
    {
        return buff_len;
    }
    else
    {
        return 0;
    }
}

int StConnectionItem::RecvData()
{
    ASSERT(m_buff_ != NULL);

    char* buff = (char*)m_buff_->GetBuffer();
    uint32_t buff_len = m_buff_->GetMsgLen();

    // 已经收到的数据长度
    int have_recv_len = m_buff_->GetHaveRecvLen();
    int ret = ::_recv(m_osfd_, (char*)buff + have_recv_len, 
        buff_len - have_recv_len, 0, m_timeout_);
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
        LOG_ERROR("tcp remote close, address: %s", m_addr_.IPPort());
        return -1;
    }
    else
    {
        have_recv_len += ret;
        m_buff_->SetHaveRecvLen(have_recv_len);
    }

    m_buff_->SetMsgLen(have_recv_len);
    return have_recv_len;
}