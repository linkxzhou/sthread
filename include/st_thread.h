/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_THREAD_H_INCLUDED_
#define _ST_THREAD_H_INCLUDED_

#include "st_util.h"
#include "st_heap_timer.h"
#include "st_item.h"

ST_NAMESPACE_BEGIN

#define MEM_PAGE_SIZE       2048

class ThreadScheduler
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

    inline ThreadItem* DaemonThread(void)
    {
        return m_daemon_;
    }

    inline ThreadItem* PrimoThread(void)
    {
        return m_primo_;
    }

    inline void SetDaemonThread(ThreadItem *thread)
    {
        m_daemon_ = thread;
    }

    inline void SetPrimoThread(ThreadItem *thread)
    {
        m_primo_ = thread;
    }

    inline void ResetHeapSize(int32_t max_num)
    {
        m_sleep_list_.HeapResize(max_num);
    }

    inline ThreadItem* GetActiveThread()
    {
        return m_active_thread_;
    }

    inline void SetActiveThread(ThreadItem *thread)
    {
        m_active_thread_ = thread;
    }

    inline ThreadItem* GetActiveThread(ThreadItem *thread)
    {
        return m_active_thread_;
    }

    void SwitchThread(ThreadItem *rthread, ThreadItem *sthread);

    // 让出当前线程
    int Yield(ThreadItem *thread);

    void Sleep(ThreadItem *thread);

    void Pend(ThreadItem *thread);

    void Unpend(ThreadItem *thread);

    int IOWaitToRunable(ThreadItem *thread);

    int RemoveIOWait(ThreadItem *thread);

    int InsertIOWait(ThreadItem *thread);

    int InsertRunable(ThreadItem *thread);

    int RemoveRunable(ThreadItem *thread);

    void RemoveSleep(ThreadItem *thread);

    void InsertSleep(ThreadItem *thread);

    void WakeupParent(ThreadItem *thread); // 唤醒父亲线程

    void Wakeup(int64_t now); // 唤醒sleep的列表中的线程

    ThreadItem* PopRunable(); // pop最新一个运行的线程

public:
    ThreadItemQueue         m_run_list_, m_io_list_, m_pend_list_;
    HeapList<ThreadItem>    m_sleep_list_;
    ThreadItem              *m_active_thread_, *m_daemon_, *m_primo_;
};

class EventScheduler
{
    typedef StEventItem* StEventItemPtr;

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

    bool Schedule(ThreadItem* thread, 
        StEventItemQueue* fdset,
        StEventItem* item,
        uint64_t wakeup_timeout);

    void Close();

    bool Add(StEventItemQueue &fdset);

    bool Add(StEventItem *item);

    bool Delete(StEventItemQueue &fdset);

    bool Delete(StEventItem *item);

    bool AddFd(int32_t fd, int32_t new_events);

    bool DeleteFd(int32_t fd, int32_t new_events);

    inline bool IsValidFd(int32_t fd)
    {
        return ((fd >= m_maxfd_) || (fd < 0)) ? false : true;
    }

    inline bool SetEventItem(int32_t fd, StEventItem *item)
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

    inline void Reset(StEventItem *item)
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
    StEventItemPtr      *m_container_;
    int32_t             m_timeout_;
    ThreadScheduler     *m_thread_scheduler_;
};

class Thread : public ThreadItem
{
public:
    Thread();

    virtual ~Thread();

    virtual void Run(void);

    virtual void Sleep(int32_t ms)
    {
        uint64_t now = Util::SysMs();
        m_wakeup_time_ = now + ms;
        LOG_TRACE("now: %ld, m_wakeup_time_: %ld", now, m_wakeup_time_);
    }

    virtual void RestoreContext(ThreadItem* switch_thread);

    void WakeupParent();

protected:

    bool InitStack();

    void FreeStack();

    void InitContext();
};

ST_NAMESPACE_END

#endif