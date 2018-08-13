/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _MT_CONNECTION_H_INCLUDED_
#define _MT_CONNECTION_H_INCLUDED_

#include "mt_utils.h"
#include "mt_heap_timer.h"
#include "mt_core.h"
#include "mt_buffer.h"

MTHREAD_NAMESPACE_BEGIN

class TcpKeepIMtConnection;
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
        IMtConnection::CloseSocket();
    }

    virtual void Reset()
    {
        IMtConnection::CloseSocket();
        IMtConnection::Reset();
    }

    virtual int CreateSocket();

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
        IMtConnection::CloseSocket();
    }

    virtual void Reset()
    {
        memset(&m_dst_addr_, 0, sizeof(m_dst_addr_));
        IMtConnection::CloseSocket();
        IMtConnection::Reset();
        IMtConnection::ResetEventer();
    }

    virtual int CreateSocket();

    virtual int OpenConnect();

    virtual int SendData();

    virtual int RecvData();

    inline void SetDestAddr(struct sockaddr_in* dst)
    {
        memcpy(&m_dst_addr_, dst, sizeof(m_dst_addr_));
    }

    inline struct sockaddr_in* GetDestAddr()
    {
        return &m_dst_addr_;
    }
};

// tcp accept连接
class TcpAcceptIMtConnection
{
public:
    TcpAcceptIMtConnection()
    {
        m_osfd_ = -1;
        m_type_ = eTCP_ACCEPT_CONN;
    }

    virtual ~TcpAcceptIMtConnection()
    {
        if (m_osfd_ > 0)
        {
            mt_close(m_osfd_);
            m_osfd_ = -1;
        }
    }

    virtual int CreateSocket();

    virtual int Accept();

    inline void SetDestAddr(struct sockaddr_in* dst)
    {
        memcpy(&m_dst_addr_, dst, sizeof(m_dst_addr_));
    }

    inline struct sockaddr_in* GetDestAddr()
    {
        return &m_dst_addr_;
    }

protected:
    eConnType           m_type_;
    IMtActionBase*      m_action_;
    IMtMsgBuffer*       m_msg_buff_;
    Eventer*            m_ev_;

    int                 m_osfd_;
    struct sockaddr_in  m_dst_addr_;
};

// tcp长链接
typedef CPP_TAILQ_ENTRY<TcpKeepIMtConnection> KeepConnLink;
typedef CPP_TAILQ_HEAD<TcpKeepIMtConnection> KeepConnList;

class TcpKeepIMtConnection : public TcpShortIMtConnection, public TimerEntry
{
public:
    TcpKeepIMtConnection() : m_timer_(NULL)
    {
        m_osfd_ = -1;
        m_keep_time_ = 10 * 60 * 1000;
        m_keep_flag_ = 0;
        m_type_ = eTCP_KEEP_CONN;
    }

    virtual ~TcpKeepIMtConnection()
    {
        IMtConnection::CloseSocket();
    }

    int CreateSocket();

    void ConnReuseClean()
    {
        IMtConnection::Reset();
    }

    bool IdleAttach();

    bool IdleDetach();

    virtual void Notify(eEventType type);

    void SetKeepTime(unsigned int time)
    {
        m_keep_time_ = time;
    }

    // 设置时钟监听控制
    void SetHeapTimer(HeapTimer* timer)
    {
        m_timer_ = timer;
    }

public:
    int             m_keep_flag_;
    KeepConnLink    m_entry_;

protected:
    unsigned int    m_keep_time_;
    HeapTimer*      m_timer_;
};

// 长链接标示的key
class TcpKeepKey : public HashKey
{
public:
    TcpKeepKey()
    {
        m_addr_ipv4_  = 0;
        m_net_port_   = 0;
        CPP_TAILQ_INIT(&m_list_);

        this->SetDataPtr(this);
    }
    TcpKeepKey(struct sockaddr_in * dst)
    {
        m_addr_ipv4_  = dst->sin_addr.s_addr;
        m_net_port_   = dst->sin_port;
        CPP_TAILQ_INIT(&m_list_);

        this->SetDataPtr(this);
    }
    ~TcpKeepKey()
    {
        CPP_TAILQ_INIT(&m_list_);
    }
    // 端口和ip组合
    virtual uint32_t HashValue()
    {
        return m_addr_ipv4_ ^ ((m_net_port_ << 16) | m_net_port_);
    }
    virtual int HashCmp(HashKey* rhs)
    {
        TcpKeepKey* data = dynamic_cast<TcpKeepKey*>(rhs);
        if (!data)
        {
            return -1;
        }

        if (m_addr_ipv4_ != data->m_addr_ipv4_)
        {
            return m_addr_ipv4_ - data->m_addr_ipv4_;
        }
        if (m_net_port_ != data->m_net_port_)
        {
            return this->m_net_port_ - data->m_net_port_;
        }
        
        return 0;
    }
    void InsertConn(TcpKeepIMtConnection *conn)
    {
        if (conn->m_keep_flag_ & eTCP_KEEP_IN_LIST)
        {
            return;
        }

        CPP_TAILQ_INSERT_TAIL(&m_list_, conn, m_entry_);
        conn->m_keep_flag_ |= eTCP_KEEP_IN_LIST;
    }
    void RemoveConn(TcpKeepIMtConnection* conn)
    {
        if (!(conn->m_keep_flag_ & eTCP_KEEP_IN_LIST))
        {
            return;
        }
        
        CPP_TAILQ_REMOVE(&m_list_, conn, m_entry_);
        conn->m_keep_flag_ &= ~eTCP_KEEP_IN_LIST;
    }
    TcpKeepIMtConnection* GetFirstConn()
    {
        return CPP_TAILQ_FIRST(&m_list_);
    }

private:
    uint32_t        m_addr_ipv4_;
    uint16_t        m_net_port_;
    KeepConnList    m_list_;
};

// 管理tcp长连接(单例)
class TcpKeepPool
{
public:
    typedef UtilsPtrPool<TcpKeepIMtConnection> TcpKeepQueue;

    TcpKeepPool() : m_keep_map_(NULL)
    {
        m_keep_map_ = new HashList<TcpKeepKey>(10000);
    }

    ~TcpKeepPool()
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

    TcpKeepIMtConnection* GetConnection(struct sockaddr_in* dst);

    bool CacheConnection(TcpKeepIMtConnection* conn);

    bool RemoveConnection(TcpKeepIMtConnection* conn);

    void FreeConnection(TcpKeepIMtConnection* conn, bool force_free); // 是否强制释放

private:
    HashList<TcpKeepKey>*   m_keep_map_;
    TcpKeepQueue            m_keep_queue_;
};

// 获取不同的连接方式: 
class ConnectionPool
{
public:
    typedef UtilsPtrPool<UdpIMtConnection>       UdpQueue;
    typedef UtilsPtrPool<TcpShortIMtConnection>  TcpShortQueue;

    IMtConnection* GetConnection(eConnType type, struct sockaddr_in* dst = NULL);

    void FreeConnection(IMtConnection* conn, bool force_free = false); // 是否强制释放

    void CloseIdleTcpKeep(IMtConnection* conn);

private:
    UdpQueue	    m_udp_queue_;
    TcpShortQueue	m_tcp_short_queue_;
    TcpKeepPool     m_tcp_keep_;
};

MTHREAD_NAMESPACE_END

#endif
