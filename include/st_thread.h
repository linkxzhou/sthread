/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_THREAD_H__
#define _ST_THREAD_H__

#include "st_util.h"
#include "st_heap_timer.h"
#include "st_base.h"

ST_NAMESPACE_BEGIN

#define MEM_PAGE_SIZE      2048

class StThreadScheduler : public referenceable
{
public:
    StThreadScheduler() : 
        m_active_thread_(NULL),
        m_daemon_(NULL),
        m_primo_(NULL)
    { 
        CPP_TAILQ_INIT(&m_run_list_);
        CPP_TAILQ_INIT(&m_io_list_);
        CPP_TAILQ_INIT(&m_pend_list_);
        CPP_TAILQ_INIT(&m_reclaim_list_);
    }

    ~StThreadScheduler()
    {
        m_active_thread_ = NULL;
        m_daemon_ = NULL;
        m_primo_ = NULL;
    }

    inline StThreadSuper* DaemonThread(void)
    {
        return m_daemon_;
    }

    inline StThreadSuper* PrimoThread(void)
    {
        return m_primo_;
    }

    inline void SetDaemonThread(StThreadSuper *thread)
    {
        m_daemon_ = thread;
    }

    inline void SetPrimoThread(StThreadSuper *thread)
    {
        m_primo_ = thread;
    }

    inline void ResetHeapSize(int32_t max_num)
    {
        m_sleep_list_.HeapResize(max_num);
    }

    inline StThreadSuper* GetActiveThread()
    {
        return m_active_thread_;
    }

    inline void SetActiveThread(StThreadSuper *thread)
    {
        m_active_thread_ = thread;
    }

    void SwitchThread(StThreadSuper *rthread, 
        StThreadSuper *sthread);

    // 让出当前线程
    uint32_t Yield(StThreadSuper *thread);

    uint32_t Sleep(StThreadSuper *thread);

    uint32_t Pend(StThreadSuper *thread);

    uint32_t Unpend(StThreadSuper *thread);

    uint32_t IOWaitToRunable(StThreadSuper *thread);

    uint32_t InsertIOWait(StThreadSuper *thread);

    uint32_t RemoveIOWait(StThreadSuper *thread);

    uint32_t InsertRunable(StThreadSuper *thread);

    uint32_t RemoveRunable(StThreadSuper *thread);

    uint32_t InsertSleep(StThreadSuper *thread);

    uint32_t RemoveSleep(StThreadSuper *thread);

    uint32_t ReclaimThread(StThreadSuper *thread);

    // 唤醒父亲线程
    void WakeupParent(StThreadSuper *thread);

    // 唤醒sleep的列表中的线程
    void Wakeup(int64_t now);

    // pop最新一个运行的线程
    StThreadSuper* PopRunable(); 

    StThread* CreateThread(StClosure *StClosure, 
        bool runable = true);

    // 创建线程
    Thread* AllocThread();

    void ForeachPrint()
    {
        LOG_TRACE("m_run_list_ size: %d, m_io_list_ size: %d,"
            " m_pend_list_ size: %d, m_reclaim_list_: %d", 
            CPP_TAILQ_SIZE(&m_run_list_), 
            CPP_TAILQ_SIZE(&m_io_list_), 
            CPP_TAILQ_SIZE(&m_pend_list_),
            CPP_TAILQ_SIZE(&m_reclaim_list_));
    }

public:
    StThreadSuperQueue         m_run_list_, m_io_list_, m_pend_list_, m_reclaim_list_;
    StHeapList<StThreadSuper>  m_sleep_list_;
    StThreadSuper              *m_active_thread_, *m_daemon_, *m_primo_;
};

class StEventScheduler : public referenceable
{
public:
    typedef StEventSuper* StEventSuperPtr;

    StEventScheduler(int32_t max_num = 1024) : 
        m_maxfd_(65535), 
        m_state_(new StApiState()), 
        m_container_(NULL), 
        m_timeout_(30000) // 默认超时30s
    { 
        int32_t r = Init(max_num);
        ASSERT(r >= 0);
    }

    ~StEventScheduler()
    { 
        this->Reset();

        st_safe_delete(m_state_);
        st_safe_delete_array(m_container_);
    }

    int32_t Init(int32_t max_num);

    bool Schedule(StThreadSuper *thread, 
        StEventSuperQueue *fdset,
        StEventSuper *item,
        uint64_t wakeup_timeout);

    bool Add(StEventSuperQueue &fdset);

    bool Add(StEventSuper *item);

    bool Delete(StEventSuperQueue &fdset);

    bool Delete(StEventSuper *item);

    bool AddFd(int32_t fd, int32_t new_events);

    bool DeleteFd(int32_t fd, int32_t new_events);

    inline bool IsValidFd(int32_t fd)
    {
        return ((fd >= m_maxfd_) || (fd < 0)) ? false : true;
    }

    inline bool SetEventItem(int32_t fd, StEventSuper *item)
    {
        if (unlikely(IsValidFd(fd)))
        {
            m_container_[fd] = item;
            return true;
        }

        return false;
    }

    inline StEventSuper* GetEventItem(int32_t fd)
    {
        if (unlikely(IsValidFd(fd)))
        {
            return m_container_[fd];
        }

        return NULL;
    }

    void Dispatch(int32_t fdnum);

    void Wait(int32_t timeout);

    inline void Reset()
    {
        st_safe_delete_array(m_container_);
        m_state_->ApiFree(); // 释放链接
    }

    inline void Reset(StEventSuper *item)
    {
        this->Delete(item);
        
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
    StEventSuperPtr     *m_container_;
    int32_t             m_timeout_;
    ThreadScheduler     *m_thread_scheduler_;
};

class StThread : public StThreadSuper
{
public:
    StThread() : 
        StThreadSuper()
    {
        bool r = InitStack();
        ASSERT(r == true);
        InitContext();
    }

    virtual ~StThread ()
    {
        FreeStack();
    }

    void InitContext();

    bool InitStack();

    void FreeStack();

    virtual void Run(void);

    // 设置休眠时间
    virtual void Sleep(int32_t ms)
    {
        uint64_t now = Util::SysMs();
        m_wakeup_time_ = now + ms;
        LOG_TRACE("now: %ld, m_wakeup_time_: %ld", now, m_wakeup_time_);
    }

    virtual void RestoreContext(StThreadSuper* switch_thread);

    void WakeupParent();

protected:

    bool InitStack();

    void FreeStack();

    void InitContext();
};

ST_NAMESPACE_END

#endif // _ST_THREAD_H__