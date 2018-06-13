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

// 通知写入等待
void UdpSessionEventer::NotifyWriteWait()
{
    ISessionEventer* ev = NULL;
    ThreadBase* thread = NULL;
    CPP_TAILQ_FOREACH(ev, &m_list_, m_entry_)
    {
        ev->SetRecvEvents(MT_WRITABLE);

        thread = ev->GetThread();
        if (NULL != thread && thread->HasFlag(eIO_LIST))
        {
            thread->RemoveIoWait();
            thread->InsertRunable();
        }
    }
}

int UdpSessionEventer::CloseSocket()
{
    if (NULL == m_conn_)
    {
        LOG_ERROR("m_conn_ is NULL");
        return -1;
    }

    int osfd = m_conn_->GetOsfd();
    if (osfd <= 0)
    {
        LOG_ERROR("osfd <= 0");
        return -2;
    }

    if (NULL != m_proxyer_)
    {
        m_proxyer_->DelFd(osfd, MT_READABLE);
        m_proxyer_->SetEventer(osfd, NULL);
    }
    this->DisableInput();
    this->SetOsfd(-1);
    // 释放链接到连接池
    GetInstance<ConnectionCtrl>()->FreeConnection(m_conn_, true);

    return 0;
}

int UdpSessionEventer::InputNotify()
{
    if (NULL == m_conn_)
    {
        LOG_ERROR("m_conn_ is NULL");
        return -1;
    }

    // while (true)
    // {
    //     if (NULL == m_msg_buff_)
    //     {
    //         m_msg_buff_ = GetInstance<IMsgBufferPool>()->GetMsgBuffer(this->GetMsgBufferSize());
    //         if (NULL == m_msg_buff_)
    //         {
    //             LOG_ERROR("get memory failed, size %d, wait next time", this->GetMsgBufferSize());
    //             return 0;
    //         }
    //         m_msg_buff_->SetBufferType(eBUFF_RECV);
    //         m_conn_->SetIMtMsgBuffer(m_msg_buff_);
    //     }
    //     // 接收数据
    //     int ret = m_conn_->RecvData();
    //     int have_recv_len = m_conn_->GetIMtMsgBuffer()->GetHaveSendLen();
    //     LOG_TRACE("ret : %d, have_recv_len : %d", ret, have_recv_len);
    //     // TODO : 异常处理

    //     int sessionid = 0;
    //     ret = this->GetSessionId(buf, have_recv_len, sessionid);
    //     if (ret <= 0)
    //     {
    //         LOG_ERROR("recv get session failed, len %d, fd %d, drop it", have_recv_len, osfd);
    //         GetInstance<IMsgBufferPool>()->FreeMsgBuffer(m_msg_buff_);
    //         m_msg_buff_ = NULL;

    //         return 0;
    //     }

    //     ISessionEventer* session_ev = GetInstance<ISessionCtrl>()->FindSession(sessionid);
    //     if (NULL == session_ev)
    //     {
    //         LOG_DEBUG("session_ev %d, not find, maybe timeout, drop pkg", sessionid);
    //         GetInstance<IMsgBufferPool>()->FreeMsgBuffer(m_msg_buff_);
    //         m_msg_buff_ = NULL;

    //         return 0;
    //     }

    //     IMtConnection* conn = session_ev->GetSessionConn();
    //     ThreadBase* thread = session_ev->GetOwnerThread();
    //     if (!thread || !conn || !conn->GetEventer())
    //     {
    //         LOG_ERROR("session_ev : %p, no thread ptr %p, no conn %p wrong", session_ev, thread, conn);
    //         GetInstance<IMsgBufferPool>()->FreeMsgBuffer(m_msg_buff_);
    //         m_msg_buff_ = NULL;

    //         return 0;
    //     }
    //     IMtMsgBuffer* msg = conn->GetIMtMsgBuffer();
    //     if (NULL != msg)
    //     {
    //         GetInstance<IMsgBufferPool>()->FreeMsgBuffer(msg);
    //     }
    //     conn->SetIMtMsgBuffer(m_msg_buff_);
    //     m_msg_buff_ = NULL;

    //     conn->GetEventer()->SetRecvEvents(MT_READABLE);
    //     if (thread->HasFlag(eIO_LIST))
    //     {
    //         // TODO : 未完成
    //         // frame->RemoveIoWait(thread);
    //         // frame->InsertRunable(thread);
    //     }
    // }

    return 0;
}

int UdpSessionEventer::OutputNotify()
{
    NotifyWriteWait();
    return 0;
}

int UdpSessionEventer::HangupNotify()
{
    if (NULL == m_conn_)
    {
        LOG_ERROR("m_conn_ is NULL");
        return -1;
    }

    int osfd = m_conn_->GetOsfd();
    if (osfd <= 0)
    {
        LOG_ERROR("osfd <= 0");
        return -2;
    }

    if (NULL != m_proxyer_)
    {
        m_proxyer_->DelFd(osfd, this->GetEvents());
        CloseSocket();
    }
    // TODO : 
    // 重新常见socket
    // CreateSocket();

    return 0;
}

int UdpSessionEventer::AddRef(void* args)
{
    FdRef* fd_ref = (FdRef*)args;
    int osfd = this->GetOsfd();

    Eventer* ev = fd_ref->GetEventer();
    if (ev != this)
    {
        LOG_ERROR("fdref conflict, fd: %d, old: %p, now: %p", osfd, ev, this);
        return -1;
    }

    if (NULL != m_proxyer_ && m_proxyer_->AddFd(osfd, MT_WRITABLE))
    {
        LOG_ERROR("epfd ref add failed, log");
        return -2;
    }
    this->EnableOutput();

    return 0;
}

int UdpSessionEventer::DelRef(void* args)
{
    FdRef* fd_ref = (FdRef*)args;
    int osfd = this->GetOsfd();

    Eventer* ev = fd_ref->GetEventer();
    if (ev != this)
    {
        LOG_ERROR("fdref conflict, fd: %d, old: %p, now: %p", osfd, ev, this);
        return -1;
    }

    if (NULL != m_proxyer_ && m_proxyer_->DelFd(osfd, MT_WRITABLE))
    {
        LOG_ERROR("epfd ref del failed, log");
        return -2;
    }
    this->DisableOutput();

    return 0;
}

// 注册sessionid
int ISessionEventerCtrl::RegisterSession(int session_name, ISessionEventer* session)
{
    if (session_name <= 0 || NULL == session)
    {
        LOG_ERROR("session %d, register %p failed", session_name, session);
        return -1;
    }

    ISessionMap::iterator it = m_session_map_.find(session_name);
    if (it != m_session_map_.end())
    {
        LOG_ERROR("session %d, register %p already", session_name, session);
        return -2;
    }
    m_session_map_[session_name] = session;

    return 0;
}

ISessionEventer* ISessionEventerCtrl::GetSessionName(int session_name)
{
    ISessionMap::iterator it = m_session_map_.find(session_name);
    if (it != m_session_map_.end())
    {
        return it->second;
    }
    else
    {
        return NULL;
    }
}

Eventer* ISessionEventerCtrl::GetEventer(int type, int session_name)
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
        case eEVENT_SESSION:
        {
            sev = m_sev_queue_.AllocPtr();
            ev = any_cast<ISessionEventer>(ev);
            ev->SetEventerType(eEVENT_SESSION);
            break;
        }
        case eEVENT_KEEPALIVE: 
        default: break;
    }

    // 如果是udp则使用session
    if (eEVENT_SESSION == type)
    {
        ISessionEventer* session_ev = this->GetSessionName(session_name);
        if (NULL == session_ev)
        {
            LOG_ERROR("session_ev get session name(%d) failed", session_name);
            this->FreeEventer(session_ev);
            return NULL;
        }
        else
        {
            // TODO :
        }
    }

    return ev;
}

void ISessionEventerCtrl::FreeEventer(Eventer* ev)
{
    ISessionEventer* sev = NULL;
    if (NULL == ev)
    {
        LOG_ERROR("ev is NULL");
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
        case eEVENT_SESSION:
        {
            sev = any_cast<ISessionEventer>(ev);
            m_sev_queue_.FreePtr(sev);
            break;
        }
        case eEVENT_KEEPALIVE:
        default: break;
    }
    ev->Reset();// 重置
    
    return;
}
