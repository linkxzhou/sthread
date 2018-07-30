/*
 * Copyright (C) zhoulv2000@163.com
 */

#include "mt_session.h"
#include "mt_action.h"
#include "mt_frame.h"

MTHREAD_NAMESPACE_USING
using namespace std;

void ISessionEventer::InsertWriteWait(ISessionEventer* sev)
{
    if (eSESSION_UNSET == sev->m_flag_)
    {
        CPP_TAILQ_INSERT_TAIL(&m_list_, sev, m_entry_);
        sev->m_flag_ = eSESSION_SET;
    }
}

void ISessionEventer::RemoveWriteWait(ISessionEventer* sev)
{
    if (eSESSION_SET == sev->m_flag_)
    {
        CPP_TAILQ_REMOVE(&m_list_, sev, m_entry_);
        sev->m_flag_ = eSESSION_UNSET;
    }
}

Eventer* ISessionEventerPool::GetEventer(int type)
{
    Eventer* ev = NULL;
    ISessionEventer* sev = NULL;

    switch (type)
    {
        case eEVENT_THREAD:
        {
            ev = m_ev_queue_.AllocPtr(); // 分配一个Eventer指针
            ev->SetEventerType(eEVENT_THREAD);
            break;
        }
        case eEVENT_KEEPALIVE: 
        default: break;
    }

    return ev;
}

void ISessionEventerPool::FreeEventer(Eventer* ev)
{
    ISessionEventer* sev = NULL;
    if (NULL == ev)
    {
        LOG_WARN("ev is NULL");
        return ;
    }

    int type = ev->GetEventerType();
    switch (type)
    {
        case eEVENT_THREAD:
        {
            m_ev_queue_.FreePtr(ev);
            break;
        }
        case eEVENT_KEEPALIVE:
        default: break;
    }

    ev->Reset(); // 重置
    return;
}
