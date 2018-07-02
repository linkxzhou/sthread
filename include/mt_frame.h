/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _MT_FRAME_H_INCLUDED_
#define _MT_FRAME_H_INCLUDED_

#include "mt_utils.h"
#include "mt_heap_timer.h"
#include "mt_event_proxyer.h"
#include "mt_sys_hook.h"
#include "mt_session.h"
#include "mt_thread.h"

MTHREAD_NAMESPACE_BEGIN

// 主框架（单列模式）
class Frame
{
    friend class Scheduler;

public:
    Frame() : m_daemon_(NULL), m_primo_(NULL), m_cur_thread_(NULL),
        m_thead_pool_(NULL), m_ev_proxyer_(NULL), m_timer_(NULL),
        m_last_clock_(0), m_wait_num_(0), m_timeout_(0), m_exitflag_(false)
    { 
        m_thead_pool_ = new ThreadPool();
        m_ev_proxyer_ = new EventProxyer();
    }

    // 清理数据
    ~Frame()
    {
    	Destroy();
    }

    void Destroy()
    {
    	safe_delete(m_primo_);
	    safe_delete(m_daemon_);

	    CPP_TAILQ_INIT(&m_io_list_);
	    // 清理sleep线程
	    Thread* thread = dynamic_cast<Thread*>(m_sleep_list_.HeapPop());
	    while (thread)
	    {
	        FreeThread(thread);
	        thread = dynamic_cast<Thread*>(m_sleep_list_.HeapPop());
	    }

	    while (!m_run_list_.empty())
	    {
	        thread = m_run_list_.front();
	        m_run_list_.pop();
	        FreeThread(thread);
	    }

	    Thread* tmp;
	    CPP_TAILQ_FOREACH_SAFE(thread, &m_pend_list_, m_entry_, tmp)
	    {
	        CPP_TAILQ_REMOVE(&m_pend_list_, thread, m_entry_);
	        FreeThread(thread);
	    }
	    safe_delete(m_thead_pool_);
	    safe_delete(m_timer_);
        safe_delete(m_ev_proxyer_);
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
    	// 初始化EventerProxyer和线程池
	    if (m_ev_proxyer_->Init(max_thread_num) < 0)
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

	    m_timer_ = new TimerCtrl(max_thread_num * 2);
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
	    m_daemon_->SetRunFunction(Frame::DaemonRun, this);

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

    inline const char* Version(void)
    {
    	return "1.0.0";
    }

    inline ThreadBase* GetActiveThread(void)
    {
        return (ThreadBase *)m_cur_thread_;
    }
    
    inline int ThreadSchedule(void)
    {
    	Thread* thread = NULL;
        Thread* active_thead = m_cur_thread_;

	    if (m_run_list_.empty())
	    {
            if (CPP_TAILQ_SIZE(&m_io_list_) <= 0 && 
                CPP_TAILQ_SIZE(&m_pend_list_) <= 0 && m_exitflag_)
            {
                return -1;
            }
	        thread = (Thread *)DaemonThread();
            LOG_TRACE("run DaemonThread, thread : %p, m_io_list_ size : %d, m_pend_list_ size : %d", 
                thread, CPP_TAILQ_SIZE(&m_io_list_), CPP_TAILQ_SIZE(&m_pend_list_));
	    }
	    else
	    {
	        thread = m_run_list_.front();
	        RemoveRunable(thread);
            LOG_TRACE("run front thread, m_run_list_ size: %d", m_run_list_.size());
	    }

	    LOG_TRACE("SetActiveThread thread : %p", thread);
	    SetActiveThread(thread);
	    thread->SetState(eRUNNING);
	    thread->RestoreContext((ThreadBase *)active_thead);

        return 0;
    }

    inline void RemoveIoWait(ThreadBase* thread)
    {
    	thread->UnsetFlag(eIO_LIST);
        LOG_TRACE("[remove]thread : %p, m_io_list_ size : %d", thread, CPP_TAILQ_SIZE(&m_io_list_));
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

    inline void InsertPend(ThreadBase* thread)
    {
    	thread->SetFlag(ePEND_LIST);
	    CPP_TAILQ_INSERT_TAIL(&m_pend_list_, (Thread*)thread, m_entry_);
	    thread->SetState(ePENDING);
    }

    inline void RemovePend(ThreadBase* thread)
    {
    	thread->UnsetFlag(ePEND_LIST);
    	CPP_TAILQ_REMOVE(&m_pend_list_, (Thread*)thread, m_entry_);
    }

    inline void InsertSleep(ThreadBase* thread)
    {
    	thread->SetFlag(eSLEEP_LIST);
	    m_sleep_list_.HeapPush(thread);
	    thread->SetState(eSLEEPING);
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
	        LOG_ERROR("remove heap failed , rc : %d, size : %d", 
                rc, m_sleep_list_.HeapSize());
	    }
    }

    inline void InsertIoWait(ThreadBase* thread)
    {
    	thread->SetFlag(eIO_LIST);
        LOG_TRACE("[insert]thread : %p, m_io_list_ size : %d", 
            thread, CPP_TAILQ_SIZE(&m_io_list_));
	    CPP_TAILQ_INSERT_TAIL(&m_io_list_, ((Thread*)thread), m_entry_);
	    InsertSleep(thread);
    }

    inline void RemoveRunable(ThreadBase* thread)
    {
    	thread->UnsetFlag(eRUN_LIST);
	    m_run_list_.pop();
	    m_wait_num_--;
    }

    inline void SetLastClock(utime64_t clock)
    {
        m_last_clock_ = clock;
    }

    inline utime64_t GetLastClock()
    {
        return m_last_clock_;
    }

    inline utime64_t GetSystemMS()
    {
        return Utils::system_ms();
    }

    inline int RunWaitNum()
    {
        return m_wait_num_;
    }

    inline void SetExitFlag(bool _exit)
    {
        m_exitflag_ = _exit;
    }

    inline TimerCtrl* GetTimerCtrl()
    {
        return m_timer_;
    }

    inline EventProxyer* GetEventProxyer()
    {
        return m_ev_proxyer_;
    }

    inline ThreadPool* GetThreadPool()
    {
        return m_thead_pool_;
    }

    inline ThreadBase* DaemonThread(void)
    {
        return m_daemon_;
    }

    inline ThreadBase* PrimoThread(void)
    {
        return m_primo_;
    }

    inline void CheckExpired()
    {
    	if (NULL != m_timer_)
	    {
	        LOG_TRACE("m_timer_ : %p", m_timer_);
	        m_timer_->CheckExpired();
	    }
    }

    void WakeupTimeout()
    {
    	utime64_t now = GetLastClock();
	    Thread* thread = dynamic_cast<Thread*>(m_sleep_list_.HeapTop());
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
	        LOG_TRACE("thread = %p", thread);
	        InsertRunable(thread);
	        thread = dynamic_cast<Thread*>(m_sleep_list_.HeapTop());
	    }
    }

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

    inline void SetTimeout(int timeout_ms)
    {
        if (NULL != m_ev_proxyer_)
        {
            m_ev_proxyer_->SetTimeout(timeout_ms);
        }
    }

    virtual int GetTimeout()
    {
        utime64_t now = GetLastClock();
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

public:
    // 对应系统函数
    static int sendto(int fd, const void *msg, int len, int flags, 
        const struct sockaddr *to, int tolen, int timeout)
    {
    	Frame* frame = GetInstance<Frame>();
	    utime64_t start = frame->GetLastClock();
	    Thread* thread = (Thread *)(frame->GetActiveThread());
        EventProxyer *proxyer = frame->GetEventProxyer();
	    utime64_t now = 0;

	    int n = 0;
	    while ((n = mt_sendto(fd, msg, len, flags, to, tolen)) < 0)
	    {
	        now = frame->GetLastClock();
	        if ((int)(now - start) > timeout)
	        {
	            errno = ETIME;
	            return -1;
	        }

	        if (errno == EINTR) {
	            continue;
	        }

	        if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
	        {
	            LOG_ERROR("sendto failed, errno: %d, strerr : %s", 
                    errno, strerror(errno));
	            return -2;
	        }

	        Eventer* ev = proxyer->GetEventer(fd);
            if (NULL == ev) 
            {
                ev = GetInstance<ISessionEventerCtrl>()->GetEventer(eEVENT_THREAD);
                ev->SetOwnerProxyer(proxyer);
            }
	        ev->SetOsfd(fd);
	        ev->EnableOutput();
	        ev->SetOwnerThread(thread);
	        int wakeup_timeout = timeout + frame->GetLastClock();
	        if (!(proxyer->Schedule(thread, NULL, ev, wakeup_timeout)))
	        {
	        	GetInstance<ISessionEventerCtrl>()->FreeEventer(ev);
	            return -3;
	        }
	    }

	    return n;
    }
    static int recvfrom(int fd, void *buf, int len, int flags, 
        struct sockaddr *from, socklen_t *fromlen, int timeout)
    {
    	Frame* frame = GetInstance<Frame>();
	    utime64_t start = frame->GetLastClock();
	    Thread* thread = (Thread *)(frame->GetActiveThread());
        EventProxyer *proxyer = frame->GetEventProxyer();
	    utime64_t now = 0;

	    if (timeout <= -1)
	    {
	        timeout = 0x7fffffff;
	    }

	    while (true)
	    {
	        now = frame->GetLastClock();
	        if ((int)(now - start) > timeout)
	        {
	            errno = ETIME;
	            return -1;
	        }

	        Eventer* ev = proxyer->GetEventer(fd);
            if (NULL == ev) 
            {
                ev = GetInstance<ISessionEventerCtrl>()->GetEventer(eEVENT_THREAD);
                ev->SetOwnerProxyer(proxyer);
            }
	        ev->SetOsfd(fd);
	        ev->EnableInput();
	        ev->SetOwnerThread(thread);
	        int wakeup_timeout = timeout + frame->GetLastClock();
	        if (!(proxyer->Schedule(thread, NULL, ev, wakeup_timeout)))
	        {
	            LOG_ERROR("eventer schedule failed, errno: %d, strerr: %s", 
                    errno, strerror(errno));
	            GetInstance<ISessionEventerCtrl>()->FreeEventer(ev);
	            return -2;
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
	                return -3;
	            }
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
	    utime64_t start = frame->GetLastClock();
	    Thread* thread = (Thread *)(frame->GetActiveThread());
        EventProxyer *proxyer = frame->GetEventProxyer();
	    utime64_t now = 0;

	    LOG_TRACE("fd: %d, timeout: %d, thread: %p", fd, timeout, thread);
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
	            return -2;
	        }

	        Eventer* ev = proxyer->GetEventer(fd);
            if (NULL == ev) 
            {
                ev = GetInstance<ISessionEventerCtrl>()->GetEventer(eEVENT_THREAD);
                ev->SetOwnerProxyer(proxyer);
            }
	        ev->SetOsfd(fd);
	        ev->EnableOutput();
	        ev->SetOwnerThread(thread);
            int wakeup_timeout = timeout + frame->GetLastClock();
	        if (!(proxyer->Schedule(thread, NULL, ev, wakeup_timeout)))
	        {
	        	LOG_ERROR("schedule error");
	        	GetInstance<ISessionEventerCtrl>()->FreeEventer(ev);
	            return -3;
	        }
	    }

	    return n;
    }

    static ssize_t read(int fd, void *buf, size_t nbyte, int timeout)
    {
    	Frame* frame = GetInstance<Frame>();
	    utime64_t start = frame->GetLastClock();
	    Thread* thread = (Thread*)(frame->GetActiveThread());
        EventProxyer *proxyer = frame->GetEventProxyer();
	    utime64_t now = 0;

	    ssize_t n = 0;
	    while ((n = mt_read(fd, buf, nbyte)) < 0)
	    {
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
	            return -2;
	        }

	        Eventer* ev = proxyer->GetEventer(fd);
            if (NULL == ev) 
            {
                ev = GetInstance<ISessionEventerCtrl>()->GetEventer(eEVENT_THREAD);
                ev->SetOwnerProxyer(proxyer);
            }
	        ev->SetOsfd(fd);
	        ev->EnableInput();
	        ev->SetOwnerThread(thread);
	        int wakeup_timeout = timeout + frame->GetLastClock();
	        if (!(proxyer->Schedule(thread, NULL, ev, wakeup_timeout)))
	        {
	        	GetInstance<ISessionEventerCtrl>()->FreeEventer(ev);
	            return -3;
	        }
	    }

	    return n;
    }

    static ssize_t write(int fd, const void *buf, size_t nbyte, int timeout)
    {
    	Frame* frame = GetInstance<Frame>();
	    utime64_t start = frame->GetLastClock();
	    Thread* thread = (Thread*)(frame->GetActiveThread());
        EventProxyer *proxyer = frame->GetEventProxyer();
	    utime64_t now = 0;

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
	                return -2;
	            }
	        }
	        else
	        {
	            send_len += n;
	            if (send_len >= nbyte)
	            {
	                return nbyte;
	            }
	        }

	        Eventer* ev = proxyer->GetEventer(fd);
            if (NULL == ev) 
            {
                ev = GetInstance<ISessionEventerCtrl>()->GetEventer(eEVENT_THREAD);
                ev->SetOwnerProxyer(proxyer);
            }
	        ev->SetOsfd(fd);
	        ev->EnableOutput();
	        ev->SetOwnerThread(thread);
	        int wakeup_timeout = timeout + frame->GetLastClock();
	        if (!(proxyer->Schedule(thread, NULL, ev, wakeup_timeout)))
	        {
	        	GetInstance<ISessionEventerCtrl>()->FreeEventer(ev);
	            return -3;
	        }
	    }

	    return nbyte;
    }

    static int recv(int fd, void *buf, int len, int flags, int timeout)
    {
    	Frame* frame = GetInstance<Frame>();
	    utime64_t start = frame->GetLastClock();
	    Thread* thread = (Thread *)(frame->GetActiveThread());
        EventProxyer *proxyer = frame->GetEventProxyer();
	    utime64_t now = 0;

	    LOG_TRACE("thread : %p", thread);

	    LOG_TRACE("timeout %d, now time: %ld, start time: %ld", timeout, now, start);
	    if (timeout <= -1)
	    {
	        timeout = 0x7fffffff;
	    }

	    while (true)
	    {
	        now = frame->GetLastClock();
	        LOG_TRACE("now time: %ld, start time: %ld", now, start);
	        if ((int)(now - start) > timeout)
	        {
	            errno = ETIME;
	            return -1;
	        }

	        Eventer* ev = proxyer->GetEventer(fd);
            if (NULL == ev) 
            {
                ev = GetInstance<ISessionEventerCtrl>()->GetEventer(eEVENT_THREAD);
                ev->SetOwnerProxyer(proxyer);
            }
	        ev->SetOsfd(fd);
	        ev->EnableInput();
	        ev->SetOwnerThread(thread);
	        int wakeup_timeout = timeout + frame->GetLastClock();
	        if (!(proxyer->Schedule(thread, NULL, ev, wakeup_timeout)))
	        {
	        	GetInstance<ISessionEventerCtrl>()->FreeEventer(ev);
	            return -2;
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
	                return -3;
	            }
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
	    utime64_t start = frame->GetLastClock();
	    Thread* thread = (Thread*)(frame->GetActiveThread());
        EventProxyer *proxyer = frame->GetEventProxyer();
	    utime64_t now = 0;

	    LOG_TRACE("thread : %p", thread);

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
	                return -2;
	            }
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

	        Eventer* ev = proxyer->GetEventer(fd);
            if (NULL == ev) 
            {
                ev = GetInstance<ISessionEventerCtrl>()->GetEventer(eEVENT_THREAD);
                ev->SetOwnerProxyer(proxyer);
            }
	        ev->SetOsfd(fd);
	        ev->EnableOutput();
	        ev->SetOwnerThread(thread);
	        int wakeup_timeout = timeout + frame->GetLastClock();
	        if (!(proxyer->Schedule(thread, NULL, ev, wakeup_timeout)))
	        {
	        	GetInstance<ISessionEventerCtrl>()->FreeEventer(ev);
	            return -3;
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
	    utime64_t start = frame->GetLastClock();
	    Thread* thread = (Thread*)(frame->GetActiveThread());
        EventProxyer *proxyer = frame->GetEventProxyer();
	    utime64_t now = 0;

	    if (timeout <= -1)
	    {
	        timeout = 0x7fffffff;
	    }

	    while (true)
	    {
	        now = frame->GetLastClock();
	        if ((int)(now - start) > timeout)
	        {
	            errno = ETIME;
	            return -1;
	        }

	        Eventer* ev = proxyer->GetEventer(fd);
            if (NULL == ev) 
            {
                ev = GetInstance<ISessionEventerCtrl>()->GetEventer(eEVENT_THREAD);
                ev->SetOwnerProxyer(proxyer);
            }
	        ev->SetOsfd(fd);
	        if (events & MT_READABLE)
	        {
	            ev->EnableInput();
	        }
	        if (events & MT_WRITABLE)
	        {
	            ev->EnableOutput();
	        }
	        ev->SetOwnerThread(thread);
	        int wakeup_timeout = timeout + frame->GetLastClock();
	        if (!(proxyer->Schedule(thread, NULL, ev, wakeup_timeout)))
	        {
	            LOG_TRACE("schedule failed, errno: %d", errno);
	            GetInstance<ISessionEventerCtrl>()->FreeEventer(ev);
	            return -2;
	        }

	        if (ev->GetRecvEvents() > 0)
	        {
	            return 0;
	        }
	    }
    }
    static ThreadBase* CreateThread(ThreadRunFunction entry, void *args, bool runable = true)
    {
    	Frame* frame = GetInstance<Frame>();
    	Thread* thread = frame->GetThreadPool()->AllocThread();

	    if (NULL == thread)
	    {
	        LOG_ERROR("create thread failed");
	        return NULL;
	    }
	    thread->SetRunFunction(entry, args);
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
        EventProxyer *proxyer = frame->GetEventProxyer();
        if (NULL == frame|| NULL == daemon || NULL == proxyer)
        {
            LOG_ERROR("frame NULL or daemon NULL or proxyer NULL");
            return ;
        }
    	LOG_TRACE("daemon : %p", daemon);
	    while (true)
	    {
            daemon->SwitchContext();
            proxyer->Dispatch();
	        frame->SetLastClock(frame->GetSystemMS());
	        LOG_TRACE("system ms :%ld", frame->GetSystemMS());
            // 将超时的转移状态
	        frame->WakeupTimeout();
            // 检查过期的timer
	        frame->CheckExpired();
	    }
    }
    // 表示可以中断运行，非Daemon方式
    static void PrimoRun()
    {
        Frame* frame = GetInstance<Frame>();
    	Thread* primo = (Thread *)(frame->PrimoThread());
        Thread* daemon = (Thread *)(frame->DaemonThread());
        frame->SetExitFlag(true);
        frame->SetActiveThread(daemon);
	    daemon->SetState(eRUNNING);
	    daemon->RestoreContext((ThreadBase *)primo);
    }

public:
    ThreadList          m_run_list_;
    TailQThreadQueue    m_io_list_, m_pend_list_;
    HeapList<ThreadBase>    m_sleep_list_;
    Thread      	*m_daemon_, *m_primo_, *m_cur_thread_;
    ThreadPool      *m_thead_pool_;
    EventProxyer    *m_ev_proxyer_;
    utime64_t       m_last_clock_;
    int             m_wait_num_;
    TimerCtrl*      m_timer_;
    int             m_timeout_;
    bool            m_exitflag_;
};

MTHREAD_NAMESPACE_END

#endif