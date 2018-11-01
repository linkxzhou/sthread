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

MTHREAD_NAMESPACE_END

#endif