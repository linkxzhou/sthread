/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_ITEM_H_INCLUDED_
#define _ST_ITEM_H_INCLUDED_

#include "st_util.h"
#include "st_ucontext.h"

// 选择不同的头文件
#if defined(__APPLE__) || defined(__OpenBSD__)
#include "st_kqueue.h"
#else
#include "st_epoll.h"
#endif

#include "st_heap.h"
#include "st_buffer.h"

ST_NAMESPACE_BEGIN

class StEventItem;
class ThreadItem;

typedef CPP_TAILQ_HEAD<StEventItem>     StEventItemQueue;
typedef CPP_TAILQ_ENTRY<StEventItem>    StEventItemNext;
typedef CPP_TAILQ_HEAD<ThreadItem>      ThreadItemQueue;
typedef CPP_TAILQ_ENTRY<ThreadItem>     ThreadItemNext;

class StEventItem : public referenceable
{
public:
    explicit StEventItem(int32_t fd = -1) : 
        m_fd_(fd), 
        m_events_(0), 
        m_revents_(0), 
        m_type_(0), 
        m_thread_(NULL)
    { }

    virtual ~StEventItem()
    { }

    virtual int32_t InputNotify();

    virtual int32_t OutputNotify();

    virtual int32_t HangupNotify();

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

    inline void SetType(int32_t type)
    {
        m_type_ = type;
    }

    inline int32_t SetType() 
    { 
        return m_type_; 
    }

    inline void SetOwnerThread(ThreadItem* thread) 
    {
        m_thread_ = thread; 
    }

    inline ThreadItem* GetOwnerThread()
    { 
        return m_thread_; 
    }

    inline void Reset()
    {
        m_fd_      = -1;
        m_events_  = 0;
        m_revents_ = 0;
        m_type_    = 0;
        m_thread_  = NULL;
    }

protected:
    int32_t     m_fd_;      // 监听句柄
    int32_t     m_events_;  // 事件
    int32_t     m_revents_; // recv事件
    int32_t     m_type_;    // 通知类型
    ThreadItem* m_thread_;

public:
    StEventItemNext m_next_;
};

class ThreadItem : public HeapEntry
{
public:
    ThreadItem() : 
        m_wakeup_time_(0), 
        m_flag_(eNOT_INLIST),
        m_type_(eNORMAL), 
        m_state_(eINITIAL), 
        m_callback_(NULL),
        m_stack_(NULL),
        m_private_(NULL),
        m_parent_(NULL), 
        m_stack_size_(STACK)
    { 
        CPP_TAILQ_INIT(&m_fdset_);
        CPP_TAILQ_INIT(&m_sub_threadlist_);
    }

    inline void Reset()
    {
        m_wakeup_time_ = 0;
        m_flag_ = eNOT_INLIST;
        m_type_ = eNORMAL;
        m_state_ = eINITIAL;
        m_callback_ = NULL;
        st_safe_free(m_private_);

        CPP_TAILQ_INIT(&m_fdset_);
        CPP_TAILQ_INIT(&m_sub_threadlist_);
        m_parent_ = NULL;
    }

    inline void SetFlag(eThreadFlag flag)
    {
        m_flag_ = (eThreadFlag)(m_flag_ | flag);
    }

    inline void UnsetFlag(eThreadFlag flag)
    {
        m_flag_ = (eThreadFlag)(m_flag_ & ~flag);
    }

    inline bool HasFlag(eThreadFlag flag)
    {
        return m_flag_ & flag;
    }

    inline eThreadFlag GetFlag()
    {
        return m_flag_;
    }

    inline void SetType(eThreadType type)
    {
        m_type_ = type;
    }
    
    inline eThreadType GetType(void)
    {
        return m_type_;
    }

    inline void SetState(eThreadState state)
    {
        m_state_ = state;
    }

    inline eThreadState GetState(void)
    {
        return m_state_;
    }
    
    inline void SetCallback(Closure* callback)
    {
        m_callback_ = callback;
    }

    inline uint64_t GetWakeupTime(void)
    {
        return m_wakeup_time_;
    }

    inline void SetWakeupTime(uint64_t wakeup_time)
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

    inline void Clear()
    {
        CPP_TAILQ_INIT(&m_fdset_);
    }

    // 获取fdset的列表
    inline StEventItemQueue& GetFdSet(void)
    {
        return m_fdset_;
    }

    // 直接运行
    virtual void Run(void) = 0;

    virtual void RestoreContext(ThreadItem* thread) = 0;

    inline uint64_t GetStThreadid()
    {
        if (NULL == m_stack_)
        {
            return -1;
        }

        return m_stack_->m_id_;
    }

    virtual void Add(StEventItem* item)
    {
        FUNCTION_INTO();

        if (CPP_TAILQ_EMPTY(&m_fdset_))
        {
            CPP_TAILQ_INIT(&m_fdset_);
        }

        // TODO : 防止重复插入数据
        StEventItem* _item = NULL;
        CPP_TAILQ_FOREACH(_item, &m_fdset_, m_next_)
        {
            if (item == _item)
            {
                return ;
            }
        }

        CPP_TAILQ_INSERT_TAIL(&m_fdset_, item, m_next_);
    }

    virtual void Add(StEventItemQueue* fdset)
    {
        if (CPP_TAILQ_EMPTY(&m_fdset_))
        {
            CPP_TAILQ_INIT(&m_fdset_);
            LOG_TRACE("m_fdset_ is INIT");
        }

        CPP_TAILQ_CONCAT(&m_fdset_, fdset, m_next_);
    }

    inline ThreadItem* GetParent()
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

    inline void SetParent(ThreadItem* parent)
    {
        m_parent_ = parent;
    }

    inline void AddSubThread(ThreadItem* sub)
    {
        if (!sub->HasFlag(eSUB_LIST))
        {
            CPP_TAILQ_INSERT_TAIL(&m_sub_threadlist_, sub, m_sub_next_);
            sub->m_parent_ = this;
        }
        sub->SetFlag(eSUB_LIST);
    }

    inline void RemoveSubThread(ThreadItem* sub)
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

protected:
    eThreadState    m_state_;
    eThreadType     m_type_;
    eThreadFlag     m_flag_;
    uint64_t        m_wakeup_time_;
    Stack*          m_stack_;       // 堆栈信息
    void*           m_private_;
    Closure*        m_callback_;    // 启动函数

public:
    StEventItemQueue    m_fdset_;
    ThreadItemQueue     m_sub_threadlist_;    // 子线程
    ThreadItem*         m_parent_;            // 父线程
    ThreadItemNext      m_next_, m_sub_next_;
    uint32_t            m_stack_size_;
    char                m_name_[64];
};

ST_NAMESPACE_END

#endif