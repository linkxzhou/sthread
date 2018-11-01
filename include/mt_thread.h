/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _MT_THREAD_H_INCLUDED_
#define _MT_THREAD_H_INCLUDED_

#include "mt_utils.h"
#include "mt_heap_timer.h"
#include "mt_core.h"

MTHREAD_NAMESPACE_BEGIN

#define STACK_PAD_SIZE      128
#define MEM_PAGE_SIZE       4096
#define DEFAULT_STACK_SIZE  512 * 1024
#define DEFAULT_THREAD_NUM  5

class Thread;

typedef std::queue<Thread*>        ThreadList; // 队列信息
typedef CPP_TAILQ_ENTRY<Thread>    ThreadLink;
typedef CPP_TAILQ_HEAD<Thread>     SubThreadQueue;
typedef CPP_TAILQ_HEAD<Thread>     TailQThreadQueue;

// 调度系统
class Scheduler
{
public:
    static time64_t GetTime();
    static void SwitchDaemonThread(Thread *thread);
    static void SwitchPrimoThread(Thread *thread);
    static int ThreadSchedule(); // 主动调度线程

    static void Sleep(); // 休眠
    static void Pend(); // 阻塞
    static void Unpend(Thread* thread); // 非阻塞
    static void Reclaim(); // 回收

    static int IoWaitToRunable(Thread* thread); // 将状态从iowait转换到runable
    static int RemoveIoWait(Thread* thread);
    static int InsertIoWait(Thread* thread);
    static int InsertRunable(Thread* thread);

    static int RemoveEvents(int fd, int events); // 删除events
    static int InsertEvents(int fd, int events); // 添加events
};

class Thread : public ThreadBase
{
public:
    Thread() : m_parent_(NULL), m_stack_size_(STACK), m_reset_(false)
    {
        CPP_TAILQ_INIT(&m_fdset_);
        CPP_TAILQ_INIT(&m_sub_list_);
    }

    virtual ~Thread()
    { 
        FreeStack();
    }

    virtual void Run(void);

    inline bool Initial(void)
    {
        if (!InitStack())
        {
            LOG_ERROR("init stack failed");
            return false;
        }
        InitContext();
        
        return true;
    }

    // 传入值是否强制初始化
    inline void Reset(bool force_init = true)
    {
        if (force_init) 
        {
            m_wakeup_time_ = 0;
            InitContext(); // 初始化context
        }
        
        // 清理状态
        CPP_TAILQ_INIT(&m_fdset_);
        CPP_TAILQ_INIT(&m_sub_list_);
        m_flag_ = eNOT_INLIST;
        m_type_ = eNORMAL;
        m_state_ = eINITIAL;
        m_callback_ = NULL;
        m_args_ = NULL;
        m_parent_ = NULL;
        SetReset(true);
    }

    virtual void Sleep(int ms)
    {
        time64_t now = Scheduler::GetTime();
        m_wakeup_time_ = now + ms;
        LOG_TRACE("now: %ld, m_wakeup_time_: %ld", now, m_wakeup_time_);
        Scheduler::Sleep();
    }

    virtual void Wait()
    {
        Scheduler::Pend(); // 线程组塞
    }

    virtual int SwitchContext()
    {
        return Scheduler::ThreadSchedule(); // 主动线程调度
    }

    virtual void RestoreContext(ThreadBase* switch_thread);

    // 将状态从iowait转换到runable
    virtual int IoWaitToRunable() 
    {
        return Scheduler::IoWaitToRunable(this);
    }

    virtual void RemoveIoWait()
    { 
        Scheduler::RemoveIoWait(this);
    }

    virtual time64_t HeapValue()
    {
        return GetWakeupTime();
    }

    virtual void InsertIoWait()
    { 
        Scheduler::InsertIoWait(this);
    }

    virtual void InsertRunable()
    { 
        Scheduler::InsertRunable(this);
    }

    inline void SetRunCallback(ThreadRunCallback func, void* args)
    {
        m_callback_ = func;
        m_args_  = args;
    }

    inline void* GetThreadArgs()
    {
        return m_args_;
    }

    virtual void ClearAllFd(void)
    {
        CPP_TAILQ_INIT(&m_fdset_);
    }

    virtual void AddFd(Eventer* ev)
    {
        if (CPP_TAILQ_EMPTY(&m_fdset_))
        {
            CPP_TAILQ_INIT(&m_fdset_);
        }
        CPP_TAILQ_INSERT_TAIL(&m_fdset_, ev, m_entry_);
    }

    virtual void AddFdList(EventerList* fdset)
    {
        if (CPP_TAILQ_EMPTY(&m_fdset_))
        {
            CPP_TAILQ_INIT(&m_fdset_);
        }
        CPP_TAILQ_CONCAT(&m_fdset_, fdset, m_entry_);
    }
    
    inline EventerList& GetFdSet(void)
    {
        return m_fdset_;
    }

    inline Thread* GetParent()
    {
        return m_parent_;
    }

    inline bool IsDaemon(void)
    {
        return (eDAEMON == m_type_);
    }

    inline bool IsPrimo(void)
    {
        return (ePRIMORDIAL == m_type_);
    }

    inline bool IsSubThread(void)
    {
        return (eSUB_THREAD == m_type_);
    }

    inline bool IsReset()
    {
        return m_reset_;
    }

    inline void SetReset(bool reset)
    {
        m_reset_ = reset;
    }

    inline void SetParent(Thread* parent)
    {
        m_parent_ = parent;
    }

    inline void AddSubThread(Thread* sub)
    {
        if (!sub->HasFlag(eSUB_LIST))
        {
            CPP_TAILQ_INSERT_TAIL(&m_sub_list_, sub, m_sub_entry_);
            sub->m_parent_ = this;
        }
        sub->SetFlag(eSUB_LIST);
    }

    inline void RemoveSubThread(Thread* sub)
    {
        if (sub->HasFlag(eSUB_LIST))
        {
            CPP_TAILQ_REMOVE(&m_sub_list_, sub, m_sub_entry_);
            sub->m_parent_ = NULL;
        }
        sub->UnsetFlag(eSUB_LIST);
    }

    inline bool HasNoSubThread()
    {
        return CPP_TAILQ_EMPTY(&m_sub_list_);
    }

    void WakeupParent();

protected:

    bool InitStack();

    void FreeStack();

    void InitContext();

public:
    SubThreadQueue  m_sub_list_; // 子线程
    Thread*         m_parent_; // 父亲线程
    ThreadLink      m_entry_, m_sub_entry_;
    uint            m_stack_size_;

protected:
    EventerList     m_fdset_;
    bool            m_reset_;
};

// 协程池
class ThreadPool
{
public:
    inline static void SetDefaultThreadNum(unsigned int num)
    {
        s_default_thread_num_ = num;
    }

    inline static void SetDefaultStackSize(unsigned int size)
    {
        s_default_stack_size_ = (size + 1) / (MEM_PAGE_SIZE * MEM_PAGE_SIZE);
    }

    bool InitialPool(int max_num);
    void DestroyPool(void);
    Thread* AllocThread(void);
    void FreeThread(Thread* thread);

public:
    static unsigned int s_default_thread_num_;
    static unsigned int s_default_stack_size_;

private:
    ThreadList      m_free_list_;
    int             m_total_num_;
    int             m_use_num_;
    int             m_max_num_;
};

MTHREAD_NAMESPACE_END

#endif