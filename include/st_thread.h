/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_THREAD_H_INCLUDED_
#define _ST_THREAD_H_INCLUDED_

#include "st_util.h"
#include "st_heap_timer.h"
#include "st_base.h"

ST_NAMESPACE_BEGIN

#define MEM_PAGE_SIZE       2048

class ThreadScheduler : public referenceable
{
public:
    ThreadScheduler() : 
        m_active_thread_(NULL),
        m_daemon_(NULL),
        m_primo_(NULL)
    { 
        CPP_TAILQ_INIT(&m_run_list_);
        CPP_TAILQ_INIT(&m_io_list_);
        CPP_TAILQ_INIT(&m_pend_list_);
    }

    inline StThreadBase* DaemonThread(void)
    {
        return m_daemon_;
    }

    inline StThreadBase* PrimoThread(void)
    {
        return m_primo_;
    }

    inline void SetDaemonThread(StThreadBase *thread)
    {
        m_daemon_ = thread;
    }

    inline void SetPrimoThread(StThreadBase *thread)
    {
        m_primo_ = thread;
    }

    inline void ResetHeapSize(int32_t max_num)
    {
        m_sleep_list_.HeapResize(max_num);
    }

    inline StThreadBase* GetActiveThread()
    {
        return m_active_thread_;
    }

    inline void SetActiveThread(StThreadBase *thread)
    {
        m_active_thread_ = thread;
    }

    void SwitchThread(StThreadBase *rthread, StThreadBase *sthread);

    // 让出当前线程
    int Yield(StThreadBase *thread);

    int Sleep(StThreadBase *thread);

    int Pend(StThreadBase *thread);

    int Unpend(StThreadBase *thread);

    int IOWaitToRunable(StThreadBase *thread);

    int InsertIOWait(StThreadBase *thread);

    int RemoveIOWait(StThreadBase *thread);

    int InsertRunable(StThreadBase *thread);

    int RemoveRunable(StThreadBase *thread);

    int InsertSleep(StThreadBase *thread);

    int RemoveSleep(StThreadBase *thread);

    // 唤醒父亲线程
    void WakeupParent(StThreadBase *thread);

    // 唤醒sleep的列表中的线程
    void Wakeup(int64_t now);

    // pop最新一个运行的线程
    StThreadBase* PopRunable(); 

    void ForeachPrint()
    {
        LOG_TRACE("m_run_list_ size: %d, m_io_list_ size: %d, m_pend_list_ size: %d", 
            CPP_TAILQ_SIZE(&m_run_list_), 
            CPP_TAILQ_SIZE(&m_io_list_), 
            CPP_TAILQ_SIZE(&m_pend_list_));
    }

public:
    StThreadBaseQueue         m_run_list_, m_io_list_, m_pend_list_;
    HeapList<StThreadBase>    m_sleep_list_;
    StThreadBase              *m_active_thread_, *m_daemon_, *m_primo_;
};

class EventScheduler : public referenceable
{
    typedef StEventBase* StEventBasePtr;

public:
    EventScheduler(int32_t max_num = 1024) : 
        m_maxfd_(65535), 
        m_state_(new StApiState()), 
        m_container_(NULL), 
        m_timeout_(30000), // 默认超时30s
        m_thread_scheduler_(NULL)
    { 
        int32_t r = Init(max_num);
        ASSERT(r >= 0);
    }

    virtual ~EventScheduler()
    { 
        Close();

        st_safe_delete(m_state_);
        st_safe_delete_array(m_container_);
    }

    int32_t Init(int32_t max_num);

    bool Schedule(StThreadBase *thread, 
        StEventBaseQueue *fdset,
        StEventBase *item,
        uint64_t wakeup_timeout);

    bool Add(StEventBaseQueue &fdset);

    bool Add(StEventBase *item);

    bool Delete(StEventBaseQueue &fdset);

    bool Delete(StEventBase *item);

    bool AddFd(int32_t fd, int32_t new_events);

    bool DeleteFd(int32_t fd, int32_t new_events);

    inline bool IsValidFd(int32_t fd)
    {
        return ((fd >= m_maxfd_) || (fd < 0)) ? false : true;
    }

    inline bool SetEventItem(int32_t fd, StEventBase *item)
    {
        if (unlikely(IsValidFd(fd)))
        {
            m_container_[fd] = item;
            return true;
        }

        return false;
    }

    inline StEventBase* GetEventItem(int32_t fd)
    {
        if (unlikely(IsValidFd(fd)))
        {
            return m_container_[fd];
        }

        return NULL;
    }

    void Dispatch(int32_t fdnum);

    void Wait(int32_t timeout);

    inline void Close()
    {
        st_safe_delete_array(m_container_);
        m_state_->ApiFree(); // 释放链接
    }

    inline void Close(StEventBase *item)
    {
        Delete(item);
        
        int osfd = item->GetOsfd();
        if (unlikely(!IsValidFd(osfd)))
        {
            LOG_ERROR("IsValidFd osfd, %d", osfd);
            return ;
        }

        m_container_[osfd] = NULL;
    }

protected:
    int32_t             m_maxfd_;
    StApiState          *m_state_;
    StEventBasePtr      *m_container_;
    int32_t             m_timeout_;
    ThreadScheduler     *m_thread_scheduler_;
};

class Thread : public StThreadBase
{
public:
    Thread() : StThreadBase()
    {
        bool r = InitStack();
        ASSERT(r == true);
        InitContext();
    }

    virtual ~Thread()
    {
        FreeStack();
    }

    virtual void Run(void);

    // 设置休眠时间
    virtual void Sleep(int32_t ms)
    {
        uint64_t now = Util::SysMs();
        m_wakeup_time_ = now + ms;
        LOG_TRACE("now: %ld, m_wakeup_time_: %ld", now, m_wakeup_time_);
    }

    virtual void RestoreContext(StThreadBase* switch_thread);

    void WakeupParent();

protected:

    bool InitStack();

    void FreeStack();

    void InitContext();
};

ST_NAMESPACE_END

#endif