/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _MT_FRAME_H_INCLUDED_
#define _MT_FRAME_H_INCLUDED_

#include "mt_utils.h"
#include "mt_heap_timer.h"
#include "mt_core.h"
#include "mt_sys_hook.h"
#include "mt_ext.h"
#include "mt_thread.h"

ST_NAMESPACE_BEGIN

// 主框架
class Frame
{
    friend class Scheduler;

public:
    Frame() : m_daemon_(NULL), m_primo_(NULL), m_cur_thread_(NULL),
        m_thead_pool_(new ThreadPool()), m_ev_driver_(new EventDriver()), 
        m_timer_(NULL), m_last_clock_(0), m_wait_num_(0), m_timeout_(0), 
        m_callback_(NULL), m_args_(NULL)
    { }

    // 清理数据
    ~Frame()
    {
    	Destroy();
    }

    void Destroy()
    {
    	safe_delete(m_primo_);
	    safe_delete(m_daemon_);

	    // 清理sleep的队列
	    Thread* thread = dynamic_cast<Thread*>(m_sleep_list_.HeapPop());
	    while (thread)
	    {
	        FreeThread(thread);
	        thread = dynamic_cast<Thread*>(m_sleep_list_.HeapPop());
	    }
        // 清除sleep后初始化io等待队列
        CPP_TAILQ_INIT(&m_io_list_);
        // 清理run的队列
	    while (!m_run_list_.empty())
	    {
	        thread = m_run_list_.front();
	        m_run_list_.pop();
	        FreeThread(thread);
	    }

	    Thread* t1;
	    CPP_TAILQ_FOREACH_SAFE(thread, &m_pend_list_, m_entry_, t1)
	    {
	        CPP_TAILQ_REMOVE(&m_pend_list_, thread, m_entry_);
	        FreeThread(thread);
	    }

	    safe_delete(m_thead_pool_);
	    safe_delete(m_timer_);
        safe_delete(m_ev_driver_);
    }

    ThreadBase* GetRootThread()
    {
    	if (NULL == m_cur_thread_)
	    {
	        return NULL;
	    }

	    eThreadType type = m_cur_thread_->GetType();
	    Thread* thread = m_cur_thread_;
	    Thread* parent = thread;

	    while (eSUB_THREAD == type)
	    {
	        thread = thread->GetParent();
	        if (NULL == thread)
	        {
	            break;
	        }

	        type   = thread->GetType();
	        parent = thread;
	    }

	    return parent;
    }

    int InitFrame(int max_thread_num = 50000)
    {
        int ret = 0;
    	// 初始化EventerDriver和线程池
	    if (m_ev_driver_->Init(max_thread_num) < 0)
	    {
	        LOG_ERROR("init event failed");
	        ret = -1;
            goto Label_Destroy;
	    }

        if (!m_thead_pool_->InitialPool(max_thread_num))
        {
            LOG_ERROR("thread pool failed");
	        ret = -10;
            goto Label_Destroy;
        }

	    if (m_sleep_list_.HeapResize(max_thread_num * 2) < 0)
	    {
	        LOG_ERROR("init heap list failed");
	        ret = -2;
            goto Label_Destroy;
	    }

	    m_timer_ = new HeapTimer(max_thread_num * 2);
	    if (NULL == m_timer_)
	    {
	        LOG_ERROR("init heap timer failed");
	        ret = -3;
            goto Label_Destroy;
	    }

	    // 获取一个daemon线程(从线程池中分配)
	    m_daemon_ = m_thead_pool_->AllocThread();
	    if (NULL == m_daemon_)
	    {
	        LOG_ERROR("alloc m_daemon_ thread failed");
	        ret = -4;
            goto Label_Destroy;
	    }
	    m_daemon_->SetType(eDAEMON);
	    m_daemon_->SetState(eRUNABLE);
	    m_daemon_->SetRunCallback(Frame::DaemonRun, this);

        // m_primo_ 为当前运行的线程
	    m_primo_ = m_thead_pool_->AllocThread();
	    if (NULL == m_primo_)
	    {
	        LOG_ERROR("alloc m_primo_ thread failed");
	        ret = -5;
            goto Label_Destroy;
	    }
        m_primo_->SetType(ePRIMORDIAL);
	    m_primo_->SetState(eRUNNING);
	    SetActiveThread(m_primo_); // 设置当前的活动线程

	    m_last_clock_ = GetSystemMS();
	    CPP_TAILQ_INIT(&m_io_list_);
	    CPP_TAILQ_INIT(&m_pend_list_);

Label_Destroy:
        if (ret != 0) 
        {
            Destroy();
        }

	    return ret;
    }

    inline void SetHookFlag()
    {
    	LOG_TRACE("set hook ...");
    	SET_HOOK_FLAG();
    }
    
    inline int ThreadSchedule(void)
    {
    	Thread* thread = NULL;
        Thread* active_thead = m_cur_thread_;

	    if (m_run_list_.empty())
	    {
	        thread = (Thread *)DaemonThread();
            LOG_TRACE("DaemonThread");
	    }
	    else
	    {
	        thread = RemoveRunable();
	    }

        LOG_TRACE("thread: %p, m_run_list_ size: %d, m_io_list_ size: %d, m_pend_list_ size: %d", 
                thread, m_run_list_.size(), CPP_TAILQ_SIZE(&m_io_list_), CPP_TAILQ_SIZE(&m_pend_list_));
	    LOG_TRACE("SetActiveThread thread: %p", thread);

	    SetActiveThread(thread);
	    thread->SetState(eRUNNING);
	    thread->RestoreContext((ThreadBase *)active_thead);

        return 0;
    }

    // 进入io等待同时插入sleep
    inline void InsertIoWait(ThreadBase* thread)
    {
    	thread->SetFlag(eIO_LIST);
        thread->SetState(eIOWAIT);
	    CPP_TAILQ_INSERT_TAIL(&m_io_list_, ((Thread*)thread), m_entry_);
	    InsertSleep(thread);
    }

    // 移除io等待同时移除sleep
    inline void RemoveIoWait(ThreadBase* thread)
    {
    	thread->UnsetFlag(eIO_LIST);
        LOG_TRACE("remove thread: %p, m_io_list_ size: %d", thread, CPP_TAILQ_SIZE(&m_io_list_));
	    CPP_TAILQ_REMOVE(&m_io_list_, (Thread*)thread, m_entry_);
	    RemoveSleep(thread);
    }

    inline void InsertRunable(ThreadBase* thread)
    {
    	thread->SetFlag(eRUN_LIST);
	    thread->SetState(eRUNABLE);
	    m_run_list_.push((Thread *)thread);
	    m_wait_num_++;
    }

    inline Thread* RemoveRunable()
    {
        Thread* thread = m_run_list_.front();
	    m_run_list_.pop();
	    m_wait_num_--;
        thread->UnsetFlag(eRUN_LIST);

        return thread;
    }

    inline void InsertPend(ThreadBase* thread)
    {
    	thread->SetFlag(ePEND_LIST);
        thread->SetState(ePENDING);
	    CPP_TAILQ_INSERT_TAIL(&m_pend_list_, (Thread*)thread, m_entry_);
    }

    inline void RemovePend(ThreadBase* thread)
    {
    	thread->UnsetFlag(ePEND_LIST);
    	CPP_TAILQ_REMOVE(&m_pend_list_, (Thread*)thread, m_entry_);
    }

    inline void InsertSleep(ThreadBase* thread)
    {
    	thread->SetFlag(eSLEEP_LIST);
        thread->SetState(eSLEEPING);
	    m_sleep_list_.HeapPush(thread);
    }

    inline void RemoveSleep(ThreadBase* thread)
    {
    	thread->UnsetFlag(eSLEEP_LIST);
        // 如果HeapSize < 0 则不需要处理
        if (m_sleep_list_.HeapSize() <= 0)
        {
            return ;
        }
	    int rc = m_sleep_list_.HeapDelete(thread);
	    if (rc < 0)
	    {
	        LOG_ERROR("remove heap failed, rc: %d, size: %d", 
                rc, m_sleep_list_.HeapSize());
	    }
    }

    inline void SetLastClock(time64_t clock)
    {
        m_last_clock_ = clock;
    }

    inline time64_t GetLastClock()
    {
        return m_last_clock_;
    }

    inline time64_t GetSystemMS()
    {
        return Util::system_ms();
    }

    inline int RunWaitNum()
    {
        return m_wait_num_;
    }

    inline HeapTimer* GetHeapTimer()
    {
        return m_timer_;
    }

    inline EventDriver* GetEventDriver()
    {
        return m_ev_driver_;
    }

    inline ThreadPool* GetThreadPool()
    {
        return m_thead_pool_;
    }

    inline void CheckExpired()
    {
    	if (NULL != m_timer_)
	    {
	        LOG_TRACE("m_timer_ : %p", m_timer_);
	        m_timer_->CheckExpired();
	    }
    }

    inline void SetCallback(FrameCallback callback)
    {
        m_callback_ = callback;
    }

    inline FrameCallback GetCallback()
    {
        return m_callback_;
    }

    inline void SetArgs(void *args)
    {
        m_args_ = args;
    }

    inline void* GetArgs()
    {
        return m_args_;
    }

    void WakeupTimeout()
    {
    	time64_t now = GetLastClock();
	    Thread* thread = dynamic_cast<Thread*>(m_sleep_list_.HeapTop());
        LOG_TRACE("thread GetWakeupTime: %ld", thread ? thread->GetWakeupTime() : 0);
	    while (thread && (thread->GetWakeupTime() <= now))
	    {
	        if (thread->HasFlag(eIO_LIST))
	        {
	            RemoveIoWait(thread);
	        }
	        else
	        {
	            RemoveSleep(thread);
	        }
            
	        InsertRunable(thread);
	        thread = dynamic_cast<Thread*>(m_sleep_list_.HeapTop());
	    }
    }

    // 设置当前的active线程
    inline void SetActiveThread(Thread* thread)
    {
        m_cur_thread_ = thread;
        // 设置当前的g_mt_threadid
        g_mt_threadid = m_cur_thread_->GetMtThreadid();
        LOG_TRACE("SetActiveThread : %p", m_cur_thread_);
    }

    inline void FreeThread(Thread* thread)
    {
        m_thead_pool_->FreeThread(thread);
    }

    // 设置事件驱动的时间
    inline void SetTimeout(int timeout_ms)
    {
        if (NULL != m_ev_driver_)
        {
            m_ev_driver_->SetTimeout(timeout_ms);
        }
    }

    virtual int GetTimeout()
    {
        time64_t now = GetLastClock();
	    Thread* thread = dynamic_cast<Thread*>(m_sleep_list_.HeapTop());
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
	        return (int)(thread->GetWakeupTime() - now);
	    }
    }

    inline void SetExit(bool exit)
    {
        m_exit_ = exit;
    }

    inline bool GetExit()
    {
        return m_exit_;
    }

public:
    // 对应系统函数
    static int sendto(int fd, const void *msg, int len, int flags, 
        const struct sockaddr *to, int tolen, int timeout)
    {
    	Frame* frame = GetInstance<Frame>();
	    time64_t start = frame->GetLastClock();
	    Thread* thread = (Thread *)(frame->GetActiveThread());
        EventDriver *driver = frame->GetEventDriver();
	    time64_t now = 0;

        timeout = (timeout <= -1) ? 0x7fffffff : timeout;

	    int n = 0;
	    while ((n = mt_sendto(fd, msg, len, flags, to, tolen)) < 0)
	    {
            if (n == 0) // 对端关闭
            {
                LOG_ERROR("[n=0]sendto failed, errno: %d, strerr : %s", 
                    errno, strerror(errno));
	            return 0;
            }

            // 判断是否超时
	        now = frame->GetLastClock();
	        if ((int)(now - start) > timeout)
	        {
	            errno = ETIME;
	            return -1;
	        }

	        if (errno == EINTR) 
            {
	            continue;
	        }

	        if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
	        {
	            LOG_ERROR("sendto failed, errno: %d, strerr : %s", 
                    errno, strerror(errno));
	            return -1;
	        }

	        Eventer* ev = driver->GetEventer(fd);
            if (NULL == ev) 
            {
                ev = GetInstance<EventerPool>()->GetEventer(eEVENT_THREAD);
                ev->SetOwnerDriver(driver);
            }
	        ev->SetOsfd(fd);
	        ev->EnableOutput();
	        ev->SetOwnerThread(thread);
	        time64_t wakeup_timeout = timeout + frame->GetLastClock();
	        if (!(driver->Schedule(thread, NULL, ev, wakeup_timeout)))
	        {
                LOG_ERROR("ev schedule failed, errno: %d, strerr: %s", 
                    errno, strerror(errno));
	        	GetInstance<EventerPool>()->FreeEventer(ev);
                errno = 0;
	            return -1;
	        }
	    }

	    return n;
    }

    static int recvfrom(int fd, void *buf, int len, int flags, 
        struct sockaddr *from, socklen_t *fromlen, int timeout)
    {
    	Frame* frame = GetInstance<Frame>();
	    time64_t start = frame->GetLastClock();
	    Thread* thread = (Thread *)(frame->GetActiveThread());
        EventDriver *driver = frame->GetEventDriver();
	    time64_t now = 0;

	    timeout = (timeout <= -1) ? 0x7fffffff : timeout;

	    while (true)
	    {
	        now = frame->GetLastClock();
	        if ((int)(now - start) > timeout)
	        {
	            errno = ETIME;
	            return -1;
	        }

	        Eventer* ev = driver->GetEventer(fd);
            if (NULL == ev) 
            {
                ev = GetInstance<EventerPool>()->GetEventer(eEVENT_THREAD);
                ev->SetOwnerDriver(driver);
            }
	        ev->SetOsfd(fd);
	        ev->EnableInput();
	        ev->SetOwnerThread(thread);
	        time64_t wakeup_timeout = timeout + frame->GetLastClock();
	        if (!(driver->Schedule(thread, NULL, ev, wakeup_timeout)))
	        {
	            LOG_ERROR("ev schedule failed, errno: %d, strerr: %s", 
                    errno, strerror(errno));
	            GetInstance<EventerPool>()->FreeEventer(ev);
                errno = 0;
	            return -1;
	        }

	        int n = mt_recvfrom(fd, buf, len, flags, from, fromlen);
            LOG_TRACE("recvfrom return n: %d, buf: %s, fd: %d, len: %d, flags: %d", 
	            n, buf, fd, len, flags);
	        if (n < 0)
	        {
	            if (errno == EINTR)
	            {
	                continue;
	            }
	            if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
	            {
	                LOG_ERROR("recvfrom failed, errno: %d", errno);
	                return -1;
	            }
	        }
            else if (n == 0) // 对端关闭
            {
                LOG_ERROR("[n=0]recvfrom failed, errno: %d", errno);
	            return 0;
            }
	        else
	        {
	            return n;
	        }
	    }
    }

    static int connect(int fd, const struct sockaddr *addr, int addrlen, int timeout)
    {
    	Frame* frame = GetInstance<Frame>();
	    time64_t start = frame->GetLastClock();
	    Thread* thread = (Thread *)(frame->GetActiveThread());
        EventDriver *driver = frame->GetEventDriver();
	    time64_t now = 0;

	    timeout = (timeout <= -1) ? 0x7fffffff : timeout;

	    int n = 0;
	    while ((n = mt_connect(fd, addr, addrlen)) < 0)
	    {
	    	LOG_TRACE("connect n : %d, errno : %d, strerror : %s", n, errno, strerror(errno));
	        now = frame->GetLastClock();
	        LOG_TRACE("now : %ld, start : %ld", now, start);
	        if ((int)(now - start) > timeout)
	        {
	            errno = ETIME;
	            return -1;
	        }

	        if (errno == EISCONN)
	        {
	        	LOG_WARN("errno = EISCONN");
	            return 0;
	        }

	        if (errno == EINTR)
	        {
	            continue;
	        }

	        if (!((errno == EAGAIN) || (errno == EINPROGRESS)))
	        {
	            LOG_ERROR("connect failed, errno: %d, strerr: %s", errno, strerror(errno));
	            return -1;
	        }

	        Eventer* ev = driver->GetEventer(fd);
            if (NULL == ev) 
            {
                ev = GetInstance<EventerPool>()->GetEventer(eEVENT_THREAD);
                ev->SetOwnerDriver(driver);
            }
	        ev->SetOsfd(fd);
	        ev->EnableOutput();
	        ev->SetOwnerThread(thread);
            time64_t wakeup_timeout = timeout + frame->GetLastClock();
	        if (!(driver->Schedule(thread, NULL, ev, wakeup_timeout)))
	        {
	        	LOG_ERROR("ev schedule failed, errno: %d, strerr: %s", 
                    errno, strerror(errno));
	        	GetInstance<EventerPool>()->FreeEventer(ev);
                errno = 0;
	            return -1;
	        }
	    }

	    return n;
    }

    static ssize_t read(int fd, void *buf, size_t nbyte, int timeout)
    {
    	Frame* frame = GetInstance<Frame>();
	    time64_t start = frame->GetLastClock();
	    Thread* thread = (Thread*)(frame->GetActiveThread());
        EventDriver *proxyer = frame->GetEventDriver();
	    time64_t now = 0;

        timeout = (timeout <= -1) ? 0x7fffffff : timeout;

	    ssize_t n = 0;
	    while ((n = mt_read(fd, buf, nbyte)) < 0)
	    {
            if (n == 0) // 句柄关闭
            {
                LOG_ERROR("[n=0]read failed, errno: %d", errno);
	            return 0;
            }

	        now = frame->GetLastClock();
	        if ((int)(now - start) > timeout)
	        {
	            errno = ETIME;
	            return -1;
	        }

	        if (errno == EINTR)
	        {
	            continue;
	        }

	        if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
	        {
	            LOG_ERROR("read failed, errno: %d", errno);
	            return -1;
	        }

	        Eventer* ev = proxyer->GetEventer(fd);
            if (NULL == ev) 
            {
                ev = GetInstance<EventerPool>()->GetEventer(eEVENT_THREAD);
                ev->SetOwnerDriver(proxyer);
            }
	        ev->SetOsfd(fd);
	        ev->EnableInput();
	        ev->SetOwnerThread(thread);
	        time64_t wakeup_timeout = timeout + frame->GetLastClock();
	        if (!(proxyer->Schedule(thread, NULL, ev, wakeup_timeout)))
	        {
                LOG_ERROR("ev schedule failed, errno: %d, strerr: %s", 
                    errno, strerror(errno));
	        	GetInstance<EventerPool>()->FreeEventer(ev);
                errno = 0;
	            return -1;
	        }
	    }

	    return n;
    }

    static ssize_t write(int fd, const void *buf, size_t nbyte, int timeout)
    {
    	Frame* frame = GetInstance<Frame>();
	    time64_t start = frame->GetLastClock();
	    Thread* thread = (Thread*)(frame->GetActiveThread());
        EventDriver *driver = frame->GetEventDriver();
	    time64_t now = 0;

        timeout = (timeout <= -1) ? 0x7fffffff : timeout;

	    ssize_t n = 0;
	    size_t send_len = 0;
	    while (send_len < nbyte)
	    {
	        now = frame->GetLastClock();
	        if ((int)(now - start) > timeout)
	        {
	            errno = ETIME;
	            return -1;
	        }

	        n = mt_write(fd, (char*)buf + send_len, nbyte - send_len);
	        if (n < 0)
	        {
	            if (errno == EINTR)
	            {
	                continue;
	            }

	            if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
	            {
	                LOG_ERROR("write failed, errno: %d", errno);
	                return -1;
	            }
	        }
            else if (n == 0) // 已经关闭句柄
            {
                LOG_ERROR("[n=0]write failed, errno: %d", errno);
	            return 0;
            }
	        else
	        {
	            send_len += n;
	            if (send_len >= nbyte)
	            {
	                return nbyte;
	            }
	        }

	        Eventer* ev = driver->GetEventer(fd);
            if (NULL == ev) 
            {
                ev = GetInstance<EventerPool>()->GetEventer(eEVENT_THREAD);
                ev->SetOwnerDriver(driver);
            }
	        ev->SetOsfd(fd);
	        ev->EnableOutput();
	        ev->SetOwnerThread(thread);
	        time64_t wakeup_timeout = timeout + frame->GetLastClock();
	        if (!(driver->Schedule(thread, NULL, ev, wakeup_timeout)))
	        {
                LOG_ERROR("ev schedule failed, errno: %d, strerr: %s", 
                    errno, strerror(errno));
	        	GetInstance<EventerPool>()->FreeEventer(ev);
                errno = 0;
	            return -1;
	        }
	    }

	    return nbyte;
    }

    static int recv(int fd, void *buf, int len, int flags, int timeout)
    {
    	Frame* frame = GetInstance<Frame>();
	    time64_t start = frame->GetLastClock();
	    Thread* thread = (Thread *)(frame->GetActiveThread());
        EventDriver *driver = frame->GetEventDriver();
	    time64_t now = 0;

	    timeout = (timeout <= -1) ? 0x7fffffff : timeout;

	    while (true)
	    {
	        now = frame->GetLastClock();
	        LOG_TRACE("now time: %ld, start time: %ld", now, start);
	        if ((int)(now - start) > timeout)
	        {
	            errno = ETIME;
	            return -1;
	        }

	        Eventer* ev = driver->GetEventer(fd);
            if (NULL == ev) 
            {
                ev = GetInstance<EventerPool>()->GetEventer(eEVENT_THREAD);
                ev->SetOwnerDriver(driver);
            }
	        ev->SetOsfd(fd);
	        ev->EnableInput();
	        ev->SetOwnerThread(thread);
	        time64_t wakeup_timeout = timeout + frame->GetLastClock();
	        if (!(driver->Schedule(thread, NULL, ev, wakeup_timeout)))
	        {
                LOG_ERROR("ev schedule failed, errno: %d, strerr: %s", 
                    errno, strerror(errno));
	        	GetInstance<EventerPool>()->FreeEventer(ev);
                errno = 0;
	            return -1;
	        }

	        int n = mt_recv(fd, buf, len, flags);
	        LOG_TRACE("recv return n: %d, buf: %s, fd: %d, len: %d, flags: %d", 
	            n, buf, fd, len, flags);
	        if (n < 0)
	        {
	            if (errno == EINTR)
	            {
	                continue;
	            }
	            if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
	            {
	                LOG_ERROR("recv failed, errno: %d, strerr: %s", errno, strerror(errno));
	                return -1;
	            }
	        }
            else if (n == 0) // 对端关闭连接
            {
                LOG_ERROR("[n=0]recv failed, errno: %d, strerr: %s", errno, strerror(errno));
                return 0;
            }
	        else
	        {
	            return n;
	        }
	    }
    }

    static ssize_t send(int fd, const void *buf, size_t nbyte, int flags, int timeout)
    {
    	Frame* frame = GetInstance<Frame>();
	    time64_t start = frame->GetLastClock();
	    Thread* thread = (Thread*)(frame->GetActiveThread());
        EventDriver *driver = frame->GetEventDriver();
	    time64_t now = 0;

	    timeout = (timeout <= -1) ? 0x7fffffff : timeout;

	    ssize_t n = 0;
	    size_t send_len = 0;
	    while (send_len < nbyte)
	    {
	        now = frame->GetLastClock();
	        if ((int)(now - start) > timeout)
	        {
	            errno = ETIME; // 超时请求
	            return -1;
	        }

	        n = mt_send(fd, (char*)buf + send_len, nbyte - send_len, flags);
	        LOG_TRACE("send fd: %d, nbyte: %d, send_len: %d, flags: %d, n: %d", 
	            fd, nbyte, send_len, flags, n);
	        if (n < 0)
	        {
	            if (errno == EINTR)
	            {
	                continue;
	            }

	            if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
	            {
	                LOG_ERROR("write failed, errno: %d, strerr: %s", errno, strerror(errno));
	                return -1;
	            }
	        }
            else if (n == 0) // 对端关闭连接
            {
                LOG_ERROR("[n=0]write failed, errno: %d, strerr: %s", errno, strerror(errno));
	            return 0;
            }
	        else
	        {
	            send_len += n;
	            if (send_len >= nbyte)
	            {
	            	LOG_TRACE("send_len : %d", send_len);
	                return nbyte;
	            }
	        }

	        Eventer* ev = driver->GetEventer(fd);
            if (NULL == ev) 
            {
                ev = GetInstance<EventerPool>()->GetEventer(eEVENT_THREAD);
                ev->SetOwnerDriver(driver);
            }
	        ev->SetOsfd(fd);
	        ev->EnableOutput();
	        ev->SetOwnerThread(thread);
	        time64_t wakeup_timeout = timeout + frame->GetLastClock();
	        if (!(driver->Schedule(thread, NULL, ev, wakeup_timeout)))
	        {
                LOG_ERROR("ev schedule failed, errno: %d, strerr: %s", 
                    errno, strerror(errno));
	        	GetInstance<EventerPool>()->FreeEventer(ev);
                errno = 0;
	            return -1;
	        }
	    }

	    return nbyte;
    }
    
    static void sleep(int ms)
    {
    	Frame* frame = GetInstance<Frame>();
	    Thread* thread = (Thread*)(frame->GetActiveThread());
	    if (thread != NULL)
	    {
	        thread->Sleep(ms);
	    }
    }

    static int WaitEvents(int fd, int events, int timeout)
    {
    	Frame* frame = GetInstance<Frame>();
	    time64_t start = frame->GetLastClock();
	    Thread* thread = (Thread*)(frame->GetActiveThread());
        EventDriver *driver = frame->GetEventDriver();
	    time64_t now = 0;

	    timeout = (timeout <= -1) ? 0x7fffffff : timeout;

	    while (true)
	    {
	        now = frame->GetLastClock();
	        if ((int)(now - start) > timeout)
	        {
	            errno = ETIME;
	            return -1;
	        }

	        Eventer* ev = driver->GetEventer(fd);
            if (NULL == ev) 
            {
                ev = GetInstance<EventerPool>()->GetEventer(eEVENT_THREAD);
                ev->SetOwnerDriver(driver);
            }
	        ev->SetOsfd(fd);
            ev->SetOwnerThread(thread);
	        if (events & ST_READABLE)
	        {
	            ev->EnableInput();
	        }
	        if (events & ST_WRITEABLE)
	        {
	            ev->EnableOutput();
	        }
	        time64_t wakeup_timeout = timeout + frame->GetLastClock();
	        if (!(driver->Schedule(thread, NULL, ev, wakeup_timeout)))
	        {
	            LOG_ERROR("ev schedule failed, errno: %d, strerr: %s", 
                    errno, strerror(errno));
	            GetInstance<EventerPool>()->FreeEventer(ev);
	            return -2;
	        }

	        if (ev->GetRecvEvents() > 0)
	        {
	            return 0;
	        }
	    }
    }

    static ThreadBase* CreateThread(ThreadRunCallback entry, void *args, bool runable = true)
    {
    	Frame* frame = GetInstance<Frame>();
    	Thread* thread = frame->GetThreadPool()->AllocThread();

	    if (NULL == thread)
	    {
	        LOG_ERROR("create thread failed");
	        return NULL;
	    }

	    thread->SetRunCallback(entry, args);
	    if (runable)
	    {
	        frame->InsertRunable(thread); // 插入运行线程
	    }

	    return thread;
    }

    static void DaemonRun(void *args)
    {
    	Frame* frame = GetInstance<Frame>();
    	Thread* daemon = (Thread *)(frame->DaemonThread());
        EventDriver *driver = frame->GetEventDriver();
        if (NULL == frame|| NULL == daemon || NULL == driver)
        {
            LOG_ERROR("frame NULL or daemon NULL or driver NULL");
            return ;
        }

    	LOG_TRACE("daemon: %p", daemon);
        
	    do
	    {
            if (frame->GetCallback() != NULL) {
                (frame->GetCallback())(frame->GetArgs());
            }

            driver->Dispatch();

            LOG_TRACE("system ms: %ld", frame->GetSystemMS());

	        frame->SetLastClock(frame->GetSystemMS());
            // 将超时的转移状态
	        frame->WakeupTimeout();
            // 检查过期的timer
	        frame->CheckExpired();
            daemon->SwitchContext();

            // 判断是否需要退出
            if (frame->GetExit())
            {
                LOG_TRACE("iosize: %d, pendsize: %d, runsize: %d, sleepsize: %d", 
                    CPP_TAILQ_SIZE(&(frame->m_io_list_)), CPP_TAILQ_SIZE(&(frame->m_pend_list_)),
                    frame->m_run_list_.size(), frame->m_sleep_list_.HeapSize());
                if (CPP_TAILQ_SIZE(&(frame->m_io_list_)) == 0 && 
                    CPP_TAILQ_SIZE(&(frame->m_pend_list_)) == 0 && 
                    frame->m_run_list_.size() == 0)
                {
                    // 退出循环
                    return ;
                }
            }
	    } while(true);
    }
    
    // 可以中断运行，非Daemon方式，exit = true 前端运行，false Daemon运行
    static void Loop(bool exit = false)
    {
        Frame* frame = GetInstance<Frame>();
        frame->SetExit(exit);
        frame->SetActiveThread((Thread *)frame->DaemonThread());
        DaemonRun(NULL);
    }

public:
    ThreadList              m_run_list_;
    TailQThreadQueue        m_io_list_, m_pend_list_;
    HeapList<ThreadBase>    m_sleep_list_;
    Thread          *m_daemon_, *m_primo_, *m_cur_thread_;
    ThreadPool      *m_thead_pool_;
    EventDriver     *m_ev_driver_;
    HeapTimer       *m_timer_;

    time64_t        m_last_clock_;
    int             m_wait_num_;
    int             m_timeout_;
    bool            m_exit_;

    FrameCallback   m_callback_;
    void            *m_args_;
};

ST_NAMESPACE_END

#endif

// // 用户态现场池
// unsigned int ThreadPool::s_default_thread_num_ = DEFAULT_THREAD_NUM;
// unsigned int ThreadPool::s_default_stack_size_ = DEFAULT_STACK_SIZE;

// // 创建一个线程池
// bool ThreadPool::InitialPool(int max_num)
// {
//     Thread *thread = NULL;
//     for (unsigned int i = 0; i < s_default_thread_num_; i++)
//     {
//         thread = new Thread();
//         if ((NULL == thread) || (false == thread->Initial()))
//         {
//             LOG_ERROR("init pool thread %p init failed", thread);
//             safe_delete(thread);
//             continue;
//         }
//         thread->SetFlag(eFREE_LIST);
//         m_free_list_.push(thread);
//     }

//     LOG_TRACE("max_num : %d, size : %d", max_num, m_free_list_.size());

//     m_total_num_ = m_free_list_.size();
//     m_max_num_ = max_num;
//     m_use_num_ = 0;
//     if (m_total_num_ <= 0)
//     {
//         return false;
//     }
//     else
//     {
//         return true;
//     }
// }

// void ThreadPool::DestroyPool()
// {
//     Thread* thread = NULL;
//     while (!m_free_list_.empty())
//     {
//         thread = m_free_list_.front();
//         m_free_list_.pop();
//         safe_delete(thread);
//     }
//     m_total_num_ = 0;
//     m_use_num_ = 0;
// }

// Thread* ThreadPool::AllocThread()
// {
//     Thread* thread = NULL;
//     if (!m_free_list_.empty())
//     {
//         thread = m_free_list_.front();
//         m_free_list_.pop();
//         thread->UnsetFlag(eFREE_LIST);
//         m_use_num_++;
//         return thread;
//     }

//     if (m_total_num_ > m_max_num_)
//     {
//         LOG_ERROR("total_num_: %d, max_num_: %d total_num_ > max_num_", 
//             m_total_num_, m_max_num_);
//         return NULL;
//     }

//     thread = new Thread();
//     if ((NULL == thread) || (false == thread->Initial()))
//     {
//         LOG_ERROR("thread alloc failed, thread: %p", thread);
//         safe_delete(thread);
//         return NULL;
//     }

//     m_total_num_++;
//     m_use_num_++;

//     return thread;
// }

// void ThreadPool::FreeThread(Thread* thread)
// {
//     thread->Reset();
//     m_use_num_--;
//     m_free_list_.push(thread);
//     thread->SetFlag(eFREE_LIST);
    
//     // 对于预分配大于的情况下需要删除
//     unsigned int free_num = m_free_list_.size();
//     if ((free_num > s_default_thread_num_) && (free_num > 1))
//     {
//         thread = m_free_list_.front();
//         m_free_list_.pop();
//         safe_delete(thread);
//         m_total_num_--;
//     }
// }