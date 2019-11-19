/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_POLL_H_
#define _ST_POLL_H_

#include "st_util.h"
#include "st_ucontext.h"

// 选择不同的头文件
#if defined(__APPLE__) || defined(__OpenBSD__)
#include "stlib/ucontext/st_kqueue.h"
#else
#include "stlib/ucontext/st_epoll.h"
#endif

#include "stlib/st_heap.h"
#include "stlib/st_buffer.h"
#include "stlib/st_netaddr.h"
#include "stlib/st_sys.h"

using namespace stlib;

namespace sthread 
{

class StEventItem;
class StThreadItem;

typedef CPP_TAILQ_HEAD<StEventItem>     StEventItemQueue;
typedef CPP_TAILQ_HEAD<StThreadItem>    StThreadItemQueue;
typedef CPP_TAILQ_ENTRY<StEventItem>    StEventItemNext;
typedef CPP_TAILQ_ENTRY<StThreadItem>   StThreadItemNext;

class StThreadItem : public StHeap
{
public:
    StThreadItem() : 
        StHeap(),
        m_wakeup_time_(0), 
        m_type_(eNORMAL), 
        m_state_(eINITIAL), 
        m_callback_(NULL),
        m_stack_(NULL),
        m_private_(NULL),
        m_parent_(NULL), 
        m_flag_(eNOT_INLIST),
        m_stack_size_(STACK)
    { 
        CPP_TAILQ_INIT(&m_fdset_);
        CPP_TAILQ_INIT(&m_sub_threadlist_);
    }

    virtual ~StThreadItem()
    {
        this->Reset();
    }

    virtual void Reset()
    {
        m_wakeup_time_ = 0;
        m_type_ = eNORMAL;
        m_state_ = eINITIAL;
        st_safe_delete(m_callback_);
        st_safe_free(m_private_);

        // TODO : 需要处理(fd全部需要从epoll移除，子线程挂载到daemon线程中)
        CPP_TAILQ_INIT(&m_fdset_);
        CPP_TAILQ_INIT(&m_sub_threadlist_);
        m_parent_ = NULL;

        CPP_TAILQ_REMOVE_SELF(this, m_next_);
    }

    inline void SetFlag(eThreadFlag flag)
    {
        m_flag_ = (eThreadState)(m_flag_ | flag);
    }

    inline void UnsetFlag(eThreadFlag flag)
    {
        m_flag_ = (eThreadFlag)(m_flag_ & ~flag);
    }

    inline bool HasFlag(eThreadFlag flag)
    {
        return (m_flag_ & flag);
    }

    inline void SetType(eThreadType type)
    {
        m_type_ = type;
    }
    
    inline eThreadType GetType()
    {
        return m_type_;
    }

    inline void SetState(eThreadState state)
    {
        m_state_ = state;
    }
    
    inline eThreadFlag GetState()
    {
        return m_state_;
    }
    
    inline void SetCallback(StClosure *callback)
    {
        m_callback_ = callback;
    }

    inline int64_t GetWakeupTime()
    {
        return m_wakeup_time_;
    }

    inline void SetWakeupTime(int64_t wakeup_time)
    {
        m_wakeup_time_ = wakeup_time;
    }

    inline int64_t HeapValue()
    {
        return m_wakeup_time_;
    }

    inline void SetPrivate(void *data)
    {
        m_private_ = data;
    }

    inline void* GetPrivate()
    {
        return m_private_;
    }

    inline Stack* GetStack()
    {
        return m_stack_;
    }

    inline void ResetFdSet()
    {
        CPP_TAILQ_INIT(&m_fdset_);
    }

    // 获取fdset的列表
    inline StEventItemQueue& GetFdSet(void)
    {
        return m_fdset_;
    }

    inline uint64_t GetStThreadid()
    {
        if (NULL == m_stack_)
        {
            return -1;
        }

        return m_stack_->m_id_;
    }

    // 直接运行
    virtual void Run(void) = 0;

    virtual void RestoreContext(StThreadItem *thread) = 0;

    virtual void Add(StEventItem *item)
    {
        if (CPP_TAILQ_EMPTY(&m_fdset_))
        {
            CPP_TAILQ_INIT(&m_fdset_);
        }

        CPP_TAILQ_INSERT_TAIL(&m_fdset_, item, m_next_);
    }

    virtual void Add(StEventItemQueue *fdset)
    {
        if (CPP_TAILQ_EMPTY(&m_fdset_))
        {
            CPP_TAILQ_INIT(&m_fdset_);
        }

        CPP_TAILQ_CONCAT(&m_fdset_, fdset, m_next_);
    }

    inline StThreadItem* GetParent()
    {
        return m_parent_;
    }

    // 判断类型
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

    inline void SetParent(StThreadItem *parent)
    {
        m_parent_ = parent;
    }

    inline void AddSubThread(StThreadItem *sub)
    {
        if (!sub->HasFlag(eSUB_LIST))
        {
            CPP_TAILQ_INSERT_TAIL(&m_sub_threadlist_, sub, m_sub_next_);
            sub->m_parent_ = this;
        }
        sub->SetFlag(eSUB_LIST);
    }

    inline void RemoveSubThread(StThreadItem *sub)
    {
        if (sub->HasFlag(eSUB_LIST))
        {
            CPP_TAILQ_REMOVE(&m_sub_threadlist_, sub, m_sub_next_);
            sub->m_parent_ = NULL;
        }
        sub->UnsetFlag(eSUB_LIST);
    }

    inline bool HasNoSubThread()
    {
        return CPP_TAILQ_EMPTY(&m_sub_threadlist_);
    }

    inline const char* GetName()
    {
        return m_name_;
    }

    inline void SetName(const char *name)
    {
        strncpy(m_name_, name, sizeof(m_name_) - 1);
    }

    StThreadItem* GetRootThread()
    {
	    eThreadType type = GetType();
        StThreadItem *thread = this;
	    StThreadItem *parent = thread;

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

protected:
    eThreadState    m_state_;
    eThreadType     m_type_;
    eThreadFlag     m_flag_;
    int64_t         m_wakeup_time_;
    Stack           *m_stack_;       // 堆栈信息
    void            *m_private_;
    StClosure       *m_callback_;    // 启动函数

public:
    StEventItemQueue    m_fdset_;
    StThreadItemQueue   m_sub_threadlist_;    // 子线程
    StThreadItem        *m_parent_;           // 父线程
    StThreadItemNext    m_next_, m_sub_next_;
    uint32_t            m_stack_size_;
    char                m_name_[64];
};

class StEventItem : public referenceable
{
public:
    explicit StEventItem(int32_t fd = -1) : 
        m_fd_(fd), 
        m_events_(0), 
        m_revents_(0), 
        m_thread_(NULL)
    { }
 
    virtual ~StEventItem()
    { 
        this->Reset();
    }

    virtual int32_t InputNotify()
    {
        LOG_TRACE("----------------------------------");
        LOG_TRACE("InputNotify, thread: %p", m_thread_);

        return 0;
    }

    virtual int32_t OutputNotify()
    {
        LOG_TRACE("----------------------------------");
        LOG_TRACE("OutputNotify, thread: %p", m_thread_);

        return 0;
    }

    virtual int32_t HangupNotify()
    {
        LOG_TRACE("----------------------------------");
        LOG_TRACE("HangupNotify, thread: %p", m_thread_);

        return 0;
    }

    inline void EnableInput() 
    { 
        m_events_ |= ST_READABLE; 
    }

    inline void EnableOutput() 
    {
        m_events_ |= ST_WRITEABLE; 
    }

    inline void DisableInput() 
    { 
        m_events_ &= ~ST_READABLE; 
    }

    inline void DisableOutput() 
    { 
        m_events_ &= ~ST_WRITEABLE; 
    }

    inline int32_t GetOsfd() 
    { 
        return m_fd_;
    }

    inline void SetOsfd(int32_t fd) 
    { 
        m_fd_ = fd; 
    }

    inline int32_t GetEvents() 
    { 
        return m_events_; 
    }

    inline void SetEvents(int32_t events)
    {
        m_events_ = events;
    }

    inline void SetRecvEvents(int32_t revents) 
    { 
        m_revents_ = revents; 
    }

    inline int32_t GetRecvEvents() 
    { 
        return m_revents_; 
    }

    inline void SetOwnerThread(StThreadItem *thread) 
    {
        m_thread_ = thread; 
    }

    inline StThreadItem* GetOwnerThread()
    {
        return m_thread_; 
    }

    virtual void Reset()
    {
        m_fd_      = -1;
        m_events_  = 0;
        m_revents_ = 0;
        m_type_    = eEVENT_UNDEF;
        m_thread_  = NULL;

        // 从队列中移除
        CPP_TAILQ_REMOVE_SELF(this, m_next_);
    }

protected:
    int32_t         m_fd_;      // 监听句柄
    int32_t         m_events_;  // 事件
    int32_t         m_revents_; // recv事件
    StThreadItem    *m_thread_;

public:
    StEventItemNext m_next_;
};

}

#endif