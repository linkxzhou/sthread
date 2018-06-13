#include "mt_event_proxyer.h"
#include "mt_session.h"

MTHREAD_NAMESPACE_USING
using namespace std;

// 等待输入数据
int Eventer::InputNotify()
{
    LOG_TRACE("InputNotify ..., thread : %p", m_thread_);
    if (NULL == m_thread_)
    {
        LOG_ERROR("Eventer fdref no found m_thread_ ptr");
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

int Eventer::AddRef(void* args)
{
    FdRef* fd_ref = (FdRef*)args;
    if (NULL == m_proxyer_ || NULL == fd_ref)
    {
        LOG_ERROR("Eventer fdref no m_proxyer_ ptr or fd_ref is NULL");
        return -1;
    }
    int osfd = GetOsfd();
    int new_events = GetEvents();

    LOG_TRACE("m_proxyer_ : %p, fd_ref : %p, osfd : %d", m_proxyer_, fd_ref, osfd);

    Eventer* old_ev = fd_ref->GetEventer();
    LOG_TRACE("AddRef old_ev : %p", old_ev);
    if ((old_ev != NULL) && (old_ev != this))
    {
        LOG_ERROR("fdref conflict, fd: %d, old: %p, now: %p", osfd, old_ev, this);
        return -3;
    }
    if (!(m_proxyer_->AddFd(osfd, new_events)))
    {
        LOG_ERROR("fdref add failed");
        return -4;
    }
    fd_ref->SetEventer(this);
    LOG_TRACE("fdref add success, fd_ref : %p, ev : %p", fd_ref, fd_ref->GetEventer()); 

    return 0;
}

int Eventer::DelRef(void* args)
{
    FdRef* fd_ref = (FdRef*)args;
    if (NULL == m_proxyer_ || NULL == fd_ref)
    {
        LOG_ERROR("Eventer fdref no m_proxyer_ ptr or fd_ref is NULL");
        return -1;
    }
    int osfd = GetOsfd();
    int events = GetEvents();

    Eventer* old_ev = fd_ref->GetEventer();
    LOG_TRACE("DelRef old_ev : %p", old_ev);
    if ((old_ev != NULL) && (old_ev != this))
    {
        LOG_ERROR("fdref conflict, fd: %d, old: %p, now: %p", osfd, old_ev, this);
        return -3;
    }
    if (!(m_proxyer_->DelFd(osfd, events)))
    {
        LOG_ERROR("fdref ref del failed");
        return -4;
    }
    fd_ref->SetEventer(NULL);
    LOG_TRACE("fdref del success, fd_ref : %p, ev : %p", fd_ref, fd_ref->GetEventer()); 

    return 0;
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

    m_fdrefs_ = new FdRef[m_maxfd_];
    if (NULL == m_fdrefs_)
    {
        rc = -3;
        goto EXIT_LABEL;
    }

    LOG_TRACE("m_fdrefs_ : %p", m_fdrefs_);

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
    safe_delete_arr(m_fdrefs_);
    m_state_->ApiFree(); // 释放链接
}

bool EventProxyer::AddList(EventerList &list)
{
    bool ret = true;
    Eventer *ev = NULL, *ev_error = NULL;

    CPP_TAILQ_FOREACH(ev, &list, m_entry_)
    {
        if (!AddNode(ev))
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
            DelNode(ev);
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
        if (!DelNode(ev))  // failed also need continue, be sure ref count ok
        {
            LOG_ERROR("ev del failed, fd: %d", ev->GetOsfd());
            ret = false;
        }
    }

    return ret;
}

bool EventProxyer::AddNode(Eventer *node)
{
    if (NULL == node)
    {
        LOG_ERROR("node input invalid, %p", node);
        return false;
    }

    FdRef* item = GetFdRef(node->GetOsfd());
    if (NULL == item)
    {
        LOG_ERROR("fdref not find failed, fd: %d", node->GetOsfd());
        return false;
    }
    LOG_TRACE("item : %p", item);
    int ret = node->AddRef(item);
    if (ret < 0) 
    {
        LOG_ERROR("AddRef error, ret : %d", ret);
        return false;
    }

    return true;
}

bool EventProxyer::DelNode(Eventer* node)
{
    if (NULL == node)
    {
        LOG_ERROR("node input invalid, %p", node);
        return false;
    }

    FdRef* item = GetFdRef(node->GetOsfd());
    if (NULL == item)
    {
        LOG_ERROR("fdref not find, failed, fd: %d", node->GetOsfd());
        return false;
    }
    int ret = node->DelRef(item);
    if (ret < 0) 
    {
        LOG_ERROR("DelRef error, ret : %d", ret);
        return false;
    }

    return true;
}

bool EventProxyer::AddFd(int fd, int events)
{
    FdRef* item = GetFdRef(fd);
    LOG_TRACE("item : %p, fd : %d, events : %d", item, fd, events);
    if (NULL == item)
    {
        LOG_ERROR("fdref not find, failed, fd: %d", fd);
        return false;
    }

    item->AttachEvents(events); // 将事件附属到FdRef

    int old_events = item->GetListenEvents();
    int new_events = old_events | events;
    if (old_events == new_events)
    {
        return true;
    }

    LOG_TRACE("fd : %d, new_events : %d", fd, new_events);
    int rc = m_state_->ApiAddEvent(fd, new_events);
    if (rc < 0)
    {
        LOG_ERROR("add event failed, fd: %d", fd);
        item->DetachEvents(events);
        return false;
    }
    item->SetListenEvents(new_events);

    return true;
}

bool EventProxyer::DelFd(int fd, int events)
{
    return DelRef(fd, events, false);
}

bool EventProxyer::DelRef(int fd, int events, bool use_ref)
{
    FdRef* item = GetFdRef(fd);
    if (NULL == item)
    {
        LOG_ERROR("fdref not find, fd: %d", fd);
        return false;
    }

    item->DetachEvents(events);
    int old_events = item->GetListenEvents();
    int new_events = old_events & ~events;

    if (use_ref)
    {
        new_events = old_events;
        if (0 == item->ReadRefCnt())
        {
            new_events = new_events & ~MT_READABLE;
        }
        if (0 == item->WriteRefCnt())
        {
            new_events = new_events & ~MT_WRITABLE;
        }
    }

    if (old_events == new_events)
    {
        return true;
    }

    int rc = m_state_->ApiAddEvent(fd, new_events);
    if (rc < 0)
    {
        LOG_ERROR("del event failed, fd: %d", fd);
        return false;
    }
    item->SetListenEvents(new_events);

    return true;
}

void EventProxyer::DisposeEventerList(int ev_fdnum)
{
    int ret = 0, osfd = 0, revents = 0;
    FdRef* item = NULL;
    Eventer* ev = NULL;

    for (int i = 0; i < ev_fdnum; i++)
    {
        osfd = m_state_->m_fired_[i].fd;
        item = GetFdRef(osfd);
        LOG_TRACE("osfd : %d, item : %p", osfd, item);
        if (NULL == item)
        {
            LOG_ERROR("fdref not find, fd:%d, ev_fdnum:%d", osfd, ev_fdnum);
            continue;
        }

        revents = m_state_->m_fired_[i].mask; // 获取对应的event

        ev = item->GetEventer();
        if (NULL == ev)
        {
            LOG_TRACE("ev is NULL, fd : %d", osfd);
            DelFd(osfd, (revents & (MT_READABLE | MT_WRITABLE)));
            continue;
        }
        ev->SetRecvEvents(revents);

        if (revents & MT_EVERR)
        {
            LOG_TRACE("MT_EVERR osfd:%d ev_fdnum:%d", osfd, ev_fdnum);
            ev->HangupNotify();
            continue;
        }

        if (revents & MT_READABLE)
        {
            ret = ev->InputNotify();
            LOG_TRACE("MT_READABLE osfd:%d, ret:%d ev_fdnum:%d", osfd, ret, ev_fdnum);
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
        LOG_WARN("recv_num : %d", recv_num);
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
        GetInstance<ISessionEventerCtrl>()->FreeEventer(m_ev_);
    }
    m_ev_ = NULL;
}