/*
 * Copyright (C) zhoulv2000@163.com
 */

#include "mt_core.h"
#include "mt_session.h"

MTHREAD_NAMESPACE_USING
using namespace std;

// 等待输入数据
int Eventer::InputNotify()
{
    LOG_TRACE("InputNotify ..., thread : %p", m_thread_);
    if (NULL == m_thread_)
    {
        LOG_ERROR("Eventer no found m_thread_ ptr");
        return -1;
    }
    if (m_thread_->HasFlag(eIO_LIST))
    {
        return m_thread_->IoWaitToRunable(); // 切换状态
    }

    return 0;
}

// 等待输出数据
int Eventer::OutputNotify()
{
    LOG_TRACE("OutputNotify ..., thread : %p", m_thread_);
    if (NULL == m_thread_)
    {
        LOG_ERROR("Eventer fdref no m_thread_ ptr, ev : %p", this);
        return -1;
    }
    if (m_thread_->HasFlag(eIO_LIST))
    {
        return m_thread_->IoWaitToRunable(); // 切换状态
    }

    return 0;
}

int Eventer::HangupNotify()
{
    LOG_TRACE("HangupNotify ..., thread : %p", m_thread_);
    if (NULL == m_proxyer_)
    {
        LOG_ERROR("Eventer fdref no m_proxyer_ ptr");
        return -1;
    }

    return m_proxyer_->DelFd(GetOsfd(), GetEvents());
}

int EventProxyer::Init(int max_num)
{
    m_maxfd_ = mt_max(max_num, m_maxfd_);;
    int rc = m_state_->ApiCreate(m_maxfd_);
    if (rc < 0)
    {
        rc = -2;
        goto EXIT_LABEL;
    }

    m_ev_arr_ = (EventerPtr *)malloc(sizeof(EventerPtr) * m_maxfd_);
    if (NULL == m_ev_arr_)
    {
        rc = -3;
        goto EXIT_LABEL;
    }
    // 初始化设置为空指针
    memset(m_ev_arr_, 0, sizeof(EventerPtr) * m_maxfd_);

    LOG_TRACE("m_ev_arr_ : %p, m_maxfd_ : %ld", m_ev_arr_, m_maxfd_);

    // 设置系统参数
    struct rlimit rlim;
    memset(&rlim, 0, sizeof(rlim));
    if (getrlimit(RLIMIT_NOFILE, &rlim) == 0)
    {
        if ((int)rlim.rlim_max < m_maxfd_)
        {
            rlim.rlim_cur = rlim.rlim_max;
            setrlimit(RLIMIT_NOFILE, &rlim);

            // 重新设置句柄大小
            rlim.rlim_cur = m_maxfd_;
            rlim.rlim_max = m_maxfd_;
            setrlimit(RLIMIT_NOFILE, &rlim);
        }
    }

EXIT_LABEL:
    if (rc < 0)
    {
        LOG_ERROR("rc : %d error", rc);
        Close();
    }

    return rc;
}

void EventProxyer::Close()
{
    safe_delete_arr(m_ev_arr_);
    m_state_->ApiFree(); // 释放链接
}

bool EventProxyer::AddList(EventerList &list)
{
    bool ret = true;
    // 保存最后一个出错的位置
    Eventer *ev = NULL, *ev_error = NULL; 

    CPP_TAILQ_FOREACH(ev, &list, m_entry_)
    {
        if (!AddEventer(ev))
        {
            LOG_ERROR("ev add failed, fd: %d", ev->GetOsfd());
            ev_error = ev;
            ret = false;

            goto EXIT_LABEL;
        }
        LOG_TRACE("ev add success ev: %p, fd: %d", ev, ev->GetOsfd());
    }

EXIT_LABEL:
    // 如果失败则回退
    if (!ret)
    {
        CPP_TAILQ_FOREACH(ev, &list, m_entry_)
        {
            if (ev == ev_error)
            {
                break;
            }
            DelEventer(ev);
        }
    }

    return ret;
}

bool EventProxyer::DelList(EventerList &list)
{
    bool ret = true;
    Eventer* ev = NULL;

    CPP_TAILQ_FOREACH(ev, &list, m_entry_)
    {
        if (!DelEventer(ev))
        {
            LOG_ERROR("ev del failed, fd: %d", ev->GetOsfd());
            ret = false;
        }
    }

    return ret;
}

bool EventProxyer::AddEventer(Eventer *ev)
{
    if (NULL == ev)
    {
        LOG_ERROR("ev input invalid, %p", ev);
        return false;
    }

    int osfd = ev->GetOsfd();
    int new_events = ev->GetEvents();
    Eventer* old_ev = IsValidFd(osfd) ? m_ev_arr_[osfd] : NULL;
    LOG_TRACE("AddEventer old_ev : %p", old_ev);
    if ((old_ev != NULL) && (old_ev != ev))
    {
        LOG_ERROR("eventer conflict, fd: %d, old: %p, now: %p", osfd, old_ev, ev);
        return false;
    }

    // 获取原来的events
    int old_events = (old_ev != NULL) ? old_ev->GetEvents() : 0;
    new_events = old_events | new_events;
    if (old_events == new_events)
    {
        // events一样不需要处理
        LOG_TRACE("old_events == new_events, %d", old_events);
        return true;
    }
    
    if (!AddFd(osfd, new_events))
    {
        LOG_ERROR("add fd : %d failed", osfd);
        return false;
    }

    // 为空的情况下赋值
    if (m_ev_arr_[osfd] == NULL)
    {
        m_ev_arr_[osfd] = ev;
    }
    m_ev_arr_[osfd]->SetEvents(new_events);

    return true;
}

bool EventProxyer::DelEventer(Eventer* ev)
{
    if (NULL == ev)
    {
        LOG_ERROR("ev input invalid, %p", ev);
        return false;
    }

    int osfd = ev->GetOsfd();
    int new_events = ev->GetEvents();
    Eventer* old_ev = IsValidFd(osfd) ? m_ev_arr_[osfd] : NULL;
    LOG_TRACE("DelEventer old_ev : %p", old_ev);
    if ((old_ev != NULL) && (old_ev != ev))
    {
        LOG_ERROR("eventer conflict, fd: %d, old: %p, now: %p", osfd, old_ev, ev);
        return false;
    }

    int old_events = 0;
    if (old_ev != NULL)
    {
        old_events = old_ev->GetEvents();
        new_events = old_events & ~new_events;
    }

    if (old_events == new_events)
    {
        // events一样不需要处理
        LOG_TRACE("old_events == new_events, %d", old_events);
        return true;
    }

    if (!DelFd(osfd, new_events))
    {
        LOG_ERROR("del fd : %d failed", osfd);
        return false;
    }

    // 为空的情况下赋值
    if (m_ev_arr_[osfd] == NULL)
    {
        m_ev_arr_[osfd] = ev;
    }
    m_ev_arr_[osfd]->SetEvents(new_events);

    return true;
}

bool EventProxyer::AddFd(int fd, int events)
{
    LOG_TRACE("fd : %d, events : %d", fd, events);
    if (!IsValidFd(fd))
    {
        LOG_ERROR("fd : %d not find", fd);
        return false;
    }

    int rc = m_state_->ApiAddEvent(fd, events);
    if (rc < 0)
    {
        LOG_ERROR("add event failed, fd : %d", fd);
        return false;
    }

    return true;
}

bool EventProxyer::DelFd(int fd, int events)
{
    return DelRef(fd, events, false);
}

bool EventProxyer::DelRef(int fd, int events, bool use_ref)
{
    LOG_TRACE("fd : %d, events : %d", fd, events);
    if (!IsValidFd(fd))
    {
        LOG_ERROR("fd: %d not find", fd);
        return false;
    }

    int rc = m_state_->ApiDelEvent(fd, events);
    if (rc < 0)
    {
        LOG_ERROR("del event failed, fd: %d", fd);
        return false;
    }

    if (use_ref)
    {
        // TODO : 需要对引用处理
    }

    return true;
}

void EventProxyer::DisposeEventerList(int ev_fdnum)
{
    int ret = 0, osfd = 0, revents = 0;
    Eventer* ev = NULL;

    for (int i = 0; i < ev_fdnum; i++)
    {
        osfd = m_state_->m_fired_[i].fd;
        if (!IsValidFd(osfd))
        {
            LOG_ERROR("fd not find, fd : %d, ev_fdnum : %d", osfd, ev_fdnum);
            continue;
        }

        ev = m_ev_arr_[osfd];
        if (NULL == ev)
        {
            LOG_TRACE("ev is NULL, fd : %d", osfd);
            DelFd(osfd, (revents & (MT_READABLE | MT_WRITABLE)));
            continue;
        }

        revents = m_state_->m_fired_[i].mask; // 获取对应的event
        ev->SetRecvEvents(revents);

        if (revents & MT_EVERR)
        {
            LOG_TRACE("MT_EVERR osfd : %d ev_fdnum : %d", osfd, ev_fdnum);
            ev->HangupNotify();
            continue;
        }

        if (revents & MT_READABLE)
        {
            ret = ev->InputNotify();
            LOG_TRACE("MT_READABLE osfd : %d, ret : %d ev_fdnum : %d", osfd, ret, ev_fdnum);
            if (ret != 0)
            {
                continue;
            }
        }

        if (revents & MT_WRITABLE)
        {
            ret = ev->OutputNotify();
            LOG_TRACE("MT_WRITABLE osfd:%d, ret:%d ev_fdnum:%d", osfd, ret, ev_fdnum);
            if (ret != 0)
            {
                continue;
            }
        }
    }
}

//  等待触发事件
void EventProxyer::Dispatch()
{
    int wait_time = GetTimeout(); // 获取需要等待时间
    LOG_TRACE("wait_time: %d ms", wait_time);
    int nfd = 0;
    if (wait_time <= 0)
    {
        nfd = m_state_->ApiPoll(NULL);
    }
    else
    {
        struct timeval tv = {0, wait_time * 1000};
        if (wait_time >= 1000)
        {
            tv.tv_sec = (int)(wait_time/1000);
            tv.tv_usec = (wait_time%1000) * 1000;
        }
        nfd = m_state_->ApiPoll(&tv);
    }

    LOG_TRACE("wait poll nfd: %d", nfd);
    if (nfd <= 0)
    {
        return ;
    }

    DisposeEventerList(nfd);
}

// 调度信息
bool EventProxyer::Schedule(ThreadBase* thread, EventerList* ev_list, 
        Eventer* ev, int wakeup_timeout)
{
    LOG_CHECK_FUNCTION
    
    // 当前的active thread调度
    if (NULL == thread)
    {
        LOG_ERROR("active thread NULL, eventer schedule failed");
        return false;
    }
    thread->ClearAllFd();
    if (ev_list)
    {
        thread->AddFdList(ev_list);
    }
    if (ev)
    {
        thread->AddFd(ev);
    }
    // 设置微线程的唤醒时间
    thread->SetWakeupTime(wakeup_timeout);
    if (!AddList(thread->GetFdSet()))
    {
        LOG_ERROR("list add failed, errno: %d", errno);
        return false;
    }
    thread->InsertIoWait(); // 线程切换为IO等待
    thread->SwitchContext(); // 切换上下文

    LOG_TRACE("thread : %p", thread);

    int recv_num = 0;
    EventerList& recv_fds = thread->GetFdSet();
    Eventer* _ev = NULL;
    CPP_TAILQ_FOREACH(_ev, &recv_fds, m_entry_)
    {
        LOG_TRACE("GetRecvEvents : %d, GetEvents : %d, fd : %d", 
            _ev->GetRecvEvents(), _ev->GetEvents(), _ev->GetOsfd());
        if (_ev->GetRecvEvents() != 0)
        {
        	recv_num++;
        }
    }
    this->DelList(recv_fds);
    if (recv_num == 0)
    {
        errno = ETIME;
        LOG_TRACE("[WARN]recv_num : %d", recv_num);
        return false;
    }
    else
    {
        LOG_TRACE("recv_num : %d", recv_num);
    }

    return true;
}

void IMtConnection::ResetEventer()
{
    LOG_TRACE("free eventer ...");
    // 释放event
    if (NULL != m_ev_)
    {
        GetInstance<ISessionEventerPool>()->FreeEventer(m_ev_);
    }
    m_ev_ = NULL;
}