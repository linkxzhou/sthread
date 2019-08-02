/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _MT_CONNECTION_H_INCLUDED_
#define _MT_CONNECTION_H_INCLUDED_

#include "mt_utils.h"
#include "mt_heap_timer.h"
#include "mt_core.h"
#include "mt_buffer.h"

ST_NAMESPACE_BEGIN

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

class TcpLongIMtConnection;
class HeapTimer;

// udp的无session连接
class UdpIMtConnection : public IMtConnection
{
public:
    UdpIMtConnection()
    {
        m_osfd_ = -1;
        m_type_ = eUDP_CONN;
    }

    virtual ~UdpIMtConnection()
    {
        IMtConnection::Reset();
    }

    virtual int CreateSocket(int fd = -1);

    virtual int SendData();

    virtual int RecvData();
};

// tcp短连接
class TcpShortIMtConnection : public IMtConnection
{
public:
    TcpShortIMtConnection()
    {
        m_osfd_ = -1;
        m_type_ = eTCP_SHORT_CONN;
    }

    virtual ~TcpShortIMtConnection()
    {
        IMtConnection::Reset();
    }

    virtual int CreateSocket(int fd = -1);

    virtual int OpenConnect();

    virtual int SendData();

    virtual int RecvData();
};

// tcp accept连接
class TcpAcceptIMtConnection : public IMtConnection
{
public:
    TcpAcceptIMtConnection()
    {
        m_osfd_ = -1;
        m_type_ = eTCP_ACCEPT_CONN;
    }

    virtual ~TcpAcceptIMtConnection()
    {
        IMtConnection::Reset();
    }

    virtual int CreateSocket(int fd = -1);

    // 暂时不需要两个虚函数
    virtual int SendData()
    {
        return 0;
    }

    virtual int RecvData()
    {
        return 0;
    }
};

class TcpLongIMtConnection : public TcpShortIMtConnection, public TimerEntry
{
public:
    TcpLongIMtConnection() : m_timer_(NULL)
    {
        m_osfd_ = -1;
        m_keep_time_ = 10 * 60 * 1000;
        m_type_ = eTCP_LONG_CONN;
    }

    virtual ~TcpLongIMtConnection()
    {
        IMtConnection::Reset();
    }

    int CreateSocket(int fd = -1);

    bool IdleAttach();

    bool IdleDetach();

    virtual void Notify(eEventType type);

    // 设置时钟监听控制
    inline void SetHeapTimer(HeapTimer* timer)
    {
        m_timer_ = timer;
    }

    inline void SetKeepTimeout(int timeout)
    {
        m_keep_time_ = timeout;
    }

protected:
    int             m_keep_time_;
    HeapTimer*      m_timer_;
};

// 管理tcp长连接(单例)
class TcpLongPool
{
public:
    TcpLongPool() : m_keep_map_(NULL)
    {
        m_keep_map_ = new HashList<KeepKey>(10000);
    }

    ~TcpLongPool()
    {
        if (!m_keep_map_)
        {
            return;
        }

        HashKey* hash_item = m_keep_map_->HashGetFirst();
        while (hash_item)
        {
            if (hash_item != NULL)
            {
                safe_delete(hash_item);
            }
            hash_item = m_keep_map_->HashGetFirst();
        }
        safe_delete(m_keep_map_);
    }

    TcpLongIMtConnection* GetConnection(struct sockaddr_in* dst);

    bool CacheConnection(TcpLongIMtConnection* conn);

    bool RemoveConnection(TcpLongIMtConnection* conn);

    void FreeConnection(TcpLongIMtConnection* conn, bool force_free); // 是否强制释放

private:
    HashList<KeepKey>*                  m_keep_map_;
    UtilsPtrPool<TcpLongIMtConnection>  m_tcplong_pool_;
};

// 获取不同的连接方式: 
class ConnectionPool
{
public:
    typedef UtilsPtrPool<UdpIMtConnection>       UdpPool;
    typedef UtilsPtrPool<TcpShortIMtConnection>  TcpShortPool;

    IMtConnection* GetConnection(eConnType type, struct sockaddr_in* dst = NULL);

    void FreeConnection(IMtConnection* conn, bool force_free = false); // 是否强制释放

    // 处理tcp长连接
    void CloseIdleTcpLong(IMtConnection* conn);

private:
    UdpPool	        m_udp_pool_;
    TcpShortPool	m_tcpshort_pool_;
    TcpLongPool     m_tcplong_pool_;
};

ST_NAMESPACE_END

#endif