/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_MANAGER_H_INCLUDED_
#define _ST_MANAGER_H_INCLUDED_

#include "st_util.h"
#include "st_heap_timer.h"
#include "st_thread.h"
#include "st_sys.h"

ST_NAMESPACE_BEGIN

#define GetEventScheduler()     (GetInstance<EventScheduler>())
#define GetThreadScheduler()    (GetInstance<ThreadScheduler>())

class Manager
{
public:
    Manager() : 
        m_daemon_(NULL), 
        m_primo_(NULL), 
        m_heap_timer_(NULL),
        m_timeout_(1000)
    { 
        Init(); // 初始化
    }

    ~Manager()
    {
    	st_safe_delete(m_primo_);
	    st_safe_delete(m_daemon_);
	    st_safe_delete(m_heap_timer_);
    }

    void Init(int max_num = 1024)
    {
	    int r = GetThreadScheduler()->m_sleep_list_.
            HeapResize(max_num * 2);
	    ASSERT(r >= 0);

	    m_heap_timer_ = new HeapTimer(max_num * 2);
        ASSERT(m_heap_timer_ != NULL);
	    
	    // 获取一个daemon线程(从线程池中分配)
	    m_daemon_ = new Thread();
	    ASSERT(m_daemon_ != NULL);

	    m_daemon_->SetType(eDAEMON);
	    m_daemon_->SetState(eRUNABLE);
	    m_daemon_->SetCallback(NewClosure(StartDaemonThread, this));
        m_daemon_->SetName("daemon");

	    m_primo_ = new Thread();
	    ASSERT(m_daemon_ != NULL);

        m_primo_->SetType(ePRIMORDIAL);
	    m_primo_->SetState(eRUNNING);
        m_primo_->SetName("primo");

        GetThreadScheduler()->SetDaemonThread(m_daemon_);
        GetThreadScheduler()->SetPrimoThread(m_primo_);
        // 设置当前的活动线程
	    GetThreadScheduler()->SetActiveThread(m_primo_);

	    m_last_clock_ = Util::SysMs();

        LOG_TRACE("m_last_clock_: %d", m_last_clock_);
    }

    inline void SetHookFlag()
    {
    	SET_HOOK_FLAG();
    }

    inline void UpdateLastClock()
    {
        m_last_clock_ = Util::SysMs();
    }

    inline int64_t GetLastClock()
    {
        return m_last_clock_;
    }

    inline HeapTimer* GetHeapTimer()
    {
        return m_heap_timer_;
    }

    inline void CheckExpired()
    {
        int32_t count = 0;
    	if (NULL != m_heap_timer_)
	    {
	        count = m_heap_timer_->CheckExpired();
	    }

        LOG_TRACE("count: %d", count);
    }

    inline int64_t GetTimeout()
    {
	    Thread *thread = dynamic_cast<Thread*>(
            GetThreadScheduler()->m_sleep_list_.HeapTop()
        );

        int64_t now = GetLastClock();
	    if (!thread)
	    {
	        return m_timeout_;
	    }
	    else if (thread->GetWakeupTime() < now)
	    {
	        return 0;
	    }
	    else
	    {
	        return (int64_t)(thread->GetWakeupTime() - now);
	    }
    }

    int WaitEvents(int fd, int events, int timeout)
    {
	    int64_t start = GetLastClock();
	    Thread *thread = (Thread*)(GetThreadScheduler()->GetActiveThread());

	    int64_t now = 0;
	    timeout = (timeout <= -1) ? 0x7fffffff : timeout;

	    while (true)
	    {
	        now = GetLastClock();
	        if ((int)(now - start) > timeout)
	        {
                LOG_TRACE("timeout is over");
	            errno = ETIME;
	            return -1;
	        }

	        StEventBase *item = GetEventScheduler()->GetEventItem(fd);
            if (NULL == item)
            {
                LOG_TRACE("item is NULL");
                return -2;
            }

            item->SetOwnerThread(thread);

	        if (events & ST_READABLE)
	        {
	            item->EnableInput();
	        }

	        if (events & ST_WRITEABLE)
	        {
	            item->EnableOutput();
	        }

	        int64_t wakeup_timeout = timeout + GetLastClock();
            bool r = GetEventScheduler()->Schedule(thread, NULL, item, wakeup_timeout);
	        if (!r)
	        {
	            LOG_ERROR("item schedule failed, errno: %d, strerr: %s", 
                    errno, strerror(errno)); 
                // 释放item数据
	            UtilPtrPoolFree(item);
	            return -3;
	        }

	        if (item->GetRecvEvents() > 0)
	        {
	            return 0;
	        }
	    }

        // TODO : 返回数据有问题
    }

    Thread* CreateThread(Closure *closure, bool runable = true)
    {
    	Thread *thread = AllocThread();
	    if (NULL == thread)
	    {
	        LOG_ERROR("alloc thread failed");
	        return NULL;
	    }
        
	    thread->SetCallback(closure);
	    if (runable)
	    {
	        GetThreadScheduler()->InsertRunable(thread); // 插入运行线程
	    }

	    return thread;
    }

    inline Thread* AllocThread()
    {
        return (Thread*)(GetInstance< UtilPtrPool<Thread> >()->AllocPtr());
    }
    
    static void StartDaemonThread(Manager *manager)
    {
        ASSERT(manager != NULL);

    	Thread *daemon = manager->m_daemon_;
        EventScheduler *event_scheduler = GetEventScheduler();
        ThreadScheduler *thread_scheduler = GetThreadScheduler();
        if (NULL == daemon || 
            NULL == event_scheduler ||
            NULL == thread_scheduler)
        {
            LOG_ERROR("(daemon, event_scheduler, thread_scheduler) is NULL");
            return ;
        }

    	LOG_TRACE("daemon: %p", daemon);

	    do
	    {
            event_scheduler->Wait(manager->GetTimeout());
            manager->UpdateLastClock();
            int64_t now = manager->GetLastClock();
            LOG_TRACE("--------[name:%s]--------- : system ms: %ld", 
                GetThreadScheduler()->GetActiveThread()->GetName(), now);
            // 判断sleep的thread
	        thread_scheduler->Wakeup(now);
	        manager->CheckExpired();
            // 让出线程
            thread_scheduler->Yield(daemon);
	    } while(true);
    }

public:
    Thread      *m_daemon_, *m_primo_;
    HeapTimer   *m_heap_timer_;
    int64_t     m_last_clock_, m_timeout_;
};

ST_NAMESPACE_END

#ifdef  __cplusplus
extern "C" 
{
#endif

int _sendto(int fd, const void *msg, int len, int flags, 
    const struct sockaddr *to, int tolen, int timeout);

int _recvfrom(int fd, void *buf, int len, int flags, 
    struct sockaddr *from, socklen_t *fromlen, int timeout);

int _connect(int fd, const struct sockaddr *addr, int addrlen, int timeout);

ssize_t _read(int fd, void *buf, size_t nbyte, int timeout);

ssize_t _write(int fd, const void *buf, size_t nbyte, int timeout);

int _recv(int fd, void *buf, int len, int flags, int timeout);

ssize_t _send(int fd, const void *buf, size_t nbyte, int flags, int timeout);

void _sleep(int ms);

int _accept(int fd, struct sockaddr *addr, socklen_t *addrlen);

#ifdef  __cplusplus
}
#endif

#endif