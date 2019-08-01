/*
 * Copyright (C) zhoulv2000@163.com
 */

#include "mt_ext.h"
#include "mt_action.h"
#include "mt_frame.h"

ST_NAMESPACE_USING
using namespace std;

Eventer* EventerPool::GetEventer(int type)
{
    Eventer* ev = NULL;

    switch (type)
    {
        case eEVENT_THREAD:
        {
            ev = m_ev_pool_.AllocPtr(); // 分配一个Eventer指针
            ev->SetEventerType(eEVENT_THREAD);
            break;
        }
        case eEVENT_KEEPALIVE: 
        {
            ev =  m_tcplong_ev_pool_.AllocPtr();
            ev->SetEventerType(eEVENT_KEEPALIVE);
            break;
        }
        default: break;
    }

    return ev;
}

void EventerPool::FreeEventer(Eventer* ev)
{
    if (NULL == ev)
    {
        LOG_WARN("ev is NULL");
        return ;
    }

    int type = ev->GetEventerType();
    ev->Reset(); // 重置
    switch (type)
    {
        case eEVENT_THREAD:
        {
            m_ev_pool_.FreePtr(ev);
            break;
        }
        case eEVENT_KEEPALIVE:
        {
            m_tcplong_ev_pool_.FreePtr((TcpLongEventer *)ev);
            break;
        }
        default: break;
    }

    return;
}
