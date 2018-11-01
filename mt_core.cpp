/*
 * Copyright (C) zhoulv2000@163.com
 */

#include "mt_core.h"
#include "mt_ext.h"

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
    if (NULL == m_driver_)
    {
        LOG_ERROR("Eventer fdref no m_driver_ ptr");
        return -1;
    }

    return m_driver_->DelFd(GetOsfd(), GetEvents());
}

// 重置proxy里面的fd
void Eventer::Reset()
{
    if (NULL != m_driver_)
    {
        m_driver_->SetEventer(m_fd_, NULL);
    }

    m_fd_      = -1;
    m_events_  = 0;
    m_revents_ = 0;
    m_type_    = 0;
    m_thread_  = NULL;
}

int EventDriver::Init(int max_num)
{
    m_maxfd_ = mt_max(max_num, m_maxfd_);;
    int rc = m_state_->ApiCreate(m_maxfd_);
    if (rc < 0)
    {
        rc = -2;
        goto EXIT_LABEL;
    }

    m_ev_array_ = (EventerPtr *)malloc(sizeof(EventerPtr) * m_maxfd_);
    if (NULL == m_ev_array_)
    {
        rc = -3;
        goto EXIT_LABEL;
    }
    // 初始化设置为空指针
    memset(m_ev_array_, 0, sizeof(EventerPtr) * m_maxfd_);

    // 设置系统参数
    struct rlimit rlim;
    memset(&rlim, 0, sizeof(rlim));
    if (getrlimit(RLIMIT_NOFILE, &rlim) == 0)
    {
        if ((int)rlim.rlim_max < m_maxfd_)
        {
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

void EventDriver::Close()
{
    safe_delete_arr(m_ev_array_);
    m_state_->ApiFree(); // 释放链接
}

bool EventDriver::AddList(EventerList &list)
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

bool EventDriver::DelList(EventerList &list)
{
    bool ret = true;
    // 保存最后一个出错的位置
    Eventer *ev = NULL, *ev_error = NULL; 

    CPP_TAILQ_FOREACH(ev, &list, m_entry_)
    {
        if (!DelEventer(ev))
        {
            LOG_ERROR("ev del failed, fd: %d", ev->GetOsfd());
            ev_error = ev;
            ret = false;

            goto EXIT_LABEL1;
        }
    }

EXIT_LABEL1:
    // 如果失败则回退
    if (!ret)
    {
        CPP_TAILQ_FOREACH(ev, &list, m_entry_)
        {
            if (ev == ev_error)
            {
                break;
            }
            AddEventer(ev);
        }
    }

    return ret;
}

bool EventDriver::AddEventer(Eventer *ev)
{
    if (NULL == ev)
    {
        LOG_ERROR("ev input invalid, %p", ev);
        return false;
    }

    int osfd = ev->GetOsfd();
    // 非法的fd直接返回
    if (unlikely(!IsValidFd(osfd)))
    {
        LOG_ERROR("IsValidFd osfd, %d", osfd);
        return false;
    }

    int new_events = ev->GetEvents();
    Eventer* old_ev = m_ev_array_[osfd];
    LOG_TRACE("AddEventer old_ev: %p, ev: %p", old_ev, ev);
    if (NULL == old_ev)
    {
        m_ev_array_[osfd] = ev;
        if (!AddFd(osfd, new_events))
        {
            LOG_ERROR("add fd: %d failed", osfd);
            return false;
        }
    }
    else
    {
        if (old_ev != ev)
        {
            LOG_ERROR("ev conflict, fd: %d, old: %p, now: %p", osfd, old_ev, ev);
            return false;
        }

        // 获取原来的events
        int old_events = old_ev->GetEvents();
        new_events = old_events | new_events;
        if (old_events == new_events)
        {
            return true;
        }
        if (!AddFd(osfd, new_events))
        {
            LOG_ERROR("add fd: %d failed", osfd);
            return false;
        }
        // 设置新的events
        old_ev->SetEvents(new_events);
    }

    return true;
}

bool EventDriver::DelEventer(Eventer* ev)
{
    if (NULL == ev)
    {
        LOG_ERROR("ev input invalid, %p", ev);
        return false;
    }

    int osfd = ev->GetOsfd();
    // 非法的fd直接返回
    if (unlikely(!IsValidFd(osfd)))
    {
        LOG_ERROR("IsValidFd osfd, %d", osfd);
        return false;
    }

    int new_events = ev->GetEvents();
    Eventer* old_ev = m_ev_array_[osfd];
    LOG_TRACE("DelEventer old_ev: %p, ev: %p", old_ev, ev);
    if (NULL == old_ev)
    {
        m_ev_array_[osfd] = ev;
        if (!DelFd(osfd, new_events))
        {
            LOG_ERROR("del fd: %d failed", osfd);
            return false;
        }
    }
    else
    {
        if (old_ev != ev)
        {
            LOG_ERROR("ev conflict, fd: %d, old: %p, now: %p", osfd, old_ev, ev);
            return false;
        }
        // TODO : 处理异常
        int old_events = old_ev->GetEvents();
        new_events = old_events & ~new_events;
        if (!DelFd(osfd, new_events))
        {
            LOG_ERROR("del fd: %d failed", osfd);
            return false;
        }
        // 去除删除的属性
        old_ev->SetEvents(new_events);
    }

    return true;
}

bool EventDriver::AddFd(int fd, int events)
{
    LOG_TRACE("fd : %d, events: %d", fd, events);
    if (unlikely(!IsValidFd(fd)))
    {
        LOG_ERROR("fd : %d not find", fd);
        return false;
    }

    int rc = m_state_->ApiAddEvent(fd, events);
    if (rc < 0)
    {
        LOG_ERROR("add event failed, fd: %d", fd);
        return false;
    }

    return true;
}

bool EventDriver::DelFd(int fd, int events)
{
    return DelRef(fd, events, false);
}

bool EventDriver::DelRef(int fd, int events, bool useref)
{
    LOG_TRACE("fd: %d, events: %d", fd, events);
    if (unlikely(!IsValidFd(fd)))
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

    if (useref)
    {
        // TODO : 需要对引用处理
        // pass
    }

    return true;
}

void EventDriver::DisposeEventerList(int ev_fdnum)
{
    int ret = 0, osfd = 0, revents = 0;
    Eventer* ev = NULL;

    for (int i = 0; i < ev_fdnum; i++)
    {
        osfd = m_state_->m_fired_[i].fd;
        if (unlikely(!IsValidFd(osfd)))
        {
            LOG_ERROR("fd not find, fd: %d, ev_fdnum: %d", osfd, ev_fdnum);
            continue;
        }

        revents = m_state_->m_fired_[i].mask; // 获取对应的event
        ev = m_ev_array_[osfd];
        if (NULL == ev)
        {
            LOG_TRACE("ev is NULL, fd: %d", osfd);
            DelFd(osfd, (revents & (MT_READABLE | MT_WRITEABLE | MT_EVERR)));
            continue;
        }
        ev->SetRecvEvents(revents); // 设置收到的事件
        if (revents & MT_EVERR)
        {
            LOG_TRACE("MT_EVERR osfd: %d ev_fdnum: %d", osfd, ev_fdnum);
            ev->HangupNotify();
            continue;
        }
        if (revents & MT_READABLE)
        {
            ret = ev->InputNotify();
            LOG_TRACE("MT_READABLE osfd: %d, ret: %d ev_fdnum: %d", osfd, ret, ev_fdnum);
            if (ret != 0)
            {
                LOG_ERROR("revents & MT_READABLE, InputNotify ret: %d", ret);
                continue;
            }
        }
        if (revents & MT_WRITEABLE)
        {
            ret = ev->OutputNotify();
            LOG_TRACE("MT_WRITEABLE osfd: %d, ret: %d ev_fdnum: %d", osfd, ret, ev_fdnum);
            if (ret != 0)
            {
                LOG_ERROR("revents & MT_WRITEABLE, OutputNotify ret: %d", ret);
                continue;
            }
        }
    }
}

//  等待触发事件
void EventDriver::Dispatch()
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
bool EventDriver::Schedule(ThreadBase* thread, EventerList* ev_list, 
        Eventer* ev, time64_t wakeup_timeout)
{
    // 当前的active thread调度
    if (NULL == thread)
    {
        LOG_ERROR("active thread NULL, eventer schedule failed");
        return false;
    }

    // 清空之前的句柄列表
    DelList(thread->GetFdSet());
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

    LOG_TRACE("thread: %p", thread);
    int recv_num = 0;
    EventerList& recv_fds = thread->GetFdSet();
    Eventer* _ev = NULL;
    CPP_TAILQ_FOREACH(_ev, &recv_fds, m_entry_)
    {
        LOG_TRACE("GetRecvEvents: %d, GetEvents: %d, fd: %d", 
            _ev->GetRecvEvents(), _ev->GetEvents(), _ev->GetOsfd());
        if (_ev->GetRecvEvents() != 0)
        {
        	recv_num++;
        }
    }
    DelList(recv_fds);
    // 如果没有收到任何recv事件则表示超时或者异常
    if (recv_num == 0)
    {
        errno = ETIME;
        LOG_ERROR("recv_num: 0");
        return false;
    }

    LOG_TRACE("recv_num: %d", recv_num);
    return true;
}

void IMtConnection::ResetEventer()
{
    // 释放event
    if (NULL != m_ev_)
    {
        GetInstance<EventerPool>()->FreeEventer(m_ev_);
    }
    m_ev_ = NULL;
}