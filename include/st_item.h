/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_CORE_H_INCLUDED_
#define _ST_CORE_H_INCLUDED_

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

typedef CPP_TAILQ_HEAD<StEventItem> StEventItemQueue;

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
    CPP_TAILQ_ENTRY<StEventItem> m_next_;
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
        m_private_(NULL)
    { }

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
    
    eThreadType GetType(void)
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

    inline void SetWakeupTime(uint64_t waketime)
    {
        m_wakeup_time_ = waketime;
    }

    inline uint64_t HeapValue()
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

    // 直接运行
    virtual void Run(void) = 0;

    // IO等待完成切换为可运行
    virtual int32_t IOWaitToRunable() = 0;

    // 从IO等待中移除
    virtual void RemoveIOWait() = 0;

    // 插入IO等待
    virtual void InsertIOWait() = 0;

    // 插入可运行
    virtual void InsertRunable() = 0;

    // 让出CPU
    virtual int32_t SwitchContext() = 0;

    // 休眠
    virtual void Sleep() = 0;

    // 阻塞
    virtual void Pend() = 0;

    // 非阻塞
    virtual void Unpend() = 0;

    // 回收
    virtual void Reclaim() = 0;

    virtual void RestoreContext(ThreadItem* thread) = 0;

    virtual void Clear() = 0;

    virtual void Add(StEventItem* item) = 0;

    virtual void Add(StEventItemQueue* fdset) = 0;

    inline uint64_t GetStThreadid()
    {
        if (NULL == m_stack_)
        {
            return -1;
        }

        return m_stack_->m_id_;
    }

protected:
    eThreadState    m_state_;
    eThreadType     m_type_;
    eThreadFlag     m_flag_;
    uint64_t        m_wakeup_time_;
    Stack*          m_stack_;       // 堆栈信息
    void*           m_private_;
    Closure*        m_callback_;    // 启动函数
};

ST_NAMESPACE_END

#endif
