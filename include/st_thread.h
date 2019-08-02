/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_THREAD_H_INCLUDED_
#define _ST_THREAD_H_INCLUDED_

#include "st_util.h"
#include "st_heap_timer.h"
#include "st_item.h"

ST_NAMESPACE_BEGIN

#define MEM_PAGE_SIZE       4096

class EventScheduler
{
    typedef StEventItem* StEventItemPtr;

public:
    EventScheduler() : 
        m_maxfd_(1000000), 
        m_state_(new StApiState()), 
        m_container_(NULL), 
        m_timeout_(30000) // 默认超时30s
    { }

    virtual ~EventScheduler()
    { 
        Close();
        
        st_safe_delete(m_state_);
        st_safe_delete_array(m_container_);
    }

    int32_t Create(int32_t max_num);

    bool Schedule(ThreadItem* thread, 
        StEventItemQueue* fdset,
        StEventItem* item,
        uint64_t wakeup_timeout);

    void Close();

    bool Add(StEventItemQueue& fdset);

    bool Add(StEventItem* ev);

    bool Delete(StEventItemQueue& fdset);

    bool Delete(StEventItem* ev);

    bool AddFd(int32_t fd, int32_t new_events);

    bool DeleteFd(int32_t fd, int32_t new_events);

    inline bool IsValidFd(int32_t fd)
    {
        return ((fd >= m_maxfd_) || (fd < 0)) ? false : true;
    }

    inline bool SetEventItem(int32_t fd, StEventItem* item)
    {
        if (unlikely(IsValidFd(fd)))
        {
            m_container_[fd] = item;
            return true;
        }

        return false;
    }

    inline StEventItem* GetEventItem(int32_t fd)
    {
        if (unlikely(IsValidFd(fd)))
        {
            return m_container_[fd];
        }

        return NULL;
    }

    void Dispatch(int32_t fdnum);

    void Wait(int32_t timeout);

protected:
    int32_t             m_maxfd_;
    StApiState*         m_state_;
    StEventItemPtr*     m_container_;
    int32_t             m_timeout_;
};

class ThreadScheduler
{
public:
    ThreadScheduler(int32_t max_num, ThreadItem *daemon, ThreadItem *primo):
        m_daemon_(daemon), 
        m_primo_(primo)
    { 
        m_sleep_list_.HeapResize(max_num);
        CPP_TAILQ_INIT(&m_run_list_);
        CPP_TAILQ_INIT(&m_io_list_);
        CPP_TAILQ_INIT(&m_pend_list_);
    }

    inline ThreadItem* DaemonThread(void)
    {
        return m_daemon_;
    }

    inline ThreadItem* PrimoThread(void)
    {
        return m_primo_;
    }

    inline ThreadItem* GetActiveThread(void)
    {
        return m_active_thread_;
    }

    inline void SetActiveThread(ThreadItem *thread)
    {
        m_active_thread_ = thread;
    }

    void SwitchThread(ThreadItem *rthread, ThreadItem *sthread);

    int Schedule(ThreadItem *item);

    void Sleep(ThreadItem *item);

    void Pend(ThreadItem *item);

    void Unpend(ThreadItem *item);

    int IOWaitToRunable(ThreadItem* thread);

    int RemoveIOWait(ThreadItem* thread);

    int InsertIOWait(ThreadItem* thread);

    int InsertRunable(ThreadItem* thread);

    ThreadItem* RemoveRunable();

    void RemoveSleep(ThreadItem* thread);

    void InsertSleep(ThreadItem* thread);

protected:
    ThreadItemQueue         m_run_list_, m_io_list_, m_pend_list_;
    HeapList<ThreadItem>    m_sleep_list_;
    ThreadItem              *m_active_thread_, *m_daemon_, *m_primo_;
};

class Thread : public ThreadItem
{
public:
    ~Thread();

    bool Create(EventScheduler *s1, ThreadScheduler *s2);

    virtual void Run(void);

    virtual void Sleep(int32_t ms)
    {
        uint64_t now = Util::SysMs();
        m_wakeup_time_ = now + ms;
        LOG_TRACE("now: %ld, m_wakeup_time_: %ld", now, m_wakeup_time_);
        m_thread_scheduler_->Sleep(this);
    }

    virtual void Wait()
    {
        m_thread_scheduler_->Pend(this); // 线程组塞
    }

    virtual int SwitchContext()
    {
        return m_thread_scheduler_->Schedule(this); // 主动线程调度
    }

    virtual void RestoreContext(ThreadItem* switch_thread);

    virtual int IOWaitToRunable()
    {
        return m_thread_scheduler_->IOWaitToRunable(this);
    }

    virtual void RemoveIOWait()
    {
        m_thread_scheduler_->RemoveIOWait(this);
    }

    virtual void InsertIOWait()
    {
        m_thread_scheduler_->InsertIOWait(this);
    }

    virtual void InsertRunable()
    {
        m_thread_scheduler_->InsertRunable(this);
    }

    void WakeupParent();

    inline EventScheduler* GetEventScheduler()
    {
        return m_event_scheduler_;
    }

    inline ThreadScheduler* GetThreadScheduler()
    {
        return m_thread_scheduler_;
    }

protected:

    bool InitStack();

    void FreeStack();

    void InitContext();

private:
    EventScheduler  *m_event_scheduler_;
    ThreadScheduler *m_thread_scheduler_;
};

ST_NAMESPACE_END

#endif