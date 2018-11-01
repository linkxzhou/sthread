/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _mt_core_H_INCLUDED_
#define _mt_core_H_INCLUDED_

#include "mt_utils.h"
#include "mt_ucontext.h"

// 选择不同的头文件
#if defined(__APPLE__) || defined(__OpenBSD__)
#include "mt_event_kqueue.h"
#else
#include "mt_event_epoll.h"
#endif

#include "mt_heap.h"
#include "mt_buffer.h"
#include "mt_sys_hook.h"

// 设置default句柄的大小
#define DEFAULT_MAX_FD_NUM  1000000

MTHREAD_NAMESPACE_BEGIN

class Eventer;
class EventDriver;
class IMtConnection;
class ThreadBase;
class IMtActionBase;

typedef CPP_TAILQ_HEAD<Eventer> EventerList;

class Eventer : public referenceable
{
public:
    explicit Eventer(int fd = -1) : m_fd_(fd), m_events_(0), 
        m_revents_(0), m_type_(0), m_thread_(NULL), m_driver_(NULL)
    { }
    virtual ~Eventer()
    { }

    virtual int InputNotify();

    virtual int OutputNotify();

    virtual int HangupNotify();

    inline void EnableInput() 
    { 
        m_events_ |= MT_READABLE; 
    }
    inline void EnableOutput() 
    {
        m_events_ |= MT_WRITEABLE; 
    }
    inline void DisableInput() 
    { 
        m_events_ &= ~MT_READABLE; 
    }
    inline void DisableOutput() 
    { 
        m_events_ &= ~MT_WRITEABLE; 
    }
    inline int GetOsfd() 
    { 
        return m_fd_;
    }
    inline void SetOsfd(int fd) 
    { 
        m_fd_ = fd; 
    }
    inline int GetEvents() 
    { 
        return m_events_; 
    }
    inline void SetEvents(int events)
    {
        m_events_ = events;
    }
    inline void SetRecvEvents(int revents) 
    { 
        m_revents_ = revents; 
    }
    inline int GetRecvEvents() 
    { 
        return m_revents_; 
    }
    inline void SetEventerType(int type)
    {
        m_type_ = type;
    }
    inline int GetEventerType() 
    { 
        return m_type_; 
    }
    inline void SetOwnerThread(ThreadBase* thread) 
    { 
        m_thread_ = thread; 
    }
    inline ThreadBase* GetOwnerThread()
    { 
        return m_thread_; 
    }
    inline void SetOwnerDriver(EventDriver* driver)
    {
        m_driver_ = driver;
    }
    inline EventDriver* GetOwnerDriver()
    {
        return m_driver_;
    }
    void Reset(); // 重置数据

protected:
    int m_fd_;      // 监听句柄
    int m_events_;  // 事件
    int m_revents_; // recv事件
    int m_type_;    // notify的类型

    ThreadBase* m_thread_; // 当前依附的线程
    EventDriver* m_driver_; // 当前依附的driver

public:
    CPP_TAILQ_ENTRY<Eventer> m_entry_;
};

class EventDriver
{
    typedef Eventer* EventerPtr;

public:
    EventDriver() : m_maxfd_(DEFAULT_MAX_FD_NUM), m_state_(new IMtApiState()), 
        m_ev_array_(NULL), m_timeout_(0)
    { }
    virtual ~EventDriver()
    { 
        // 清理数据
        safe_delete(m_state_);
        safe_delete_arr(m_ev_array_);
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
        Eventer* ev, time64_t timeout);

    void Close();
    bool AddList(EventerList& fdset);
    bool DelList(EventerList& fdset);
    bool AddEventer(Eventer* ev);
    bool DelEventer(Eventer* ev);

    void Dispatch(void);
    bool AddFd(int fd, int new_events);
    bool DelFd(int fd, int new_events);
    // 注意：use_ref = true 对其句柄的引用统一处理
    bool DelRef(int fd, int new_events, bool useref);

    inline bool IsValidFd(int fd)
    {
        return ((fd >= m_maxfd_) || (fd < 0)) ? false : true;
    }

    inline bool SetEventer(int fd, Eventer* ev)
    {
        if (unlikely(IsValidFd(fd)))
        {
            m_ev_array_[fd] = ev;
            return true;
        }

        return false;
    }

    inline Eventer* GetEventer(int fd)
    {
        if (unlikely(IsValidFd(fd)))
        {
            return m_ev_array_[fd];
        }

        return NULL;
    }

protected:
    void DisposeEventerList(int ev_fdnum);

protected:
    int             m_maxfd_;
    IMtApiState*    m_state_;
    EventerPtr*     m_ev_array_;
    int             m_timeout_;
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
    ThreadBase() : m_wakeup_time_(0), m_flag_(eNOT_INLIST),
        m_type_(eNORMAL), m_state_(eINITIAL), m_callback_(NULL),
        m_args_(NULL), m_stack_(NULL)
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
    inline void SetRunCallback(ThreadRunCallback func, void* args)
    {
        m_callback_ = func;
        m_args_  = args;
    }
    void* GetThreadArgs()
    {
        return m_args_;
    }
    inline time64_t GetWakeupTime(void)
    {
        return m_wakeup_time_;
    }
    inline void SetWakeupTime(time64_t waketime)
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

    virtual void Run(void) = 0;

    virtual int IoWaitToRunable() = 0;

    virtual void RemoveIoWait() = 0;

    virtual void InsertIoWait() = 0;

    virtual void InsertRunable() = 0;

    virtual int SwitchContext() = 0;

    virtual void RestoreContext(ThreadBase* switch_thread) = 0;

    virtual void ClearAllFd() = 0;

    virtual void AddFd(Eventer* ev) = 0;

    virtual void AddFdList(EventerList* fdset) = 0;

    virtual EventerList& GetFdSet() = 0;

    inline unsigned long GetMtThreadid()
    {
        if (NULL == m_stack_)
        {
            return -1;
        }

        return m_stack_->m_id_;
    }

protected:
    eThreadState m_state_;
    eThreadType m_type_;
    eThreadFlag m_flag_;
    time64_t    m_wakeup_time_;
    Stack*      m_stack_; // 堆栈信息
    void*       m_private_;

    ThreadRunCallback m_callback_; // 启动函数
    void*       m_args_; // 参数
};

typedef CPP_TAILQ_ENTRY<IMtConnection> KeepConnLink;
typedef CPP_TAILQ_HEAD<IMtConnection> KeepConnList;

class IMtConnection : public referenceable
{
public:
    IMtConnection() : m_type_(eUNDEF_CONN), m_action_(NULL), m_keep_flag_(0),
        m_osfd_(-1), m_msg_buff_(NULL), m_ev_(NULL), m_timeout_(mt_maxint)
    { }

    virtual ~IMtConnection()
    {
        if (m_msg_buff_)
        {
            GetInstance<IMsgBufferPool>()->FreeMsgBuffer(m_msg_buff_);
        }
    }

    inline eConnType GetConnType()
    {
        return m_type_;
    }

    // attach的action
    inline void SetIMtActon(IMtActionBase* action)
    {
        m_action_ = action;
    }

    inline IMtActionBase* GetIMtActon()
    {
        return m_action_;
    }

    // 设置message的buffer
    inline void SetIMtMsgBuffer(IMtMsgBuffer* msg_buff)
    {
        m_msg_buff_ = msg_buff;
    }

    // 获取message的buffer
    inline IMtMsgBuffer* GetIMtMsgBuffer()
    {
        return m_msg_buff_;
    }

    inline void SetEventer(Eventer *ev)
    {
        m_ev_ = ev;
    }

    inline Eventer* GetEventer()
    {
        return m_ev_;
    }

    inline int CloseSocket()
    {
        if (m_osfd_ > 0)
        {
            mt_close(m_osfd_);
            m_osfd_ = -1;
        }

        return 0;
    }

    inline int GetOsfd()
    {
        return m_osfd_;
    }

    inline void SetMsgDstAddr(struct sockaddr_in* dst)
    {
        memcpy(&m_dst_addr_, dst, sizeof(m_dst_addr_));
    }

    inline struct sockaddr_in* GetMsgDstAddr()
    {
        return &m_dst_addr_;
    }

    inline void SetTimeout(int timeout)
    {
        m_timeout_ = timeout;
    }

    inline int GetTimeout()
    {
        return m_timeout_;
    }

    inline void Reset()
    {
        // 清空buffer
        if (NULL != m_msg_buff_)
        {
            GetInstance<IMsgBufferPool>()->FreeMsgBuffer(m_msg_buff_);
        }
        m_action_ = NULL;
        m_msg_buff_ = NULL;
        m_keep_flag_ = 0;

        memset(&m_dst_addr_, 0, sizeof(m_dst_addr_));
        CloseSocket();
        ResetEventer();
    }

    void ResetEventer();

public:
    virtual int CreateSocket(int fd = -1) = 0;

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
    int                 m_timeout_;

public:
    KeepConnLink        m_entry_;
    int                 m_keep_flag_;
};

// 请求链接的标识
class KeepKey : public HashKey
{
public:
    KeepKey()
    {
        m_addr_ipv4_  = 0;
        m_addr_port_   = 0;
        CPP_TAILQ_INIT(&m_list_);

        this->SetDataPtr(this);
    }
    KeepKey(struct sockaddr_in * dst)
    {
        m_addr_ipv4_  = dst->sin_addr.s_addr;
        m_addr_port_  = dst->sin_port;
        CPP_TAILQ_INIT(&m_list_);

        this->SetDataPtr(this);
    }
    ~KeepKey()
    {
        CPP_TAILQ_INIT(&m_list_);
    }
    // 端口和ip组合
    virtual uint32_t HashValue()
    {
        return m_addr_ipv4_ ^ ((m_addr_port_ << 16) | m_addr_port_);
    }
    virtual int HashCmp(HashKey* rhs)
    {
        KeepKey* data = dynamic_cast<KeepKey*>(rhs);
        if (!data)
        {
            return -1;
        }

        if (m_addr_ipv4_ != data->m_addr_ipv4_)
        {
            return m_addr_ipv4_ - data->m_addr_ipv4_;
        }
        if (m_addr_port_ != data->m_addr_port_)
        {
            return m_addr_port_ - data->m_addr_port_;
        }
        
        return 0;
    }
    void InsertConnection(IMtConnection *conn)
    {
        if (conn->m_keep_flag_ & eKEEP_IN_LIST)
        {
            return;
        }
        CPP_TAILQ_INSERT_TAIL(&m_list_, conn, m_entry_);
        conn->m_keep_flag_ |= eKEEP_IN_LIST;
    }
    void RemoveConnection(IMtConnection* conn)
    {
        if (!(conn->m_keep_flag_ & eKEEP_IN_LIST))
        {
            return;
        }
        CPP_TAILQ_REMOVE(&m_list_, conn, m_entry_);
        conn->m_keep_flag_ &= ~eKEEP_IN_LIST;
    }
    IMtConnection* GetFirstConnection()
    {
        return CPP_TAILQ_FIRST(&m_list_);
    }

private:
    uint32_t        m_addr_ipv4_;
    uint16_t        m_addr_port_;
    KeepConnList    m_list_;
};

MTHREAD_NAMESPACE_END

#endif
