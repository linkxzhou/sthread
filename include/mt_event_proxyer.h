/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _MT_EVENT_PROXYER_H_INCLUDED_
#define _MT_EVENT_PROXYER_H_INCLUDED_

#include "mt_utils.h"
#include "mt_ucontext.h"

#define MT_OS_OSX 1

#if MT_OS_OSX
#include "mt_event_kqueue.h"
#elif MT_OS_LINUX
#include "mt_event_epoll.h"
#endif

#include "mt_heap.h"
#include "mt_buffer.h"
#include "mt_sys_hook.h"

MTHREAD_NAMESPACE_BEGIN

class Eventer;
class EventProxyer;
class ThreadBase;
class IMtActionBase;

typedef CPP_TAILQ_HEAD<Eventer> EventerList;

class Eventer
{
public:
    explicit Eventer(int fd = -1) : m_fd_(fd), m_events_(0), 
        m_revents_(0), m_type_(0), m_thread_(NULL), m_proxyer_(NULL)
    { }
    virtual ~Eventer()
    { }

    virtual int InputNotify();

    virtual int OutputNotify();

    virtual int HangupNotify();

    virtual int AddRef(void* args);

    virtual int DelRef(void* args);

    inline void EnableInput() 
    { 
        m_events_ |= MT_READABLE; 
    }
    void EnableOutput() 
    { 
        m_events_ |= MT_WRITABLE; 
    }
    void DisableInput() 
    { 
        m_events_ &= ~MT_READABLE; 
    }
    void DisableOutput() 
    { 
        m_events_ &= ~MT_WRITABLE; 
    }
    int GetOsfd() 
    { 
        return m_fd_; 
    }
    void SetOsfd(int fd) 
    { 
        m_fd_ = fd; 
    }
    int GetEvents() 
    { 
        return m_events_; 
    }
    void SetRecvEvents(int revents) 
    { 
        m_revents_ = revents; 
    }
    int GetRecvEvents() 
    { 
        return m_revents_; 
    }
    void SetEventerType(int type)
    {
        m_type_ = type;
    }
    int GetEventerType() 
    { 
        return m_type_; 
    }
    void SetOwnerThread(ThreadBase* thread) 
    { 
        LOG_TRACE("ev : %p, thread : %p", this, thread);
        m_thread_ = thread; 
    }
    ThreadBase* GetOwnerThread()
    { 
        return m_thread_; 
    }
    void SetOwnerProxyer(EventProxyer* proxyer)
    {
        m_proxyer_ = proxyer;
    }
    EventProxyer* GetOwnerProxyer()
    {
        return m_proxyer_;
    }
    // 虚函数
    virtual void Reset()
    {
        m_fd_      = -1;
        m_events_  = 0;
        m_revents_ = 0;
        m_type_    = 0;
        m_thread_  = NULL;
    }

protected:
    int m_fd_;      // 监听句柄
    int m_events_;  // 事件
    int m_revents_;
    int m_type_;    // notify的类型

    ThreadBase* m_thread_; // 当前依附的线程
    EventProxyer* m_proxyer_; // 当前依附的proxyer

public:
    CPP_TAILQ_ENTRY<Eventer> m_entry_;
};

// 句柄引用数据
class FdRef
{
public:
    FdRef() : m_wr_ref_(0), m_rd_ref_(0), m_events_(0), m_ev_(NULL)
    { }
    ~FdRef()
    { }
    inline void SetListenEvents(int events)
    {
        m_events_ = events;
    }
    inline int GetListenEvents()
    {
        return m_events_;
    }
    inline void SetEventer(Eventer *ev)
    {
        LOG_TRACE("ref : %p, ev : %p", this, ev);
        m_ev_ = ev;
    }
    inline Eventer* GetEventer()
    {
        return m_ev_;
    }
    inline void AttachEvents(int event)
    {
        (event & MT_READABLE) ? m_rd_ref_++ : NULL;
        (event & MT_WRITABLE) ? m_wr_ref_++ : NULL;
    };
    inline void DetachEvents(int event)
    {
        if (event & MT_READABLE)
        {
            (m_rd_ref_ > 0) ? m_rd_ref_-- : m_rd_ref_ = 0;
        }
        if (event & MT_WRITABLE)
        {
            (m_wr_ref_ > 0) ? m_wr_ref_-- : m_wr_ref_ = 0;
        }
    }
    inline int ReadRefCnt() 
    { 
        return m_rd_ref_; 
    }
    inline int WriteRefCnt() 
    { 
        return m_wr_ref_; 
    }

private:
    int m_wr_ref_;
    int m_rd_ref_;
    int m_events_;

    Eventer* m_ev_;
};

class EventProxyer
{
public:
    EventProxyer() : m_maxfd_(DEFAULT_MAX_FD_NUM), m_state_(new IMtApiState()), 
        m_fdrefs_(NULL), m_timeout_(0)
    { }
    virtual ~EventProxyer()
    { 
        // 清理数据
        safe_delete(m_state_);
        safe_delete_arr(m_fdrefs_);
    }
    int Init(int max_num);
    void Stop(void);
    inline int GetTimeout(void) 
    { 
        return m_timeout_;
    }
    inline void SetTimeout(int timeout_ms)
    {
        m_timeout_ = timeout_ms;
    }
    bool Schedule(ThreadBase* thread, EventerList* fdlist, 
        Eventer* ev, int timeout);

    void Close();
    bool AddList(EventerList& fdset);
    bool DelList(EventerList& fdset);
    bool AddNode(Eventer* ev);
    bool DelNode(Eventer* ev);

    void Dispatch(void);
    bool AddFd(int fd, int new_events);
    bool DelFd(int fd, int new_events);
    bool DelRef(int fd, int new_events, bool use_ref);
    inline FdRef* GetFdRef(int fd)
    {
        LOG_TRACE("m_fdrefs_ : %p", m_fdrefs_);
        return ((fd >= m_maxfd_) || (fd < 0)) ? (FdRef*)NULL : &m_fdrefs_[fd];
    }

    inline void SetEventer(int fd, Eventer* ev)
    {
        FdRef* ref = GetFdRef(fd);
        if (ref)
        {
            LOG_TRACE("ref : %p, ev : %p", ref, ev);
            ref->SetEventer(ev);
        }
    }

    inline Eventer* GetEventer(int fd)
    {
        FdRef* ref = GetFdRef(fd);
        if (ref)
        {
            return ref->GetEventer();
        }

        return NULL;
    }

protected:
    void DisposeEventerList(int ev_fdnum);

protected:
    int     m_maxfd_;
    IMtApiState*  m_state_;
    FdRef*  m_fdrefs_;
    int     m_timeout_;
};

// action的基类
class IMtActionBase
{
public:
    // 四个处理阶段
    virtual int DoEncode() = 0;

    virtual int DoInput() = 0;

    virtual int DoProcess() = 0;
    
    virtual int DoError() = 0;

    inline void SetMsgDstAddr(struct sockaddr_in* dst)
    {
        memcpy(&m_addr_, dst, sizeof(m_addr_));
    }
    inline struct sockaddr_in* GetMsgDstAddr()
    {
        return &m_addr_;
    }

protected:
    struct sockaddr_in  m_addr_;
};

// thread的基类
class ThreadBase : public HeapEntry
{
public:
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
    inline void SetRunFunction(ThreadRunFunction func, void* args)
    {
        m_runfunc_ = func;
        m_args_  = args;
    }
    void* GetThreadArgs()
    {
        return m_args_;
    }
    inline utime64_t GetWakeupTime(void)
    {
        return m_wakeup_time_;
    }
    inline void SetWakeupTime(utime64_t waketime)
    {
        m_wakeup_time_ = waketime;
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

    virtual int IoWaitToRunable() = 0;

    virtual void RemoveIoWait() = 0;

    virtual void InsertIoWait() = 0;

    virtual void InsertRunable() = 0;

    virtual void SwitchContext() = 0;

    virtual void RestoreContext(ThreadBase* switch_thread) = 0;

    virtual void ClearAllFd() = 0;

    virtual void AddFd(Eventer* ev) = 0;

    virtual void AddFdList(EventerList* fdset) = 0;

    virtual EventerList& GetFdSet() = 0;

protected:
    eThreadState m_state_;
    eThreadType m_type_;
    eThreadFlag m_flag_;
    utime64_t   m_wakeup_time_;
    Stack*      m_stack_; // 堆栈信息
    void*       m_private_;

    ThreadRunFunction m_runfunc_; // 启动函数
    void*       m_args_; // 参数
};

// connection的基类
class IMtConnection
{
public:
    IMtConnection() : m_type_(eCONN_UNDEF), m_action_(NULL), 
        m_osfd_(-1), m_msg_buff_(NULL), m_ev_(NULL)
    { }

    virtual ~IMtConnection()
    {
        if (m_msg_buff_)
        {
            GetInstance<IMsgBufferPool>()->FreeMsgBuffer(m_msg_buff_);
        }
    }

    virtual void Reset()
    {
        // 清空buffer
        if (m_msg_buff_)
        {
            GetInstance<IMsgBufferPool>()->FreeMsgBuffer(m_msg_buff_);
        }
        m_action_ = NULL;
        m_msg_buff_ = NULL;
    }

    virtual void ResetEventer();

    eConnType GetConnType()
    {
        return m_type_;
    }

    // attach的action
    void SetIMtActon(IMtActionBase* action)
    {
        m_action_ = action;
    }

    IMtActionBase* GetIMtActon()
    {
        return m_action_;
    }

    // 设置message的buffer
    void SetIMtMsgBuffer(IMtMsgBuffer* msg_buff)
    {
        m_msg_buff_ = msg_buff;
    }

    // 获取message的buffer
    IMtMsgBuffer* GetIMtMsgBuffer()
    {
        return m_msg_buff_;
    }

    void SetEventer(Eventer *ev)
    {
        m_ev_ = ev;
    }

    Eventer* GetEventer()
    {
        return m_ev_;
    }

    int CloseSocket()
    {
        if (m_osfd_ < 0)
        {
            return 0;
        }

        mt_close(m_osfd_);
        m_osfd_ = -1;

        return 0;
    }

    int GetOsfd()
    {
        return m_osfd_;
    }

    void SetMsgDstAddr(struct sockaddr_in* dst)
    {
        memcpy(&m_dst_addr_, dst, sizeof(m_dst_addr_));
    }

public:
    virtual int CreateSocket() = 0;

    virtual int SendData() = 0;

    virtual int RecvData() = 0;

    virtual int OpenConnect()
    {
        return 0;
    }
    
protected:
    eConnType           m_type_;
    IMtActionBase*      m_action_;
    IMtMsgBuffer*       m_msg_buff_;
    Eventer*            m_ev_;

    int                 m_osfd_;
    struct sockaddr_in  m_dst_addr_;
};

// session信息
class ISession : public HashKey
{
public:
    ISession() : m_session_id_(0), m_session_flag_(0) 
    { }
    // 虚析构函数
    virtual ~ISession()
    { }

public:
    void SetSessionId(int id)
    {
        m_session_id_ = id;
    }
    int GetSessionId()
    {
        return m_session_id_;
    }
    void SetSessionFlag(int flag)
    {
        m_session_flag_ = flag;
    }
    int GetSessionFlag()
    {
        return m_session_flag_;
    }
    virtual uint32_t HashValue()
    {
        return m_session_id_;
    }
    virtual int HashCmp(HashKey* rhs)
    {
        return m_session_id_ - (int)rhs->HashValue();
    }

protected:
    int  m_session_id_;
    int  m_session_flag_;
};

MTHREAD_NAMESPACE_END

#endif
