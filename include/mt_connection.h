/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _MT_CONNECTION_H_INCLUDED_
#define _MT_CONNECTION_H_INCLUDED_

#include "mt_utils.h"
#include "mt_heap_timer.h"
#include "mt_event_proxyer.h"
#include "mt_buffer.h"

MTHREAD_NAMESPACE_BEGIN

class TcpKeepConn;
class TimerCtrl;

// udp的无session连接
class UdpShortConn : public IMtConnection
{
public:
    UdpShortConn()
    {
        m_osfd_ = -1;
        m_type_ = eUDP_SHORT_CONN;
    }

    virtual ~UdpShortConn()
    {
        CloseSocket();
    }

    virtual void Reset();

    virtual int CreateSocket();

    virtual int SendData();

    virtual int RecvData();
};

// udp的session连接
class UdpSessionConn : public UdpShortConn
{
public:
    UdpSessionConn()
    {
        m_type_ = eUDP_SESSION_CONN;
    }

    virtual ~UdpSessionConn() 
    {
        CloseSocket(); 
    }

    virtual int CreateSocket();

    virtual int SendData();

    virtual int RecvData();
};

// tcp短连接
class TcpShortConn : public IMtConnection
{
public:
    TcpShortConn()
    {
        m_osfd_ = -1;
        m_type_ = eTCP_SHORT_CONN;
    }

    virtual ~TcpShortConn()
    {
        IMtConnection::CloseSocket();
    }

    virtual void Reset();

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

// tcp长链接
typedef CPP_TAILQ_ENTRY<TcpKeepConn> KeepConnLink;
typedef CPP_TAILQ_HEAD<TcpKeepConn> KeepConnList;

class TcpKeepConn : public TcpShortConn, public TimerNotify
{
public:
    TcpKeepConn() : m_timer_(NULL)
    {
        m_osfd_ = -1;
        m_keep_time_ = 10 * 60 * 1000;
        m_keep_flag_ = 0;
        m_type_ = eTCP_KEEP_CONN;
    }

    virtual ~TcpKeepConn()
    {
        IMtConnection::CloseSocket();
    }

    int CreateSocket();

    void ConnReuseClean();

    bool IdleAttach();

    bool IdleDetach();

    virtual void TimerNotify();

    void SetKeepTime(unsigned int time)
    {
        m_keep_time_ = time;
    }

    // 设置时钟监听控制
    void SetTimerCtrl(TimerCtrl* timer)
    {
        m_timer_ = timer;
    }

public:
    int             m_keep_flag_;
    KeepConnLink    m_entry_;

protected:
    unsigned int    m_keep_time_;
    TimerCtrl*      m_timer_;
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
    void InsertConn(TcpKeepConn* conn)
    {
        if (conn->m_keep_flag_ & eTCP_KEEP_IN_LIST)
        {
            return;
        }

        CPP_TAILQ_INSERT_TAIL(&m_list_, conn, m_entry_);
        conn->m_keep_flag_ |= eTCP_KEEP_IN_LIST;
    }
    void RemoveConn(TcpKeepConn* conn)
    {
        if (!(conn->m_keep_flag_ & eTCP_KEEP_IN_LIST))
        {
            return;
        }
        
        CPP_TAILQ_REMOVE(&m_list_, conn, m_entry_);
        conn->m_keep_flag_ &= ~eTCP_KEEP_IN_LIST;
    }
    TcpKeepConn* GetFirstConn()
    {
        return CPP_TAILQ_FIRST(&m_list_);
    }

private:
    uint32_t        m_addr_ipv4_;
    uint16_t        m_net_port_;
    KeepConnList    m_list_;
};

// 管理tcp长连接(单例)
class TcpKeepCtrl
{
public:
    typedef UtilsPtrPool<TcpKeepConn> TcpKeepQueue;

    TcpKeepCtrl();

    ~TcpKeepCtrl();

    TcpKeepConn* GetTcpKeepConn(struct sockaddr_in* dst);

    bool CacheTcpKeepConn(TcpKeepConn* conn);

    bool RemoveTcpKeepConn(TcpKeepConn* conn);

    void FreeTcpKeepConn(TcpKeepConn* conn, bool force_free); // 是否强制释放

private:
    HashList<TcpKeepKey>*   m_keep_map_;
    TcpKeepQueue            m_keep_queue_;
};

// connection 连接管理（单例模式）
// 处理四种不同的连接方式: 
// UdpShortQueue, UdpSessionQueue, TcpKeepCtrl, TcpShortQueue
class ConnectionCtrl
{
public:
    typedef UtilsPtrPool<UdpShortConn>   UdpShortQueue;
    typedef UtilsPtrPool<UdpSessionConn> UdpSessionQueue;
    typedef UtilsPtrPool<TcpShortConn>   TcpShortQueue;

    IMtConnection* GetConnection(eConnType type, struct sockaddr_in* dst = NULL);

    void FreeConnection(IMtConnection* conn, bool force_free); // 是否强制释放

    void CloseIdleTcpKeep(IMtConnection* conn);

private:
    UdpShortQueue	m_udp_short_queue_;
    UdpSessionQueue	m_udp_session_queue_;
    TcpShortQueue	m_tcp_short_queue_;
    TcpKeepCtrl     m_tcp_keep_;
};

MTHREAD_NAMESPACE_END

#endif
