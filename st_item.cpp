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

    if (m_thread_->HasFlag(eIO_LIST))
    {
        return m_thread_->IOWaitToRunable();
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

    if (m_thread_->HasFlag(eIO_LIST))
    {
        return m_thread_->IOWaitToRunable();
    }

    return 0;
}

int StEventItem::HangupNotify()
{
    LOG_TRACE("HangupNotify ###, thread : %p", m_thread_);

    return 0;
}