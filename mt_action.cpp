/*
 * Copyright (C) zhoulv2000@163.com
 */

#include "mt_action.h"
#include "mt_session.h"
#include "mt_frame.h"

MTHREAD_NAMESPACE_USING
using namespace std;

// 重置连接
void IMtAction::Reset(bool reset_all)
{
    bool force_free = false;
    if (m_errno_ != eERR_NONE)
    {
        force_free = true;
    }

    if (NULL != m_conn_)
    {
        GetInstance<ConnectionPool>()->FreeConnection((IMtConnection *)m_conn_, force_free);
        m_conn_ = NULL;
    }

    if (reset_all)
    {
        m_flag_       = eACTION_FLAG_UNDEF;
        m_conn_type_  = eTCP_SHORT_CLIENT_CONN;
        m_errno_      = eERR_NONE;
        m_time_cost_  = 0;
        m_buff_size_  = 0;
        m_msg_        = NULL;
        m_conn_       = NULL;
        memset(&m_addr_, 0, sizeof(m_addr_));
    }
}

Eventer* IMtAction::GetEventer()
{
    IMtConnection* conn = GetIConnection();
    if (conn)
    {
        return conn->GetEventer();
    }
    else
    {
        return NULL;
    }
}

// 初始化环境
int IMtAction::InitConnection()
{
    ConnectionPool* conn = GetInstance<ConnectionPool>();
    IMsgBufferPool* buf_pool = GetInstance<IMsgBufferPool>();
    ISessionEventerPool* ev_session = GetInstance<ISessionEventerPool>();
    Frame* frame = GetInstance<Frame>();
    EventProxyer* proxyer = frame->GetEventProxyer();

    eEventType event_type = eEVENT_THREAD;
    eConnType conn_type = GetConnType();

    m_conn_ = conn->GetConnection(conn_type, this->GetMsgDstAddr());
    if (!m_conn_)
    {
        LOG_ERROR("conn failed, type: %d", conn_type);
        return -1;
    }
    m_conn_->SetIMtActon(this);
    m_conn_->SetMsgDstAddr(this->GetMsgDstAddr());

    int max_len = this->GetMsgBufferSize();
    IMtMsgBuffer* msg_buff = buf_pool->GetMsgBuffer(max_len);
    if (!msg_buff)
    {
        LOG_ERROR("memory buff size: %d, get failed", max_len);
        return -2;
    }
    msg_buff->SetBufferType(eBUFF_SEND);
    m_conn_->SetIMtMsgBuffer(msg_buff);

    Eventer* ev = ev_session->GetEventer(event_type);
    if (!ev)
    {
        LOG_ERROR("memory type: %d, get failed", event_type);
        return -3;
    }
    
    LOG_TRACE("ev : %p", ev);
    ev->SetOwnerProxyer(proxyer);
    m_conn_->SetEventer(ev);
    ThreadBase* thread = frame->GetActiveThread();
    ev->SetOwnerThread(thread);
    // TODO : 用户自己设置
    // this->SetIMessagePtr((IMessage*)(thread->GetThreadArgs()));

    return 0;
}

int IMtAction::DoEncode()
{
    IMtMsgBuffer* msg_buff = NULL;
    if (m_conn_)
    {
        msg_buff = m_conn_->GetIMtMsgBuffer();
    }
    if (!m_conn_ || !msg_buff)
    {
        LOG_ERROR("conn(%p) or msg_buff(%p) null", m_conn_, msg_buff);
        return -100;
    }

    int msg_len = msg_buff->GetMaxLen();
    LOG_TRACE("msg_len : %d, m_msg_ : %p", msg_len, m_msg_);
    int ret = HandleEncode(msg_buff->GetMsgBuffer(), msg_len, m_msg_);
    if (ret < 0)
    {
        LOG_DEBUG("handle encode failed, ret %d", ret);
        return ret;
    }
    msg_buff->SetMsgLen(msg_len);

    return 0;
}

int IMtAction::DoInput()
{
    IMtMsgBuffer* msg_buff = NULL;
    if (m_conn_)
    {
        msg_buff = m_conn_->GetIMtMsgBuffer();
    }
    if (!m_conn_ || !msg_buff)
    {
        LOG_ERROR("conn(%p) or msg_buff(%p) null", m_conn_, msg_buff);
        return -100;
    }

    int msg_len = msg_buff->GetHaveRecvLen();
    int ret = HandleInput(msg_buff->GetMsgBuffer(), msg_len, m_msg_);
    if (ret < 0)
    {
        LOG_DEBUG("handle input failed, ret %d", ret);
    }

    return ret;
}
// 处理函数
int IMtAction::DoProcess()
{
    IMtMsgBuffer* msg_buff = NULL;
    if (m_conn_)
    {
        msg_buff = m_conn_->GetIMtMsgBuffer();
    }
    if (!m_conn_ || !msg_buff)
    {
        LOG_ERROR("conn(%p) or msg buff(%p) null", m_conn_, msg_buff);
        return -100;
    }

    int msg_len = msg_buff->GetMsgLen();
    int ret = HandleProcess(msg_buff->GetMsgBuffer(), msg_len, m_msg_);
    if (ret < 0)
    {
        LOG_DEBUG("handle process failed, ret %d", ret);
        return ret;
    }

    return 0;
}
int IMtAction::DoError()
{
    return HandleError((int)m_errno_, m_msg_);
}

IMtAction::IMtAction()
{
    Reset();
}
IMtAction::~IMtAction()
{
    Reset(true);
}

int IMtActionClient::SendRecv(int timeout)
{
    int ret = 0;

    LOG_TRACE("m_action_list_ size : %d", m_action_list_.size());

    for (IMtActionList::iterator iter = m_action_list_.begin(); 
        iter != m_action_list_.end(); ++iter)
    {
        IMtAction* action = *iter;
        if (!action || action->InitConnection() < 0)
        {
            LOG_ERROR("invalid action(%p) or int failed, error", action);
            return -1;
        }

        LOG_TRACE("action -> DoEncode");
        ret = action->DoEncode();
        if (ret < 0)
        {
            action->SetErrno(eERR_ENCODE_ERROR);
            LOG_ERROR("pack action pkg failed, action(%p), ret %d", action, ret);
            continue;
        }
    }

    RunSendRecv(timeout);

    for (IMtActionList::iterator iter = m_action_list_.begin(); 
        iter != m_action_list_.end(); ++iter)
    {
        IMtAction* action = *iter;
        if (action->GetMsgFlag() != eACTION_FLAG_FIN)
        {
            action->DoError();
            LOG_DEBUG("send recv failed: %d", action->GetErrno());
            continue;
        }
        ret = action->DoProcess();
        if (ret < 0)
        {
            LOG_DEBUG("action process failed: %d", ret);
            continue;
        }
    }

    // 最后处理
    for (IMtActionList::iterator iter = m_action_list_.begin(); 
        iter != m_action_list_.end(); ++iter)
    {
        IMtAction* action = *iter;
        action->Reset();
    }

    return 0;
}

int IMtActionClient::Poll(IMtActionList list, int mask, int timeout)
{
    LOG_CHECK_FUNCTION;

    EventerList evlist;
    CPP_TAILQ_INIT(&evlist);

    Eventer* ev = NULL;
    IMtAction* action = NULL;
    for (IMtActionList::iterator iter = list.begin(); 
        iter != list.end(); ++iter)
    {
        action = *iter;
        if (action) 
        {
            ev = action->GetEventer();
        }
        if (!action || !ev)
        {
            action->SetErrno(eERR_FRAME_ERROR);
            LOG_ERROR("input action %p or ev null", action);
            return -1;
        }
        ev->SetRecvEvents(0);
        if (mask & MT_READABLE)
        {
            ev->EnableInput();
        }
        else
        {
            ev->DisableInput();
        }
        if (mask & MT_WRITABLE)
        {
            ev->EnableOutput();
        }
        else
        {
            ev->DisableOutput();
        }
        CPP_TAILQ_INSERT_TAIL(&evlist, ev, m_entry_);
    }

    LOG_TRACE("list size : %d", list.size());

    Frame* frame = GetInstance<Frame>();
    Thread* thread = (Thread* )frame->GetActiveThread();
    EventProxyer* proxyer = frame->GetEventProxyer();
    if (!proxyer || !proxyer->Schedule(thread, &evlist, NULL, (int)timeout))
    {
        if (errno != ETIME)
        {
            action->SetErrno(eERR_POLL_FAIL);
            LOG_ERROR("frame %p, proxyer %p schedule failed, errno %d", 
                frame, proxyer, errno);
            return -2;
        }
        return -3;
    }

    return 0;
}

int IMtActionClient::NewSock()
{
    int sock = -1, hasok_count = 0;
    IMtAction* action = NULL;
    IMtConnection* connection = NULL;

    for (IMtActionList::iterator iter = m_action_list_.begin(); 
        iter != m_action_list_.end(); ++iter)
    {
        action = *iter;
        if (NULL == action)
        {
            action->SetErrno(eERR_PARAM_ERROR);
            LOG_ERROR("Invalid param, action %p null!!", action);
            return -1;
        }
        if (action->GetErrno() != eERR_NONE)
        {
            continue;
        }
        connection = action->GetIConnection();
        if (NULL == connection)
        {
            action->SetErrno(eERR_FRAME_ERROR);
            LOG_ERROR("Invalid param, conn %p null!!", connection);
            return -2;
        }
        sock = connection->CreateSocket();
        if (sock < 0)
        {
            action->SetErrno(eERR_SOCKET_FAIL);
            LOG_ERROR("Get sock data failed, ret %d, errno %d!!", sock, errno);
            return -3;
        }
        ++hasok_count;
        action->SetMsgFlag(eACTION_FLAG_INIT);
    }
    // 不同的错误返回不同的值
    if (hasok_count >= m_action_list_.size())
    {
        return 0;
    }
    else if (0 == hasok_count)
    {
        return -4;
    }
    else
    {
        return 5;
    }
}

int IMtActionClient::Open(int timeout)
{
    Frame* frame = GetInstance<Frame>();
    utime64_t start_ms = frame->GetLastClock();
    utime64_t end_ms = start_ms + timeout;
    utime64_t cur_ms = 0;

    int ret = 0, hasopen = 0;
    IMtAction* action = NULL;
    IMtConnection* connection = NULL;

    while (true)
    {
        IMtActionList waitlist;
        for (IMtActionList::iterator iter = m_action_list_.begin(); 
            iter != m_action_list_.end(); ++iter)
        {
            action = *iter;
            if (action->GetErrno() != eERR_NONE)
            {
                continue;
            }
            if (action->GetMsgFlag() == eACTION_FLAG_OPEN)
            {
                hasopen = 1;
                continue;
            }
            connection = action->GetIConnection();
            if (NULL == connection)
            {
                action->SetErrno(eERR_FRAME_ERROR);
                LOG_ERROR("Invalid param, conn %p null", connection);
                return -1;
            }
            ret = connection->OpenConnect();
            if (ret < 0)
            {
                waitlist.push_back(action);
            }
            else
            {
                action->SetMsgFlag(eACTION_FLAG_OPEN);
            }
        }
        cur_ms = frame->GetLastClock();
        if (cur_ms > end_ms)
        {
            LOG_DEBUG("Open connect timeout, errno : %d", errno);
            for (IMtActionList::iterator iter = waitlist.begin(); 
                iter != waitlist.end(); ++iter)
            {
                (*iter)->SetErrno(eERR_CONNECT_FAIL);
            }
            if (!hasopen)
            {
                return 0;
            }
            else
            {
                return -2;
            }
        }
        if (!waitlist.empty())
        {
            Poll(waitlist, MT_WRITABLE, end_ms - cur_ms);
        }
        else
        {
            return 0;
        }
    }
}

int IMtActionClient::Sendto(int timeout)
{
    Frame* frame = GetInstance<Frame>();
    utime64_t start_ms = frame->GetLastClock();
    utime64_t end_ms = start_ms + timeout;
    utime64_t cur_ms = 0;

    int ret = 0, hassend = 0;
    IMtAction* action = NULL;
    IMtConnection* connection = NULL;

    while (true)
    {
        IMtActionList waitlist;
        for (IMtActionList::iterator iter = m_action_list_.begin(); 
            iter != m_action_list_.end(); ++iter)
        {
            action = *iter;
            if (action->GetErrno() != eERR_NONE)
            {
                continue;
            }
            if (action->GetMsgFlag() == eACTION_FLAG_SEND)
            {
                hassend = 1;
                continue;
            }
            connection = action->GetIConnection();
            if (NULL == connection)
            {
                action->SetErrno(eERR_FRAME_ERROR);
                LOG_ERROR("Invalid param, conn %p null", connection);
                return -2;
            }
            ret = connection->SendData();
            if (ret == -1)
            {
                action->SetErrno(eERR_SEND_FAIL);
                LOG_ERROR("msg send error %d", errno);
                continue;
            }
            else if (ret == 0)
            {
                waitlist.push_back(action);
                continue;
            }
            else
            {
                action->SetMsgFlag(eACTION_FLAG_SEND);
            }
        }
        cur_ms = frame->GetLastClock();
        if (cur_ms > end_ms)
        {
            LOG_DEBUG("send data timeout");
            for (IMtActionList::iterator iter = waitlist.begin(); 
                iter != waitlist.end(); ++iter)
            {
                (*iter)->SetErrno(eERR_SEND_FAIL);
            }
            if (hassend)
            {
                return 0;
            }
            else
            {
                return -5;
            }
        }
        if (!waitlist.empty())
        {
            Poll(waitlist, MT_WRITABLE, end_ms - cur_ms);
        }
        else
        {
            return 0;
        }
    }

    return 0;
}

int IMtActionClient::Recvfrom(int timeout)
{
    Frame* frame = GetInstance<Frame>();
    utime64_t start_ms = frame->GetLastClock();
    utime64_t end_ms = start_ms + timeout;
    utime64_t cur_ms = 0;

    int ret = 0;
    IMtAction* action = NULL;
    IMtConnection* connection = NULL;

    while (true)
    {
        IMtActionList waitlist;
        for (IMtActionList::iterator iter = m_action_list_.begin(); 
            iter != m_action_list_.end(); ++iter)
        {
            action = *iter;
            if (action->GetErrno() != eERR_NONE) 
            {
                continue;
            }
            if (eACTION_FLAG_FIN == action->GetMsgFlag())
            {
                continue;
            }
            connection = action->GetIConnection();
            if (NULL == connection)
            {
                action->SetErrno(eERR_FRAME_ERROR);
                LOG_ERROR("Invalid param, conn %p null", connection);
                return -2;
            }
            ret = connection->RecvData();
            if (ret < 0)
            {
                action->SetErrno(eERR_RECV_FAIL);
                LOG_ERROR("recv failed: %p", connection);
                continue;
            }
            else if (ret == 0)
            {
                waitlist.push_back(action);
                continue;
            }
            else
            {
                action->SetMsgFlag(eACTION_FLAG_FIN);
                action->SetCost(frame->GetLastClock() - start_ms);
            }
        }
        cur_ms = frame->GetLastClock();
        if (cur_ms > end_ms)
        {
            LOG_DEBUG("Recv data timeout, cur_ms: %llu, start_ms: %llu", cur_ms, start_ms);
            for (IMtActionList::iterator iter = waitlist.begin(); 
                iter != waitlist.end(); ++iter)
            {
                (*iter)->SetErrno(eERR_RECV_TIMEOUT);
            }
            return -5;
        }

        if (!waitlist.empty())
        {
            Poll(waitlist, MT_READABLE, end_ms - cur_ms);
        }
        else
        {
            return 0;
        }
    }
}

int IMtActionClient::RunSendRecv(int timeout)
{
    Frame* frame = GetInstance<Frame>();
    utime64_t start_ms = frame->GetLastClock();
    utime64_t cur_ms = 0;

    int rc = NewSock();
    if (rc < 0)
    {
        LOG_ERROR("new sock failed, ret: %d", rc);
        return -1;
    }

    rc = Open(timeout);
    if (rc < 0)
    {
        LOG_ERROR("open failed, ret: %d", rc);
        return -2;
    }

    cur_ms = frame->GetLastClock();
    rc = Sendto(timeout - (cur_ms - start_ms));
    if (rc < 0)
    {
        LOG_ERROR("send failed, ret: %d", rc);
        return -3;
    }

    cur_ms = frame->GetLastClock();
    rc = Recvfrom(timeout - (cur_ms - start_ms));
    if (rc < 0)
    {
        LOG_ERROR("recv failed, ret: %d", rc);
        return -4;
    }

    return 0;
}