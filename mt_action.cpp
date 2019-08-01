/*
 * Copyright (C) zhoulv2000@163.com
 */

#include "mt_action.h"
#include "mt_ext.h"
#include "mt_frame.h"

ST_NAMESPACE_USING
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
        m_conn_type_  = eTCP_SHORT_CONN;
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
    EventerPool* ev_pool = GetInstance<EventerPool>();
    Frame* frame = GetInstance<Frame>();
    EventDriver* driver = frame->GetEventDriver();

    eEventType event_type = eEVENT_THREAD;
    eConnType conn_type = GetConnType();

    m_conn_ = conn->GetConnection(conn_type, GetMsgDstAddr());
    if (!m_conn_)
    {
        LOG_ERROR("conn failed, type: %d", conn_type);
        return -1;
    }
    m_conn_->SetIMtActon(this);
    m_conn_->SetMsgDstAddr(GetMsgDstAddr());

    int max_len = GetMsgBufferSize();
    IMtMsgBuffer* msg_buff = buf_pool->GetMsgBuffer(max_len);
    if (!msg_buff)
    {
        LOG_ERROR("memory buff size: %d, get failed", max_len);
        return -2;
    }
    msg_buff->SetBufferType(eBUFF_SEND);
    m_conn_->SetIMtMsgBuffer(msg_buff);

    Eventer* ev = ev_pool->GetEventer(event_type);
    if (!ev)
    {
        LOG_ERROR("memory type: %d, get failed", event_type);
        return -3;
    }
    
    ev->SetOwnerDriver(driver);
    m_conn_->SetEventer(ev);
    ThreadBase* thread = frame->GetActiveThread();
    ev->SetOwnerThread(thread);

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

// 同时调度IMtActionList
int IMtActionRunable::Poll(IMtActionList list, int mask, int timeout)
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
        
        // 清空recv事件
        ev->SetRecvEvents(0);
        if (mask & ST_READABLE)
        {
            ev->EnableInput();
        }
        else
        {
            ev->DisableInput();
        }

        if (mask & ST_WRITEABLE)
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
    EventDriver* driver = frame->GetEventDriver();
    if (!driver || !driver->Schedule(thread, &evlist, NULL, (int)timeout))
    {
        if (errno != ETIME)
        {
            action->SetErrno(eERR_POLL_FAIL);
            LOG_ERROR("frame %p, driver %p schedule failed, errno %d", 
                frame, driver, errno);
            return -2;
        }

        return -3;
    }

    return 0;
}

int IMtActionRunable::Sendto(IMtActionList list, int timeout)
{
    Frame* frame = GetInstance<Frame>();
    time64_t start_ms = frame->GetLastClock(), 
        end_ms = start_ms + timeout, current_ms = 0;

    int ret = 0, hassend = 0;
    IMtAction* action = NULL;
    IMtConnection* connection = NULL;

    do
    {
        IMtActionList waitlist;
        for (IMtActionList::iterator iter = list.begin(); iter != list.end(); ++iter)
        {
            action = *iter;
            if (action->GetErrno() != eERR_NONE)
            {
                LOG_ERROR("action: %p, error: %d", action, action->GetErrno());
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
                return -1;
            }

            ret = connection->SendData();
            LOG_DEBUG("ret: %d", ret);
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

        current_ms = frame->GetLastClock();
        if (current_ms > end_ms)
        {
            LOG_DEBUG("send data timeout");
            for (IMtActionList::iterator iter = waitlist.begin(); iter != waitlist.end(); ++iter)
            {
                (*iter)->SetErrno(eERR_SEND_TIMEOUT);
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

        LOG_DEBUG("waitlist size: %d", waitlist.size());

        if (!waitlist.empty())
        {
            Poll(waitlist, ST_WRITEABLE, end_ms - current_ms);
        }
        else
        {
            return 0;
        }
    } while (true);

    return 0;
}

int IMtActionRunable::Recvfrom(IMtActionList list, int timeout)
{
    Frame* frame = GetInstance<Frame>();
    time64_t start_ms = frame->GetLastClock(), end_ms = start_ms + timeout, current_ms = 0;

    int ret = 0;
    IMtAction* action = NULL;
    IMtConnection* connection = NULL;

    do
    {
        IMtActionList waitlist;
        for (IMtActionList::iterator iter = list.begin(); iter != list.end(); ++iter)
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
                LOG_ERROR("recv failed: %p, ret: %d", connection, ret);
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
        current_ms = frame->GetLastClock();
        if (current_ms > end_ms)
        {
            LOG_DEBUG("Recv data timeout, current_ms: %llu, start_ms: %llu, end_ms: %llu", 
                current_ms, start_ms, end_ms);
            for (IMtActionList::iterator iter = waitlist.begin(); 
                iter != waitlist.end(); ++iter)
            {
                (*iter)->SetErrno(eERR_RECV_TIMEOUT);
            }
            return -5;
        }

        LOG_DEBUG("waitlist size: %d", waitlist.size());

        if (!waitlist.empty())
        {
            Poll(waitlist, ST_READABLE, end_ms - current_ms);
        }
        else
        {
            return 0;
        }
    } while (true);
}

int IMtActionClient::SendRecv(int timeout)
{
    int ret = 0;

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
        return -5;
    }
}

int IMtActionClient::Open(int timeout)
{
    Frame* frame = GetInstance<Frame>();
    time64_t start_ms = frame->GetLastClock(), end_ms = start_ms + timeout, current_ms = 0;

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
        current_ms = frame->GetLastClock();
        if (current_ms > end_ms)
        {
            LOG_DEBUG("Open connect timeout, errno: %d", errno);
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
            Poll(waitlist, ST_WRITEABLE, end_ms - current_ms);
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

    rc = Sendto(m_action_list_, timeout);
    if (rc < 0)
    {
        LOG_ERROR("send failed, ret: %d", rc);
        return -3;
    }

    rc = Recvfrom(m_action_list_, timeout);
    if (rc < 0)
    {
        LOG_ERROR("recv failed, ret: %d", rc);
        return -4;
    }

    return 0;
}

_IMtAction_construct IMtActionServer::m_construct_callback_ = NULL;
_IMtAction_destruct IMtActionServer::m_destruct_callback_ = NULL;

int IMtActionServer::Loop(int timeout)
{
    Frame* frame = GetInstance<Frame>();
    EventDriver* driver = frame->GetEventDriver();

    int ret = m_accept_action_->InitConnection();
    if (ret < 0)
    {
        LOG_ERROR("InitConnection error, ret : %d", ret);
        return ret;
    }

    IMtConnection* accept_connection = m_accept_action_->GetIConnection();
    m_listen_fd_ = accept_connection->CreateSocket();
    if (m_listen_fd_ < 0)
    {
        LOG_ERROR("CreateSocket error, m_listen_fd_: %d", m_listen_fd_);
        return m_listen_fd_;
    }

    LOG_TRACE("m_listen_fd_ : %d", m_listen_fd_);

    IMtAction* action = NULL;
    IMtConnection* connection = NULL;
    NewThreadTransform *transform = NULL;
    do
    {
        Eventer* ev = NULL;
        if (m_accept_action_) 
        {
            ev = m_accept_action_->GetEventer();
        }

        if (!ev)
        {
            m_accept_action_->SetErrno(eERR_FRAME_ERROR);
            LOG_ERROR("input m_accept_action_ %p or ev NULL", m_accept_action_);
            ret = -1;
        }
        ev->EnableInput();
        ev->DisableOutput();
        Thread* thread = (Thread* )frame->GetActiveThread();
        if (!driver || !(driver->Schedule(thread, NULL, ev, (int)timeout)))
        {
            if (errno != ETIME)
            {
                m_accept_action_->SetErrno(eERR_POLL_FAIL);
                LOG_ERROR("frame %p, driver %p schedule failed, errno %d", 
                    frame, driver, errno);
                ret = -2;
            }
            else
            {
                ret = 1;
            }
        }
        // 事件处理返回值
        if (ret < 0)
        {
            LOG_ERROR("ev error, ret: %d", ret);
            continue;
        }

        struct sockaddr client_address;
        socklen_t address_len = sizeof(client_address);
        int fd = -1;
        if (transform == NULL)
        {
            transform = new NewThreadTransform();
            transform->SetTimeout(timeout);
        }

        while ((fd = mt_accept(m_listen_fd_, &client_address, &address_len)) > 0)
        {
            LOG_TRACE("m_listen_fd_: %d, accept fd: %d", m_listen_fd_, fd);
            IMtAction* action = m_construct_callback_ ? m_construct_callback_() : NULL;
            action->SetMsgDstAddr((struct sockaddr_in *)&client_address);
            action->SetMsgBufferSize(8192);
            action->SetTimeout(timeout);

            transform->Add(action, fd, &client_address);
            if (transform->Size() >= TRANSFORM_MAX_SIZE)
            {
                Frame::CreateThread(IMtActionServer::NewThread, transform);
                transform = new NewThreadTransform();
                transform->SetTimeout(timeout);
            }
        }
        if (transform->Size() > 0)
        {
            Frame::CreateThread(IMtActionServer::NewThread, transform);
            transform = new NewThreadTransform();
            transform->SetTimeout(timeout);
        }

        LOG_TRACE("mt_accept ...");
    } while (true);

    return 0;
}

void IMtActionServer::NewThread(void *args)
{
    NewThreadTransform* transform = (NewThreadTransform*)args;
    IMtConnection* connection = NULL;
    IMtAction* action = NULL;
    Frame* frame = GetInstance<Frame>();

    int ret = 0;
    IMtActionList action_list;

    for (int idx = 0; idx < transform->Size(); idx++)
    {
        action = transform->m_action_arr_[idx];
        if (!action || action->InitConnection() < 0)
        {
            LOG_ERROR("invalid action(%p) or int failed, error", action);
            if (m_destruct_callback_) 
            {
                m_destruct_callback_(action);
            }
        }

        LOG_TRACE("action: %p", action);

        connection = action->GetIConnection();
        if (NULL == connection)
        {
            LOG_ERROR("connection is NULL");
            if (m_destruct_callback_) 
            {
                m_destruct_callback_(action);
            }
        }
        connection->CreateSocket(transform->m_fd_arr_[idx]);
        action_list.push_back(action);
    }

    int timeout = transform->GetTimeout();
    LOG_DEBUG("timeout: %d", timeout);

    ret = transform->GetServer()->Recvfrom(action_list, timeout);
    if (ret < 0)
    {
        LOG_ERROR("recv failed, ret: %d", ret);
        return;
    }

    LOG_DEBUG("Recvfrom ...");

    // 中间处理
    for (IMtActionList::iterator iter = action_list.begin(); 
        iter != action_list.end(); ++iter)
    {
        IMtAction* action = *iter;
        if (action->GetMsgFlag() != eACTION_FLAG_FIN)
        {
            action->DoError();
            LOG_DEBUG("send recv failed: %d", action->GetErrno());
            continue;
        }
        ret = action->DoEncode();
        if (ret < 0)
        {
            LOG_DEBUG("action process failed: %d", ret);
            continue;
        }
    }

    ret = transform->GetServer()->Sendto(action_list, timeout);
    if (ret < 0)
    {
        LOG_ERROR("send failed, ret: %d", ret);
        return;
    }

    LOG_DEBUG("Sendto ...");

    // 最后处理
    for (IMtActionList::iterator iter = action_list.begin(); 
        iter != action_list.end(); ++iter)
    {
        IMtAction* action = *iter;
        if (action->GetMsgFlag() != eACTION_FLAG_SEND)
        {
            action->DoError();
            LOG_DEBUG("send recv failed: %d", action->GetErrno());
        }
        
        if (m_destruct_callback_) 
        {
            m_destruct_callback_(action);
        }
    }
}