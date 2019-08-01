/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_THREAD_H_INCLUDED_
#define _ST_THREAD_H_INCLUDED_

#include "st_utils.h"
#include "st_heap_timer.h"
#include "st_item.h"

ST_NAMESPACE_BEGIN

#define STACK_PAD_SIZE      128
#define MEM_PAGE_SIZE       4096
#define DEFAULT_STACK_SIZE  512 * 1024
#define DEFAULT_THREAD_NUM  5

class Thread;

typedef CPP_TAILQ_ENTRY<Thread>    ThreadNext;
typedef CPP_TAILQ_HEAD<Thread>     ThreadQueue;

// class StPoller
// {
//     typedef StEventItem* StEventItemPtr;

// public:
//     StPoller() : m_maxfd_(1000000), 
//         m_state_(new StApiState()), 
//         m_container_(NULL), 
//         m_timeout_(0)
//     { }

//     virtual ~StPoller()
//     { 
//         // 清理数据
//         st_safe_delete(m_state_);
//         st_safe_delete_array(m_container_);
//     }

//     int32_t Create(int32_t max_num);

//     void Stop(void);

//     inline int32_t GetTimeout(void) 
//     { 
//         return m_timeout_;
//     }

//     inline void SetTimeout(int32_t timeout_ms)
//     {
//         m_timeout_ = timeout_ms;
//     }

//     bool Schedule(ThreadItem* thread, StEventItemQueue* fdset, uint64_t timeout);

//     bool Schedule(ThreadItem* thread, StEventItem* item, uint64_t timeout);

//     void Close();

//     bool Add(StEventItemQueue& fdset);

//     bool Del(StEventItemQueue& fdset);

//     bool Add(StEventItem* ev);

//     bool DelEventer(StEventItem* ev);

//     void Dispatch(void);
//     bool AddFd(int32_t fd, int32_t new_events);
//     bool DelFd(int32_t fd, int32_t new_events);
//     // 注意：use_ref = true 对其句柄的引用统一处理
//     bool DelRef(int32_t fd, int32_t new_events, bool useref);

//     inline bool IsValidFd(int32_t fd)
//     {
//         return ((fd >= m_maxfd_) || (fd < 0)) ? false : true;
//     }

//     inline bool SetEventer(int32_t fd, StEventItem* ev)
//     {
//         if (unlikely(IsValidFd(fd)))
//         {
//             m_ev_array_[fd] = ev;
//             return true;
//         }

//         return false;
//     }

//     inline StEventItem* GetEventer(int32_t fd)
//     {
//         if (unlikely(IsValidFd(fd)))
//         {
//             return m_ev_array_[fd];
//         }

//         return NULL;
//     }

// protected:
//     void DisposeEventerList(int32_t ev_fdnum);

// protected:
//     int32_t             m_maxfd_;
//     StApiState*         m_state_;
//     StEventItemPtr*     m_container_;
//     int32_t             m_timeout_;
// };

// // action的基类
// class IMtActionBase
// {
// public:
//     // 四个处理阶段
//     virtual int32_t DoEncode() = 0;

//     virtual int32_t DoInput() = 0;

//     virtual int32_t DoProcess() = 0;
    
//     virtual int32_t DoError() = 0;

//     inline void SetMsgDstAddr(struct sockaddr_in* dst)
//     {
//         memcpy(&m_addr_, dst, sizeof(m_addr_));
//     }
//     inline struct sockaddr_in* GetMsgDstAddr()
//     {
//         return &m_addr_;
//     }

// protected:
//     struct sockaddr_in  m_addr_;
// };

// 调度系统
// class ThreadScheduler
// {
// public:
//     ime64_t GetTime();
//     static void SwitchDaemonThread(Thread *thread);
//     static void SwitchPrimoThread(Thread *thread);
//     static int ThreadSchedule(); // 主动调度线程

//     static void Sleep(); // 休眠
//     static void Pend(); // 阻塞
//     static void Unpend(Thread* thread); // 非阻塞
//     static void Reclaim(); // 回收

//     static int IoWaitToRunable(Thread* thread); // 将状态从iowait转换到runable
//     static int RemoveIoWait(Thread* thread);
//     static int InsertIoWait(Thread* thread);
//     static int InsertRunable(Thread* thread);

//     static int RemoveEvents(int fd, int events); // 删除events
//     static int InsertEvents(int fd, int events); // 添加events
// };

class Thread : public ThreadItem
{
public:
    Thread() : 
        m_parent_(NULL), 
        m_stack_size_(STACK), 
        m_reset_(false)
    {
        CPP_TAILQ_INIT(&m_fdset_);
        CPP_TAILQ_INIT(&m_sub_threadlist_);
    }

    virtual ~Thread()
    { 
        FreeStack();
    }

    virtual void Run(void);

    inline bool Create(void)
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
        CPP_TAILQ_INIT(&m_sub_threadlist_);
        m_flag_ = eNOT_INLIST;
        m_type_ = eNORMAL;
        m_state_ = eINITIAL;
        m_callback_ = NULL;
        m_parent_ = NULL;
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

    virtual int IOWaitToRunable() 
    {
        return Scheduler::IoWaitToRunable(this);
    }

    virtual void RemoveIOWait()
    { 
        Scheduler::RemoveIoWait(this);
    }

    virtual void InsertIOWait()
    { 
        Scheduler::InsertIoWait(this);
    }

    virtual void InsertRunable()
    { 
        Scheduler::InsertRunable(this);
    }

    virtual void Clear(void)
    {
        CPP_TAILQ_INIT(&m_fdset_);
    }

    virtual void Add(StEventItem* item)
    {
        if (CPP_TAILQ_EMPTY(&m_fdset_))
        {
            CPP_TAILQ_INIT(&m_fdset_);
        }
        CPP_TAILQ_INSERT_TAIL(&m_fdset_, ev, m_entry_);
    }

    virtual void Add(StEventItemQueue* fdset)
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

    inline void SetParent(Thread* parent)
    {
        m_parent_ = parent;
    }

    inline void AddSubThread(Thread* sub)
    {
        if (!sub->HasFlag(eSUB_LIST))
        {
            CPP_TAILQ_INSERT_TAIL(&m_sub_threadlist_, sub, m_sub_entry_);
            sub->m_parent_ = this;
        }
        sub->SetFlag(eSUB_LIST);
    }

    inline void RemoveSubThread(Thread* sub)
    {
        if (sub->HasFlag(eSUB_LIST))
        {
            CPP_TAILQ_REMOVE(&m_sub_threadlist_, sub, m_sub_entry_);
            sub->m_parent_ = NULL;
        }
        sub->UnsetFlag(eSUB_LIST);
    }

    inline bool HasNoSubThread()
    {
        return CPP_TAILQ_EMPTY(&m_sub_threadlist_);
    }

    void WakeupParent();

protected:

    bool InitStack();

    void FreeStack();

    void InitContext();

public:
    ThreadQueue     m_sub_threadlist_;    // 子线程
    Thread*         m_parent_;            // 父线程
    ThreadNext      m_next_, m_sub_next_;
    uint32_t        m_stack_size_;

protected:
    StEventItemQueue    m_fdset_;
};

// // 协程池
// class ThreadPool
// {
// public:
//     inline static void SetDefaultThreadNum(unsigned int num)
//     {
//         s_default_thread_num_ = num;
//     }

//     inline static void SetDefaultStackSize(unsigned int size)
//     {
//         s_default_stack_size_ = (size + 1) / (MEM_PAGE_SIZE * MEM_PAGE_SIZE);
//     }

//     bool InitialPool(int max_num);
//     void DestroyPool(void);
//     Thread* AllocThread(void);
//     void FreeThread(Thread* thread);

// public:
//     static unsigned int s_default_thread_num_;
//     static unsigned int s_default_stack_size_;

// private:
//     ThreadList      m_free_list_;
//     int             m_total_num_;
//     int             m_use_num_;
//     int             m_max_num_;
// };

// typedef CPP_TAILQ_ENTRY<IMtConnection> KeepConnLink;
// typedef CPP_TAILQ_HEAD<IMtConnection> KeepConnList;

// class IMtConnection : public referenceable
// {
// public:
//     IMtConnection() : m_type_(eUNDEF_CONN), m_action_(NULL), m_keep_flag_(0),
//         m_osfd_(-1), m_msg_buff_(NULL), m_ev_(NULL), m_timeout_(mt_maxint)
//     { }

//     virtual ~IMtConnection()
//     {
//         if (m_msg_buff_)
//         {
//             GetInstance<IMsgBufferPool>()->FreeMsgBuffer(m_msg_buff_);
//         }
//     }

//     inline eConnType GetConnType()
//     {
//         return m_type_;
//     }

//     // attach的action
//     inline void SetIMtActon(IMtActionBase* action)
//     {
//         m_action_ = action;
//     }

//     inline IMtActionBase* GetIMtActon()
//     {
//         return m_action_;
//     }

//     // 设置message的buffer
//     inline void SetIMtMsgBuffer(IMtMsgBuffer* msg_buff)
//     {
//         m_msg_buff_ = msg_buff;
//     }

//     // 获取message的buffer
//     inline IMtMsgBuffer* GetIMtMsgBuffer()
//     {
//         return m_msg_buff_;
//     }

//     inline void SetEventer(StEventItem *ev)
//     {
//         m_ev_ = ev;
//     }

//     inline StEventItem* GetEventer()
//     {
//         return m_ev_;
//     }

//     inline int32_t CloseSocket()
//     {
//         if (m_osfd_ > 0)
//         {
//             mt_close(m_osfd_);
//             m_osfd_ = -1;
//         }

//         return 0;
//     }

//     inline int32_t GetOsfd()
//     {
//         return m_osfd_;
//     }

//     inline void SetMsgDstAddr(struct sockaddr_in* dst)
//     {
//         memcpy(&m_dst_addr_, dst, sizeof(m_dst_addr_));
//     }

//     inline struct sockaddr_in* GetMsgDstAddr()
//     {
//         return &m_dst_addr_;
//     }

//     inline void SetTimeout(int32_t timeout)
//     {
//         m_timeout_ = timeout;
//     }

//     inline int32_t GetTimeout()
//     {
//         return m_timeout_;
//     }

//     inline void Reset()
//     {
//         // 清空buffer
//         if (NULL != m_msg_buff_)
//         {
//             GetInstance<IMsgBufferPool>()->FreeMsgBuffer(m_msg_buff_);
//         }
//         m_action_ = NULL;
//         m_msg_buff_ = NULL;
//         m_keep_flag_ = 0;

//         memset(&m_dst_addr_, 0, sizeof(m_dst_addr_));
//         CloseSocket();
//         ResetEventer();
//     }

//     void ResetEventer();

// public:
//     virtual int32_t CreateSocket(int32_t fd = -1) = 0;

//     virtual int32_t SendData() = 0;

//     virtual int32_t RecvData() = 0;

//     virtual int32_t OpenConnect()
//     {
//         return 0;
//     }
    
// protected:
//     eConnType           m_type_;
//     IMtActionBase*      m_action_;
//     IMtMsgBuffer*       m_msg_buff_;
//     StEventItem*            m_ev_;

//     int32_t                 m_osfd_;
//     struct sockaddr_in  m_dst_addr_;
//     int32_t                 m_timeout_;

// public:
//     KeepConnLink        m_entry_;
//     int32_t                 m_keep_flag_;
// };

// // 请求链接的标识
// class KeepKey : public HashKey
// {
// public:
//     KeepKey()
//     {
//         m_addr_ipv4_  = 0;
//         m_addr_port_   = 0;
//         CPP_TAILQ_INIT(&m_list_);

//         this->SetDataPtr(this);
//     }
//     KeepKey(struct sockaddr_in * dst)
//     {
//         m_addr_ipv4_  = dst->sin_addr.s_addr;
//         m_addr_port_  = dst->sin_port;
//         CPP_TAILQ_INIT(&m_list_);

//         this->SetDataPtr(this);
//     }
//     ~KeepKey()
//     {
//         CPP_TAILQ_INIT(&m_list_);
//     }
//     // 端口和ip组合
//     virtual uint32_t HashValue()
//     {
//         return m_addr_ipv4_ ^ ((m_addr_port_ << 16) | m_addr_port_);
//     }
//     virtual int32_t HashCmp(HashKey* rhs)
//     {
//         KeepKey* data = dynamic_cast<KeepKey*>(rhs);
//         if (!data)
//         {
//             return -1;
//         }

//         if (m_addr_ipv4_ != data->m_addr_ipv4_)
//         {
//             return m_addr_ipv4_ - data->m_addr_ipv4_;
//         }
//         if (m_addr_port_ != data->m_addr_port_)
//         {
//             return m_addr_port_ - data->m_addr_port_;
//         }
        
//         return 0;
//     }
//     void InsertConnection(IMtConnection *conn)
//     {
//         if (conn->m_keep_flag_ & eKEEP_IN_LIST)
//         {
//             return;
//         }
//         CPP_TAILQ_INSERT_TAIL(&m_list_, conn, m_entry_);
//         conn->m_keep_flag_ |= eKEEP_IN_LIST;
//     }
//     void RemoveConnection(IMtConnection* conn)
//     {
//         if (!(conn->m_keep_flag_ & eKEEP_IN_LIST))
//         {
//             return ;
//         }
//         CPP_TAILQ_REMOVE(&m_list_, conn, m_entry_);
//         conn->m_keep_flag_ &= ~eKEEP_IN_LIST;
//     }
//     IMtConnection* GetFirstConnection()
//     {
//         return CPP_TAILQ_FIRST(&m_list_);
//     }

// private:
//     uint32_t        m_addr_ipv4_;
//     uint16_t        m_addr_port_;
//     KeepConnList    m_list_;
// };

ST_NAMESPACE_END

#endif